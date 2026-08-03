// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Myldy design / @myldy20
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "LibraryDatabaseInternal.hpp"

#include <system_error>

namespace brknam::library {

using detail::Statement;
using detail::Transaction;
using detail::bind_optional;
using detail::display_name_for;
using detail::kind_to_int;
using detail::modified_ticks;
using detail::normalize_path;
using detail::path_from_utf8;
using detail::path_to_utf8;
using detail::relative_to_root;
using detail::required_text;
using detail::unix_time_now;

RefreshStats LibraryDatabase::refresh_root(const std::int64_t root_id,
                                           const ScanOptions& options) {
  Statement root_statement(
      impl_->database,
      "SELECT path, scan_generation FROM library_roots "
      "WHERE id = ? AND enabled = 1");
  root_statement.bind(1, root_id);
  if (root_statement.step() != SQLITE_ROW) {
    throw LibraryDatabaseError("Unknown or disabled library root");
  }

  const auto root_path = path_from_utf8(required_text(root_statement.get(), 0));
  std::error_code root_error;
  const auto root_status = std::filesystem::status(root_path, root_error);
  if (root_error || (!std::filesystem::is_directory(root_status) &&
                     !std::filesystem::is_regular_file(root_status))) {
    throw LibraryDatabaseError(
        "Library root is unavailable; existing index was preserved");
  }

  const std::int64_t generation =
      static_cast<std::int64_t>(sqlite3_column_int64(root_statement.get(), 1)) + 1;
  auto report = scan_library(root_path, options);
  RefreshStats stats;
  stats.discovered = report.assets.size();
  stats.scan_issues = report.issues;

  Transaction transaction(impl_->database);
  const auto now = unix_time_now();
  {
    Statement update_root(
        impl_->database,
        "UPDATE library_roots SET scan_generation = ?, "
        "last_scan_started_at = ? WHERE id = ?");
    update_root.bind(1, generation);
    update_root.bind(2, now);
    update_root.bind(3, root_id);
    static_cast<void>(update_root.step());
  }

  Statement existing(
      impl_->database,
      "SELECT id, kind, size_bytes, modified_ticks, missing "
      "FROM assets WHERE root_id = ? AND relative_path = ?");
  Statement touch(
      impl_->database,
      "UPDATE assets SET path = ?, seen_generation = ?, missing = 0, "
      "updated_at = ? WHERE id = ?");
  Statement upsert(impl_->database, R"SQL(
    INSERT INTO assets(
      root_id, path, relative_path, kind, size_bytes, modified_ticks, seen_generation,
      missing, parse_status, parse_error, display_name, file_version, architecture,
      sample_rate_hz, modeled_by, gear_make, gear_model, gear_type, tone_type,
      input_level_dbu, output_level_dbu, created_at, updated_at
    ) VALUES(?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(root_id, relative_path) DO UPDATE SET
      path = excluded.path,
      kind = excluded.kind,
      size_bytes = excluded.size_bytes,
      modified_ticks = excluded.modified_ticks,
      seen_generation = excluded.seen_generation,
      missing = 0,
      parse_status = excluded.parse_status,
      parse_error = excluded.parse_error,
      display_name = excluded.display_name,
      file_version = excluded.file_version,
      architecture = excluded.architecture,
      sample_rate_hz = excluded.sample_rate_hz,
      modeled_by = excluded.modeled_by,
      gear_make = excluded.gear_make,
      gear_model = excluded.gear_model,
      gear_type = excluded.gear_type,
      tone_type = excluded.tone_type,
      input_level_dbu = excluded.input_level_dbu,
      output_level_dbu = excluded.output_level_dbu,
      updated_at = excluded.updated_at
  )SQL");

  for (const auto& scanned : report.assets) {
    const auto relative_path = relative_to_root(scanned.path, root_path);
    const auto relative_text = path_to_utf8(relative_path);
    const auto absolute_text = path_to_utf8(normalize_path(scanned.path));
    const auto modified = modified_ticks(scanned.path);

    existing.reset();
    existing.bind(1, root_id);
    existing.bind(2, relative_text);
    bool unchanged = false;
    bool existed = false;
    std::int64_t existing_id{};
    if (existing.step() == SQLITE_ROW) {
      existed = true;
      existing_id = sqlite3_column_int64(existing.get(), 0);
      const auto existing_kind = sqlite3_column_int(existing.get(), 1);
      const auto existing_size = sqlite3_column_int64(existing.get(), 2);
      const auto existing_modified = sqlite3_column_int64(existing.get(), 3);
      const bool was_missing = sqlite3_column_int(existing.get(), 4) != 0;
      unchanged =
          existing_kind == kind_to_int(scanned.kind) &&
          existing_size == static_cast<std::int64_t>(scanned.size_bytes) &&
          existing_modified == modified && !was_missing;
    }

    if (unchanged) {
      touch.reset();
      touch.bind(1, absolute_text);
      touch.bind(2, generation);
      touch.bind(3, now);
      touch.bind(4, existing_id);
      static_cast<void>(touch.step());
      ++stats.unchanged;
      continue;
    }

    nam::NamReadResult metadata;
    ParseStatus parse_status = ParseStatus::not_applicable;
    std::optional<std::string> parse_error;
    if (scanned.kind == AssetKind::nam_model) {
      metadata = nam::read_metadata(scanned.path);
      if (metadata) {
        parse_status = ParseStatus::parsed;
      } else {
        parse_status = ParseStatus::error;
        parse_error = metadata.error.has_value()
                          ? metadata.error->message
                          : std::string("Unknown metadata error");
        ++stats.parse_errors;
      }
    }

    const auto* value = metadata.metadata.has_value()
                            ? &*metadata.metadata
                            : nullptr;
    upsert.reset();
    int index = 1;
    upsert.bind(index++, root_id);
    upsert.bind(index++, absolute_text);
    upsert.bind(index++, relative_text);
    upsert.bind(index++, kind_to_int(scanned.kind));
    upsert.bind(index++, static_cast<std::int64_t>(scanned.size_bytes));
    upsert.bind(index++, modified);
    upsert.bind(index++, generation);
    upsert.bind(index++, static_cast<int>(parse_status));
    bind_optional(upsert, index++, parse_error);
    upsert.bind(index++, display_name_for(scanned, metadata));
    bind_optional(upsert, index++,
                  value != nullptr
                      ? std::optional<std::string>(value->file_version)
                      : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr
                      ? std::optional<std::string>(value->architecture)
                      : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr
                      ? std::optional<double>(value->sample_rate_hz)
                      : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->modeled_by : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->gear_make : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->gear_model : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->gear_type : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->tone_type : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->input_level_dbu : std::nullopt);
    bind_optional(upsert, index++,
                  value != nullptr ? value->output_level_dbu : std::nullopt);
    upsert.bind(index++, now);
    upsert.bind(index, now);
    static_cast<void>(upsert.step());

    if (existed) {
      ++stats.updated;
    } else {
      ++stats.inserted;
    }
  }

  {
    Statement mark_missing(
        impl_->database,
        "UPDATE assets SET missing = 1, updated_at = ? "
        "WHERE root_id = ? AND seen_generation <> ? AND missing = 0");
    mark_missing.bind(1, now);
    mark_missing.bind(2, root_id);
    mark_missing.bind(3, generation);
    static_cast<void>(mark_missing.step());
    stats.newly_missing =
        static_cast<std::size_t>(sqlite3_changes(impl_->database));
  }

  {
    Statement finish_root(
        impl_->database,
        "UPDATE library_roots SET last_scan_completed_at = ? WHERE id = ?");
    finish_root.bind(1, unix_time_now());
    finish_root.bind(2, root_id);
    static_cast<void>(finish_root.step());
  }

  transaction.commit();
  return stats;
}

}  // namespace brknam::library
