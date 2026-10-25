[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$plan=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
$root=[IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
if(Test-Path -LiteralPath $root){throw 'Fresh Host connection test directory required'}
[IO.Directory]::CreateDirectory($root)|Out-Null
. (Join-Path $PSScriptRoot 'verify-installed-host-binding.ps1')
$rows=[Collections.Generic.List[object]]::new()
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Check([bool]$Pass,[string]$Name){$rows.Add([pscustomobject]@{name=$Name;passed=$Pass});if(-not $Pass){throw ('CHECK_FAILED: '+$Name)}}
function Fixture([string]$Name){
 $dst=[IO.Path]::GetFullPath((Join-Path $root $Name))
 if(-not $dst.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test fixture escaped evidence root'}
 foreach($rel in @($plan.changes.relative)+@($plan.preserved.relative)|Select-Object -Unique){
  if([IO.Path]::IsPathRooted($rel) -or $rel -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'Unsafe fixture path'}
  $to=Join-Path $dst $rel;[IO.Directory]::CreateDirectory((Split-Path -Parent $to))|Out-Null
  $source=Join-Path $plan.installRoot $rel
  $changed=@($plan.changes|Where-Object {$_.relative -ceq $rel})
  if($changed.Count -eq 1 -and (Hash $source) -cne $changed[0].beforeSha256){
   $source=Join-Path $plan.evidenceDir ('preimage/'+$rel)
   if((Hash $source) -cne $changed[0].beforeSha256){throw ('Original connection preimage unavailable: '+$rel)}
  }
  Copy-Item -LiteralPath $source -Destination $to
 }
 return $dst
}
function Rejection([scriptblock]$Body,[string]$Reason,[string]$Name){
 try{& $Body|Out-Null}catch{Check ($_.Exception.Message.Contains($Reason)) $Name;return}
 Check $false $Name
}
$verified=Assert-YmccInstalledHostBinding -Root $plan.stageRoot -ExpectedHostSha256 $plan.newHostDllSha256 -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256
Check ($verified.status -eq 'PASS') 'complete staged Host/update/authorization/runtime/export chain passes'
$baseline=Fixture 'preserved-old-binding'
if($plan.kind -cin @('host-only-v2','host-only-v3')){
 $oldHostHash=Hash (Join-Path $baseline 'PowerControl/fan-host-v2/YeManFanHost.dll')
 $oldBinding=Assert-YmccInstalledHostBinding -Root $baseline -ExpectedHostSha256 $oldHostHash -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256
 Check ($oldBinding.status -eq 'PASS' -and $oldHostHash -cne $plan.newHostDllSha256) 'valid current old installed chain is preserved as a distinct preimage'
 Rejection {Assert-YmccInstalledHostBinding -Root $baseline -ExpectedHostSha256 $plan.newHostDllSha256 -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256} 'HOST_BINDING_HOST_IDENTITY_MISMATCH' 'valid old binding cannot masquerade as new candidate'
}else{
 Rejection {Assert-YmccInstalledHostBinding -Root $baseline -ExpectedHostSha256 (Hash (Join-Path $baseline 'PowerControl/fan-host-v2/YeManFanHost.dll')) -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256} 'HOST_BINDING_RUNTIME_MANIFEST_MISMATCH' 'historical old runtime binding must fail under its original regression'
}
# Execute the same connector against copied ordinary files only. Never start Host or inspect session/token state.
$engine=Join-Path $PSHOME 'pwsh.exe';$connector=Join-Path $PSScriptRoot 'connect-installed-components.ps1'
foreach($case in @('success','locked-backend')){
 $copy=Fixture $case;$testPlan=$plan|ConvertTo-Json -Depth 15|ConvertFrom-Json
 $testPlan.installRoot=$copy;$testPlan.evidenceDir=Join-Path $root ($case+'-receipt')
 $testFile=Join-Path $root ($case+'-plan.json');[IO.File]::WriteAllText($testFile,($testPlan|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
 $lock=$null
 try{
  if($case -eq 'locked-backend'){$lock=[IO.File]::Open((Join-Path $copy 'PowerControl/fan-host-v2/YeManFanBackend.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
  & $engine -NoProfile -File $connector -PlanPath $testFile *> (Join-Path $root ($case+'.log'));$code=$LASTEXITCODE
 }finally{if($lock){$lock.Dispose()}}
 $receipt=Get-Content -LiteralPath (Join-Path $testPlan.evidenceDir 'verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
 if($case -eq 'success'){
  Check ($code -eq 0 -and $receipt.passed -and @($receipt.changedFiles).Count -eq 6 -and $receipt.binding.updateBindingVerified) 'same connector commits all six files and verifies current update binding'
 }else{
  Check ($code -eq 2 -and -not $receipt.passed -and $receipt.rollbackPerformed -and -not $receipt.rollbackError -and -not $receipt.rollbackConflict) 'second-file sharing failure triggers complete atomic rollback'
  Check ((Hash (Join-Path $copy 'PowerControl/fan-host-v2/YeManFanHost.dll')) -eq ($plan.changes|Where-Object relative -eq 'PowerControl/fan-host-v2/YeManFanHost.dll').beforeSha256) 'first replaced Host was actually restored after Backend lock failure'
  foreach($change in $plan.changes){Check ((Hash (Join-Path $copy $change.relative)) -eq $change.beforeSha256) ('rollback preserves original '+$change.relative)}
 }
 foreach($item in $plan.preserved){if((Hash (Join-Path $copy $item.relative)) -ne $item.sha256){throw ('Unrelated fixture file changed: '+$item.relative)}}
 $transactionTemps=@(Get-ChildItem -LiteralPath $copy -Recurse -File -Filter '*.ymcc-*.tmp')
 Check ($transactionTemps.Count -eq 0) ('no transaction temporary files remain: '+$case)
 Check $true ('all '+@($plan.preserved).Count+' preserved files unchanged: '+$case)
}
# Admission remains independent of a caller-controlled plan kind. These invoke the real connector.
$originalTransactionChecks=$rows.Count
$admissionCases=[Collections.Generic.List[object]]::new()
$baselineRecords=@($plan.changes|ForEach-Object {[pscustomobject]@{relative=$_.relative;sha256=$_.beforeSha256}})+@($plan.preserved)
function RejectAdmission($TestPlan,[string]$Name,[string]$Expected){
 $TestPlan.installRoot=$baseline;$TestPlan.evidenceDir=Join-Path $root ($Name+'-receipt')
 $file=Join-Path $root ($Name+'-plan.json')
 [IO.File]::WriteAllText($file,($TestPlan|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
 & $engine -NoProfile -NonInteractive -File $connector -PlanPath $file *> (Join-Path $root ($Name+'.log'));$code=$LASTEXITCODE
 $proofFile=Join-Path $TestPlan.evidenceDir 'verification.json'
 $proof=Get-Content -LiteralPath $proofFile -Raw -Encoding UTF8|ConvertFrom-Json
 $drift=@($baselineRecords|Where-Object {(Hash (Join-Path $baseline $_.relative)) -cne $_.sha256})
 $admissionCases.Add([pscustomobject]@{id=$Name;naturalExitCode=$code;error=$proof.error;actualChangedFiles=@($proof.changedFiles);ordinaryFileDrift=@($drift|ForEach-Object {$_.relative});receipt=$proofFile;receiptSha256=(Hash $proofFile)})
 Check ($code -eq 2 -and $proof.passed -eq $false -and $proof.error.Contains($Expected) -and @($proof.changedFiles).Count -eq 0 -and -not $proof.rollbackPerformed) ('admission rejects before replacement: '+$Name)
 Check ($drift.Count -eq 0) ('all '+$baselineRecords.Count+' originals unchanged: '+$Name)
}
if($plan.kind -ceq 'host-only-v2'){
 foreach($name in @('v2-missing-capability','v1-downgrade','unknown-kind-downgrade','array-kind','numeric-kind','v1-forged-legacy-pins')){
  $negative=$plan|ConvertTo-Json -Depth 30|ConvertFrom-Json
  $negative.PSObject.Properties.Remove('capabilityEvidence')
  switch($name){
   'v2-missing-capability' {$reason='HOST_CAPABILITY_CONTRACT'}
   'v1-downgrade' {$negative.kind='host-only-v1';$reason='HOST_ADMISSION_V2_REQUIRED'}
   'unknown-kind-downgrade' {$negative.kind='host-only-v0';$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'array-kind' {$negative.kind=@('host-only-v2');$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'numeric-kind' {$negative.kind=2;$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'v1-forged-legacy-pins' {
    $negative.kind='host-only-v1'
    $negative.newHostDllSha256=Hash (Join-Path $baseline 'PowerControl/fan-host-v2/YeManFanHost.dll')
    $negative.newBackendDllSha256=Hash (Join-Path $baseline 'PowerControl/fan-host-v2/YeManFanBackend.dll')
    $negative.newHostManifestSha256=Hash (Join-Path $baseline 'PowerControl/fan-host-v2/YeManFanHost.payload.json')
    $reason='HOST_ADMISSION_LEGACY_TUPLE_MISMATCH'
   }
  }
  RejectAdmission $negative $name $reason
 }

 }elseif($plan.kind -ceq 'host-only-v3'){
 foreach($name in @('v3-missing-capability','v1-downgrade','v2-downgrade','unknown-kind-downgrade','array-kind','numeric-kind','retired149-tuple','missing-full-host-proof')){
  $negative=$plan|ConvertTo-Json -Depth 30|ConvertFrom-Json
  switch($name){
   'v3-missing-capability' {$negative.PSObject.Properties.Remove('capabilityEvidence');$reason='HOST_CATALOG_CONTRACT'}
   'v1-downgrade' {$negative.kind='host-only-v1';$reason='HOST_ADMISSION_V2_REQUIRED'}
   'v2-downgrade' {$negative.kind='host-only-v2';$reason='HOST_CAPABILITY_CONTRACT'}
   'unknown-kind-downgrade' {$negative.kind='host-only-v0';$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'array-kind' {$negative.kind=@('host-only-v3');$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'numeric-kind' {$negative.kind=3;$reason='HOST_ADMISSION_UNKNOWN_KIND'}
   'retired149-tuple' {$negative.newHostDllSha256='149042f482b122adc85a19a905c8579c75cfbc881cd78d85c221aac1fe5cb424';$reason='HOST_CATALOG_FIXED_CANDIDATE_TUPLE'}
   'missing-full-host-proof' {$negative.capabilityEvidence.fullHostSelfTest=$null;$reason='HOST_CATALOG_PROOF_DESCRIPTOR'}
  }
  RejectAdmission $negative $name $reason
 }
 }elseif($plan.kind -ceq 'host-only-v1'){
 foreach($name in @('old-tuple-unknown-kind-six','old-tuple-missing-kind-six','old-tuple-unknown-kind-ten','old-tuple-missing-kind-ten')){
  $negative=$plan|ConvertTo-Json -Depth 30|ConvertFrom-Json
  if($name.Contains('missing-kind')){$negative.PSObject.Properties.Remove('kind')}else{$negative.kind='host-only-v0'}
  if($name.EndsWith('-ten')){
   foreach($member in @('SharpDX.dll','SharpDX.DirectInput.dll','SharpDX.Direct3D9.dll','HandheldCompanion.runtime.json')){
    $relative=$plan.runtimeRelative+'/'+$member
    $beforeHash=Hash (Join-Path $baseline $relative);$afterHash=Hash (Join-Path $plan.stageRoot $relative)
    if($beforeHash -cne $afterHash){throw 'Legacy ten-file cross-control must use unchanged original runtime members'}
    $negative.changes+=@([pscustomobject]@{relative=$relative;sourceRelative=$relative;beforeSha256=$beforeHash;afterSha256=$afterHash})
   }
  }
  RejectAdmission $negative $name 'HOST_ADMISSION_UNKNOWN_KIND'
 }

 $negative=$plan|ConvertTo-Json -Depth 30|ConvertFrom-Json
 $negative.newHostDllSha256='0'*64
 RejectAdmission $negative 'legacy-wrong-declared-host-hash' 'HOST_ADMISSION_V2_REQUIRED'
 # A real successful old plan above fixes these input paths; each mutation changes one actual file.
 $legacyManifest=Get-Content -LiteralPath (Join-Path $plan.stageRoot 'PowerControl/fan-host-v2/YeManFanHost.payload.json') -Raw|ConvertFrom-Json
 $legacyFiles=@('PowerControl/fan-host-v2/YeManFanHost.payload.json')+@($legacyManifest.files|ForEach-Object {'PowerControl/fan-host-v2/'+$_.path})+@(($plan.runtimeRelative+'/HandheldCompanion.runtime.json'),($plan.runtimeRelative+'/HandheldCompanion.dll'))
 $index=0
 foreach($changedFile in $legacyFiles){
  $index++;$name='legacy-tuple-file-'+$index
  $copyStage=[IO.Path]::GetFullPath((Join-Path $root ($name+'-stage')))
  foreach($relative in $legacyFiles){
   if([IO.Path]::IsPathRooted($relative) -or $relative.Replace('\','/') -match '(^|/)\.\.?(/|$)'){throw 'Unsafe legacy stage fixture path'}
   $target=[IO.Path]::GetFullPath((Join-Path $copyStage $relative))
   if(-not $target.StartsWith($copyStage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Legacy mutation escaped stage fixture'}
   [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
   Copy-Item -LiteralPath (Join-Path $plan.stageRoot $relative) -Destination $target
  }
  $mutated=Join-Path $copyStage $changedFile
  [IO.File]::AppendAllText($mutated,'admission-counterexample',[Text.UTF8Encoding]::new($false))
  $negative=$plan|ConvertTo-Json -Depth 30|ConvertFrom-Json;$negative.stageRoot=$copyStage
  RejectAdmission $negative $name ('HOST_ADMISSION_LEGACY_TUPLE_MISMATCH: '+$changedFile)
 }
}
$report=[ordered]@{status='PASS';checks=$rows;checksPassed=$rows.Count;originalTransactionChecks=$originalTransactionChecks;admissionCases=@($admissionCases);hardwareIo=$false;hostStarted=$false;installedTreeWritten=$false;sourcePlanSha256=(Hash $PlanPath)}
[IO.File]::WriteAllText((Join-Path $root 'result.json'),($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ('HOST_CONNECTION_SELFTEST PASS '+$rows.Count)