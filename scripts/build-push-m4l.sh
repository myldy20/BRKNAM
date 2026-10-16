#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build Linux C++ Max externals for standalone Push (experimental).
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="$ROOT/.build/neural_tilde"
BUILD="$ROOT/.build/neural-build"
DSPTOOL="$ROOT/.build/AudioDSPTools"
DSPREV="0827c6c2fc0deced568536142ea86f189e0b98a1"
PROBE="$ROOT/.build/brknam_probe~.mxl_x86_64"
ARTIFACT="$ROOT/.build/package"
ZIP="$ROOT/dist/BRKNAM-Push3-M4L-linux-x86_64.tar.gz"
REV="09abd17dc95e386027f777540b4c1ccf31edbb1a"
[[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]] || { echo "Requires Linux x86_64 host" >&2; exit 1; }
for tool in git cmake python3 "${CXX:-g++-12}" objdump nm tar; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"
mkdir -p "$ROOT/.build" "$ROOT/dist"
if [[ ! -d "$SOURCE/.git" ]]; then git clone -q https://github.com/apresta/neural_tilde.git "$SOURCE"; fi
git -C "$SOURCE" fetch --quiet --no-tags origin "$REV"
git -C "$SOURCE" checkout --quiet --detach "$REV"
git -C "$SOURCE" submodule update --init --recursive --quiet
[[ "$(git -C "$SOURCE" rev-parse HEAD)" == "$REV" ]] || { echo "Upstream revision mismatch" >&2; exit 1; }
# Older neural~ expected this header under NAM Core's Dependencies; the pinned
# NeuralAudio core now carries a different source layout.
if [[ ! -d "$DSPTOOL/.git" ]]; then git clone -q https://github.com/sdatkinson/AudioDSPTools.git "$DSPTOOL"; fi
git -C "$DSPTOOL" fetch --quiet --no-tags origin "$DSPREV"
git -C "$DSPTOOL" checkout --quiet --detach "$DSPREV"
test -f "$DSPTOOL/dsp/ResamplingContainer/ResamplingContainer.h" || {
  echo "Pinned AudioDSPTools missing ResamplingContainer" >&2; exit 1;
}

# Ubuntu 22.04's GCC 11/libstdc++ lacks C++20 atomic<shared_ptr>.
# Upstream's CMake currently pins C++17 and Windows/macOS target output.
# Modify only this throwaway pinned checkout for Linux build.
python3 - "$SOURCE/CMakeLists.txt" "$DSPTOOL/dsp/ResamplingContainer/ResamplingContainer.h" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
s=p.read_text()
key="set(CMAKE_CXX_STANDARD 17)"
if key not in s: raise SystemExit("Upstream CMake layout changed: review patch")
s=s.replace(key,"set(CMAKE_CXX_STANDARD 20)",1)
patch='''

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_target_properties(neural_tilde PROPERTIES SUFFIX ".mxl_x86_64")
  target_link_options(neural_tilde PRIVATE -static-libstdc++ -static-libgcc)
endif()
'''
if patch not in s:s+=patch
# Add the missing resampling header from the MIT-licensed pinned AudioDSPTools checkout.
s += chr(10) + 'target_include_directories(neural_tilde SYSTEM PRIVATE "' + str(p.parent.parent / 'AudioDSPTools/dsp') + '")' + chr(10)
p.write_text(s)
# GCC rejects the old recursive unqualified type alias in AudioDSPTools.
hdr = pathlib.Path(sys.argv[2])
source = hdr.read_text()
old = "using LanczosResampler = LanczosResampler<T, NCHANS, A>;"
new = "using LanczosResampler = ::dsp::LanczosResampler<T, NCHANS, A>;"
if old not in source:
    raise SystemExit("Unexpected AudioDSPTools ResamplingContainer layout")
hdr.write_text(source.replace(old, new, 1))
PY
cmake -S "$SOURCE" -B "$BUILD" -DSUPERBUILD=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
cmake --build "$BUILD" --target neural_tilde --parallel "${JOBS:-2}"
NEURAL="$(find "$BUILD" -name 'neural~.mxl_x86_64' -type f -print -quit)"
[[ -n "$NEURAL" ]] || { echo "Linux neural~ external not found" >&2; exit 1; }

DEVKIT="$SOURCE/deps/min-devkit/source/min-api"
HDR="$DEVKIT/max-sdk-base/c74support"
"$CXX" -std=c++17 -O2 -fPIC -shared -static-libstdc++ -static-libgcc \
  -DC74_MIN_API -DC74_NO_MIN_LIB_SUBMODULE \
  -I"$DEVKIT/include" \
  -I"$HDR/max-includes" -I"$HDR/msp-includes" \
  -I"$HDR/jit-includes" -I"$HDR" \
  "$ROOT/src/brknam_probe.cpp" -o "$PROBE"

validate() {
  local binary="$1"
  file "$binary" | grep -q 'ELF 64-bit.*x86-64' || { echo "Wrong architecture: $binary" >&2; exit 1; }
  nm -D "$binary" | grep -Eq '[[:space:]]ext_main$' || { echo "No exported ext_main: $binary" >&2; exit 1; }
}
validate "$NEURAL"
validate "$PROBE"

rm -rf "$ARTIFACT"
mkdir -p "$ARTIFACT/externals" "$ARTIFACT/patchers" "$ARTIFACT/LICENSES"
cp "$NEURAL" "$ARTIFACT/externals/neural~.mxl_x86_64"
cp "$PROBE" "$ARTIFACT/externals/brknam_probe~.mxl_x86_64"
cp "$ROOT/patchers/BRKNAM Probe.maxpat" "$ARTIFACT/patchers/"
cp "$SOURCE/LICENSE" "$ARTIFACT/LICENSES/neural-LICENSE"
cp "$DSPTOOL/LICENSE" "$ARTIFACT/LICENSES/AudioDSPTools-LICENSE"
cp "$ROOT/LICENSE" "$ARTIFACT/LICENSES/BRKNAM-LICENSE"
cp "$ROOT/NOTICE" "$ARTIFACT/LICENSES/BRKNAM-NOTICE"
# Fail closed against the Push's glibc 2.35 / libstdc++ 6.0.29.
python3 "$ROOT/scripts/check-push-abi.py" "$NEURAL" "$PROBE" > "$ARTIFACT/ABI-REPORT.txt"
cat "$ARTIFACT/ABI-REPORT.txt"
tar -czf "$ZIP" -C "$ARTIFACT" externals patchers LICENSES ABI-REPORT.txt
echo "Built: $ZIP"
echo "This binary is NOT hardware-validated yet."
