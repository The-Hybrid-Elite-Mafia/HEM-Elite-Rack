<#
  CI: proves the plug-ins-only installer on a clean Windows machine.
  1. Silent install (the Store requires it) -> the VST3 bundle and its resources are in Common Files\VST3,
     the licences are installed, it is listed for uninstall, no rack app and no desktop shortcut.
  2. Reinstall over itself (upgrade) works.
  3. Side by side with the full HEM-Elite-Rack installer: uninstalling one leaves the plug-ins for the other;
     uninstalling both removes them.
#>
param([Parameter(Mandatory = $true)][string]$Installer, [Parameter(Mandatory = $true)][string]$FullInstaller)
$ErrorActionPreference = 'Stop'

$dir     = Join-Path $env:ProgramFiles 'Hybrid Elite Mafia\HEM-Elite-Rack VST3'
$fullDir = Join-Path $env:ProgramFiles 'Hybrid Elite Mafia\HEM-Elite-Rack'
$vst3    = Join-Path $env:CommonProgramFiles 'VST3\HEM-Elite-Rack.vst3'
$desktop = [Environment]::GetFolderPath('CommonDesktopDirectory')
$fails   = 0
function Check([bool]$ok, [string]$what) { if ($ok) { Write-Host "PASS  $what" } else { Write-Host "FAIL  $what"; $script:fails++ } }
function Run([string]$exe, [string[]]$extra = @()) { (Start-Process -FilePath $exe -ArgumentList (@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-') + $extra) -Wait -PassThru).ExitCode }
function Uninst([string]$folder) { $u = Join-Path $folder 'unins000.exe'; $c = (Start-Process -FilePath $u -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait -PassThru).ExitCode; Start-Sleep -Seconds 3; $c }
function HasPlugin() { $null -ne (Get-ChildItem $vst3 -Recurse -Filter '*.vst3' -File -ErrorAction SilentlyContinue | Select-Object -First 1) }
function Listed([string]$name) {
    $keys = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    $null -ne (Get-ItemProperty $keys -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -eq $name } | Select-Object -First 1)
}

# 1. plug-ins only
Check ((Run $Installer) -eq 0) 'silent install exit code 0'
Check (HasPlugin) "VST3 plug-in binary in $vst3"
foreach ($r in 'fonts\Poppins-Bold.ttf','faceplates\Main-Faceplate.png','brand\crown_badge.png','brand\ghost.png') {
    Check (Test-Path (Join-Path $vst3 "Contents\Resources\$r")) "bundle has Resources\$r"
}
foreach ($f in 'LICENSE.txt','THIRD-PARTY-NOTICES.txt','licenses\Steinberg-VST3-SDK-LICENSE.txt','licenses\Poppins-OFL.txt','licenses\stb-LICENSE.txt','licenses\dr_libs-LICENSE.txt','unins000.exe') {
    Check (Test-Path (Join-Path $dir $f)) "installed $f"
}
Check (Listed 'HEM-Elite-Rack VST3') 'listed in Apps (uninstall entry "HEM-Elite-Rack VST3")'
Check (-not (Test-Path (Join-Path $fullDir 'HEMEliteRack.exe'))) 'no rack app installed (plug-ins only)'
Check (-not (Test-Path (Join-Path $desktop 'HEM-Elite-Rack.lnk'))) 'no desktop shortcut'

# 2. upgrade
Check ((Run $Installer) -eq 0) 'reinstall over itself (upgrade) exit code 0'
Check (HasPlugin) 'plug-ins still there after the reinstall'

# 3. side by side with the full installer
Check ((Run $FullInstaller @('/TASKS=vst3')) -eq 0) 'full installer (with VST3) installs next to it'
Check ((Uninst $dir) -eq 0) 'uninstall the plug-ins-only one: exit code 0'
Check (HasPlugin) 'plug-ins kept for the full install'
Check (-not (Listed 'HEM-Elite-Rack VST3')) 'plug-ins-only entry gone from Apps'
Check ((Uninst $fullDir) -eq 0) 'uninstall the full one: exit code 0'
Check (-not (Test-Path $vst3)) 'plug-ins removed once neither is installed'

# and the other order: plug-ins-only alone, uninstall removes them
Check ((Run $Installer) -eq 0) 'install plug-ins-only again'
Check ((Uninst $dir) -eq 0) 'uninstall it: exit code 0'
Check (-not (Test-Path $vst3)) 'plug-ins removed on uninstall when nothing else uses them'

Write-Host ""
Write-Host "VST3 installer verification: $fails failure(s)"
exit $fails
