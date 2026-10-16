# BRKNAM: native Linux Max external on Push 3

**No automated firmware changes. Real Push testing still required.** These instructions assume a Push 3 Standalone with SSH already enabled by the owner and Ableton Live Suite authorized. Do not modify /opt, /boot, system services, authorization or Preferences.cfg.

## Already known from earlier Push inspection

- Intel i3-1115G4 (x86_64), 2 cores / 4 threads, ~8 GB RAM, running AbletonOS v3.21 and Live 12.4.3 at the time of inspection.
- Max 9 resources and Linux `.mxl_x86_64` externals are present on the filesystem, confirming that Max can load native Linux extensions in principle.
- Old logs indicate glibc 2.35 in the Max builds, but **do not prove** the version of the currently installed C++ standard library.
- The license has subsequently been upgraded to Live 12 Suite.

Don't repeat the entire prior reverse-engineering collection.

## Phase A: desktop preparation

Download the GitHub Actions artifact containing the two `.mxl_x86_64` files, the `.maxpat`, upstream license and an ABI report. On the Mac, in Live Suite, create **Max Audio Effect**, edit it in Max, and paste/rebuild the layout in `patchers/BRKNAM Probe.maxpat`; alternatively open the source patch in Max and save it as an Audio Effect `.amxd` from Live after checking the inlets/outlets. Test first with the Mac's own platform external or a passthrough placeholder: Linux `.mxl_x86_64` cannot run on macOS.

A Max for Live device can bundle a platform-specific external when frozen; make sure its **Linux** artifact is actually shipped alongside and discoverable to Max on Push. Freezing a Mac-only binary does not magically produce a Linux binary.

## Phase B: inspect target ABI and Max search path

From **Mac Terminal**, when Push and Mac are on the same LAN:

```sh
ssh -i ~/.ssh/push3_rsa ableton@push.local '
  printf "ARCH: "; uname -m
  printf "LIBC: "; getconf GNU_LIBC_VERSION 2>/dev/null || true
  echo "C++ libraries:"
  find /lib /usr/lib /opt/push3 -name "libstdc++.so.6*" 2>/dev/null | head -8
  echo "Max external search locations:"
  find /data -maxdepth 6 -type d \( -name externals -o -name Packages \) 2>/dev/null | head -20
'
```

Compare the **ABI-REPORT.txt** in the artifact to the specific runtime on the Push. `GLIBCXX` and `CXXABI` symbol requirements matter alongside `GLIBC`. Do not swap system `libstdc++` to make an incompatible module load; fix the build/toolchain instead.

## Phase C: prove the external is discoverable

Work with a copy of the diagnostic `.amxd` device, keep input/output gains low, and back up User Library. Do **not** overwrite pre-existing externals. Use the exact user-writable Max package path discovered on the device and a uniquely named BRKNAM package. If the Max device is frozen, verify the included Linux file is actually the one loaded.

Expected evidence:
1. The Max object `brknam_probe~` resolves without a "no such object" error.
2. Sending `ping` emits the text `ready` from the object's status outlet.
3. A low-amplitude test signal passes through with unity gain, across multiple audio restarts.
4. A terminal/Max log reports no missing symbols, undefined C++ ABI, crashes or DSP overruns.

Stop here if the probe fails. This is a compatibility problem and does not justify patching the Push's firmware.

## Phase D: real neural audio

Once Phase C is confirmed, replace the probe with `neural~` in a throwaway test device. Load a known A2-Lite `.nam` model by absolute path after copying it into a user-writable, non-system directory. Confirm loading via the external's second (status) outlet, then monitor input/output and CPU. Test at 48 kHz, buffer 128 and 256 if selectable, plus stop/restart and high CPU load. Check that only the intended model is loaded, and measure latency and timing outliers before judging live suitability.

**Known upstream risk:** `neural~` uses a mutex in its model-swapping code, including in its audio callback; we will audit and address any deadline violations before performance use. The DSP wrapper is not declared performance-ready by the existence of a successful build.

## Rollback

Keep Live and Max stopped before removing your manually installed BRKNAM package from user-writable space. Restore the backed-up `.amxd` or Live Set. No changes to firmware, licensing, /opt, /boot or Live's VST3 preferences should have occurred.

Official guidance: [Ableton Max for Live Production Guidelines](https://github.com/Ableton/maxdevtools/blob/main/m4l-production-guidelines/m4l-production-guidelines.md), including Push-specific Linux external builds and `live.banks`.
