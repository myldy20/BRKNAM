// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/audio/OneSlotProcessor.hpp"

#include <filesystem>
#include <memory>

namespace brknam::audio {

class NamCoreModel final : public MonoModel {
 public:
  [[nodiscard]] static std::unique_ptr<NamCoreModel> load(
      const std::filesystem::path& model_path);

  ~NamCoreModel() override;

  NamCoreModel(NamCoreModel&&) noexcept;
  NamCoreModel& operator=(NamCoreModel&&) noexcept;

  NamCoreModel(const NamCoreModel&) = delete;
  NamCoreModel& operator=(const NamCoreModel&) = delete;

  void prepare(double sample_rate_hz,
               std::size_t maximum_block_frames) override;
  void process(const float* input, float* output,
               std::size_t frames) noexcept override;
  [[nodiscard]] ModelInfo info() const noexcept override;

  [[nodiscard]] const std::filesystem::path& source_path() const noexcept;
  [[nodiscard]] bool prepared() const noexcept;

 private:
  struct Impl;
  explicit NamCoreModel(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace brknam::audio
