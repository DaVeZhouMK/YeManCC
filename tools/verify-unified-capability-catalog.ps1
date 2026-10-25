[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDir)
if($evidence -eq $repo -or $evidence.StartsWith($repo.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Capability check evidence must be outside product sources'}
Push-Location -LiteralPath $repo
try {
 & node (Join-Path $repo 'tools/hc_capability_catalog_generate.mjs') --check --out $evidence
 if($LASTEXITCODE -ne 0){throw 'Unified HC capability catalog differs from its frozen source or generated consumers'}
 & node (Join-Path $repo 'tools/hc_capability_maintenance.mjs') --out (Join-Path $evidence 'maintenance')
 if($LASTEXITCODE -ne 0){throw 'Unified capability consumer/route issue records differ from source'}
} finally {Pop-Location}
