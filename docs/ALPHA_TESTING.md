# BRKNAM 0.1.0 alpha test guide

This is an **unsigned development build** intended for functional testing. It is not a public release or an installer.

## What this build contains

- one NAM model slot;
- A1 and A2 model support through the pinned NeuralAmpModelerCore;
- standalone application;
- VST3 on Windows and macOS;
- Audio Unit on macOS;
- input and output trim;
- model bypass;
- optional metadata-based output normalization;
- automatic model-rate resampling and host latency reporting;
- click-reduced background model replacement.

## Known limitations

- There is no IR loader yet. Amp-only captures will usually need a separate cab/IR plugin after BRKNAM. Full-rig or cab-inclusive NAM captures can be auditioned directly.
- The local library browser is not connected to the plugin UI yet; models are selected through `LOAD .NAM`.
- The selected model path is not yet serialized into DAW sessions. Parameter values are stored by the host, but model/session recall belongs to E3.
- Packages are not Developer ID signed, notarized, or Windows code-signed.
- These packages contain no bundled third-party NAM captures.

## macOS installation

The archive is universal and contains Intel and Apple Silicon binaries.

1. Copy `Standalone/BRKNAM.app` to `/Applications` or run it from the extracted folder.
2. Copy `VST3/BRKNAM.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
3. Copy `Audio Unit/BRKNAM.component` to `~/Library/Audio/Plug-Ins/Components/`.
4. Because this is an unsigned test archive, clear the download quarantine from the copied items:

```sh
xattr -dr com.apple.quarantine /Applications/BRKNAM.app
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/BRKNAM.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/BRKNAM.component
```

5. Restart the DAW. For AU rescanning, this can help:

```sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

In Ableton Live, enable VST3 and/or Audio Units in **Settings → Plug-Ins**, then run a full rescan if BRKNAM does not appear immediately.

## Windows installation

1. Run `Standalone/BRKNAM.exe` directly.
2. Copy the entire `VST3/BRKNAM.vst3` directory to one of these locations:
   - `%LOCALAPPDATA%\Programs\Common\VST3`
   - `C:\Program Files\Common Files\VST3`
3. Rescan VST3 plug-ins in the DAW.

Windows SmartScreen may warn because this alpha is not code-signed. Verify the SHA-256 file included beside the archive before running it.

## First useful test

Use a short clean DI guitar loop so every model receives identical input.

1. Start at 48 kHz with a 128- or 256-sample buffer.
2. Load a known-working A1 model.
3. Compare `ACTIVE` and `BYPASS` and check the input/output controls.
4. Load an A2 Lite or A2 Full model while audio is running.
5. Switch between several models quickly and listen for crashes, silence, harsh clicks, or stuck status text.
6. Repeat at 44.1 kHz and 96 kHz.
7. Try mono-in/mono-out and mono-in/stereo-out routing in the DAW.
8. Check that the displayed latency changes when required and that DAW playback stays aligned.
9. Try `NORMALIZED`; models without the required loudness metadata may sound unchanged, which is expected.

## Do not test yet

The following are not implemented in this alpha and should not be reported as regressions:

- IR/cab loading;
- library search inside the plugin;
- favorites, tags, ratings, previous/next navigation;
- preset portability;
- restoring the selected NAM after reopening a DAW session;
- online/TONE3000 access.

## Bug report checklist

Please include:

- operating system and CPU;
- standalone, VST3, or AU;
- DAW and version;
- sample rate and buffer size;
- NAM architecture if known: A1, A2 Lite, A2 Full, LSTM, WaveNet, etc.;
- whether the capture includes a cab;
- exact reproduction steps;
- whether bypass still passes audio;
- screenshot or short recording when the problem is audible or visual.

Do not upload commercial or redistribution-restricted NAM files to a public issue. A filename, metadata summary, and private reproduction description are usually sufficient.
