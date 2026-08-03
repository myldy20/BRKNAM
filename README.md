# BRKNAM

**Minimal open-source NAM librarian and DAW player.**

BRKNAM is a local-first tool for finding, auditioning, organizing, and playing Neural Amp Modeler captures without forcing an account, a cloud library, or a giant amp-suite interface.

> Find the tone. Skip the bullshit.

## Status

Early development. The current E1 foundation can recursively discover local NAM/IR assets, safely read NAM metadata without loading model weights, persist multiple library roots in SQLite, maintain an incremental index, track missing files, and search metadata and user tags through FTS5.

Audio processing and plugin targets begin in E2 after the local-library contract is stable.

## Product principles

- **Local-first:** existing `.nam` and IR files remain usable offline and in place.
- **No mandatory account:** online providers are optional adapters, not the product core.
- **Fast auditioning:** compare many captures against the same recorded DI loop.
- **Small native UI:** a practical browser/player, not another virtual showroom.
- **Realtime-safe audio:** no file I/O, network calls, locks, or allocation in the audio callback.
- **Open formats:** portable presets and an inspectable local index.

## Current build

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
```

## Planned targets

- VST3 and Audio Unit plugin
- standalone desktop application
- later: CLAP and Linux packaging

See [the roadmap](docs/ROADMAP.md), [architecture](docs/ARCHITECTURE.md), and [product definition](docs/PRODUCT.md).

## Licensing

BRKNAM is licensed under **GNU GPL v3 or later**. Copyright and origin notices must be preserved as described in [`NOTICE`](NOTICE).

Copyright © 2026 [Myldy design](https://github.com/myldy20) / [@myldy20](https://github.com/myldy20).
