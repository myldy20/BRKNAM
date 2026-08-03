// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/NamCoreModel.hpp"

#include <NAM/activations.h>
#include <NAM/get_dsp.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace brknam::audio {
namespace {

constexpr double kKnownSampleRateToleranceHz = 0.01;

void enable_fast_tanh_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    nam::activations::Activation::enable_fast_tanh();
  });
}

[[nodiscard]] ModelInfo read_info(nam::DSP& dsp) noexcept {
  ModelInfo info;
  info.expected_sample_rate_hz = dsp.GetExpectedSampleRate();
  info.latency_samples = 0;
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

}  // namespace

struct NamCoreModel::Impl {
  std::filesystem::path source_path;
  std::unique_ptr<nam::DSP> dsp;
  ModelInfo info;
  bool prepared{false};
};

std::unique_ptr<NamCoreModel> NamCoreModel::load(
    const std::filesystem::path& model_path) {
  enable_fast_tanh_once();

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
  impl->info = read_info(*dsp);
  impl->dsp = std::move(dsp);
  return std::unique_ptr<NamCoreModel>(new NamCoreModel(std::move(impl)));
}

NamCoreModel::NamCoreModel(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
NamCoreModel::~NamCoreModel() = default;
NamCoreModel::NamCoreModel(NamCoreModel&&) noexcept = default;
NamCoreModel& NamCoreModel::operator=(NamCoreModel&&) noexcept = default;

void NamCoreModel::prepare(const double sample_rate_hz,
                           const std::size_t maximum_block_frames) {
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument("Sample rate must be finite and positive");
  }
  if (maximum_block_frames == 0 ||
      maximum_block_frames > static_cast<std::size_t>(
                                 std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Maximum block size is outside Core limits");
  }

  const auto expected = impl_->dsp->GetExpectedSampleRate();
  if (expected != NAM_UNKNOWN_EXPECTED_SAMPLE_RATE &&
      std::abs(expected - sample_rate_hz) > kKnownSampleRateToleranceHz) {
    throw std::runtime_error(
        "Model sample rate does not match the host; resampling is not yet "
        "available in this E2 slice");
  }

  impl_->dsp->SetPrewarmOnReset(true);
  impl_->dsp->Reset(sample_rate_hz,
                    static_cast<int>(maximum_block_frames));
  impl_->prepared = true;
  impl_->info = read_info(*impl_->dsp);
}

void NamCoreModel::process(const float* input, float* output,
                           const std::size_t frames) noexcept {
  if (!impl_->prepared || input == nullptr || output == nullptr || frames == 0 ||
      frames > static_cast<std::size_t>(impl_->dsp->GetMaxBufferSize())) {
    if (output != nullptr) {
      std::fill_n(output, frames, 0.0F);
    }
    return;
  }

  // The Core API predates const-correct input buffers. BRKNAM owns the actual
  // mutable mono scratch buffer supplied here; the public MonoModel contract
  // keeps callers from depending on input mutation.
  auto* mutable_input = const_cast<float*>(input);
  float* input_channels[]{mutable_input};
  float* output_channels[]{output};
  impl_->dsp->process(input_channels, output_channels,
                      static_cast<int>(frames));
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
