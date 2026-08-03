# BRKNAM

**Minimal open-source NAM librarian and DAW player.**

BRKNAM is a local-first tool for finding, auditioning, organizing, and playing Neural Amp Modeler captures without forcing an account, a cloud library, or a giant amp-suite interface.

> Find the tone. Skip the bullshit.

## Status

Early development. The first vertical slice is a dependency-free C++20 library scanner plus the `brknam-scan` command-line tool. Audio processing and plugin targets are planned next.

## Product principles

- **Local-first:** existing `.nam` and IR files remain usable offline and in place.
- **No mandatory account:** online providers are optional adapters, not the product core.
- **Fast auditioning:** compare many captures against the same recorded DI loop.
- **Small native UI:** a practical browser/player, not another virtual showroom.
- **Realtime-safe audio:** no file I/O, network calls, locks, or allocation in the audio callback.
- **Open formats:** portable presets and an inspectable local index.

## Current build

Requirements: CMake 3.24+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/brknam-scan /path/to/your/NAM/library
```

On multi-config generators, add `--config Release` when building and `-C Release` when testing.

## Planned targets

- VST3 and Audio Unit plugin
- standalone desktop application
- later: CLAP and Linux packaging

See [the roadmap](docs/ROADMAP.md), [architecture](docs/ARCHITECTURE.md), and [product definition](docs/PRODUCT.md).

## Licensing

BRKNAM is licensed under **GNU GPL v3 or later**. Copyright and author notices must be preserved. Interactive distributions must retain the reasonable author attribution described in [ATTRIBUTION.md](ATTRIBUTION.md).

Copyright (C) 2026 Ilya Tolstoukhov ([@myldy20](https://github.com/myldy20)).
