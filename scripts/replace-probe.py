#!/usr/bin/env python3
"""Replace only the existing BRKNAM probe external with the fixed class name.

The Linux module must be from the new ABI-checked GitHub Actions archive.
The destination stays under /data; only this one existing module is replaced.
No settings, system files, Live process or models are touched.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import tempfile
import uuid

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("push_m4l",ROOT/"scripts/push-m4l.py")
base=importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

ROOT_DIR="/data/.local/share/Max 9/Packages/BRKNAM/externals"
MODULE="brknam_probe~.mxl_x86_64"

def candidate(archive):
    """Return the new binary only after checking all archive members and ABI."""
    base.inspect_archive(archive)
    with tarfile.open(archive,"r:gz") as tf:
        f=tf.extractfile("externals/"+MODULE)
        if f is None:
            raise ValueError("No probe module in archive")
        data=f.read()
    if b"brknam_probe~" not in data:
        raise ValueError("New module lacks explicitly registered Max class brknam_probe~")
    return data

def sshargs(args):
    key=Path(args.key).expanduser()
    if not key.is_file():
        raise ValueError("SSH identity not found: "+str(key))
    return ["-i",str(key),"-o","IdentitiesOnly=yes","-o","BatchMode=yes",
            "-o","StrictHostKeyChecking=yes","-o","ConnectTimeout=8"]

def call(cmd):
    subprocess.run(cmd,check=True)

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--archive",type=Path,required=True)
    p.add_argument("--host",default="push.local")
    p.add_argument("--user",default="ableton")
    p.add_argument("--key",default="~/.ssh/push3_rsa")
    p.add_argument("--confirm-replace",action="store_true")
    args=p.parse_args(argv)
    try:
        raw=candidate(args.archive.expanduser().resolve())
        digest=hashlib.sha256(raw).hexdigest()
        dest=ROOT_DIR+"/"+MODULE
        backup=dest+".before-tilde-fix"
        print("New probe:",len(raw),"bytes, SHA-256:",digest)
        print("Target:",dest)
        print("One-time backup:",backup)
        if not args.confirm_replace:
            print("DRY RUN ONLY. Re-run with --confirm-replace after saving and closing test Set.")
            return 0
        opts=sshargs(args)
        peer=f"{args.user}@{args.host}"
        suffix=uuid.uuid4().hex[:12]
        temp=ROOT_DIR+"/.brknam-probe-new-"+suffix
        check=("set -eu; "+
               f"test -d {shlex.quote(ROOT_DIR)}; "+
               f"test -f {shlex.quote(dest)}; "+
               f"test ! -e {shlex.quote(backup)}; "+
               f"test ! -e {shlex.quote(temp)}")
        call(["ssh",*opts,peer,check])
        with tempfile.TemporaryDirectory(prefix="brknam-replacement-") as d:
            local=Path(d)/MODULE
            local.write_bytes(raw)
            call(["scp",*opts,str(local),peer+":"+shlex.quote(temp)])
        apply=("set -eu; umask 077; "+
               f"test -f {shlex.quote(dest)}; "+
               f"test -f {shlex.quote(temp)}; "+
               f"test ! -e {shlex.quote(backup)}; "+
               f"cp -p {shlex.quote(dest)} {shlex.quote(backup)}; "+
               f"chmod 700 {shlex.quote(temp)}; "+
               f"mv -f {shlex.quote(temp)} {shlex.quote(dest)}; "+
               f"sha256sum {shlex.quote(dest)}")
        call(["ssh",*opts,peer,apply])
        print("Probe updated. Restart Push from its own UI before testing.")
        print("Rollback uses the saved *.before-tilde-fix module, with Live stopped.")
    except (OSError,ValueError,tarfile.TarError,subprocess.CalledProcessError) as e:
        print("BRKNAM:",e,file=sys.stderr)
        return 1
    return 0

if __name__=="__main__":
    raise SystemExit(main())
