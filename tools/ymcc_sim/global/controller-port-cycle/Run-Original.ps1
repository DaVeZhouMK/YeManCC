[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
# Original build/script and original top-level SelfTest-only entry; TEMP/TMP were
# fixed to this case's directory before this process was created.
& (Join-Path $taskConfig.sourceRoot 'tools/external_controller_port_cycle_selftest.ps1')
if(-not $?){exit 2}
exit 0
