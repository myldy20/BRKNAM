"""Push-mapped diagnostic probe regression tests (all offline)."""
import importlib.util
import json
import pathlib
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]

def load(path,name):
    spec=importlib.util.spec_from_file_location(name,ROOT/"scripts"/path)
    m=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m

mapped=load("create-mapped-probe.py","create_mapped_probe")
amxd=load("build-probe-amxd.py","build_probe_for_mapped")

class MappedProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch=mapped.create(ROOT/"patchers/BRKNAM Probe.maxpat")["patcher"]

    def test_bank_name_matches_real_parameter(self):
        boxes={b["box"]["id"]:b["box"] for b in self.patch["boxes"]}
        dial=boxes["obj-9"]
        self.assertEqual(dial["maxclass"],"live.dial")
        self.assertEqual(dial["parameter_enable"],1)
        self.assertEqual(dial["saved_attribute_attributes"]["valueof"]["parameter_longname"],"Gain")
        self.assertEqual(dial["saved_attribute_attributes"]["valueof"]["parameter_initial"],[1.0])
        self.assertEqual(self.patch["parameters"]["obj-9"][0],"Gain")
        self.assertEqual(self.patch["parameters"]["parameterbanks"]["0"]["parameters"][0],"Gain")
        self.assertEqual(len(self.patch["parameters"]["parameterbanks"]["0"]["parameters"]),8)

    def test_audio_flows_through_native_probe_and_gain(self):
        edges={(tuple(x["patchline"]["source"]),tuple(x["patchline"]["destination"]))
               for x in self.patch["lines"]}
        self.assertIn((("obj-1",0),("obj-2",0)),edges)
        self.assertIn((("obj-2",0),("obj-12",0)),edges)
        self.assertIn((("obj-12",0),("obj-3",0)),edges)
        self.assertIn((("obj-9",0),("obj-10",0)),edges)
        self.assertIn((("obj-11",0),("obj-12",1)),edges)
        self.assertNotIn((("obj-2",0),("obj-3",0)),edges)
        self.assertIn((("obj-1",1),("obj-3",1)),edges)
        self.assertIn((("obj-8",0),("obj-4",0)),edges)

    def test_can_build_mapped_audio_effect_without_changing_source(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/"mapped.maxpat"
            p.write_text(json.dumps({"patcher":self.patch}))
            blob=amxd.build(p)
            result=amxd.inspect(blob)
            self.assertIn("parameterbanks",result["parameters"])
            self.assertEqual(result["project"]["amxdtype"],1633771873)
            self.assertEqual(result["parameters"]["parameterbanks"]["0"]["name"],"BRKNAM")
if __name__=="__main__":unittest.main()
