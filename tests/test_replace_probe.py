"""Fail-closed tests for replacing just BRKNAM's existing user-space probe."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("replace_probe",ROOT/"scripts/replace-probe.py")
rep=importlib.util.module_from_spec(spec)
spec.loader.exec_module(rep)
ELF=b"\x7fELF\x02\x01"+b"\x00"*12+b"\x3e\x00"+b"brknam_probe~\x00"

class TestReplaceProbe(unittest.TestCase):
    def archive(self,path,probe=ELF):
        entries={
            "externals/neural~.mxl_x86_64":ELF,
            "externals/brknam_probe~.mxl_x86_64":probe,
            "patchers/BRKNAM Probe.maxpat":b"{}",
            "ABI-REPORT.txt":b"PASS: versioned symbols within observed Push limits.",
        }
        with tarfile.open(path,"w:gz") as f:
            for name,data in entries.items():
                info=tarfile.TarInfo(name)
                info.size=len(data)
                f.addfile(info,io.BytesIO(data))
    def test_validated_binary(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/"probe.tar.gz"
            self.archive(f)
            self.assertEqual(rep.candidate(f),ELF)
    def test_misnamed_binary_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/"bad.tar.gz"
            self.archive(f,ELF.replace(b"brknam_probe~",b"brknam_probe "))
            with self.assertRaisesRegex(ValueError,"registered Max class"):
                rep.candidate(f)
    def test_dry_run_does_not_contact_device(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/"probe.tar.gz"
            self.archive(f)
            with patch.object(rep,"call") as c:
                self.assertEqual(rep.main(["--archive",str(f)]),0)
                c.assert_not_called()

if __name__=="__main__":
    unittest.main()
