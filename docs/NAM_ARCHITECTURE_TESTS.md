# A1 and A2 architecture validation

BRKNAM validates the NAM architectures that users are most likely to encounter without committing third-party captures or model weights to the repository.

## Deterministic fixtures

`NamArchitectureReferenceTests` constructs model configurations and weights at test runtime. The files are synthetic, compact, reproducible, and contain no captured amplifier data.

The covered model classes are:

- **A1 Standard** — the established two-stack WaveNet shape: a 16-channel layer array feeding an 8-channel layer array, ten layers per array, kernel size 3, powers-of-two dilations, Tanh activation, and `head_scale` 0.02.
- **A2 Lite** — the strict 23-layer A2 signature with three channels.
- **A2 Full** — the same strict signature with eight channels.

The A2 kernel-size, dilation, activation, gating, and head configuration follows the detector in the pinned NeuralAmpModelerCore revision. If upstream changes the accepted architecture, the fixture-count and detector assertions fail explicitly rather than silently testing a generic WaveNet.

## Reference method

For each architecture, the test creates one deterministic two-tone input and one deterministic weight vector.

A1 is rendered through:

1. generic WaveNet created directly from the config;
2. automatic `.nam` dispatch;
3. `NamCoreModel`, BRKNAM's production adapter.

A2 is rendered through:

1. generic WaveNet created directly from the config;
2. the explicit A2 fast factory;
3. automatic `.nam` dispatch;
4. `NamCoreModel`.

The automatic A2 implementation must have the same dynamic type as the explicit fast implementation and a different type from generic WaveNet. Their output vectors must then agree with the generic reference within the tolerance used by upstream Core tests.

## Block and sample-rate coverage

Reference renders use irregular block sizes, including single-frame calls, to catch state-rewind and block-boundary errors.

The production adapter is additionally exercised with:

- A1 Standard at a 44.1 kHz host rate;
- A2 Full at a 96 kHz host rate.

Both models are authored at 48 kHz, so these checks also validate the complete model-rate adaptation and latency-reporting path for the two architecture generations.

The test is part of the Core-enabled CI matrix on Linux, macOS, and Windows.
