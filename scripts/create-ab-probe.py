#!/usr/bin/env python3
"""Build independent DRY/C++ A-B diagnostic from the mapped BRKNAM Probe.

ROUTE=0 sends input L directly to output L through Gain (initially DRY).
ROUTE=1 uses the C++ brknam_probe~; silence only on C++ narrows loader/DSP fault.
This leaves the original BRKNAM Probe and BRKNAM Probe Mapped unchanged.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("brknam_mapped",ROOT/"scripts/create-mapped-probe.py")
mapped=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mapped)

def create(source):
    data=mapped.create(source)
    p=data["patcher"]
    boxes={entry["box"]["id"]:entry["box"] for entry in p["boxes"]}
    def box(id, cls, rect, **kw):
        b={"id":id,"maxclass":cls,"patching_rect":rect}
        b.update(kw)
        p["boxes"].append({"box":b})
    def edge(src,n,dst,m):
        return {"patchline":{"source":[src,n],"destination":[dst,m]}}
    native_gain=edge("obj-2",0,"obj-12",0)
    if native_gain not in p["lines"]:
        raise ValueError("Mapped probe routing changed, refusing unsafe patch")
    p["lines"].remove(native_gain)
    box("obj-15","live.tab",[390,277,115,20],
        numinlets=1,numoutlets=3,outlettype=["","","float"],
        parameter_enable=1,presentation=1,presentation_rect=[80,18,115,18],
        saved_attribute_attributes={"valueof":{
            "parameter_enum":["DRY","CPP"],
            "parameter_initial":[0],"parameter_initial_enable":1,
            "parameter_longname":"Route","parameter_mmax":1,
            "parameter_modmode":0,"parameter_shortname":"Route",
            "parameter_type":2,"parameter_unitstyle":9}},
        varname="Route")
    box("obj-16","newobj",[390,315,42,22],text="+ 1",
        numinlets=2,numoutlets=1,outlettype=["int"])
    box("obj-17","newobj",[150,170,105,22],text="selector~ 2 1",
        numinlets=3,numoutlets=1,outlettype=["signal"])
    box("obj-18","comment",[280,365,290,20],
        text="Route: DRY=Max-only; CPP=native Linux probe.")
    p["lines"].extend([
        edge("obj-1",0,"obj-17",1),  # dry input
        edge("obj-2",0,"obj-17",2),  # native external
        edge("obj-17",0,"obj-12",0), # gain
        edge("obj-15",0,"obj-16",0),
        edge("obj-16",0,"obj-17",0),
    ])
    p["parameters"]["obj-15"]=["Route","Route",0]
    p["parameters"]["parameterbanks"]["0"]["parameters"]=[
        "Route","Gain","-","-","-","-","-","-"
    ]
    p["rect"]=[0,0,620,430]
    boxes["obj-14"]["text"]="Left: Route DRY or CPP; right: dry reference. Gain after selector."
    return data

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--source",type=Path,default=ROOT/"patchers/BRKNAM Probe.maxpat")
    p.add_argument("--output",type=Path,default=ROOT/"dist/BRKNAM Probe AB.maxpat")
    args=p.parse_args(argv)
    if args.output.exists():
        p.error("Refusing to overwrite existing "+str(args.output))
    try:
        patch=create(args.source)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(patch,indent=2)+"\n")
    except (OSError,ValueError,KeyError) as e:
        p.error(str(e))
    print("Created",args.output)
    return 0

if __name__=="__main__":
    raise SystemExit(main())
