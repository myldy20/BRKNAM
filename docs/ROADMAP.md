# Development roadmap

The roadmap is organized as gated vertical slices. A stage is complete only when its code, tests, documentation, and user-visible failure behavior are present.

## E0 — Repository and engineering baseline

**Status: complete.**

**Goal:** establish a buildable, legally clear, testable project.

- [x] Product definition and architecture.
- [x] GNU GPL v3-or-later license and GPLv3 section 7(b) origin notice.
- [x] CMake C++20 core.
- [x] Cross-platform CI for Linux, macOS, and Windows.
- [x] Initial recursive local asset scanner.
- [x] Scanner CLI and tests.
- [ ] Add formatter and static-analysis jobs during release hardening.

**Exit:** clean configure/build/test on all three CI operating systems.

## E1 — Local library foundation

**Status: complete.**

**Goal:** turn arbitrary folders into a reliable searchable catalog.

- [x] Parse NAM file version, architecture, sample rate, creator, gear, tone and calibration metadata without loading model weights.
- [x] Reject malformed or unsupported files without crashing or blocking the rest of the scan.
- [x] Add a versioned SQLite schema and FTS5 search.
- [x] Add multiple library roots, incremental scan, missing-file tracking, and manual CLI rescan.
- [x] Add lazy SHA-256 hashing, duplicate grouping, and moved-file recovery.
- [x] Add favorites, ratings, user tags, and recent models.
- [x] Add saved searches.
- [x] Expose a library service independent of the eventual UI framework.
- [x] Extend command-line diagnostics with stable machine-readable JSON output.
- [x] Add a 25,000-record benchmark with a 100 ms average interactive-search threshold.
- [x] Add migration fixtures for schema versions 1 and 2.

**Exit:** 25,000 synthetic records search within the defined interactive threshold; fixture libraries rescan deterministically; schema migrations and JSON compatibility tests pass.

## E2 — One-slot NAM player

**Status: in progress.**

**Goal:** load and process one NAM model safely in real time.

- [x] Pin `NeuralAmpModelerCore` and its reviewed permissive dependencies.
- [x] Implement the framework-neutral processor wrapper.
- [x] Add input/output trim, bypass, normalization, post-model DC blocker, and model information.
- [x] Build and execute real `.nam` fixtures through the pinned Core on Linux, macOS, and Windows.
- [ ] Add a coalescing worker that loads and prepares requested model paths outside the audio thread.
- [x] Publish prepared models at block boundaries.
- [x] Implement bounded deferred graph destruction and a 20 ms click-free crossfade.
- [x] Report the maximum active latency while crossfading and the settled model latency afterwards.
- [ ] Add model-rate resampling and include its latency in the reported total.
- [ ] Add representative A1/A2 reference-vector and sample-rate tests.
- [ ] Create initial iPlug2 VST3, AU, and standalone targets.

**Exit:** plugin validation passes; repeated model switching under audio load produces no crash, blocking call, or discontinuity above the defined threshold.

## E3 — IR and session-safe presets

**Goal:** complete the minimum useful signal chain and reliable DAW recall.

- Add IR decoding, resampling, convolution, bypass, and level control.
- Define versioned open JSON preset schema.
- Serialize plugin state into DAW sessions.
- Restore assets by exact path, library-relative path, then content hash.
- Provide unresolved-asset and relink workflows.
- Add preset migrations and golden compatibility tests.

**Exit:** sessions survive library-root relocation and reopen consistently on Windows and macOS.

## E4 — Minimal library UI

**Goal:** make the local catalog faster than a file dialog without becoming an amp-suite dashboard.

- Compact player strip and collapsible browser.
- Search-as-you-type, filters, sort, favorites, rating, and recent models.
- Previous/next navigation and keyboard/MIDI commands.
- Background progress and clear malformed/missing-file diagnostics.
- Responsive resizing with a deliberately constrained minimum window.
- About/Legal screen with required notices.
- Accessibility labels and full keyboard navigation.

**Exit:** a fresh user can select a folder, find a model, and play it within two minutes.

## E5 — Audition workflow

**Goal:** compare captures against identical source material.

- Record a configurable clean-input loop into a bounded ring buffer.
- Start/stop/replace audition capture without transport restart.
- A/B slots with loudness-aware optional matching.
- Preload neighboring search results where memory permits.
- Coalesce rapid next/previous commands and cancel obsolete loads.
- MIDI mapping for capture, A/B, favorite, next, and previous.

**Exit:** auditioning 50 models requires playing the reference riff once and remains stable under rapid navigation.

## E6 — Multi-slot BRKNAM rig

**Goal:** add the useful chain, not a kitchen sink.

- Gate.
- Pedal NAM slot.
- Amp NAM slot.
- Cab NAM or IR slot.
- Three-band post EQ, wet/dry mix, and output.
- Per-slot bypass, gain, metadata, and latency contribution.
- CPU-aware quality/slim controls where supported upstream.

**Exit:** preset/state compatibility is preserved and the realtime budget is documented per model class.

## E7 — Optional TONE3000 provider

**Goal:** acquire explicitly selected online models without making cloud access mandatory.

- Confirm current API terms and obtain required application registration.
- OAuth 2.0 PKCE through the system browser.
- Documented select/search/favorites flows only.
- Creator, license, model ID, tone ID, and attribution preservation.
- Explicit downloads into a user-selected managed folder.
- Credential storage via Keychain/Credential Manager/Secret Service.
- Offline and signed-out behavior indistinguishable from local-only mode.

**Exit:** integration passes provider terms review; disabling the feature removes all network behavior.

## E8 — Release hardening and 1.0

- Reproducible release builds and dependency notices.
- Signed Windows packages and notarized macOS packages.
- Crash diagnostics that are opt-in and contain no model/audio data.
- Plugin state tests in REAPER and Ableton Live; additional host smoke tests.
- Performance profiling across representative A1/A2 and slim models.
- CLAP and Linux packaging if core platforms are stable.
- Security review of model parsing, preset loading, downloads, and OAuth callbacks.
- User documentation, contribution guide, and support boundaries.

**1.0 definition:** local-first search, stable NAM+IR playback, auditioning, portable presets, tested session recall, no mandatory account, and no known realtime-safety violations.
