#!/usr/bin/env python3
"""Create an independent BRKNAM Probe with one Push-mapped Gain parameter.

The original BRKNAM Probe.amxd is preserved. This experimental device
demonstrates that Push can enumerate and change a Live parameter while
the audio must still traverse the same brknam_probe~ native Linux external.

The explicit parameterbanks format follows Ableton's CPU Reporter.amxd
from Ableton/maxdevtools; the bank is saved in the maxpat, rather than
initialized by a message at runtime.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def create(src):
    patch = json.loads(pathlib.Path(src).read_text())
    p = patch["patcher"]
    by_id = {x["box"]["id"]: x["box"] for x in p["boxes"]}
    if "obj-9" in by_id:
        raise ValueError("This is already a mapped diagnostic patch")
    boxes = [
        {"id": "obj-9", "maxclass": "live.dial",
         "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
         "parameter_enable": 1,
         "patching_rect": [390, 66, 44, 48],
         "presentation": 1,
         "presentation_rect": [16, 16, 44, 48],
         "saved_attribute_attributes": {"valueof": {
             "parameter_longname": "Gain",
             "parameter_shortname": "Gain",
             "parameter_type": 0,
             "parameter_mmin": 0.0,
             "parameter_mmax": 2.0,
             "parameter_modmode": 0,
             "parameter_unitstyle": 0,
             "parameter_initial_enable": 1,
             "parameter_initial": [1.0],
         }},
         "varname": "Gain"},
        {"id": "obj-10", "maxclass": "message", "text": "$1 10",
         "numinlets": 2, "numoutlets": 1, "patching_rect": [385, 121, 46, 22]},
        {"id": "obj-11", "maxclass": "newobj", "text": "line~ 1.",
         "numinlets": 2, "numoutlets": 2, "outlettype": ["signal","bang"],
         "patching_rect": [385, 156, 62, 22]},
        {"id": "obj-12", "maxclass": "newobj", "text": "*~ 1.",
         "numinlets": 2, "numoutlets": 1, "outlettype": ["signal"],
         "patching_rect": [20, 159, 48, 22]},
        {"id": "obj-13", "maxclass": "newobj", "text": "live.banks",
         "numinlets": 1, "numoutlets": 1, "patching_rect": [385, 215, 75, 22]},
        {"id": "obj-14", "maxclass": "comment",
         "text": "Gain: mapped to Push encoder 1. Left channel tests native C++; right is dry.",
         "patching_rect": [81, 239, 440, 22]},
    ]
    p["boxes"].extend({"box": x} for x in boxes)
    def edge(source, outlet, destination, inlet):
        return {"patchline": {"source": [source, outlet], "destination": [destination, inlet]}}
    old = edge("obj-2", 0, "obj-3", 0)
    assert old in p["lines"], "Expected probe output wire is missing"
    p["lines"].remove(old)
    p["lines"] += [
        edge("obj-2", 0, "obj-12", 0),
        edge("obj-12", 0, "obj-3", 0),
        edge("obj-9", 0, "obj-10", 0),
        edge("obj-10", 0, "obj-11", 0),
        edge("obj-11", 0, "obj-12", 1),
    ]
    p["parameters"] = {
        "obj-9": ["Gain", "Gain", 0],
        "parameterbanks": {"0": {
            "index": 0,
            "name": "BRKNAM",
            "parameters": ["Gain", "-", "-", "-", "-", "-", "-", "-"]
        }},
        "inherited_shortname": 1,
    }
    p["rect"] = [0, 0, 580, 300]
    return patch

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--source",type=pathlib.Path,default=ROOT/"patchers/BRKNAM Probe.maxpat")
    p.add_argument("--output",type=pathlib.Path,default=ROOT/"dist/BRKNAM Probe Mapped.maxpat")
    args=p.parse_args(argv)
    if args.output.exists():
        p.error("refusing to overwrite existing file: "+str(args.output))
    try:
        patch=create(args.source)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(patch,indent=2)+"\n")
    except (OSError,ValueError,AssertionError,KeyError) as e:
        p.error(str(e))
    print(f"Created {args.output}")
    print("This is an offline-generated diagnostic patch; not hardware-tested.")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
