function Assert-YmccLocalFrozenPath([string]$Path) {
 $cursor=$Path
 while($cursor){if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'LOCAL_SIMULATION_CORE_REPARSE_PATH'};$next=[IO.Path]::GetDirectoryName($cursor);if($next -eq $cursor){break};$cursor=$next}
}
function Assert-YmccLocalFrozenCore([string]$Core,[string]$Repo,$Marker) {
 $parent=[IO.Path]::GetFullPath('G:\YeManCC-Work\Testing\YMCC').TrimEnd('\')
 $workspace=Join-Path $parent 'Workspace'
 $fields=@($Marker.PSObject.Properties.Name|Sort-Object)
 if(($fields -join '|') -cne 'core|kind|owner|parentCore|repo|schemaVersion' -or $Marker.schemaVersion -isnot [ValueType] -or $Marker.schemaVersion -is [bool] -or $Marker.schemaVersion -ne 1 -or $Marker.owner -isnot [string] -or $Marker.kind -isnot [string] -or $Marker.owner -cne 'YMCC-local-check' -or $Marker.kind -cne 'ymcc-local-frozen-core-v1' -or $Marker.core -isnot [string] -or [IO.Path]::GetFullPath($Marker.core).TrimEnd('\') -ine $Core -or $Marker.parentCore -isnot [string] -or $Marker.repo -isnot [string] -or [IO.Path]::GetFullPath($Marker.parentCore).TrimEnd('\') -ine $parent -or [IO.Path]::GetFullPath($Marker.repo).TrimEnd('\') -ine $Repo){throw 'LOCAL_SIMULATION_CORE_OWNER_INVALID'}
 if(-not $Core.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $Repo.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or $Repo -notmatch '\\Mainline\\YeManCC-source\\YeManCC$' -or $Core -ieq $Repo -or $Core.StartsWith($Repo+'\',[StringComparison]::OrdinalIgnoreCase) -or $Repo.StartsWith($Core+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_SIMULATION_CORE_SCOPE_INVALID'}
 foreach($path in @($Core,$Repo)) { Assert-YmccLocalFrozenPath $path }
}
function Get-YmccSimulationCoreRoot {
 $root=if($env:YMCC_SIM_CORE_ROOT){[IO.Path]::GetFullPath($env:YMCC_SIM_CORE_ROOT)}else{[IO.Path]::GetFullPath('G:\YeManCC-Work\Testing\YMCC')}
 $root=$root.TrimEnd('\')
 if($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\')){throw 'SIMULATION_CORE_SCOPE_INVALID'}
 if($env:YMCC_SIM_CORE_ROOT){
  $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).TrimEnd('\')
  $localMarker=Join-Path $root '.ymcc-local-check-core.json'
  if(Test-Path -LiteralPath $localMarker -PathType Leaf){
   Assert-YmccLocalFrozenPath $localMarker
   $marker=Get-Content -LiteralPath $localMarker -Raw -Encoding UTF8|ConvertFrom-Json
   Assert-YmccLocalFrozenCore $root $repo $marker
  } else {
   $marker=Get-Content -LiteralPath (Join-Path $root '.ymcc-release-check-core.json') -Raw -Encoding UTF8|ConvertFrom-Json
   if($marker.schemaVersion -ne 1 -or $marker.owner -cne 'YMCC-release-check' -or [IO.Path]::GetFullPath([string]$marker.repo).TrimEnd('\') -ine $repo){throw 'SIMULATION_CORE_OWNER_CHANGED'}
  }
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