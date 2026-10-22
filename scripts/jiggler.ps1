[CmdletBinding(DefaultParameterSetName = 'Status')]
param(
  [Parameter(Mandatory, ParameterSetName = 'On')][switch]$On,
  [Parameter(Mandatory, ParameterSetName = 'Off')][switch]$Off,
  [Parameter(Mandatory, ParameterSetName = 'Slow')][switch]$Slow,
  [Parameter(Mandatory, ParameterSetName = 'Medium')][switch]$Medium,
  [Parameter(Mandatory, ParameterSetName = 'Fast')][switch]$Fast,
  [Parameter(Mandatory, ParameterSetName = 'Status')][switch]$Status,
  [Parameter(Mandatory, ParameterSetName = 'Screen')][ValidateRange(320,32767)][int]$ScreenWidth,
  [Parameter(Mandatory, ParameterSetName = 'Screen')][ValidateRange(240,32767)][int]$ScreenHeight,
  [string]$Port
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = '1'
$python = Join-Path $root '.venv\Scripts\python.exe'
$command = switch ($PSCmdlet.ParameterSetName) {
  'On' { 'JIGGLE ON' }
  'Off' { 'JIGGLE OFF' }
  'Slow' { 'JIGGLE SLOW' }
  'Medium' { 'JIGGLE MEDIUM' }
  'Fast' { 'JIGGLE FAST' }
  'Screen' { "JIGGLE SCREEN $ScreenWidth $ScreenHeight" }
  default { 'JIGGLE STATUS' }
}
$cliArgs = @((Join-Path $PSScriptRoot 'jiggler_cli.py'), '--command', $command)
if ($Port) { $cliArgs += @('--port', $Port) }
& $python @cliArgs
exit $LASTEXITCODE
