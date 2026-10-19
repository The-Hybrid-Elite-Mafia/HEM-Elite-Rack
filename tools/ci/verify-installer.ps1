<#
  CI: proves the installer works on a clean Windows machine: silent install -> files, shortcut,
  VST3 plug-ins present -> the installed self-test passes -> settings survive a reinstall
  (upgrade) and an uninstall -> uninstall removes the program and the plug-ins.
#>
param([Parameter(Mandatory = $true)][string]$Installer)
$ErrorActionPreference = 'Stop'

$app      = Join-Path $env:ProgramFiles 'Hybrid Elite Mafia\HEM-Elite-Rack'
$vst3     = Join-Path $env:CommonProgramFiles 'VST3\HEM-Elite-Rack.vst3'
$settings = Join-Path $env:APPDATA 'HEM-Elite-Rack'
$desktop  = [Environment]::GetFolderPath('CommonDesktopDirectory')
$fails    = 0
function Check([bool]$ok, [string]$what) { if ($ok) { Write-Host "PASS  $what" } else { Write-Host "FAIL  $what"; $script:fails++ } }
function Install() { (Start-Process -FilePath $Installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-' -Wait -PassThru).ExitCode }

Check ((Install) -eq 0) 'silent install exit code 0'
foreach ($f in 'HEMEliteRack.exe','bin\hem_rack_tests.exe','bin\hem_rack_snapshot.exe','assets\brand\crown_badge.png','assets\brand\ghost.png',
              'assets\faceplates\Main-Faceplate.png','assets\fonts\Poppins-Medium.ttf','assets\fonts\Poppins-SemiBold.ttf','assets\fonts\Poppins-Bold.ttf',
              'HOW-TO-USE.txt','THIRD-PARTY-NOTICES.txt','licenses\Poppins-OFL.txt','licenses\stb-LICENSE.txt','licenses\dr_libs-LICENSE.txt',
              'licenses\Steinberg-VST3-SDK-LICENSE.txt','unins000.exe') {
    Check (Test-Path (Join-Path $app $f)) "installed $f"
}
Check (Test-Path (Join-Path $desktop 'HEM-Elite-Rack.lnk')) 'desktop shortcut'
$dll = Get-ChildItem $vst3 -Recurse -Filter '*.vst3' -File -ErrorAction SilentlyContinue | Select-Object -First 1
Check ($null -ne $dll) "VST3 plug-ins installed in $vst3"
foreach ($r in 'fonts\Poppins-Bold.ttf','faceplates\Main-Faceplate.png','brand\crown_badge.png','brand\ghost.png') {
    Check (Test-Path (Join-Path $vst3 "Contents\Resources\$r")) "VST3 bundle has Resources\$r"
}

$out = & (Join-Path $app 'bin\hem_rack_tests.exe') 2>&1 | Out-String
Check ($LASTEXITCODE -eq 0 -and $out -match ', 0 failed') 'installed hem_rack_tests.exe passes'
($out -split "`n" | Select-Object -Last 2) -join ' ' | Write-Host

New-Item -ItemType Directory -Force -Path $settings | Out-Null
$marker = Join-Path $settings 'ci-marker.txt'
'keep me' | Out-File $marker -Encoding ascii
Check ((Install) -eq 0) 'reinstall over the existing one (upgrade) exit code 0'
Check (Test-Path $marker) 'settings kept across the reinstall'

$p = Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait -PassThru
Start-Sleep -Seconds 3
Check ($p.ExitCode -eq 0) 'silent uninstall exit code 0'
Check (-not (Test-Path (Join-Path $app 'HEMEliteRack.exe'))) 'program removed on uninstall'
Check (-not (Test-Path $vst3)) 'VST3 plug-ins removed on uninstall'
Check (Test-Path $marker) 'settings kept after uninstall'

Write-Host ""
Write-Host "installer verification: $fails failure(s)"
exit $fails
