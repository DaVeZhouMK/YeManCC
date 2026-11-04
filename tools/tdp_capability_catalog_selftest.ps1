[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$taskRepo = Split-Path -Parent $PSScriptRoot
$taskTools = Get-Content -LiteralPath (Join-Path $taskRepo 'tools/ymcc_sim/global/tdp-protocol-consumer/toolchain-inputs.json') -Raw | ConvertFrom-Json
& $taskTools.pythonExecutable -B (Join-Path $PSScriptRoot 'tdp_capability_catalog_selftest.py') --output-dir $EvidenceDir
exit $LASTEXITCODE
