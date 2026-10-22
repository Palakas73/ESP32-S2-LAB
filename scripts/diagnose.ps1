$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
& (Join-Path $root '.venv\Scripts\python.exe') --version
& (Join-Path $root '.venv\Scripts\platformio.exe') --version
& (Join-Path $root '.venv\Scripts\python.exe') -m esptool version
& (Join-Path $root '.venv\Scripts\python.exe') (Join-Path $PSScriptRoot 'device.py') list
Push-Location $root
try { & (Join-Path $root '.venv\Scripts\platformio.exe') pkg list -e lolin_s2_mini } finally { Pop-Location }
