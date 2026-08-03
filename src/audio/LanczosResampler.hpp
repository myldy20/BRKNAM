// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.
//
// This is a BRKNAM-specific mono adaptation of the Lanczos resampler in
// AudioDSPTools by Steven Atkinson, pinned in cmake/DependencyPins.cmake.
// The upstream file is derived from iPlug2 and sst-basic-blocks and explicitly
// permits use in MIT/BSD as well as GPL projects. This adaptation removes the
// iPlug/WDL dependencies, uses fixed-width phase counters, and exposes only the
// allocation-free operations required by BRKNAM.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace brknam::audio::detail {

class MonoLanczosResampler final {
 public:
  static constexpr std::size_t kRadius = 12;
  static constexpr std::size_t kMaximumPushFrames = 131072;

  MonoLanczosResampler(const double input_sample_rate_hz,
                       const double output_sample_rate_hz)
      : input_sample_rate_hz_(validate_rate(input_sample_rate_hz)),
        output_sample_rate_hz_(validate_rate(output_sample_rate_hz)) {
    initialize_tables();
    set_phases();
    clear();
  }

  MonoLanczosResampler(const MonoLanczosResampler&) = delete;
  MonoLanczosResampler& operator=(const MonoLanczosResampler&) = delete;

  [[nodiscard]] std::size_t input_frames_required_for(
      const std::size_t output_frames) const noexcept {
    const auto output_advance =
        phase_output_increment_ * static_cast<std::int64_t>(output_frames);
    const auto distance = static_cast<double>(
                              phase_input_ - phase_output_ - output_advance) /
                          static_cast<double>(phase_denominator_);
    const auto required = static_cast<double>(kRadius) + 1.0 - distance;
    if (required <= 0.0) {
      return 0;
    }
    return static_cast<std::size_t>(required + 1.0);
  }

  void push(const float* input, const std::size_t frames) noexcept {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const auto value = input[frame];
      input_buffer_[write_position_] = value;
      input_buffer_[write_position_ + kBufferSize] = value;
      write_position_ = (write_position_ + 1) & (kBufferSize - 1);
      phase_input_ += phase_input_increment_;
    }
  }

  [[nodiscard]] std::size_t pop(float* output,
                                const std::size_t maximum_frames) noexcept {
    std::size_t populated = 0;
    const auto minimum_distance =
        phase_denominator_ * static_cast<std::int64_t>(kRadius + 1);
    while (populated < maximum_frames &&
           (phase_input_ - phase_output_) > minimum_distance) {
      const auto distance =
          static_cast<double>(phase_input_ - phase_output_) /
          static_cast<double>(phase_denominator_);
      output[populated] = read_sample(distance);
      phase_output_ += phase_output_increment_;
      ++populated;
    }
    return populated;
  }

  void renormalize_phases() noexcept {
    phase_input_ -= phase_output_;
    phase_output_ = 0;
  }

  void clear() noexcept {
    input_buffer_.fill(0.0F);
    write_position_ = 0;
    phase_input_ = 0;
    phase_output_ = 0;
  }

 private:
  static constexpr std::size_t kBufferSize = 131072;
  static constexpr std::size_t kFilterWidth = kRadius * 2;
  static constexpr std::size_t kTablePoints = 8192;
  static constexpr double kTableStep = 1.0 / kTablePoints;

  using FilterRow = std::array<float, kFilterWidth>;
  using FilterTable = std::array<FilterRow, kTablePoints + 1>;

  static double validate_rate(const double sample_rate_hz) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        sample_rate_hz >
            static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
        std::abs(sample_rate_hz - std::round(sample_rate_hz)) > 0.01) {
      throw std::invalid_argument(
          "Lanczos resampler requires a positive integer-like sample rate");
    }
    return sample_rate_hz;
  }

  static void initialize_tables() {
    std::call_once(table_once_, [] {
      const auto kernel = [](const double x) {
        if (std::abs(x) < 1.0e-12) {
          return 1.0;
        }
        const auto pi_x = std::numbers::pi * x;
        return static_cast<double>(kRadius) * std::sin(pi_x) *
               std::sin(pi_x / static_cast<double>(kRadius)) /
               (pi_x * pi_x);
      };

      for (std::size_t table_index = 0; table_index <= kTablePoints;
           ++table_index) {
        const auto fractional = kTableStep *
                                static_cast<double>(table_index);
        for (std::size_t tap = 0; tap < kFilterWidth; ++tap) {
          const auto x = fractional + static_cast<double>(tap) -
                         static_cast<double>(kRadius);
          table_[table_index][tap] = static_cast<float>(kernel(x));
        }
      }

      for (std::size_t table_index = 0; table_index < kTablePoints;
           ++table_index) {
        for (std::size_t tap = 0; tap < kFilterWidth; ++tap) {
          delta_table_[table_index][tap] =
              table_[table_index + 1][tap] - table_[table_index][tap];
        }
      }
      delta_table_[kTablePoints] = delta_table_[0];
    });
  }

  void set_phases() noexcept {
    const auto input_rate =
        static_cast<std::int64_t>(std::llround(input_sample_rate_hz_));
    const auto output_rate =
        static_cast<std::int64_t>(std::llround(output_sample_rate_hz_));
    const auto divisor = std::gcd(input_rate, output_rate);
    phase_input_increment_ = output_rate / divisor;
    phase_denominator_ = phase_input_increment_;
    phase_output_increment_ = input_rate / divisor;
  }

  [[nodiscard]] float read_sample(const double distance_back) const noexcept {
    const auto buffer_position =
        static_cast<double>(write_position_) - distance_back;
    auto buffer_index = static_cast<std::int64_t>(std::floor(buffer_position));
    const auto fractional =
        1.0 - (buffer_position - static_cast<double>(buffer_index));

    buffer_index =
        (buffer_index + static_cast<std::int64_t>(kBufferSize)) &
        static_cast<std::int64_t>(kBufferSize - 1);
    if (buffer_index <= static_cast<std::int64_t>(kRadius)) {
      buffer_index += static_cast<std::int64_t>(kBufferSize);
    }

    const auto table_position = fractional * kTablePoints;
    const auto table_index = std::min(
        static_cast<std::size_t>(table_position), kTablePoints);
    const auto table_fraction =
        table_position - static_cast<double>(table_index);

    double sum = 0.0;
    for (std::size_t tap = 0; tap < kRadius; ++tap) {
      const auto coefficient_before =
          static_cast<double>(table_[table_index][tap]) +
          static_cast<double>(delta_table_[table_index][tap]) *
              table_fraction;
      const auto second_tap = kRadius + tap;
      const auto coefficient_after =
          static_cast<double>(table_[table_index][second_tap]) +
          static_cast<double>(delta_table_[table_index][second_tap]) *
              table_fraction;

      const auto before_index = buffer_index -
                                static_cast<std::int64_t>(kRadius) +
                                static_cast<std::int64_t>(tap);
      const auto after_index =
          buffer_index + static_cast<std::int64_t>(tap);
      sum += coefficient_before *
                 static_cast<double>(input_buffer_[
                     static_cast<std::size_t>(before_index)]) +
             coefficient_after *
                 static_cast<double>(input_buffer_[
                     static_cast<std::size_t>(after_index)]);
    }
    return static_cast<float>(sum);
  }

  inline static FilterTable table_{};
  inline static FilterTable delta_table_{};
  inline static std::once_flag table_once_;

  std::array<float, kBufferSize * 2> input_buffer_{};
  double input_sample_rate_hz_{};
  double output_sample_rate_hz_{};
  std::size_t write_position_{};
  std::int64_t phase_input_{};
  std::int64_t phase_output_{};
  std::int64_t phase_input_increment_{1};
  std::int64_t phase_output_increment_{1};
  std::int64_t phase_denominator_{1};
};

}  // namespace brknam::audio::detail
