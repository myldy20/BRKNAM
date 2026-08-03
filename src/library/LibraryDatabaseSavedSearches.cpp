// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "LibraryDatabaseInternal.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace brknam::library {

using detail::Statement;
using detail::required_text;
using detail::unix_time_now;

namespace {

[[nodiscard]] bool blank_name(const std::string& name) {
  return std::all_of(name.begin(), name.end(), [](const unsigned char value) {
    return std::isspace(value) != 0;
  });
}

[[nodiscard]] SearchOptions checked_options(const SearchOptions& options) {
  if (options.limit == 0 || options.limit > 1000) {
    throw LibraryDatabaseError(
        "Saved-search result limit must be between 1 and 1000");
  }
  return options;
}

[[nodiscard]] SavedSearchRecord read_saved_search(sqlite3_stmt* statement) {
  SavedSearchRecord record;
  record.id = sqlite3_column_int64(statement, 0);
  record.name = required_text(statement, 1);
  record.query = required_text(statement, 2);
  record.options.include_missing = sqlite3_column_int(statement, 3) != 0;
  record.options.limit =
      static_cast<std::size_t>(sqlite3_column_int64(statement, 4));
  return record;
}

}  // namespace

SavedSearchRecord LibraryDatabase::save_search(
    std::string name,
    std::string query,
    const SearchOptions& requested_options) {
  if (name.empty() || blank_name(name)) {
    throw LibraryDatabaseError("Saved-search name must not be empty");
  }
  const auto options = checked_options(requested_options);
  const auto now = unix_time_now();

  Statement statement(impl_->database, R"SQL(
    INSERT INTO saved_searches(
      name, query, include_missing, result_limit, created_at, updated_at
    ) VALUES(?, ?, ?, ?, ?, ?)
    ON CONFLICT(name) DO UPDATE SET
      query = excluded.query,
      include_missing = excluded.include_missing,
      result_limit = excluded.result_limit,
      updated_at = excluded.updated_at
    RETURNING id, name, query, include_missing, result_limit
  )SQL");
  statement.bind(1, name);
  statement.bind(2, query);
  statement.bind(3, options.include_missing ? 1 : 0);
  statement.bind(4, static_cast<std::int64_t>(options.limit));
  statement.bind(5, now);
  statement.bind(6, now);

  if (statement.step() != SQLITE_ROW) {
    throw LibraryDatabaseError("Unable to save library search");
  }
  return read_saved_search(statement.get());
}

std::vector<SavedSearchRecord> LibraryDatabase::saved_searches() const {
  Statement statement(impl_->database, R"SQL(
    SELECT id, name, query, include_missing, result_limit
    FROM saved_searches
    ORDER BY name COLLATE NOCASE, id
  )SQL");

  std::vector<SavedSearchRecord> result;
  while (statement.step() == SQLITE_ROW) {
    result.push_back(read_saved_search(statement.get()));
  }
  return result;
}

std::vector<AssetRecord> LibraryDatabase::run_saved_search(
    const std::int64_t saved_search_id) const {
  Statement statement(impl_->database, R"SQL(
    SELECT id, name, query, include_missing, result_limit
    FROM saved_searches WHERE id = ?
  )SQL");
  statement.bind(1, saved_search_id);
  if (statement.step() != SQLITE_ROW) {
    throw LibraryDatabaseError("Unknown saved-search id");
  }

  const auto saved = read_saved_search(statement.get());
  return search(saved.query, saved.options);
}

void LibraryDatabase::remove_saved_search(
    const std::int64_t saved_search_id) {
  Statement statement(impl_->database,
                      "DELETE FROM saved_searches WHERE id = ?");
  statement.bind(1, saved_search_id);
  static_cast<void>(statement.step());
  if (sqlite3_changes(impl_->database) == 0) {
    throw LibraryDatabaseError("Unknown saved-search id");
  }
}

}  // namespace brknam::library
