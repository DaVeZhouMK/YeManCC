# Explicit, hash-pinned compact baseline gate. No baseline is auto-registered.
# Identity/inventory verification only; ABI and runtime acceptance remain separate.
function Assert-YmccHcExportBaseline {
 [CmdletBinding()]
 param(
  [Parameter(Mandatory=$true)][string]$BaselineProfilePath,
  [Parameter(Mandatory=$true)][string]$BaselineProfileSha256,
  [Parameter(Mandatory=$true)][string]$BaselineManifestPath,
  [Parameter(Mandatory=$true)][string]$BaselineManifestSha256,
  [Parameter(Mandatory=$true)][string]$BaselineInventoryPath,
  [Parameter(Mandatory=$true)][string]$BaselineInventorySha256,
  [string]$RuntimeRoot='',
  [string]$ArchivePath='',
  [string]$ArchivePrefix='',
  [string]$CandidateInventoryPath='',
  [string]$CandidateInventorySha256='',
  [string]$ApprovedChangesPath='',
  [string]$ApprovedChangesSha256=''
 )
 $ErrorActionPreference='Stop'
 function Refuse([string]$Code,$Details=$null){
  $errorObject=[IO.InvalidDataException]::new(('HC_EXPORT_BASELINE_'+$Code))
  if($null -ne $Details){$errorObject.Data['details']=ConvertTo-Json -InputObject $Details -Depth 15 -Compress}
  throw $errorObject
 }
 function Name([string]$Value){
  if([string]::IsNullOrWhiteSpace($Value) -or $Value.Contains('\') -or $Value.Contains(':') -or $Value.StartsWith('/') -or $Value.Contains('//') -or $Value -match '(^|/)\.{1,2}(/|$)' -or $Value -match '[\x00-\x1f]'){Refuse 'UNSAFE_PATH' $Value}
  return $Value
 }
 function Digest([IO.Stream]$Stream){
  $algorithm=[Security.Cryptography.SHA256]::Create()
  try{return ([BitConverter]::ToString($algorithm.ComputeHash($Stream))).Replace('-','').ToLowerInvariant()}finally{$algorithm.Dispose()}
 }
 function FileHash([string]$Path){$stream=[IO.File]::OpenRead($Path);try{return Digest $stream}finally{$stream.Dispose()}}
 function PinnedJson([string]$Path,[string]$Pin,[string]$Code){
  if($Pin -cnotmatch '^[0-9a-f]{64}$' -or -not [IO.File]::Exists($Path)){Refuse ($Code+'_PIN')}
  $bytes=[IO.File]::ReadAllBytes($Path);$stream=[IO.MemoryStream]::new($bytes,$false)
  try{$actual=Digest $stream}finally{$stream.Dispose()}
  if($actual -cne $Pin){Refuse ($Code+'_HASH')}
  return [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff)|ConvertFrom-Json
 }
 function Rows($Values,[string]$Code){
  $map=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($r in @($Values)){
   $relative=Name ([string]$r.path)
   if($map.ContainsKey($relative) -or [string]$r.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $null -eq $r.bytes -or [string]$r.bytes -notmatch '^\d+$'){Refuse $Code $relative}
   $map.Add($relative,[pscustomobject]@{path=$relative;bytes=[int64]$r.bytes;sha256=[string]$r.sha256})
  }
  if(-not $map.Count){Refuse $Code 'Empty inventory'}
  return ,$map
 }
 function Same($A,$B){return $null -ne $A -and $null -ne $B -and $A.path -ceq $B.path -and [int64]$A.bytes -eq [int64]$B.bytes -and $A.sha256 -ceq $B.sha256}
 $profile=PinnedJson $BaselineProfilePath $BaselineProfileSha256 'PROFILE'
 $manifest=PinnedJson $BaselineManifestPath $BaselineManifestSha256 'MANIFEST'
 $inventory=PinnedJson $BaselineInventoryPath $BaselineInventorySha256 'INVENTORY'
 if([int]$profile.schemaVersion -ne 1 -or [int]$manifest.schemaVersion -ne 1 -or [int]$inventory.schemaVersion -ne 1 -or $manifest.runtimeId -cne $profile.runtimeId -or $manifest.deviceCoreProfileSha256 -cne $BaselineProfileSha256){Refuse 'BASELINE_BINDING'}
 $runtimeId=Name ([string]$profile.runtimeId)
 if($runtimeId.Contains('/')){Refuse 'RUNTIME_ID'}
 $expected=Rows $inventory.files 'BASELINE_INVENTORY'
 $manifestName='HandheldCompanion.runtime.json'
 if(-not $expected.ContainsKey($manifestName) -or $expected[$manifestName].sha256 -cne $BaselineManifestSha256 -or $expected[$manifestName].bytes -ne (Get-Item -LiteralPath $BaselineManifestPath).Length){Refuse 'BASELINE_MANIFEST_INVENTORY'}
 $declared=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($r in @($manifest.files)){
  $relative=Name ([string]$r.path)
  if(-not $declared.Add($relative) -or -not $expected.ContainsKey($relative) -or $expected[$relative].path -cne $relative -or $expected[$relative].sha256 -cne [string]$r.sha256){Refuse 'BASELINE_CLOSURE' $relative}
 }
 if($declared.Count+1 -ne $expected.Count){Refuse 'BASELINE_CLOSURE'}
 foreach($replacement in @($profile.replacements)){
  $relative=Name ([string]$replacement.path)
  if(-not $expected.ContainsKey($relative) -or $expected[$relative].sha256 -cne [string]$replacement.sha256 -or $expected[$relative].bytes -ne [int64]$replacement.bytes){Refuse 'BASELINE_REPLACEMENT' $relative}
 }
 $reviewedCount=0
 if([bool]$ApprovedChangesPath -ne [bool]$ApprovedChangesSha256){Refuse 'REVIEW_PIN_PAIR'}
 if($ApprovedChangesPath){
  $review=PinnedJson $ApprovedChangesPath $ApprovedChangesSha256 'REVIEW'
  if([int]$review.schemaVersion -ne 1 -or $review.baselineProfileSha256 -cne $BaselineProfileSha256 -or $review.baselineManifestSha256 -cne $BaselineManifestSha256 -or $review.baselineInventorySha256 -cne $BaselineInventorySha256){Refuse 'REVIEW_BASELINE'}
  $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($change in @($review.changes)){
   $relative=Name ([string]$change.path)
   if(-not $seen.Add($relative) -or [string]::IsNullOrWhiteSpace([string]$change.reason)){Refuse 'REVIEW_ENTRY' $relative}
   if($null -eq $change.before){if($expected.ContainsKey($relative)){Refuse 'REVIEW_BEFORE' $relative}}
   else{if(-not $expected.ContainsKey($relative)){Refuse 'REVIEW_BEFORE' $relative};$before=Rows @([pscustomobject]@{path=$relative;bytes=$change.before.bytes;sha256=$change.before.sha256}) 'REVIEW_BEFORE';if(-not (Same $expected[$relative] $before[$relative])){Refuse 'REVIEW_BEFORE' $relative}}
   if($null -eq $change.after){if($null -eq $change.before){Refuse 'REVIEW_EMPTY' $relative};$null=$expected.Remove($relative)}
   else{$after=Rows @([pscustomobject]@{path=$relative;bytes=$change.after.bytes;sha256=$change.after.sha256}) 'REVIEW_AFTER';$expected[$relative]=$after[$relative]}
   $reviewedCount++
  }
 }
 $inputs=[Collections.Generic.List[object]]::new()
 if($RuntimeRoot){
  $folder=[IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
  if(-not [IO.Directory]::Exists($folder) -or ((Get-Item -LiteralPath $folder -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){Refuse 'RUNTIME_ROOT'}
  # Walk one level at a time so directory links are rejected before traversal.
  $pending=[Collections.Generic.Queue[string]]::new();$pending.Enqueue($folder)
  $values=[Collections.Generic.List[object]]::new()
  while($pending.Count){foreach($entry in Get-ChildItem -LiteralPath $pending.Dequeue() -Force){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){Refuse 'RUNTIME_REPARSE' $entry.FullName}
   if($entry.PSIsContainer){$pending.Enqueue($entry.FullName)}else{$values.Add([pscustomobject]@{path=$entry.FullName.Substring($folder.Length+1).Replace('\','/');bytes=$entry.Length;sha256=(FileHash $entry.FullName)})}
  }}
  $inputs.Add([pscustomobject]@{kind='runtime-files';path=$folder;files=(Rows $values.ToArray() 'RUNTIME_INVENTORY')})
 }
 if($ArchivePath){
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  if(-not $ArchivePrefix){$ArchivePrefix='PowerControl/handheldcompanion-runtime/'+$runtimeId+'/'}
  $prefix=(Name $ArchivePrefix.TrimEnd('/'))+'/'
  $archive=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ArchivePath))
  try{
   $all=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$values=[Collections.Generic.List[object]]::new()
   foreach($entry in $archive.Entries){
    $full=$entry.FullName.Replace('\','/')
    if($full.EndsWith('/')){if($full.TrimEnd('/')){$null=Name $full.TrimEnd('/')};continue}
    $null=Name $full
    if(-not $all.Add($full)){Refuse 'ZIP_DUPLICATE' $full}
    if($full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){
     if(-not $full.StartsWith($prefix,[StringComparison]::Ordinal)){Refuse 'ZIP_PATH_CASE' $full}
     $stream=$entry.Open();try{$hash=Digest $stream}finally{$stream.Dispose()}
     $values.Add([pscustomobject]@{path=$full.Substring($prefix.Length);bytes=$entry.Length;sha256=$hash})
    }elseif($full.StartsWith('PowerControl/handheldcompanion-runtime/',[StringComparison]::OrdinalIgnoreCase)){Refuse 'ZIP_OTHER_RUNTIME' $full}
   }
   $inputs.Add([pscustomobject]@{kind='zip-entry-bytes';path=[IO.Path]::GetFullPath($ArchivePath);files=(Rows $values.ToArray() 'ZIP_INVENTORY')})
  }finally{$archive.Dispose()}
 }
 if([bool]$CandidateInventoryPath -ne [bool]$CandidateInventorySha256){Refuse 'CANDIDATE_INVENTORY_PIN_PAIR'}
 if($CandidateInventoryPath){$candidate=PinnedJson $CandidateInventoryPath $CandidateInventorySha256 'CANDIDATE_INVENTORY';$inputs.Add([pscustomobject]@{kind='pinned-inventory-only';path=$CandidateInventoryPath;files=(Rows $candidate.files 'CANDIDATE_INVENTORY')})}
 if(-not $inputs.Count){Refuse 'NO_CANDIDATE'}
 $differences=[Collections.Generic.List[object]]::new()
 foreach($input in $inputs){
  foreach($r in $expected.Values){
   if(-not $input.files.ContainsKey($r.path)){$differences.Add([pscustomobject]@{input=$input.kind;path=$r.path;problem='missing';expected=$r});continue}
   $actual=$input.files[$r.path]
   if(-not (Same $r $actual)){$differences.Add([pscustomobject]@{input=$input.kind;path=$r.path;problem='unreviewed-bytes-or-path-change';expected=$r;actual=$actual;largerThanReviewed=([int64]$actual.bytes -gt [int64]$r.bytes)})}
  }
  foreach($r in $input.files.Values){if(-not $expected.ContainsKey($r.path)){$differences.Add([pscustomobject]@{input=$input.kind;path=$r.path;problem='extra';actual=$r})}}
 }
 if($differences.Count){Refuse 'CANDIDATE_DIFF' $differences.ToArray()}
 return [pscustomobject]@{status='PASS';runtimeId=$runtimeId;files=$expected.Count;baselineProfileSha256=$BaselineProfileSha256;baselineManifestSha256=$BaselineManifestSha256;baselineInventorySha256=$BaselineInventorySha256;approvedChangesSha256=$ApprovedChangesSha256;reviewedChanges=$reviewedCount;inputs=@($inputs|ForEach-Object {[pscustomobject]@{kind=$_.kind;path=$_.path;files=$_.files.Count}});autoRegisteredBaseline=$false;identityOnly=$true;abiAcceptanceClaimed=$false;installedDeploymentPerformed=$false}
}