# Third-party notices

BRKNAM includes or builds against the following reviewed upstream components. Exact revisions are pinned in `cmake/DependencyPins.cmake`.

## NeuralAmpModelerCore

Copyright © Steven Atkinson and contributors.

Licensed under the MIT License. BRKNAM builds the pinned Core sources only when `BRKNAM_ENABLE_NAM_CORE=ON`.

## iPlug2

Copyright © the iPlug2 developers and contributors.

The optional APP, VST3 and AU shell is built with the pinned iPlug2 source under its permissive zlib-style license. iPlug2 also contains third-party components with their own notices; their upstream license files remain part of the fetched source tree.

## Steinberg VST3 SDK

The optional VST3 target uses the official pinned Steinberg VST3 SDK aggregate repository and its official component submodules. Use and distribution of VST3 SDK material remain subject to the license texts distributed by Steinberg with that source.

BRKNAM fetches the SDK only when `BRKNAM_ENABLE_IPLUG2=ON` and places it in the directory required by iPlug2. It does not vendor or republish the SDK inside this repository.

## AudioDSPTools Lanczos resampler provenance

The private mono resampler in `src/audio/LanczosResampler.hpp` is a BRKNAM-specific adaptation of the Lanczos resampler published in AudioDSPTools by Steven Atkinson.

The upstream AudioDSPTools repository is MIT-licensed. Its Lanczos implementation also retains notices from iPlug2 and `sst-basic-blocks`; the upstream file explicitly permits this resampler implementation to be used and modified in MIT/BSD as well as GPL contexts. BRKNAM removes the iPlug/WDL dependencies, uses fixed-width rational phase counters, and exposes only the allocation-free mono operations needed by the NAM rate adapter.

## SQLite

BRKNAM's default build downloads the pinned SQLite amalgamation from the SQLite project. SQLite is in the public domain. A compatible system SQLite may be selected with `BRKNAM_USE_SYSTEM_SQLITE=ON`.

These notices supplement, and do not replace, the complete notices and license texts distributed with the corresponding upstream source code.
