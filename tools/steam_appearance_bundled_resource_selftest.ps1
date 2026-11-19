param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot=(Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'))
$ErrorActionPreference='Stop'
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Fresh fixture output required'}
New-Item -ItemType Directory -Path $Out|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$script:PackRoot=Join-Path $Out 'bundle'
Copy-Item -LiteralPath (Join-Path $ComponentRoot 'steam-appearance') -Destination $script:PackRoot -Recurse
$script:BundleThemeRoot=Join-Path $script:PackRoot 'themes'
$manifestPath=Join-Path $script:PackRoot 'manifest.json'
$fileCount=@(([IO.File]::ReadAllText($manifestPath)|ConvertFrom-Json).files).Count
$script:BundleItems=0;$script:BundleTests=0;$script:MarkedResource=''
function Get-Item {param([string]$LiteralPath,[switch]$Force,$ErrorAction)
 if($LiteralPath.StartsWith($script:BundleThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:BundleItems++}
 $item=Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath -Force:$Force -ErrorAction Stop
 if($LiteralPath -ceq $script:MarkedResource){
  $item|Add-Member -NotePropertyName Attributes -NotePropertyValue ($item.Attributes -bor [IO.FileAttributes]::ReparsePoint) -Force
 }
 $item
}
function Test-Path {param([string]$LiteralPath,$PathType='Any',$ErrorAction)
 if($LiteralPath.StartsWith($script:BundleThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:BundleTests++}
 Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath -PathType $PathType -ErrorAction Stop
}
$script:BundleJsonReads=0;$script:BundleOtherReads=0;$script:WatchedResource='';$script:WatchedHashAttempts=0
Set-Item Function:\Read-BundleConfigOriginal (Get-Command Read-AppearanceThemeConfig).ScriptBlock
Set-Item Function:\Get-BundleHashOriginal (Get-Command Get-ConfigHash).ScriptBlock
function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
 if($Path.StartsWith($script:BundleThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:BundleJsonReads++}
 Read-BundleConfigOriginal @PSBoundParameters
}
function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0,$ObservedLength=$null){
 if($Path.StartsWith($script:BundleThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:BundleOtherReads++}
 if($Path -ceq $script:WatchedResource){$script:WatchedHashAttempts++}
 Get-BundleHashOriginal @PSBoundParameters
}
$cold=Read-PresetManifest
$coldTotal=$script:BundleItems+$script:BundleTests
$coldJson=$script:BundleJsonReads;$coldOther=$script:BundleOtherReads
$script:BundleItems=0;$script:BundleTests=0;$script:BundleJsonReads=0;$script:BundleOtherReads=0
$warm=Read-PresetManifest
$warmTotal=$script:BundleItems+$script:BundleTests
[pscustomobject]@{files=$fileCount;coldProviderLeafProbes=$coldTotal;warmProviderLeafProbes=$warmTotal;coldSchemaReads=$coldJson;warmSchemaReads=$script:BundleJsonReads;coldOtherByteReads=$coldOther;warmOtherByteReads=$script:BundleOtherReads;digest=$warm.digest}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'resource-probes.json') -Encoding UTF8
$schemaCount=2*$script:ThemeNames.Count
if($coldTotal -ne $fileCount -or $warmTotal -ne $fileCount -or $coldJson -ne $schemaCount -or $script:BundleJsonReads -ne $schemaCount -or $coldOther -ne ($fileCount-$schemaCount) -or $script:BundleOtherReads -ne ($fileCount-$schemaCount)){throw 'Unexpected bundled validation read counts'}
if($warm.digest -cne $cold.digest -or $warm.digest -cne (Get-ConfigHash $manifestPath)){throw 'Manifest digest changed between cold and warm validation'}
$resource=Join-Path $script:BundleThemeRoot 'Round\shared.css'
$bytes=[IO.File]::ReadAllBytes($resource)
$expected='Bundled preset checksum failed: Round\shared.css'
foreach($scenario in @('missing','directory','tampered','junction','file-reparse-metadata','oversized')){
 [IO.File]::Delete($resource)
 try{
  switch($scenario){
   directory {[IO.Directory]::CreateDirectory($resource)|Out-Null}
   tampered {[IO.File]::WriteAllText($resource,'fixture mismatch')}
   junction {
    $target=Join-Path $Out 'junction-target';[IO.Directory]::CreateDirectory($target)|Out-Null
    New-Item -ItemType Junction -Path $resource -Target $target -ErrorAction Stop|Out-Null
   }
   'file-reparse-metadata' {
    # Exercise the FileInfo attribute branch without administrator-only symlink creation.
    [IO.File]::WriteAllBytes($resource,$bytes);$script:MarkedResource=$resource
   }
   oversized {
    $stream=[IO.File]::Create($resource)
    try{$stream.SetLength(16777217)}finally{$stream.Dispose()}
    $script:WatchedResource=$resource
   }
  }
  $failure='';try{Read-PresetManifest|Out-Null}catch{$failure=$_.Exception.Message}
  if($failure -cne $expected){throw ('Expected bundled checksum diagnostic for '+$scenario+'; got: '+$failure)}
  if($scenario -eq 'oversized' -and $script:WatchedHashAttempts -ne 0){throw 'Oversized bundled resource reached hashing'}
 }finally{
  $script:MarkedResource='';$script:WatchedResource=''
  if([IO.Directory]::Exists($resource)){[IO.Directory]::Delete($resource,$false)}else{[IO.File]::Delete($resource)}
  [IO.File]::WriteAllBytes($resource,$bytes)
 }
}
foreach($rel in 'Round\shared.css','Round\config_USER.json'){
 $path=Join-Path $script:BundleThemeRoot $rel
 $before=Get-Item -LiteralPath $path
 $data=[IO.File]::ReadAllBytes($path);$copy=$data.Clone();$copy[0]=$copy[0] -bxor 1
 try {
  $null=Read-PresetManifest
  [IO.File]::WriteAllBytes($path,$copy)
  [IO.File]::SetLastWriteTimeUtc($path,$before.LastWriteTimeUtc)
  $after=Get-Item -LiteralPath $path
  if($after.Length -ne $before.Length -or $after.CreationTimeUtc -ne $before.CreationTimeUtc -or $after.LastWriteTimeUtc -ne $before.LastWriteTimeUtc -or $after.Attributes -ne $before.Attributes){throw 'Fixture did not preserve the full replaced-resource stamp'}
  $failure='';try{Read-PresetManifest|Out-Null}catch{$failure=$_.Exception.Message}
  if($failure -cne "Bundled preset checksum failed: $rel"){throw "Same-stamp resource replacement not rejected: $rel ($failure)"}
 }finally{[IO.File]::WriteAllBytes($path,$data);[IO.File]::SetLastWriteTimeUtc($path,$before.LastWriteTimeUtc)}
}
if((Read-PresetManifest).digest -cne $cold.digest){throw 'Good package did not recover after rejected resource replacements'}
'PASS: bundled cold/warm reads, exact digest, six invalid resources, two same-stamp replacements, restored manifest'
