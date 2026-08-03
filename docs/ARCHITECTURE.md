# Architecture

## Decision summary

BRKNAM uses a modular C++20 core with thin platform and plugin adapters. The DSP engine, library index, online providers, preset state, and UI are isolated so none of them becomes a mandatory dependency of the others.

The intended plugin framework is **iPlug2**, matching the official open-source Neural Amp Modeler plugin and avoiding a mandatory proprietary framework license. `NeuralAmpModelerCore` will be pinned as an upstream dependency under its MIT license. BRKNAM's own code is GPL-3.0-or-later.

## Repository layout

```text
include/brknam/        Public core interfaces
src/audio/             Realtime processing graph and NAM/IR adapters
src/library/           Filesystem scan, metadata extraction, SQLite/FTS index
src/providers/         Optional remote catalog providers
src/state/             Preset schema and session restoration
src/ui/                Framework-independent view models
plugin/                iPlug2 VST3/AU/standalone adapter
resources/             UI resources and schemas
tools/                 Developer and library command-line tools
tests/                 Unit, integration, DSP, and state compatibility tests
docs/                  Product, architecture, roadmap, and decisions
```

E1 implements the filesystem scanner, bounded NAM metadata reader, SQLite library database, FTS5 search, incremental rescan, missing-file tracking, user metadata, saved searches, lazy content hashes, duplicate grouping, move recovery, and stable JSON CLI output. These remain independent of iPlug2 and the future audio processor.

## Component boundaries

### Library service

Responsibilities:

- discover `.nam` files and supported IR candidates;
- parse NAM JSON metadata without loading model weights into the DSP engine;
- maintain an application-owned SQLite database with FTS5 search;
- retain original file locations rather than silently copying user libraries;
- store optional SHA-256 hashes, file size, modification time, user tags, rating, favorite state, recent use, saved searches, and provider identifiers;
- perform incremental rescans and explicitly report unreadable or malformed assets.

The database is accessed through `brknam::library::LibraryDatabase`; SQLite types do not appear in the public header. Schema changes use `PRAGMA user_version` and forward-only migrations.

Schema version 1 introduced:

- named library roots and scan generations;
- indexed NAM/IR assets and extracted metadata;
- missing and parse-status state;
- favorites, ratings, recent use, tags, and searchable tag text;
- an FTS5 index synchronized by database triggers.

Schema version 2 added lazy SHA-256 content identity and an indexed hash column. Schema version 3 added named saved searches with query, missing-file policy, and bounded result limits. Migrations from versions 1 and 2 are covered by fixtures.

Normal rescans compare root-relative path, kind, file size, and modification ticks. Unchanged files are marked as seen without reparsing. Deleted paths remain in the database as missing so the application can explain unresolved assets instead of silently forgetting them.

Content hashes are calculated only for explicit hash or duplicate operations, or when a previously hashed missing asset has a plausible move candidate. A move is accepted only when exactly one missing record matches the new file by kind, size, and verified SHA-256. Ambiguous duplicate candidates are not auto-relinked.

The FTS5 performance gate seeds 25,000 synthetic records and requires repeated unique searches to average no more than 100 ms on CI. This is deliberately generous enough to avoid machine-noise failures while still catching accidental full-table or per-result work.

SQLite 3.53.4 is pinned as a CMake-fetched amalgamation for deterministic builds. Developers may explicitly select a compatible system SQLite. FTS5 is mandatory; runtime extension loading is disabled in the bundled build.

### Command-line contract

`brknam-library` is a developer and automation surface for indexing, searching, hashing, duplicate inspection, and saved searches. Human-readable output remains the default. `--json` emits compact UTF-8 JSON with stable field names and structured errors:

```json
{"ok":false,"error":{"code":"invalid_arguments","message":"..."}}
```

The JSON representation is compatibility-tested. New fields may be added in later development, but existing fields must not be silently renamed or change type without an explicit compatibility decision.

### Audio engine

The initial graph is:

```text
Input trim -> DC blocker -> NAM model -> IR convolver -> Output trim
```

The later rig graph is:

```text
Input -> Gate -> Pedal NAM -> Amp NAM -> Cab NAM or IR -> Post EQ -> Mix/Output
```

The graph is framework-independent and receives planar floating-point blocks from a thin plugin adapter.

### Model loader

Model parsing, allocation, resampling preparation, IR decoding, and warm-up happen on a worker thread. A prepared immutable processing graph is published to the audio engine only at a block boundary.

The audio callback must never:

- perform filesystem or network access;
- acquire a mutex;
- allocate or free heap memory;
- parse JSON;
- initialize a model or convolution kernel;
- wait for another thread.

A bounded crossfade, initially 20 ms, hides discontinuities during graph replacement. Graph retirement is deferred to a non-audio thread; an atomic `shared_ptr` swap is not considered sufficient because destruction may occur on the audio thread.

### Audition recorder

A bounded ring buffer stores a user-selected duration of clean input. Playback reads an immutable snapshot into the current graph. Changing a search result reloads only the model graph while keeping the audition source unchanged, allowing meaningful A/B comparison.

Live monitoring and audition playback are mutually explicit modes to avoid accidental feedback and ambiguous routing.

### Preset state

The public preset format is versioned JSON. Each referenced asset may include:

- original and relative paths;
- SHA-256 content hash;
- asset kind;
- provider and provider-side identifiers;
- creator, license, and attribution metadata;
- user-visible display name.

Restore order:

1. exact path;
2. path relative to a configured library root;
3. content hash in the local index;
4. provider-assisted retrieval with explicit user action;
5. unresolved placeholder with a relink action.

DAW session state may embed the compact JSON document but must not embed third-party model data unless its license permits redistribution.

### Online provider boundary

Online catalogs implement a narrow provider interface: authenticate, select/search within permitted API capabilities, obtain metadata, download an explicitly selected asset, and revoke credentials.

For TONE3000, the plan is OAuth 2.0 with PKCE and documented API flows only. BRKNAM will not scrape, mirror, bulk-download, or present itself as a white-label TONE3000 catalog. Provider credentials are stored in the operating-system credential store, never inside a DAW preset.

The application remains fully functional when every provider adapter is disabled.

### UI architecture

The UI is a compact native view rendered by iPlug2 IGraphics. It communicates through view models and command queues rather than calling the audio processor or database directly.

Primary views:

- player strip;
- collapsible library browser;
- audition controls;
- preset and A/B controls;
- settings;
- About/Legal view containing required licensing and origin notices.

No embedded browser is required for the normal interface. A system browser may be opened for OAuth consent.

## Threads and communication

```text
UI thread
  -> commands -> library/model workers
  <- immutable snapshots / progress events

Library worker
  -> filesystem + SQLite

Model loader worker
  -> NAM parsing + IR preparation + graph warm-up
  -> publish prepared graph

Audio thread
  -> lock-free command intake at block boundaries
  -> processing only
  -> deferred-retirement notification
```

Queues are bounded. Overflow behavior is explicit: repeated navigation commands may coalesce to the newest requested model, while critical state changes return an error instead of blocking.

## Dependency policy

- Dependencies must be open-source and GPLv3-compatible.
- Versions are pinned and listed in `THIRD_PARTY.md` before release.
- Network, database, and plugin-framework types must not leak into the DSP core public API.
- Optional providers compile behind feature flags.
- The default build uses pinned sources; system dependencies require an explicit configure option.

## Testing strategy

- unit tests for scanning, metadata, search parsing, state migration, saved searches, JSON compatibility, and path/hash restore;
- deterministic fixture tests for inserted, changed, unchanged, malformed, missing, duplicate, and moved assets;
- a 25,000-record FTS5 performance gate;
- golden tests for preset serialization;
- DSP impulse and reference-vector tests against known upstream behavior;
- realtime-safety tests that detect allocation and locking in the audio callback;
- plugin validation with `pluginval` once VST3/AU targets exist;
- session restore tests in at least REAPER and Ableton Live before 1.0;
- malformed, oversized, and unsupported model fixtures kept out of production bundles.
