// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace brknam::nam {

struct NamMetadata {
  std::string file_version;
  std::string architecture;
  double sample_rate_hz{48000.0};
  bool sample_rate_declared{false};

  std::optional<std::string> name;
  std::optional<std::string> modeled_by;
  std::optional<std::string> gear_make;
  std::optional<std::string> gear_model;
  std::optional<std::string> gear_type;
  std::optional<std::string> tone_type;
  std::optional<double> input_level_dbu;
  std::optional<double> output_level_dbu;
};

enum class NamReadErrorCode {
  none,
  file_not_found,
  not_a_regular_file,
  file_too_large,
  open_failed,
  malformed_json,
  nesting_too_deep,
  string_too_long,
  missing_required_field,
};

struct NamReadError {
  NamReadErrorCode code{NamReadErrorCode::none};
  std::uintmax_t byte_offset{};
  std::string message;
};

struct NamReadLimits {
  std::uintmax_t max_file_bytes{512ULL * 1024ULL * 1024ULL};
  std::size_t max_nesting_depth{64};
  std::size_t max_string_bytes{1024ULL * 1024ULL};
};

struct NamReadResult {
  std::optional<NamMetadata> metadata;
  std::optional<NamReadError> error;

  [[nodiscard]] explicit operator bool() const noexcept { return metadata.has_value(); }
};

[[nodiscard]] const char* to_string(NamReadErrorCode code) noexcept;
[[nodiscard]] NamReadResult read_metadata(const std::filesystem::path& path,
                                          const NamReadLimits& limits = {});

}  // namespace brknam::nam
