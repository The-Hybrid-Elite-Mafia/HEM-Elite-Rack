<#
  CI: runs Steinberg's VST3 validator (built from the SDK) on HEM-Elite-Rack.vst3.
#>
param([Parameter(Mandatory = $true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$validator = Get-ChildItem $BuildDir -Recurse -Filter 'validator.exe' -File | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
if (-not $validator) { throw 'validator.exe not found' }
$bundle = Get-ChildItem $BuildDir -Recurse -Directory -Filter 'HEM-Elite-Rack.vst3' | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
if (-not $bundle) { throw 'HEM-Elite-Rack.vst3 not found' }
Write-Host "validator: $($validator.FullName)"
Write-Host "plug-in  : $($bundle.FullName)"
$out  = & $validator.FullName $bundle.FullName 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$out | ForEach-Object { Write-Host $_ }
$ok  = ($out | Select-String -SimpleMatch '[Succeeded]').Count
$bad = ($out | Select-String -SimpleMatch '[Failed]').Count
Write-Host ""
Write-Host "VALIDATOR SUMMARY: $ok test(s) succeeded, $bad failed, exit code $code"
exit $code
