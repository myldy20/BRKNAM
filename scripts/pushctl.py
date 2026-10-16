#!/usr/bin/env python3
"""BRKNAM Push 3: read-only doctor and fail-closed staging (stdlib only)."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile
import uuid

STAGE_DIR = "/data/.vst3"
MODELS_DIR = "/data/brknam/models"
PRESETS_DIR = "/data/Music/Ableton/User Library/BRKNAM Presets"

class StageError(ValueError):
    pass

def validate_archive(archive: pathlib.Path, destination: pathlib.Path) -> pathlib.Path:
    """Extract only regular, relative paths from a trusted BRKNAM package."""
    with tarfile.open(archive, "r:gz") as tf:
        members = tf.getmembers()
        if not members:
            raise StageError("empty archive")
        total = 0
        for member in members:
            path = pathlib.PurePosixPath(member.name)
            if (path.is_absolute() or ".." in path.parts or not path.parts or
                    path.parts[0] not in {"NAMix.vst3", "LICENSES", "BUILD-INFO.txt"} or
                    not (member.isfile() or member.isdir())):
                raise StageError(f"unsafe archive entry: {member.name}")
            total += member.size
            if total > 150 * 1024 * 1024:
                raise StageError("expanded archive exceeds 150 MiB")
        for member in members:
            dest = destination.joinpath(*pathlib.PurePosixPath(member.name).parts)
            if member.isdir():
                dest.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                dest.parent.mkdir(parents=True, exist_ok=True)
                source = tf.extractfile(member)
                if source is None:
                    raise StageError(f"could not extract {member.name}")
                with source, dest.open("wb") as out:
                    import shutil
                    shutil.copyfileobj(source, out)
    plugin = destination / "NAMix.vst3"
    binary = plugin / "Contents" / "x86_64-linux" / "NAMix.so"
    if not binary.is_file() or binary.stat().st_size < 1024:
        raise StageError("not a NAMix Linux x86_64 VST3 bundle")
    if binary.open("rb").read(4) != b"\x7fELF":
        raise StageError("NAMix.so is not an ELF binary")
    return plugin

def commands(args):
    base = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
    if args.key:
        base += ["-i", str(pathlib.Path(args.key).expanduser()), "-o", "IdentitiesOnly=yes"]
    return base, f"{args.user}@{args.host}"

def ssh(args, shell_command: str) -> subprocess.CompletedProcess:
    opts, target = commands(args)
    return subprocess.run(["ssh", *opts, target, shell_command],
                          check=True, text=True)

def scp(args, source: pathlib.Path, destination: str, recursive: bool = False):
    opts, target = commands(args)
    cmd = ["scp", *opts]
    if recursive:
        cmd.append("-r")
    subprocess.run([*cmd, str(source), f"{target}:{destination}"], check=True)

DOCTOR = r"""
echo "ARCH:"; uname -m
echo "LIBC:"; (getconf GNU_LIBC_VERSION || true) 2>/dev/null
echo "LIVE:"; find /data/.config/Ableton -maxdepth 1 -name 'Live *' -type d 2>/dev/null | tail -3
echo "VST3:"; if [ -f /data/.vst3/NAMix.vst3/Contents/x86_64-linux/NAMix.so ]; then echo staged; else echo absent; fi
echo "LIBSTDCXX:"
lib="$(find /lib /usr/lib -name 'libstdc++.so.6*' -print -quit 2>/dev/null)"
if [ -n "$lib" ]; then strings "$lib" 2>/dev/null | grep '^GLIBCXX_' | sort -V | tail -1; else echo missing; fi
echo "SCANNER:"
find /data/.config/Ableton -maxdepth 2 -name PluginScanner.txt -type f 2>/dev/null | while read -r log; do
    grep -aE 'VST3: found: NAMix|NAMix.*(error|failed)|GLIBC_2\.' "$log" 2>/dev/null | tail -8
done
"""

def doctor(args):
    print("BRKNAM Push 3 preflight (read-only)")
    ssh(args, DOCTOR)

def stage(args):
    archive = pathlib.Path(args.archive).expanduser()
    if not archive.is_file():
        raise StageError(f"missing archive: {archive}")
    token = uuid.uuid4().hex[:12]
    incoming = f"{STAGE_DIR}/.brknam-stage-{token}"
    actual = f"{STAGE_DIR}/NAMix.vst3"
    with tempfile.TemporaryDirectory(prefix="brknam-") as temp:
        plugin = validate_archive(archive, pathlib.Path(temp))
        print("Validated ELF VST3 bundle; staging without restarting Push.")
        ssh(args, f"set -e; mkdir -p {STAGE_DIR}; test ! -e {actual}; test ! -e {incoming}")
        scp(args, plugin, incoming, recursive=True)
        # Atomically publish the staged directory; never clobber an existing bundle.
        ssh(args, f"set -e; test ! -e {actual}; mv {incoming} {actual}")
    print(f"Staged to {actual}. Live preferences and process are unchanged.")
    print("Next: review docs/PUSH3-BRINGUP.md. This is NOT activated yet.")

def upload_model(args):
    model = pathlib.Path(args.model).expanduser()
    if not model.is_file() or model.suffix.lower() != ".nam":
        raise StageError("expected an existing .nam model")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,149}\.nam", model.name, re.IGNORECASE):
        raise StageError("use a simple ASCII .nam filename (letters, numbers, _.- and spaces)")
    import shlex
    target = f"{MODELS_DIR}/{model.name}"
    ssh(args, f"set -e; mkdir -p {shlex.quote(MODELS_DIR)}; test ! -e {shlex.quote(target)}")
    tmp = f"/data/.brknam-upload-{uuid.uuid4().hex[:12]}"
    scp(args, model, tmp)
    ssh(args, f"set -e; test ! -e {shlex.quote(target)}; mv {tmp} {shlex.quote(target)}")
    print(f"Copied model to {target}. No activation or audio changes.")

def upload_ir(args):
    ir = pathlib.Path(args.model).expanduser()
    if not ir.is_file() or ir.suffix.lower() != ".wav":
        raise StageError("expected an existing .nam model")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,149}\.wav", ir.name, re.IGNORECASE):
        raise StageError("use a simple ASCII .wav filename (letters, numbers, _.- and spaces)")
    import shlex
    target = f"{MODELS_DIR}/{ir.name}"
    ssh(args, f"set -e; mkdir -p {shlex.quote(MODELS_DIR)}; test ! -e {shlex.quote(target)}")
    tmp = f"/data/.brknam-upload-{uuid.uuid4().hex[:12]}"
    scp(args, model, tmp)
    ssh(args, f"set -e; test ! -e {shlex.quote(target)}; mv {tmp} {shlex.quote(target)}")
    print(f"Copied IR to {target}. No activation or audio changes.")


def upload_preset(args):
    import shlex
    preset = pathlib.Path(args.preset).expanduser()
    if not preset.is_file() or preset.suffix.lower() != ".vstpreset":
        raise StageError("expected an existing .vstpreset file")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. ()-]{0,149}\.vstpreset", preset.name, re.IGNORECASE):
        raise StageError("use a simple ASCII .vstpreset filename")
    target = f"{PRESETS_DIR}/{preset.name}"
    tmp = f"/data/.brknam-upload-{uuid.uuid4().hex[:12]}"
    ssh(args, f"set -e; mkdir -p {shlex.quote(PRESETS_DIR)}; test ! -e {shlex.quote(target)}")
    scp(args, preset, tmp)
    ssh(args, f"set -e; test ! -e {shlex.quote(target)}; mv {tmp} {shlex.quote(target)}")
    print(f"Copied preset to {target}. Browser Bridge loading unverified.")

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("doctor", "stage", "upload-model", "upload-ir", "upload-preset"):
        command = sub.add_parser(name)
        command.add_argument("--host", default="push.local")
        command.add_argument("--user", default="ableton")
        command.add_argument("--key")
        if name == "stage":
            command.add_argument("--archive", required=True)
        if name == "upload-model":
            command.add_argument("--model", required=True)
    args = parser.parse_args(argv)
    try:
        {"doctor": doctor, "stage": stage, "upload-model": upload_model, "upload-ir": upload_ir, "upload-preset": upload_preset}[args.command](args)
    except (StageError, OSError, subprocess.CalledProcessError) as exc:
        print(f"BRKNAM: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
