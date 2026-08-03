// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/ModelLoadWorker.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
constexpr double kSampleRate = 1000.0;
constexpr std::size_t kBlockFrames = 20;

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TrackingModel final : public brknam::audio::MonoModel {
 public:
  TrackingModel(const float gain, const int latency,
                std::atomic<int>* destruction_count = nullptr)
      : gain_(gain), latency_(latency),
        destruction_count_(destruction_count) {}

  ~TrackingModel() override {
    if (destruction_count_ != nullptr) {
      destruction_count_->fetch_add(1, std::memory_order_relaxed);
    }
  }

  void prepare(const double sample_rate_hz,
               const std::size_t maximum_block_frames) override {
    if (sample_rate_hz <= 0.0 || maximum_block_frames == 0) {
      throw std::runtime_error("invalid test preparation");
    }
    sample_rate_hz_ = sample_rate_hz;
    maximum_block_frames_ = maximum_block_frames;
    prepared_ = true;
  }

  void process(const float* input, float* output,
               const std::size_t frames) noexcept override {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      output[frame] = input[frame] * gain_;
    }
  }

  [[nodiscard]] brknam::audio::ModelInfo info() const noexcept override {
    brknam::audio::ModelInfo result;
    result.expected_sample_rate_hz = sample_rate_hz_;
    result.latency_samples = latency_;
    result.loudness_db = -18.0;
    return result;
  }

 private:
  float gain_{};
  int latency_{};
  std::atomic<int>* destruction_count_{};
  double sample_rate_hz_{};
  std::size_t maximum_block_frames_{};
  bool prepared_{};
};

void process_crossfade(brknam::audio::OneSlotProcessor& processor) {
  std::array<float, kBlockFrames> input{};
  std::array<float, kBlockFrames> output{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    input[index] = index % 2 == 0 ? 1.0F : -1.0F;
  }
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};
  require(processor.process(inputs, 1, outputs, 1, input.size()) ==
              brknam::audio::ProcessStatus::ok,
          "crossfade block should process");
}

void test_successful_load_and_publication() {
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kSampleRate, kBlockFrames);

  brknam::audio::ModelLoadWorker worker(
      processor, kSampleRate, kBlockFrames,
      [](const std::filesystem::path& path) {
        require(path.filename() == "gain-two.nam",
                "loader should receive the requested path");
        return std::make_unique<TrackingModel>(2.0F, 12);
      });

  const auto generation = worker.request_model("gain-two.nam");
  require(worker.wait_until_settled(generation, 2s),
          "successful generation should settle");
  const auto status = worker.status();
  require(status.state == brknam::audio::ModelLoadState::published,
          "successful load should publish");
  require(status.completed_generation == generation,
          "published status should expose generation");
  require(processor.has_pending_model_change(),
          "published worker result should reach processor pending slot");

  process_crossfade(processor);
  require(processor.model_info().latency_samples == 12,
          "audio thread should accept the prepared worker result");
}

void test_structured_error_keeps_processor_dry() {
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kSampleRate, kBlockFrames);

  brknam::audio::ModelLoadWorker worker(
      processor, kSampleRate, kBlockFrames,
      [](const std::filesystem::path&) -> std::unique_ptr<brknam::audio::MonoModel> {
        throw std::runtime_error("broken model fixture");
      });

  const auto generation = worker.request_model("broken.nam");
  require(worker.wait_until_settled(generation, 2s),
          "failed generation should settle");
  const auto status = worker.status();
  require(status.state == brknam::audio::ModelLoadState::error,
          "loader exception should become error state");
  require(status.error_message.has_value() &&
              status.error_message->find("broken model") != std::string::npos,
          "error state should preserve useful diagnostics");
  require(!processor.has_pending_model_change(),
          "failed load must not publish a model");
}

void test_inflight_load_is_superseded_by_latest_request() {
  struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool first_started{};
    bool release_first{};
    std::vector<std::string> loaded_paths;
  } gate;
  std::atomic<int> destroyed{0};

  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kSampleRate, kBlockFrames);
  brknam::audio::ModelLoadWorker worker(
      processor, kSampleRate, kBlockFrames,
      [&](const std::filesystem::path& path) {
        const auto name = path.filename().string();
        {
          std::unique_lock lock(gate.mutex);
          gate.loaded_paths.push_back(name);
          if (name == "first.nam") {
            gate.first_started = true;
            gate.condition.notify_all();
            gate.condition.wait(lock, [&] { return gate.release_first; });
          }
        }
        const auto latency = name == "first.nam" ? 1 : 2;
        return std::make_unique<TrackingModel>(
            static_cast<float>(latency), latency, &destroyed);
      });

  const auto first = worker.request_model("first.nam");
  {
    std::unique_lock lock(gate.mutex);
    require(gate.condition.wait_for(lock, 2s,
                                    [&] { return gate.first_started; }),
            "first load should enter the blocking factory");
  }

  const auto second = worker.request_model("second.nam");
  {
    std::lock_guard lock(gate.mutex);
    gate.release_first = true;
  }
  gate.condition.notify_all();

  require(worker.wait_until_settled(first, 2s),
          "superseded generation should settle when newer work settles");
  require(worker.wait_until_settled(second, 2s),
          "latest generation should settle");
  const auto status = worker.status();
  require(status.state == brknam::audio::ModelLoadState::published &&
              status.completed_generation == second &&
              status.requested_path.filename() == "second.nam",
          "only the newest generation should publish");
  require(destroyed.load(std::memory_order_relaxed) == 1,
          "obsolete prepared model should be destroyed on worker thread");

  process_crossfade(processor);
  require(processor.model_info().latency_samples == 2,
          "processor should receive only the latest model");
  require(gate.loaded_paths.size() == 2,
          "worker should finish in-flight work then load latest request");
}

void test_detach_and_periodic_retired_collection() {
  std::atomic<int> destroyed{0};
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kSampleRate, kBlockFrames);
  brknam::audio::ModelLoadWorker worker(
      processor, kSampleRate, kBlockFrames,
      [&](const std::filesystem::path&) {
        return std::make_unique<TrackingModel>(2.0F, 9, &destroyed);
      });

  const auto load_generation = worker.request_model("model.nam");
  require(worker.wait_until_settled(load_generation, 2s),
          "model should load before detach test");
  process_crossfade(processor);
  require(processor.latency_samples() == 9,
          "loaded model should become active");

  const auto detach_generation = worker.request_detach();
  require(worker.wait_until_settled(detach_generation, 2s),
          "detach request should settle");
  process_crossfade(processor);
  require(processor.latency_samples() == 0,
          "detach should settle to dry latency");

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (destroyed.load(std::memory_order_relaxed) == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  require(destroyed.load(std::memory_order_relaxed) == 1,
          "idle worker should periodically collect retired models");
}

void test_request_validation() {
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(kSampleRate, kBlockFrames);
  brknam::audio::ModelLoadWorker worker(
      processor, kSampleRate, kBlockFrames,
      [](const std::filesystem::path&) {
        return std::make_unique<TrackingModel>(1.0F, 0);
      });

  bool rejected = false;
  try {
    static_cast<void>(worker.request_model({}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "empty model path should be rejected");
}

}  // namespace

int main() {
  try {
    test_successful_load_and_publication();
    test_structured_error_keeps_processor_dry();
    test_inflight_load_is_superseded_by_latest_request();
    test_detach_and_periodic_retired_collection();
    test_request_validation();
    std::cout << "ModelLoadWorkerTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
