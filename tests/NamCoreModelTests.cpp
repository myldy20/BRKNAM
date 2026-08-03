// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/ModelLoadWorker.hpp"
#include "brknam/audio/NamCoreModel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

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
                           const int output_channels = 1,
                           const int sample_rate = 48000) {
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
    "sample_rate":)" +
         std::to_string(sample_rate) + R"(
  })";
}

void fill_sine(std::vector<float>& values, const double sample_rate_hz,
               const double frequency_hz, const float amplitude) {
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = amplitude * static_cast<float>(std::sin(
                                  2.0 * std::numbers::pi * frequency_hz *
                                  static_cast<double>(index) /
                                  sample_rate_hz));
  }
}

float rms(const std::vector<float>& values, const std::size_t begin) {
  require(begin < values.size(), "RMS range must contain samples");
  double sum = 0.0;
  for (std::size_t index = begin; index < values.size(); ++index) {
    const auto value = static_cast<double>(values[index]);
    sum += value * value;
  }
  return static_cast<float>(
      std::sqrt(sum / static_cast<double>(values.size() - begin)));
}

void process_variable_blocks(brknam::audio::NamCoreModel& model,
                             const std::vector<float>& input,
                             std::vector<float>& output,
                             const std::size_t maximum_block_frames) {
  constexpr std::array<std::size_t, 6> block_pattern{1, 17, 64, 127, 31,
                                                     257};
  std::size_t offset = 0;
  std::size_t pattern_index = 0;
  while (offset < input.size()) {
    const auto requested = block_pattern[pattern_index % block_pattern.size()];
    const auto frames = std::min(
        {requested, maximum_block_frames, input.size() - offset});
    model.process(input.data() + offset, output.data() + offset, frames);
    offset += frames;
    ++pattern_index;
  }
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
  require(model->info().latency_samples == 0,
          "matching-rate model should not report resampler latency");

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

void test_sample_rate_adaptation(const double host_sample_rate_hz) {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());
  auto model = brknam::audio::NamCoreModel::load(path);

  constexpr std::size_t maximum_block_frames = 257;
  model->prepare(host_sample_rate_hz, maximum_block_frames);
  require(model->prepared(), "mismatched-rate model should prepare");
  require(model->info().latency_samples > 0,
          "active resampling should report positive host latency");
  require_near(static_cast<float>(model->info().expected_sample_rate_hz),
               48000.0F, 0.01F,
               "resampling must preserve model-rate metadata");

  const auto sample_count =
      static_cast<std::size_t>(std::llround(host_sample_rate_hz));
  std::vector<float> input(sample_count);
  std::vector<float> output(sample_count, 0.0F);
  fill_sine(input, host_sample_rate_hz, 997.0, 0.1F);
  process_variable_blocks(*model, input, output, maximum_block_frames);

  require(std::all_of(output.begin(), output.end(),
                      [](const float value) { return std::isfinite(value); }),
          "resampler output must remain finite across variable blocks");
  const auto analysis_begin = std::min(
      output.size() - 1,
      static_cast<std::size_t>(model->info().latency_samples) + 2048);
  const auto measured_gain =
      rms(output, analysis_begin) / rms(input, analysis_begin);
  require_near(measured_gain, 2.0F, 0.04F,
               "round-trip resampling should preserve linear-model gain");
}

void test_sample_rate_mismatch_is_resampled() {
  test_sample_rate_adaptation(44100.0);
  test_sample_rate_adaptation(96000.0);
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

void test_end_to_end_worker_core_resampler_and_audio_path() {
  TempDirectory temp;
  const auto path = temp.path / "linear.nam";
  write_text(path, linear_fixture());

  constexpr double host_sample_rate_hz = 44100.0;
  constexpr std::size_t crossfade_frames = 882;
  brknam::audio::OneSlotProcessor processor;
  processor.prepare(host_sample_rate_hz, crossfade_frames);
  brknam::audio::ModelLoadWorker worker(
      processor, host_sample_rate_hz, crossfade_frames,
      [](const std::filesystem::path& requested) {
        return brknam::audio::NamCoreModel::load(requested);
      });

  const auto generation = worker.request_model(path);
  require(worker.wait_until_settled(generation, 5s),
          "resampled Core load should settle on worker thread");
  const auto status = worker.status();
  require(status.state == brknam::audio::ModelLoadState::published,
          "resampled Core model should publish through worker");

  std::vector<float> input(crossfade_frames, 0.0F);
  std::vector<float> output(crossfade_frames, 0.0F);
  fill_sine(input, host_sample_rate_hz, 997.0, 0.1F);
  const float* inputs[]{input.data()};
  float* outputs[]{output.data()};
  require(processor.process(inputs, 1, outputs, 1, input.size()) ==
              brknam::audio::ProcessStatus::ok,
          "audio block should accept worker-published resampled model");
  require(!processor.model_crossfade_active(),
          "one 20 ms block should finish resampled model transition");
  require(processor.latency_samples() > 0,
          "OneSlotProcessor should report model resampler latency");

  double output_energy = 0.0;
  for (int block = 0; block < 8; ++block) {
    require(processor.process(inputs, 1, outputs, 1, input.size()) ==
                brknam::audio::ProcessStatus::ok,
            "settled resampled model should keep processing");
    if (block >= 2) {
      for (const auto value : output) {
        output_energy += static_cast<double>(value) * value;
      }
    }
  }
  require(output_energy > 1.0,
          "end-to-end resampled Core path should produce modeled audio");
}

}  // namespace

int main() {
  try {
    test_load_prepare_process_and_metadata();
    test_sample_rate_mismatch_is_resampled();
    test_multichannel_model_is_rejected();
    test_unprepared_process_clears_output();
    test_end_to_end_worker_core_resampler_and_audio_path();
    std::cout << "NamCoreModelTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
