<#
  CI: builds the plug-ins-only installer (HEM-Elite-Rack VST3) from the Release build's VST3 bundle.
  Same version rule as the full installer: main uses VERSION as-is, other branches get "-dev.<run>".
  Sets HEM_VST3_INSTALLER for the next steps.
#>
param([string]$BuildDir = 'build\rack')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$bin  = Join-Path $root $BuildDir

$base = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ($base -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION must be x.y.z, got '$base'" }
if ($env:GITHUB_REF -eq 'refs/heads/main') { $ver = $base } else { $ver = "$base-dev.$($env:GITHUB_RUN_NUMBER)" }

$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) { choco install innosetup -y --no-progress | Out-Host }
if (-not (Test-Path $iscc)) { throw "ISCC.exe not found at $iscc" }

$bundle = Get-ChildItem $bin -Recurse -Directory -Filter 'HEM-Elite-Rack.vst3' | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
$vstLic = Get-ChildItem (Join-Path $bin '_deps') -Recurse -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue |
          Where-Object { $_.Directory.Name -eq 'vst3sdk-src' } | Select-Object -First 1
if (-not $bundle -or -not $vstLic) { throw 'VST3 bundle or the SDK licence not found in the build' }
$dll = Get-ChildItem $bundle.FullName -Recurse -Filter '*.vst3' -File | Select-Object -First 1
if (-not $dll) { throw "no plug-in binary inside $($bundle.FullName)" }
Write-Host "VST3 bundle: $($bundle.FullName)  (binary $($dll.Name), $([math]::Round($dll.Length/1MB,1)) MB)"

& $iscc "/DAppVersion=$ver" "/DVst3Bundle=$($bundle.FullName)" "/DVst3License=$($vstLic.FullName)" (Join-Path $root 'installer\HEMEliteRackVST3.iss') | Out-Host
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$exe = Get-ChildItem (Join-Path $root 'installer\output') -Filter 'HEM-Elite-Rack-VST3-Setup-*.exe' | Select-Object -First 1
Write-Host ("VST3 installer: {0}  ({1:N1} MB, {2} bytes)" -f $exe.Name, ($exe.Length / 1MB), $exe.Length)
Write-Host ("SHA-256: {0}" -f (Get-FileHash $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant())
"HEM_VST3_INSTALLER=$($exe.FullName)" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
