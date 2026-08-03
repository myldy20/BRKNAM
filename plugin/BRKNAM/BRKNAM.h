// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "brknam/audio/ModelLoadWorker.hpp"
#include "brknam/audio/NamCoreModel.hpp"
#include "brknam/audio/OneSlotProcessor.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

constexpr int kNumPresets = 1;

enum EParams {
  kInputTrim = 0,
  kOutputTrim,
  kModelBypass,
  kNormalizeOutput,
  kNumParams,
};

enum ECtrlTags {
  kModelNameTag = 0,
  kStatusTag,
  kLatencyTag,
  kNumCtrlTags,
};

class BRKNAM final : public iplug::Plugin {
 public:
  explicit BRKNAM(const iplug::InstanceInfo& info);
  ~BRKNAM() override;

#if IPLUG_DSP
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs,
                    int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
  void OnIdle() override;
#endif

 private:
#if IPLUG_DSP
  void apply_parameters() noexcept;
  void rebuild_model_worker();
  void request_model(const std::filesystem::path& path);
  void update_status_controls();

  brknam::audio::OneSlotProcessor processor_;
  // Declared after processor_ so the worker is stopped and joined first.
  std::unique_ptr<brknam::audio::ModelLoadWorker> model_worker_;

  std::filesystem::path selected_model_path_;
  std::uint64_t displayed_generation_{};
  int reported_latency_samples_{};
  std::string displayed_model_name_{"No model loaded"};
  std::string displayed_status_{"Dry signal"};

  WDL_String dialog_file_name_;
  WDL_String dialog_path_;
#endif
};
