$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
$pio = Join-Path $root '.venv\Scripts\platformio.exe'
Push-Location $root
try { & $pio run -e lolin_s2_mini; exit $LASTEXITCODE } finally { Pop-Location }
