// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>

namespace brknam::audio {

struct ModelInfo {
  double expected_sample_rate_hz{-1.0};
  int latency_samples{};
  std::optional<double> loudness_db;
  std::optional<double> input_level_dbu;
  std::optional<double> output_level_dbu;
};

class MonoModel {
 public:
  virtual ~MonoModel() = default;

  // Non-realtime. The model must be fully allocated and warmed before it is
  // attached to a running processor.
  virtual void prepare(double sample_rate_hz,
                       std::size_t maximum_block_frames) = 0;

  // Realtime. Implementations must not allocate, lock, throw, or perform I/O.
  virtual void process(const float* input, float* output,
                       std::size_t frames) noexcept = 0;

  [[nodiscard]] virtual ModelInfo info() const noexcept = 0;
};

enum class OutputMode {
  raw,
  normalized,
};

enum class ProcessStatus {
  ok,
  not_prepared,
  invalid_channel_count,
  invalid_buffer,
  block_too_large,
};

class OneSlotProcessor final {
 public:
  static constexpr std::size_t kMaximumExternalChannels = 2;
  static constexpr double kDcBlockerCutoffHz = 5.0;
  static constexpr double kNormalizationTargetDb = -18.0;

  OneSlotProcessor();
  ~OneSlotProcessor();

  OneSlotProcessor(OneSlotProcessor&&) noexcept;
  OneSlotProcessor& operator=(OneSlotProcessor&&) noexcept;

  OneSlotProcessor(const OneSlotProcessor&) = delete;
  OneSlotProcessor& operator=(const OneSlotProcessor&) = delete;

  // Non-realtime. May allocate and resets all filter/model state.
  void prepare(double sample_rate_hz, std::size_t maximum_block_frames);

  // Non-realtime contract for the first E2 slice. The caller owns the model and
  // must keep it alive until detach_model() returns. A later E2 slice replaces
  // this stop-the-world attachment with prepared graph publication/crossfade.
  void attach_prepared_model(MonoModel* model) noexcept;
  void detach_model() noexcept;

  void set_input_trim_db(float value) noexcept;
  void set_output_trim_db(float value) noexcept;
  void set_model_bypassed(bool value) noexcept;
  void set_output_mode(OutputMode value) noexcept;

  [[nodiscard]] float input_trim_db() const noexcept;
  [[nodiscard]] float output_trim_db() const noexcept;
  [[nodiscard]] bool model_bypassed() const noexcept;
  [[nodiscard]] OutputMode output_mode() const noexcept;
  [[nodiscard]] int latency_samples() const noexcept;
  [[nodiscard]] ModelInfo model_info() const noexcept;

  // Realtime. External input is folded to one mono stream by arithmetic mean;
  // the processed mono stream is broadcast to all output channels.
  [[nodiscard]] ProcessStatus process(const float* const* inputs,
                                      std::size_t input_channels,
                                      float* const* outputs,
                                      std::size_t output_channels,
                                      std::size_t frames) noexcept;

  void reset() noexcept;

 private:
  struct Impl;
  Impl* impl_{};
};

[[nodiscard]] const char* to_string(ProcessStatus status) noexcept;

}  // namespace brknam::audio
