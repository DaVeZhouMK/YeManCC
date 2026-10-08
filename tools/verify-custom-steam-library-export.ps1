[CmdletBinding()]
param([string]$PackageRoot='', [string]$ZipPath='', [string]$OutputPath='')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\Build'))
$canonical=Join-Path $projectRoot 'CustomSteamLibrary'
if(-not $PackageRoot){$PackageRoot=$canonical}
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
if(-not $OutputPath){$OutputPath=Join-Path $buildRoot 'CustomSteamLibrary\mainline-export-audit.json'}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if(-not $OutputPath.StartsWith($buildRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Audit report must stay in Mainline Build'}
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $projectRoot
$manifest=Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $projectRoot -PackageRoot $canonical
function Safe-Relative([string]$Path){
  $key=$Path.Replace('\','/')
  if(-not $key -or [IO.Path]::IsPathRooted($key) -or $key.Contains(':') -or @($key.Split('/')|Where-Object{$_ -eq '..' -or $_ -eq '.' -or -not $_}).Count){throw "Unsafe export path: $Path"}
  return $key
}
$expected=@{};$files=@()
foreach($entry in @($manifest.fileIndex)){
  $key=Safe-Relative ([string]$entry.path)
  if($expected.ContainsKey($key)){throw "Duplicate fileIndex path: $key"}
  $source=Join-Path $canonical $key
  $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
  if($hash -ine [string]$entry.sha256 -or (Get-Item -LiteralPath $source).Length -ne [int64]$entry.bytes){throw "Current source package has drifted: $key"}
  $expected[$key]=$hash
  $files+=[pscustomobject]@{path=$key;sha256=$hash;bytes=[int64]$entry.bytes}
}
foreach($field in @('files','managedPaths')){
  $paths=@($manifest.$field|ForEach-Object{Safe-Relative ([string]$_)})
  if($paths.Count -ne $expected.Count -or @($paths|Sort-Object -Unique).Count -ne $paths.Count -or (Compare-Object @($expected.Keys|Sort-Object) @($paths|Sort-Object))){throw "Manifest $field coverage differs from fileIndex"}
}
$expected['package-manifest.json']=(Get-FileHash -LiteralPath (Join-Path $canonical 'package-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
foreach($key in $expected.Keys){
  $path=Join-Path $PackageRoot $key
  if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected[$key]){throw "Export folder is not current mainline: $key ($PackageRoot)"}
}
$zipHash=$null
if($ZipPath){
  $ZipPath=[IO.Path]::GetFullPath($ZipPath)
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive=[IO.Compression.ZipFile]::OpenRead($ZipPath)
  try{
    $entries=@($archive.Entries|Where-Object{-not $_.FullName.EndsWith('/')})
    $manifests=@($entries|Where-Object{$_.FullName.Replace('\','/').EndsWith('CustomSteamLibrary/package-manifest.json')})
    if($manifests.Count -ne 1){throw 'ZIP must contain one unambiguous CustomSteamLibrary manifest'}
    $manifestName=$manifests[0].FullName.Replace('\','/')
    $prefix=$manifestName.Substring(0,$manifestName.Length-'package-manifest.json'.Length)
    $seen=@{}
    foreach($entry in $entries){
      $name=$entry.FullName.Replace('\','/')
      if(-not $name.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){continue}
      $key=Safe-Relative ($name.Substring($prefix.Length))
      if($seen.ContainsKey($key) -or -not $expected.ContainsKey($key)){throw "Unexpected/duplicate ZIP child file: $name"}
      $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
      try{$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$sha.Dispose()}
      if($hash -ne $expected[$key]){throw "ZIP child payload is stale or modified: $name"}
      $seen[$key]=$true
    }
    if($seen.Count -ne $expected.Count){throw 'ZIP child payload is incomplete'}
  }finally{$archive.Dispose()}
  $zipHash=(Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
# Recheck inputs after reading the ZIP so source edits during the audit cannot
# produce a green result for a now-superseded binary.
Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $projectRoot -PackageRoot $canonical | Out-Null
$gitHead=[string](& git -C $projectRoot rev-parse HEAD)
$gitDirty=@(& git -C $projectRoot status --porcelain -- CustomSteamLibrary native/custom-steam-library tools/build-custom-steam-library-host.ps1 tools/build-custom-steam-library-worker.ps1 tools/custom_steam_library_build_guard.ps1)
$report=[ordered]@{allPassed=$true;checkedAt=[DateTimeOffset]::Now.ToString('o');sourceRoot=$projectRoot;branch=[string](& git -C $projectRoot branch --show-current);gitHead=$gitHead;workingTreeHasUncommittedChanges=$gitDirty.Count -gt 0;packageVersion=$manifest.packageVersion;sourceManifestGeneratedAt=$manifest.generatedAt;packageRoot=$PackageRoot;zipPath=$ZipPath;zipSha256=$zipHash;hostSourceInputCount=@($manifest.sourceBuild.hostSources).Count;workerSourceInputCount=@($manifest.sourceBuild.workerSources).Count;files=$files;realSteamFilesModified=$false;installedPackageModified=$false}
New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath)|Out-Null
$report|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output 'CUSTOM_STEAM_LIBRARY_CURRENT_MAINLINE_EXPORT_OK'
Write-Output "Report: $OutputPath"
