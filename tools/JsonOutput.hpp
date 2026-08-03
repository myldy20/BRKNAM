// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/library/LibraryDatabase.hpp"

#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace brknam::tools::json {

[[nodiscard]] inline std::string path_utf8(
    const std::filesystem::path& path) {
  const auto value = path.generic_u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] inline std::string quote(const std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20U) {
          output << "\\u00" << std::hex << std::setw(2)
                 << std::setfill('0') << static_cast<int>(character)
                 << std::dec << std::setfill(' ');
        } else {
          output << static_cast<char>(character);
        }
        break;
    }
  }
  output << '"';
  return output.str();
}

[[nodiscard]] inline std::string optional_string(
    const std::optional<std::string>& value) {
  return value.has_value() ? quote(*value) : "null";
}

[[nodiscard]] inline std::string optional_int(
    const std::optional<int>& value) {
  return value.has_value() ? std::to_string(*value) : "null";
}

[[nodiscard]] inline std::string optional_double(
    const std::optional<double>& value) {
  if (!value.has_value()) {
    return "null";
  }
  std::ostringstream output;
  output << std::setprecision(15) << *value;
  return output.str();
}

[[nodiscard]] inline std::string parse_status(
    const library::ParseStatus status) {
  switch (status) {
    case library::ParseStatus::not_applicable:
      return "not_applicable";
    case library::ParseStatus::parsed:
      return "parsed";
    case library::ParseStatus::error:
      return "error";
  }
  return "unknown";
}

[[nodiscard]] inline std::string string_array(
    const std::vector<std::string>& values) {
  std::ostringstream output;
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << quote(values[index]);
  }
  output << ']';
  return output.str();
}

[[nodiscard]] inline std::string asset(
    const library::AssetRecord& value) {
  std::ostringstream output;
  output << "{"
         << "\"id\":" << value.id << ','
         << "\"root_id\":" << value.root_id << ','
         << "\"kind\":" << quote(library::to_string(value.kind)) << ','
         << "\"display_name\":" << quote(value.display_name) << ','
         << "\"path\":" << quote(path_utf8(value.path)) << ','
         << "\"relative_path\":" << quote(path_utf8(value.relative_path)) << ','
         << "\"size_bytes\":" << value.size_bytes << ','
         << "\"modified_ticks\":" << value.modified_ticks << ','
         << "\"content_sha256\":" << optional_string(value.content_sha256) << ','
         << "\"missing\":" << (value.missing ? "true" : "false") << ','
         << "\"parse_status\":" << quote(parse_status(value.parse_status)) << ','
         << "\"parse_error\":" << optional_string(value.parse_error) << ','
         << "\"file_version\":" << optional_string(value.file_version) << ','
         << "\"architecture\":" << optional_string(value.architecture) << ','
         << "\"sample_rate_hz\":" << optional_double(value.sample_rate_hz) << ','
         << "\"modeled_by\":" << optional_string(value.modeled_by) << ','
         << "\"gear_make\":" << optional_string(value.gear_make) << ','
         << "\"gear_model\":" << optional_string(value.gear_model) << ','
         << "\"gear_type\":" << optional_string(value.gear_type) << ','
         << "\"tone_type\":" << optional_string(value.tone_type) << ','
         << "\"input_level_dbu\":" << optional_double(value.input_level_dbu) << ','
         << "\"output_level_dbu\":" << optional_double(value.output_level_dbu) << ','
         << "\"favorite\":" << (value.favorite ? "true" : "false") << ','
         << "\"rating\":" << optional_int(value.rating) << ','
         << "\"tags\":" << string_array(value.tags)
         << '}';
  return output.str();
}

[[nodiscard]] inline std::string assets(
    const std::vector<library::AssetRecord>& values) {
  std::ostringstream output;
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << asset(values[index]);
  }
  output << ']';
  return output.str();
}

[[nodiscard]] inline std::string saved_search(
    const library::SavedSearchRecord& value) {
  std::ostringstream output;
  output << "{"
         << "\"id\":" << value.id << ','
         << "\"name\":" << quote(value.name) << ','
         << "\"query\":" << quote(value.query) << ','
         << "\"include_missing\":"
         << (value.options.include_missing ? "true" : "false") << ','
         << "\"limit\":" << value.options.limit
         << '}';
  return output.str();
}

[[nodiscard]] inline std::string saved_searches(
    const std::vector<library::SavedSearchRecord>& values) {
  std::ostringstream output;
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << saved_search(values[index]);
  }
  output << ']';
  return output.str();
}

[[nodiscard]] inline std::string error(
    const std::string_view code,
    const std::string_view message) {
  return std::string("{\"ok\":false,\"error\":{\"code\":") +
         quote(code) + ",\"message\":" + quote(message) + "}}";
}

}  // namespace brknam::tools::json
