# Push 3 Standalone bring-up and rollback

**Status: awaiting physical-device test. Do not assume success because the binary builds.** Test on a powered, idle Push connected to mains. Back up all Live sets, samples, User Library, and relevant preferences; confirm official Ableton recovery instructions before changing its software.

## Read-only checks

```bash
python3 scripts/pushctl.py doctor --host push.local --user ableton --key ~/.ssh/push3_rsa
```

This reports architecture, glibc if available, version folders, presence of the VST3 bundle, and bounded excerpts of plugin scanner output; it does not disclose licenses, credentials or model data.

## Stage plugin, without restarting anything

Download the GitHub Actions artifact or build locally. Then run:

```bash
python3 scripts/pushctl.py stage \
  --archive dist/BRKNAM-Push3-NAMix-0.9.0-linux-x86_64.tar.gz \
  --host push.local --user ableton --key ~/.ssh/push3_rsa
```

This creates `/data/.vst3/NAMix.vst3` **only if it does not exist**. If anything already exists at that path, the command refuses to replace it. It leaves AbletonOS system partitions untouched.

To copy your own legally obtained `.nam` file (no model activation yet):

```bash
python3 scripts/pushctl.py upload-model --model /path/to/model.nam \
  --host push.local --user ableton --key ~/.ssh/push3_rsa
```

This writes into `/data/brknam/models` without overwriting existing files.

## Scanner activation (manual, separate opt-in)

Stock Push Live has its VST3 **system-path scan disabled**. [The independent Push Hack research](https://github.com/federico-pepe/ableton-push-hack/blob/main/docs/vst3-on-push3.md) documents a binary `Preferences.cfg` field named `AreSystemPathsEnabled`; enabling it requires stopping Live, backing up the original file, a **version-checked binary edit**, and restarting the stack. Do not apply that recipe blindly against a different Live version: restore the backed-up preferences if its layout differs. BRKNAM intentionally does not automate this high-risk operation in its first phase.

Once scanning has been explicitly enabled, check `PluginScanner.txt` for `VST3: found: NAMix` or a clear error. If the plugin fails to scan, compare GLIBC and GLIBCXX versions with BUILD-INFO.txt, then check shared libraries; stop rather than disabling safety checks.

## Instantiate effect and load a model

Push's native browser hides third-party VST3. [Push Hack Browser Bridge](https://github.com/federico-pepe/push-hack-browser-bridge) can load scanned plugins into the selected Live track. Select an **audio track**, enable the Remote Script once, then load `NAMix` through its Plugins list or its `load_plugin:NAMix` command. This remains **untested for NAMix**.

NAMix cannot load a `.nam` path through a numeric VST3 parameter. Its model/IR browser is a desktop GUI and is inaccessible on Push. The first hardware experiment must additionally verify a preset/state-based model-loading path or implement a companion host bridge. **Until that milestone passes, staging and scanning do not constitute a usable NAM amp on Push.**

For initial audio verification record a short DI loop, monitor safely at low volume, test mono input/output with/without model, note DSP CPU, dropouts and restart behavior. Do not test with a loud monitor chain.

## Rollback

The user-level VST3 staged files are removable from `/data/.vst3` after stopping Live. Do not remove any bundle you did not install yourself; archive files you want to preserve. For a scanner-setting change restore the *exact* backed-up `Preferences.cfg` while Live is stopped, then restart Live normally. Never edit or flash `/boot`, `/opt`, licenses, firmware or kernel settings.

If full Push Hack was installed, uninstall it **before** updating AbletonOS: its display hook interposes on firmware-transfer USB I/O and can freeze an update.

## Offline model/IR presets (experimental)

Upstream NAMix v0.9.0 serializes a `getState` v3 structure containing the model and IR paths. We can generate a standard Steinberg `.vstpreset` binary with that state even without NAMix's desktop UI:

```bash
python3 scripts/namix_preset.py --model "my A2 lite" --ir "cab 212"
python3 scripts/pushctl.py upload-preset \
  --preset "dist/presets/NAMix - my A2 lite.vstpreset" \
  --host push.local --user ableton --key ~/.ssh/push3_rsa
```

Upload the corresponding `my A2 lite.nam` and `cab 212.wav` assets to `/data/brknam/models`:

```bash
python3 scripts/pushctl.py upload-model --model "/path/to/my A2 lite.nam" --key ~/.ssh/push3_rsa
python3 scripts/pushctl.py upload-ir --ir "/path/to/cab 212.wav" --key ~/.ssh/push3_rsa
``` Live indexes `.vstpreset` files in the User Library; Browser Bridge can address them by plugin/preset name.

**The binary format is covered by offline tests but the resulting preset is not yet confirmed to load NAMix and its model on Push 3.** This is the next hardware validation gate. A `.vstpreset` file holds absolute paths and will not be portable to a different filesystem layout without rewriting those paths.
