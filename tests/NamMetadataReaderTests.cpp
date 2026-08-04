// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/nam/NamMetadataReader.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

class TemporaryFile {
 public:
  explicit TemporaryFile(const std::string_view contents) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("brknam-nam-" + std::to_string(suffix) + ".nam");
    std::ofstream stream(path_, std::ios::binary);
    stream << contents;
  }

  ~TemporaryFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

bool expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool reads_metadata_without_materializing_weights() {
  TemporaryFile file(R"json({
    "weights": [0.1, -2.5e-3, 4, 5],
    "config": {"layers": [{"channels": 16}]},
    "version": "0.5.4",
    "architecture": "WaveNet",
    "sample_rate": 44100.5,
    "metadata": {
      "name": "Crunch \u2605",
      "modeled_by": "Ilya",
      "gear_make": "Example",
      "gear_model": "Loud Box",
      "gear_type": "amp_cab",
      "tone_type": "crunch",
      "input_level_dbu": -12.25,
      "output_level_dbu": 7.5,
      "training": {"ignored": [true, false, null]}
    }
  })json");

  const auto result = brknam::nam::read_metadata(file.path());
  return expect(static_cast<bool>(result), "valid NAM JSON should parse") &&
         expect(result.metadata->file_version == "0.5.4", "version should be captured") &&
         expect(result.metadata->architecture == "WaveNet", "architecture should be captured") &&
         expect(std::abs(result.metadata->sample_rate_hz - 44100.5) < 0.001,
                "sample rate should be captured") &&
         expect(result.metadata->sample_rate_declared, "declared sample rate should be marked") &&
         expect(result.metadata->name == std::optional<std::string>{"Crunch ★"},
                "Unicode metadata should be decoded") &&
         expect(result.metadata->gear_type == std::optional<std::string>{"amp_cab"},
                "gear type should be captured") &&
         expect(result.metadata->input_level_dbu == std::optional<double>{-12.25},
                "calibration metadata should be captured");
}

bool parses_decimal_exponents_portably() {
  TemporaryFile file(R"json({
    "version":"0.5.4",
    "architecture":"WaveNet",
    "sample_rate":4.8e4,
    "weights":[1e-3,-2.5E+2,0],
    "metadata":{"input_level_dbu":-1.225e1,"output_level_dbu":7.5e-1}
  })json");
  const auto result = brknam::nam::read_metadata(file.path());

  TemporaryFile overflow(
      R"json({"version":"0.5.4","architecture":"WaveNet","sample_rate":1e9999,"weights":[]})json");
  const auto overflow_result = brknam::nam::read_metadata(overflow.path());

  TemporaryFile underflow(
      R"json({"version":"0.5.4","architecture":"WaveNet","weights":[],"metadata":{"input_level_dbu":1e-9999}})json");
  const auto underflow_result = brknam::nam::read_metadata(underflow.path());

  return expect(static_cast<bool>(result), "scientific decimal forms should parse") &&
         expect(result.metadata->sample_rate_hz == 48000.0,
                "scientific sample rate should be exact") &&
         expect(result.metadata->input_level_dbu == std::optional<double>{-12.25},
                "negative decimal exponent should parse") &&
         expect(result.metadata->output_level_dbu == std::optional<double>{0.75},
                "fractional metadata should parse") &&
         expect(!overflow_result, "overflowing JSON number should be rejected") &&
         expect(overflow_result.error->code == brknam::nam::NamReadErrorCode::malformed_json,
                "overflow should report malformed JSON number") &&
         expect(!underflow_result, "underflowing nonzero JSON number should be rejected") &&
         expect(underflow_result.error->code == brknam::nam::NamReadErrorCode::malformed_json,
                "underflow should report malformed JSON number");
}

bool defaults_sample_rate_to_48khz() {
  TemporaryFile file(R"json({"version":"0.5.3","architecture":"LSTM","config":{},"weights":[]})json");
  const auto result = brknam::nam::read_metadata(file.path());
  return expect(static_cast<bool>(result), "minimal NAM should parse") &&
         expect(result.metadata->sample_rate_hz == 48000.0, "missing sample rate should default to 48 kHz") &&
         expect(!result.metadata->sample_rate_declared, "default sample rate should be distinguishable");
}

bool rejects_malformed_and_missing_required_fields() {
  TemporaryFile malformed(R"json({"version":"0.5.4","architecture":"WaveNet","weights":[1,]})json");
  const auto malformed_result = brknam::nam::read_metadata(malformed.path());

  TemporaryFile missing(R"json({"version":"0.5.4","weights":[]})json");
  const auto missing_result = brknam::nam::read_metadata(missing.path());

  return expect(!malformed_result, "trailing array comma should be rejected") &&
         expect(malformed_result.error->code == brknam::nam::NamReadErrorCode::malformed_json,
                "malformed JSON should have the correct code") &&
         expect(!missing_result, "missing architecture should be rejected") &&
         expect(missing_result.error->code == brknam::nam::NamReadErrorCode::missing_required_field,
                "missing field should have the correct code");
}

bool enforces_limits() {
  TemporaryFile large(R"json({"version":"0.5.4","architecture":"WaveNet","weights":[]})json");
  brknam::nam::NamReadLimits file_limits;
  file_limits.max_file_bytes = 8;
  const auto size_result = brknam::nam::read_metadata(large.path(), file_limits);

  TemporaryFile deep(R"json({"version":"0.5.4","architecture":"WaveNet","x":[[[[0]]]],"weights":[]})json");
  brknam::nam::NamReadLimits depth_limits;
  depth_limits.max_nesting_depth = 3;
  const auto depth_result = brknam::nam::read_metadata(deep.path(), depth_limits);

  return expect(size_result.error->code == brknam::nam::NamReadErrorCode::file_too_large,
                "file-size limit should be enforced") &&
         expect(depth_result.error->code == brknam::nam::NamReadErrorCode::nesting_too_deep,
                "nesting-depth limit should be enforced");
}

}  // namespace

int main() {
  const bool passed = reads_metadata_without_materializing_weights() &&
                      parses_decimal_exponents_portably() &&
                      defaults_sample_rate_to_48khz() &&
                      rejects_malformed_and_missing_required_fields() && enforces_limits();
  if (!passed) {
    return 1;
  }
  std::cout << "All BRKNAM NAM metadata tests passed.\n";
  return 0;
}
