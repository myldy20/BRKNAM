#!/usr/bin/env python3
"""Reject native Max Linux builds needing newer libraries than Push 3.

Owner's Push: glibc 2.35; libstdc++.so.6.0.29 (GLIBCXX up to 3.4.29).
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import pathlib
import re
import subprocess
import sys

ABI_CAPS = {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 29), "CXXABI": (1, 3, 13)}
PATTERN = re.compile(r"(GLIBCXX|GLIBC|CXXABI)_(\d+(?:\.\d+)+)")

def version_tuple(s):
    return tuple(int(n) for n in s.split("."))

def find_versions(output):
    found = {name: set() for name in ABI_CAPS}
    for name, ver in PATTERN.findall(output):
        found[name].add(version_tuple(ver))
    return found

def assess(output):
    versions = find_versions(output)
    errors = []
    for name, max_version in ABI_CAPS.items():
        newer = sorted(v for v in versions[name] if v > max_version)
        if newer:
            requested = ".".join(map(str, newer[-1]))
            supported = ".".join(map(str, max_version))
            errors.append(f"{name}_{requested} exceeds Push {name}_{supported}")
    return errors

def inspect(path):
    if not path.is_file():
        raise ValueError(f"missing module: {path}")
    with path.open("rb") as f:
        elf = f.read(20)
    if len(elf) < 20 or elf[:4] != b"\x7fELF" or elf[4:6] != b"\x02\x01" or elf[18:20] != b"\x3e\x00":
        raise ValueError(f"expected x86-64 ELF module: {path}")
    table = subprocess.run(["objdump", "-T", str(path)], check=True, text=True, capture_output=True).stdout
    dyn = subprocess.run(["readelf", "-d", str(path)], check=True, text=True, capture_output=True).stdout
    versions = find_versions(table)
    report = [f"MODULE {path.name}"]
    for name, values in versions.items():
        txt = ".".join(map(str, max(values))) if values else "none (no versioned imports)"
        report.append(f"  {name}: {txt}")
    for lib in re.findall(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]", dyn):
        report.append(f"  NEEDED: {lib}")
    return report, assess(table)

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("modules", nargs="+", type=pathlib.Path)
    args = p.parse_args(argv)
    problems = []
    print("PUSH ABI CAPS: glibc 2.35; GLIBCXX 3.4.29; CXXABI 1.3.13")
    for path in args.modules:
        try:
            report, errs = inspect(path)
            print("\n" + "\n".join(report))
            problems.extend(str(path) + ": " + x for x in errs)
        except (ValueError, OSError, subprocess.CalledProcessError) as e:
            problems.append(str(path) + ": " + str(e))
    if problems:
        print("\nABI CHECK FAILED:\n" + "\n".join(problems), file=sys.stderr)
        return 1
    print("\nPASS: versioned symbols within observed Push limits.")
    print("Physical Max 9 loader and audio-thread compatibility remain UNTESTED.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
