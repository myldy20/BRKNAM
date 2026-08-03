# Third-party notices

BRKNAM includes or builds against the following reviewed upstream components. Exact revisions are pinned in `cmake/DependencyPins.cmake`.

## NeuralAmpModelerCore

Copyright © Steven Atkinson and contributors.

Licensed under the MIT License. BRKNAM builds the pinned Core sources only when `BRKNAM_ENABLE_NAM_CORE=ON`.

## AudioDSPTools Lanczos resampler provenance

The private mono resampler in `src/audio/LanczosResampler.hpp` is a BRKNAM-specific adaptation of the Lanczos resampler published in AudioDSPTools by Steven Atkinson.

The upstream AudioDSPTools repository is MIT-licensed. Its Lanczos implementation also retains notices from iPlug2 and `sst-basic-blocks`; the upstream file explicitly permits this resampler implementation to be used and modified in MIT/BSD as well as GPL contexts. BRKNAM removes the iPlug/WDL dependencies, uses fixed-width rational phase counters, and exposes only the allocation-free mono operations needed by the NAM rate adapter.

## SQLite

BRKNAM's default build downloads the pinned SQLite amalgamation from the SQLite project. SQLite is in the public domain. A compatible system SQLite may be selected with `BRKNAM_USE_SYSTEM_SQLITE=ON`.

These notices supplement, and do not replace, the complete notices and license texts distributed with the corresponding upstream source code.
