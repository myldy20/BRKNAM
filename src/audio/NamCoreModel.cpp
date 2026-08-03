// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/NamCoreModel.hpp"

#include <NAM/activations.h>
#include <NAM/container.h>
#include <NAM/convnet.h>
#include <NAM/get_dsp.h>
#include <NAM/linear.h>
#include <NAM/lstm.h>
#include <NAM/wavenet/model.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "LanczosResampler.hpp"

namespace brknam::audio {
namespace {

constexpr double kKnownSampleRateToleranceHz = 0.01;
constexpr double kAssumedLegacyModelSampleRateHz = 48000.0;
constexpr double kMinimumSupportedSampleRateHz = 8000.0;
constexpr double kMaximumSupportedSampleRateHz = 384000.0;
constexpr std::size_t kMaximumExternalBlockFrames = 8192;
constexpr std::size_t kLanczosRadius = 12;
constexpr std::size_t kResamplerBlockMargin = (kLanczosRadius * 2) + 8;

void enable_fast_tanh_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    nam::activations::Activation::enable_fast_tanh();
  });
}

// NeuralAmpModelerCore registers architecture parsers from static initializers
// located in separate translation units. When Core is linked as a static
// library, an optimizing linker may otherwise discard those translation units
// because get_dsp() reaches them only through the registry. These observable
// volatile references create explicit symbol dependencies without invoking the
// parsers, so all official architectures and their registration objects remain
// in the final binary.
void retain_architecture_registrations() noexcept {
  using Parser = std::unique_ptr<nam::ModelConfig> (*)(
      const nlohmann::json&, double);
  volatile Parser container_parser = &nam::container::create_config;
  volatile Parser convnet_parser = &nam::convnet::create_config;
  volatile Parser linear_parser = &nam::linear::create_config;
  volatile Parser lstm_parser = &nam::lstm::create_config;
  volatile Parser wavenet_parser = &nam::wavenet::create_config;
  static_cast<void>(container_parser);
  static_cast<void>(convnet_parser);
  static_cast<void>(linear_parser);
  static_cast<void>(lstm_parser);
  static_cast<void>(wavenet_parser);
}

[[nodiscard]] double effective_model_sample_rate(const nam::DSP& dsp) noexcept {
  const auto reported = dsp.GetExpectedSampleRate();
  return reported == NAM_UNKNOWN_EXPECTED_SAMPLE_RATE || reported <= 0.0
             ? kAssumedLegacyModelSampleRateHz
             : reported;
}

void validate_sample_rate(const double sample_rate_hz, const char* label) {
  if (!std::isfinite(sample_rate_hz) ||
      sample_rate_hz < kMinimumSupportedSampleRateHz ||
      sample_rate_hz > kMaximumSupportedSampleRateHz) {
    throw std::invalid_argument(std::string(label) +
                                " sample rate is outside BRKNAM limits");
  }
  if (std::abs(sample_rate_hz - std::round(sample_rate_hz)) >
      kKnownSampleRateToleranceHz) {
    throw std::invalid_argument(std::string(label) +
                                " sample rate must be integer-like");
  }
}

[[nodiscard]] ModelInfo read_info(nam::DSP& dsp,
                                  const int latency_samples) noexcept {
  ModelInfo info;
  info.expected_sample_rate_hz = dsp.GetExpectedSampleRate();
  info.latency_samples = std::max(0, latency_samples);
  if (dsp.HasLoudness()) {
    info.loudness_db = dsp.GetLoudness();
  }
  if (dsp.HasInputLevel()) {
    info.input_level_dbu = dsp.GetInputLevel();
  }
  if (dsp.HasOutputLevel()) {
    info.output_level_dbu = dsp.GetOutputLevel();
  }
  return info;
}

class ModelRateAdapter final {
 public:
  explicit ModelRateAdapter(nam::DSP& dsp) : dsp_(dsp) {}

  void prepare(const double host_sample_rate_hz,
               const std::size_t maximum_host_block_frames) {
    validate_sample_rate(host_sample_rate_hz, "Host");
    model_sample_rate_hz_ = effective_model_sample_rate(dsp_);
    validate_sample_rate(model_sample_rate_hz_, "Model");

    if (maximum_host_block_frames == 0 ||
        maximum_host_block_frames > kMaximumExternalBlockFrames) {
      throw std::invalid_argument(
          "Maximum host block size must be between 1 and 8192 frames");
    }

    host_sample_rate_hz_ = host_sample_rate_hz;
    maximum_host_block_frames_ = maximum_host_block_frames;
    active_ = std::abs(host_sample_rate_hz_ - model_sample_rate_hz_) >
              kKnownSampleRateToleranceHz;
    latency_samples_ = 0;

    if (!active_) {
      input_resampler_.reset();
      output_resampler_.reset();
      model_input_.clear();
      model_output_.clear();
      dsp_.SetPrewarmOnReset(true);
      dsp_.Reset(host_sample_rate_hz_,
                 static_cast<int>(maximum_host_block_frames_));
      return;
    }

    const auto ratio = model_sample_rate_hz_ / host_sample_rate_hz_;
    const auto estimated_model_frames =
        std::ceil(static_cast<double>(maximum_host_block_frames_) * ratio);
    if (!std::isfinite(estimated_model_frames) || estimated_model_frames < 1.0 ||
        estimated_model_frames >
            static_cast<double>(std::numeric_limits<int>::max() -
                                kResamplerBlockMargin)) {
      throw std::invalid_argument("Resampled block size is outside Core limits");
    }

    maximum_model_block_frames_ =
        static_cast<std::size_t>(estimated_model_frames) +
        kResamplerBlockMargin;
    model_input_.assign(maximum_model_block_frames_, 0.0F);
    model_output_.assign(maximum_model_block_frames_, 0.0F);

    dsp_.SetPrewarmOnReset(true);
    dsp_.Reset(model_sample_rate_hz_,
               static_cast<int>(maximum_model_block_frames_));

    input_resampler_ = std::make_unique<detail::MonoLanczosResampler>(
        host_sample_rate_hz_, model_sample_rate_hz_);
    output_resampler_ = std::make_unique<detail::MonoLanczosResampler>(
        model_sample_rate_hz_, host_sample_rate_hz_);

    prime_pipeline();
  }

  void process(const float* input, float* output,
               const std::size_t frames) noexcept {
    if (!active_) {
      auto* mutable_input = const_cast<float*>(input);
      float* input_channels[]{mutable_input};
      float* output_channels[]{output};
      dsp_.process(input_channels, output_channels, static_cast<int>(frames));
      return;
    }

    auto* mutable_input = const_cast<float*>(input);
    float* host_input_channels[]{mutable_input};
    input_resampler_->push(host_input_channels[0], frames);

    float* model_input_channels[]{model_input_.data()};
    float* model_output_channels[]{model_output_.data()};
    while (input_resampler_->input_frames_required_for(1) == 0) {
      const auto populated = input_resampler_->pop(
          model_input_channels[0], maximum_model_block_frames_);
      if (populated == 0) {
        break;
      }
      dsp_.process(model_input_channels, model_output_channels,
                   static_cast<int>(populated));
      output_resampler_->push(model_output_channels[0], populated);
    }

    float* host_output_channels[]{output};
    const auto produced = output_resampler_->pop(host_output_channels[0],
                                                 frames);
    if (produced < frames) {
      std::fill_n(output + produced, frames - produced, 0.0F);
    }

    input_resampler_->renormalize_phases();
    output_resampler_->renormalize_phases();
  }

  [[nodiscard]] int latency_samples() const noexcept {
    return latency_samples_;
  }
  [[nodiscard]] std::size_t maximum_host_block_frames() const noexcept {
    return maximum_host_block_frames_;
  }

 private:
  void prime_pipeline() {
    const auto model_samples_needed =
        output_resampler_->input_frames_required_for(1);
    const auto host_latency =
        input_resampler_->input_frames_required_for(model_samples_needed);
    if (host_latency == 0 ||
        host_latency > static_cast<std::size_t>(
                           std::numeric_limits<int>::max())) {
      throw std::runtime_error("Unable to calculate resampler latency");
    }
    if (model_samples_needed > maximum_model_block_frames_) {
      throw std::runtime_error(
          "Resampler warm-up exceeds the prepared model block size");
    }

    std::vector<float> host_silence(host_latency, 0.0F);
    input_resampler_->push(host_silence.data(), host_latency);

    const auto populated = input_resampler_->pop(
        model_input_.data(), model_samples_needed);
    if (populated < model_samples_needed) {
      throw std::runtime_error(
          "Resampler warm-up did not produce enough model-rate samples");
    }

    std::fill_n(model_output_.data(), populated, 0.0F);
    output_resampler_->push(model_output_.data(), populated);
    latency_samples_ = static_cast<int>(host_latency);
  }

  nam::DSP& dsp_;
  std::unique_ptr<detail::MonoLanczosResampler> input_resampler_;
  std::unique_ptr<detail::MonoLanczosResampler> output_resampler_;
  std::vector<float> model_input_;
  std::vector<float> model_output_;
  double host_sample_rate_hz_{};
  double model_sample_rate_hz_{};
  std::size_t maximum_host_block_frames_{};
  std::size_t maximum_model_block_frames_{};
  int latency_samples_{};
  bool active_{};
};

}  // namespace

struct NamCoreModel::Impl {
  std::filesystem::path source_path;
  std::unique_ptr<nam::DSP> dsp;
  std::unique_ptr<ModelRateAdapter> rate_adapter;
  ModelInfo info;
  bool prepared{false};
};

std::unique_ptr<NamCoreModel> NamCoreModel::load(
    const std::filesystem::path& model_path) {
  enable_fast_tanh_once();
  retain_architecture_registrations();

  nam::DspLoadOptions options;
  options.prewarm = false;
  auto dsp = nam::get_dsp(model_path, options);
  if (dsp == nullptr) {
    throw std::runtime_error("NeuralAmpModelerCore returned no DSP model");
  }
  if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1) {
    throw std::runtime_error(
        "BRKNAM E2 requires a one-input/one-output NAM model");
  }

  auto impl = std::make_unique<Impl>();
  impl->source_path = model_path;
  impl->info = read_info(*dsp, 0);
  impl->dsp = std::move(dsp);
  impl->rate_adapter = std::make_unique<ModelRateAdapter>(*impl->dsp);
  return std::unique_ptr<NamCoreModel>(new NamCoreModel(std::move(impl)));
}

NamCoreModel::NamCoreModel(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
NamCoreModel::~NamCoreModel() = default;
NamCoreModel::NamCoreModel(NamCoreModel&&) noexcept = default;
NamCoreModel& NamCoreModel::operator=(NamCoreModel&&) noexcept = default;

void NamCoreModel::prepare(const double sample_rate_hz,
                           const std::size_t maximum_block_frames) {
  impl_->prepared = false;
  impl_->rate_adapter->prepare(sample_rate_hz, maximum_block_frames);
  impl_->info =
      read_info(*impl_->dsp, impl_->rate_adapter->latency_samples());
  impl_->prepared = true;
}

void NamCoreModel::process(const float* input, float* output,
                           const std::size_t frames) noexcept {
  if (!impl_->prepared || input == nullptr || output == nullptr || frames == 0 ||
      frames > impl_->rate_adapter->maximum_host_block_frames()) {
    if (output != nullptr) {
      std::fill_n(output, frames, 0.0F);
    }
    return;
  }

  impl_->rate_adapter->process(input, output, frames);
}

ModelInfo NamCoreModel::info() const noexcept {
  return impl_->info;
}

const std::filesystem::path& NamCoreModel::source_path() const noexcept {
  return impl_->source_path;
}

bool NamCoreModel::prepared() const noexcept {
  return impl_->prepared;
}

}  // namespace brknam::audio
