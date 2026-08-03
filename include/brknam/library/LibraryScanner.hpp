// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace brknam::library {

enum class AssetKind {
  nam_model,
  impulse_response,
};

struct LibraryAsset {
  std::filesystem::path path;
  AssetKind kind;
  std::uintmax_t size_bytes{};
};

struct ScanIssue {
  std::filesystem::path path;
  std::string message;
};

struct ScanOptions {
  bool include_impulse_responses{true};
  bool follow_directory_symlinks{false};
};

struct ScanReport {
  std::vector<LibraryAsset> assets;
  std::vector<ScanIssue> issues;
};

[[nodiscard]] const char* to_string(AssetKind kind) noexcept;
[[nodiscard]] ScanReport scan_library(const std::filesystem::path& root,
                                      const ScanOptions& options = {});

}  // namespace brknam::library
