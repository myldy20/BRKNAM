// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/OneSlotProcessor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr double kTestSampleRate = 1000.0;
constexpr std::size_t kCrossfadeFrames = 20;

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
  explicit GainModel(const float gain, const double loudness_db = -18.0,
                     const int latency = 17,
                     std::atomic<int>* destruction_count = nullptr)
      : gain_(gain), loudness_db_(loudness_db), latency_(latency),
        destruction_count_(destruction_count) {}

  ~GainModel() override {
    if (destruction_count_ != nullptr) {
      destruction_count_->fetch_add(1, std::memory_order_relaxed);
    }
  }

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
    result.expected_sample_rate_hz = sample_rate_hz_;
    result.latency_samples = latency_;
    result.loudness_db = loudness_db_;
    return result;
  }

  float gain_{};
  double loudness_db_{};
  int latency_{};
  std::atomic<int>* destruction_count_{};
  double sample_rate_hz_{};
  std::size_t maximum_block_frames_{};
  int process_calls_{};
};

std::unique_ptr<GainModel> prepared_gain(
    const float gain, const std::size_t maximum_block_frames,
    const double loudness_db = -18.0, const int latency = 17,
    std::atomic<int>* destruction_count = nullptr) {
  auto model = std::make_unique<GainModel>(
      gain, loudness_db, latency, destruction_count);
  model->prepare(kTestSampleRate, maximum_block_frames);
  return model;
}

void process_mono(brknam::audio::OneSlotProcessor& processor,
                  const float* input, float* output,
                  const std::size_t frames) {
  const float* inputs[]{input};
  float* outputs[]{output};
  require(processor.process(inputs, 1, outputs, 1, frames) ==
              brknam::audio::ProcessStatus::ok,
          "mono block should process");
}

void finish_pending_crossfade(brknam::audio::OneSlotProcessor& processor,
                              const std::size_t maximum_block_frames) {
  std::array<float, kCrossfadeFrames> input{};
  std::array<float, kCrossfadeFrames> output{};
  require(maximum_block_frames >= kCrossfadeFrames,
          "test helper requires one-block crossfade capacity");
  process_mono(processor, input.data(), output.data(), kCrossfadeFrames);
  require(!processor.has_pending_model_change(),
          "audio block should consume pending model");
  require(!processor.model_crossfade_active(),
          "full crossfade block should finish transition");
}

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

  bool publication_rejected = false;
  try {
    processor.publish_prepared_model(std::make_unique<GainModel>(1.0F));
  } catch (const std::logic_error&) {
    publication_rejected = true;
  }
  require(publication_rejected,
          "models must not publish before processor preparation");

  processor.prepare(kTestSampleRate, 2);
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
  processor.prepare(kTestSampleRate, kCrossfadeFrames);
  auto model = prepared_gain(2.0F, kCrossfadeFrames);
  auto* const observer = model.get();
  processor.publish_prepared_model(std::move(model));
  finish_pending_crossfade(processor, kCrossfadeFrames);
  processor.reset();

  std::array<float, 4> left{1.0F, 0.0F, -1.0F, 0.5F};
  std::array<float, 4> right{3.0F, 2.0F, 1.0F, -0.5F};
  std::array<float, 4> out_left{};
  std::array<float, 4> out_right{};
  const float* inputs[]{left.data(), right.data()};
  float* outputs[]{out_left.data(), out_right.data()};

  require(processor.process(inputs, 2, outputs, 2, left.size()) ==
              brknam::audio::ProcessStatus::ok,
          "valid stereo block should process");
  require(observer->process_calls_ >= 2,
          "mono model should run once per processed block");
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
  processor.prepare(kTestSampleRate, kCrossfadeFrames);
  processor.publish_prepared_model(
      prepared_gain(4.0F, kCrossfadeFrames, -24.0));
  finish_pending_crossfade(processor, kCrossfadeFrames);
  processor.set_input_trim_db(6.0205999F);
  processor.set_output_trim_db(-6.0205999F);
  processor.reset();

  float input_value = 0.25F;
  float output_value = 0.0F;
  process_mono(processor, &input_value, &output_value, 1);
  require_near(output_value, 1.0F, 0.01F,
               "input/model/output gains should combine deterministically");

  processor.reset();
  processor.set_model_bypassed(true);
  output_value = 0.0F;
  process_mono(processor, &input_value, &output_value, 1);
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
  process_mono(processor, &input_value, &output_value, 1);
  require_near(output_value, 0.9976F, 0.02F,
               "normalization should use model loudness metadata");
}

void test_crossfade_and_deferred_destruction() {
  std::atomic<int> destroyed{0};
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kTestSampleRate, kCrossfadeFrames);
  processor.publish_prepared_model(
      prepared_gain(1.0F, kCrossfadeFrames, -18.0, 5, &destroyed));
  finish_pending_crossfade(processor, kCrossfadeFrames);
  static_cast<void>(processor.collect_retired_models());

  processor.publish_prepared_model(
      prepared_gain(3.0F, kCrossfadeFrames, -18.0, 17, &destroyed));
  std::array<float, 10> first_input{};
  std::array<float, 10> second_input{};
  std::array<float, 10> first_output{};
  std::array<float, 10> second_output{};
  for (std::size_t index = 0; index < first_input.size(); ++index) {
    first_input[index] = index % 2 == 0 ? 1.0F : -1.0F;
    second_input[index] = (index + first_input.size()) % 2 == 0 ? 1.0F : -1.0F;
  }

  processor.reset();
  process_mono(processor, first_input.data(), first_output.data(),
               first_input.size());
  require(processor.model_crossfade_active(),
          "half crossfade should remain active");
  require(processor.latency_samples() == 17,
          "crossfade should report the larger active latency");
  require_near(first_output.front(), 1.0F, 0.05F,
               "crossfade must begin at the previous model");

  process_mono(processor, second_input.data(), second_output.data(),
               second_input.size());
  require(!processor.model_crossfade_active(),
          "second half should finish crossfade");
  require_near(std::abs(second_output.back()), 3.0F, 0.2F,
               "crossfade must finish at the new model");
  require(destroyed.load(std::memory_order_relaxed) == 0,
          "audio processing must not destroy the old model");

  require(processor.collect_retired_models() == 1,
          "non-realtime collector should receive the old model");
  require(destroyed.load(std::memory_order_relaxed) == 1,
          "old model should be destroyed by the collector thread");

  processor.detach_model();
  process_mono(processor, first_input.data(), first_output.data(),
               first_input.size());
  require(processor.latency_samples() == 17,
          "fade-to-dry should retain old latency until transition completes");
  process_mono(processor, second_input.data(), second_output.data(),
               second_input.size());
  require(processor.latency_samples() == 0,
          "dry state should report zero latency after transition");
}

void test_pending_requests_are_coalesced_off_audio_thread() {
  std::atomic<int> destroyed{0};
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kTestSampleRate, kCrossfadeFrames);

  processor.publish_prepared_model(
      prepared_gain(2.0F, kCrossfadeFrames, -18.0, 1, &destroyed));
  processor.publish_prepared_model(
      prepared_gain(4.0F, kCrossfadeFrames, -18.0, 2, &destroyed));
  require(destroyed.load(std::memory_order_relaxed) == 1,
          "replaced pending request should be destroyed by publisher");
  require(processor.has_pending_model_change(),
          "latest pending request should remain available");

  finish_pending_crossfade(processor, kCrossfadeFrames);
  require(processor.model_info().latency_samples == 2,
          "audio thread should accept the latest coalesced model");
}

void test_retirement_backpressure_never_deletes_in_callback() {
  std::atomic<int> destroyed{0};
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kTestSampleRate, kCrossfadeFrames);
  processor.publish_prepared_model(
      prepared_gain(1.0F, kCrossfadeFrames, -18.0, 0, &destroyed));
  finish_pending_crossfade(processor, kCrossfadeFrames);

  std::array<float, kCrossfadeFrames> input{};
  std::array<float, kCrossfadeFrames> output{};
  for (int index = 0; index < 20; ++index) {
    processor.publish_prepared_model(prepared_gain(
        static_cast<float>(index + 2), kCrossfadeFrames, -18.0, index,
        &destroyed));
    process_mono(processor, input.data(), output.data(), input.size());
  }

  require(processor.has_pending_model_change(),
          "full retire queue should defer the newest swap instead of blocking");
  require(destroyed.load(std::memory_order_relaxed) >= 2,
          "coalesced pending models may be reclaimed by publisher");

  const auto first_collection = processor.collect_retired_models();
  require(first_collection ==
              brknam::audio::detail::RealtimeModelSlot::kRetiredQueueCapacity,
          "collector should drain the bounded retire queue");
  process_mono(processor, input.data(), output.data(), input.size());
  require(!processor.has_pending_model_change(),
          "audio thread should resume swapping after retirement capacity frees");
  require(processor.collect_retired_models() >= 1,
          "waiting retirement should eventually reach non-realtime collector");
}

void test_post_model_dc_blocker() {
  brknam::audio::OneSlotProcessor processor;
  constexpr std::size_t frames = 960;
  processor.prepare(48000.0, frames);
  auto model = std::make_unique<GainModel>(1.0F);
  model->prepare(48000.0, frames);
  processor.publish_prepared_model(std::move(model));

  std::array<float, frames> input{};
  std::array<float, frames> output{};
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};
  static_cast<void>(processor.process(inputs, 1, outputs, 1, frames));
  processor.reset();

  input.fill(1.0F);
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
    test_crossfade_and_deferred_destruction();
    test_pending_requests_are_coalesced_off_audio_thread();
    test_retirement_backpressure_never_deletes_in_callback();
    test_post_model_dc_blocker();
    std::cout << "OneSlotProcessorTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
