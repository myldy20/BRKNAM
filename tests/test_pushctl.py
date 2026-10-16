"""Offline safety checks for the BRKNAM staging tool."""
import io
import pathlib
import sys
import tarfile
import tempfile
import unittest
from importlib.machinery import SourceFileLoader

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
import pushctl

class ArchiveTests(unittest.TestCase):
    def build(self, path, entries):
        with tarfile.open(path, "w:gz") as tf:
            for name, content, kind in entries:
                ti = tarfile.TarInfo(name)
                if kind == "file":
                    ti.size = len(content)
                    tf.addfile(ti, io.BytesIO(content))
                else:
                    ti.type = kind
                    tf.addfile(ti)

    def test_accepts_bundle(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            path = p / "good.tar.gz"
            self.build(path, [("NAMix.vst3/Contents/x86_64-linux/NAMix.so",
                               b"\x7fELF" + b"A" * 2048, "file")])
            got = pushctl.validate_archive(path, p / "out")
            self.assertTrue((got / "Contents/x86_64-linux/NAMix.so").exists())

    def test_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            path = p / "bad.tar.gz"
            self.build(path, [("NAMix.vst3/../../../bad", b"oops", "file")])
            with self.assertRaises(pushctl.StageError):
                pushctl.validate_archive(path, p / "out")

    def test_rejects_symlink(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            path = p / "link.tar.gz"
            self.build(path, [("NAMix.vst3/Contents/escape", b"", tarfile.SYMTYPE)])
            with self.assertRaises(pushctl.StageError):
                pushctl.validate_archive(path, p / "out")

    def test_rejects_empty_or_unrelated(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            path = p / "not-plugin.tar.gz"
            self.build(path, [("LICENSES/LICENSE", b"test", "file")])
            with self.assertRaises(pushctl.StageError):
                pushctl.validate_archive(path, p / "out")

if __name__ == "__main__":
    unittest.main()
