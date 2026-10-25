[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 required'}
& node (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir
exit $LASTEXITCODE
