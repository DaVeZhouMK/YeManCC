[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$env:YMCC_CONTROLLER_SOURCE_ROOT=$taskConfig.sourceRoot
$env:YMCC_CONTROLLER_TRACE_ROOT=$taskConfig.trace
& $taskConfig.node --require $taskConfig.observer (Join-Path $taskConfig.sourceRoot $taskConfig.entry)
exit $LASTEXITCODE
