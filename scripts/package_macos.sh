#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_dir="${1:-${repo_root}/build-plugin}"
dist_dir="${2:-${repo_root}/dist}"
version="${BRKNAM_PACKAGE_VERSION:-0.1.0-alpha.1}"
package_name="BRKNAM-${version}-macOS-universal"
stage_dir="${dist_dir}/${package_name}"
archive_path="${dist_dir}/${package_name}.zip"

find_bundle() {
  local suffix="$1"
  find "${build_dir}/out" -type d -name "BRKNAM.${suffix}" -print -quit
}

app_bundle="$(find_bundle app)"
vst3_bundle="$(find_bundle vst3)"
au_bundle="$(find_bundle component)"

for bundle in "${app_bundle}" "${vst3_bundle}" "${au_bundle}"; do
  if [[ -z "${bundle}" || ! -d "${bundle}" ]]; then
    echo "Required BRKNAM bundle is missing under ${build_dir}/out" >&2
    exit 1
  fi
done

rm -rf "${stage_dir}" "${archive_path}" "${archive_path}.sha256"
mkdir -p "${stage_dir}/Standalone" "${stage_dir}/VST3" "${stage_dir}/Audio Unit"

cp -R "${app_bundle}" "${stage_dir}/Standalone/"
cp -R "${vst3_bundle}" "${stage_dir}/VST3/"
cp -R "${au_bundle}" "${stage_dir}/Audio Unit/"
cp "${repo_root}/LICENSE" "${stage_dir}/LICENSE.txt"
cp "${repo_root}/NOTICE" "${stage_dir}/NOTICE.txt"
cp "${repo_root}/THIRD_PARTY_NOTICES.md" "${stage_dir}/THIRD_PARTY_NOTICES.md"
cp "${repo_root}/docs/ALPHA_TESTING.md" "${stage_dir}/README.md"

commit="${GITHUB_SHA:-unknown}"
printf 'BRKNAM %s\nSource commit: %s\nBuild: unsigned universal macOS alpha\n' \
  "${version}" "${commit}" > "${stage_dir}/BUILD-INFO.txt"

for bundle in \
  "${stage_dir}/Standalone/BRKNAM.app" \
  "${stage_dir}/VST3/BRKNAM.vst3" \
  "${stage_dir}/Audio Unit/BRKNAM.component"; do
  codesign --force --deep --sign - "${bundle}"
done

for binary in \
  "${stage_dir}/Standalone/BRKNAM.app/Contents/MacOS/BRKNAM" \
  "${stage_dir}/VST3/BRKNAM.vst3/Contents/MacOS/BRKNAM" \
  "${stage_dir}/Audio Unit/BRKNAM.component/Contents/MacOS/BRKNAM"; do
  lipo -verify_arch x86_64 arm64 "${binary}"
done

mkdir -p "${dist_dir}"
ditto -c -k --sequesterRsrc --keepParent "${stage_dir}" "${archive_path}"
shasum -a 256 "${archive_path}" > "${archive_path}.sha256"

printf 'Created %s\n' "${archive_path}"
