# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

param(
  [string]$BuildDir = "build-plugin",
  [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$distPath = Join-Path $repoRoot $DistDir
$version = if ($env:BRKNAM_PACKAGE_VERSION) { $env:BRKNAM_PACKAGE_VERSION } else { "0.1.0-alpha.1" }
$packageName = "BRKNAM-$version-Windows-x64"
$stageDir = Join-Path $distPath $packageName
$archivePath = Join-Path $distPath "$packageName.zip"

$app = Get-ChildItem $buildPath -Recurse -File -Filter "BRKNAM.exe" |
  Where-Object { $_.FullName -match '[\\/]out[\\/]' } |
  Select-Object -First 1
$vst3 = Get-ChildItem $buildPath -Recurse -Directory -Filter "BRKNAM.vst3" |
  Where-Object { $_.FullName -match '[\\/]out[\\/]' } |
  Select-Object -First 1

if (-not $app) { throw "BRKNAM.exe was not found under $buildPath" }
if (-not $vst3) { throw "BRKNAM.vst3 was not found under $buildPath" }

Remove-Item $stageDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archivePath -Force -ErrorAction SilentlyContinue
Remove-Item "$archivePath.sha256" -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path (Join-Path $stageDir "Standalone") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir "VST3") | Out-Null

Copy-Item $app.FullName (Join-Path $stageDir "Standalone/BRKNAM.exe")
Copy-Item $vst3.FullName (Join-Path $stageDir "VST3") -Recurse
Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $stageDir "LICENSE.txt")
Copy-Item (Join-Path $repoRoot "NOTICE") (Join-Path $stageDir "NOTICE.txt")
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") (Join-Path $stageDir "THIRD_PARTY_NOTICES.md")
Copy-Item (Join-Path $repoRoot "docs/ALPHA_TESTING.md") (Join-Path $stageDir "README.md")

$commit = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { "unknown" }
@"
BRKNAM $version
Source commit: $commit
Build: unsigned Windows x64 alpha
"@ | Set-Content -Encoding utf8 (Join-Path $stageDir "BUILD-INFO.txt")

New-Item -ItemType Directory -Force -Path $distPath | Out-Null
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $archivePath -CompressionLevel Optimal
$hash = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path -Leaf $archivePath)" | Set-Content -Encoding ascii "$archivePath.sha256"

Write-Host "Created $archivePath"
