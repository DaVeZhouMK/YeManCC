[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskConfig.kind -notin @('policy','runtime')) {throw 'UNREVIEWED_CONTROLLER_PRIORITY_CASE'}
$taskScript=Join-Path $taskConfig.sourceRoot ('tools/external_controller_priority_'+$taskConfig.kind+'_selftest.ps1')
$env:TEMP=$taskConfig.privateTemp
$env:TMP=$taskConfig.privateTemp
# Original scripts and assertions run unchanged. They compile their standalone
# production-header fixture; this wrapper supplies only installed SDK setup.
& $taskScript -OutputRoot $taskConfig.outputRoot -VcVarsPath $taskConfig.toolchainCmd
if(-not $?){exit 2}
exit 0
