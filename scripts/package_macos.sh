#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_dir="${1:-${repo_root}/build-plugin}"
dist_dir="${2:-${repo_root}/dist}"
version="${BRKNAM_PACKAGE_VERSION:-0.1.0-alpha.2}"
package_name="BRKNAM-${version}-macOS-universal"
stage_dir="${dist_dir}/${package_name}"
archive_path="${dist_dir}/${package_name}.zip"

find_bundle() {
  local suffix="$1"
  find "${build_dir}/out" -type d -name "BRKNAM.${suffix}" -print -quit
}

bundle_executable() {
  local bundle="$1"
  local plist="${bundle}/Contents/Info.plist"
  local executable_name=""
  local executable_path=""

  if [[ -f "${plist}" ]]; then
    executable_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${plist}" 2>/dev/null || true)"
  fi

  if [[ -n "${executable_name}" ]]; then
    executable_path="${bundle}/Contents/MacOS/${executable_name}"
  fi

  if [[ -z "${executable_path}" || ! -f "${executable_path}" ]]; then
    executable_path="$(find "${bundle}/Contents/MacOS" -maxdepth 1 -type f -perm -111 -print -quit 2>/dev/null || true)"
  fi

  if [[ -z "${executable_path}" || ! -f "${executable_path}" ]]; then
    echo "No bundle executable was found in ${bundle}" >&2
    find "${bundle}/Contents" -maxdepth 3 -print >&2 || true
    return 1
  fi

  printf '%s\n' "${executable_path}"
}

verify_bundle() {
  local bundle="$1"
  local executable=""
  local architectures=""

  executable="$(bundle_executable "${bundle}")"
  architectures="$(lipo -archs "${executable}")"
  echo "Verifying universal executable: ${executable} (${architectures})"

  if ! grep -qw x86_64 <<<"${architectures}" || ! grep -qw arm64 <<<"${architectures}"; then
    echo "Expected x86_64 and arm64 slices, found: ${architectures}" >&2
    return 1
  fi

  codesign --verify --deep --strict --verbose=2 "${bundle}"
}

app_bundle="$(find_bundle app)"
vst3_bundle="$(find_bundle vst3)"
au_bundle="$(find_bundle component)"

for bundle in "${app_bundle}" "${vst3_bundle}" "${au_bundle}"; do
  if [[ -z "${bundle}" || ! -d "${bundle}" ]]; then
    echo "Required BRKNAM bundle is missing under ${build_dir}/out" >&2
    find "${build_dir}/out" -maxdepth 5 -print >&2 || true
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
printf 'BRKNAM %s\nSource commit: %s\nBuild: ad-hoc-signed universal macOS alpha\nMinimum macOS: 11.0\n' \
  "${version}" "${commit}" > "${stage_dir}/BUILD-INFO.txt"

staged_app="${stage_dir}/Standalone/BRKNAM.app"
staged_vst3="${stage_dir}/VST3/BRKNAM.vst3"
staged_au="${stage_dir}/Audio Unit/BRKNAM.component"

for bundle in "${staged_app}" "${staged_vst3}" "${staged_au}"; do
  codesign --force --deep --sign - "${bundle}"
  verify_bundle "${bundle}"
done

mkdir -p "${dist_dir}"
ditto -c -k --sequesterRsrc --keepParent "${stage_dir}" "${archive_path}"
shasum -a 256 "${archive_path}" > "${archive_path}.sha256"

printf 'Created %s\n' "${archive_path}"
