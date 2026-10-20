<#
  CI: builds the Inno Setup installer from the Release build (app, self-test, snapshot tool, VST3 bundle).
  Version comes from the VERSION file. Builds of main use it as-is; other branches get "-dev.<run>".
  Refuses to build if the app has no ASIO in it (the SDK was missing) or if the licence files are missing.
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

# The app must have ASIO in it (Steinberg's SDK under its GPLv3 option).
$app = Get-ChildItem $bin -Recurse -Filter 'HEMEliteRack.exe' -File | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
if (-not $app) { throw 'HEMEliteRack.exe (Release) not found' }
$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($app.FullName))
if (-not $text.Contains('Could not open the ASIO driver')) { throw "refused: $($app.Name) was built without ASIO" }
Write-Host "ASIO in: $($app.Name)"
$asioLic = Join-Path $root 'external\HybridCore\external\asiosdk\sdk\LICENSE.txt'
if (-not (Test-Path $asioLic)) { throw "ASIO SDK licence not found at $asioLic" }

$defs = @("/DAppVersion=$ver", "/DBinDir=$bin", "/DAsioLicense=$asioLic")
$bundle = Get-ChildItem $bin -Recurse -Directory -Filter 'HEM-Elite-Rack.vst3' | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
$vstLic = Get-ChildItem (Join-Path $bin '_deps') -Recurse -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue |
          Where-Object { $_.Directory.Name -eq 'vst3sdk-src' } | Select-Object -First 1
if (-not $bundle -or -not $vstLic) { throw 'VST3 bundle or the SDK licence not found in the build' }
$defs += "/DVst3Bundle=$($bundle.FullName)", "/DVst3License=$($vstLic.FullName)"
Write-Host "VST3 bundle: $($bundle.FullName)"

& $iscc @defs (Join-Path $root 'installer\HEMEliteRack.iss') | Out-Host
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$exe = Get-ChildItem (Join-Path $root 'installer\output') -Filter 'HEM-Elite-Rack-Setup-*.exe' | Select-Object -First 1
Write-Host ("Installer: {0}  ({1:N1} MB, {2} bytes)" -f $exe.Name, ($exe.Length / 1MB), $exe.Length)
Write-Host ("SHA-256: {0}" -f (Get-FileHash $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant())
"HEM_INSTALLER=$($exe.FullName)" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
"HEM_VERSION=$ver"               | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
