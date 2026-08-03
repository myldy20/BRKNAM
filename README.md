# BRKNAM

**Minimal open-source NAM librarian and DAW player.**

BRKNAM is a local-first tool for finding, auditioning, organizing, and playing Neural Amp Modeler captures without forcing an account, a cloud library, or a giant amp-suite interface.

> Find the tone. Skip the bullshit.

## Status

**E1 — Local library foundation is complete.**

BRKNAM can recursively discover local NAM/IR assets, safely read NAM metadata without loading model weights, persist multiple library roots in SQLite, maintain an incremental index, track missing files, and search metadata and user tags through FTS5.

Content hashes are calculated only when needed. BRKNAM can identify byte-identical duplicates and preserve an asset's database identity, favorites, ratings, tags, and recent state when a previously hashed file is safely recognized after a rename or move. Saved searches and stable JSON CLI output are included.

**E2 — One-slot NAM player is feature-complete for automated validation and is entering manual alpha testing.**

The current product shell builds as a Windows/macOS standalone application, VST3, and macOS Audio Unit. It uses the pinned NeuralAmpModelerCore, supports representative A1/A2 models, loads models outside the audio thread, performs click-reduced block-boundary replacement, adapts model sample rates, and reports processing latency to the host. Steinberg VST3 Validator, Apple `auval`, cross-platform reference tests, and concurrent switching stress tests pass in CI.

Unsigned downloadable test packages are produced by the **Alpha packages** GitHub Actions workflow. See [the alpha test guide](docs/ALPHA_TESTING.md) before installing them.

## Product principles

- **Local-first:** existing `.nam` and IR files remain usable offline and in place.
- **No mandatory account:** online providers are optional adapters, not the product core.
- **Fast auditioning:** compare many captures against the same recorded DI loop.
- **Small native UI:** a practical browser/player, not another virtual showroom.
- **Realtime-safe audio:** no file I/O, network calls, locks, or allocation in the audio callback.
- **Open formats:** portable presets and an inspectable local index.

## Build the core and tools

Requirements: CMake 3.24+, a C++20 compiler, and internet access during the first default configure so CMake can download the pinned SQLite amalgamation. A compatible system SQLite can be selected explicitly.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Use a system SQLite installation instead:

```sh
cmake -S . -B build -DBRKNAM_USE_SYSTEM_SQLITE=ON
```

On multi-config generators, add `--config Release` when building and `-C Release` when testing.

### Current tools

```sh
./build/brknam-scan /path/to/your/NAM/library
./build/brknam-inspect /path/to/model.nam

./build/brknam-library library.sqlite3 index /path/to/your/NAM/library
./build/brknam-library library.sqlite3 search "5150 crunch"
./build/brknam-library library.sqlite3 hash 42
./build/brknam-library library.sqlite3 duplicates 42

./build/brknam-library library.sqlite3 save-search "High gain" "5150 crunch" 0 100
./build/brknam-library library.sqlite3 list-searches
./build/brknam-library library.sqlite3 run-search 1
```

Put `--json` before the database path to receive stable machine-readable output and diagnostics:

```sh
./build/brknam-library --json library.sqlite3 search "5150 crunch"
```

## Build the plugin shell

The optional product build fetches the pinned NeuralAmpModelerCore, iPlug2, and official Steinberg VST3 SDK sources.

```sh
cmake -S . -B build-plugin \
  -DBRKNAM_ENABLE_IPLUG2=ON \
  -DBRKNAM_BUILD_TESTS=OFF \
  -DBRKNAM_BUILD_TOOLS=OFF \
  -DIPLUG_DEPLOY_PLUGINS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --config Release
```

Current targets are Windows/macOS standalone, VST3, and macOS AU. Later targets include CLAP and Linux packaging.

See [the roadmap](docs/ROADMAP.md), [architecture](docs/ARCHITECTURE.md), [product definition](docs/PRODUCT.md), and [alpha test guide](docs/ALPHA_TESTING.md).

## Licensing

BRKNAM is licensed under **GNU GPL v3 or later**. Copyright and origin notices must be preserved as described in [`NOTICE`](NOTICE).

Copyright © 2026 [Ilya Tolstoukhov](https://github.com/myldy20), using the project name **Myldy design / @myldy20**.
