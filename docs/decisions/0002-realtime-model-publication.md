# Decision 0002 — Realtime model publication and retirement

**Status:** accepted  
**Date:** 2026-08-03

## Problem

A NAM model is a large stateful DSP graph. Loading, preparing, prewarming, resetting, and destroying it may allocate memory and execute arbitrary amounts of non-realtime work. None of those operations may occur in the audio callback.

A simple atomic `shared_ptr` is not sufficient. Although exchanging it can be lock-free on some platforms, releasing the last reference in the callback can run the complete model destructor on the audio thread.

## Ownership path

BRKNAM uses an explicit single-publisher/single-audio-consumer path:

```text
model-loader/UI thread
  owns prepared unique_ptr<MonoModel>
       |
       | publish_prepared_model()
       v
atomic pending pointer
       |
       | accepted only at an audio block boundary
       v
audio-owned current + fade-from nodes
       |
       | crossfade completes
       v
bounded SPSC retired queue
       |
       | collect_retired_models()
       v
model-loader/UI thread destroys retired models
```

A published model is immutable from the ownership system's point of view. Its internal DSP state is used only by the audio thread after publication.

## Pending-request coalescing

Only the newest not-yet-consumed model request matters. Publishing replaces the existing pending node atomically; the superseded pending model is destroyed immediately on the publisher thread.

This prevents rapid browser navigation from building an unbounded queue of models that the user no longer wants.

## Block-boundary acceptance

The audio thread checks the pending slot once per processing block. It never accepts a second model while a crossfade is active or while a completed old model cannot yet enter the retired queue.

This produces deterministic ownership transitions and prevents overlapping multi-model fades.

## Crossfade

Model changes use a 20 ms linear crossfade. Both the old and new models process the same mono input while the transition is active. Loudness normalization, when enabled, is calculated independently for both sides before interpolation.

The processor reports the maximum latency of the two active models during the crossfade and the settled current-model latency after it completes. A transition to dry keeps the old latency until the fade ends, then reports zero.

The crossfade itself does not align models with different latencies. Host-level latency changes and future resampling latency must therefore be stabilized before plugin publication semantics are considered final.

## Deferred destruction

Completed old nodes are pushed into a bounded SPSC queue. `collect_retired_models()` is the only normal runtime path that destroys those nodes and must be called from a non-audio thread.

The queue capacity is deliberately finite. If it fills, the audio thread stores one waiting-retirement node and stops accepting additional pending swaps. It does not allocate, block, drop ownership, or delete a model in the callback. Once the non-realtime collector frees queue capacity, swapping resumes.

## Dry state

Dry is represented as a normal published node whose model pointer is null. This lets transitions to and from dry use exactly the same atomic publication and crossfade machinery as model-to-model changes.

## Threading contract

- `prepare()` is stop-the-world and may destroy all current, pending, and retired models.
- `publish_prepared_model()` has one non-audio publisher at a time.
- `process()` has exactly one audio consumer.
- `collect_retired_models()` has exactly one non-audio consumer and must not run concurrently from multiple threads.
- parameter atomics may be written by UI/plugin-control threads and read by audio.

## Tests

The processor tests cover:

- crossfade start and end behavior;
- model-to-model and model-to-dry transitions;
- maximum latency during transition;
- pending-request coalescing;
- proof that the old model is not destroyed during audio processing;
- explicit retire-queue saturation and recovery after collection.

A later realtime audit will additionally instrument allocation and lock calls inside the callback path.
