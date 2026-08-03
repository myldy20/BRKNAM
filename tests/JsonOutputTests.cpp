// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "../tools/JsonOutput.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main() {
  try {
    namespace json = brknam::tools::json;
    require(json::quote("a\"b\\c\n\t") == "\"a\\\"b\\\\c\\n\\t\"",
            "JSON string escaping must remain stable");
    require(json::quote(std::string("x\x01y", 3)) == "\"x\\u0001y\"",
            "JSON control bytes must use Unicode escapes");

    brknam::library::SavedSearchRecord saved;
    saved.id = 7;
    saved.name = "Heavy";
    saved.query = "5150 crunch";
    saved.options.include_missing = true;
    saved.options.limit = 25;
    require(
        json::saved_search(saved) ==
            "{\"id\":7,\"name\":\"Heavy\",\"query\":\"5150 crunch\","
            "\"include_missing\":true,\"limit\":25}",
        "saved-search JSON field order is part of the CLI contract");

    brknam::library::AssetRecord asset;
    asset.id = 3;
    asset.root_id = 1;
    asset.kind = brknam::library::AssetKind::nam_model;
    asset.display_name = "Test";
    asset.path = "/tmp/Test.nam";
    asset.relative_path = "Test.nam";
    asset.size_bytes = 42;
    const auto encoded = json::asset(asset);
    require(encoded.find("\"id\":3") != std::string::npos,
            "asset JSON should include the id");
    require(encoded.find("\"content_sha256\":null") != std::string::npos,
            "asset JSON should encode missing optional strings as null");
    require(encoded.find("\"tags\":[]") != std::string::npos,
            "asset JSON should encode empty arrays explicitly");

    require(
        json::error("invalid_arguments", "bad \"input\"") ==
            "{\"ok\":false,\"error\":{\"code\":\"invalid_arguments\","
            "\"message\":\"bad \\\"input\\\"\"}}",
        "machine-readable diagnostics must remain stable");

    std::cout << "JsonOutputTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
