# BRKNAM — Push 3 Standalone NAM experiment

**Status: development prototype; not tested on an actual Push yet.**

BRKNAM is being rebuilt as a guitar/bass amplifier processor for Ableton Push 3 Standalone. The first milestone produces a **Linux x86_64 VST3 audio effect** using an **unmodified, commit-pinned [NAMix](https://github.com/rations/NAMix)** upstream, and provides safe tooling to inspect a Push and stage the plugin for Live's scanner.

This is a deliberate proof of concept. The resulting effect identifies itself as **NAMix**. It is **not** a finished BRKNAM-branded plugin, model browser, or Push-native UI.

## Why VST3?

[Ableton Push Hack's VST3 research](https://github.com/federico-pepe/ableton-push-hack/blob/main/docs/vst3-on-push3.md) demonstrates that Push's bundled Live can scan and instantiate Linux x86_64 VST3 instruments and effects through a hidden system-path scanning setting. Recent NAM Max for Live devices have **not yet been demonstrated working on Push 3 Standalone**, mainly because their third-party C++ externals ship for macOS/Windows rather than the Push Linux runtime. This is **not a ban on native C++**: Ableton's own [Max for Live production guidelines](https://github.com/Ableton/maxdevtools/blob/main/m4l-production-guidelines/m4l-production-guidelines.md) explicitly state that third-party externals must be compiled separately for Push 3 Linux. The Push filesystem already contains first-party Linux `.mxl_x86_64` externals. A Linux-built `neural~` external and Max for Live device are a **viable second route**, potentially preferable to hidden VST3 scanning. VST3 has the clearest recent end-to-end third-party proof of concept but is **unsupported by Ableton**.

## Quick start

On a Linux build host (or in an Ubuntu 22.04 x86_64 VM/container), install the dependencies described in [docs/BUILD.md](docs/BUILD.md), then:

```bash
./scripts/build-linux.sh
python3 -m unittest discover -s tests -v
python3 scripts/pushctl.py doctor --host push.local --user ableton --key ~/.ssh/push3_rsa
python3 scripts/pushctl.py stage --archive dist/BRKNAM-Push3-NAMix-0.9.0-linux-x86_64.tar.gz --host push.local --user ableton --key ~/.ssh/push3_rsa
```

**Stage is fail-closed:** it only copies a plugin into `/data/.vst3`, refuses to overwrite an existing installation, and never restarts Live or edits its preferences. Staging alone does not make the plugin usable.

To scan and actually load the effect, follow [docs/PUSH3-BRINGUP.md](docs/PUSH3-BRINGUP.md). Those operations are manual and require a full backup and a maintenance window. Never test this on Push during a performance.

## Scope

- Native Linux x86_64 effect; NAM/A2 loading is provided by upstream NAMix.
- NAMix already includes gate, tone stack, cabinet IR and a model/IR path state interface.
- Build pinned to Ubuntu 22.04 / glibc 2.35, with ABI and archive checks.
- Read-only SSH diagnostics; non-destructive plugin, model, and experimental preset staging.\n- Offline NAMix `.vstpreset` generator for model/IR paths (pending Push validation).
- Hardware sound, CPU load, latency, preset recall and Push encoder mapping: **still unverified**.

## Former BRKNAM

The previous macOS/Windows NAM library, iPlug2 plug-in, native DSP adapter, tests and documentation have been preserved in Git history and in the `archive/brknam-desktop-20260804` branch. They were not silently copied into this different product architecture. We may later reuse the mature model-swapping and resampling components in a proper BRKNAM VST3.

Tracked development: [Push 3 end-to-end validation issue](https://github.com/myldy20/BRKNAM/issues/19).

## Licenses and credits

Project code and tools: GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE). NAMix is an independent MIT-licensed upstream, compiled without local changes. The build downloads dependencies; no NAMix binaries or proprietary Push firmware are committed here. See [THIRD_PARTY.md](THIRD_PARTY.md).

**Unofficial.** Neither Ableton nor NAMix maintainers endorse or support this project.
