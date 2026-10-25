[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$taskNode=(Get-Command node.exe -CommandType Application | Select-Object -First 1).Source
& $taskNode (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir
exit $LASTEXITCODE
