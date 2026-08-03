// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#include "brknam/library/LibraryScanner.hpp"

#include <filesystem>
#include <iostream>

int main(const int argc, const char* const argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: brknam-scan <library-path>\n";
    return 2;
  }

  const auto report = brknam::library::scan_library(std::filesystem::path{argv[1]});
  for (const auto& asset : report.assets) {
    std::cout << brknam::library::to_string(asset.kind) << '\t' << asset.size_bytes << '\t'
              << asset.path.string() << '\n';
  }

  for (const auto& issue : report.issues) {
    std::cerr << "warning: " << issue.path.string() << ": " << issue.message << '\n';
  }

  std::cerr << "Found " << report.assets.size() << " supported asset(s); "
            << report.issues.size() << " warning(s).\n";
  return report.issues.empty() ? 0 : 1;
}
