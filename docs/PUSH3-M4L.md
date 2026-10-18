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

## User package directories found on the actual Push

The owner confirmed AbletonOS 3.21, plus these two *existing* Max 9 directories:

- `/data/.local/share/Max 9/Packages`
- `/data/.config/Cycling '74/Max 9/Packages`

**Their existence alone does not prove which is on Max's active search path.** Inspect the contents and use Max console/search path evidence first. On the Mac, after checking out this branch, the read-only command is:

```sh
python3 scripts/push-m4l.py doctor --host push.local --key ~/.ssh/push3_rsa
```

When the active directory is confirmed, download **BRKNAM-Push3-M4L-Linux** from the [successful CI run](https://github.com/myldy20/BRKNAM/actions/runs/38963624643). GitHub's artifact is a ZIP containing a `.tar.gz`; extract the ZIP on your Mac, then dry-run:

```sh
python3 scripts/push-m4l.py stage \
  --archive /path/to/BRKNAM-Push3-M4L-linux-x86_64.tar.gz \
  --packages-root "/data/.local/share/Max 9/Packages"
```

The example root is **not yet confirmed active**. The tool rejects other directories, path-traversal archives, unexpected file types, oversized payloads and binary ABI reports without PASS, and refuses to overwrite an existing BRKNAM package. Without `--confirm-stage` the command contacts no remote host and changes nothing. Only after validating the active root and independently backing up relevant user files should you add `--confirm-stage` to perform an opt-in transfer. The script does not edit Max search paths, restart Live or install an `.amxd`.


## Real-device probe package successfully staged (2026-10-16)

The owner staged and confirmed the two ELF modules in
`/data/.local/share/Max 9/Packages/BRKNAM/externals/`:
`brknam_probe~.mxl_x86_64` (2.1 MB) and `neural~.mxl_x86_64` (5.1 MB).
**The Max runtime has not yet loaded either module**.

### Generate a test Audio Effect without editing Max on the Mac

The repository now contains an offline Audio Effect generator derived from the
**ampF framing actually used by Ableton's sample MaxDevTools .amxd** (meta value
0 and a NUL-terminated .maxpat JSON). Generate on the Mac after pulling PR #21:

```sh
git -C /tmp/brknam-push3 pull --ff-only
python3 /tmp/brknam-push3/scripts/build-probe-amxd.py \
  --output ~/Downloads/"BRKNAM Probe.amxd"
```

Alternatively download the `.amxd` bundled in the newer GitHub Actions build.
Transfer that file using **Live's browser**: connect/pair Push, add the
`.amxd` to the computer's User Library first, and drag it onto the Push entry
under Places. Use a separate empty audio track or throwaway set. If Live on the
Mac reports a missing `brknam_probe~` object, that is expected because the
current module is Linux-only; the test itself belongs on Push.

The diagnostic device sends a `ping` from `live.thisdevice` after
initialization, and connects its status outlet to `print BRKNAM_PROBE`.
A successful Linux module load should therefore produce a ready notification.
The left audio channel passes through the native C++ object; right channel is
a dry reference. Start at low volume.

If Max cannot discover `brknam_probe~`, examine its own log for "no such
object", "missing or incompatible", or "undefined symbol". A successful
installation directory by itself does not prove Max's search-path discovery.
Do not alter `/opt/push3` to force a load; revisit the Max package search path
or bundle the Linux external as a device dependency.

**Offline-generated .amxd is not yet certified as a functioning Live device.
Keep any test to a disposable Live Set.**

### BRKNAM Probe Mapped: one Push encoder

The first on-device diagnostic `BRKNAM Probe.amxd` was inserted successfully
on an audio track and appears on Push's device screen, displaying
**"No parameters mapped"**. That is normal for the original probe: it has
no `live.dial` or parameter bank. **It only proves the Max for Live
wrapper loaded; it does not prove that the `brknam_probe~` C++ object
resolved or processed audio.**

A separate variant `BRKNAM Probe Mapped.amxd` now contains:

- One **Gain** `live.dial` with a saved parameter, range 0–2, default 1;
- A persistent `BRKNAM` bank assigning Gain to Push encoder 1 (using the
  parameterbanks dictionary structure from Ableton's own CPU Reporter);
- A smoothing `line~` and `*~` AFTER the native `brknam_probe~`, so the
  audio must still travel through the C++ module;
- Left processed channel, right dry channel, and the original status auto-ping.

The explicit parameter bank is stored in the patch, so it does not rely on
a runtime `new` message during device initialization. The bank and
`.amxd` are **not yet tested on the real Push**.

Generate without replacing the original diagnostic device:

```sh
git -C /tmp/brknam-push3 pull --ff-only
python3 /tmp/brknam-push3/scripts/create-mapped-probe.py \
  --output /tmp/"BRKNAM Probe Mapped.maxpat"
python3 /tmp/brknam-push3/scripts/build-probe-amxd.py \
  --source /tmp/"BRKNAM Probe Mapped.maxpat" \
  --output ~/Downloads/"BRKNAM Probe Mapped.amxd"
```

Transfer the new `.amxd` through Live's Push browser into an empty test set,
keeping the old diagnostic device separate. If the Gain control shows up,
turn it between 0 and 1 while playing a low-level audio clip.
An encoder appearing does not yet establish that the native DSP was loaded:
verify left-channel audio passthrough and status/log messages.

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


## Correcting a native Max class-name mismatch (2026-10-18)

On the real Push, `BRKNAM Probe Mapped.amxd` loads, and its Gain appears
on the encoder, but left-channel output from `brknam_probe~` is silent at
all Gain settings.

Cycling '74 [Min API macro documentation](https://github.com/Cycling74/min-api/blob/main/include/c74_min_api.h)
shows that `MIN_EXTERNAL(classname)` derives the Max object name from the
**source filename**, replacing only the suffix `_tilde.cpp` with `~`.
Our probe source was named `brknam_probe.cpp`, so
`MIN_EXTERNAL(brknam_probe)` registered `brknam_probe` (no tilde).
The `.mxl_x86_64` file and the Max patch, however, use `brknam_probe~`.

Corrected source: `MIN_EXTERNAL_CUSTOM(brknam_probe, brknam_probe~)`, which
registers the exact Max object requested by the patch. This is a
**source-confirmed naming bug and likely cause** of the silence; actual
device playback must still confirm the fix.

The earlier Max and M4L test files remain. A new dry/C++ A/B diagnostic
can independently distinguish a Max-only signal route from the native
external route.

To update an already staged package, `push-m4l.py stage` must not be used:
it correctly refuses to overwrite existing BRKNAM contents. Close the
test Set, back up only our prior `brknam_probe~.mxl_x86_64` in user space,
replace that module atomically, then restart Push from its UI before
re-testing at low gain. Never change system Max directories or Live prefs.
