# Production binding for the explicitly reviewed compact HC reference.
# This gate does not register baselines and does not qualify runtime compatibility.
. (Join-Path $PSScriptRoot 'hc-export-baseline-guard.ps1')
function Assert-YmccMainlineHcExport {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[string]$RuntimeRoot='',[string]$ArchivePath='')
 $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
 $lockPath=Join-Path $root 'deps\ymcc-device-core-runtime\export-baseline.lock.json'
 if(!(Test-Path -LiteralPath $lockPath -PathType Leaf)){throw 'HC_EXPORT_REFERENCE_MISSING'}
 $lock=Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8|ConvertFrom-Json
 if($lock.schemaVersion -ne 1 -or !$lock.referenceOnly -or $lock.baselineId -cne 'HC-COMPACT-PARENT-PROMOTED-20261018' -or
    $lock.runtimeId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]+$' -or $lock.currentExpandedPayloadApproved -ne $false){throw 'HC_EXPORT_REFERENCE_INVALID'}
 function Resolve-HcReferenceFile([string]$Relative){
  if(!$Relative -or $Relative -match '(^[\\/]|:|\*|\?|[\x00-\x1f]|(^|[\\/])\.{1,2}([\\/]|$))'){throw 'HC_EXPORT_REFERENCE_UNSAFE_PATH'}
  $full=[IO.Path]::GetFullPath((Join-Path $root $Relative))
  if(!$full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'HC_EXPORT_REFERENCE_PATH_ESCAPED'}
  $cursor=$full
  while($cursor -ne $root){
   if(!(Test-Path -LiteralPath $cursor) -or ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'HC_EXPORT_REFERENCE_REPARSE_OR_MISSING'}
   $cursor=[IO.Path]::GetDirectoryName($cursor)
  }
  return $full
 }
 $arguments=@{
  BaselineProfilePath=(Resolve-HcReferenceFile $lock.files.profile.path);BaselineProfileSha256=[string]$lock.files.profile.sha256
  BaselineManifestPath=(Resolve-HcReferenceFile $lock.files.manifest.path);BaselineManifestSha256=[string]$lock.files.manifest.sha256
  BaselineInventoryPath=(Resolve-HcReferenceFile $lock.files.inventory.path);BaselineInventorySha256=[string]$lock.files.inventory.sha256
 }
 if($lock.approvedChanges){
  $arguments.ApprovedChangesPath=Resolve-HcReferenceFile $lock.approvedChanges.path
  $arguments.ApprovedChangesSha256=[string]$lock.approvedChanges.sha256
 }
 if($RuntimeRoot){$arguments.RuntimeRoot=$RuntimeRoot}
 if($ArchivePath){$arguments.ArchivePath=$ArchivePath;$arguments.ArchivePrefix='PowerControl/handheldcompanion-runtime/'+$lock.runtimeId+'/'}
 if(!$RuntimeRoot -and !$ArchivePath){throw 'HC_EXPORT_REFERENCE_NO_ACTUAL_PAYLOAD'}
 return Assert-YmccHcExportBaseline @arguments
}
