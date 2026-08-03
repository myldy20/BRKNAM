# Third-party software

## SQLite

BRKNAM currently pins the SQLite **3.53.4** amalgamation for its local library database and FTS5 index.

- Source: `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip`
- Archive SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
- License: public domain
- BRKNAM build options: FTS5 enabled, runtime extension loading disabled, thread safety enabled

Developers may configure with `-DBRKNAM_USE_SYSTEM_SQLITE=ON` to use a compatible system installation. Release builds should use the pinned amalgamation unless the release notes explicitly document another reviewed version.

## Planned dependencies

The following dependencies have not yet been integrated and remain subject to pinning and final license review:

- NeuralAmpModelerCore — MIT License.
- iPlug2 — permissive license; intended plugin and standalone adapter.

This file records exact versions, source locations, licenses, modifications, and notices for dependencies included in distributed builds.
