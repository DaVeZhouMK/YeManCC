[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$NodePath,[Parameter(Mandatory=$true)][string]$RepoRoot,[Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
& $NodePath (Join-Path $PSScriptRoot 'worker.mjs') $RepoRoot $EvidenceDir
exit $LASTEXITCODE
