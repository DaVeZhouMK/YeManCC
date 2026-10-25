[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$EnginePath,[Parameter(Mandatory=$true)][string]$EvidenceRoot,[Parameter(Mandatory=$true)][string]$LegacyV2PlanPath,[Parameter(Mandatory=$true)][string]$ReviewedV3ContractPath)
$ErrorActionPreference='Stop'
$ownModules=Join-Path $PSHOME 'Modules';$env:PSModulePath=$ownModules+';'+$env:PSModulePath
if(Test-Path -LiteralPath $EvidenceRoot){throw 'Fresh v4 test evidence required'};New-Item -ItemType Directory -Path $EvidenceRoot|Out-Null
. (Join-Path $PSScriptRoot 'verify-lighting-catalog-evidence.ps1')
$base=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json;$checks=[Collections.Generic.List[object]]::new();$executions=[Collections.Generic.List[object]]::new()
function Save([string]$file,$value){[IO.File]::WriteAllText($file,($value|ConvertTo-Json -Depth 90),[Text.UTF8Encoding]::new($false))}
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{name=$name;passed=$ok});if(-not$ok){throw ('CHECK_FAILED '+$name)};Write-Output ('PASS '+$name)}
function Clone($value){$value|ConvertTo-Json -Depth 90|ConvertFrom-Json}
$positive=Assert-YmccLightingCatalogEvidence -Plan $base
Check ($positive.kind -ceq 'lighting-build-v4' -and $positive.nativeConsumerCases -eq 22 -and $positive.actualArtifactChecks -eq 282) 'exact dynamic native/helper tuple admitted'
function RejectPlan([string]$name,[scriptblock]$mutate,[string]$expected){$p=Clone $base;&$mutate $p;$errorText=$null;try{Assert-YmccLightingCatalogEvidence -Plan $p|Out-Null}catch{$errorText=$_.Exception.Message};Check ($errorText -and $errorText.Contains($expected)) ('admission rejects '+$name)}
RejectPlan 'helper retag' {param($p)$p.newHelperSha256='0'*64} 'HELPER_CORE_TUPLE'
RejectPlan 'native retag' {param($p)$p.preservedNativeSha256='0'*64} 'NATIVE_TUPLE'
RejectPlan 'core retag' {param($p)$p.preservedCoreSha256='0'*64} 'HELPER_CORE_TUPLE'
RejectPlan 'missing manifest pin' {param($p)$p.buildDelivery.sha256='0'*64} 'MANIFEST_BYTES'
RejectPlan 'extra write target' {param($p)$p.changes+=@{relative='YeManCC/extra.bin'}} 'WRITE_SET'
RejectPlan 'missing protected core' {param($p)$p.preserved=@($p.preserved|Where-Object relative -CNE 'PowerControl/light-setter/YeManLightDeviceCore.dll')} 'PRESERVED_SET'
$manifestPath=$base.buildDelivery.path;$manifestBytes=[IO.File]::ReadAllBytes($manifestPath);$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
foreach($case in @('wrong-catalog','wrong-profile','missing-native-source-checksum','missing-native-consumer','runtime-file-missing')){
 $m=Clone $manifest;$p=Clone $base
 try{
  switch($case){
   'wrong-catalog'{$m.tuple.catalog='0'*64}
   'wrong-profile'{$m.tuple.hc='0'*64}
   'missing-native-source-checksum'{$m.nativePdb.sha256='0'*64}
   'missing-native-consumer'{$m.nativeConsumer.sha256='0'*64}
   'runtime-file-missing'{$m.members=@($m.members|Where-Object path -CNE 'runtime/YeManLightDeviceCore.dll')}
  }
  Save $manifestPath $m;$p.buildDelivery.sha256=Hash $manifestPath;$errorText=$null
  try{Assert-YmccLightingCatalogEvidence -Plan $p|Out-Null}catch{$errorText=$_.Exception.Message}
  Check ($null -ne $errorText) ('recomputed manifest cannot approve '+$case)
 }finally{[IO.File]::WriteAllBytes($manifestPath,$manifestBytes)}
}
Check ((Hash $manifestPath) -ceq $base.buildDelivery.sha256) 'original delivery manifest restored byte for byte'
foreach($case in @('success','locked-export','wrong-stage-helper','wrong-native-file','extra-write','unrelated-export-edit')){
 $p=Clone $base;$root=Join-Path $EvidenceRoot $case;$copy=Join-Path $root 'installation';$stage=Join-Path $root 'stage';New-Item -ItemType Directory -Path $copy,$stage -Force|Out-Null
 foreach($row in @($base.preserved)+@($base.changes)){$rel=$row.relative;$to=Join-Path $copy $rel;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $base.installRoot $rel) -Destination $to}
 foreach($row in $p.changes){$to=Join-Path $stage $row.sourceRelative;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $base.stageRoot $row.sourceRelative) -Destination $to}
 $p.installRoot=$copy;$p.stageRoot=$stage;$p.evidenceDir=Join-Path $root 'connection'
 foreach($row in $p.preserved){$row.sddl=(Get-Acl -LiteralPath (Join-Path $copy $row.relative)).Sddl}
 foreach($row in $p.changes){$row.beforeSddl=(Get-Acl -LiteralPath (Join-Path $copy $row.relative)).Sddl}
 if($case -eq 'wrong-stage-helper'){$p.changes[0].afterSha256='0'*64}
 if($case -eq 'extra-write'){$p.changes+=@{relative='YeManCC/extra.bin';sourceRelative='extra.bin';beforeSha256='0'*64;afterSha256='0'*64}}
 if($case -eq 'wrong-native-file'){[IO.File]::WriteAllText((Join-Path $copy 'YeManCC/YeManCC.exe'),'wrong native')}
 if($case -eq 'unrelated-export-edit'){$f=Join-Path $stage 'YeManCC/export-identity.json';$d=Get-Content -LiteralPath $f -Raw -Encoding UTF8|ConvertFrom-Json;$d.version='unrelated-change';Save $f $d;$p.changes[1].afterSha256=Hash $f}
 $saved=@(Get-ChildItem -LiteralPath $copy -Recurse -File|ForEach-Object {[pscustomobject]@{path=$_.FullName.Substring($copy.Length+1).Replace('\','/');sha256=(Hash $_.FullName);sddl=(Get-Acl -LiteralPath $_.FullName).Sddl}})
 $plan=Join-Path $root 'plan.json';Save $plan $p;$lock=$null
 try{
  if($case -eq 'locked-export'){$lock=[IO.File]::Open((Join-Path $copy 'YeManCC/export-identity.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$EnginePath;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $arguments=@('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',(Join-Path $PSScriptRoot 'connect-installed-lighting.ps1'),'-PlanPath',$plan)
  # Works under both PS7 and Windows PowerShell 5.1.
  $info.Arguments=($arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'}) -join ' '
  $info.EnvironmentVariables['PSModulePath']=(Join-Path (Split-Path -Parent $EnginePath) 'Modules')+';'+$env:PSModulePath
  $process=[Diagnostics.Process]::new();$process.StartInfo=$info;[void]$process.Start();$pidValue=$process.Id;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit(60000)){$process.Kill();throw 'Connector test timed out'};$exit=$process.ExitCode
  [IO.File]::WriteAllText((Join-Path $root 'run.log'),($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()),[Text.UTF8Encoding]::new($false));$process.Dispose()
 }finally{if($lock){$lock.Dispose()}}
 $receipt=Get-Content -LiteralPath (Join-Path $p.evidenceDir 'verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $executions.Add([pscustomobject]@{case=$case;pid=$pidValue;exitCode=$exit;naturalExit=$true;receipt=(Join-Path $p.evidenceDir 'verification.json');receiptSha256=(Hash (Join-Path $p.evidenceDir 'verification.json'))})
 if($case -eq 'success'){
  Check ($exit -eq 0 -and $receipt.passed -and @($receipt.changedFiles).Count -eq 2 -and $receipt.nativeAlreadyMatched -eq $true) 'actual two-file connector succeeds with matching native'
  foreach($row in $p.changes){Check ((Hash (Join-Path $copy $row.relative)) -ceq $row.afterSha256 -and (Hash (Join-Path $p.evidenceDir ('preimage/'+$row.relative))) -ceq $row.beforeSha256 -and (Get-Acl -LiteralPath (Join-Path $copy $row.relative)).Sddl -ceq $row.beforeSddl) ('true preimage and output ACL '+$row.relative)}
 }else{
  Check ($exit -eq 2 -and -not$receipt.passed) ('connector rejects '+$case)
  if($case -eq 'locked-export'){Check ($receipt.rollbackPerformed -eq $true -and @($receipt.changedFiles).Count -eq 0) 'real second-file lock triggers rollback of first replacement'}
  $same=$true;foreach($row in $saved){$f=Join-Path $copy $row.path;if((Hash $f) -cne $row.sha256 -or (Get-Acl -LiteralPath $f).Sddl -cne $row.sddl){$same=$false}}
  Check $same ('all initial file hashes and ACL restored or retained '+$case)
 }
 $unchanged=$true;foreach($row in $p.preserved){if($case -eq 'wrong-native-file' -and $row.relative -ceq 'YeManCC/YeManCC.exe'){continue};$f=Join-Path $copy $row.relative;if((Hash $f) -cne $row.sha256 -or (Get-Acl -LiteralPath $f).Sddl -cne $row.sddl){$unchanged=$false}}
 Check $unchanged ('protected product tuple and ACL retained '+$case)
}
# Preserve previous fixed-version semantics: one positive and a pin-refusal per kind.
$legacy=Get-Content -LiteralPath $LegacyV2PlanPath -Raw -Encoding UTF8|ConvertFrom-Json;$v2=Assert-YmccLightingCatalogEvidence -Plan $legacy
Check ($v2.contract -ceq 'lighting-catalog-v2') 'original v2 exact candidate still admitted'
$bad=Clone $legacy;$bad.newHelperSha256=$base.newHelperSha256;$errorText=$null;try{Assert-YmccLightingCatalogEvidence -Plan $bad|Out-Null}catch{$errorText=$_.Exception.Message};Check ($errorText -and $errorText.Contains('REVIEWED_ARTIFACT')) 'v2 cannot borrow new dynamic candidate'
. (Join-Path $PSScriptRoot '../ymcc_sim/global/lighting-component-continuity/New-ReviewedPlan.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$v3plan=New-YmccLightingReviewedPlan -ReviewedContractPath $ReviewedV3ContractPath -OutputPlanPath (Join-Path $EvidenceRoot 'v3-plan.json') -EvidenceRoot (Join-Path $EvidenceRoot 'v3-proof') -RepoRoot $repo
$v3=Assert-YmccLightingCatalogEvidence -Plan $v3plan;Check ($v3.contract -ceq 'lighting-catalog-v3') 'original v3 fixed archive still admitted'
$bad=Clone $v3plan;$bad.newHelperSha256=$base.newHelperSha256;$errorText=$null;try{Assert-YmccLightingCatalogEvidence -Plan $bad|Out-Null}catch{$errorText=$_.Exception.Message};Check ($errorText -and $errorText.Contains('REVIEWED_ARTIFACT')) 'v3 cannot borrow new dynamic candidate'
$result=[ordered]@{schemaVersion=1;status='PASS';checks=$checks.Count;cases=@($checks);executions=@($executions);engine=$EnginePath;planSha256=(Hash $PlanPath);actualCWritten=$false;actualGWritten=$false;nativeExecuted=$false;hardwareIo=$false;physicalAcceptance=$false}
Save (Join-Path $EvidenceRoot 'result.json') $result;Write-Output ('LIGHTING_BUILD_DELIVERY_TEST PASS '+$checks.Count)
