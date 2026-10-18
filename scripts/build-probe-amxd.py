#!/usr/bin/env python3
"""Make a Max 9.1-compatible Audio Effect .amxd from a .maxpat.

Binary framing is taken from Ableton's official MaxDevTools DoneLoading.amxd:
  ampf | uint32 4 | aaaameta | uint32 4 | uint32 0
  ptch | uint32 (len(UTF-8 patcher JSON) + 1) | JSON | NUL.
Unlike a Max save, this is an *offline-assembled* test device; hardware
and Live/Max loading are still unverified.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
import pathlib
import struct
import sys

HEADER = b"ampf" + struct.pack("<I", 4) + b"aaaameta" + struct.pack("<II", 4, 0) + b"ptch"

def build(path):
    raw = pathlib.Path(path).read_bytes()
    payload = json.loads(raw)
    if "patcher" not in payload or not isinstance(payload["patcher"], dict):
        raise ValueError("expected a Max .maxpat JSON with top-level patcher")
    p = payload["patcher"]
    objects = [x["box"].get("text", "") for x in p.get("boxes", [])]
    for required in ("plugin~", "plugout~", "brknam_probe~", "live.thisdevice"):
        if required not in objects:
            raise ValueError(f"missing required Max object: {required}")
    if "parameters" in p:
        banks = p["parameters"].get("parameterbanks") or {}
        if banks and all(not bank.get("parameters") for bank in banks.values()):
            raise ValueError("empty parameterbanks can crash the Live Max parameter handler")
    if p.get("project", {}).get("amxdtype") != 1633771873:
        raise ValueError("expected an audio-effect Max project type")
    data = raw + (b"" if raw.endswith(b"\x00") else b"\x00")
    return HEADER + struct.pack("<I", len(data)) + data

def inspect(blob):
    if blob[:28] != HEADER:
        raise ValueError("invalid ampf header")
    if len(blob) < 33:
        raise ValueError("truncated .amxd")
    size = struct.unpack_from("<I", blob, 28)[0]
    if len(blob) != size + 32 or not blob.endswith(b"\x00"):
        raise ValueError("invalid patch size or missing NUL terminator")
    return json.loads(blob[32:-1]).get("patcher", {})

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    default = pathlib.Path(__file__).resolve().parents[1] / "patchers/BRKNAM Probe.maxpat"
    p.add_argument("--source", type=pathlib.Path, default=default)
    p.add_argument("--output", type=pathlib.Path, default=pathlib.Path("BRKNAM Probe.amxd"))
    args = p.parse_args(argv)
    try:
        blob = build(args.source)
        inspect(blob)
        if args.output.exists():
            raise ValueError("refusing to overwrite " + str(args.output))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(blob)
        print(f"Created {args.output} ({len(blob)} bytes)")
        print("This diagnostic file is unverified on real Max for Live / Push.")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as e:
        print(f"BRKNAM: {e}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
