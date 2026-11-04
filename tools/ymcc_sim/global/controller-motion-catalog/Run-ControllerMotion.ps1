[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$NodePath,[ValidateSet('all','activation')][string]$Case='all')
$ErrorActionPreference='Stop'
# Original global powershell adapters enter the existing owned Job before this script.
$taskRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$taskNodeArgs=@((Join-Path $taskRepo 'tools/hc_controller_motion_consumer_selftest.mjs'),'--out',$EvidenceDir)
if($Case -eq 'activation'){$taskNodeArgs+='--activation-only'}
& $NodePath @taskNodeArgs
exit $LASTEXITCODE
