$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
& (Join-Path $root '.venv\Scripts\python.exe') (Join-Path $PSScriptRoot 'hardware_test.py')
exit $LASTEXITCODE
