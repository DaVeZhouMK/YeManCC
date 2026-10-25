[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$out=[IO.Path]::GetFullPath($EvidenceDir)
if($out -eq $repo -or $out.StartsWith($repo.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Use evidence outside product sources'}
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location -LiteralPath $repo
try {
 & node tools/hc_model_motion_consumer_selftest.mjs --generate-only --out (Join-Path $out 'generated') 2>&1 | Tee-Object -FilePath (Join-Path $out 'generation.log')
 if($LASTEXITCODE -ne 0){throw 'Actual motion consumer generation failed'}
 & node tools/hc_generic_motion_hotpath_selftest.mjs --out (Join-Path $out 'micro') --generated (Join-Path $out 'generated') --before (Join-Path $repo 'tools/fixtures/hc_capability_hotpaths/generic-recipe-before.inc') 2>&1 | Tee-Object -FilePath (Join-Path $out 'micro.log')
 if($LASTEXITCODE -ne 0){throw 'Actual motion recipe cache failed'}
 & node tools/hc_generic_motion_cache_negative_selftest.mjs --cpp (Join-Path $out 'micro/motion-hotpath.cpp') --out (Join-Path $out 'negative') 2>&1 | Tee-Object -FilePath (Join-Path $out 'negative.log')
 if($LASTEXITCODE -ne 0){throw 'Compiled cache-key negative cases failed'}
} finally {Pop-Location}
