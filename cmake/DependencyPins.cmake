# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
# See NOTICE for the GPLv3 section 7(b) origin notice.

set(BRKNAM_NAM_CORE_REPOSITORY
    "https://github.com/sdatkinson/NeuralAmpModelerCore.git")
set(BRKNAM_NAM_CORE_COMMIT
    "3cde95c354d5ba6da01316cad90b05cfc4855053") # DSP 0.5.5

set(BRKNAM_IPLUG2_REPOSITORY
    "https://github.com/iPlug2/iPlug2.git")
set(BRKNAM_IPLUG2_COMMIT
    "5c2df9dce3f5258acfeff3846a6a9563f382212c")

# iPlug2 expects the Steinberg SDK at Dependencies/IPlug/VST3_SDK but its
# source checkout does not contain that directory. BRKNAM populates the exact
# expected location from the official 3.8.0 aggregate repository and its
# pinned submodules instead of running iPlug2's mutable setup script.
set(BRKNAM_VST3_SDK_REPOSITORY
    "https://github.com/steinbergmedia/vst3sdk.git")
set(BRKNAM_VST3_SDK_COMMIT
    "9fad9770f2ae8542ab1a548a68c1ad1ac690abe0") # VST SDK 3.8.0

set(BRKNAM_AUDIODSPTOOLS_REPOSITORY
    "https://github.com/sdatkinson/AudioDSPTools.git")
set(BRKNAM_AUDIODSPTOOLS_COMMIT
    "0827c6c2fc0deced568536142ea86f189e0b98a1") # 0.1.1

# Reference implementation used to compare routing, model loading, latency,
# normalization and DC-blocker behavior. BRKNAM does not copy its UI or code.
set(BRKNAM_NAM_PLUGIN_REFERENCE_COMMIT
    "96337e9ab6e3beb619459779bbb5c47e1b04d8c4")
