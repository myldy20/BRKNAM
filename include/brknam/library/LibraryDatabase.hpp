// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/library/LibraryScanner.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace brknam::library {

class LibraryDatabaseError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class ParseStatus : int {
  not_applicable = 0,
  parsed = 1,
  error = 2,
};

struct LibraryRootRecord {
  std::int64_t id{};
  std::string name;
  std::filesystem::path path;
  bool enabled{true};
};

struct AssetRecord {
  std::int64_t id{};
  std::int64_t root_id{};
  std::filesystem::path path;
  std::filesystem::path relative_path;
  AssetKind kind{AssetKind::nam_model};
  std::uintmax_t size_bytes{};
  std::int64_t modified_ticks{};
  std::optional<std::string> content_sha256;
  bool missing{false};
  ParseStatus parse_status{ParseStatus::not_applicable};
  std::optional<std::string> parse_error;

  std::string display_name;
  std::optional<std::string> file_version;
  std::optional<std::string> architecture;
  std::optional<double> sample_rate_hz;
  std::optional<std::string> modeled_by;
  std::optional<std::string> gear_make;
  std::optional<std::string> gear_model;
  std::optional<std::string> gear_type;
  std::optional<std::string> tone_type;
  std::optional<double> input_level_dbu;
  std::optional<double> output_level_dbu;

  bool favorite{false};
  std::optional<int> rating;
  std::vector<std::string> tags;
};

struct RefreshStats {
  std::size_t discovered{};
  std::size_t inserted{};
  std::size_t updated{};
  std::size_t moved{};
  std::size_t unchanged{};
  std::size_t parse_errors{};
  std::size_t newly_missing{};
  std::vector<ScanIssue> scan_issues;
};

struct SearchOptions {
  std::size_t limit{100};
  bool include_missing{false};
};

class LibraryDatabase final {
 public:
  explicit LibraryDatabase(const std::filesystem::path& database_path);
  ~LibraryDatabase();

  LibraryDatabase(LibraryDatabase&&) noexcept;
  LibraryDatabase& operator=(LibraryDatabase&&) noexcept;

  LibraryDatabase(const LibraryDatabase&) = delete;
  LibraryDatabase& operator=(const LibraryDatabase&) = delete;

  [[nodiscard]] int schema_version() const;

  [[nodiscard]] LibraryRootRecord add_or_update_root(
      const std::filesystem::path& path,
      std::string name = {});
  [[nodiscard]] std::vector<LibraryRootRecord> roots() const;

  [[nodiscard]] RefreshStats refresh_root(std::int64_t root_id,
                                          const ScanOptions& options = {});

  [[nodiscard]] std::vector<AssetRecord> search(
      std::string query,
      const SearchOptions& options = {}) const;
  [[nodiscard]] std::optional<AssetRecord> asset(std::int64_t asset_id) const;
  [[nodiscard]] std::vector<AssetRecord> recent(std::size_t limit = 20) const;

  [[nodiscard]] std::string ensure_sha256(std::int64_t asset_id);
  [[nodiscard]] std::vector<AssetRecord> duplicate_group(std::int64_t asset_id);

  void set_favorite(std::int64_t asset_id, bool favorite);
  void set_rating(std::int64_t asset_id, std::optional<int> rating);
  void replace_tags(std::int64_t asset_id, const std::vector<std::string>& tags);
  void record_recent(std::int64_t asset_id);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace brknam::library
