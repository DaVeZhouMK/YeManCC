[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$NativeBinding,
  [Parameter(Mandatory=$true)][string]$HostBinding,
  [Parameter(Mandatory=$true)][string]$SourceDriftBinding,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo=Split-Path -Parent $PSScriptRoot
$allowed='G:\YeManCC-Work\Build\Validation\Controller-Diagnostics-R10-20261013'
$out=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if(-not $out.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be a child of the R10 validation directory'}
$baseline='G:\YeManCC-Work\Release\Packages\YeManCC-0.0.35-FAN971-SIDEBAR-WAKE-20261013-72E3526E.zip'
$baselineHash='72E3526EAEBA43A3A7CF569C6D9427933F98DADDA1EB858555F404F717757419'
function FileHash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function StreamHash($stream){$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$sha.Dispose()}}
function ZipInventory([string]$p){
  $z=[IO.Compression.ZipFile]::OpenRead($p)
  try{
    $rows=@{};foreach($e in $z.Entries){
      $key=$e.FullName.Replace('\','/');if($key.EndsWith('/')){continue}
      if($key.StartsWith('/') -or $key -match '(^|/)\.\.(/|$)' -or $rows.ContainsKey($key)){throw "Unsafe/duplicate package entry: $key"}
      $s=$e.Open();try{$hash=StreamHash $s}finally{$s.Dispose()}
      $rows[$key]=[ordered]@{name=$e.FullName;bytes=$e.Length;sha256=$hash}
    };return $rows
  }finally{$z.Dispose()}
}
function VerifyNative($binding){
  if(-not ($binding.compiled -and $binding.linked -and $binding.sourceStable -and $binding.faultHooksAbsent)){throw 'Native binding is unqualified'}
  $observedDrift=0
  foreach($s in @($binding.source)){
    $currentHash=FileHash $s.Path
    if($currentHash -cne $s.Hash){
      $declared=@($sourceDrift.rows | Where-Object {$_.path -ceq $s.Path -and $_.snapshotSha256 -ceq $s.Hash -and $_.currentSha256 -ceq $currentHash})
      if($declared.Count -ne 1){throw "Unrecorded ambient Native source drift: $($s.Path)"}
      $observedDrift++
    }
  }
  if($observedDrift -ne @($sourceDrift.rows).Count){throw 'Recorded ambient Native drift no longer matches actual current files'}
  $mainSources=@($binding.source | Where-Object {$_.Path.EndsWith('\native\main.cpp',[StringComparison]::OrdinalIgnoreCase)})
  if($mainSources.Count -ne 1){throw 'Native snapshot main is ambiguous'}
  $mainText=Get-Content -LiteralPath $mainSources[0].Snapshot -Raw -Encoding utf8
  foreach($marker in @('keyboard_modifier_snapshot.h','keyboard_modifier_event_evidence.h','keyboard-modifier-events','keyboardModifierObserveRaw(*raw)','keyboardModifierObserveSelfSend')){if(-not $mainText.Contains($marker)){throw ('R10 modifier diagnostic marker missing: '+$marker)}}
  foreach($binary in @($binding.native,$binding.probe)){if((FileHash $binary.Path) -cne $binary.Hash){throw "Native output drift: $($binary.Path)"}}
  $requiredFlags=@('YMCC_MSI_HOST_STAGE_DIAGNOSTIC','YMCC_MSI_VISIBILITY_DIAGNOSTIC','YMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC','YMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC')
  if(@($binding.engineeringFlags).Count -ne 4 -or @(Compare-Object $requiredFlags @($binding.engineeringFlags)).Count){throw 'Combined engineering flag contract mismatch'}
  if($binding.expectedProbeFormat -ne 6 -or $binding.probeCommandSuffix -cne '--raw-msi-receipts'){throw 'Probe command/format contract mismatch'}
  foreach($s in @($binding.source)){if((FileHash $s.Snapshot) -cne $s.Hash){throw "Native frozen snapshot drift: $($s.Snapshot)"}}
  foreach($s in @($binding.dependencyInputs)){if((FileHash $s.Path) -cne $s.Hash){throw "Native dependency drift: $($s.Path)"}}
}
function VerifyHost($binding){
  if($binding.status -cne 'BUILT_BYTE_BOUND_NOT_EXECUTED' -or $binding.buildExitCode -ne 0 -or -not $binding.originalInputsStable -or -not $binding.nativeSourceHashesMatched){throw 'Host binding is unqualified'}
  $original=Get-Content -LiteralPath $binding.originalAfterPath -Raw -Encoding utf8 | ConvertFrom-Json
  foreach($s in @($original)+@($binding.inputs)+@($binding.outputs)){
    $currentHash=FileHash $s.path
    if($currentHash -cne $s.sha256){
      # Original Native files were observed as matching during Host build; they
      # are not compiled Host inputs. Only the two reviewed later source edits
      # may differ; frozen Host sources and all output bytes must still match.
      $declared=@($sourceDrift.rows | Where-Object {$_.path -ceq $s.path -and $_.snapshotSha256 -ceq $s.sha256 -and $_.currentSha256 -ceq $currentHash})
      $isOriginalNative=@($original | Where-Object {$_.path -ceq $s.path}).Count -gt 0 -and $s.path.Contains('\native\')
      if($declared.Count -ne 1 -or -not $isOriginalNative){throw "Host input/output drift: $($s.path)"}
    }
  }
  if((FileHash $NativeBinding) -cne $binding.nativeCandidateBinding.sha256){throw 'Host built against a different Native binding'}
  if(@($binding.dependencyCopies | Where-Object approvedDirectLock).Count -ne 3 -or @($binding.dependencyCopies | Where-Object {-not $_.byteIdentical}).Count){throw 'Host dependency copy check is unqualified'}
}
$sourceDrift=Get-Content -LiteralPath $SourceDriftBinding -Raw -Encoding utf8 | ConvertFrom-Json
if(-not $sourceDrift.reviewed -or -not $sourceDrift.frozenSnapshotStable){throw 'Ambient source drift must be reviewed and frozen inputs unchanged'}
foreach($r in @($sourceDrift.rows)){if([IO.Path]::GetFileName($r.path) -cnotin @('main.cpp','gamepad_driver_setup.h')){throw 'Ambient drift exceeds the separate install-notice scope'}}
$native=Get-Content -LiteralPath $NativeBinding -Raw -Encoding utf8 | ConvertFrom-Json
$matchedHost=Get-Content -LiteralPath $HostBinding -Raw -Encoding utf8 | ConvertFrom-Json
VerifyNative $native;VerifyHost $matchedHost
& (Join-Path $repo 'tools\verify-input-host-dependencies.ps1') -ProjectRoot $repo
if((FileHash $baseline) -cne $baselineHash){throw 'Fixed baseline ZIP changed'}
$before=ZipInventory $baseline
$replacements=@{'YeManCC/YeManCC.exe'=$native.native.Path;'YeManCC/YeManControllerVisibilityProbe.exe'=$native.probe.Path}
$required=@('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json')
foreach($name in $required){
  $files=@($matchedHost.outputs | Where-Object {[IO.Path]::GetFileName($_.path) -ceq $name})
  if($files.Count -ne 1){throw "Missing/ambiguous matched Host cohort member: $name"}
  $key='PowerControl/feature-assets/virtual-gamepad/'+$name
  if(-not $before.ContainsKey($key)){throw "Baseline lacks approved Host entry: $key"}
  $replacements[$key]=$files[0].path
}
# Preserve the existing approved placement: some DLLs are resolved from the HC
# shared directory rather than copied into virtual-gamepad.
foreach($copy in $matchedHost.dependencyCopies){
  $name=[IO.Path]::GetFileName($copy.output.path)
  $locations=@("PowerControl/feature-assets/virtual-gamepad/$name","PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/$name")
  $found=$false
  foreach($key in $locations){if($before.ContainsKey($key)){if($before[$key].sha256 -cne $copy.output.sha256){throw "Baseline Host dependency mismatch: $key"};$found=$true}}
  if(-not $found){throw "No approved baseline location for Host dependency: $name"}
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$package=Join-Path $out 'YeManCC-0.0.35-GP-R10-MSI-SHIELD-MODIFIER-20261013.zip'
if(Test-Path -LiteralPath $package){throw 'Candidate output already exists; choose a fresh output directory'}
Copy-Item -LiteralPath $baseline -Destination $package
$z=[IO.Compression.ZipFile]::Open($package,[IO.Compression.ZipArchiveMode]::Update)
try{
  foreach($key in $replacements.Keys){
    if($before.ContainsKey($key)){$entry=$z.GetEntry($before[$key].name);$entry.Delete();$name=$before[$key].name}else{$name=$key.Replace('/','\')}
    $entry=$z.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
    $dst=$entry.Open();$src=[IO.File]::OpenRead($replacements[$key]);try{$src.CopyTo($dst)}finally{$src.Dispose();$dst.Dispose()}
  }
}finally{$z.Dispose()}
$after=ZipInventory $package
$changed=@();$unchanged=0
foreach($key in $before.Keys){
  if(-not $after.ContainsKey($key)){throw "Baseline entry was removed: $key"}
  if($before[$key].sha256 -cne $after[$key].sha256){if(-not $replacements.ContainsKey($key)){throw "Unapproved package change: $key"};$changed+=@([ordered]@{entry=$key;before=$before[$key].sha256;after=$after[$key].sha256})}else{$unchanged++}
}
$added=@($after.Keys | Where-Object {-not $before.ContainsKey($_)})
if($added.Count -ne 1 -or $added[0] -cne 'YeManCC/YeManControllerVisibilityProbe.exe'){throw 'Unexpected added entry set'}
foreach($key in $replacements.Keys){if($after[$key].sha256 -cne (FileHash $replacements[$key])){throw "Candidate binary mismatch: $key"}}
# Pin the complete Host dependency closure; leave the baseline shared fallback DLLs unchanged.
$depsPath=$replacements['PowerControl/feature-assets/virtual-gamepad/YeManInputHost.deps.json']
$deps=Get-Content -LiteralPath $depsPath -Raw -Encoding utf8 | ConvertFrom-Json
foreach($target in $deps.targets.PSObject.Properties){foreach($pkg in $target.Value.PSObject.Properties){if($pkg.Value.runtime){foreach($name in $pkg.Value.runtime.PSObject.Properties.Name){
  if(-not $after.ContainsKey('PowerControl/feature-assets/virtual-gamepad/'+$name) -and -not $after.ContainsKey('PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/'+$name)){throw "Declared Host runtime asset absent: $name"}
}}}}
VerifyNative $native;VerifyHost $matchedHost
if((FileHash $baseline) -cne $baselineHash){throw 'Baseline changed during packaging'}
[ordered]@{schemaVersion=1;createdAt=[DateTimeOffset]::Now.ToString('o');package=$package;packageSha256=(FileHash $package);baseline=$baseline;baselineSha256=$baselineHash;nativeBinding=$NativeBinding;hostBinding=$HostBinding;changedEntries=$changed;addedEntries=$added;unchangedBaselineEntries=$unchanged;baselineEntryCount=$before.Count;candidateEntryCount=$after.Count;allUnlistedEntriesByteIdentical=$true;nativeFrozenAndHostSourceRechecked=$true;ambientSourceDriftBinding=$SourceDriftBinding;ambientSourceDriftCount=@($sourceDrift.rows).Count;productionLaunched=$false;liveProbeLaunched=$false;servicesOrTasksOperated=$false;deployed=$false;hardwareVerified=$false;scope='Matched R10 engineering package with MSI Host-stage and visibility flags plus ROG/MSI modifier snapshot and source diagnostics over the fixed shared FAN971 baseline; all other entries byte-identical. Later shared installation-notice edits are preserved in source and separately recorded; they are not part of this frozen Native binary. Shared Native snapshot includes prior work of other tasks; this is not a claim that all compiled source differences are MSI-only.'} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $out 'package-verification.json') -Encoding utf8
Write-Output "CONTROLLER_R10_ENGINEERING_PACKAGE_OK $package"




