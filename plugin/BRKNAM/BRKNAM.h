// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "IPlug_include_in_plug_hdr.h"

#if defined(APP_API)
#if defined(OS_MAC)
#include "IPlugSWELL.h"
#elif defined(OS_WIN)
#include <windows.h>
#endif
#endif

#include "brknam/audio/ModelLoadWorker.hpp"
#include "brknam/audio/NamCoreModel.hpp"
#include "brknam/audio/OneSlotProcessor.hpp"

#include <atomic>
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
  kAudioStatusTag,
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
  std::string displayed_audio_status_{"AUDIO: waiting for stream"};

  // Written by the realtime thread and consumed by OnIdle. The peak atomics
  // hold the maximum absolute sample observed since the previous UI refresh.
  std::atomic<std::uint64_t> audio_callback_count_{0};
  std::atomic<float> input_peak_{0.0F};
  std::atomic<float> output_peak_{0.0F};
  std::atomic<int> last_process_status_{
      static_cast<int>(brknam::audio::ProcessStatus::not_prepared)};
  std::atomic<int> last_input_channels_{0};
  std::atomic<int> last_output_channels_{0};

  std::uint64_t displayed_callback_count_{};
  int stale_audio_ticks_{};
  float displayed_input_peak_{};
  float displayed_output_peak_{};

  WDL_String dialog_file_name_;
  WDL_String dialog_path_;
#endif
};
