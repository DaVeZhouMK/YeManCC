[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$NodePath)
$ErrorActionPreference='Stop'
# Original global powershell adapters enter the existing owned Job before this script.
$taskRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
& $NodePath (Join-Path $taskRepo 'tools/hc_controller_motion_consumer_selftest.mjs') --out $EvidenceDir
exit $LASTEXITCODE
