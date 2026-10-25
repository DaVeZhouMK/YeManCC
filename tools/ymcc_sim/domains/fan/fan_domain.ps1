<# Real dependency initialization plus existing mock lifecycle, in the existing simulator.
   No real-backend Host, device Open, firmware/EC writes or OS sleep are permitted. #>
[CmdletBinding()]
param(
 [ValidateSet('connect','plan')][string]$Mode='connect',
 [string]$RuntimeRoot='',
 [string]$SharedRuntimeRoot='',
 [string]$HostPayloadRoot='',
 [string]$EvidenceDir=''
)
$ErrorActionPreference='Stop'
$simRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$repo=Split-Path -Parent (Split-Path -Parent $simRoot)
. (Join-Path $simRoot 'lib\sim_core.ps1')
if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $repo 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'}
if(-not $SharedRuntimeRoot){$SharedRuntimeRoot=Join-Path $repo 'PowerControl\ymcc-shared-runtime'}
if(-not $HostPayloadRoot){$HostPayloadRoot=$env:YEMAN_SIM_PAYLOAD_ROOT}
if(-not $HostPayloadRoot){$HostPayloadRoot=Join-Path $repo 'PowerControl\fan-host'}
if(-not $EvidenceDir){$EvidenceDir=Join-Path (Get-SimDefaults).evidenceDir ('fan-connect-'+[guid]::NewGuid().ToString('N'))}
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Force -Path $EvidenceDir|Out-Null
$run=New-SimRun -Domain 'fan' -Mode $Mode -ModeKind 'offline-injection' -ToolPath $PSCommandPath -Notes @{realDllInitializationRequired=$true;nullBackendAloneCannotPass=$true;newEnvironmentRequired=$false}
$run.extra['acceptanceBoundary']='Real HC assemblies, audited derivative COM ABI with broken-library rejection, and all 13 manager constructors, followed by reviewed mocked fan lifecycle; real handheld firmware/RPM/EC acceptance is not claimed.'
$dependencyProfile=Get-Content -LiteralPath (Join-Path $repo 'deps\ymcc-device-core-runtime\profile.json') -Raw -Encoding UTF8|ConvertFrom-Json
$upstreamRuntimeRoot=Join-Path $repo $dependencyProfile.sourceDirectory
$phases=@(
 [ordered]@{id='runtime-integrity-positive-negative';script=(Join-Path $repo 'tools\hc-device-runtime\runtime-selftest.ps1');arguments=@('-ProjectRoot',$repo,'-RuntimeRoot',$RuntimeRoot,'-ScratchRoot',(Join-Path $EvidenceDir 'integrity'));receipt=(Join-Path $EvidenceDir 'integrity\verification.json')},
 [ordered]@{id='interop-abi-positive-negative';script=(Join-Path $repo 'tools\hc-application-dependency-trim\test-com-gate.ps1');arguments=@('-RuntimeRoot',$RuntimeRoot,'-UpstreamRuntimeRoot',$upstreamRuntimeRoot,'-KnownBadWasapi',(Join-Path $repo 'deps\ymcc-device-core-runtime\regression-fixtures\pre-abi-repair\NAudio.Wasapi.dll'),'-OutputDirectory',(Join-Path $EvidenceDir 'interop'));receipt=(Join-Path $EvidenceDir 'interop\result.json')},
 [ordered]@{id='sharpdx-callback-positive-negative';script=(Join-Path $repo 'tools\SharpDxCallbackSelfTest\Run-SelfTest.ps1');arguments=@('-RuntimeRoot',$RuntimeRoot,'-SharedRuntimeRoot',$SharedRuntimeRoot,'-UpstreamRuntimeRoot',$upstreamRuntimeRoot,'-KnownTrimmedRoot',(Join-Path $repo 'deps\ymcc-device-core-runtime\regression-fixtures\pre-abi-repair'),'-ExpectedManifestSha256',((Get-FileHash -LiteralPath (Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json') -Algorithm SHA256).Hash.ToLowerInvariant()),'-Scope','full','-OutputDirectory',(Join-Path $EvidenceDir 'sharpdx'));receipt=(Join-Path $EvidenceDir 'sharpdx\result.json')},
 [ordered]@{id='real-manager-initialization-positive-negative';script=(Join-Path $repo 'tools\HcManagerInitializationSelfTest\Run-SelfTest.ps1');arguments=@('-EvidenceRoot',(Join-Path $EvidenceDir 'manager-initialization'),'-RuntimeRoot',$RuntimeRoot,'-SharedRuntimeRoot',$SharedRuntimeRoot);receipt=(Join-Path $EvidenceDir 'manager-initialization\verification.json')},
 [ordered]@{id='fan-mock-open-control-close-return';script=(Join-Path $simRoot 'tests\fan_runtime_connection.ps1');arguments=@('-RuntimeRoot',$RuntimeRoot,'-SharedRuntimeRoot',$SharedRuntimeRoot,'-HostPayloadRoot',$HostPayloadRoot,'-OutDir',(Join-Path $EvidenceDir 'connection'));receipt=(Join-Path $EvidenceDir 'connection\verification.json')}
)
$run.extra['requiredPhases']=@($phases|ForEach-Object {$_.id})
$run.identity['runtimeManifest']=Get-SimFileIdentity (Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json')
$run.identity['host']=Get-SimFileIdentity (Join-Path $HostPayloadRoot 'YeManFanHost.dll')
$engine=Join-Path $PSHOME 'pwsh.exe'
if(-not(Test-Path -LiteralPath $engine)){ $engine=(Get-Command pwsh.exe -ErrorAction Stop).Source }
if($Mode -eq 'plan'){
 $run.extra['plan']=@($phases|ForEach-Object {[ordered]@{phase=$_.id;script=$_.script;receipt=$_.receipt}})
 Add-SimStatus -Run $run -Scope 'execution' -Status 'NOT_RUN' -Detail 'Plan only; neither initialization nor mock was executed.' -Required $true
}else{
 foreach($phase in $phases){
  $phaseLog=Join-Path $EvidenceDir ($phase.id+'.log')
  try{
   if(-not(Test-Path -LiteralPath $phase.script -PathType Leaf)){throw ('REQUIRED_PHASE_MISSING: '+$phase.id)}
   $beganUtc=[DateTime]::UtcNow
   $phaseArguments=@($phase.arguments)
   & $engine -NoProfile -NonInteractive -File $phase.script @phaseArguments *> $phaseLog
   $code=$LASTEXITCODE
   $receiptExists=Test-Path -LiteralPath $phase.receipt -PathType Leaf
   $fresh=$receiptExists -and (Get-Item -LiteralPath $phase.receipt).LastWriteTimeUtc -ge $beganUtc.AddSeconds(-1)
   $receipt=if($receiptExists){Get-Content -LiteralPath $phase.receipt -Raw -Encoding UTF8|ConvertFrom-Json}else{$null}
   $passed=$code -eq 0 -and $fresh -and $receipt.status -in @('PASS','passed')
   Add-SimCheck -Run $run -Form 'FAN-CONNECTION' -Name $phase.id -Ok $passed -Detail ('exit='+$code+' fresh='+$fresh+' receipt='+$phase.receipt)|Out-Null
   Add-SimStep $run ([ordered]@{phase=$phase.id;exitCode=$code;fresh=$fresh;receipt=$phase.receipt;log=$phaseLog;passed=$passed})
  }catch{
   Add-SimCheck -Run $run -Form 'FAN-CONNECTION' -Name $phase.id -Ok $false -Detail $_.Exception.Message|Out-Null
  }
 }
}
Add-SimStatus -Run $run -Scope 'physical-handheld-firmware-rpm-ec' -Status 'NOT_RUN' -Detail 'Desktop software simulation cannot prove handheld firmware or physical fan control.' -Required $false
$exitCode=Complete-SimRun -Run $run -EvidenceDir $EvidenceDir
$receiptPath=Join-Path $EvidenceDir 'verification.json'
[ordered]@{status=$(if($exitCode -eq 0){'PASS'}else{'FAIL'});exitCode=$exitCode;runId=$run.runId;checks=@($run.checks);steps=@($run.steps);realDllInitializationRequired=$true;hardwareAcceptance=$false;existingEnvironment=$true}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $receiptPath -Encoding UTF8
exit $exitCode