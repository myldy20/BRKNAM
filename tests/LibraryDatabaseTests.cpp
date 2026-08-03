// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/library/LibraryDatabase.hpp"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write_file(const std::filesystem::path& path,
                const std::string& contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
  if (!output) {
    throw std::runtime_error("unable to write fixture");
  }
}

struct TempDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("brknam-db-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));

  TempDirectory() {
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

void test_index_refresh_and_search() {
  TempDirectory temp;
  const auto models = temp.path / "Mødels";
  const auto valid = models / "Crunch One.nam";
  const auto invalid = models / "Broken.nam";
  const auto ir = models / "Cab.wav";
  write_file(valid, R"({"version":"0.5.4","architecture":"WaveNet","metadata":{"name":"Crunch One","modeled_by":"Tester","gear_make":"TestCo","gear_model":"Crunch","gear_type":"amp","tone_type":"crunch"},"weights":[1,2,3]})");
  write_file(invalid, R"({"weights":[]})");
  write_file(ir, "RIFF");

  brknam::library::LibraryDatabase database(temp.path / "library.sqlite3");
  require(database.schema_version() == 2, "schema version should be 2");
  const auto root = database.add_or_update_root(models, "Fixtures");
  require(root.id > 0, "root id should be assigned");

  const auto first = database.refresh_root(root.id);
  require(first.discovered == 3, "first scan should discover three assets");
  require(first.inserted == 3, "first scan should insert three assets");
  require(first.parse_errors == 1,
          "invalid NAM should be isolated as a parse error");

  const auto all = database.search("");
  require(all.size() == 3, "empty search should list all present assets");
  const auto crunch = database.search("Crunch");
  require(crunch.size() == 1, "FTS should find model by display name");
  require(crunch.front().modeled_by == "Tester",
          "metadata should be indexed");
  require(database.search("TestCo").size() == 1,
          "FTS should find model by gear metadata");

  const auto second = database.refresh_root(root.id);
  require(second.unchanged == 3,
          "unchanged scan should avoid reparsing all assets");
  require(second.inserted == 0 && second.updated == 0,
          "unchanged scan should not write content");

  write_file(valid, R"({"version":"0.5.4","architecture":"WaveNet","metadata":{"name":"Crunch One","modeled_by":"Tester","gear_make":"TestCo","gear_model":"Crunch","gear_type":"amp","tone_type":"crunch"},"weights":[1,2,3,4]})");
  const auto third = database.refresh_root(root.id);
  require(third.updated == 1, "changed file should be reparsed and updated");
  require(third.unchanged == 2, "other files should remain unchanged");

  std::filesystem::remove(ir);
  const auto fourth = database.refresh_root(root.id);
  require(fourth.newly_missing == 1,
          "deleted file should be marked missing");
  require(database.search("").size() == 2,
          "missing assets should be hidden by default");
  brknam::library::SearchOptions include_missing;
  include_missing.include_missing = true;
  require(database.search("", include_missing).size() == 3,
          "missing assets should remain recoverable");
}

void test_user_metadata_survives_refresh() {
  TempDirectory temp;
  const auto models = temp.path / "models";
  const auto model = models / "Lead.nam";
  write_file(model,
             R"({"version":"0.5.4","architecture":"WaveNet","weights":[]})");

  brknam::library::LibraryDatabase database(temp.path / "library.sqlite3");
  const auto root = database.add_or_update_root(models);
  static_cast<void>(database.refresh_root(root.id));
  auto found = database.search("Lead");
  require(found.size() == 1, "fixture should be indexed");
  const auto id = found.front().id;

  database.set_favorite(id, true);
  database.set_rating(id, 5);
  database.replace_tags(id, {"live", "favorite", "live"});
  database.record_recent(id);
  require(database.search("favorite").size() == 1,
          "user tags should be searchable");

  write_file(model,
             R"({"version":"0.5.4","architecture":"WaveNet","weights":[0]})");
  static_cast<void>(database.refresh_root(root.id));
  const auto updated = database.asset(id);
  require(updated.has_value(), "asset should still exist");
  require(updated->favorite, "favorite should survive index refresh");
  require(updated->rating == 5, "rating should survive index refresh");
  require(updated->tags.size() == 2, "tags should be deduplicated");
  require(database.recent().front().id == id,
          "recent usage should be recorded");

  bool rejected = false;
  try {
    database.set_rating(id, 6);
  } catch (const brknam::library::LibraryDatabaseError&) {
    rejected = true;
  }
  require(rejected, "invalid rating should be rejected");
}

void test_lazy_hash_and_duplicates() {
  TempDirectory temp;
  const auto models = temp.path / "models";
  write_file(models / "A.wav", "abc");
  write_file(models / "B.wav", "abc");
  write_file(models / "C.wav", "abd");

  brknam::library::LibraryDatabase database(temp.path / "library.sqlite3");
  const auto root = database.add_or_update_root(models);
  static_cast<void>(database.refresh_root(root.id));

  const auto assets = database.search("");
  require(assets.size() == 3, "hash fixture should contain three assets");

  std::int64_t a_id = 0;
  for (const auto& asset : assets) {
    if (asset.relative_path.filename() == "A.wav") {
      a_id = asset.id;
    }
  }
  require(a_id != 0, "A.wav should be indexed");

  const auto hash = database.ensure_sha256(a_id);
  require(hash ==
              "ba7816bf8f01cfea414140de5dae2223"
              "b00361a396177a9cb410ff61f20015ad",
          "SHA-256 should match the standard abc test vector");

  const auto duplicates = database.duplicate_group(a_id);
  require(duplicates.size() == 2,
          "only byte-identical assets should form a duplicate group");

  write_file(models / "A.wav", "abcd");
  bool stale_rejected = false;
  try {
    static_cast<void>(database.ensure_sha256(a_id));
  } catch (const brknam::library::LibraryDatabaseError&) {
    stale_rejected = true;
  }
  require(stale_rejected,
          "hashing should reject a file changed since the last refresh");

  const auto refresh = database.refresh_root(root.id);
  require(refresh.updated == 1, "changed hashed file should be updated");
  const auto refreshed = database.asset(a_id);
  require(refreshed.has_value() && !refreshed->content_sha256.has_value(),
          "content changes should invalidate the cached hash");
  require(database.ensure_sha256(a_id) != hash,
          "changed content should receive a different hash");
}

void test_move_recovery_preserves_identity() {
  TempDirectory temp;
  const auto models = temp.path / "models";
  const auto original = models / "Original.wav";
  const auto renamed = models / "Renamed.wav";
  write_file(original, "move-data");

  brknam::library::LibraryDatabase database(temp.path / "library.sqlite3");
  const auto root = database.add_or_update_root(models);
  static_cast<void>(database.refresh_root(root.id));
  const auto initial = database.search("Original");
  require(initial.size() == 1, "move fixture should be indexed");
  const auto id = initial.front().id;
  const auto hash = database.ensure_sha256(id);
  database.set_favorite(id, true);
  database.replace_tags(id, {"keeper"});

  std::filesystem::rename(original, renamed);
  const auto refresh = database.refresh_root(root.id);
  require(refresh.moved == 1, "renamed hashed asset should be recovered");
  require(refresh.inserted == 0,
          "move recovery should not create a new asset identity");
  require(refresh.newly_missing == 0,
          "recovered asset should not remain marked missing");

  const auto recovered = database.asset(id);
  require(recovered.has_value(), "recovered asset should keep its id");
  require(recovered->relative_path.filename() == "Renamed.wav",
          "recovered asset should use the new path");
  require(recovered->content_sha256 == hash,
          "move recovery should preserve the validated content hash");
  require(recovered->favorite && recovered->tags.size() == 1,
          "move recovery should preserve user metadata");
}

void test_schema_v1_migration() {
  TempDirectory temp;
  const auto path = temp.path / "legacy.sqlite3";

  sqlite3* raw = nullptr;
  require(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK,
          "legacy fixture database should open");
  char* message = nullptr;
  const int result = sqlite3_exec(
      raw,
      "CREATE TABLE assets(id INTEGER PRIMARY KEY);"
      "PRAGMA user_version = 1;",
      nullptr, nullptr, &message);
  if (result != SQLITE_OK) {
    const std::string error =
        message != nullptr ? message : "unknown SQLite error";
    sqlite3_free(message);
    sqlite3_close(raw);
    throw std::runtime_error(error);
  }
  sqlite3_close(raw);

  brknam::library::LibraryDatabase database(path);
  require(database.schema_version() == 2,
          "schema version 1 should migrate to version 2");
}

}  // namespace

int main() {
  try {
    test_index_refresh_and_search();
    test_user_metadata_survives_refresh();
    test_lazy_hash_and_duplicates();
    test_move_recovery_preserves_identity();
    test_schema_v1_migration();
    std::cout << "LibraryDatabaseTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
