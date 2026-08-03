# NAM sample-rate adaptation

NAM captures are evaluated at their capture sample rate. `NamCoreModel` therefore uses two paths:

- when the host and model rates match, audio is passed directly to `NeuralAmpModelerCore` with zero added latency;
- when the rates differ, BRKNAM converts host-rate input to the model rate, evaluates the model, then converts the result back to the host rate.

Older NAM files that do not report a sample rate are treated as 48 kHz, matching the established behavior of the official NAM plugin.

## Realtime contract

The rate adapter is prepared on `ModelLoadWorker` before publication. Preparation allocates the model-rate scratch buffers, initializes the Lanczos coefficient table, resets and prewarms the Core model at its own rate, and primes the two resamplers with silence.

After publication, the audio callback performs only bounded buffer writes, interpolation, Core processing, and buffer reads. It performs no allocation, destruction, locking, file access, JSON parsing, or coefficient initialization.

The current E2 adapter accepts integer-like rates from 8 kHz through 384 kHz and host block sizes up to 8192 frames. Unsupported preparation requests fail on the worker thread and leave the currently playing model unchanged.

## Latency

The input and output Lanczos stages are primed so the first host block always produces a complete output block. The number of host samples required to prime that round trip is stored in `ModelInfo::latency_samples`.

`OneSlotProcessor` reports that settled latency to the plugin adapter. During a model crossfade it reports the maximum latency of the old and new models, then switches to the new settled value after the transition.

## Validation

Core-enabled CI renders a known linear `.nam` fixture at matching 48 kHz and at mismatched 44.1 and 96 kHz host rates. The tests use changing block sizes, require finite output, verify the reported latency, measure round-trip gain, and exercise the complete worker/publication/audio path on Linux, macOS, and Windows.
