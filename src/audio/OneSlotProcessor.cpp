// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/OneSlotProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace brknam::audio {
namespace {

[[nodiscard]] float db_to_gain(const float db) noexcept {
  return std::pow(10.0F, db / 20.0F);
}

[[nodiscard]] float normalized_gain_db(const MonoModel* model,
                                       const OutputMode mode) noexcept {
  if (model == nullptr || mode != OutputMode::normalized) {
    return 0.0F;
  }
  const auto info = model->info();
  if (!info.loudness_db.has_value() || !std::isfinite(*info.loudness_db)) {
    return 0.0F;
  }
  return static_cast<float>(OneSlotProcessor::kNormalizationTargetDb -
                            *info.loudness_db);
}

void clear_outputs(float* const* outputs, const std::size_t output_channels,
                   const std::size_t frames) noexcept {
  if (outputs == nullptr) {
    return;
  }
  for (std::size_t channel = 0; channel < output_channels; ++channel) {
    if (outputs[channel] != nullptr) {
      std::fill_n(outputs[channel], frames, 0.0F);
    }
  }
}

}  // namespace

struct OneSlotProcessor::Impl {
  std::vector<float> mono_input;
  std::vector<float> mono_output;
  MonoModel* model{};
  std::atomic<float> input_trim_db{0.0F};
  std::atomic<float> output_trim_db{0.0F};
  std::atomic<bool> model_bypassed{false};
  std::atomic<OutputMode> output_mode{OutputMode::raw};
  double sample_rate_hz{};
  std::size_t maximum_block_frames{};
  float dc_coefficient{};
  float previous_dc_input{};
  float previous_dc_output{};
  bool prepared{false};
};

OneSlotProcessor::OneSlotProcessor() : impl_(new Impl) {}
OneSlotProcessor::~OneSlotProcessor() { delete impl_; }

OneSlotProcessor::OneSlotProcessor(OneSlotProcessor&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}

OneSlotProcessor& OneSlotProcessor::operator=(OneSlotProcessor&& other) noexcept {
  if (this != &other) {
    delete impl_;
    impl_ = std::exchange(other.impl_, nullptr);
  }
  return *this;
}

void OneSlotProcessor::prepare(const double sample_rate_hz,
                               const std::size_t maximum_block_frames) {
  if (impl_ == nullptr) {
    throw std::logic_error("Cannot prepare a moved-from processor");
  }
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument("Sample rate must be finite and positive");
  }
  if (maximum_block_frames == 0) {
    throw std::invalid_argument("Maximum block size must be positive");
  }

  impl_->mono_input.assign(maximum_block_frames, 0.0F);
  impl_->mono_output.assign(maximum_block_frames, 0.0F);
  impl_->sample_rate_hz = sample_rate_hz;
  impl_->maximum_block_frames = maximum_block_frames;
  impl_->dc_coefficient = static_cast<float>(
      std::exp(-2.0 * std::numbers::pi * kDcBlockerCutoffHz / sample_rate_hz));
  impl_->prepared = true;
  reset();

  if (impl_->model != nullptr) {
    impl_->model->prepare(sample_rate_hz, maximum_block_frames);
  }
}

void OneSlotProcessor::attach_prepared_model(MonoModel* const model) noexcept {
  if (impl_ != nullptr) {
    impl_->model = model;
  }
}

void OneSlotProcessor::detach_model() noexcept {
  attach_prepared_model(nullptr);
}

void OneSlotProcessor::set_input_trim_db(const float value) noexcept {
  if (impl_ != nullptr && std::isfinite(value)) {
    impl_->input_trim_db.store(value, std::memory_order_relaxed);
  }
}

void OneSlotProcessor::set_output_trim_db(const float value) noexcept {
  if (impl_ != nullptr && std::isfinite(value)) {
    impl_->output_trim_db.store(value, std::memory_order_relaxed);
  }
}

void OneSlotProcessor::set_model_bypassed(const bool value) noexcept {
  if (impl_ != nullptr) {
    impl_->model_bypassed.store(value, std::memory_order_relaxed);
  }
}

void OneSlotProcessor::set_output_mode(const OutputMode value) noexcept {
  if (impl_ != nullptr) {
    impl_->output_mode.store(value, std::memory_order_relaxed);
  }
}

float OneSlotProcessor::input_trim_db() const noexcept {
  return impl_ != nullptr
             ? impl_->input_trim_db.load(std::memory_order_relaxed)
             : 0.0F;
}

float OneSlotProcessor::output_trim_db() const noexcept {
  return impl_ != nullptr
             ? impl_->output_trim_db.load(std::memory_order_relaxed)
             : 0.0F;
}

bool OneSlotProcessor::model_bypassed() const noexcept {
  return impl_ != nullptr &&
         impl_->model_bypassed.load(std::memory_order_relaxed);
}

OutputMode OneSlotProcessor::output_mode() const noexcept {
  return impl_ != nullptr
             ? impl_->output_mode.load(std::memory_order_relaxed)
             : OutputMode::raw;
}

int OneSlotProcessor::latency_samples() const noexcept {
  return impl_ != nullptr && impl_->model != nullptr && !model_bypassed()
             ? std::max(0, impl_->model->info().latency_samples)
             : 0;
}

ModelInfo OneSlotProcessor::model_info() const noexcept {
  return impl_ != nullptr && impl_->model != nullptr ? impl_->model->info()
                                                     : ModelInfo{};
}

ProcessStatus OneSlotProcessor::process(
    const float* const* inputs, const std::size_t input_channels,
    float* const* outputs, const std::size_t output_channels,
    const std::size_t frames) noexcept {
  if (impl_ == nullptr || !impl_->prepared) {
    clear_outputs(outputs, output_channels, frames);
    return ProcessStatus::not_prepared;
  }
  if (input_channels == 0 || input_channels > kMaximumExternalChannels ||
      output_channels == 0 || output_channels > kMaximumExternalChannels) {
    clear_outputs(outputs, output_channels, frames);
    return ProcessStatus::invalid_channel_count;
  }
  if (inputs == nullptr || outputs == nullptr) {
    clear_outputs(outputs, output_channels, frames);
    return ProcessStatus::invalid_buffer;
  }
  for (std::size_t channel = 0; channel < input_channels; ++channel) {
    if (inputs[channel] == nullptr) {
      clear_outputs(outputs, output_channels, frames);
      return ProcessStatus::invalid_buffer;
    }
  }
  for (std::size_t channel = 0; channel < output_channels; ++channel) {
    if (outputs[channel] == nullptr) {
      clear_outputs(outputs, output_channels, frames);
      return ProcessStatus::invalid_buffer;
    }
  }
  if (frames > impl_->maximum_block_frames) {
    clear_outputs(outputs, output_channels, frames);
    return ProcessStatus::block_too_large;
  }
  if (frames == 0) {
    return ProcessStatus::ok;
  }

  const auto input_gain =
      db_to_gain(impl_->input_trim_db.load(std::memory_order_relaxed));
  const auto channel_gain = input_gain / static_cast<float>(input_channels);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float value = 0.0F;
    for (std::size_t channel = 0; channel < input_channels; ++channel) {
      value += inputs[channel][frame];
    }
    impl_->mono_input[frame] = value * channel_gain;
  }

  const auto bypassed =
      impl_->model_bypassed.load(std::memory_order_relaxed);
  if (impl_->model != nullptr && !bypassed) {
    impl_->model->process(impl_->mono_input.data(), impl_->mono_output.data(),
                          frames);
  } else {
    std::copy_n(impl_->mono_input.data(), frames, impl_->mono_output.data());
  }

  // NAM captures can produce a DC component; mirror the official plugin's
  // post-model 5 Hz high-pass placement rather than filtering the dry input.
  auto previous_input = impl_->previous_dc_input;
  auto previous_output = impl_->previous_dc_output;
  const auto coefficient = impl_->dc_coefficient;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto current_input = impl_->mono_output[frame];
    const auto current_output =
        current_input - previous_input + coefficient * previous_output;
    impl_->mono_output[frame] = current_output;
    previous_input = current_input;
    previous_output = current_output;
  }
  impl_->previous_dc_input = previous_input;
  impl_->previous_dc_output = previous_output;

  const auto mode = impl_->output_mode.load(std::memory_order_relaxed);
  const auto total_output_db =
      impl_->output_trim_db.load(std::memory_order_relaxed) +
      normalized_gain_db(impl_->model, mode);
  const auto output_gain = db_to_gain(total_output_db);
  for (std::size_t channel = 0; channel < output_channels; ++channel) {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      outputs[channel][frame] = impl_->mono_output[frame] * output_gain;
    }
  }
  return ProcessStatus::ok;
}

void OneSlotProcessor::reset() noexcept {
  if (impl_ == nullptr) {
    return;
  }
  impl_->previous_dc_input = 0.0F;
  impl_->previous_dc_output = 0.0F;
  std::fill(impl_->mono_input.begin(), impl_->mono_input.end(), 0.0F);
  std::fill(impl_->mono_output.begin(), impl_->mono_output.end(), 0.0F);
}

const char* to_string(const ProcessStatus status) noexcept {
  switch (status) {
    case ProcessStatus::ok:
      return "ok";
    case ProcessStatus::not_prepared:
      return "not_prepared";
    case ProcessStatus::invalid_channel_count:
      return "invalid_channel_count";
    case ProcessStatus::invalid_buffer:
      return "invalid_buffer";
    case ProcessStatus::block_too_large:
      return "block_too_large";
  }
  return "unknown";
}

}  // namespace brknam::audio
