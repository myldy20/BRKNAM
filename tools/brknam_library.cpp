// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "JsonOutput.hpp"
#include "brknam/library/LibraryDatabase.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace json = brknam::tools::json;
namespace library = brknam::library;

void print_usage() {
  std::cerr
      << "Usage:\n"
      << "  brknam-library [--json] <database> index <library-root> [root-name]\n"
      << "  brknam-library [--json] <database> search [query]\n"
      << "  brknam-library [--json] <database> hash <asset-id>\n"
      << "  brknam-library [--json] <database> duplicates <asset-id>\n"
      << "  brknam-library [--json] <database> save-search <name> <query> "
         "[include-missing:0|1] [limit]\n"
      << "  brknam-library [--json] <database> list-searches\n"
      << "  brknam-library [--json] <database> run-search <saved-search-id>\n"
      << "  brknam-library [--json] <database> delete-search <saved-search-id>\n";
}

std::int64_t parse_positive_id(const char* value,
                               const std::string_view label) {
  std::size_t parsed = 0;
  const std::string text(value);
  const auto id = std::stoll(text, &parsed);
  if (parsed != text.size() || id <= 0) {
    throw std::invalid_argument(std::string(label) +
                                " must be a positive integer");
  }
  return id;
}

std::size_t parse_limit(const char* value) {
  const auto parsed = parse_positive_id(value, "limit");
  if (parsed > 1000) {
    throw std::invalid_argument("limit must be between 1 and 1000");
  }
  return static_cast<std::size_t>(parsed);
}

bool parse_boolean(const char* value) {
  const std::string text(value);
  if (text == "1" || text == "true") {
    return true;
  }
  if (text == "0" || text == "false") {
    return false;
  }
  throw std::invalid_argument(
      "include-missing must be 0, 1, false, or true");
}

int index_root(const std::filesystem::path& database_path,
               const std::filesystem::path& root_path,
               const std::string& root_name,
               const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto root = database.add_or_update_root(root_path, root_name);
  const auto stats = database.refresh_root(root.id);

  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"index\",\"root\":{"
              << "\"id\":" << root.id << ','
              << "\"name\":" << json::quote(root.name) << ','
              << "\"path\":" << json::quote(json::path_utf8(root.path))
              << "},\"stats\":{"
              << "\"discovered\":" << stats.discovered << ','
              << "\"inserted\":" << stats.inserted << ','
              << "\"updated\":" << stats.updated << ','
              << "\"moved\":" << stats.moved << ','
              << "\"unchanged\":" << stats.unchanged << ','
              << "\"parse_errors\":" << stats.parse_errors << ','
              << "\"newly_missing\":" << stats.newly_missing
              << "},\"issues\":[";
    for (std::size_t index = 0; index < stats.scan_issues.size(); ++index) {
      if (index != 0) {
        std::cout << ',';
      }
      const auto& issue = stats.scan_issues[index];
      std::cout << "{\"path\":"
                << json::quote(json::path_utf8(issue.path))
                << ",\"message\":" << json::quote(issue.message) << '}';
    }
    std::cout << "]}\n";
    return 0;
  }

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
                   const std::string& query,
                   const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto results = database.search(query);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"search\",\"query\":"
              << json::quote(query) << ",\"assets\":"
              << json::assets(results) << "}\n";
    return 0;
  }

  for (const auto& asset : results) {
    std::cout << asset.id << '\t' << library::to_string(asset.kind)
              << '\t' << (asset.favorite ? "★" : " ") << '\t'
              << asset.display_name << '\t' << asset.relative_path << '\n';
  }
  return 0;
}

int hash_asset(const std::filesystem::path& database_path,
               const std::int64_t asset_id,
               const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto hash = database.ensure_sha256(asset_id);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"hash\",\"asset_id\":"
              << asset_id << ",\"sha256\":" << json::quote(hash) << "}\n";
  } else {
    std::cout << hash << '\n';
  }
  return 0;
}

int print_duplicates(const std::filesystem::path& database_path,
                     const std::int64_t asset_id,
                     const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto duplicates = database.duplicate_group(asset_id);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"duplicates\",\"asset_id\":"
              << asset_id << ",\"assets\":" << json::assets(duplicates)
              << "}\n";
    return 0;
  }

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

int save_search(const std::filesystem::path& database_path,
                const std::string& name,
                const std::string& query,
                const library::SearchOptions& options,
                const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto saved = database.save_search(name, query, options);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"save-search\",\"saved_search\":"
              << json::saved_search(saved) << "}\n";
  } else {
    std::cout << saved.id << '\t' << saved.name << '\t' << saved.query << '\n';
  }
  return 0;
}

int list_searches(const std::filesystem::path& database_path,
                  const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto saved = database.saved_searches();
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"list-searches\","
                 "\"saved_searches\":"
              << json::saved_searches(saved) << "}\n";
    return 0;
  }
  for (const auto& value : saved) {
    std::cout << value.id << '\t' << value.name << '\t' << value.query
              << '\t' << (value.options.include_missing ? "missing" : "present")
              << '\t' << value.options.limit << '\n';
  }
  return 0;
}

int run_search(const std::filesystem::path& database_path,
               const std::int64_t saved_search_id,
               const bool json_mode) {
  library::LibraryDatabase database(database_path);
  const auto results = database.run_saved_search(saved_search_id);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"run-search\","
                 "\"saved_search_id\":"
              << saved_search_id << ",\"assets\":" << json::assets(results)
              << "}\n";
    return 0;
  }
  for (const auto& asset : results) {
    std::cout << asset.id << '\t' << library::to_string(asset.kind)
              << '\t' << asset.display_name << '\t'
              << asset.relative_path << '\n';
  }
  return 0;
}

int delete_search(const std::filesystem::path& database_path,
                  const std::int64_t saved_search_id,
                  const bool json_mode) {
  library::LibraryDatabase database(database_path);
  database.remove_saved_search(saved_search_id);
  if (json_mode) {
    std::cout << "{\"ok\":true,\"command\":\"delete-search\","
                 "\"saved_search_id\":"
              << saved_search_id << "}\n";
  }
  return 0;
}

}  // namespace

int main(const int argc, char** argv) {
  bool json_mode = false;
  try {
    int index = 1;
    if (index < argc && std::string_view(argv[index]) == "--json") {
      json_mode = true;
      ++index;
    }
    if (argc - index < 2) {
      if (json_mode) {
        std::cout << json::error("invalid_arguments",
                                 "database and command are required")
                  << '\n';
      } else {
        print_usage();
      }
      return 2;
    }

    const std::filesystem::path database_path = argv[index++];
    if (index < argc && std::string_view(argv[index]) == "--json") {
      json_mode = true;
      ++index;
    }
    if (index >= argc) {
      throw std::invalid_argument("command is required");
    }
    const std::string command = argv[index++];

    if (command == "index" && index < argc) {
      const auto root = argv[index++];
      const std::string name = index < argc ? argv[index++] : "";
      if (index != argc) {
        throw std::invalid_argument("too many arguments for index");
      }
      return index_root(database_path, root, name, json_mode);
    }
    if (command == "search") {
      const std::string query = index < argc ? argv[index++] : "";
      if (index != argc) {
        throw std::invalid_argument("too many arguments for search");
      }
      return search_library(database_path, query, json_mode);
    }
    if (command == "hash" && argc - index == 1) {
      return hash_asset(database_path,
                        parse_positive_id(argv[index], "asset id"),
                        json_mode);
    }
    if (command == "duplicates" && argc - index == 1) {
      return print_duplicates(database_path,
                              parse_positive_id(argv[index], "asset id"),
                              json_mode);
    }
    if (command == "save-search" && argc - index >= 2 &&
        argc - index <= 4) {
      const std::string name = argv[index++];
      const std::string query = argv[index++];
      library::SearchOptions options;
      if (index < argc) {
        options.include_missing = parse_boolean(argv[index++]);
      }
      if (index < argc) {
        options.limit = parse_limit(argv[index++]);
      }
      return save_search(database_path, name, query, options, json_mode);
    }
    if (command == "list-searches" && index == argc) {
      return list_searches(database_path, json_mode);
    }
    if (command == "run-search" && argc - index == 1) {
      return run_search(
          database_path,
          parse_positive_id(argv[index], "saved-search id"),
          json_mode);
    }
    if (command == "delete-search" && argc - index == 1) {
      return delete_search(
          database_path,
          parse_positive_id(argv[index], "saved-search id"),
          json_mode);
    }

    throw std::invalid_argument("unknown command or invalid arguments");
  } catch (const std::invalid_argument& error) {
    if (json_mode) {
      std::cout << json::error("invalid_arguments", error.what()) << '\n';
    } else {
      std::cerr << "brknam-library: " << error.what() << '\n';
      print_usage();
    }
    return 2;
  } catch (const std::exception& error) {
    if (json_mode) {
      std::cout << json::error("runtime_error", error.what()) << '\n';
    } else {
      std::cerr << "brknam-library: " << error.what() << '\n';
    }
    return 1;
  }
}
