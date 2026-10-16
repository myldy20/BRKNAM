"""Test CLI command wiring without contacting a Push."""
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
import pushctl

class CommandTests(unittest.TestCase):
    def test_doctor_stays_readonly(self):
        with mock.patch.object(pushctl, "ssh") as ssh, mock.patch.object(pushctl, "scp") as scp:
            self.assertEqual(pushctl.main(["doctor", "--host", "push.local"]), 0)
            ssh.assert_called_once()
            scp.assert_not_called()

    def test_upload_ir_wiring(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "cab.wav"
            p.write_bytes(b"RIFF" + b"X" * 64)
            with mock.patch.object(pushctl, "ssh") as ssh, mock.patch.object(pushctl, "scp") as scp:
                self.assertEqual(pushctl.main(["upload-ir", "--ir", str(p)]), 0)
                self.assertEqual(ssh.call_count, 2)
                self.assertEqual(scp.call_args.args[1], p)

    def test_upload_preset_wiring(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "NAMix - amp.vstpreset"
            p.write_bytes(b"VST3" + b"Z" * 128)
            with mock.patch.object(pushctl, "ssh"), mock.patch.object(pushctl, "scp") as scp:
                self.assertEqual(pushctl.main(["upload-preset", "--preset", str(p)]), 0)
                self.assertEqual(scp.call_args.args[1], p)

    def test_upload_rejects_wrong_suffix_without_ssh(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "oops.txt"
            p.write_text("not an ir")
            with mock.patch.object(pushctl, "ssh") as ssh:
                self.assertEqual(pushctl.main(["upload-ir", "--ir", str(p)]), 1)
                ssh.assert_not_called()

if __name__ == "__main__":
    unittest.main()
