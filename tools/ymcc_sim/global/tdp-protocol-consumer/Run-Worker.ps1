[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Python,
 [Parameter(Mandatory=$true)][string]$Worker,
 [Parameter(Mandatory=$true)][string]$TestScript,
 [Parameter(Mandatory=$true)][string]$EvidenceRoot
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'TDP_PROTOCOL_PWSH7_REQUIRED'}
Set-Location -LiteralPath $EvidenceRoot
& $Python -I -S -B -X ('pycache_prefix='+(Join-Path $EvidenceRoot 'ignored-cache')) $Worker --test $TestScript --root $EvidenceRoot
exit $LASTEXITCODE
