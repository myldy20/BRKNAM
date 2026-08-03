// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#include "brknam/library/LibraryScanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("brknam-test-" + std::to_string(suffix));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string_view contents = "test") {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream << contents;
}

bool expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool scans_supported_files_recursively() {
  TemporaryDirectory temporary;
  write_file(temporary.path() / "amps" / "clean.NAM", "12345");
  write_file(temporary.path() / "cabs" / "cab.wav", "123");
  write_file(temporary.path() / "notes.txt", "ignored");

  const auto report = brknam::library::scan_library(temporary.path());
  return expect(report.issues.empty(), "valid library should not produce warnings") &&
         expect(report.assets.size() == 2, "scanner should find NAM and IR assets") &&
         expect(report.assets[0].path.filename() == "clean.NAM", "results should be sorted") &&
         expect(report.assets[0].kind == brknam::library::AssetKind::nam_model,
                "uppercase NAM extension should be recognized") &&
         expect(report.assets[0].size_bytes == 5, "file size should be recorded") &&
         expect(report.assets[1].kind == brknam::library::AssetKind::impulse_response,
                "WAV should be classified as an impulse-response candidate");
}

bool can_disable_ir_discovery() {
  TemporaryDirectory temporary;
  write_file(temporary.path() / "amp.nam");
  write_file(temporary.path() / "cab.flac");

  brknam::library::ScanOptions options;
  options.include_impulse_responses = false;
  const auto report = brknam::library::scan_library(temporary.path(), options);

  return expect(report.assets.size() == 1, "IR discovery should be optional") &&
         expect(report.assets.front().kind == brknam::library::AssetKind::nam_model,
                "NAM model should remain discoverable");
}

bool reports_missing_roots() {
  TemporaryDirectory temporary;
  const auto missing = temporary.path() / "missing";
  const auto report = brknam::library::scan_library(missing);

  return expect(report.assets.empty(), "missing root should not return assets") &&
         expect(report.issues.size() == 1, "missing root should produce one warning");
}

}  // namespace

int main() {
  const bool passed = scans_supported_files_recursively() && can_disable_ir_discovery() &&
                      reports_missing_roots();
  if (!passed) {
    return 1;
  }

  std::cout << "All BRKNAM scanner tests passed.\n";
  return 0;
}
