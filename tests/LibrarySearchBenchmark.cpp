// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/library/LibraryDatabase.hpp"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kRecordCount = 25000;
constexpr int kMeasuredQueries = 25;
constexpr auto kMaximumAverageSearch = std::chrono::milliseconds(100);

void check(sqlite3* database, const int result, const std::string& context) {
  if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
    throw std::runtime_error(context + ": " + sqlite3_errmsg(database));
  }
}

struct TempDatabase {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("brknam-search-benchmark-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".sqlite3");

  ~TempDatabase() {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
  }
};

void seed_records(const std::filesystem::path& path) {
  sqlite3* database = nullptr;
  const int open_result = sqlite3_open(path.string().c_str(), &database);
  if (open_result != SQLITE_OK) {
    const std::string message =
        database != nullptr ? sqlite3_errmsg(database)
                            : "SQLite could not allocate a database handle";
    if (database != nullptr) {
      sqlite3_close(database);
    }
    throw std::runtime_error("open benchmark database: " + message);
  }

  try {
    check(database, sqlite3_exec(database, "PRAGMA synchronous = OFF;"
                                           "BEGIN IMMEDIATE;"
                                           "INSERT INTO library_roots("
                                           "id,path,name,enabled"
                                           ") VALUES(1,'/synthetic','Synthetic',1);",
                                 nullptr, nullptr, nullptr),
          "begin benchmark seed");

    sqlite3_stmt* statement = nullptr;
    check(database,
          sqlite3_prepare_v2(
              database,
              "INSERT INTO assets("
              "root_id,path,relative_path,kind,size_bytes,modified_ticks,"
              "seen_generation,missing,parse_status,display_name,modeled_by,"
              "gear_make,gear_model,gear_type,tone_type,tags_text,"
              "created_at,updated_at"
              ") VALUES(1,?,?,?,?,?,1,0,1,?,?,?,?,?,?,?,1,1)",
              -1, &statement, nullptr),
          "prepare benchmark insert");

    for (int index = 0; index < kRecordCount; ++index) {
      const auto number = std::to_string(index);
      const auto relative = "Vendor" + std::to_string(index % 50) +
                            "/Model" + number + ".nam";
      const auto absolute = "/synthetic/" + relative;
      const auto display =
          index == kRecordCount - 1
              ? "UltraRareTarget Model " + number
              : "Synthetic Model " + number;
      const auto modeled_by = "Creator" + std::to_string(index % 200);
      const auto make = "Make" + std::to_string(index % 40);
      const auto model = "Amp" + std::to_string(index % 500);
      const auto tone = index % 2 == 0 ? "clean" : "crunch";
      const auto tags = index % 10 == 0 ? "favorite live" : "";

      sqlite3_reset(statement);
      sqlite3_clear_bindings(statement);
      check(database, sqlite3_bind_text(statement, 1, absolute.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind absolute path");
      check(database, sqlite3_bind_text(statement, 2, relative.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind relative path");
      check(database, sqlite3_bind_int(statement, 3, 0), "bind kind");
      check(database, sqlite3_bind_int64(statement, 4, 1024 + index),
            "bind size");
      check(database, sqlite3_bind_int64(statement, 5, index),
            "bind modified time");
      check(database, sqlite3_bind_text(statement, 6, display.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind display name");
      check(database, sqlite3_bind_text(statement, 7, modeled_by.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind creator");
      check(database, sqlite3_bind_text(statement, 8, make.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind make");
      check(database, sqlite3_bind_text(statement, 9, model.c_str(), -1,
                                        SQLITE_TRANSIENT),
            "bind model");
      check(database, sqlite3_bind_text(statement, 10, "amp", -1,
                                        SQLITE_STATIC),
            "bind gear type");
      check(database, sqlite3_bind_text(statement, 11, tone, -1,
                                        SQLITE_STATIC),
            "bind tone type");
      check(database, sqlite3_bind_text(statement, 12, tags, -1,
                                        SQLITE_TRANSIENT),
            "bind tags");
      check(database, sqlite3_step(statement), "insert benchmark asset");
    }

    sqlite3_finalize(statement);
    check(database, sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr),
          "commit benchmark seed");
    sqlite3_close(database);
  } catch (...) {
    if (database != nullptr) {
      sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
      sqlite3_close(database);
    }
    throw;
  }
}

}  // namespace

int main() {
  try {
    TempDatabase temp;
    {
      brknam::library::LibraryDatabase create_schema(temp.path);
      if (create_schema.schema_version() != 3) {
        throw std::runtime_error("benchmark requires schema version 3");
      }
    }
    seed_records(temp.path);

    brknam::library::LibraryDatabase database(temp.path);
    brknam::library::SearchOptions options;
    options.limit = 10;

    const auto warmup = database.search("UltraRareTarget", options);
    if (warmup.size() != 1 ||
        warmup.front().display_name.find("UltraRareTarget") ==
            std::string::npos) {
      throw std::runtime_error("benchmark fixture search returned wrong data");
    }

    const auto started = std::chrono::steady_clock::now();
    for (int index = 0; index < kMeasuredQueries; ++index) {
      const auto result = database.search("UltraRareTarget", options);
      if (result.size() != 1) {
        throw std::runtime_error("measured search returned wrong result count");
      }
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto average =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed) /
        kMeasuredQueries;

    std::cout << "records=" << kRecordCount
              << " measured_queries=" << kMeasuredQueries
              << " average_search_us=" << average.count()
              << " threshold_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     kMaximumAverageSearch)
                     .count()
              << '\n';

    if (average > kMaximumAverageSearch) {
      throw std::runtime_error(
          "25,000-record FTS search exceeded the 100 ms average threshold");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
