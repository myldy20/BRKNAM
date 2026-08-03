# Initial iPlug2 product shell

E2 exposes the framework-neutral BRKNAM audio core through a deliberately small iPlug2 adapter. The shell is not the final library UI; its purpose is to make the validated one-slot player usable in a host and as a standalone application without moving model loading or realtime ownership into the plugin framework.

## Formats

With `BRKNAM_ENABLE_IPLUG2=ON`, CMake creates:

- `BRKNAM-app` on macOS and Windows;
- `BRKNAM-vst3` on macOS and Windows;
- `BRKNAM-au` on macOS.

The normal library, scanner and CLI builds do not download iPlug2 or the Steinberg SDK. Enabling the shell automatically enables the pinned NeuralAmpModelerCore adapter.

Example configuration:

```sh
cmake -S . -B build-plugin \
  -DBRKNAM_ENABLE_IPLUG2=ON \
  -DBRKNAM_BUILD_TESTS=OFF \
  -DBRKNAM_BUILD_TOOLS=OFF \
  -DIPLUG_DEPLOY_PLUGINS=OFF \
  -DCMAKE_BUILD_TYPE=Release
```

macOS:

```sh
cmake --build build-plugin --config Release \
  --target BRKNAM-app BRKNAM-vst3 BRKNAM-au
```

Windows:

```powershell
cmake --build build-plugin --config Release `
  --target BRKNAM-app BRKNAM-vst3
```

`IPLUG_DEPLOY_PLUGINS=OFF` keeps CI and local development builds inside the build tree instead of copying unsigned products into user or system plugin directories.

## Signal and loading path

The shell accepts one or two host input channels and one or two output channels. Stereo input is averaged to the mono NAM core; the processed mono stream is copied to each connected output channel.

The `.nam` picker submits only a filesystem path. `ModelLoadWorker` performs file I/O, JSON parsing, Core allocation, model-rate preparation, prewarming and publication outside the audio callback. The current model continues playing while another model loads. A failure is reported in the status line and does not replace the current graph.

The callback delegates to `OneSlotProcessor`, so it retains the tested block-boundary publication, 20 ms model crossfade, bounded retirement queue, deferred destruction, model-rate resampling, post-model DC blocker and fail-silent validation behavior.

## Parameters and latency

The first shell exposes four host-automatable parameters:

- input trim, −24 to +24 dB;
- output trim, −24 to +24 dB;
- model bypass;
- raw or metadata-normalized output.

The adapter polls the processor's reported latency from the non-audio idle callback and updates host PDC. During a model transition the core reports the maximum latency of both active models; after the crossfade it reports the settled model and resampler latency.

## Current UI and deliberate limits

The window is fixed at 650 × 260 and contains only model selection, model/status text, latency, trims, bypass and normalization. It does not contain the E4 SQLite browser, tags, favorites, search or online content.

The selected model path is retained across host audio resets during the current plugin instance. Session-safe state, path/hash recovery and portable presets belong to E3 and are not claimed by this shell.

## CI scope

The dedicated plugin matrix performs clean pinned-dependency builds with deployment disabled:

- standalone, VST3 and AU on macOS;
- standalone and VST3 on Windows.

These jobs prove that the products and bundle resources compile and link. Runtime host validation, repeated live switching under host load, signing, notarization and installers remain release-hardening work.
