$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
$python = Join-Path $root '.venv\Scripts\python.exe'
& $python (Join-Path $PSScriptRoot 'hardware_test.py')
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $python (Join-Path $PSScriptRoot 'hid_motion_capture.py')
exit $LASTEXITCODE
