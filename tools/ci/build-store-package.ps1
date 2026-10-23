<#
  CI: builds the Microsoft Store package (MSIX) of the HEM-Elite-Rack app from the Release build, then proves it:
    1. store\out\HEM-Elite-Rack-<ver>.msix  - unsigned, for Partner Center (the Store signs it).
    2. A test copy (plus Mesa's software OpenGL, because the runner has no GPU) is signed with a throw-away
       certificate, installed with Add-AppxPackage, started INSIDE its package with the plug-in picker open,
       screenshotted, checked, and removed again.
    3. The Windows App Certification Kit runs on it when the runner has it (the Store's own checks).
  Identity (Name / Publisher) comes from store\identity.json; version from VERSION (x.y.z -> x.y.z.0).
#>
param([string]$BuildDir = 'build\rack')
$ErrorActionPreference = 'Stop'
$root  = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$bin   = Join-Path $root $BuildDir
$store = Join-Path $root 'store'
$out   = Join-Path $store 'out'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null

$base = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ($base -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION must be x.y.z, got '$base'" }
$ver = "$base.0"
$id = Get-Content (Join-Path $store 'identity.json') -Raw | ConvertFrom-Json
Write-Host "Package: $($id.Name) / $($id.Publisher) / $ver  (test identity: $($id.IsTest))"

# ---- the Windows SDK tools ----
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$sdk = Get-ChildItem $kits -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'x64\makeappx.exe') } |
       Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw "makeappx.exe not found under $kits" }
$tools = Join-Path $sdk.FullName 'x64'
$makeappx = Join-Path $tools 'makeappx.exe'; $makepri = Join-Path $tools 'makepri.exe'; $signtool = Join-Path $tools 'signtool.exe'
Write-Host "Windows SDK $($sdk.Name)"

# ---- stage: the app, its art (all 120 plates and cards), licences, Store images, manifest ----
$app = Get-ChildItem $bin -Recurse -Filter 'HEMEliteRack.exe' -File | Where-Object { $_.FullName -match '\\rackapp\\Release\\' } | Select-Object -First 1
if (-not $app) { throw 'HEMEliteRack.exe (Release) not found' }
$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($app.FullName))
if (-not $text.Contains('Could not open the ASIO driver')) { throw "refused: $($app.Name) was built without ASIO" }
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Force -Path $stage, (Join-Path $stage 'licenses') | Out-Null
Copy-Item $app.FullName $stage
Copy-Item (Join-Path $app.DirectoryName 'assets') (Join-Path $stage 'assets') -Recurse
Copy-Item (Join-Path $store 'Images') (Join-Path $stage 'Images') -Recurse
Copy-Item (Join-Path $root 'installer\HOW-TO-USE.txt'), (Join-Path $root 'installer\THIRD-PARTY-NOTICES.txt') $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
$lic = @{
  'Steinberg-ASIO-SDK-LICENSE.txt' = 'external\HybridCore\external\asiosdk\sdk\LICENSE.txt'
  'Poppins-OFL.txt'                = 'external\HybridCore\assets\fonts\OFL.txt'
  'stb-LICENSE.txt'                = 'external\HybridCore\external\stb\LICENSE'
  'dr_libs-LICENSE.txt'            = 'external\HybridCore\external\dr_libs\LICENSE'
}
foreach ($k in $lic.Keys) {
  $src = Join-Path $root $lic[$k]
  if (-not (Test-Path $src)) { throw "licence missing: $src" }
  Copy-Item $src (Join-Path $stage "licenses\$k")
}
foreach ($sub in 'plates', 'cards') {
  $n = (Get-ChildItem (Join-Path $stage "assets\$sub") -Filter 'HEM_*.jpg').Count
  Write-Host "assets\$sub : $n pictures"
  if ($n -lt 120) { throw "assets\$sub has $n pictures, expected 120" }
}
$manifest = (Get-Content (Join-Path $store 'AppxManifest.template.xml') -Raw).
  Replace('$NAME$', $id.Name).Replace('$PUBLISHER$', $id.Publisher).
  Replace('$PUBLISHERDISPLAYNAME$', $id.PublisherDisplayName).Replace('$VERSION$', $ver)
Set-Content (Join-Path $stage 'AppxManifest.xml') $manifest -Encoding UTF8

# resources.pri: lets Windows pick the scale-100 / scale-200 / target-size images
$pricfg = Join-Path $out 'priconfig.xml'
& $makepri createconfig /cf $pricfg /dq en-US /pv 10.0.0 /o | Out-Host
if ($LASTEXITCODE) { throw "makepri createconfig failed ($LASTEXITCODE)" }
& $makepri new /pr $stage /cf $pricfg /mn (Join-Path $stage 'AppxManifest.xml') /of (Join-Path $stage 'resources.pri') /o | Out-Host
if ($LASTEXITCODE) { throw "makepri new failed ($LASTEXITCODE)" }

$msix = Join-Path $out "HEM-Elite-Rack-$ver.msix"
& $makeappx pack /d $stage /p $msix /o | Out-Host
if ($LASTEXITCODE) { throw "makeappx pack failed ($LASTEXITCODE)" }
$mb = [Math]::Round((Get-Item $msix).Length / 1MB, 1)
Write-Host "::notice title=store package::HEM-Elite-Rack-$ver.msix, $mb MB (unsigned, for Partner Center)"

# ---- test copy: + software OpenGL, signed with a throw-away certificate, installed, started, checked ----
$tstage = Join-Path $out 'stage-test'
Copy-Item $stage $tstage -Recurse
& (Join-Path $PSScriptRoot 'fetch-mesa.ps1') -TargetDirs $tstage
$tmsix = Join-Path $out 'test.msix'
& $makeappx pack /d $tstage /p $tmsix /o | Out-Null
if ($LASTEXITCODE) { throw "makeappx pack (test) failed ($LASTEXITCODE)" }
$cert = New-SelfSignedCertificate -Type Custom -Subject $id.Publisher -KeyUsage DigitalSignature -FriendlyName 'HEM Store CI test' `
          -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$pw = ConvertTo-SecureString -String ([guid]::NewGuid().ToString()) -Force -AsPlainText
$pfx = Join-Path $out 'test.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pw | Out-Null
$cer = Join-Path $out 'test.cer'
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
& $signtool sign /fd SHA256 /f $pfx /p ([Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($pw))) $tmsix | Out-Host
if ($LASTEXITCODE) { throw "signtool (test copy) failed ($LASTEXITCODE)" }

Add-AppxPackage -Path $tmsix
$pkg = Get-AppxPackage -Name $id.Name
if (-not $pkg) { throw 'the package did not install' }
Write-Host "installed: $($pkg.PackageFullName) at $($pkg.InstallLocation)"
$failures = 0
try {
  foreach ($sub in 'plates', 'cards') {
    $n = (Get-ChildItem (Join-Path $pkg.InstallLocation "assets\$sub") -Filter 'HEM_*.jpg').Count
    Write-Host "installed assets\$sub : $n"
    if ($n -lt 120) { $failures++ }
  }
  $shot = Join-Path $out 'store_app_picker.png'
  $exe = Join-Path $pkg.InstallLocation 'HEMEliteRack.exe'
  $env:GALLIUM_DRIVER = 'llvmpipe'
  Invoke-CommandInDesktopPackage -PackageFamilyName $pkg.PackageFamilyName -AppId 'HEMEliteRack' -Command $exe `
    -Args "--demo --device offline --picker 2 --exit-after 8 --screenshot `"$shot`""
  $t0 = Get-Date
  while (-not (Test-Path $shot) -and ((Get-Date) - $t0).TotalSeconds -lt 120) { Start-Sleep -Seconds 2 }
  Start-Sleep -Seconds 3
  if (Test-Path $shot) {
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($shot)
    $n = 0; $sum = 0.0; $sq = 0.0
    for ($y = 0; $y -lt $bmp.Height; $y += 7) { for ($x = 0; $x -lt $bmp.Width; $x += 7) {
      $c = $bmp.GetPixel($x, $y); $l = 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B; $n++; $sum += $l; $sq += $l * $l } }
    $w = $bmp.Width; $h = $bmp.Height; $bmp.Dispose()
    $sd = [Math]::Sqrt([Math]::Max(0, $sq / $n - ($sum / $n) * ($sum / $n)))
    Write-Host ("started inside its package: screenshot {0}x{1}, luma spread {2:N1}" -f $w, $h, $sd)
    if ($sd -lt 8) { Write-Host 'screenshot looks blank'; $failures++ }
  } else {
    Write-Host 'the packaged app wrote no screenshot within 120 s'; $failures++
  }
} finally {
  Get-Process HEMEliteRack -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Remove-AppxPackage -Package $pkg.PackageFullName
  Write-Host 'test package removed'
}

# ---- the Store's own checks (Windows App Certification Kit), when the runner has it ----
$appcert = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\App Certification Kit\appcert.exe'
if (Test-Path $appcert) {
  $report = Join-Path $out 'wack-report.xml'
  & $appcert reset | Out-Null
  & $appcert test -appxpackagepath $tmsix -reportoutputpath $report | Out-Host
  if (Test-Path $report) {
    [xml]$r = Get-Content $report
    $overall = $r.REPORT.OVERALL_RESULT
    Write-Host "::notice title=certification kit::OVERALL $overall"
    foreach ($t in $r.SelectNodes('//TEST')) { if ($t.RESULT.'#cdata-section' -and $t.RESULT.'#cdata-section' -ne 'PASS') { Write-Host "  $($t.NAME): $($t.RESULT.'#cdata-section')" } }
  } else { Write-Host '::notice title=certification kit::no report written' }
} else {
  Write-Host '::notice title=certification kit::not on this runner (skipped)'
}

Remove-Item $pfx, $cer -ErrorAction SilentlyContinue
if ($failures) { throw "$failures store package check(s) failed" }
Write-Host "::notice title=store package::installed from the package, started inside it with the picker open, removed again: OK"
