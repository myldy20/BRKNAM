// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ilya Tolstoukhov (@myldy20)
// See ATTRIBUTION.md for the GPLv3 section 7(b) attribution term.

#include "brknam/nam/NamMetadataReader.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <istream>
#include <string_view>
#include <system_error>

namespace brknam::nam {
namespace {

class JsonProbe {
 public:
  JsonProbe(std::istream& stream, const NamReadLimits& limits) : stream_(stream), limits_(limits) {}

  [[nodiscard]] NamReadResult parse() {
    NamMetadata metadata;
    skip_whitespace();
    if (!consume('{')) {
      return failure(NamReadErrorCode::malformed_json, "The root JSON value must be an object");
    }

    if (!parse_root_object(metadata, 1)) {
      return {.metadata = std::nullopt, .error = error_};
    }

    skip_whitespace();
    if (peek() != std::char_traits<char>::eof()) {
      return failure(NamReadErrorCode::malformed_json, "Unexpected data after the root object");
    }

    if (metadata.file_version.empty()) {
      return failure(NamReadErrorCode::missing_required_field,
                     "Required top-level string field 'version' is missing");
    }
    if (metadata.architecture.empty()) {
      return failure(NamReadErrorCode::missing_required_field,
                     "Required top-level string field 'architecture' is missing");
    }

    return {.metadata = std::move(metadata), .error = std::nullopt};
  }

 private:
  [[nodiscard]] int peek() { return stream_.peek(); }

  [[nodiscard]] int get() {
    const int value = stream_.get();
    if (value != std::char_traits<char>::eof()) {
      ++offset_;
    }
    return value;
  }

  void skip_whitespace() {
    while (true) {
      const int value = peek();
      if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
        static_cast<void>(get());
      } else {
        return;
      }
    }
  }

  [[nodiscard]] bool consume(const char expected) {
    skip_whitespace();
    if (peek() != expected) {
      return false;
    }
    static_cast<void>(get());
    return true;
  }

  [[nodiscard]] bool expect(const char expected, const std::string_view context) {
    if (consume(expected)) {
      return true;
    }
    set_error(NamReadErrorCode::malformed_json,
              "Expected '" + std::string(1, expected) + "' " + std::string(context));
    return false;
  }

  [[nodiscard]] bool check_depth(const std::size_t depth) {
    if (depth <= limits_.max_nesting_depth) {
      return true;
    }
    set_error(NamReadErrorCode::nesting_too_deep, "JSON nesting limit exceeded");
    return false;
  }

  void set_error(const NamReadErrorCode code, std::string message) {
    if (!error_.has_value()) {
      error_ = NamReadError{code, offset_, std::move(message)};
    }
  }

  [[nodiscard]] NamReadResult failure(const NamReadErrorCode code, std::string message) {
    set_error(code, std::move(message));
    return {.metadata = std::nullopt, .error = error_};
  }

  [[nodiscard]] bool parse_root_object(NamMetadata& metadata, const std::size_t depth) {
    if (!check_depth(depth)) {
      return false;
    }

    skip_whitespace();
    if (consume('}')) {
      return true;
    }

    while (true) {
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      if (!expect(':', "after an object key")) {
        return false;
      }

      if (key == "version") {
        if (!parse_required_string(metadata.file_version, "version")) {
          return false;
        }
      } else if (key == "architecture") {
        if (!parse_required_string(metadata.architecture, "architecture")) {
          return false;
        }
      } else if (key == "sample_rate") {
        double sample_rate = 0.0;
        if (!parse_number(sample_rate) || !std::isfinite(sample_rate) || sample_rate <= 0.0) {
          if (!error_.has_value()) {
            set_error(NamReadErrorCode::malformed_json,
                      "Field 'sample_rate' must be a finite positive number");
          }
          return false;
        }
        metadata.sample_rate_hz = sample_rate;
        metadata.sample_rate_declared = true;
      } else if (key == "metadata") {
        if (!parse_metadata_object(metadata, depth + 1)) {
          return false;
        }
      } else if (!skip_value(depth + 1)) {
        return false;
      }

      skip_whitespace();
      if (consume('}')) {
        return true;
      }
      if (!expect(',', "between object members")) {
        return false;
      }
    }
  }

  [[nodiscard]] bool parse_metadata_object(NamMetadata& metadata, const std::size_t depth) {
    if (!check_depth(depth)) {
      return false;
    }
    if (!expect('{', "for field 'metadata'")) {
      return false;
    }

    skip_whitespace();
    if (consume('}')) {
      return true;
    }

    while (true) {
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      if (!expect(':', "after a metadata key")) {
        return false;
      }

      if (key == "name") {
        if (!parse_optional_string(metadata.name, key)) {
          return false;
        }
      } else if (key == "modeled_by") {
        if (!parse_optional_string(metadata.modeled_by, key)) {
          return false;
        }
      } else if (key == "gear_make") {
        if (!parse_optional_string(metadata.gear_make, key)) {
          return false;
        }
      } else if (key == "gear_model") {
        if (!parse_optional_string(metadata.gear_model, key)) {
          return false;
        }
      } else if (key == "gear_type") {
        if (!parse_optional_string(metadata.gear_type, key)) {
          return false;
        }
      } else if (key == "tone_type") {
        if (!parse_optional_string(metadata.tone_type, key)) {
          return false;
        }
      } else if (key == "input_level_dbu") {
        if (!parse_optional_number(metadata.input_level_dbu, key)) {
          return false;
        }
      } else if (key == "output_level_dbu") {
        if (!parse_optional_number(metadata.output_level_dbu, key)) {
          return false;
        }
      } else if (!skip_value(depth + 1)) {
        return false;
      }

      skip_whitespace();
      if (consume('}')) {
        return true;
      }
      if (!expect(',', "between metadata members")) {
        return false;
      }
    }
  }

  [[nodiscard]] bool parse_required_string(std::string& target, const std::string_view field) {
    skip_whitespace();
    if (peek() != '"') {
      set_error(NamReadErrorCode::malformed_json,
                "Field '" + std::string(field) + "' must be a string");
      return false;
    }
    return parse_string(target);
  }

  [[nodiscard]] bool parse_optional_string(std::optional<std::string>& target,
                                            const std::string_view field) {
    skip_whitespace();
    if (try_consume_literal("null")) {
      target.reset();
      return true;
    }
    if (peek() != '"') {
      set_error(NamReadErrorCode::malformed_json,
                "Metadata field '" + std::string(field) + "' must be a string or null");
      return false;
    }
    std::string value;
    if (!parse_string(value)) {
      return false;
    }
    target = std::move(value);
    return true;
  }

  [[nodiscard]] bool parse_optional_number(std::optional<double>& target,
                                            const std::string_view field) {
    skip_whitespace();
    if (try_consume_literal("null")) {
      target.reset();
      return true;
    }
    double value = 0.0;
    if (!parse_number(value) || !std::isfinite(value)) {
      if (!error_.has_value()) {
        set_error(NamReadErrorCode::malformed_json,
                  "Metadata field '" + std::string(field) + "' must be a finite number or null");
      }
      return false;
    }
    target = value;
    return true;
  }

  [[nodiscard]] bool parse_string(std::string& output) {
    skip_whitespace();
    if (get() != '"') {
      set_error(NamReadErrorCode::malformed_json, "Expected a JSON string");
      return false;
    }

    output.clear();
    while (true) {
      const int value = get();
      if (value == std::char_traits<char>::eof()) {
        set_error(NamReadErrorCode::malformed_json, "Unterminated JSON string");
        return false;
      }
      if (value == '"') {
        return true;
      }
      if (value >= 0 && value < 0x20) {
        set_error(NamReadErrorCode::malformed_json,
                  "Unescaped control character in JSON string");
        return false;
      }
      if (value != '\\') {
        output.push_back(static_cast<char>(value));
      } else {
        const int escaped = get();
        switch (escaped) {
          case '"':
          case '\\':
          case '/':
            output.push_back(static_cast<char>(escaped));
            break;
          case 'b':
            output.push_back('\b');
            break;
          case 'f':
            output.push_back('\f');
            break;
          case 'n':
            output.push_back('\n');
            break;
          case 'r':
            output.push_back('\r');
            break;
          case 't':
            output.push_back('\t');
            break;
          case 'u': {
            std::uint32_t codepoint = 0;
            if (!parse_unicode_escape(codepoint)) {
              return false;
            }
            if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
              if (get() != '\\' || get() != 'u') {
                set_error(NamReadErrorCode::malformed_json,
                          "High Unicode surrogate is not followed by a low surrogate");
                return false;
              }
              std::uint32_t low = 0;
              if (!parse_unicode_escape(low) || low < 0xDC00U || low > 0xDFFFU) {
                set_error(NamReadErrorCode::malformed_json, "Invalid low Unicode surrogate");
                return false;
              }
              codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
            } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
              set_error(NamReadErrorCode::malformed_json, "Unexpected low Unicode surrogate");
              return false;
            }
            append_utf8(output, codepoint);
            break;
          }
          default:
            set_error(NamReadErrorCode::malformed_json, "Invalid JSON string escape");
            return false;
        }
      }

      if (output.size() > limits_.max_string_bytes) {
        set_error(NamReadErrorCode::string_too_long, "JSON string limit exceeded");
        return false;
      }
    }
  }

  [[nodiscard]] bool parse_unicode_escape(std::uint32_t& value) {
    value = 0;
    for (int index = 0; index < 4; ++index) {
      const int digit = get();
      if (digit == std::char_traits<char>::eof()) {
        set_error(NamReadErrorCode::malformed_json, "Incomplete Unicode escape");
        return false;
      }
      value <<= 4U;
      if (digit >= '0' && digit <= '9') {
        value |= static_cast<std::uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        value |= static_cast<std::uint32_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        value |= static_cast<std::uint32_t>(digit - 'A' + 10);
      } else {
        set_error(NamReadErrorCode::malformed_json, "Invalid hexadecimal Unicode escape");
        return false;
      }
    }
    return true;
  }

  static void append_utf8(std::string& output, const std::uint32_t codepoint) {
    if (codepoint <= 0x7FU) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
      output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
      output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
  }

  [[nodiscard]] bool parse_number(double& output) {
    skip_whitespace();
    std::string token;
    token.reserve(32);

    if (peek() == '-') {
      token.push_back(static_cast<char>(get()));
    }

    if (peek() == '0') {
      token.push_back(static_cast<char>(get()));
      if (peek() >= '0' && peek() <= '9') {
        set_error(NamReadErrorCode::malformed_json, "Leading zero in JSON number");
        return false;
      }
    } else if (peek() >= '1' && peek() <= '9') {
      while (peek() >= '0' && peek() <= '9') {
        token.push_back(static_cast<char>(get()));
      }
    } else {
      set_error(NamReadErrorCode::malformed_json, "Invalid JSON number");
      return false;
    }

    if (peek() == '.') {
      token.push_back(static_cast<char>(get()));
      if (peek() < '0' || peek() > '9') {
        set_error(NamReadErrorCode::malformed_json,
                  "JSON fraction must contain at least one digit");
        return false;
      }
      while (peek() >= '0' && peek() <= '9') {
        token.push_back(static_cast<char>(get()));
      }
    }

    if (peek() == 'e' || peek() == 'E') {
      token.push_back(static_cast<char>(get()));
      if (peek() == '+' || peek() == '-') {
        token.push_back(static_cast<char>(get()));
      }
      if (peek() < '0' || peek() > '9') {
        set_error(NamReadErrorCode::malformed_json,
                  "JSON exponent must contain at least one digit");
        return false;
      }
      while (peek() >= '0' && peek() <= '9') {
        token.push_back(static_cast<char>(get()));
      }
    }

    const auto begin = token.data();
    const auto end = token.data() + token.size();
    const auto result = std::from_chars(begin, end, output, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end) {
      set_error(NamReadErrorCode::malformed_json, "Unable to parse JSON number");
      return false;
    }
    return true;
  }

  [[nodiscard]] bool try_consume_literal(const std::string_view literal) {
    skip_whitespace();
    for (std::size_t index = 0; index < literal.size(); ++index) {
      if (peek() != literal[index]) {
        return false;
      }
      static_cast<void>(get());
    }
    return true;
  }

  [[nodiscard]] bool skip_value(const std::size_t depth) {
    if (!check_depth(depth)) {
      return false;
    }
    skip_whitespace();
    const int value = peek();
    if (value == '"') {
      std::string ignored;
      return parse_string(ignored);
    }
    if (value == '{') {
      static_cast<void>(get());
      skip_whitespace();
      if (consume('}')) {
        return true;
      }
      while (true) {
        std::string ignored_key;
        if (!parse_string(ignored_key) || !expect(':', "after an object key") ||
            !skip_value(depth + 1)) {
          return false;
        }
        if (consume('}')) {
          return true;
        }
        if (!expect(',', "between object members")) {
          return false;
        }
      }
    }
    if (value == '[') {
      static_cast<void>(get());
      skip_whitespace();
      if (consume(']')) {
        return true;
      }
      while (true) {
        if (!skip_value(depth + 1)) {
          return false;
        }
        if (consume(']')) {
          return true;
        }
        if (!expect(',', "between array elements")) {
          return false;
        }
      }
    }
    if (value == 't') {
      return consume_exact_literal("true");
    }
    if (value == 'f') {
      return consume_exact_literal("false");
    }
    if (value == 'n') {
      return consume_exact_literal("null");
    }
    double ignored = 0.0;
    return parse_number(ignored);
  }

  [[nodiscard]] bool consume_exact_literal(const std::string_view literal) {
    for (const char expected : literal) {
      if (get() != expected) {
        set_error(NamReadErrorCode::malformed_json,
                  "Invalid JSON literal; expected '" + std::string(literal) + "'");
        return false;
      }
    }
    return true;
  }

  std::istream& stream_;
  const NamReadLimits& limits_;
  std::uintmax_t offset_{};
  std::optional<NamReadError> error_;
};

[[nodiscard]] NamReadResult file_error(const NamReadErrorCode code, std::string message) {
  return {.metadata = std::nullopt, .error = NamReadError{code, 0, std::move(message)}};
}

}  // namespace

const char* to_string(const NamReadErrorCode code) noexcept {
  switch (code) {
    case NamReadErrorCode::none:
      return "none";
    case NamReadErrorCode::file_not_found:
      return "file_not_found";
    case NamReadErrorCode::not_a_regular_file:
      return "not_a_regular_file";
    case NamReadErrorCode::file_too_large:
      return "file_too_large";
    case NamReadErrorCode::open_failed:
      return "open_failed";
    case NamReadErrorCode::malformed_json:
      return "malformed_json";
    case NamReadErrorCode::nesting_too_deep:
      return "nesting_too_deep";
    case NamReadErrorCode::string_too_long:
      return "string_too_long";
    case NamReadErrorCode::missing_required_field:
      return "missing_required_field";
  }
  return "unknown";
}

NamReadResult read_metadata(const std::filesystem::path& path, const NamReadLimits& limits) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error) {
    if (error == std::errc::no_such_file_or_directory) {
      return file_error(NamReadErrorCode::file_not_found, "NAM file does not exist");
    }
    return file_error(NamReadErrorCode::open_failed,
                      "Unable to inspect NAM file: " + error.message());
  }
  if (!std::filesystem::is_regular_file(status)) {
    return file_error(NamReadErrorCode::not_a_regular_file,
                      "NAM path is not a regular file");
  }

  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return file_error(NamReadErrorCode::open_failed,
                      "Unable to read NAM file size: " + error.message());
  }
  if (size > limits.max_file_bytes) {
    return file_error(NamReadErrorCode::file_too_large,
                      "NAM file exceeds the configured size limit");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return file_error(NamReadErrorCode::open_failed, "Unable to open NAM file");
  }

  JsonProbe probe(stream, limits);
  return probe.parse();
}

}  // namespace brknam::nam
