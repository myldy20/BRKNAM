# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
# See NOTICE for the GPLv3 section 7(b) origin notice.

# Reviewed E2 upstream revisions. Normal local-library/tool builds do not fetch
# audio or plugin dependencies unless their feature flags are enabled.

set(BRKNAM_NAM_CORE_REPOSITORY
    "https://github.com/sdatkinson/NeuralAmpModelerCore.git")
set(BRKNAM_NAM_CORE_COMMIT
    "3cde95c354d5ba6da01316cad90b05cfc4855053") # DSP 0.5.5

set(BRKNAM_AUDIO_DSP_TOOLS_REPOSITORY
    "https://github.com/sdatkinson/AudioDSPTools.git")
set(BRKNAM_AUDIO_DSP_TOOLS_COMMIT
    "0827c6c2fc0deced568536142ea86f189e0b98a1") # v0.1.1

set(BRKNAM_IPLUG2_REPOSITORY
    "https://github.com/iPlug2/iPlug2.git")
set(BRKNAM_IPLUG2_COMMIT
    "5c2df9dce3f5258acfeff3846a6a9563f382212c")

# Reference implementation used to compare routing, model loading, resampling,
# latency, normalization and DC-blocker behavior. BRKNAM does not copy its UI
# or branded assets.
set(BRKNAM_NAM_PLUGIN_REFERENCE_COMMIT
    "96337e9ab6e3beb619459779bbb5c47e1b04d8c4")
