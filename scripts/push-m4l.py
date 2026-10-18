#!/usr/bin/env python3
"""Inspect, validate, and explicitly stage the BRKNAM Max 9 Linux package on Push.

No firmware, system files, authentication settings or running processes are changed.
Stage only when the owner has confirmed the chosen user package directory.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import pathlib
import re
import shlex
import subprocess
import sys
import tarfile
import uuid

ALLOWED_ROOTS = (
    "/data/.local/share/Max 9/Packages",
    "/data/.config/Cycling '74/Max 9/Packages",
)
REQUIRED = frozenset({
    "externals/neural~.mxl_x86_64",
    "externals/brknam_probe~.mxl_x86_64",
    "patchers/BRKNAM Probe.maxpat",
    "ABI-REPORT.txt",
})
MAX_BYTES = 64 * 1024 * 1024

def inspect_archive(path):
    """Validate a self-contained build artifact without extracting it."""
    if not path.is_file():
        raise ValueError(f"Missing archive: {path}")
    with tarfile.open(path, mode="r:gz") as tf:
        files = set()
        size = 0
        for member in tf.getmembers():
            name = pathlib.PurePosixPath(member.name)
            parts = name.parts
            if (name.is_absolute() or ".." in parts or not parts
                or parts[0] not in ("externals", "patchers", "LICENSES", "ABI-REPORT.txt")
                or not (member.isfile() or member.isdir())):
                raise ValueError(f"Unsafe tar entry: {member.name}")
            size += member.size
            if size > MAX_BYTES:
                raise ValueError("Archive exceeds 64 MiB expanded")
            if member.isfile():
                if member.name in files:
                    raise ValueError(f"Duplicate archive entry: {member.name}")
                files.add(member.name)
                if member.name in REQUIRED and member.name.endswith(".mxl_x86_64"):
                    f = tf.extractfile(member)
                    if f is None:
                        raise ValueError("Cannot read ELF header")
                    hdr = f.read(20)
                    if len(hdr) < 20 or hdr[:4] != b"\x7fELF" or hdr[4:6] != b"\x02\x01" or hdr[18:20] != b"\x3e\x00":
                        raise ValueError(f"Invalid x86-64 ELF: {member.name}")
        missing = REQUIRED - files
        if missing:
            raise ValueError("Incomplete package: " + ", ".join(sorted(missing)))
        report = tf.extractfile("ABI-REPORT.txt")
        if report is None:
            raise ValueError("Missing ABI-REPORT")
        report_text = report.read(8192).decode("utf-8", "replace")
        if "PASS: versioned symbols within observed Push limits." not in report_text:
            raise ValueError("ABI check has not passed")
    return sorted(files)

def ssh_opts(args):
    key = pathlib.Path(args.key).expanduser()
    if not key.is_file():
        raise ValueError(f"SSH key not found: {key}")
    return ["-i", str(key), "-o", "BatchMode=yes",
            "-o", "IdentitiesOnly=yes", "-o", "StrictHostKeyChecking=yes",
            "-o", "ConnectTimeout=8"]

def run_checked(argv):
    return subprocess.run(argv, check=True, capture_output=True, text=True)

def doctor(args):
    opts = ssh_opts(args)
    command = """printf 'DEVICE: '; uname -m
printf 'OS: '; grep '^VERSION_ID=' /etc/os-release || true
for d in "/data/.local/share/Max 9/Packages" "/data/.config/Cycling '74/Max 9/Packages"; do
  printf '\\nPACKAGE DIR: %s\\n' "$d"
  if [ -d "$d" ]; then ls -ld "$d"; find "$d" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -10; else echo ABSENT; fi
done"""
    result = run_checked(["ssh", *opts, f"{args.user}@{args.host}", command])
    print(result.stdout, end="")

def stage(args):
    if args.packages_root not in ALLOWED_ROOTS:
        raise ValueError("Refusing unapproved packages directory")
    archive = pathlib.Path(args.archive).expanduser().resolve()
    files = inspect_archive(archive)
    root = args.packages_root
    print(f"Validated: {archive.name} ({len(files)} files)")
    print(f"Target: {root}/BRKNAM")
    if not args.confirm_stage:
        print("Dry run only. Pass --confirm-stage after confirming the Max package path on Push.")
        return
    opts = ssh_opts(args)
    dest = f"{args.user}@{args.host}"
    tag = uuid.uuid4().hex[:16]
    inbound = f"/data/.brknam-upload-{tag}.tar.gz"
    tempdir = f"{root}/.brknam-stage-{tag}"
    final = f"{root}/BRKNAM"
    # fail closed: root must already exist, no existing BRKNAM package.
    check = f"set -eu; test -d {shlex.quote(root)}; test ! -e {shlex.quote(final)}; test ! -L {shlex.quote(final)}"
    run_checked(["ssh", *opts, dest, check])
    run_checked(["scp", *opts, str(archive), f"{dest}:{inbound}"])
    command = "; ".join([
        "set -eu",
        "umask 077",
        f"test -d {shlex.quote(root)}",
        f"test ! -e {shlex.quote(final)}",
        f"test ! -L {shlex.quote(final)}",
        f"mkdir {shlex.quote(tempdir)}",
        f"tar -xzf {shlex.quote(inbound)} -C {shlex.quote(tempdir)}",
        f"test -f {shlex.quote(tempdir + '/externals/neural~.mxl_x86_64')}",
        f"test -f {shlex.quote(tempdir + '/externals/brknam_probe~.mxl_x86_64')}",
        f"test ! -e {shlex.quote(final)}",
        f"mv {shlex.quote(tempdir)} {shlex.quote(final)}",
        f"rm -f {shlex.quote(inbound)}",
    ])
    run_checked(["ssh", *opts, dest, command])
    print(f"Staged to {final}. Live/Max were not restarted.")
    print("Runtime Max loading and M4L device wiring are still unverified.")

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="action", required=True)
    for name in ("doctor", "stage"):
        cmd = sub.add_parser(name)
        cmd.add_argument("--host", default="push.local")
        cmd.add_argument("--user", default="ableton")
        cmd.add_argument("--key", default="~/.ssh/push3_rsa")
        if name == "stage":
            cmd.add_argument("--archive", required=True)
            cmd.add_argument("--packages-root", choices=ALLOWED_ROOTS, required=True)
            cmd.add_argument("--confirm-stage", action="store_true")
    args = p.parse_args(argv)
    try:
        if args.action == "doctor":
            doctor(args)
        else:
            stage(args)
    except (ValueError, OSError, tarfile.TarError, subprocess.CalledProcessError) as e:
        print(f"BRKNAM: {e}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
