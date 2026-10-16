#!/usr/bin/env python3
"""Create experimental NAMix .vstpreset files for Push without a plugin GUI.

Serialized component state matches NAMix 0.9.0's getState() v3.
VST3 preset container follows Steinberg's Comp/Cont/Info chunk format.
Awaiting end-to-end test with Live on Push 3.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import pathlib
import re
import struct
from xml.sax.saxutils import quoteattr

UID = "8078153012284EB489676AE552A4FB2B"  # src/namids.h in NAMix 0.9.0
MODEL_BASE = "/data/brknam/models"
DEFAULT_VALUES = (
    0.0, 0.5, 0.5, 0.2, 0.5, 0.5, 0.5, 1.0, 1.0,
    0.5, 0.0, 0.0, 0.6,
)

def safe_filename(name: str, suffix: str) -> str:
    """Reject traversal and ambiguous paths before embedding in VST state."""
    if (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,149}", name)
            or name.endswith((".", " ")) or "/" in name or "\\" in name):
        raise ValueError(f"invalid {suffix} filename: {name!r}")
    return name + suffix

def state(model: str, ir: str = "", values=DEFAULT_VALUES) -> bytes:
    """NAMix NamProcessor getState: LE int32 v3, 13 doubles, two writeStr8."""
    if len(values) != 13 or not all(0.0 <= x <= 1.0 for x in values):
        raise ValueError("expected 13 normalized values in 0..1")
    def str8(path: str) -> bytes:
        b = path.encode("utf-8")
        if len(b) > 260000:
            raise ValueError("model path is too long")
        return struct.pack("<i", len(b) + 1) + b + b"\0"
    return (struct.pack("<i13d", 3, *values) + str8(model) + str8(ir))

def vstpreset(component: bytes) -> bytes:
    """VST3 file: 48 byte header + Comp, Cont, Info + List chunk table."""
    info = ("<?xml version='1.0' encoding='utf-8'?>\n<MetaInfo>"
            "<Attribute id='MediaType' value='VstPreset' type='string' flags='writeProtected'/>"
            "<Attribute id='PlugInCategory' value='Fx|Distortion' type='string' flags='writeProtected'/>"
            "<Attribute id='PlugInName' value='NAMix' type='string' flags='writeProtected'/>"
            "<Attribute id='PlugInVendor' value='rations' type='string' flags='writeProtected'/>"
            "</MetaInfo>").encode("utf-8")
    body = bytearray()
    entries = []
    for code, payload in ((b"Comp", component), (b"Cont", b""), (b"Info", info)):
        entries.append((code, 48 + len(body), len(payload)))
        body.extend(payload)
    offset = 48 + len(body)
    header = b"VST3" + struct.pack("<I", 1) + UID.encode("ascii") + struct.pack("<Q", offset)
    return header + body + b"List" + struct.pack("<I", len(entries)) + b"".join(
        code + struct.pack("<QQ", start, size) for code, start, size in entries
    )

def build(model_name: str, ir_name: str = "") -> bytes:
    model_path = MODEL_BASE + "/" + safe_filename(model_name, ".nam")
    ir_path = MODEL_BASE + "/" + safe_filename(ir_name, ".wav") if ir_name else ""
    return vstpreset(state(model_path, ir_path))

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--model", required=True, help="model basename without .nam, uploaded to /data/brknam/models")
    p.add_argument("--ir", default="", help="optional impulse response basename without .wav")
    p.add_argument("--out", type=pathlib.Path, help="output .vstpreset (default: dist/presets/NAMix - <model>.vstpreset)")
    args = p.parse_args(argv)
    try:
        data = build(args.model, args.ir)
        output = args.out or pathlib.Path("dist/presets") / ("NAMix - " + args.model + ".vstpreset")
        if output.suffix != ".vstpreset":
            p.error("output filename must have .vstpreset extension")
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            p.error(f"refusing to overwrite {output}")
        output.write_bytes(data)
        print("Wrote", output)
        print("UNVERIFIED: load through Live's Browser Bridge after the plugin is scanned.")
    except (ValueError, OSError) as e:
        p.error(str(e))

if __name__ == "__main__":
    main()
