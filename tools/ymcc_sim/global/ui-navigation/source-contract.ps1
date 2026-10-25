# Exact source snapshots for the existing UI harness. No browser or App access.
function Get-SimUiTemplateSnapshot {
 param([Parameter(Mandatory=$true)][string]$Root)
 $source=[IO.Path]::GetFullPath($Root)
 $files=@(Get-ChildItem -LiteralPath $source -Recurse -File | Sort-Object FullName | ForEach-Object {
  if(($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'UI_TEMPLATE_LINK_UNSUPPORTED'}
  [pscustomobject]@{relative=[IO.Path]::GetRelativePath($source,$_.FullName).Replace('\','/');source=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
 })
 [pscustomobject]@{root=$source;files=$files}
}
function Compare-SimUiTemplateSnapshot {
 param([Parameter(Mandatory=$true)]$Before,[Parameter(Mandatory=$true)]$After)
 $previous=@{};$current=@{}
 foreach($row in $Before.files){$previous[$row.relative]=$row.sha256}
 foreach($row in $After.files){$current[$row.relative]=$row.sha256}
 $changes=@(foreach($name in @($previous.Keys+$current.Keys|Sort-Object -Unique)){
  if(-not $previous.ContainsKey($name)){[pscustomobject]@{path=$name;kind='added'}}
  elseif(-not $current.ContainsKey($name)){[pscustomobject]@{path=$name;kind='missing'}}
  elseif($previous[$name] -ne $current[$name]){[pscustomobject]@{path=$name;kind='changed'}}
 })
 [pscustomobject]@{unchanged=$changes.Count -eq 0 -and $previous.Count -gt 0;changes=$changes}
}
function Copy-SimUiTemplateSnapshot {
 param([Parameter(Mandatory=$true)]$Snapshot,[Parameter(Mandatory=$true)][string]$Destination)
 $targetRoot=[IO.Path]::GetFullPath($Destination)
 foreach($row in $Snapshot.files){
  $target=[IO.Path]::GetFullPath((Join-Path $targetRoot $row.relative))
  if(-not $target.StartsWith($targetRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'UI_TEMPLATE_DESTINATION_ESCAPE'}
  if((Get-FileHash -LiteralPath $row.source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){throw 'UI_TEMPLATE_CHANGED_BEFORE_COPY'}
  New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null
  Copy-Item -LiteralPath $row.source -Destination $target
  if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){throw 'UI_TEMPLATE_COPY_IDENTITY_MISMATCH'}
 }
}