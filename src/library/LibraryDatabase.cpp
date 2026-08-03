// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "LibraryDatabaseInternal.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace brknam::library {

using detail::Statement;
using detail::Transaction;
using detail::default_root_name;
using detail::exec;
using detail::kind_to_int;
using detail::make_fts_query;
using detail::modified_ticks;
using detail::normalize_path;
using detail::path_from_utf8;
using detail::path_to_utf8;
using detail::required_text;
using detail::sha256_file;
using detail::unix_time_now;

namespace {

constexpr auto kAssetColumns =
    "id, root_id, path, relative_path, kind, size_bytes, modified_ticks, "
    "content_sha256, missing, parse_status, parse_error, display_name, "
    "file_version, architecture, sample_rate_hz, modeled_by, gear_make, "
    "gear_model, gear_type, tone_type, input_level_dbu, output_level_dbu, "
    "favorite, rating ";

constexpr auto kPrefixedAssetColumns =
    "assets.id, assets.root_id, assets.path, assets.relative_path, assets.kind, "
    "assets.size_bytes, assets.modified_ticks, assets.content_sha256, "
    "assets.missing, assets.parse_status, assets.parse_error, "
    "assets.display_name, assets.file_version, assets.architecture, "
    "assets.sample_rate_hz, assets.modeled_by, assets.gear_make, "
    "assets.gear_model, assets.gear_type, assets.tone_type, "
    "assets.input_level_dbu, assets.output_level_dbu, assets.favorite, "
    "assets.rating ";

}  // namespace

LibraryDatabase::LibraryDatabase(const std::filesystem::path& database_path)
    : impl_(std::make_unique<Impl>(database_path)) {}

LibraryDatabase::~LibraryDatabase() = default;
LibraryDatabase::LibraryDatabase(LibraryDatabase&&) noexcept = default;
LibraryDatabase& LibraryDatabase::operator=(LibraryDatabase&&) noexcept = default;

int LibraryDatabase::schema_version() const {
  Statement statement(impl_->database, "PRAGMA user_version");
  if (statement.step() != SQLITE_ROW) {
    throw LibraryDatabaseError("Unable to read database schema version");
  }
  return sqlite3_column_int(statement.get(), 0);
}

LibraryRootRecord LibraryDatabase::add_or_update_root(
    const std::filesystem::path& path,
    std::string name) {
  const auto normalized = normalize_path(path);
  const auto normalized_text = path_to_utf8(normalized);
  if (name.empty()) {
    name = default_root_name(normalized);
  }

  Statement statement(
      impl_->database,
      "INSERT INTO library_roots(path, name, enabled) VALUES(?, ?, 1) "
      "ON CONFLICT(path) DO UPDATE SET name = excluded.name, enabled = 1 "
      "RETURNING id, name, path, enabled");
  statement.bind(1, normalized_text);
  statement.bind(2, name);
  if (statement.step() != SQLITE_ROW) {
    throw LibraryDatabaseError("Unable to add library root");
  }

  return {.id = sqlite3_column_int64(statement.get(), 0),
          .name = required_text(statement.get(), 1),
          .path = path_from_utf8(required_text(statement.get(), 2)),
          .enabled = sqlite3_column_int(statement.get(), 3) != 0};
}

std::vector<LibraryRootRecord> LibraryDatabase::roots() const {
  Statement statement(
      impl_->database,
      "SELECT id, name, path, enabled FROM library_roots ORDER BY name");
  std::vector<LibraryRootRecord> result;
  while (statement.step() == SQLITE_ROW) {
    result.push_back(
        {.id = sqlite3_column_int64(statement.get(), 0),
         .name = required_text(statement.get(), 1),
         .path = path_from_utf8(required_text(statement.get(), 2)),
         .enabled = sqlite3_column_int(statement.get(), 3) != 0});
  }
  return result;
}

std::vector<AssetRecord> LibraryDatabase::search(
    std::string query,
    const SearchOptions& options) const {
  const auto bounded_limit = std::min<std::size_t>(options.limit, 1000);
  const auto fts_query = make_fts_query(query);

  std::string sql;
  if (fts_query.empty()) {
    sql = std::string("SELECT ") + kPrefixedAssetColumns +
          "FROM assets WHERE (? OR assets.missing = 0) "
          "ORDER BY assets.favorite DESC, assets.display_name COLLATE NOCASE "
          "LIMIT ?";
  } else {
    sql = std::string("SELECT ") + kPrefixedAssetColumns +
          "FROM asset_fts JOIN assets ON assets.id = asset_fts.rowid "
          "WHERE asset_fts MATCH ? AND (? OR assets.missing = 0) "
          "ORDER BY assets.favorite DESC, bm25(asset_fts), "
          "assets.display_name COLLATE NOCASE LIMIT ?";
  }

  Statement statement(impl_->database, sql);
  int index = 1;
  if (!fts_query.empty()) {
    statement.bind(index++, fts_query);
  }
  statement.bind(index++, options.include_missing ? 1 : 0);
  statement.bind(index, static_cast<std::int64_t>(bounded_limit));

  std::vector<AssetRecord> result;
  while (statement.step() == SQLITE_ROW) {
    result.push_back(impl_->read_asset(statement.get()));
  }
  return result;
}

std::optional<AssetRecord> LibraryDatabase::asset(
    const std::int64_t asset_id) const {
  Statement statement(
      impl_->database,
      std::string("SELECT ") + kAssetColumns + "FROM assets WHERE id = ?");
  statement.bind(1, asset_id);
  if (statement.step() != SQLITE_ROW) {
    return std::nullopt;
  }
  return impl_->read_asset(statement.get());
}

std::vector<AssetRecord> LibraryDatabase::recent(const std::size_t limit) const {
  Statement statement(
      impl_->database,
      std::string("SELECT ") + kAssetColumns +
          "FROM assets WHERE last_used_at IS NOT NULL "
          "ORDER BY last_used_at DESC LIMIT ?");
  statement.bind(
      1, static_cast<std::int64_t>(std::min<std::size_t>(limit, 1000)));

  std::vector<AssetRecord> result;
  while (statement.step() == SQLITE_ROW) {
    result.push_back(impl_->read_asset(statement.get()));
  }
  return result;
}

std::string LibraryDatabase::ensure_sha256(const std::int64_t asset_id) {
  const auto current = asset(asset_id);
  if (!current.has_value()) {
    throw LibraryDatabaseError("Unknown asset id");
  }
  if (current->missing) {
    throw LibraryDatabaseError("Cannot hash a missing asset");
  }

  std::error_code error;
  const auto status = std::filesystem::status(current->path, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    throw LibraryDatabaseError("Asset is unavailable; refresh its library root");
  }
  const auto actual_size = std::filesystem::file_size(current->path, error);
  if (error) {
    throw LibraryDatabaseError("Unable to read asset size before hashing");
  }
  const auto actual_modified = modified_ticks(current->path);
  if (actual_size != current->size_bytes ||
      actual_modified != current->modified_ticks) {
    throw LibraryDatabaseError(
        "Asset changed since the last scan; refresh its library root before hashing");
  }
  if (current->content_sha256.has_value()) {
    return *current->content_sha256;
  }

  const auto hash = sha256_file(current->path);

  error.clear();
  const auto size_after = std::filesystem::file_size(current->path, error);
  if (error || size_after != actual_size ||
      modified_ticks(current->path) != actual_modified) {
    throw LibraryDatabaseError("Asset changed while its SHA-256 was being calculated");
  }

  Statement update(
      impl_->database,
      "UPDATE assets SET content_sha256 = ?, updated_at = ? "
      "WHERE id = ? AND missing = 0 AND size_bytes = ? AND modified_ticks = ?");
  update.bind(1, hash);
  update.bind(2, unix_time_now());
  update.bind(3, asset_id);
  update.bind(4, static_cast<std::int64_t>(actual_size));
  update.bind(5, actual_modified);
  static_cast<void>(update.step());
  if (sqlite3_changes(impl_->database) == 0) {
    throw LibraryDatabaseError("Asset changed while its SHA-256 was being stored");
  }
  return hash;
}

std::vector<AssetRecord> LibraryDatabase::duplicate_group(
    const std::int64_t asset_id) {
  const auto target = asset(asset_id);
  if (!target.has_value()) {
    throw LibraryDatabaseError("Unknown asset id");
  }
  if (target->missing) {
    throw LibraryDatabaseError("Cannot inspect duplicates for a missing asset");
  }

  const auto hash = ensure_sha256(asset_id);

  Statement candidates(
      impl_->database,
      "SELECT id FROM assets WHERE id <> ? AND missing = 0 "
      "AND kind = ? AND size_bytes = ?");
  candidates.bind(1, asset_id);
  candidates.bind(2, kind_to_int(target->kind));
  candidates.bind(3, static_cast<std::int64_t>(target->size_bytes));

  std::vector<std::int64_t> candidate_ids;
  while (candidates.step() == SQLITE_ROW) {
    candidate_ids.push_back(sqlite3_column_int64(candidates.get(), 0));
  }
  for (const auto candidate_id : candidate_ids) {
    try {
      static_cast<void>(ensure_sha256(candidate_id));
    } catch (const LibraryDatabaseError&) {
      // A stale or concurrently changed candidate should not hide valid duplicates.
    }
  }

  Statement matches(
      impl_->database,
      std::string("SELECT ") + kAssetColumns +
          "FROM assets WHERE missing = 0 AND content_sha256 = ? "
          "ORDER BY display_name COLLATE NOCASE, path");
  matches.bind(1, hash);

  std::vector<AssetRecord> result;
  while (matches.step() == SQLITE_ROW) {
    result.push_back(impl_->read_asset(matches.get()));
  }
  if (result.size() < 2) {
    result.clear();
  }
  return result;
}

void LibraryDatabase::set_favorite(const std::int64_t asset_id,
                                   const bool favorite) {
  Statement statement(impl_->database,
                      "UPDATE assets SET favorite = ? WHERE id = ?");
  statement.bind(1, favorite ? 1 : 0);
  statement.bind(2, asset_id);
  static_cast<void>(statement.step());
  if (sqlite3_changes(impl_->database) == 0) {
    throw LibraryDatabaseError("Unknown asset id");
  }
}

void LibraryDatabase::set_rating(const std::int64_t asset_id,
                                 const std::optional<int> rating) {
  if (rating.has_value() && (*rating < 1 || *rating > 5)) {
    throw LibraryDatabaseError("Rating must be between 1 and 5");
  }

  Statement statement(impl_->database,
                      "UPDATE assets SET rating = ? WHERE id = ?");
  if (rating.has_value()) {
    statement.bind(1, *rating);
  } else {
    statement.bind_null(1);
  }
  statement.bind(2, asset_id);
  static_cast<void>(statement.step());
  if (sqlite3_changes(impl_->database) == 0) {
    throw LibraryDatabaseError("Unknown asset id");
  }
}

void LibraryDatabase::replace_tags(const std::int64_t asset_id,
                                   const std::vector<std::string>& tags) {
  if (!asset(asset_id).has_value()) {
    throw LibraryDatabaseError("Unknown asset id");
  }

  Transaction transaction(impl_->database);
  Statement remove(impl_->database,
                   "DELETE FROM asset_tags WHERE asset_id = ?");
  remove.bind(1, asset_id);
  static_cast<void>(remove.step());

  Statement create_tag(
      impl_->database,
      "INSERT INTO tags(name) VALUES(?) "
      "ON CONFLICT(name) DO UPDATE SET name = excluded.name RETURNING id");
  Statement link(
      impl_->database,
      "INSERT OR IGNORE INTO asset_tags(asset_id, tag_id) VALUES(?, ?)");

  for (const auto& tag : tags) {
    if (tag.empty()) {
      continue;
    }
    create_tag.reset();
    create_tag.bind(1, tag);
    if (create_tag.step() != SQLITE_ROW) {
      throw LibraryDatabaseError("Unable to create library tag");
    }
    const std::int64_t tag_id =
        static_cast<std::int64_t>(sqlite3_column_int64(create_tag.get(), 0));
    if (create_tag.step() != SQLITE_DONE) {
      throw LibraryDatabaseError("Unable to finish creating library tag");
    }

    link.reset();
    link.bind(1, asset_id);
    link.bind(2, tag_id);
    static_cast<void>(link.step());
  }

  {
    Statement update_tags_text(impl_->database, R"SQL(
      UPDATE assets SET tags_text = COALESCE((
        SELECT group_concat(name, ' ') FROM (
          SELECT tags.name AS name FROM tags
          JOIN asset_tags ON asset_tags.tag_id = tags.id
          WHERE asset_tags.asset_id = assets.id
          ORDER BY tags.name COLLATE NOCASE
        )
      ), '') WHERE id = ?
    )SQL");
    update_tags_text.bind(1, asset_id);
    static_cast<void>(update_tags_text.step());
  }

  exec(impl_->database,
       "DELETE FROM tags WHERE NOT EXISTS "
       "(SELECT 1 FROM asset_tags WHERE asset_tags.tag_id = tags.id)");
  transaction.commit();
}

void LibraryDatabase::record_recent(const std::int64_t asset_id) {
  Statement statement(
      impl_->database,
      "UPDATE assets SET last_used_at = ? WHERE id = ?");
  statement.bind(1, unix_time_now());
  statement.bind(2, asset_id);
  static_cast<void>(statement.step());
  if (sqlite3_changes(impl_->database) == 0) {
    throw LibraryDatabaseError("Unknown asset id");
  }
}

}  // namespace brknam::library
