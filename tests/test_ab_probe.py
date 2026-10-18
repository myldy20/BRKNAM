"""Offline tests for two-path BRKNAM diagnostic (DRY vs native C++)."""
import importlib.util
from pathlib import Path
import json
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("ab",ROOT/"scripts/create-ab-probe.py")
ab=importlib.util.module_from_spec(spec)
spec.loader.exec_module(ab)
spec2=importlib.util.spec_from_file_location("amxd",ROOT/"scripts/build-probe-amxd.py")
amxd=importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(amxd)

class TestProbeAB(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p=ab.create(ROOT/"patchers/BRKNAM Probe.maxpat")["patcher"]
    def test_bank_uses_both_push_controls(self):
        bank=self.p["parameters"]["parameterbanks"]["0"]["parameters"]
        self.assertEqual(bank[:2],["Route","Gain"])
        self.assertEqual(len(bank),8)
    def test_dry_and_cpp_paths_are_independent(self):
        edges={(tuple(l["patchline"]["source"]),tuple(l["patchline"]["destination"]))
               for l in self.p["lines"]}
        self.assertIn((("obj-1",0),("obj-17",1)),edges)
        self.assertIn((("obj-2",0),("obj-17",2)),edges)
        self.assertIn((("obj-17",0),("obj-12",0)),edges)
        self.assertIn((("obj-12",0),("obj-3",0)),edges)
        self.assertNotIn((("obj-2",0),("obj-12",0)),edges)
        self.assertIn((("obj-1",1),("obj-3",1)),edges)
        self.assertIn((("obj-15",0),("obj-16",0)),edges)
        self.assertIn((("obj-16",0),("obj-17",0)),edges)
    def test_selector_defaults_to_dry(self):
        boxes={b["box"]["id"]:b["box"] for b in self.p["boxes"]}
        self.assertEqual(boxes["obj-17"]["text"],"selector~ 2 1")
        self.assertEqual(boxes["obj-15"]["saved_attribute_attributes"]["valueof"]["parameter_initial"],[0])
    def test_offline_amxd_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/"AB.maxpat"
            source.write_text(json.dumps({"patcher":self.p}))
            final=amxd.inspect(amxd.build(source))
            self.assertEqual(final["parameters"]["parameterbanks"]["0"]["name"],"BRKNAM")
if __name__=="__main__":
    unittest.main()
