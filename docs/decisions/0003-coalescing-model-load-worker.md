# Decision 0003 — Coalescing model-load worker

**Status:** accepted  
**Date:** 2026-08-03

## Problem

Loading a `.nam` file includes filesystem access, JSON parsing, graph allocation, Core construction, reset, and prewarm. These operations are unsuitable for the audio callback and may take long enough that a user selects several newer search results before an older load completes.

A FIFO queue would waste CPU and memory preparing models that are already obsolete. It would also make rapid next/previous navigation feel delayed because the audible result would trail behind the current selection.

## Decision

BRKNAM uses one dedicated model-load worker per one-slot processor. Requests receive monotonically increasing generations. The worker may finish an in-flight operation, but only the latest requested generation is allowed to publish.

```text
request path A (generation 10)
  -> worker begins load A
request path B (generation 11)
request path C (generation 12)
  -> load A may finish, but generation 10 is obsolete and is destroyed
  -> worker skips B and snapshots latest request C
  -> load/prepare C
  -> publish C through RealtimeModelSlot
```

The worker intentionally does not attempt unsafe cancellation inside NeuralAmpModelerCore. Obsolescence is checked after loading/preparation and before publication. An obsolete prepared model is destroyed on the worker thread.

## Last-request-wins race

The request mutex remains held for the short final generation check and atomic publication. This prevents a newer request from arriving between those operations and guarantees that an older result cannot publish after it has become obsolete.

The mutex is never acquired by the audio callback.

## States and diagnostics

The observable states are:

- `idle` — no request has been made;
- `queued` — a newer generation is waiting or has superseded in-flight work;
- `loading` — the current generation is executing loader and preparation work;
- `published` — the generation has entered the processor's pending slot;
- `error` — the latest generation failed before publication;
- `stopped` — worker shutdown completed.

Status includes requested and completed generations, the current path, and a human-readable error. Loading failures do not detach or replace the currently audible model.

`wait_until_settled()` is a non-realtime coordination helper. Waiting for an obsolete generation succeeds when that generation or any newer generation reaches `published` or `error`, so superseded requests cannot leave waiters blocked forever.

## Detach

A detach is represented as a generation without a file load. It publishes a dry node through the same block-boundary and crossfade path as model changes.

## Retired-model collection

The worker calls `collect_retired_models()` after completed work and during idle timed waits. This makes it the normal single consumer of the realtime retirement queue and ensures old DSP graphs are destroyed away from the audio thread even when no new model is requested.

## Lifetime contract

The processor must be prepared before the worker is created and must outlive it. Worker destruction waits for any in-flight loader call to return, stops the thread, and performs a final retired-model collection.

The first plugin adapter will own members in this order:

```text
OneSlotProcessor processor;
ModelLoadWorker worker;  // destroyed before processor
```

## Testing

Tests cover:

- successful preparation and publication;
- structured loader errors without processor mutation;
- a blocked in-flight load superseded by a newer request;
- destruction of the obsolete result on the worker thread;
- click-free detach;
- periodic retired-model collection;
- a real end-to-end `.nam → NeuralAmpModelerCore → worker → processor` path.
