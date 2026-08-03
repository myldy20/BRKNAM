# Contributing

BRKNAM is in early development. Before implementing a large feature, open an issue describing the user problem, proposed boundary, realtime implications, and test strategy.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Rules that matter

- Keep the core independent of plugin UI, network, and database frameworks.
- Never perform allocation, file I/O, network access, JSON parsing, or locking in the audio callback.
- Add tests for behavior and failure paths.
- Preserve source and About/Legal attribution notices.
- Do not add model captures, IRs, or other audio assets without verified redistribution permission.
- Do not scrape or mirror third-party catalogs.

Contributions are submitted under `GPL-3.0-or-later`; see `docs/LICENSING.md`.
