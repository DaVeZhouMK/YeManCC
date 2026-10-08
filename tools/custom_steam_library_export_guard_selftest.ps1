[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build')).TrimEnd('\')
$root=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $root.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $root)){throw 'Use a fresh isolated Mainline Build fixture directory'}
New-Item -ItemType Directory -Path $root|Out-Null
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
# Extract only the production validation functions, never execute the packager.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'package-release.ps1'),[ref]$tokens,[ref]$errors)
if($errors){throw 'Packager parse errors'}
foreach($name in @('Get-FullPath','Get-Sha256','Get-RelativePath','Normalize-PackageRelativePath','Assert-CustomSteamLibraryPackage','Sync-CustomSteamLibraryMainline')){
  $fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
  if(-not $fn){throw "Missing production function: $name"}
  Invoke-Expression $fn.Extent.Text
}
function Write-Text($Path,$Text){New-Item -ItemType Directory -Force -Path (Split-Path $Path)|Out-Null;[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Assert($Value,$Text){if(-not $Value){throw $Text}}
function Must-Reject([scriptblock]$Action){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Assert $failed 'Expected fail-closed rejection'}
$reports=[Collections.Generic.List[object]]::new()
function Run($Name,[scriptblock]$Action){
  $case=Join-Path $root ('c'+$reports.Count);New-Item -ItemType Directory -Path $case|Out-Null
  try{& $Action $case;$reports.Add([ordered]@{name=$Name;passed=$true})}catch{$reports.Add([ordered]@{name=$Name;passed=$false;error=$_.Exception.Message})}
}
function Fixture($Case){
  $src=Join-Path $Case 'source';$pkg=Join-Path $src 'CustomSteamLibrary'
  foreach($kind in @('host','worker')){
    foreach($input in @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $project -Kind $kind)){Write-Text (Join-Path $src $input.path) ('fixture '+$input.path)}
  }
  Write-Text (Join-Path $src 'version.json') '{"version":"0.0.33"}'
  $index=@();$sourceBuild=@{}
  foreach($kind in @('host','worker')){
    $name=if($kind -eq 'host'){'CustomSteamLibrary.exe'}else{'SteamArtworkLab.exe'}
    $path=Join-Path $pkg $name;Write-Text $path ('fixture '+$kind)
    $hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
    $index+=@{path=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=$hash}
    $sourceBuild[$kind]=$hash;$sourceBuild[$kind+'Sources']=@(Get-CustomSteamLibrarySourceIndex -ProjectRoot $src -Kind $kind)
  }
  $ico=Join-Path $pkg 'assets/custom-steam-library.ico'
  $index+=@{path='assets/custom-steam-library.ico';bytes=(Get-Item -LiteralPath $ico).Length;sha256=(Get-FileHash -LiteralPath $ico).Hash.ToLowerInvariant()}
  $m=@{schemaVersion=2;packageId='custom-steam-library';packageType='green-child';packageVersion='0.0.33';entryPoint='CustomSteamLibrary.exe';worker='SteamArtworkLab.exe';updater=@{unknownPaths='preserve';healthHandshake=@{protocol=1;requiredBeforeCommit=$true}};files=@('CustomSteamLibrary.exe','SteamArtworkLab.exe','assets/custom-steam-library.ico');managedPaths=@('CustomSteamLibrary.exe','SteamArtworkLab.exe','assets/custom-steam-library.ico');fileIndex=$index;sourceBuild=$sourceBuild}
  Write-Text (Join-Path $pkg 'package-manifest.json') ($m|ConvertTo-Json -Depth 20)
  return @{source=$src;package=$pkg;manifest=$m}
}
Run 'valid-package-validation-is-read-only' {param($p)$f=Fixture $p;$path=Join-Path $f.package 'package-manifest.json';$before=(Get-FileHash $path).Hash;Assert-CustomSteamLibraryPackage $f.package|Out-Null;Assert ((Get-FileHash $path).Hash -eq $before) 'Validator rewrote manifest'}
Run 'tampered-binary-is-rejected-not-rehashed' {param($p)$f=Fixture $p;$mp=Join-Path $f.package 'package-manifest.json';$before=(Get-FileHash $mp).Hash;Write-Text (Join-Path $f.package 'SteamArtworkLab.exe') 'stale binary';Must-Reject {Assert-CustomSteamLibraryPackage $f.package};Assert ((Get-FileHash $mp).Hash -eq $before) 'Validator blessed tampered file'}
Run 'same-size-binary-change-is-rejected' {param($p)$f=Fixture $p;Write-Text (Join-Path $f.package 'SteamArtworkLab.exe') 'fixture wrong!';Must-Reject {Assert-CustomSteamLibraryPackage $f.package}}
Run 'valid-current-source-fingerprints-pass' {param($p)$f=Fixture $p;Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package|Out-Null}
foreach($path in @('native/custom-steam-library/steam_artwork_lab.cpp','native/custom-steam-library/custom_steam_library_exclusions.h','deps/json/json.hpp','CustomSteamLibrary/assets/custom-steam-library.ico','tools/build-custom-steam-library-worker.ps1','native/custom-steam-library/build_worker.bat')){
  $changedPath=$path
  Run ('source-drift-'+$path.Replace('/','-')) {param($p)$f=Fixture $p;Write-Text (Join-Path $f.source $changedPath) 'changed during compilation';Must-Reject {Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package}}
}
Run 'webview-library-drift-is-rejected' {param($p)$f=Fixture $p;Write-Text (Join-Path $f.source 'deps/webview2/build/native/x64/WebView2LoaderStatic.lib') 'old lib';Must-Reject {Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package}}
Run 'new-header-not-in-build-snapshot-is-rejected' {param($p)$f=Fixture $p;Write-Text (Join-Path $f.source 'native/custom-steam-library/new_dependency.h') 'new header';Must-Reject {Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package}}
Run 'incomplete-fingerprint-coverage-is-rejected' {param($p)$f=Fixture $p;$idx=@($f.manifest.sourceBuild.workerSources|Select-Object -Skip 1);Must-Reject {Assert-CustomSteamLibrarySourceIndex -ProjectRoot $f.source -Kind worker -SourceIndex $idx}}
Run 'duplicated-fingerprint-is-rejected' {param($p)$f=Fixture $p;$idx=@($f.manifest.sourceBuild.workerSources)+@($f.manifest.sourceBuild.workerSources[0]);Must-Reject {Assert-CustomSteamLibrarySourceIndex -ProjectRoot $f.source -Kind worker -SourceIndex $idx}}
Run 'source-snapshot-catches-mid-build-edit' {param($p)$f=Fixture $p;$before=@(Get-CustomSteamLibrarySourceIndex -ProjectRoot $f.source -Kind worker);Write-Text (Join-Path $f.source 'native/custom-steam-library/steam_artwork_lab.cpp') 'edit after compiler started';Must-Reject {Assert-CustomSteamLibrarySourceIndex -ProjectRoot $f.source -Kind worker -SourceIndex $before}}
Run 'old-release-source-is-not-a-mainline-sync-source' {param($p)$f=Fixture $p;$ProjectRoot=$f.source;Must-Reject {Sync-CustomSteamLibraryMainline (Join-Path $p 'legacy-lab') '0.0.33'}}
Run 'missing-git-authority-is-rejected' {param($p)Must-Reject {Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $p}}
Run 'source-build-executable-hash-mismatch-is-rejected' {param($p)$f=Fixture $p;Write-Text (Join-Path $f.package 'CustomSteamLibrary.exe') 'different build';Must-Reject {Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package}}
Run 'version-only-is-not-allowed-to-bless-an-old-child' {param($p)$f=Fixture $p;Write-Text (Join-Path $f.source 'version.json') '{"version":"0.0.34"}';Must-Reject {Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $f.source -PackageRoot $f.package}}
$failed=@($reports|Where-Object{-not $_.passed});$report=[ordered]@{allPassed=$failed.Count -eq 0;caseCount=$reports.Count;failedCount=$failed.Count;cases=@($reports.ToArray());realSteamFilesModified=$false;formalReleaseModified=$false}
Write-Text (Join-Path $root 'summary.json') ($report|ConvertTo-Json -Depth 12)
$report|ConvertTo-Json -Depth 12
if($failed.Count){throw 'Mainline export guard regression failed'}
