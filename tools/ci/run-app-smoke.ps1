<#
  CI: starts the real HEMEliteRack.exe with the offline clock (no sound card on the runner),
  a made-up demo song and a two-plug-in chain, lets it play, saves a screenshot of its window
  and checks it exited cleanly and drew something.
#>
param([Parameter(Mandatory = $true)][string]$Exe, [Parameter(Mandatory = $true)][string]$Shot, [int]$Select = 2, [double]$Seconds = 6, [switch]$Vintage)
$ErrorActionPreference = 'Stop'
$shotPath = [IO.Path]::GetFullPath($Shot)
New-Item -ItemType Directory -Force -Path (Split-Path $shotPath) | Out-Null
Remove-Item $shotPath -ErrorAction SilentlyContinue
$demo = if ($Vintage) { '--demo2' } else { '--demo' }
$p = Start-Process -FilePath $Exe -ArgumentList $demo,'--device','offline','--select',"$Select",'--exit-after',"$Seconds",'--screenshot',"`"$shotPath`"" -PassThru
if (-not $p.WaitForExit(120000)) { $p.Kill(); throw 'HEMEliteRack.exe did not exit within 120 s' }
Write-Host "exit code: $($p.ExitCode)"
if ($p.ExitCode -ne 0) { throw "HEMEliteRack.exe exited with $($p.ExitCode)" }
if (-not (Test-Path $shotPath)) { throw 'no screenshot written' }
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($shotPath)
$n = 0; $sum = 0.0; $sq = 0.0
for ($y = 0; $y -lt $bmp.Height; $y += 7) { for ($x = 0; $x -lt $bmp.Width; $x += 7) {
    $c = $bmp.GetPixel($x, $y); $l = 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B; $n++; $sum += $l; $sq += $l * $l } }
$w = $bmp.Width; $h = $bmp.Height
$bmp.Dispose()
$sd = [Math]::Sqrt([Math]::Max(0, $sq / $n - ($sum / $n) * ($sum / $n)))
Write-Host ("screenshot {0}x{1}, luma spread {2:N1}" -f $w, $h, $sd)
if ($sd -lt 8) { throw 'screenshot looks blank' }
