"""No-network checks for safe Push Max package staging."""
import io
import importlib.util
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("push_m4l", ROOT / "scripts/push-m4l.py")
push = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(push)
ELF = b"\x7fELF" + b"\x02\x01" + b"\0" * 12 + b"\x3e\x00" + b"\0" * 1024

class StagingTests(unittest.TestCase):
    def make_archive(self, dst, extra=None, omit=None):
        entries = {
            "externals/neural~.mxl_x86_64": ELF,
            "externals/brknam_probe~.mxl_x86_64": ELF,
            "patchers/BRKNAM Probe.maxpat": b'{"patcher": {}}',
            "ABI-REPORT.txt": b"PASS: versioned symbols within observed Push limits.\n",
        }
        if omit:
            del entries[omit]
        if extra:
            entries.update(extra)
        with tarfile.open(dst, "w:gz") as f:
            for name, body in entries.items():
                info = tarfile.TarInfo(name)
                info.size = len(body)
                f.addfile(info, io.BytesIO(body))
    def test_valid_package(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"good.tar.gz"
            self.make_archive(archive)
            self.assertEqual(len(push.inspect_archive(archive)), 4)
    def test_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"bad.tar.gz"
            self.make_archive(archive, extra={"patchers/../../escape": b"no"})
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                push.inspect_archive(archive)
    def test_missing_external_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"bad.tar.gz"
            self.make_archive(archive, omit="externals/neural~.mxl_x86_64")
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                push.inspect_archive(archive)
    def test_no_abi_pass_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"bad.tar.gz"
            self.make_archive(archive, extra={"ABI-REPORT.txt": b"FAILED"})
            with self.assertRaisesRegex(ValueError, "ABI check"):
                push.inspect_archive(archive)
    def test_wrong_elf_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"bad.tar.gz"
            self.make_archive(archive, extra={"externals/neural~.mxl_x86_64": b"invalid"})
            with self.assertRaisesRegex(ValueError, "ELF"):
                push.inspect_archive(archive)
    def test_stage_dry_run_does_not_contact_push(self):
        with tempfile.TemporaryDirectory() as d:
            archive = pathlib.Path(d)/"good.tar.gz"
            self.make_archive(archive)
            with mock.patch.object(push, "run_checked") as run:
                code = push.main(["stage", "--archive", str(archive),
                    "--packages-root", push.ALLOWED_ROOTS[0]])
                self.assertEqual(code, 0)
                run.assert_not_called()

if __name__ == "__main__":
    unittest.main()
