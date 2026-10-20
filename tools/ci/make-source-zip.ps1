<#
  CI: packs the complete source code of this build next to the installer, as the GPL asks:
  HEM-Elite-Rack, The Hybrid Core (submodule), the Steinberg ASIO SDK as built (GPLv3 option, with the one-line
  asiolist.cpp fix) and the parts of the Steinberg VST3 SDK the build fetched (MIT).
  Sets HEM_SOURCE_ZIP for the release step.
#>
param([string]$BuildDir = 'build\rack')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$base = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ($env:GITHUB_REF -eq 'refs/heads/main') { $ver = $base } else { $ver = "$base-dev.$($env:GITHUB_RUN_NUMBER)" }

$work  = Join-Path ([IO.Path]::GetTempPath()) ('hem-src-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $work "HEM-Elite-Rack-$ver-source"
New-Item -ItemType Directory -Force -Path $stage | Out-Null

function Export-Git([string]$repo, [string]$dest) {
    $tar = Join-Path $work ('src-' + [Guid]::NewGuid().ToString('N') + '.tar')
    git -C $repo archive --format=tar -o $tar HEAD
    if ($LASTEXITCODE -ne 0) { throw "git archive failed for $repo" }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    tar -xf $tar -C $dest
    if ($LASTEXITCODE -ne 0) { throw "tar failed for $repo" }
}

Export-Git $root $stage
Export-Git (Join-Path $root 'external\HybridCore') (Join-Path $stage 'external\HybridCore')

$asio = Join-Path $root 'external\HybridCore\external\asiosdk\sdk'
if (-not (Test-Path (Join-Path $asio 'common\asio.h'))) { throw "ASIO SDK not found at $asio" }
Copy-Item -Recurse -Force $asio (Join-Path $stage 'external\HybridCore\external\asiosdk\sdk')

$vst = Join-Path $root "$BuildDir\_deps\vst3sdk-src"
if (-not (Test-Path (Join-Path $vst 'LICENSE.txt'))) { throw "VST3 SDK sources not found at $vst" }
$vstDest = Join-Path $stage 'third-party\vst3sdk'
New-Item -ItemType Directory -Force -Path $vstDest | Out-Null
robocopy $vst $vstDest /E /XD .git /XF .git /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

@"
HEM-Elite-Rack $ver - complete source code of the installer released with it.

HEM-Elite-Rack and The Hybrid Core: GNU General Public License v3 or later (LICENSE).
Copyright (C) 2026 Steven Reliford / Hybrid Elite Mafia.

external\HybridCore\external\asiosdk\sdk - Steinberg ASIO SDK 2.3.4, used under its GPLv3 option
  (its LICENSE.txt). Downloaded from download.steinberg.net; the only change is in host\pc\asiolist.cpp,
  where 'delete lpdrv;' became 'delete[] lpdrv;' (memory allocated with new[]).
  ASIO is a trademark of Steinberg Media Technologies GmbH.
third-party\vst3sdk - Steinberg VST3 SDK v3.8.1 (base, cmake, pluginterfaces, public.sdk), MIT licence
  (its LICENSE.txt), unchanged. The build fetches it itself; this copy is here for completeness.

To build (Windows, Visual Studio 2022, CMake 3.28+):
  cmake --preset rack
  cmake --build --preset rack-release
(external\HybridCore\tools\fetch-asio-sdk.ps1 fetches the ASIO SDK again if it is ever missing.)
"@ | Out-File -FilePath (Join-Path $stage 'SOURCE-README.txt') -Encoding utf8

$zip = Join-Path $root "installer\output\HEM-Elite-Rack-$ver-source.zip"
New-Item -ItemType Directory -Force -Path (Split-Path $zip) | Out-Null
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$f = Get-Item $zip
Write-Host ("Source: {0}  ({1:N1} MB)" -f $f.Name, ($f.Length / 1MB))
"HEM_SOURCE_ZIP=$($f.FullName)" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
exit 0
