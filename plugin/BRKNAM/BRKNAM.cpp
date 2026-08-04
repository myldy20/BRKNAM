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

constexpr const char* kUiFont = "Roboto-Regular";

const IColor kBackground{255, 20, 22, 25};
const IColor kPanel{255, 31, 34, 39};
const IColor kControlFill{255, 47, 51, 58};
const IColor kControlPressed{255, 234, 179, 8};
const IColor kFrame{255, 82, 88, 98};
const IColor kText{255, 235, 238, 242};
const IColor kMutedText{255, 156, 163, 174};
const IColor kAccent{255, 234, 179, 8};
const IColor kHighlight{96, 234, 179, 8};

IVStyle make_control_style() {
  return DEFAULT_STYLE.WithColor(kBG, kPanel)
      .WithColor(kFG, kControlFill)
      .WithColor(kPR, kControlPressed)
      .WithColor(kFR, kFrame)
      .WithColor(kHL, kHighlight)
      .WithColor(kSH, COLOR_TRANSPARENT)
      .WithColor(kX1, kAccent)
      .WithLabelText(
          IText(12.0F, kMutedText, kUiFont, EAlign::Center, EVAlign::Top))
      .WithValueText(
          IText(12.0F, kText, kUiFont, EAlign::Center, EVAlign::Bottom))
      .WithRoundness(0.12F)
      .WithFrameThickness(1.0F)
      .WithDrawShadows(false)
      .WithWidgetFrac(0.82F);
}

IVStyle make_button_style() {
  return make_control_style()
      .WithShowValue(false)
      .WithLabelText(
          IText(13.0F, kText, kUiFont, EAlign::Center, EVAlign::Middle))
      .WithRoundness(0.14F)
      .WithWidgetFrac(1.0F);
}

IVStyle make_toggle_style() {
  return make_control_style()
      .WithValueText(
          IText(12.0F, kText, kUiFont, EAlign::Center, EVAlign::Middle))
      .WithWidgetFrac(0.76F);
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
    graphics->LoadFont(kUiFont, ROBOTO_FN);

    const auto bounds = graphics->GetBounds();
    const auto control_style = make_control_style();
    const auto button_style = make_button_style();
    const auto toggle_style = make_toggle_style();
    const auto content = bounds.GetPadded(-20.0F);

    const auto header_area =
        IRECT(content.L, content.T, content.R, content.T + 38.0F);
    const auto model_area =
        IRECT(content.L, content.T + 48.0F, content.R, content.T + 102.0F);
    const auto routing_title_area =
        IRECT(content.L, content.T + 114.0F, content.R, content.T + 132.0F);
    const auto routing_flow_area =
        IRECT(content.L, content.T + 134.0F, content.R, content.T + 154.0F);
    const auto routing_hint_area =
        IRECT(content.L, content.T + 154.0F, content.R, content.T + 176.0F);
    const auto controls_area =
        IRECT(content.L, content.T + 188.0F, content.R, content.B - 26.0F);
    const auto footer =
        IRECT(content.L, content.B - 20.0F, content.R, content.B);

    graphics->AttachControl(new ITextControl(
        header_area, "BRKNAM",
        IText(28.0F, kText, kUiFont, EAlign::Near, EVAlign::Middle)));
    graphics->AttachControl(new ITextControl(
        header_area.GetFromRight(300.0F), "OPEN-SOURCE NAM PLAYER",
        IText(11.0F, kMutedText, kUiFont, EAlign::Far, EVAlign::Middle)));

    const auto load_button_area = model_area.GetFromLeft(150.0F);
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
        "LOAD .NAM", button_style, true, false));

    graphics->AttachControl(
        new ITextControl(
            model_area.GetReducedFromLeft(166.0F),
            displayed_model_name_.c_str(),
            IText(16.0F, kText, kUiFont, EAlign::Near, EVAlign::Middle)),
        kModelNameTag);

    graphics->AttachControl(new ITextControl(
        routing_title_area, "SIGNAL FLOW",
        IText(11.0F, kAccent, kUiFont, EAlign::Near, EVAlign::Middle)));
    graphics->AttachControl(new ITextControl(
        routing_flow_area,
        "INPUT DEVICE  →  INPUT TRIM  →  NAM MODEL  →  OUTPUT TRIM  →  OUTPUT DEVICE",
        IText(12.0F, kText, kUiFont, EAlign::Near, EVAlign::Middle)));
    graphics->AttachControl(new ITextControl(
        routing_hint_area,
        "Standalone: BRKNAM menu → Preferences… for audio I/O   |   Plug-in: routing comes from the DAW track",
        IText(11.0F, kMutedText, kUiFont, EAlign::Near, EVAlign::Middle)));

    const auto input_area = controls_area.GetGridCell(0, 0, 1, 4)
                                .GetPadded(-8.0F);
    const auto output_area = controls_area.GetGridCell(0, 1, 1, 4)
                                 .GetPadded(-8.0F);
    const auto bypass_area = controls_area.GetGridCell(0, 2, 1, 4)
                                 .GetPadded(-12.0F);
    const auto normalize_area = controls_area.GetGridCell(0, 3, 1, 4)
                                    .GetPadded(-12.0F);

    // Native value text entry currently crashes on some macOS versions in the
    // pinned iPlug2 Cocoa view. Keep safe drag/wheel control and double-click
    // reset, but do not invoke PromptUserInput from the knob value area.
    graphics->AttachControl(new IVKnobControl(
        input_area, kInputTrim, "INPUT TRIM", control_style, false, false));
    graphics->AttachControl(new IVKnobControl(
        output_area, kOutputTrim, "OUTPUT TRIM", control_style, false, false));
    graphics->AttachControl(new IVToggleControl(
        bypass_area, kModelBypass, "NAM MODEL", toggle_style, "ACTIVE",
        "BYPASS"));
    graphics->AttachControl(new IVToggleControl(
        normalize_area, kNormalizeOutput, "LEVEL MODE", toggle_style, "RAW",
        "NORMALIZED"));

    graphics->AttachControl(
        new ITextControl(
            footer.GetReducedFromRight(190.0F), displayed_status_.c_str(),
            IText(11.0F, kMutedText, kUiFont, EAlign::Near,
                  EVAlign::Middle)),
        kStatusTag);
    graphics->AttachControl(
        new ITextControl(
            footer.GetFromRight(180.0F), "Latency: 0 samples",
            IText(11.0F, kMutedText, kUiFont, EAlign::Far,
                  EVAlign::Middle)),
        kLatencyTag);
  };
#endif
}

BRKNAM::~BRKNAM() = default;

#if IPLUG_DSP
void BRKNAM::ProcessBlock(sample** inputs, sample** outputs,
                          const int nFrames) {
  const auto connected_inputs = NInChansConnected();
  const auto connected_outputs = NOutChansConnected();
  const auto output_channels = std::clamp(connected_outputs, 0, 2);
  if (outputs == nullptr || nFrames <= 0 || connected_inputs <= 0 ||
      connected_outputs <= 0 || inputs == nullptr) {
    clear_host_outputs(outputs, output_channels, std::max(0, nFrames));
    return;
  }

  const auto input_channels = std::clamp(connected_inputs, 1, 2);
  std::array<const float*, 2> core_inputs{};
  std::array<float*, 2> core_outputs{};
  for (int channel = 0; channel < input_channels; ++channel) {
    if (inputs[channel] == nullptr) {
      clear_host_outputs(outputs, output_channels, nFrames);
      return;
    }
    core_inputs[static_cast<std::size_t>(channel)] = inputs[channel];
  }
  for (int channel = 0; channel < output_channels; ++channel) {
    if (outputs[channel] == nullptr) {
      clear_host_outputs(outputs, output_channels, nFrames);
      return;
    }
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
