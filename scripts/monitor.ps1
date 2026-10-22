param([string]$Port)
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
$pio = Join-Path $root '.venv\Scripts\platformio.exe'
$monitorArgs = @('device','monitor','-b','115200')
if ($Port) { $monitorArgs += @('-p',$Port) }
Push-Location $root
try { & $pio @monitorArgs; exit $LASTEXITCODE } finally { Pop-Location }
