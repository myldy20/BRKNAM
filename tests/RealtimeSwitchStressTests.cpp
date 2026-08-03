// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/OneSlotProcessor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

constexpr double kSampleRateHz = 48000.0;
constexpr std::size_t kBlockFrames = 64;
constexpr std::size_t kBurstRequests = 2000;
constexpr std::size_t kSettledSwaps = 160;
constexpr float kMaximumAllowedJump = 0.03F;

thread_local bool g_inside_audio_callback = false;

struct LifetimeCounters {
  std::atomic<std::size_t> created{0};
  std::atomic<std::size_t> destroyed{0};
  std::atomic<std::size_t> destroyed_on_audio_thread{0};
};

class GainModel final : public brknam::audio::MonoModel {
 public:
  GainModel(const float gain, LifetimeCounters& counters)
      : gain_(gain), counters_(counters) {
    counters_.created.fetch_add(1, std::memory_order_relaxed);
  }

  ~GainModel() override {
    counters_.destroyed.fetch_add(1, std::memory_order_relaxed);
    if (g_inside_audio_callback) {
      counters_.destroyed_on_audio_thread.fetch_add(
          1, std::memory_order_relaxed);
    }
  }

  void prepare(const double sample_rate_hz,
               const std::size_t maximum_block_frames) override {
    if (sample_rate_hz != kSampleRateHz ||
        maximum_block_frames != kBlockFrames) {
      throw std::runtime_error("stress model received unexpected preparation");
    }
    prepared_ = true;
  }

  void process(const float* input, float* output,
               const std::size_t frames) noexcept override {
    if (!prepared_) {
      std::fill_n(output, frames, 0.0F);
      return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
      output[frame] = input[frame] * gain_;
    }
  }

  [[nodiscard]] brknam::audio::ModelInfo info() const noexcept override {
    brknam::audio::ModelInfo result;
    result.expected_sample_rate_hz = kSampleRateHz;
    result.latency_samples = 0;
    result.loudness_db = -18.0;
    return result;
  }

 private:
  float gain_{};
  LifetimeCounters& counters_;
  bool prepared_{};
};

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::unique_ptr<brknam::audio::MonoModel> make_model(
    const std::size_t index, LifetimeCounters& counters) {
  const auto phase = static_cast<float>(index % 17) / 16.0F;
  auto model = std::make_unique<GainModel>(0.8F + (0.4F * phase), counters);
  model->prepare(kSampleRateHz, kBlockFrames);
  return model;
}

void update_maximum(std::atomic<float>& destination,
                    const float candidate) noexcept {
  auto current = destination.load(std::memory_order_relaxed);
  while (candidate > current &&
         !destination.compare_exchange_weak(
             current, candidate, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

void run_stress() {
  LifetimeCounters lifetimes;
  std::atomic<bool> publisher_done{false};
  std::atomic<bool> audio_done{false};
  std::atomic<std::size_t> audio_blocks{0};
  std::atomic<std::size_t> process_failures{0};
  std::atomic<std::size_t> non_finite_samples{0};
  std::atomic<float> maximum_jump{0.0F};

  {
    brknam::audio::OneSlotProcessor processor;
    processor.prepare(kSampleRateHz, kBlockFrames);

    std::thread audio_thread([&] {
      std::array<float, kBlockFrames> input{};
      std::array<float, kBlockFrames> output{};
      const float* inputs[]{input.data()};
      float* outputs[]{output.data()};
      double oscillator_phase = 0.0;
      float previous_output = 0.0F;
      bool have_previous = false;
      const auto phase_step =
          (2.0 * std::numbers::pi * 440.0) / kSampleRateHz;

      const auto deadline = std::chrono::steady_clock::now() + 20s;
      while (std::chrono::steady_clock::now() < deadline) {
        for (auto& sample : input) {
          sample = 0.05F * static_cast<float>(std::sin(oscillator_phase));
          oscillator_phase += phase_step;
          if (oscillator_phase >= 2.0 * std::numbers::pi) {
            oscillator_phase -= 2.0 * std::numbers::pi;
          }
        }

        g_inside_audio_callback = true;
        const auto status = processor.process(inputs, 1, outputs, 1,
                                              input.size());
        g_inside_audio_callback = false;
        if (status != brknam::audio::ProcessStatus::ok) {
          process_failures.fetch_add(1, std::memory_order_relaxed);
        }

        for (const auto sample : output) {
          if (!std::isfinite(sample)) {
            non_finite_samples.fetch_add(1, std::memory_order_relaxed);
            continue;
          }
          if (have_previous) {
            update_maximum(maximum_jump, std::abs(sample - previous_output));
          }
          previous_output = sample;
          have_previous = true;
        }

        audio_blocks.fetch_add(1, std::memory_order_release);
        if (publisher_done.load(std::memory_order_acquire) &&
            !processor.has_pending_model_change() &&
            !processor.model_crossfade_active()) {
          break;
        }
      }
      g_inside_audio_callback = false;
      audio_done.store(true, std::memory_order_release);
    });

    // Phase 1 intentionally overwhelms the single pending slot. Superseded
    // models must be reclaimed by this publisher thread, never by audio.
    for (std::size_t index = 0; index < kBurstRequests; ++index) {
      processor.publish_prepared_model(make_model(index, lifetimes));
      if ((index % 11) == 0) {
        static_cast<void>(processor.collect_retired_models());
      }
      if ((index % 31) == 0) {
        std::this_thread::yield();
      }
    }

    // Phase 2 gives every requested model enough audio blocks to finish the
    // 20 ms crossfade. This exercises actual graph retirement repeatedly,
    // rather than testing only pending-request coalescing.
    for (std::size_t index = 0; index < kSettledSwaps; ++index) {
      const auto starting_block =
          audio_blocks.load(std::memory_order_acquire);
      processor.publish_prepared_model(
          make_model(kBurstRequests + index, lifetimes));

      const auto required_blocks = static_cast<std::size_t>(
          std::ceil((kSampleRateHz *
                     brknam::audio::OneSlotProcessor::
                         kModelCrossfadeMilliseconds / 1000.0) /
                    static_cast<double>(kBlockFrames))) +
                                   2;
      const auto target_block = starting_block + required_blocks;
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (audio_blocks.load(std::memory_order_acquire) < target_block &&
             std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(processor.collect_retired_models());
        std::this_thread::yield();
      }
      require(audio_blocks.load(std::memory_order_acquire) >= target_block,
              "audio callback stopped making progress during model switching");
    }

    processor.detach_model();
    publisher_done.store(true, std::memory_order_release);
    while (!audio_done.load(std::memory_order_acquire)) {
      static_cast<void>(processor.collect_retired_models());
      std::this_thread::yield();
    }
    audio_thread.join();
    while (processor.collect_retired_models() != 0) {
    }
  }

  require(audio_done.load(std::memory_order_relaxed),
          "audio stress thread did not terminate");
  require(process_failures.load(std::memory_order_relaxed) == 0,
          "processor rejected a valid stress block");
  require(non_finite_samples.load(std::memory_order_relaxed) == 0,
          "model switching produced NaN or infinity");
  require(maximum_jump.load(std::memory_order_relaxed) <=
              kMaximumAllowedJump,
          "model switching exceeded the click/discontinuity threshold");
  require(lifetimes.destroyed_on_audio_thread.load(
              std::memory_order_relaxed) == 0,
          "a model was destroyed from the audio callback");
  require(lifetimes.created.load(std::memory_order_relaxed) ==
              lifetimes.destroyed.load(std::memory_order_relaxed),
          "model ownership leaked during concurrent switching");
  require(audio_blocks.load(std::memory_order_relaxed) >
              kSettledSwaps * 10,
          "stress test did not process enough audio blocks");
}

}  // namespace

int main() {
  try {
    run_stress();
    std::cout << "RealtimeSwitchStressTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
