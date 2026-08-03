// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#include "brknam/nam/NamMetadataReader.hpp"

#include <iomanip>
#include <iostream>

namespace {

void print_optional(const char* label, const std::optional<std::string>& value) {
  if (value.has_value()) {
    std::cout << label << ": " << *value << '\n';
  }
}

void print_optional(const char* label, const std::optional<double>& value) {
  if (value.has_value()) {
    std::cout << label << ": " << *value << '\n';
  }
}

}  // namespace

int main(const int argc, const char* const argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: brknam-inspect <model.nam>\n";
    return 2;
  }

  const auto result = brknam::nam::read_metadata(argv[1]);
  if (!result) {
    const auto& error = *result.error;
    std::cerr << brknam::nam::to_string(error.code) << " at byte " << error.byte_offset << ": "
              << error.message << '\n';
    return 1;
  }

  const auto& metadata = *result.metadata;
  std::cout << "Version: " << metadata.file_version << '\n';
  std::cout << "Architecture: " << metadata.architecture << '\n';
  std::cout << "Sample rate: " << std::setprecision(12) << metadata.sample_rate_hz;
  if (!metadata.sample_rate_declared) {
    std::cout << " (default)";
  }
  std::cout << '\n';
  print_optional("Name", metadata.name);
  print_optional("Modeled by", metadata.modeled_by);
  print_optional("Gear make", metadata.gear_make);
  print_optional("Gear model", metadata.gear_model);
  print_optional("Gear type", metadata.gear_type);
  print_optional("Tone type", metadata.tone_type);
  print_optional("Input level dBu", metadata.input_level_dbu);
  print_optional("Output level dBu", metadata.output_level_dbu);
  return 0;
}
