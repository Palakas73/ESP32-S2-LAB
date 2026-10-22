$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
Push-Location $root
try {
  if (!(Test-Path '.venv\Scripts\python.exe')) { py -3.14 -m venv .venv }
  & '.venv\Scripts\python.exe' -m pip install -r requirements-lock.txt
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  & '.venv\Scripts\platformio.exe' run -e lolin_s2_mini
  exit $LASTEXITCODE
} finally { Pop-Location }
