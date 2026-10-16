"""Binary-format contract tests (no Push required)."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
import namix_preset as mod

class NAMixPresetTests(unittest.TestCase):
    def test_state_matches_namix_v3_layout(self):
        data = mod.state("/data/brknam/models/test.nam", "")
        ver, *values = struct.unpack_from("<i13d", data)
        self.assertEqual(ver, 3)
        self.assertEqual(tuple(values), mod.DEFAULT_VALUES)
        pos = struct.calcsize("<i13d")
        size = struct.unpack_from("<i", data, pos)[0]
        self.assertEqual(data[pos + 4:pos + 4 + size], b"/data/brknam/models/test.nam\0")
        self.assertEqual(struct.unpack_from("<i", data, pos + 4 + size)[0], 1)

    def test_file_chunks_and_uid(self):
        component = mod.state("/data/brknam/models/amp.nam")
        blob = mod.vstpreset(component)
        self.assertEqual(blob[0:4], b"VST3")
        self.assertEqual(struct.unpack_from("<I", blob, 4)[0], 1)
        self.assertEqual(blob[8:40], mod.UID.encode("ascii"))
        offset = struct.unpack_from("<Q", blob, 40)[0]
        self.assertEqual(blob[offset:offset + 4], b"List")
        self.assertEqual(struct.unpack_from("<I", blob, offset + 4)[0], 3)
        chunks = [struct.unpack_from("<4sQQ", blob, offset + 8 + 20 * i) for i in range(3)]
        self.assertEqual([c[0] for c in chunks], [b"Comp", b"Cont", b"Info"])
        self.assertEqual(blob[chunks[0][1]:chunks[0][1]+chunks[0][2]], component)
        self.assertEqual(chunks[1][2], 0)

    def test_paths_and_validation(self):
        blob = mod.build("amp a2", "cab")
        self.assertIn(b"/data/brknam/models/amp a2.nam\0", blob)
        self.assertIn(b"/data/brknam/models/cab.wav\0", blob)
        for bad in ["../evil", "..", "bad/evil", "hi\\x00", " trailing "]:
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    mod.build(bad)

if __name__ == "__main__":
    unittest.main()
