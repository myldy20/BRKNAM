// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/library/LibraryDatabase.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage() {
  std::cerr << "Usage:\n"
            << "  brknam-library <database> index <library-root> [root-name]\n"
            << "  brknam-library <database> search [query]\n"
            << "  brknam-library <database> hash <asset-id>\n"
            << "  brknam-library <database> duplicates <asset-id>\n";
}

std::int64_t parse_asset_id(const char* value) {
  std::size_t parsed = 0;
  const std::string text(value);
  const auto id = std::stoll(text, &parsed);
  if (parsed != text.size() || id <= 0) {
    throw std::invalid_argument("asset id must be a positive integer");
  }
  return id;
}

int index_root(const std::filesystem::path& database_path,
               const std::filesystem::path& root_path,
               const std::string& root_name) {
  brknam::library::LibraryDatabase database(database_path);
  const auto root = database.add_or_update_root(root_path, root_name);
  const auto stats = database.refresh_root(root.id);
  std::cout << "Indexed " << root.path << '\n'
            << "discovered=" << stats.discovered << '\n'
            << "inserted=" << stats.inserted << '\n'
            << "updated=" << stats.updated << '\n'
            << "moved=" << stats.moved << '\n'
            << "unchanged=" << stats.unchanged << '\n'
            << "parse_errors=" << stats.parse_errors << '\n'
            << "newly_missing=" << stats.newly_missing << '\n';
  for (const auto& issue : stats.scan_issues) {
    std::cerr << "warning: " << issue.path << ": " << issue.message << '\n';
  }
  return 0;
}

int search_library(const std::filesystem::path& database_path,
                   const std::string& query) {
  brknam::library::LibraryDatabase database(database_path);
  for (const auto& asset : database.search(query)) {
    std::cout << asset.id << '\t' << brknam::library::to_string(asset.kind)
              << '\t' << (asset.favorite ? "★" : " ") << '\t'
              << asset.display_name << '\t' << asset.relative_path << '\n';
  }
  return 0;
}

int hash_asset(const std::filesystem::path& database_path,
               const std::int64_t asset_id) {
  brknam::library::LibraryDatabase database(database_path);
  std::cout << database.ensure_sha256(asset_id) << '\n';
  return 0;
}

int print_duplicates(const std::filesystem::path& database_path,
                     const std::int64_t asset_id) {
  brknam::library::LibraryDatabase database(database_path);
  const auto duplicates = database.duplicate_group(asset_id);
  if (duplicates.empty()) {
    std::cout << "No byte-identical duplicates found\n";
    return 0;
  }
  for (const auto& asset : duplicates) {
    std::cout << asset.id << '\t' << asset.relative_path << '\t'
              << asset.content_sha256.value_or("") << '\n';
  }
  return 0;
}

}  // namespace

int main(const int argc, char** argv) {
  try {
    if (argc < 3) {
      print_usage();
      return 2;
    }

    const std::filesystem::path database_path = argv[1];
    const std::string command = argv[2];
    if (command == "index" && argc >= 4) {
      return index_root(database_path, argv[3], argc >= 5 ? argv[4] : "");
    }
    if (command == "search") {
      return search_library(database_path, argc >= 4 ? argv[3] : "");
    }
    if (command == "hash" && argc == 4) {
      return hash_asset(database_path, parse_asset_id(argv[3]));
    }
    if (command == "duplicates" && argc == 4) {
      return print_duplicates(database_path, parse_asset_id(argv[3]));
    }

    print_usage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "brknam-library: " << error.what() << '\n';
    return 1;
  }
}
