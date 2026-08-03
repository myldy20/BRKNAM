// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/ModelLoadWorker.hpp"
#include "brknam/audio/NamCoreModel.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace std::chrono_literals;

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

struct TempDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("brknam-nam-core-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));

  TempDirectory() {
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

void write_text(const std::filesystem::path& path,
                const std::string& contents) {
  std::ofstream output(path, std::ios::binary);
  output << contents;
  if (!output) {
    throw std::runtime_error("Unable to write NAM test fixture");
  }
}

std::string linear_fixture(const int input_channels = 1,
                           const int output_channels = 1) {
  return std::string(R"({
    "version":"0.7.0",
    "architecture":"Linear",
    "config":{
      "receptive_field":1,
      "bias":false,
      "in_channels":)") +
         std::to_string(input_channels) + R"(,
      "out_channels":)" +
         std::to_string(output_channels) + R"(,
      "implementation":"direct"
    },
    "metadata":{
      "loudness":-24.0,
      "input_level_dbu":12.0,
      "output_level_dbu":4.0
    },
    "weights":[2.0],
    "sample_rate":48000
  })";
}

void test_load_prepare_process_and_metadata() {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());

  auto model = brknam::audio::NamCoreModel::load(path);
  require(model != nullptr, "valid NAM fixture should load");
  require(model->source_path() == path, "adapter should preserve source path");
  require(!model->prepared(), "loaded model should not be prepared implicitly");

  const auto before_prepare = model->info();
  require_near(static_cast<float>(before_prepare.expected_sample_rate_hz),
               48000.0F, 0.01F, "expected sample rate should be exposed");
  require(before_prepare.loudness_db == -24.0,
          "loudness metadata should be exposed");
  require(before_prepare.input_level_dbu == 12.0,
          "input calibration metadata should be exposed");
  require(before_prepare.output_level_dbu == 4.0,
          "output calibration metadata should be exposed");

  model->prepare(48000.0, 4);
  require(model->prepared(), "matching-rate model should prepare");

  std::array<float, 4> input{1.0F, 2.0F, -1.0F, 0.5F};
  std::array<float, 4> output{};
  model->process(input.data(), output.data(), input.size());
  require_near(output[0], 2.0F, 1.0e-5F,
               "linear Core model should process first sample");
  require_near(output[1], 4.0F, 1.0e-5F,
               "linear Core model should process second sample");
  require_near(output[2], -2.0F, 1.0e-5F,
               "linear Core model should process negative sample");
  require_near(output[3], 1.0F, 1.0e-5F,
               "linear Core model should process final sample");
}

void test_sample_rate_mismatch_is_explicit() {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());
  auto model = brknam::audio::NamCoreModel::load(path);

  bool rejected = false;
  try {
    model->prepare(44100.0, 64);
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("resampling") !=
               std::string::npos;
  }
  require(rejected,
          "known-rate mismatch should explain that resampling is pending");
  require(!model->prepared(), "failed preparation must not publish the model");
}

void test_multichannel_model_is_rejected() {
  TempDirectory temp;
  const auto path = temp.path / "stereo-linear.nam";
  write_text(path, linear_fixture(2, 2));

  bool rejected = false;
  try {
    static_cast<void>(brknam::audio::NamCoreModel::load(path));
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("one-input/one-output") !=
               std::string::npos;
  }
  require(rejected, "E2 adapter must reject non-mono NAM models");
}

void test_unprepared_process_clears_output() {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());
  auto model = brknam::audio::NamCoreModel::load(path);

  std::array<float, 2> input{1.0F, 1.0F};
  std::array<float, 2> output{9.0F, 9.0F};
  model->process(input.data(), output.data(), input.size());
  require(output[0] == 0.0F && output[1] == 0.0F,
          "unprepared adapter must fail silent");
}

void test_end_to_end_worker_core_and_audio_path() {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());

  constexpr std::size_t crossfade_frames = 960;
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(48000.0, crossfade_frames);
  brknam::audio::ModelLoadWorker worker(
      processor, 48000.0, crossfade_frames,
      [](const std::filesystem::path& requested) {
        return brknam::audio::NamCoreModel::load(requested);
      });

  const auto generation = worker.request_model(path);
  require(worker.wait_until_settled(generation, 5s),
          "real Core load should settle on worker thread");
  const auto status = worker.status();
  require(status.state == brknam::audio::ModelLoadState::published,
          "real Core model should publish through worker");

  std::array<float, crossfade_frames> transition_input{};
  std::array<float, crossfade_frames> transition_output{};
  for (std::size_t index = 0; index < transition_input.size(); ++index) {
    transition_input[index] = index % 2 == 0 ? 0.25F : -0.25F;
  }
  const float* transition_inputs[]{transition_input.data()};
  float* transition_outputs[]{transition_output.data()};
  require(processor.process(transition_inputs, 1, transition_outputs, 1,
                            transition_input.size()) ==
              brknam::audio::ProcessStatus::ok,
          "audio block should accept worker-published Core model");
  require(!processor.model_crossfade_active(),
          "one 20 ms block should finish Core model transition");

  processor.reset();
  std::array<float, 4> input{0.25F, -0.25F, 0.25F, -0.25F};
  std::array<float, 4> output{};
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};
  require(processor.process(inputs, 1, outputs, 1, input.size()) ==
              brknam::audio::ProcessStatus::ok,
          "settled Core model should process through OneSlotProcessor");
  require_near(output[0], 0.5F, 0.01F,
               "end-to-end path should apply real Core model gain");
  require_near(output[1], -0.5F, 0.01F,
               "end-to-end path should preserve alternating polarity");
}

}  // namespace

int main() {
  try {
    test_load_prepare_process_and_metadata();
    test_sample_rate_mismatch_is_explicit();
    test_multichannel_model_is_rejected();
    test_unprepared_process_clears_output();
    test_end_to_end_worker_core_and_audio_path();
    std::cout << "NamCoreModelTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
