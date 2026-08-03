// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Myldy design / @myldy20
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/library/LibraryDatabase.hpp"
#include "brknam/nam/NamMetadataReader.hpp"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brknam::library::detail {

constexpr int kCurrentSchemaVersion = 1;

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value);
[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path& path);
[[nodiscard]] std::int64_t modified_ticks(const std::filesystem::path& path);
[[nodiscard]] std::int64_t unix_time_now();
[[nodiscard]] int kind_to_int(AssetKind kind) noexcept;
[[nodiscard]] AssetKind int_to_kind(int value);
[[nodiscard]] std::string default_root_name(const std::filesystem::path& path);
[[nodiscard]] std::string display_name_for(const LibraryAsset& asset,
                                           const nam::NamReadResult& metadata);
[[nodiscard]] std::string required_text(sqlite3_stmt* statement, int column);
[[nodiscard]] std::optional<std::string> optional_text(sqlite3_stmt* statement, int column);
[[nodiscard]] std::optional<double> optional_double(sqlite3_stmt* statement, int column);
[[nodiscard]] std::optional<int> optional_int(sqlite3_stmt* statement, int column);
[[nodiscard]] std::string make_fts_query(std::string_view query);
[[nodiscard]] std::filesystem::path relative_to_root(const std::filesystem::path& asset,
                                                     const std::filesystem::path& root);
void exec(sqlite3* database, std::string_view sql);

class Statement final {
 public:
  Statement(sqlite3* database, std::string_view sql);
  ~Statement();

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  [[nodiscard]] sqlite3_stmt* get() const noexcept;
  void reset();
  [[nodiscard]] int step();
  void bind(int index, std::string_view value);
  void bind(int index, std::int64_t value);
  void bind(int index, int value);
  void bind(int index, double value);
  void bind_null(int index);

 private:
  sqlite3* database_{};
  sqlite3_stmt* statement_{};
};

class Transaction final {
 public:
  explicit Transaction(sqlite3* database);
  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  void commit();

 private:
  sqlite3* database_{};
  bool committed_{false};
};

void bind_optional(Statement& statement, int index, const std::optional<std::string>& value);
void bind_optional(Statement& statement, int index, const std::optional<double>& value);

}  // namespace brknam::library::detail

namespace brknam::library {

struct LibraryDatabase::Impl {
  explicit Impl(const std::filesystem::path& path);
  ~Impl();

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void migrate();
  [[nodiscard]] std::vector<std::string> tags_for(std::int64_t asset_id) const;
  [[nodiscard]] AssetRecord read_asset(sqlite3_stmt* statement) const;

  sqlite3* database{};
};

}  // namespace brknam::library
