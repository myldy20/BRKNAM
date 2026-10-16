"""Static tests: package source, build safety and Max patch wiring."""
from pathlib import Path
import json
import unittest
ROOT=Path(__file__).resolve().parents[1]
class M4LSmoke(unittest.TestCase):
    def test_patcher_is_json_and_has_audio_route(self):
        p=json.loads((ROOT/"patchers/BRKNAM Probe.maxpat").read_text())["patcher"]
        boxes={b["box"]["id"]:b["box"].get("text","") for b in p["boxes"]}
        self.assertIn("brknam_probe~",boxes.values())
        self.assertIn("live.thisdevice",boxes.values())
        self.assertIn("plugin~",boxes.values())
        self.assertIn("plugout~",boxes.values())
        edges={tuple((tuple(x["patchline"]["source"]),tuple(x["patchline"]["destination"]))) for x in p["lines"]}
        self.assertIn((("obj-1",0),("obj-2",0)),edges)
        self.assertIn((("obj-2",0),("obj-3",0)),edges)
    def test_build_pinned_without_device_mutations(self):
        t=(ROOT/"scripts/build-push-m4l.sh").read_text()
        self.assertIn("09abd17dc95e386027f777540b4c1ccf31edbb1a",t)
        self.assertIn("SUPERBUILD=OFF",t)
        self.assertIn(".mxl_x86_64",t)
        for forbidden in ("sudo ssh","/boot","/opt/push3","Preferences.cfg","systemctl restart"):
            self.assertNotIn(forbidden,t)
    def test_probe_exports_max_entry(self):
        t=(ROOT/"src/brknam_probe.cpp").read_text()
        self.assertIn("MIN_EXTERNAL(brknam_probe)",t)
        self.assertIn('status.send("ready")',t)
if __name__=="__main__":unittest.main()
