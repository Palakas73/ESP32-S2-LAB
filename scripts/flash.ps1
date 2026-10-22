param([string]$Port)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
$python = Join-Path $root '.venv\Scripts\python.exe'
$pio = Join-Path $root '.venv\Scripts\platformio.exe'
if (!$Port) { $Port = & $python (Join-Path $PSScriptRoot 'device.py') detect; if ($LASTEXITCODE) { throw 'No se detectó un puerto serie compatible.' } }
Push-Location $root
try { & $pio run -e lolin_s2_mini -t upload --upload-port $Port; exit $LASTEXITCODE } finally { Pop-Location }
