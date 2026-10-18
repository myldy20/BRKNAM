# BRKNAM for Push 3 Standalone — Max for Live Linux experiment

**Status: experimental. A compiled Linux Max external has not yet been verified inside Max on a physical Push 3.**

BRKNAM's primary direction is now a **native Linux Max for Live audio effect** on Push 3 Standalone. We aim to run NAM A2-Lite captures in the device's existing Max 9 engine without changing AbletonOS, firmware, Live's license, or its hidden third-party VST3-scanning preference.

## Phase 0: verify the Linux Max external loader

The CI build contains two native x86-64 Max externals:

- `brknam_probe~.mxl_x86_64`: a tiny BRKNAM signal-through external with a `ping` message. This proves the load path, DSP registration and audio in/out without needing a neural model.
- `neural~.mxl_x86_64`: [Alessandro Presta's neural~](https://github.com/apresta/neural_tilde) compiled with an isolated, documented Linux-specific CMake adaptation. It already implements NAM/AIDA-X loading, resampling, and model switching. It is named `neural~` and includes the upstream MIT license.

Use the [test patch source](patchers/BRKNAM%20Probe.maxpat) in desktop Max 9 to create a Max Audio Effect, and follow [Push bring-up](docs/PUSH3-M4L.md) for on-device testing. The `.maxpat` source has not been verified as an installable standalone `.amxd`; opening and saving it as a Max for Live device in Live on the Mac is part of the validation.

Build (Ubuntu 22.04 x86_64, see [instructions](docs/LINUX-BUILD.md)):

```sh
bash scripts/build-push-m4l.sh
python3 -m unittest discover -s tests -v
```

A successful Linux build is **not evidence** Max accepts or runs the external on Push. Testing requires checking its Max 9 native ABI, the Max object search path, real-time stability, and model loading with a small legitimate A2-Lite `.nam` capture.

## Roadmap

1. Linux `brknam_probe~` external loads in the Push's Max 9 and performs unity-gain passthrough.
2. Linux `neural~` loads in the same environment and passes a silent/bypass smoke test.
3. NAM A2-Lite model + audio + CPU/xrun tests at 48 kHz, including stop/restart and scene switching.
4. Dedicated BRKNAM `.amxd` wrapper with eight Push encoders via `live.banks`, gain, mix, state recall, model browse/selection and trustworthy status indication.
5. Optional refactoring into BRKNAM-owned DSP using the desktop-era C++ code.

The former desktop BRKNAM is preserved at [archive/brknam-desktop-20260804](https://github.com/myldy20/BRKNAM/tree/archive/brknam-desktop-20260804). The [independent VST3 feasibility prototype](https://github.com/myldy20/BRKNAM/pull/20) remains available but is not the primary approach.

## Safety and licenses

**This repository is not endorsed by Ableton or Cycling '74.** Third-party Linux Max externals must match the particular Push firmware's Max engine, C++ runtime and dependency paths. Do not test during a live show. CI never SSHes into Push.

Project code: GPL-3.0-or-later, see [LICENSE](LICENSE) and [NOTICE](NOTICE). neural~ is MIT; its source is fetched from the official pinned upstream. See [THIRD_PARTY.md](THIRD_PARTY.md). No commercial `.nam` models are distributed.

Issue: [#19 — Push 3 Native NAM](https://github.com/myldy20/BRKNAM/issues/19).
