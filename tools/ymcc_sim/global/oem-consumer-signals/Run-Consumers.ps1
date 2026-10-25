[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$NodePath)
$ErrorActionPreference='Stop'
# The global powershell adapter runs this entry inside the existing host-process
# Windows Job before this process starts Node or its compiler/test descendants.
$taskRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if(-not (Test-Path -LiteralPath $NodePath -PathType Leaf)){throw 'PINNED_NODE_MISSING'}
& $NodePath (Join-Path $taskRepo 'tools/hc_oem_input_consumer_selftest.mjs') --out $EvidenceDir
exit $LASTEXITCODE
