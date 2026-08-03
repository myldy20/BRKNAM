// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "BRKNAM.h"

#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <utility>

using namespace iplug;
using namespace igraphics;

namespace {

const IColor kBackground{255, 22, 24, 27};
const IColor kPanel{255, 35, 38, 43};
const IColor kText{255, 232, 235, 239};
const IColor kMutedText{255, 158, 164, 173};
const IColor kAccent{255, 234, 179, 8};
const IColor kPressedAccent{255, 184, 138, 4};

IVStyle make_style() {
  return DEFAULT_STYLE.WithColor(kBG, kPanel)
      .WithColor(kFG, kAccent)
      .WithColor(kPR, kPressedAccent)
      .WithColor(kFR, kMutedText)
      .WithColor(kHL, kAccent)
      .WithColor(kSH, COLOR_BLACK)
      .WithColor(kX1, kText);
}

void clear_host_outputs(sample** outputs, const int output_channels,
                        const int frames) noexcept {
  if (outputs == nullptr || frames <= 0) {
    return;
  }
  for (int channel = 0; channel < output_channels; ++channel) {
    if (outputs[channel] != nullptr) {
      std::fill_n(outputs[channel], frames, 0.0F);
    }
  }
}

}  // namespace

BRKNAM::BRKNAM(const InstanceInfo& info)
    : Plugin(info, MakeConfig(kNumParams, kNumPresets)) {
  GetParam(kInputTrim)->InitGain("Input", 0.0, -24.0, 24.0, 0.1);
  GetParam(kOutputTrim)->InitGain("Output", 0.0, -24.0, 24.0, 0.1);
  GetParam(kModelBypass)->InitBool("Model bypass", false);
  GetParam(kNormalizeOutput)->InitBool("Normalize output", false);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                        GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* graphics) {
    graphics->AttachPanelBackground(kBackground);
    graphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    graphics->EnableMouseOver(true);

    const auto bounds = graphics->GetBounds();
    const auto style = make_style();
    const auto content = bounds.GetPadded(-18.0F);
    const auto title_area = content.GetFromTop(34.0F);
    const auto model_area = content.GetFromTop(42.0F).GetVShifted(40.0F);
    const auto controls_area = content.GetFromBottom(126.0F);

    graphics->AttachControl(new ITextControl(
        title_area, "BRKNAM",
        IText(25.0F, kText, nullptr, EAlign::Near, EVAlign::Middle)));
    graphics->AttachControl(new ITextControl(
        title_area.GetFromRight(245.0F), "OPEN-SOURCE NAM PLAYER",
        IText(12.0F, kMutedText, nullptr, EAlign::Far, EVAlign::Middle)));

    const auto load_button_area = model_area.GetFromLeft(126.0F);
    graphics->AttachControl(new IVButtonControl(
        load_button_area,
        [this](IControl* caller) {
          auto* ui = caller->GetUI();
          if (ui == nullptr) {
            return;
          }
          ui->PromptForFile(
              dialog_file_name_, dialog_path_, EFileAction::Open, ".nam",
              [this](const WDL_String& file_name, const WDL_String&) {
                if (file_name.GetLength() == 0) {
                  return;
                }
                request_model(std::filesystem::path(file_name.Get()));
              });
        },
        "LOAD .NAM", style, true, false));

    graphics->AttachControl(
        new ITextControl(
            model_area.GetReducedFromLeft(140.0F),
            displayed_model_name_.c_str(),
            IText(16.0F, kText, nullptr, EAlign::Near, EVAlign::Middle)),
        kModelNameTag);

    const auto input_area = controls_area.GetGridCell(0, 0, 1, 4)
                                .GetPadded(-8.0F);
    const auto output_area = controls_area.GetGridCell(0, 1, 1, 4)
                                 .GetPadded(-8.0F);
    const auto bypass_area = controls_area.GetGridCell(0, 2, 1, 4)
                                 .GetPadded(-12.0F);
    const auto normalize_area = controls_area.GetGridCell(0, 3, 1, 4)
                                    .GetPadded(-12.0F);

    graphics->AttachControl(new IVKnobControl(
        input_area, kInputTrim, "INPUT", style, true, true));
    graphics->AttachControl(new IVKnobControl(
        output_area, kOutputTrim, "OUTPUT", style, true, true));
    graphics->AttachControl(new IVToggleControl(
        bypass_area, kModelBypass, "MODEL", style, "ACTIVE", "BYPASS"));
    graphics->AttachControl(new IVToggleControl(
        normalize_area, kNormalizeOutput, "OUTPUT", style, "RAW",
        "NORMALIZED"));

    const auto footer = content.GetFromBottom(20.0F);
    graphics->AttachControl(
        new ITextControl(
            footer.GetReducedFromRight(150.0F), displayed_status_.c_str(),
            IText(12.0F, kMutedText, nullptr, EAlign::Near,
                  EVAlign::Middle)),
        kStatusTag);
    graphics->AttachControl(
        new ITextControl(
            footer.GetFromRight(140.0F), "Latency: 0 samples",
            IText(12.0F, kMutedText, nullptr, EAlign::Far,
                  EVAlign::Middle)),
        kLatencyTag);
  };
#endif
}

BRKNAM::~BRKNAM() = default;

#if IPLUG_DSP
void BRKNAM::ProcessBlock(sample** inputs, sample** outputs,
                          const int nFrames) {
  const auto input_channels = std::clamp(NInChansConnected(), 1, 2);
  const auto output_channels = std::clamp(NOutChansConnected(), 1, 2);
  if (inputs == nullptr || outputs == nullptr || nFrames < 0) {
    clear_host_outputs(outputs, output_channels, std::max(0, nFrames));
    return;
  }

  std::array<const float*, 2> core_inputs{};
  std::array<float*, 2> core_outputs{};
  for (int channel = 0; channel < input_channels; ++channel) {
    core_inputs[static_cast<std::size_t>(channel)] = inputs[channel];
  }
  for (int channel = 0; channel < output_channels; ++channel) {
    core_outputs[static_cast<std::size_t>(channel)] = outputs[channel];
  }

  static_cast<void>(processor_.process(
      core_inputs.data(), static_cast<std::size_t>(input_channels),
      core_outputs.data(), static_cast<std::size_t>(output_channels),
      static_cast<std::size_t>(nFrames)));
}

void BRKNAM::OnReset() {
  rebuild_model_worker();
}

void BRKNAM::OnParamChange(const int paramIdx) {
  switch (paramIdx) {
    case kInputTrim:
      processor_.set_input_trim_db(
          static_cast<float>(GetParam(kInputTrim)->Value()));
      break;
    case kOutputTrim:
      processor_.set_output_trim_db(
          static_cast<float>(GetParam(kOutputTrim)->Value()));
      break;
    case kModelBypass:
      processor_.set_model_bypassed(GetParam(kModelBypass)->Bool());
      break;
    case kNormalizeOutput:
      processor_.set_output_mode(
          GetParam(kNormalizeOutput)->Bool()
              ? brknam::audio::OutputMode::normalized
              : brknam::audio::OutputMode::raw);
      break;
    default:
      break;
  }
}

void BRKNAM::OnIdle() {
  const auto latency = processor_.latency_samples();
  if (latency != reported_latency_samples_) {
    reported_latency_samples_ = latency;
    SetLatency(latency);
  }
  update_status_controls();
}

void BRKNAM::apply_parameters() noexcept {
  processor_.set_input_trim_db(
      static_cast<float>(GetParam(kInputTrim)->Value()));
  processor_.set_output_trim_db(
      static_cast<float>(GetParam(kOutputTrim)->Value()));
  processor_.set_model_bypassed(GetParam(kModelBypass)->Bool());
  processor_.set_output_mode(
      GetParam(kNormalizeOutput)->Bool()
          ? brknam::audio::OutputMode::normalized
          : brknam::audio::OutputMode::raw);
}

void BRKNAM::rebuild_model_worker() {
  model_worker_.reset();
  const auto sample_rate = GetSampleRate();
  const auto maximum_block_frames =
      static_cast<std::size_t>(std::max(1, GetBlockSize()));
  processor_.prepare(sample_rate, maximum_block_frames);
  apply_parameters();
  reported_latency_samples_ = 0;
  SetLatency(0);

  model_worker_ = std::make_unique<brknam::audio::ModelLoadWorker>(
      processor_, sample_rate, maximum_block_frames,
      [](const std::filesystem::path& path) {
        return brknam::audio::NamCoreModel::load(path);
      });

  if (!selected_model_path_.empty()) {
    static_cast<void>(model_worker_->request_model(selected_model_path_));
    displayed_status_ = "Reloading model after host reset";
  } else {
    displayed_status_ = "Dry signal";
  }
}

void BRKNAM::request_model(const std::filesystem::path& path) {
  selected_model_path_ = path;
  displayed_model_name_ = path.filename().string();
  displayed_status_ = "Queued";
  displayed_generation_ = 0;
  if (model_worker_ != nullptr) {
    displayed_generation_ = model_worker_->request_model(path);
  }
  update_status_controls();
}

void BRKNAM::update_status_controls() {
  if (model_worker_ != nullptr) {
    const auto status = model_worker_->status();
    if (displayed_generation_ == 0 ||
        status.requested_generation >= displayed_generation_) {
      displayed_generation_ = status.requested_generation;
      displayed_status_ = brknam::audio::to_string(status.state);
      if (status.error_message.has_value()) {
        displayed_status_ += ": ";
        displayed_status_ += *status.error_message;
      }
    }
  }

#if IPLUG_EDITOR
  auto* ui = GetUI();
  if (ui == nullptr) {
    return;
  }
  if (auto* control = ui->GetControlWithTag(kModelNameTag)) {
    if (auto* text = dynamic_cast<ITextControl*>(control)) {
      text->SetStr(displayed_model_name_.c_str());
      text->SetDirty(false);
    }
  }
  if (auto* control = ui->GetControlWithTag(kStatusTag)) {
    if (auto* text = dynamic_cast<ITextControl*>(control)) {
      text->SetStr(displayed_status_.c_str());
      text->SetDirty(false);
    }
  }
  if (auto* control = ui->GetControlWithTag(kLatencyTag)) {
    if (auto* text = dynamic_cast<ITextControl*>(control)) {
      text->SetStrFmt(64, "Latency: %d samples", reported_latency_samples_);
      text->SetDirty(false);
    }
  }
#endif
}
#endif
