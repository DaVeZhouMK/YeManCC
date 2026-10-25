[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$EnginePath,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$base=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
if(Test-Path -LiteralPath $EvidenceRoot){throw 'Fresh lighting test evidence required'}
New-Item -ItemType Directory -Path $EvidenceRoot|Out-Null
. (Join-Path $PSScriptRoot 'verify-lighting-catalog-evidence.ps1')
$checks=[Collections.Generic.List[object]]::new();$executions=[Collections.Generic.List[object]]::new()
function Check([bool]$Value,[string]$Name){$checks.Add([pscustomobject]@{name=$Name;passed=$Value});if(-not $Value){throw ('TEST_FAIL '+$Name)}}
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$File,$Object){[IO.File]::WriteAllText($File,($Object|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))}
function Clone{return $base|ConvertTo-Json -Depth 60|ConvertFrom-Json}
$positive=Assert-YmccLightingCatalogEvidence -Plan $base
Check ($positive.status -eq 'PASS' -and $positive.actualCatalogChecks -eq 549) 'exact candidate admitted with actual DLL/source evidence'
function RejectPlan([string]$Name,[scriptblock]$Mutate,[string]$Expected){
 $p=Clone;& $Mutate $p
 $observed=$null;try{Assert-YmccLightingCatalogEvidence -Plan $p|Out-Null}catch{$observed=$_.Exception.Message}
 Check ($null -ne $observed -and $observed.Contains($Expected)) ('admission refuses '+$Name)
}
RejectPlan 'missing kind' {param($p)$p.PSObject.Properties.Remove('kind')} 'kind'
RejectPlan 'downgrade kind' {param($p)$p.kind='lighting-v1'} 'ADMISSION_KIND'
RejectPlan 'old helper' {param($p)$p.newHelperSha256='e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf'} 'REVIEWED_ARTIFACT'
RejectPlan 'different core' {param($p)$p.preservedCoreSha256='0'*64} 'REVIEWED_ARTIFACT'
RejectPlan 'missing lifecycle Compile' {param($p)$p.compiledInputs=@($p.compiledInputs|Where-Object path -CNE 'FanLab/LightSetter/OneXLightingSession.cs')} 'COMPILED_SET'
RejectPlan 'changed catalog Compile' {param($p)($p.compiledInputs|Where-Object path -CEQ 'capabilities/HcLightingCapabilityCatalog.cs').sha256='0'*64} 'COMPILED_BINDING'
RejectPlan 'missing actual binary evidence' {param($p)$p.lightingEvidence.PSObject.Properties.Remove('catalog')} 'catalog'
RejectPlan 'retagged source receipt' {param($p)$p.lightingEvidence.catalog.sha256='0'*64} 'PROOF_REFERENCE'
RejectPlan 'source is another root' {param($p)$p.componentSourceRoot=$EvidenceRoot} 'SOURCE_MISSING'
foreach($case in @('success','locked-export','wrong-stage-helper','extra-write','missing-preserved-core','unrelated-export-edit')){
 $p=Clone;$copy=Join-Path $EvidenceRoot ($case+'/installation');New-Item -ItemType Directory -Path $copy -Force|Out-Null
 foreach($record in @($p.preserved)+@($p.changes)){
  $relative=$record.relative;$to=Join-Path $copy $relative;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null
  $from=Join-Path $base.installRoot $relative
  if($record.PSObject.Properties['beforeSha256'] -and $base.kind -ceq 'lighting-catalog-v3'){
   $from=Join-Path $base.testPreimageRoot $relative
   if(-not (Test-Path -LiteralPath $from -PathType Leaf) -or (Hash $from) -cne $record.beforeSha256){throw ('Reviewed portable preimage unavailable: '+$relative)}
  }
  elseif($record.PSObject.Properties['beforeSha256'] -and (Hash $from) -cne $record.beforeSha256){
   $from=Join-Path $base.evidenceDir ('preimage/'+$relative)
   if(-not (Test-Path -LiteralPath $from -PathType Leaf) -or (Hash $from) -cne $record.beforeSha256){throw ('Exact saved test preimage unavailable: '+$relative)}
  }
  Copy-Item -LiteralPath $from -Destination $to
 }
 $preimagesMatch=$true;foreach($change in $base.changes){if((Hash (Join-Path $copy $change.relative)) -cne $change.beforeSha256){$preimagesMatch=$false}}
 Check $preimagesMatch ('exact preimages replayed after installation '+$case)
 $p.installRoot=$copy;$p.evidenceDir=Join-Path $EvidenceRoot ($case+'/connection')
 foreach($record in $p.preserved){$record|Add-Member NoteProperty sddl (Get-Acl -LiteralPath (Join-Path $copy $record.relative)).Sddl -Force}
 foreach($record in $p.changes){$record|Add-Member NoteProperty beforeSddl (Get-Acl -LiteralPath (Join-Path $copy $record.relative)).Sddl -Force}
 if($case -eq 'wrong-stage-helper'){$p.changes[0].afterSha256='0'*64}
 if($case -eq 'extra-write'){$p.changes+=@{relative='YeManCC/YeManCC.exe';sourceRelative='YeManCC/YeManCC.exe';beforeSha256=$p.preservedNativeSha256;afterSha256=$p.preservedNativeSha256}}
 if($case -eq 'missing-preserved-core'){$p.preserved=@($p.preserved|Where-Object relative -CNE 'PowerControl/light-setter/YeManLightDeviceCore.dll')}
 if($case -eq 'unrelated-export-edit'){
  $stage=Join-Path $EvidenceRoot ($case+'/stage');foreach($change in $p.changes){$to=Join-Path $stage $change.relative;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $base.stageRoot $change.relative) -Destination $to}
  $p.stageRoot=$stage;$file=Join-Path $stage 'YeManCC/export-identity.json';$j=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json;$j.payload[0].sha256='0'*64;Save $file $j;$p.changes[1].afterSha256=Hash $file
 }
 $planPath=Join-Path $EvidenceRoot ($case+'/plan.json');Save $planPath $p
 $lock=$null
 try{
  if($case -eq 'locked-export'){$lock=[IO.File]::Open((Join-Path $copy 'YeManCC/export-identity.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
  & $EnginePath -NoProfile -ExecutionPolicy RemoteSigned -File (Join-Path $PSScriptRoot 'connect-installed-lighting.ps1') -PlanPath $planPath *> (Join-Path $EvidenceRoot ($case+'/run.log'))
  $code=$LASTEXITCODE
 }finally{if($null -ne $lock){$lock.Dispose()}}
 $proofFile=Join-Path $p.evidenceDir 'verification.json';$proof=Get-Content -LiteralPath $proofFile -Raw -Encoding UTF8|ConvertFrom-Json
 $executions.Add([pscustomobject]@{case=$case;exitCode=$code;proof=$proofFile;proofSha256=(Hash $proofFile)})
 if($case -eq 'success'){
  Check ($code -eq 0 -and $proof.passed -and $proof.truePreimages -eq 2) 'copied installation applies exact two-file transaction'
  foreach($change in $p.changes){Check ((Hash (Join-Path $copy $change.relative)) -ceq $change.afterSha256) ('successful readback '+$change.relative);Check ((Hash (Join-Path $p.evidenceDir ('preimage/'+$change.relative))) -ceq $change.beforeSha256) ('true backup '+$change.relative)}
 }else{
  Check ($code -eq 2 -and $proof.passed -eq $false) ('transaction refuses '+$case)
  if($case -eq 'locked-export'){Check ($proof.rollbackPerformed -eq $true -and -not $proof.rollbackConflict -and -not $proof.rollbackError) 'second-file lock rolls helper back'}
  else{Check (-not $proof.rollbackPerformed -and @($proof.changedFiles).Count -eq 0) ('refusal precedes replacement '+$case)}
  foreach($change in @($base.changes)){Check ((Hash (Join-Path $copy $change.relative)) -ceq $change.beforeSha256) ('original retained '+$case+' '+$change.relative)}
 }
 $preservedOk=$true;foreach($record in $p.preserved){$file=Join-Path $copy $record.relative;if((Hash $file) -cne $record.sha256 -or (Get-Acl -LiteralPath $file).Sddl -cne $record.sddl){$preservedOk=$false}}
 Check $preservedOk ('preserved files and ACLs '+$case)
 $changedAcl=$true;foreach($change in $base.changes){$expected=@($p.changes|Where-Object relative -CEQ $change.relative)[0].beforeSddl;if((Get-Acl -LiteralPath (Join-Path $copy $change.relative)).Sddl -cne $expected){$changedAcl=$false}}
 Check $changedAcl ('target ACLs retained '+$case)
 Check (@(Get-ChildItem -LiteralPath $copy -Filter '*.ymcc-*.tmp' -Recurse -File).Count -eq 0) ('temporary files cleaned '+$case)
}
$result=[ordered]@{status='PASS';checks=$checks.Count;cases=$checks;executions=$executions;actualInstallationWritten=$false;hardwareIo=$false;engine=$EnginePath;planSha256=(Hash $PlanPath);sourceFiles=@(@('Test-LightingConnection.ps1','connect-installed-lighting.ps1','verify-lighting-catalog-evidence.ps1')+@('verify-lighting-portable-evidence.ps1'|Where-Object {$base.kind -ceq 'lighting-catalog-v3'})|ForEach-Object {$f=Join-Path $PSScriptRoot $_;[pscustomobject]@{path=$f;sha256=(Hash $f)}})}
Save (Join-Path $EvidenceRoot 'verification.json') $result
Write-Output ('LIGHTING_TRANSACTION_TESTS PASS '+$checks.Count)