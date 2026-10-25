function Get-YmccSimulationCoreRoot {
 $root=if($env:YMCC_SIM_CORE_ROOT){[IO.Path]::GetFullPath($env:YMCC_SIM_CORE_ROOT)}else{[IO.Path]::GetFullPath('G:\YeManCC-Work\Testing\YMCC')}
 $root=$root.TrimEnd('\')
 if($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\')){throw 'SIMULATION_CORE_SCOPE_INVALID'}
 if($env:YMCC_SIM_CORE_ROOT){
  $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).TrimEnd('\')
  $marker=Get-Content -LiteralPath (Join-Path $root '.ymcc-release-check-core.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if($marker.schemaVersion -ne 1 -or $marker.owner -cne 'YMCC-release-check' -or [IO.Path]::GetFullPath([string]$marker.repo).TrimEnd('\') -ine $repo){throw 'SIMULATION_CORE_OWNER_CHANGED'}
  if($root -ieq $repo -or $root.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or $repo.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'SIMULATION_CORE_SCOPE_INVALID'}
 }
 return $root
}
function Get-YmccPackageFixtureToolAuthority([string]$OriginalManifest) {
 if($env:YMCC_RELEASE_HOSTED_ENVIRONMENT -cne '1'){return (Get-Content -LiteralPath $OriginalManifest -Raw|ConvertFrom-Json)}
 if($env:GITHUB_ACTIONS -cne 'true'){throw 'HOSTED_RELEASE_REQUIRES_GITHUB_RUNNER'}
 $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).TrimEnd('\')
 $profile=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hosted-release-tools.json') -Raw|ConvertFrom-Json
 if($profile.schemaVersion -ne 1 -or $profile.kind -cne 'ymcc-hosted-release-tool-declaration-candidate' -or $profile.status -cne 'REVIEWED_HOST_DECLARATION'){throw 'HOSTED_RELEASE_TOOL_REVIEW_REQUIRED'}
 if($profile.imageOS -cne $env:ImageOS -or $profile.imageVersion -cne $env:ImageVersion){throw 'HOSTED_RELEASE_RUNNER_IMAGE_CHANGED'}
 if($profile.originalDeclarations.Count -ne 3 -or $profile.missing.Count -ne 0){throw 'HOSTED_RELEASE_TOOL_DECLARATION_INCOMPLETE'}
 foreach($row in $profile.originalDeclarations){
  $file=[IO.Path]::GetFullPath((Join-Path $repo $row.path))
  if(-not $file.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row.sha256){throw 'HOSTED_RELEASE_ORIGINAL_DECLARATION_CHANGED'}
 }
 $authority=$profile.packageFixtureManifest
 if($authority.schemaVersion -ne 1 -or $authority.files.Count -ne 42){throw 'PACKAGE_FIXTURE_MANIFEST_SCHEMA'}
 return $authority
}