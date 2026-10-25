[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Node,[Parameter(Mandatory=$true)][string]$Worker,[Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
& $Node $Worker $Root
exit $LASTEXITCODE