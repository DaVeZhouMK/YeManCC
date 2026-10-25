# Exact approved component-source archive validation; compatible with PS 5.1.
# Original source locations and active development files never replace a missing archive.
function Assert-YmccHostSourceBundle {
 [CmdletBinding()]
 param(
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$ExpectedManifestSha256,
  [Parameter(Mandatory=$true)][string]$ExpectedHostSha256,
  [Parameter(Mandatory=$true)][string]$ExpectedBackendSha256,
  [Parameter(Mandatory=$true)][System.Collections.IDictionary]$ExpectedSourcePins,
  [string]$LiveSourceRoot=''
 )
 $ErrorActionPreference='Stop'
 foreach($pin in @($ExpectedManifestSha256,$ExpectedHostSha256,$ExpectedBackendSha256)) {
  if($pin -notmatch '^[a-fA-F0-9]{64}$'){throw 'FAN_HOST_SOURCE_BUNDLE_EXPECTED_PIN_INVALID'}
 }
 function Get-SourceBundleHash([string]$File) {
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$stream=[IO.File]::OpenRead($File);try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose()}}finally{$sha.Dispose()}
 }
 $sourceFull=[IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
 if([IO.Path]::GetFileName($sourceFull) -cne 'source'){throw 'FAN_HOST_SOURCE_BUNDLE_SOURCE_ROOT_INVALID'}
 $bundleFull=Split-Path -Parent $sourceFull
 function Get-SourceBundleMember([string]$Relative) {
  if([string]::IsNullOrWhiteSpace($Relative) -or $Relative -match '[:\\]' -or [IO.Path]::IsPathRooted($Relative)){throw 'FAN_HOST_SOURCE_BUNDLE_MEMBER_UNSAFE'}
  foreach($part in $Relative.Split('/')){if(-not $part -or $part -in @('.','..') -or $part -cne $part.TrimEnd(' ','.')){throw 'FAN_HOST_SOURCE_BUNDLE_MEMBER_UNSAFE'}}
  $path=[IO.Path]::GetFullPath((Join-Path $bundleFull $Relative))
  if(-not $path.StartsWith($bundleFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'FAN_HOST_SOURCE_BUNDLE_MEMBER_UNSAFE'}
  $cursor=$path
  while($cursor){
   if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'FAN_HOST_SOURCE_BUNDLE_REPARSE_REFUSED'}
   $cursor=Split-Path -Parent $cursor
  }
  return $path
 }
 $manifestPath=Get-SourceBundleMember 'manifest.json'
 if(-not(Test-Path -LiteralPath $manifestPath -PathType Leaf)){throw 'FAN_HOST_SOURCE_BUNDLE_UNAVAILABLE'}
 if((Get-SourceBundleHash $manifestPath) -cne $ExpectedManifestSha256.ToLowerInvariant()){throw 'FAN_HOST_SOURCE_BUNDLE_MANIFEST_CHANGED'}
 $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
 if([int]$manifest.schemaVersion -ne 1 -or [string]$manifest.kind -cne 'ymcc-host-component-source-bundle' -or $manifest.runtimeDeployment -ne $false){throw 'FAN_HOST_SOURCE_BUNDLE_SCHEMA_INVALID'}
 $archivedSourceRows=@($manifest.sourceInputs)
 $sourceRows=@($archivedSourceRows|Where-Object {$ExpectedSourcePins.Contains([string]$_.path)});$sourceNames=@{}
 if($sourceRows.Count -ne $ExpectedSourcePins.Count){throw 'FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED'}
 foreach($row in $sourceRows){
  $relative=[string]$row.path
  if($relative -notmatch '^(FanLab|native|capabilities)/[^:]+\.(cs|csproj)$' -or $sourceNames.ContainsKey($relative) -or -not $ExpectedSourcePins.Contains($relative) -or
     [string]$row.archivePath -cne ('source/'+$relative) -or [string]$row.sha256 -cne ([string]$ExpectedSourcePins[$relative]).ToLowerInvariant()){throw 'FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED'}
  $sourceNames[$relative]=$true
  $file=Get-SourceBundleMember ([string]$row.archivePath)
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw ('FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: '+$relative)}
  if((Get-SourceBundleHash $file) -cne ([string]$row.sha256)){throw ('FAN_HOST_COMPILED_SOURCE_CHANGED: '+$relative)}
 }
 $expectedModules=@{YeManFanHost=$ExpectedHostSha256.ToLowerInvariant();YeManFanBackend=$ExpectedBackendSha256.ToLowerInvariant()}
 $seenModules=@{}
 foreach($module in @($manifest.modules)){
  $name=[string]$module.name
  if(-not $expectedModules.ContainsKey($name) -or $seenModules.ContainsKey($name) -or
     [string]$module.dllPath -cne ('artifacts/'+$name+'.dll') -or [string]$module.pdbPath -cne ('artifacts/'+$name+'.pdb') -or
     [string]$module.dllSha256 -cne $expectedModules[$name]){throw 'FAN_HOST_SOURCE_BUNDLE_ARTIFACT_PAIR_MISMATCH'}
  $seenModules[$name]=$true
 }
 if($seenModules.Count -ne 2){throw 'FAN_HOST_SOURCE_BUNDLE_ARTIFACT_PAIR_MISMATCH'}
 $listed=@{}
 foreach($row in @($manifest.files)){
  $relative=[string]$row.path
  if($relative -ceq 'manifest.json' -or $listed.ContainsKey($relative)){throw 'FAN_HOST_SOURCE_BUNDLE_DUPLICATE_MEMBER'}
  $file=Get-SourceBundleMember $relative
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw ('FAN_HOST_SOURCE_BUNDLE_MEMBER_UNAVAILABLE: '+$relative)}
  if((Get-Item -LiteralPath $file).Length -ne [int64]$row.bytes -or (Get-SourceBundleHash $file) -cne [string]$row.sha256){throw ('FAN_HOST_SOURCE_BUNDLE_MEMBER_CHANGED: '+$relative)}
  $listed[$relative]=$true
 }
 foreach($item in Get-ChildItem -LiteralPath $bundleFull -Recurse -Force){
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'FAN_HOST_SOURCE_BUNDLE_REPARSE_REFUSED'}
  if(-not $item.PSIsContainer){
   $relative=$item.FullName.Substring($bundleFull.Length+1).Replace('\','/')
   if($relative -cne 'manifest.json' -and -not $listed.ContainsKey($relative)){throw 'FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED'}
  }
 }
 $drift=@()
 if(-not [string]::IsNullOrWhiteSpace($LiveSourceRoot)){
  foreach($relative in $ExpectedSourcePins.Keys){
   $file=Join-Path $LiveSourceRoot $relative
   $actual=if(Test-Path -LiteralPath $file -PathType Leaf){Get-SourceBundleHash $file}else{$null}
   if($actual -cne ([string]$ExpectedSourcePins[$relative]).ToLowerInvariant()){$drift+= [pscustomobject]@{path=$relative;archiveSha256=([string]$ExpectedSourcePins[$relative]).ToLowerInvariant();liveSha256=$actual}}
  }
 }
 [pscustomobject]@{componentId=[string]$manifest.componentId;sourceRoot=$sourceFull;bundleRoot=$bundleFull;manifestSha256=$ExpectedManifestSha256.ToLowerInvariant();sourceInputs=$sourceRows.Count;archivedSourceInputs=$archivedSourceRows.Count;knownLiveInputsCompared=$(if($LiveSourceRoot){$ExpectedSourcePins.Count}else{0});knownLiveInputDrift=$drift;physicalSourceSelection='approved-component-archive';liveSourceAcceptanceClaimed=$false}
}
