# Decision 0001 — E2 NAM Core integration baseline

**Status:** accepted for the first E2 slices  
**Date:** 2026-08-03

## Decision

BRKNAM will integrate the official `sdatkinson/NeuralAmpModelerCore` behind its own framework-neutral mono model interface. The initial reviewed pin is DSP **0.5.5**, commit `3cde95c354d5ba6da01316cad90b05cfc4855053`.

The plugin shell will later use the official `iPlug2/iPlug2` repository, initially reviewed at commit `5c2df9dce3f5258acfeff3846a6a9563f382212c`. The framework is not allowed to leak into `include/brknam/audio`.

## Why Core 0.5.5

The selected Core revision contains recent fixes that matter directly to BRKNAM:

- LSTM processing was changed to avoid per-sample heap allocation;
- A2 fast-path prewarm was corrected to match the generic WaveNet path;
- A2 detection was tightened to avoid silently routing unsupported conditioned/gated models;
- WaveNet layer-array head dilation support was added;
- the public DSP version is 0.5.5.

Pinning the commit rather than following `main` makes reference-vector tests, release archives, and bug reports reproducible.

## Build boundary

The upstream top-level CMake project primarily builds its tools and assumes its own dependency layout. BRKNAM will therefore not blindly call `add_subdirectory()` on the upstream root.

A dedicated adapter target will:

1. fetch or consume the exact pinned source archive;
2. build the reviewed Core source set with its pinned Eigen/AudioDSPTools dependencies;
3. compile the Core for float samples (`NAM_SAMPLE_FLOAT`) to match the BRKNAM audio API;
4. keep the A2 fast path enabled;
5. expose only a private adapter implementing `brknam::audio::MonoModel`.

The normal library/index build remains usable without downloading NAM Core or iPlug2.

## Loading contract

Model file access, JSON parsing, allocation, Core construction, sample-rate adaptation, reset, and prewarm occur outside the audio callback. Only a fully prepared model may be presented to the realtime processor.

The first processor slice intentionally uses a stop-the-world non-owning attachment API. It exists to freeze routing, gains, normalization, DC blocking, errors, and latency semantics before concurrent graph publication is implemented. It must not be called while the audio callback can access the processor.

A later E2 slice replaces this with:

- worker-owned prepared graphs;
- block-boundary publication;
- bounded crossfade;
- deferred retirement on a non-audio thread;
- coalescing of obsolete load requests.

## Channel routing

The pinned upstream model API may expose channel counts, but BRKNAM E2 accepts only one-input/one-output NAM models.

External routing is deterministic:

- one input channel is used directly;
- two input channels are averaged into mono;
- the mono model is processed once per block;
- the processed mono stream is copied to one or two outputs.

This matches the behavior of the official plugin in a DAW and avoids running one stateful mono model twice as though it were two independent channels.

## Signal order

The E2 one-slot chain is:

```text
external inputs -> mono fold-down -> input trim -> NAM model/bypass
                -> 5 Hz DC blocker -> normalization -> output trim
                -> output broadcast
```

The DC blocker is after the model. NAM captures may generate DC themselves, and the official plugin places its 5 Hz high-pass after NAM/IR processing. This corrects the earlier provisional architecture diagram that placed the blocker before the model.

## Normalization and latency

Raw mode applies no model-derived gain. Normalized mode targets `-18 dB` using Core loudness metadata when present; absent or non-finite metadata falls back to raw gain.

The processor reports the active model latency when the model is not bypassed. The future resampling adapter is responsible for adding its own latency contribution before the plugin adapter reports the total to the host.

## Rejected alternatives

- **Fork the official plugin wholesale:** rejected because its library browser, threading, state, and UI concerns are coupled differently from BRKNAM's local-first product architecture.
- **Run one mono model independently for left and right:** rejected because a stateful DSP instance cannot represent two independent channel histories.
- **Load models in the callback:** rejected as filesystem, JSON, allocation, reset, and prewarm are not realtime-safe.
- **Track upstream `main`:** rejected because reproducible audio output and regression triage require exact revisions.
