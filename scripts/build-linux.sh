#!/usr/bin/env bash
# BRKNAM Linux VST3 proof of concept: build pinned upstream NAMix.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REF="3a8466563e60a98a0150fd5d3d6705291fc6e2e4"
VERSION="0.9.0"
SRC="$ROOT/.build/NAMix"
BUILD="$ROOT/.build/NAMix-build"
STAGE="$ROOT/.build/package"
DIST="$ROOT/dist"
ARCHIVE="$DIST/BRKNAM-Push3-NAMix-${VERSION}-linux-x86_64.tar.gz"

for cmd in git cmake c++ objdump tar; do
  command -v "$cmd" >/dev/null || { echo "missing build tool: $cmd" >&2; exit 1; }
done
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "Build only on Linux x86_64 (use an amd64 Ubuntu 22.04 container)." >&2
  exit 1
fi
if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Build host must run Linux." >&2
  exit 1
fi

mkdir -p "$ROOT/.build" "$DIST"
if [[ ! -d "$SRC/.git" ]]; then
  git clone https://github.com/rations/NAMix.git "$SRC"
fi
git -C "$SRC" fetch --no-tags origin "$REF"
git -C "$SRC" checkout --detach "$REF"
git -C "$SRC" submodule update --init --recursive
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$REF" ]] || { echo "upstream commit mismatch" >&2; exit 1; }

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build "$BUILD" --target NAMix --parallel "${JOBS:-2}"

BUNDLE="$(find "$BUILD" -type d -name NAMix.vst3 -print -quit)"
[[ -n "$BUNDLE" ]] || { echo "NAMix.vst3 bundle not built" >&2; exit 1; }
SO="$BUNDLE/Contents/x86_64-linux/NAMix.so"
[[ -f "$SO" ]] || { echo "expected Linux NAMix.so missing" >&2; exit 1; }
if command -v file >/dev/null; then
  file "$SO" | grep -Eq 'ELF 64-bit.*x86-64' || { echo "wrong plugin ELF architecture" >&2; exit 1; }
fi

MAX_GLIBC="$(objdump -T "$SO" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -Vu | tail -1 || true)"
[[ -n "$MAX_GLIBC" ]] || { echo "unable to inspect GLIBC requirements" >&2; exit 1; }
if [[ "$(printf '%s\n' '2.35' "$MAX_GLIBC" | sort -V | tail -1)" != "2.35" ]]; then
  echo "binary needs glibc $MAX_GLIBC, but Push reference glibc is 2.35" >&2
  exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/LICENSES"
cp -a "$BUNDLE" "$STAGE/NAMix.vst3"
cp "$SRC/LICENSE" "$STAGE/LICENSES/NAMix-LICENSE"
cp "$SRC/NOTICE" "$STAGE/LICENSES/NAMix-NOTICE"
cat >"$STAGE/BUILD-INFO.txt" <<EOF
BRKNAM Push 3 NAM proof of concept
Upstream: https://github.com/rations/NAMix
NAMix commit: $REF
NAMix version: $VERSION
Target: Linux x86_64, Ubuntu 22.04 / glibc <= 2.35
Maximum GLIBC symbol requirement: $MAX_GLIBC
Status: not verified on physical Push 3
EOF
tar -C "$STAGE" -czf "$ARCHIVE" NAMix.vst3 LICENSES BUILD-INFO.txt
echo "Built: $ARCHIVE"
