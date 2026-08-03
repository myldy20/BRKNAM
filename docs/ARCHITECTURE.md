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

The current bootstrap implements `src/library` and `brknam-scan` first. This provides a testable vertical slice before adding large audio dependencies.

## Component boundaries

### Library service

Responsibilities:

- discover `.nam` files and supported IR candidates;
- parse NAM JSON metadata without loading model weights into the DSP engine;
- maintain an application-owned SQLite database with FTS5 search;
- retain original file locations rather than silently copying user libraries;
- store optional SHA-256 hashes, file size, modification time, user tags, rating, favorite state, and provider identifiers;
- perform incremental rescans and explicitly report unreadable or malformed assets.

The content hash is calculated lazily. Normal rescans first compare path, size, and modification time to avoid hashing an entire large library repeatedly.

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
- About/Legal view containing required licensing and author notices.

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
- The first CI gate compiles and tests the dependency-free core on Linux, macOS, and Windows.

## Testing strategy

- unit tests for scanning, metadata, search parsing, state migration, and path/hash restore;
- golden tests for preset serialization;
- DSP impulse and reference-vector tests against known upstream behavior;
- realtime-safety tests that detect allocation and locking in the audio callback;
- plugin validation with `pluginval` once VST3/AU targets exist;
- session restore tests in at least REAPER and Ableton Live before 1.0;
- malformed, oversized, and unsupported model fixtures kept out of production bundles.
