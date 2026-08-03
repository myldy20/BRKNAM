// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#include "brknam/library/LibraryScanner.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <system_error>

namespace brknam::library {
namespace {

[[nodiscard]] std::string lowercase_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return extension;
}

[[nodiscard]] std::optional<AssetKind> classify(const std::filesystem::path& path,
                                                 const ScanOptions& options) {
  const auto extension = lowercase_extension(path);
  if (extension == ".nam") {
    return AssetKind::nam_model;
  }

  if (options.include_impulse_responses &&
      (extension == ".wav" || extension == ".aif" || extension == ".aiff" ||
       extension == ".flac")) {
    return AssetKind::impulse_response;
  }

  return std::nullopt;
}

void inspect_file(const std::filesystem::path& path,
                  const ScanOptions& options,
                  ScanReport& report) {
  const auto kind = classify(path, options);
  if (!kind.has_value()) {
    return;
  }

  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    report.issues.push_back({path, "Unable to read file size: " + error.message()});
    return;
  }

  report.assets.push_back({path, *kind, size});
}

}  // namespace

const char* to_string(const AssetKind kind) noexcept {
  switch (kind) {
    case AssetKind::nam_model:
      return "NAM";
    case AssetKind::impulse_response:
      return "IR";
  }
  return "unknown";
}

ScanReport scan_library(const std::filesystem::path& root, const ScanOptions& options) {
  ScanReport report;
  std::error_code error;

  const auto status = std::filesystem::status(root, error);
  if (error) {
    report.issues.push_back({root, "Unable to inspect path: " + error.message()});
    return report;
  }

  if (std::filesystem::is_regular_file(status)) {
    inspect_file(root, options, report);
  } else if (std::filesystem::is_directory(status)) {
    auto directory_options = std::filesystem::directory_options::skip_permission_denied;
    if (options.follow_directory_symlinks) {
      directory_options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::filesystem::recursive_directory_iterator iterator(root, directory_options, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
      report.issues.push_back({root, "Unable to open directory: " + error.message()});
      return report;
    }

    while (iterator != end) {
      const auto entry = *iterator;
      error.clear();
      if (entry.is_regular_file(error)) {
        inspect_file(entry.path(), options, report);
      } else if (error) {
        report.issues.push_back({entry.path(), "Unable to inspect entry: " + error.message()});
      }

      error.clear();
      iterator.increment(error);
      if (error) {
        report.issues.push_back({root, "Directory traversal warning: " + error.message()});
      }
    }
  } else {
    report.issues.push_back({root, "Path is neither a regular file nor a directory"});
  }

  std::sort(report.assets.begin(), report.assets.end(), [](const auto& left, const auto& right) {
    return left.path.generic_string() < right.path.generic_string();
  });

  return report;
}

}  // namespace brknam::library
