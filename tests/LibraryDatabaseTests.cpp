// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Myldy design / @myldy20
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/library/LibraryDatabase.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
  if (!output) throw std::runtime_error("unable to write fixture");
}

struct TempDirectory {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("brknam-db-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDirectory() { std::filesystem::create_directories(path); }
  ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
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
  require(database.schema_version() == 1, "schema version should be 1");
  const auto root = database.add_or_update_root(models, "Fixtures");
  require(root.id > 0, "root id should be assigned");

  const auto first = database.refresh_root(root.id);
  require(first.discovered == 3, "first scan should discover three assets");
  require(first.inserted == 3, "first scan should insert three assets");
  require(first.parse_errors == 1, "invalid NAM should be isolated as a parse error");

  const auto all = database.search("");
  require(all.size() == 3, "empty search should list all present assets");
  const auto crunch = database.search("Crunch");
  require(crunch.size() == 1, "FTS should find model by display name");
  require(crunch.front().modeled_by == "Tester", "metadata should be indexed");
  const auto maker = database.search("TestCo");
  require(maker.size() == 1, "FTS should find model by gear metadata");

  const auto second = database.refresh_root(root.id);
  require(second.unchanged == 3, "unchanged scan should avoid reparsing all assets");
  require(second.inserted == 0 && second.updated == 0, "unchanged scan should not write content");

  write_file(valid, R"({"version":"0.5.4","architecture":"WaveNet","metadata":{"name":"Crunch One","modeled_by":"Tester","gear_make":"TestCo","gear_model":"Crunch","gear_type":"amp","tone_type":"crunch"},"weights":[1,2,3,4]})");
  const auto third = database.refresh_root(root.id);
  require(third.updated == 1, "changed file should be reparsed and updated");
  require(third.unchanged == 2, "other files should remain unchanged");

  std::filesystem::remove(ir);
  const auto fourth = database.refresh_root(root.id);
  require(fourth.newly_missing == 1, "deleted file should be marked missing");
  require(database.search("").size() == 2, "missing assets should be hidden by default");
  brknam::library::SearchOptions include_missing;
  include_missing.include_missing = true;
  require(database.search("", include_missing).size() == 3, "missing assets should remain recoverable");
}

void test_user_metadata_survives_refresh() {
  TempDirectory temp;
  const auto models = temp.path / "models";
  const auto model = models / "Lead.nam";
  write_file(model, R"({"version":"0.5.4","architecture":"WaveNet","weights":[]})");

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
  require(database.search("favorite").size() == 1, "user tags should be searchable");

  write_file(model, R"({"version":"0.5.4","architecture":"WaveNet","weights":[0]})");
  static_cast<void>(database.refresh_root(root.id));
  const auto updated = database.asset(id);
  require(updated.has_value(), "asset should still exist");
  require(updated->favorite, "favorite should survive index refresh");
  require(updated->rating == 5, "rating should survive index refresh");
  require(updated->tags.size() == 2, "tags should be deduplicated");
  require(database.recent().front().id == id, "recent usage should be recorded");

  bool rejected = false;
  try { database.set_rating(id, 6); } catch (const brknam::library::LibraryDatabaseError&) { rejected = true; }
  require(rejected, "invalid rating should be rejected");
}

}  // namespace

int main() {
  try {
    test_index_refresh_and_search();
    test_user_metadata_survives_refresh();
    std::cout << "LibraryDatabaseTests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
