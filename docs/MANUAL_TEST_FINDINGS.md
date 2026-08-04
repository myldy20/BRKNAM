# Manual alpha test findings

This file records release-blocking findings from hands-on testing of downloadable builds.

## 0.1.0-alpha.1

- macOS UI text was invisible because the font resource was not packaged or loaded.
- Clicking an editable knob value could crash in the native Cocoa text-entry path.

## 0.1.0-alpha.2

- The model loader can report `published` even when the realtime audio callback has not consumed the pending model.
- The standalone shell does not expose whether the stream is running or whether any input/output signal is present.
- Manual diagnosis currently requires toggling model bypass and output normalization, which is not acceptable product UX.

Required follow-up:

- expose audio callback state, channel counts and input/output peak levels;
- distinguish a prepared/pending model from a model active on the audio thread;
- provide direct access to standalone audio preferences;
- keep RAW as the recommended diagnostic mode and explain BYPASS isolation in the UI/test guide.

## 0.1.0-alpha.3

- The new diagnostics correctly reported `AUDIO: OFF` and `Model ready — waiting for audio thread`.
- With the one-channel `MacBook Air Microphone`, pinned iPlug2 left `Input 1 (L)` empty because its input-list loop added the only channel only to `Input 2 (R)`.
- A saved sample rate unsupported by the selected input/output pair left the sample-rate combo box with no active selection instead of choosing a common supported rate.
- The pinned standalone host ignored the selected first hardware input/output channels when opening the RtAudio stream.

Implemented for `0.1.0-alpha.4`:

- standalone is explicitly mono input to stereo output while VST3/AU retain mono/stereo host routing;
- channel 1 and all additional hardware channels populate both input selectors correctly;
- an unavailable saved sample rate falls back to a common supported rate, preferring 48 kHz;
- the selected first hardware input/output channels are passed to RtAudio;
- the pinned dependency is modified by an asserted, idempotent project-owned CMake patch so a future iPlug2 pin change fails visibly instead of silently drifting.
