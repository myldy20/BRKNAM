"""Offline Max .amxd format tests, compared with Ableton MaxDevTools framing."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_probe", ROOT / "scripts/build-probe-amxd.py")
build_probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_probe)

class TestAMXD(unittest.TestCase):
    def test_ableton_ampf_header_and_payload(self):
        blob = build_probe.build(ROOT / "patchers/BRKNAM Probe.maxpat")
        self.assertEqual(blob[:4], b"ampf")
        self.assertEqual(struct.unpack_from("<I", blob, 4)[0], 4)
        self.assertEqual(blob[8:16], b"aaaameta")
        self.assertEqual(struct.unpack_from("<II", blob, 16), (4, 0))
        self.assertEqual(blob[24:28], b"ptch")
        self.assertEqual(struct.unpack_from("<I", blob, 28)[0], len(blob)-32)
        self.assertTrue(blob.endswith(b"\x00"))
        self.assertEqual(build_probe.inspect(blob)["project"]["amxdtype"], 1633771873)

    def test_device_has_audio_and_initialization_messages(self):
        patch = build_probe.inspect(build_probe.build(ROOT / "patchers/BRKNAM Probe.maxpat"))
        boxes={x["box"]["id"]:x["box"].get("text","") for x in patch["boxes"]}
        self.assertIn("plugin~",boxes.values())
        self.assertIn("plugout~",boxes.values())
        self.assertIn("brknam_probe~",boxes.values())
        self.assertIn("live.thisdevice",boxes.values())
        edges={(tuple(x["patchline"]["source"]),tuple(x["patchline"]["destination"]))
               for x in patch["lines"]}
        self.assertIn((("obj-8",0),("obj-4",0)),edges)
        self.assertIn((("obj-2",1),("obj-5",0)),edges)

    def test_invalid_bytes_rejected(self):
        with self.assertRaises(ValueError):
            build_probe.inspect(b"BAD!")

if __name__=="__main__":
    unittest.main()
