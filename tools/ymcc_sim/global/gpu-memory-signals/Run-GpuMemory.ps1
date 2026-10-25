[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$nodePath=(Get-Command node.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
& $nodePath (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir
exit $LASTEXITCODE
