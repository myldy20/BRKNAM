// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/OneSlotProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(const float actual, const float expected,
                  const float tolerance, const std::string& message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                             " expected=" + std::to_string(expected));
  }
}

class GainModel final : public brknam::audio::MonoModel {
 public:
  explicit GainModel(const float gain, const double loudness_db = -18.0)
      : gain_(gain), loudness_db_(loudness_db) {}

  void prepare(const double sample_rate_hz,
               const std::size_t maximum_block_frames) override {
    sample_rate_hz_ = sample_rate_hz;
    maximum_block_frames_ = maximum_block_frames;
  }

  void process(const float* input, float* output,
               const std::size_t frames) noexcept override {
    ++process_calls_;
    for (std::size_t frame = 0; frame < frames; ++frame) {
      output[frame] = input[frame] * gain_;
    }
  }

  [[nodiscard]] brknam::audio::ModelInfo info() const noexcept override {
    brknam::audio::ModelInfo result;
    result.expected_sample_rate_hz = 48000.0;
    result.latency_samples = 17;
    result.loudness_db = loudness_db_;
    return result;
  }

  float gain_{};
  double loudness_db_{};
  double sample_rate_hz_{};
  std::size_t maximum_block_frames_{};
  int process_calls_{};
};

void test_requires_prepare_and_bounds_blocks() {
  brknam::audio::OneSlotProcessor processor;
  std::array<float, 4> input{1.0F, 2.0F, 3.0F, 4.0F};
  std::array<float, 4> output{9.0F, 9.0F, 9.0F, 9.0F};
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};

  require(processor.process(inputs, 1, outputs, 1, input.size()) ==
              brknam::audio::ProcessStatus::not_prepared,
          "unprepared processor must reject audio");
  require(std::all_of(output.begin(), output.end(),
                      [](const float value) { return value == 0.0F; }),
          "rejected audio must be cleared");

  processor.prepare(48000.0, 2);
  std::fill(output.begin(), output.end(), 9.0F);
  require(processor.process(inputs, 1, outputs, 1, input.size()) ==
              brknam::audio::ProcessStatus::block_too_large,
          "oversized block must be rejected");
  require(std::all_of(output.begin(), output.end(),
                      [](const float value) { return value == 0.0F; }),
          "oversized output must be cleared");
}

void test_stereo_fold_down_model_and_broadcast() {
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(48000.0, 4);
  GainModel model(2.0F);
  model.prepare(48000.0, 4);
  processor.attach_prepared_model(&model);

  std::array<float, 4> left{1.0F, 0.0F, -1.0F, 0.5F};
  std::array<float, 4> right{3.0F, 2.0F, 1.0F, -0.5F};
  std::array<float, 4> out_left{};
  std::array<float, 4> out_right{};
  const float* inputs[]{left.data(), right.data()};
  float* outputs[]{out_left.data(), out_right.data()};

  require(processor.process(inputs, 2, outputs, 2, left.size()) ==
              brknam::audio::ProcessStatus::ok,
          "valid stereo block should process");
  require(model.process_calls_ == 1, "mono model should run once per block");
  require_near(out_left[0], 4.0F, 0.01F,
               "stereo inputs should be averaged then modeled");
  for (std::size_t frame = 0; frame < out_left.size(); ++frame) {
    require_near(out_left[frame], out_right[frame], 1.0e-7F,
                 "mono output should broadcast identically");
  }
  require(processor.latency_samples() == 17,
          "active model latency should be reported");
}

void test_trims_bypass_and_normalization() {
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(48000.0, 1);
  GainModel model(4.0F, -24.0);
  model.prepare(48000.0, 1);
  processor.attach_prepared_model(&model);
  processor.set_input_trim_db(6.0205999F);
  processor.set_output_trim_db(-6.0205999F);

  float input_value = 0.25F;
  float output_value = 0.0F;
  const float* inputs[]{&input_value};
  float* outputs[]{&output_value};
  require(processor.process(inputs, 1, outputs, 1, 1) ==
              brknam::audio::ProcessStatus::ok,
          "trim fixture should process");
  require_near(output_value, 1.0F, 0.01F,
               "input/model/output gains should combine deterministically");

  processor.reset();
  processor.set_model_bypassed(true);
  output_value = 0.0F;
  static_cast<void>(processor.process(inputs, 1, outputs, 1, 1));
  require_near(output_value, 0.25F, 0.01F,
               "model bypass should preserve trim stages but skip model gain");
  require(processor.latency_samples() == 0,
          "bypassed model should report zero latency");

  processor.reset();
  processor.set_model_bypassed(false);
  processor.set_input_trim_db(0.0F);
  processor.set_output_trim_db(0.0F);
  processor.set_output_mode(brknam::audio::OutputMode::normalized);
  input_value = 0.125F;
  output_value = 0.0F;
  static_cast<void>(processor.process(inputs, 1, outputs, 1, 1));
  require_near(output_value, 0.9976F, 0.02F,
               "normalization should use model loudness metadata");
}

void test_post_model_dc_blocker() {
  brknam::audio::OneSlotProcessor processor;
  constexpr std::size_t frames = 256;
  processor.prepare(48000.0, frames);
  GainModel model(1.0F);
  model.prepare(48000.0, frames);
  processor.attach_prepared_model(&model);

  std::array<float, frames> input{};
  std::array<float, frames> output{};
  input.fill(1.0F);
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};
  for (int block = 0; block < 80; ++block) {
    static_cast<void>(processor.process(inputs, 1, outputs, 1, frames));
  }
  require(std::abs(output.back()) < 0.001F,
          "5 Hz post-model high-pass should reject settled DC");
}

}  // namespace

int main() {
  try {
    test_requires_prepare_and_bounds_blocks();
    test_stereo_fold_down_model_and_broadcast();
    test_trims_bypass_and_normalization();
    test_post_model_dc_blocker();
    std::cout << "OneSlotProcessorTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
