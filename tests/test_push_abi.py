"""ABI regression tests for GLIBC, GLIBCXX and CXXABI bounds."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pushabi", ROOT / "scripts/check-push-abi.py")
abi = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(abi)

class PushABITests(unittest.TestCase):
    def test_gcc11_versions_pass(self):
        symbols = "(GLIBC_2.35) f\n(GLIBCXX_3.4.29) g\n(CXXABI_1.3.13) h\n"
        self.assertEqual(abi.assess(symbols), [])
    def test_gcc12_glibcxx_fails(self):
        self.assertTrue(any("GLIBCXX_3.4.30" in x for x in abi.assess("(GLIBCXX_3.4.30) f")))
    def test_glibc_236_fails(self):
        self.assertTrue(any("GLIBC_2.36" in x for x in abi.assess("(GLIBC_2.36) f")))
    def test_cpp_static_link_passes_without_imports(self):
        self.assertEqual(abi.assess("(GLIBC_2.34) pthread"), [])
    def test_numeric_version_comparison(self):
        self.assertLess(abi.version_tuple("3.4.9"), abi.version_tuple("3.4.29"))

if __name__ == "__main__":
    unittest.main()
