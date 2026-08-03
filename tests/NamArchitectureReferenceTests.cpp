// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/NamCoreModel.hpp"

#include <NAM/activations.h>
#include <NAM/get_dsp.h>
#include <NAM/wavenet/a2_fast.h>
#include <NAM/wavenet/model.h>
#include <json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
constexpr double kModelSampleRateHz = 48000.0;
constexpr std::size_t kReferenceFrames = 2048;
constexpr std::array<std::size_t, 7> kBlockPattern{1, 17, 64, 127,
                                                   31, 257, 93};

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct TempDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("brknam-architecture-reference-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));

  TempDirectory() { std::filesystem::create_directories(path); }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

Json inactive_film() {
  return Json{{"active", false}, {"shift", true}, {"groups", 1}};
}

void add_inactive_optional_modules(Json& layer) {
  layer["head1x1"] =
      Json{{"active", false}, {"out_channels", 1}, {"groups", 1}};
  layer["layer1x1"] = Json{{"active", true}, {"groups", 1}};
  layer["groups_input"] = 1;
  layer["groups_input_mixin"] = 1;
  layer["conv_pre_film"] = inactive_film();
  layer["conv_post_film"] = inactive_film();
  layer["input_mixin_pre_film"] = inactive_film();
  layer["input_mixin_post_film"] = inactive_film();
  layer["activation_pre_film"] = inactive_film();
  layer["activation_post_film"] = inactive_film();
  layer["layer1x1_post_film"] = inactive_film();
  layer["head1x1_post_film"] = inactive_film();
}

Json make_a1_layer(const int input_size, const int channels,
                   const int head_output_channels, const bool head_bias) {
  constexpr std::array<int, 10> dilations{1, 2, 4, 8, 16,
                                          32, 64, 128, 256, 512};
  Json layer;
  layer["input_size"] = input_size;
  layer["condition_size"] = 1;
  layer["head"] = Json{{"out_channels", head_output_channels},
                       {"kernel_size", 1},
                       {"bias", head_bias}};
  layer["channels"] = channels;
  layer["bottleneck"] = channels;
  layer["kernel_sizes"] = std::vector<int>(dilations.size(), 3);
  layer["dilations"] = dilations;
  layer["activation"] =
      std::vector<std::string>(dilations.size(), "Tanh");
  layer["gating_mode"] =
      std::vector<std::string>(dilations.size(), "none");
  layer["secondary_activation"] =
      std::vector<Json>(dilations.size(), nullptr);
  add_inactive_optional_modules(layer);
  return layer;
}

Json make_a1_standard_config() {
  Json config;
  config["layers"] = Json::array(
      {make_a1_layer(1, 16, 8, false), make_a1_layer(16, 8, 1, true)});
  config["head"] = nullptr;
  config["head_scale"] = 0.02F;
  return config;
}

Json make_a2_config(const int channels) {
  constexpr std::array<int, 23> kernel_sizes{
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
  constexpr std::array<int, 23> dilations{
      1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41,
      101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239};

  std::vector<Json> activations;
  activations.reserve(dilations.size());
  for (std::size_t index = 0; index < dilations.size(); ++index) {
    activations.push_back(
        Json{{"type", "LeakyReLU"}, {"negative_slope", 0.01F}});
  }

  Json layer;
  layer["input_size"] = 1;
  layer["condition_size"] = 1;
  layer["head"] =
      Json{{"out_channels", 1}, {"kernel_size", 16}, {"bias", true}};
  layer["channels"] = channels;
  layer["bottleneck"] = channels;
  layer["kernel_sizes"] = kernel_sizes;
  layer["dilations"] = dilations;
  layer["activation"] = activations;
  layer["gating_mode"] =
      std::vector<std::string>(dilations.size(), "none");
  layer["secondary_activation"] =
      std::vector<Json>(dilations.size(), nullptr);
  add_inactive_optional_modules(layer);

  Json config;
  config["layers"] = Json::array({layer});
  config["head"] = nullptr;
  config["head_scale"] = 0.01F;
  return config;
}

std::vector<int> expanded_kernel_sizes(const Json& layer) {
  if (layer.contains("kernel_sizes")) {
    return layer.at("kernel_sizes").get<std::vector<int>>();
  }
  const auto count = layer.at("dilations").size();
  return std::vector<int>(count, layer.at("kernel_size").get<int>());
}

std::vector<std::string> expanded_gating_modes(const Json& layer) {
  const auto count = layer.at("dilations").size();
  if (!layer.contains("gating_mode")) {
    return std::vector<std::string>(count, "none");
  }
  const auto& value = layer.at("gating_mode");
  if (value.is_string()) {
    return std::vector<std::string>(count, value.get<std::string>());
  }
  return value.get<std::vector<std::string>>();
}

std::size_t layer_array_weight_count(const Json& layer) {
  const auto input_size = layer.at("input_size").get<int>();
  const auto condition_size = layer.at("condition_size").get<int>();
  const auto channels = layer.at("channels").get<int>();
  const auto bottleneck = layer.value("bottleneck", channels);
  const auto kernels = expanded_kernel_sizes(layer);
  const auto gating_modes = expanded_gating_modes(layer);
  require(kernels.size() == gating_modes.size(),
          "kernel and gating arrays must have matching lengths");

  std::size_t count =
      static_cast<std::size_t>(input_size * channels);  // rechannel
  for (std::size_t index = 0; index < kernels.size(); ++index) {
    auto gating = gating_modes[index];
    std::transform(gating.begin(), gating.end(), gating.begin(),
                   [](const unsigned char value) {
                     return static_cast<char>(std::tolower(value));
                   });
    const auto paired = gating == "gated" || gating == "blended";
    const auto convolution_output = paired ? 2 * bottleneck : bottleneck;
    count += static_cast<std::size_t>(channels * convolution_output *
                                      kernels[index]);
    count += static_cast<std::size_t>(convolution_output);  // conv bias
    count += static_cast<std::size_t>(condition_size *
                                      convolution_output);  // input mixin

    const auto layer1x1 = layer.value(
        "layer1x1", Json{{"active", true}, {"groups", 1}});
    if (layer1x1.value("active", true)) {
      count += static_cast<std::size_t>(bottleneck * channels + channels);
    }

    const auto head1x1 = layer.value(
        "head1x1",
        Json{{"active", false}, {"out_channels", 1}, {"groups", 1}});
    if (head1x1.value("active", false)) {
      const auto output_channels = head1x1.at("out_channels").get<int>();
      count += static_cast<std::size_t>(bottleneck * output_channels +
                                        output_channels);
    }
  }

  const auto head1x1 = layer.value(
      "head1x1",
      Json{{"active", false}, {"out_channels", 1}, {"groups", 1}});
  const auto head_input_channels = head1x1.value("active", false)
                                       ? head1x1.at("out_channels").get<int>()
                                       : bottleneck;
  const auto& head = layer.at("head");
  const auto head_output_channels = head.at("out_channels").get<int>();
  const auto head_kernel_size = head.at("kernel_size").get<int>();
  count += static_cast<std::size_t>(head_input_channels *
                                    head_output_channels * head_kernel_size);
  if (head.at("bias").get<bool>()) {
    count += static_cast<std::size_t>(head_output_channels);
  }
  return count;
}

std::size_t model_weight_count(const Json& config) {
  std::size_t count = 1;  // trailing head_scale
  for (const auto& layer : config.at("layers")) {
    count += layer_array_weight_count(layer);
  }
  return count;
}

std::vector<float> make_weights(const Json& config) {
  const auto count = model_weight_count(config);
  std::vector<float> weights(count);
  for (std::size_t index = 0; index < weights.size(); ++index) {
    const auto position = static_cast<double>(index + 1);
    weights[index] = static_cast<float>(
        0.055 * std::sin(position * 0.017) +
        0.035 * std::cos(position * 0.031));
  }
  weights.back() = config.at("head_scale").get<float>();
  return weights;
}

std::filesystem::path write_model(const std::filesystem::path& directory,
                                  const std::string& name,
                                  const Json& config,
                                  const std::vector<float>& weights) {
  Json root;
  root["version"] = "0.7.0";
  root["metadata"] = Json{{"name", name},
                          {"modeled_by", "BRKNAM deterministic fixture"},
                          {"loudness", -18.0}};
  root["architecture"] = "WaveNet";
  root["config"] = config;
  root["weights"] = weights;
  root["sample_rate"] = static_cast<int>(kModelSampleRateHz);

  const auto path = directory / (name + ".nam");
  std::ofstream output(path, std::ios::binary);
  output << root.dump();
  if (!output) {
    throw std::runtime_error("Unable to write architecture fixture");
  }
  return path;
}

std::vector<float> make_input(const std::size_t frames,
                              const double sample_rate_hz) {
  std::vector<float> input(frames);
  for (std::size_t index = 0; index < frames; ++index) {
    const auto time = static_cast<double>(index) / sample_rate_hz;
    input[index] = static_cast<float>(
        0.25 * std::sin(2.0 * std::numbers::pi * 220.0 * time) +
        0.10 * std::sin(2.0 * std::numbers::pi * 1230.0 * time));
  }
  return input;
}

std::vector<float> run_core(nam::DSP& dsp, const std::vector<float>& input,
                            const int maximum_block_frames,
                            const bool variable_blocks) {
  dsp.SetPrewarmOnReset(true);
  dsp.Reset(kModelSampleRateHz, maximum_block_frames);
  std::vector<float> output(input.size(), 0.0F);
  std::size_t position = 0;
  std::size_t pattern_index = 0;
  while (position < input.size()) {
    const auto requested = variable_blocks
                               ? kBlockPattern[pattern_index % kBlockPattern.size()]
                               : static_cast<std::size_t>(maximum_block_frames);
    const auto frames = std::min(
        {requested, static_cast<std::size_t>(maximum_block_frames),
         input.size() - position});
    float* input_channels[]{const_cast<float*>(input.data() + position)};
    float* output_channels[]{output.data() + position};
    dsp.process(input_channels, output_channels, static_cast<int>(frames));
    position += frames;
    ++pattern_index;
  }
  return output;
}

std::vector<float> run_adapter(const std::filesystem::path& path,
                               const std::vector<float>& input,
                               const double host_sample_rate_hz,
                               const std::size_t maximum_block_frames,
                               const bool variable_blocks,
                               int* latency_samples = nullptr) {
  auto model = brknam::audio::NamCoreModel::load(path);
  model->prepare(host_sample_rate_hz, maximum_block_frames);
  if (latency_samples != nullptr) {
    *latency_samples = model->info().latency_samples;
  }

  std::vector<float> output(input.size(), 0.0F);
  std::size_t position = 0;
  std::size_t pattern_index = 0;
  while (position < input.size()) {
    const auto requested = variable_blocks
                               ? kBlockPattern[pattern_index % kBlockPattern.size()]
                               : maximum_block_frames;
    const auto frames = std::min(
        {requested, maximum_block_frames, input.size() - position});
    model->process(input.data() + position, output.data() + position, frames);
    position += frames;
    ++pattern_index;
  }
  return output;
}

void compare_vectors(const std::vector<float>& reference,
                     const std::vector<float>& actual, const double tolerance,
                     const std::string& label) {
  require(reference.size() == actual.size(), label + " size mismatch");
  double maximum_difference = 0.0;
  std::size_t maximum_index = 0;
  for (std::size_t index = 0; index < reference.size(); ++index) {
    const auto difference = std::abs(static_cast<double>(reference[index]) -
                                     static_cast<double>(actual[index]));
    if (difference > maximum_difference) {
      maximum_difference = difference;
      maximum_index = index;
    }
  }
  if (!(maximum_difference <= tolerance)) {
    throw std::runtime_error(
        label + " max difference=" + std::to_string(maximum_difference) +
        " at sample=" + std::to_string(maximum_index));
  }
}

void require_finite_non_silent(const std::vector<float>& output,
                               const std::string& label) {
  double energy = 0.0;
  for (const auto value : output) {
    require(std::isfinite(value), label + " produced a non-finite sample");
    energy += static_cast<double>(value) * value;
  }
  require(energy > 1.0e-12, label + " produced only silence");
}

std::unique_ptr<nam::DSP> make_generic_dsp(
    const Json& config, const std::vector<float>& weights) {
  auto parsed = nam::wavenet::parse_config_json(config, kModelSampleRateHz);
  return parsed.create(weights, kModelSampleRateHz);
}

std::unique_ptr<nam::DSP> make_fast_a2_dsp(
    const Json& config, const std::vector<float>& weights) {
  auto parsed = nam::wavenet::a2_fast::create_a2_fast_config(
      config, kModelSampleRateHz);
  return parsed->create(weights, kModelSampleRateHz);
}

void test_a1_standard_reference() {
  TempDirectory temp;
  const auto config = make_a1_standard_config();
  const auto weights = make_weights(config);
  require(model_weight_count(config) == 13802,
          "A1-Standard fixture weight count changed unexpectedly");
  require(!nam::wavenet::a2_fast::is_a2_shape(config, nullptr),
          "A1-Standard fixture must not match the A2 detector");
  const auto path = write_model(temp.path, "a1-standard", config, weights);

  auto automatic = nam::get_dsp(path);
  auto generic = make_generic_dsp(config, weights);
  require(typeid(*automatic) == typeid(*generic),
          "A1-Standard file must dispatch to generic WaveNet");

  const auto input = make_input(kReferenceFrames, kModelSampleRateHz);
  const auto reference = run_core(*generic, input, 257, false);
  const auto automatic_output = run_core(*automatic, input, 257, true);
  const auto adapter_output =
      run_adapter(path, input, kModelSampleRateHz, 257, true);
  compare_vectors(reference, automatic_output, 1.0e-5,
                  "A1 automatic/generic reference");
  compare_vectors(reference, adapter_output, 1.0e-5,
                  "A1 BRKNAM/generic reference");
  require_finite_non_silent(reference, "A1 reference");
}

void test_a2_reference(const int channels, const std::string& name) {
  TempDirectory temp;
  const auto config = make_a2_config(channels);
  const auto weights = make_weights(config);
  const auto expected_weight_count = channels == 8 ? 12146U : 1871U;
  require(model_weight_count(config) == expected_weight_count,
          name + " fixture weight count changed unexpectedly");

  int detected_channels = 0;
  require(nam::wavenet::a2_fast::is_a2_shape(config, &detected_channels),
          name + " fixture must match the A2 detector");
  require(detected_channels == channels,
          name + " detector returned the wrong channel count");
  const auto path = write_model(temp.path, name, config, weights);

  auto automatic = nam::get_dsp(path);
  auto fast = make_fast_a2_dsp(config, weights);
  auto generic = make_generic_dsp(config, weights);
  require(typeid(*automatic) == typeid(*fast),
          name + " file must dispatch to A2 fast implementation");
  require(typeid(*automatic) != typeid(*generic),
          name + " fast and generic implementations must remain distinct");

  const auto input = make_input(kReferenceFrames, kModelSampleRateHz);
  const auto reference = run_core(*generic, input, 257, false);
  const auto fast_output = run_core(*fast, input, 257, true);
  const auto automatic_output = run_core(*automatic, input, 257, true);
  const auto adapter_output =
      run_adapter(path, input, kModelSampleRateHz, 257, true);
  compare_vectors(reference, fast_output, 5.0e-5,
                  name + " fast/generic reference");
  compare_vectors(reference, automatic_output, 5.0e-5,
                  name + " automatic/generic reference");
  compare_vectors(reference, adapter_output, 5.0e-5,
                  name + " BRKNAM/generic reference");
  require_finite_non_silent(reference, name + " reference");
}

void test_architecture_sample_rate(const Json& config,
                                   const std::string& name,
                                   const double host_sample_rate_hz) {
  TempDirectory temp;
  const auto weights = make_weights(config);
  const auto path = write_model(temp.path, name, config, weights);
  const auto input = make_input(4096, host_sample_rate_hz);
  int latency_samples = 0;
  const auto output = run_adapter(path, input, host_sample_rate_hz, 257, true,
                                  &latency_samples);
  require(latency_samples > 0,
          name + " mismatched-rate path must report resampler latency");
  require_finite_non_silent(output, name + " resampled output");
}

void test_a1_and_a2_sample_rates() {
  test_architecture_sample_rate(make_a1_standard_config(),
                                "a1-standard-44100", 44100.0);
  test_architecture_sample_rate(make_a2_config(8), "a2-full-96000", 96000.0);
}

}  // namespace

int main() {
  try {
    nam::activations::Activation::enable_fast_tanh();
    test_a1_standard_reference();
    test_a2_reference(3, "a2-lite");
    test_a2_reference(8, "a2-full");
    test_a1_and_a2_sample_rates();
    std::cout << "NamArchitectureReferenceTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
