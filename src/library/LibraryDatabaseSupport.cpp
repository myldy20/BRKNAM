// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "LibraryDatabaseInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace brknam::library::detail {

std::string path_to_utf8(const std::filesystem::path& path) {
  const auto value = path.generic_u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path path_from_utf8(const std::string_view value) {
  const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()),
                           value.size());
  return std::filesystem::path(utf8);
}

std::filesystem::path normalize_path(const std::filesystem::path& path) {
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return normalized;
  }
  error.clear();
  normalized = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : normalized.lexically_normal();
}

std::int64_t modified_ticks(const std::filesystem::path& path) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(path, error);
  if (error) {
    throw LibraryDatabaseError("Unable to read modification time for '" +
                               path_to_utf8(path) + "': " + error.message());
  }
  const auto ticks = time.time_since_epoch().count();
  if (ticks > std::numeric_limits<std::int64_t>::max() ||
      ticks < std::numeric_limits<std::int64_t>::min()) {
    throw LibraryDatabaseError(
        "Modification timestamp is outside SQLite integer range");
  }
  return static_cast<std::int64_t>(ticks);
}

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int kind_to_int(const AssetKind kind) noexcept {
  return kind == AssetKind::nam_model ? 0 : 1;
}

AssetKind int_to_kind(const int value) {
  if (value == 0) {
    return AssetKind::nam_model;
  }
  if (value == 1) {
    return AssetKind::impulse_response;
  }
  throw LibraryDatabaseError("Database contains an unknown asset kind");
}

std::string default_root_name(const std::filesystem::path& path) {
  auto name = path.filename();
  if (name.empty()) {
    name = path.root_name();
  }
  auto value = path_to_utf8(name);
  return value.empty() ? path_to_utf8(path) : value;
}

std::string display_name_for(const LibraryAsset& asset,
                             const nam::NamReadResult& metadata) {
  if (metadata.metadata.has_value() && metadata.metadata->name.has_value() &&
      !metadata.metadata->name->empty()) {
    return *metadata.metadata->name;
  }
  return path_to_utf8(asset.path.stem());
}

namespace {

[[nodiscard]] std::string sqlite_message(sqlite3* database,
                                         const std::string_view context) {
  return std::string(context) + ": " + sqlite3_errmsg(database);
}

void check_result(sqlite3* database, const int result,
                  const std::string_view context) {
  if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
    throw LibraryDatabaseError(sqlite_message(database, context));
  }
}

void create_saved_searches_table(sqlite3* database) {
  exec(database, R"SQL(
    CREATE TABLE saved_searches (
      id INTEGER PRIMARY KEY,
      name TEXT NOT NULL UNIQUE COLLATE NOCASE,
      query TEXT NOT NULL,
      include_missing INTEGER NOT NULL DEFAULT 0
        CHECK(include_missing IN (0, 1)),
      result_limit INTEGER NOT NULL DEFAULT 100
        CHECK(result_limit BETWEEN 1 AND 1000),
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
  )SQL");
}

void create_schema_v3(sqlite3* database) {
  exec(database, R"SQL(
    CREATE TABLE library_roots (
      id INTEGER PRIMARY KEY,
      path TEXT NOT NULL UNIQUE,
      name TEXT NOT NULL,
      enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
      scan_generation INTEGER NOT NULL DEFAULT 0,
      last_scan_started_at INTEGER,
      last_scan_completed_at INTEGER
    );

    CREATE TABLE assets (
      id INTEGER PRIMARY KEY,
      root_id INTEGER NOT NULL REFERENCES library_roots(id) ON DELETE CASCADE,
      path TEXT NOT NULL,
      relative_path TEXT NOT NULL,
      kind INTEGER NOT NULL CHECK(kind IN (0, 1)),
      size_bytes INTEGER NOT NULL,
      modified_ticks INTEGER NOT NULL,
      content_sha256 TEXT CHECK(content_sha256 IS NULL OR length(content_sha256) = 64),
      seen_generation INTEGER NOT NULL DEFAULT 0,
      missing INTEGER NOT NULL DEFAULT 0 CHECK(missing IN (0, 1)),
      parse_status INTEGER NOT NULL DEFAULT 0 CHECK(parse_status IN (0, 1, 2)),
      parse_error TEXT,
      display_name TEXT NOT NULL,
      file_version TEXT,
      architecture TEXT,
      sample_rate_hz REAL,
      modeled_by TEXT,
      gear_make TEXT,
      gear_model TEXT,
      gear_type TEXT,
      tone_type TEXT,
      input_level_dbu REAL,
      output_level_dbu REAL,
      tags_text TEXT NOT NULL DEFAULT '',
      favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN (0, 1)),
      rating INTEGER CHECK(rating BETWEEN 1 AND 5),
      last_used_at INTEGER,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      UNIQUE(root_id, relative_path)
    );

    CREATE INDEX assets_root_missing_idx ON assets(root_id, missing);
    CREATE INDEX assets_favorite_idx ON assets(favorite, display_name);
    CREATE INDEX assets_recent_idx ON assets(last_used_at DESC);
    CREATE INDEX assets_sha256_idx ON assets(content_sha256)
      WHERE content_sha256 IS NOT NULL;

    CREATE VIRTUAL TABLE asset_fts USING fts5(
      display_name,
      relative_path,
      modeled_by,
      gear_make,
      gear_model,
      gear_type,
      tone_type,
      tags_text,
      tokenize = 'unicode61 remove_diacritics 2'
    );

    CREATE TRIGGER assets_fts_insert AFTER INSERT ON assets BEGIN
      INSERT INTO asset_fts(rowid, display_name, relative_path, modeled_by,
                            gear_make, gear_model, gear_type, tone_type, tags_text)
      VALUES (new.id, new.display_name, new.relative_path, new.modeled_by,
              new.gear_make, new.gear_model, new.gear_type, new.tone_type,
              new.tags_text);
    END;

    CREATE TRIGGER assets_fts_update AFTER UPDATE OF display_name, relative_path,
      modeled_by, gear_make, gear_model, gear_type, tone_type, tags_text ON assets BEGIN
      DELETE FROM asset_fts WHERE rowid = old.id;
      INSERT INTO asset_fts(rowid, display_name, relative_path, modeled_by,
                            gear_make, gear_model, gear_type, tone_type, tags_text)
      VALUES (new.id, new.display_name, new.relative_path, new.modeled_by,
              new.gear_make, new.gear_model, new.gear_type, new.tone_type,
              new.tags_text);
    END;

    CREATE TRIGGER assets_fts_delete AFTER DELETE ON assets BEGIN
      DELETE FROM asset_fts WHERE rowid = old.id;
    END;

    CREATE TABLE tags (
      id INTEGER PRIMARY KEY,
      name TEXT NOT NULL UNIQUE COLLATE NOCASE
    );

    CREATE TABLE asset_tags (
      asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
      tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
      PRIMARY KEY(asset_id, tag_id)
    );
  )SQL");
  create_saved_searches_table(database);
  exec(database, "PRAGMA user_version = 3");
}

}  // namespace

Statement::Statement(sqlite3* database, const std::string_view sql)
    : database_(database) {
  const auto result =
      sqlite3_prepare_v2(database_, sql.data(), static_cast<int>(sql.size()),
                         &statement_, nullptr);
  check_result(database_, result, "Unable to prepare SQLite statement");
}

Statement::~Statement() {
  sqlite3_finalize(statement_);
}

sqlite3_stmt* Statement::get() const noexcept {
  return statement_;
}

void Statement::reset() {
  check_result(database_, sqlite3_reset(statement_),
               "Unable to reset SQLite statement");
  check_result(database_, sqlite3_clear_bindings(statement_),
               "Unable to clear SQLite bindings");
}

int Statement::step() {
  const int result = sqlite3_step(statement_);
  if (result != SQLITE_ROW && result != SQLITE_DONE) {
    check_result(database_, result, "Unable to execute SQLite statement");
  }
  return result;
}

void Statement::bind(const int index, const std::string_view value) {
  check_result(database_,
               sqlite3_bind_text(statement_, index, value.data(),
                                 static_cast<int>(value.size()), SQLITE_TRANSIENT),
               "Unable to bind text");
}

void Statement::bind(const int index, const std::int64_t value) {
  check_result(database_, sqlite3_bind_int64(statement_, index, value),
               "Unable to bind integer");
}

void Statement::bind(const int index, const int value) {
  check_result(database_, sqlite3_bind_int(statement_, index, value),
               "Unable to bind integer");
}

void Statement::bind(const int index, const double value) {
  check_result(database_, sqlite3_bind_double(statement_, index, value),
               "Unable to bind number");
}

void Statement::bind_null(const int index) {
  check_result(database_, sqlite3_bind_null(statement_, index),
               "Unable to bind null");
}

void exec(sqlite3* database, const std::string_view sql) {
  char* error_message = nullptr;
  const auto result = sqlite3_exec(database, std::string(sql).c_str(), nullptr,
                                   nullptr, &error_message);
  if (result != SQLITE_OK) {
    std::string message =
        error_message != nullptr ? error_message : sqlite3_errmsg(database);
    sqlite3_free(error_message);
    throw LibraryDatabaseError(message);
  }
}

Transaction::Transaction(sqlite3* database) : database_(database) {
  exec(database_, "BEGIN IMMEDIATE");
}

Transaction::~Transaction() {
  if (!committed_) {
    sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
  }
}

void Transaction::commit() {
  exec(database_, "COMMIT");
  committed_ = true;
}

std::optional<std::string> optional_text(sqlite3_stmt* statement,
                                         const int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
    return std::nullopt;
  }
  const auto* text =
      reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
  const auto size = sqlite3_column_bytes(statement, column);
  return std::string(text, static_cast<std::size_t>(size));
}

std::string required_text(sqlite3_stmt* statement, const int column) {
  return optional_text(statement, column).value_or(std::string{});
}

std::optional<double> optional_double(sqlite3_stmt* statement,
                                      const int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_double(statement, column);
}

std::optional<int> optional_int(sqlite3_stmt* statement, const int column) {
  if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_int(statement, column);
}

void bind_optional(Statement& statement, const int index,
                   const std::optional<std::string>& value) {
  if (value.has_value()) {
    statement.bind(index, *value);
  } else {
    statement.bind_null(index);
  }
}

void bind_optional(Statement& statement, const int index,
                   const std::optional<double>& value) {
  if (value.has_value()) {
    statement.bind(index, *value);
  } else {
    statement.bind_null(index);
  }
}

std::string make_fts_query(const std::string_view query) {
  std::istringstream input{std::string(query)};
  std::string token;
  std::string result;
  while (input >> token) {
    token.erase(
        std::remove_if(token.begin(), token.end(),
                       [](const unsigned char character) {
                         return std::iscntrl(character) != 0;
                       }),
        token.end());
    if (token.empty()) {
      continue;
    }

    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
      if (character == '"') {
        escaped += "\"\"";
      } else {
        escaped.push_back(character);
      }
    }
    if (!result.empty()) {
      result += " AND ";
    }
    result += '"' + escaped + '"';
    result += '*';
  }
  return result;
}

std::filesystem::path relative_to_root(const std::filesystem::path& asset,
                                       const std::filesystem::path& root) {
  std::error_code error;
  auto relative = std::filesystem::relative(asset, root, error);
  if (!error && !relative.empty() &&
      relative != std::filesystem::path(".")) {
    return relative.lexically_normal();
  }
  return asset.filename();
}

}  // namespace brknam::library::detail

namespace brknam::library {

using detail::Statement;
using detail::Transaction;
using detail::exec;
using detail::int_to_kind;
using detail::optional_double;
using detail::optional_int;
using detail::optional_text;
using detail::path_from_utf8;
using detail::path_to_utf8;
using detail::required_text;

LibraryDatabase::Impl::Impl(const std::filesystem::path& path) {
  const auto utf8_path = path_to_utf8(path);
  const int result = sqlite3_open_v2(
      utf8_path.c_str(), &database,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (result != SQLITE_OK) {
    std::string message =
        database != nullptr ? sqlite3_errmsg(database)
                            : "SQLite could not allocate a database handle";
    if (database != nullptr) {
      sqlite3_close(database);
      database = nullptr;
    }
    throw LibraryDatabaseError("Unable to open library database: " + message);
  }

  sqlite3_busy_timeout(database, 5000);
  exec(database, "PRAGMA foreign_keys = ON");
  exec(database, "PRAGMA journal_mode = WAL");
  exec(database, "PRAGMA synchronous = NORMAL");
  migrate();
}

LibraryDatabase::Impl::~Impl() {
  if (database != nullptr) {
    sqlite3_close(database);
  }
}

void LibraryDatabase::Impl::migrate() {
  int version = 0;
  {
    Statement statement(database, "PRAGMA user_version");
    if (statement.step() != SQLITE_ROW) {
      throw LibraryDatabaseError("Unable to read database schema version");
    }
    version = sqlite3_column_int(statement.get(), 0);
  }

  if (version > detail::kCurrentSchemaVersion) {
    throw LibraryDatabaseError(
        "Library database was created by a newer BRKNAM version");
  }
  if (version == detail::kCurrentSchemaVersion) {
    return;
  }

  if (version == 0) {
    Transaction transaction(database);
    detail::create_schema_v3(database);
    transaction.commit();
    return;
  }

  if (version == 1) {
    Transaction transaction(database);
    exec(database, R"SQL(
      ALTER TABLE assets ADD COLUMN content_sha256 TEXT
        CHECK(content_sha256 IS NULL OR length(content_sha256) = 64);
      CREATE INDEX assets_sha256_idx ON assets(content_sha256)
        WHERE content_sha256 IS NOT NULL;
      PRAGMA user_version = 2;
    )SQL");
    transaction.commit();
    version = 2;
  }

  if (version == 2) {
    Transaction transaction(database);
    detail::create_saved_searches_table(database);
    exec(database, "PRAGMA user_version = 3");
    transaction.commit();
    return;
  }

  throw LibraryDatabaseError("No migration path for library database");
}

std::vector<std::string> LibraryDatabase::Impl::tags_for(
    const std::int64_t asset_id) const {
  Statement statement(database,
                      "SELECT tags.name FROM tags "
                      "JOIN asset_tags ON asset_tags.tag_id = tags.id "
                      "WHERE asset_tags.asset_id = ? "
                      "ORDER BY tags.name COLLATE NOCASE");
  statement.bind(1, asset_id);
  std::vector<std::string> tags;
  while (statement.step() == SQLITE_ROW) {
    tags.push_back(required_text(statement.get(), 0));
  }
  return tags;
}

AssetRecord LibraryDatabase::Impl::read_asset(sqlite3_stmt* statement) const {
  AssetRecord asset;
  asset.id = sqlite3_column_int64(statement, 0);
  asset.root_id = sqlite3_column_int64(statement, 1);
  asset.path = path_from_utf8(required_text(statement, 2));
  asset.relative_path = path_from_utf8(required_text(statement, 3));
  asset.kind = int_to_kind(sqlite3_column_int(statement, 4));
  asset.size_bytes =
      static_cast<std::uintmax_t>(sqlite3_column_int64(statement, 5));
  asset.modified_ticks = sqlite3_column_int64(statement, 6);
  asset.content_sha256 = optional_text(statement, 7);
  asset.missing = sqlite3_column_int(statement, 8) != 0;
  asset.parse_status =
      static_cast<ParseStatus>(sqlite3_column_int(statement, 9));
  asset.parse_error = optional_text(statement, 10);
  asset.display_name = required_text(statement, 11);
  asset.file_version = optional_text(statement, 12);
  asset.architecture = optional_text(statement, 13);
  asset.sample_rate_hz = optional_double(statement, 14);
  asset.modeled_by = optional_text(statement, 15);
  asset.gear_make = optional_text(statement, 16);
  asset.gear_model = optional_text(statement, 17);
  asset.gear_type = optional_text(statement, 18);
  asset.tone_type = optional_text(statement, 19);
  asset.input_level_dbu = optional_double(statement, 20);
  asset.output_level_dbu = optional_double(statement, 21);
  asset.favorite = sqlite3_column_int(statement, 22) != 0;
  asset.rating = optional_int(statement, 23);
  asset.tags = tags_for(asset.id);
  return asset;
}

}  // namespace brknam::library
