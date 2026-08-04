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
