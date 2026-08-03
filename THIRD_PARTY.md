# Third-party software

## SQLite

BRKNAM currently pins the SQLite **3.53.4** amalgamation for its local library database and FTS5 index.

- Source: `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip`
- Archive SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
- License: public domain
- BRKNAM build options: FTS5 enabled, runtime extension loading disabled, thread safety enabled

Developers may configure with `-DBRKNAM_USE_SYSTEM_SQLITE=ON` to use a compatible system installation. Release builds should use the pinned amalgamation unless the release notes explicitly document another reviewed version.

## NeuralAmpModelerCore

The E2 integration target is reviewed against official NeuralAmpModelerCore DSP **0.5.5**.

- Repository: `https://github.com/sdatkinson/NeuralAmpModelerCore`
- Commit: `3cde95c354d5ba6da01316cad90b05cfc4855053`
- License: MIT
- Planned BRKNAM build definitions: float sample API, A2 fast path enabled
- Current status: revision recorded; adapter/build target follows in E2

The Core repository includes Eigen and AudioDSPTools as pinned submodules. Their exact revisions and notices must be recorded here when the BRKNAM adapter target begins compiling them into a distributed binary.

## iPlug2

The initial plugin-framework review uses the official iPlug2 repository.

- Repository: `https://github.com/iPlug2/iPlug2`
- Commit: `5c2df9dce3f5258acfeff3846a6a9563f382212c`
- License: zlib-style permissive license
- Planned formats: VST3, Audio Unit, standalone
- Current status: revision recorded; not downloaded by the normal library/tool build

## Reference implementation

The official NeuralAmpModelerPlugin at commit `96337e9ab6e3beb619459779bbb5c47e1b04d8c4` is used as behavioral reference material for channel routing, model loading, normalization, latency, and DC blocking. It is not vendored and BRKNAM does not copy its UI or branded assets.

This file records exact versions, source locations, licenses, modifications, and notices for dependencies included in or used to validate distributed builds.
