<# Apply a reviewed runtime/Host plan only after the existing full simulator gate passes.
   Uses the production Host admission objects; no elevation, app restart, driver or hardware operation. #>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference='Stop'
$planBytes=[IO.File]::ReadAllBytes($PlanPath)
$plan=[Text.Encoding]::UTF8.GetString($planBytes).TrimStart([char]0xFEFF)|ConvertFrom-Json
$install=[IO.Path]::GetFullPath($plan.installRoot).TrimEnd('\')
$stage=[IO.Path]::GetFullPath($plan.stageRoot).TrimEnd('\')
$evidence=[IO.Path]::GetFullPath($plan.evidenceDir)
if(Test-Path -LiteralPath $evidence){throw 'Fresh connection evidence directory required'}
New-Item -ItemType Directory -Path $evidence | Out-Null
$report=[ordered]@{status='FAIL';passed=$false;planSha256=(Get-FileHash -LiteralPath $PlanPath -Algorithm SHA256).Hash.ToLowerInvariant();startedUtc=[DateTime]::UtcNow.ToString('o');applicationRestarted=$false;nativeExecutableReplaced=$false;frontendReplaced=$false;hardwareActionPerformed=$false;forceUsed=$false;rollbackPerformed=$false;changedFiles=@()}
$hostOnlyV2=$plan.kind -ceq 'host-only-v2'
$hostOnlyV3=$plan.kind -ceq 'host-only-v3'
$hostCapabilityRequired=$hostOnlyV2 -or $hostOnlyV3
$hostOnly=$hostCapabilityRequired -or $plan.kind -eq 'host-only-v1'
$leases=@();$applied=[Collections.Generic.List[object]]::new()
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function SafePath([string]$Root,[string]$Relative){
 if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'Unsafe connection path'}
 $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
 if(-not $full.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Connection path escaped its root'}
 $cursor=$full
 while($cursor.Length -ge $Root.Length){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse connection path refused'}};if($cursor -ieq $Root){break};$cursor=Split-Path -Parent $cursor}
 return $full
}
function AtomicReplace([string]$From,[string]$To){
 $temp=$To+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp'
 try{
  Copy-Item -LiteralPath $From -Destination $temp
  [IO.File]::Replace($temp,$To,[Management.Automation.Language.NullString]::Value,$true)
 }finally{
  if(Test-Path -LiteralPath $temp -PathType Leaf){Remove-Item -LiteralPath $temp -ErrorAction Stop}
 }
}
function Assert-HistoricalHostTuple {
 # Only this independently archived v1 target is retained; the plan cannot declare a new candidate legacy.
 $legacyPlanPins=@{
  newHostDllSha256='7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f'
  newBackendDllSha256='f8fdbd516d2a002fa65e1cad5bc02405f16ec7b04208d4f6a8c2d26f330dea09'
  newHostManifestSha256='94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6'
  newRuntimeManifestSha256='6389f7a22b1e294ebaf6c75b05b28bb9839abc7459be0d8898985de27dfe47b8'
  preservedNativeSha256='9e9e31d88aad4fa537e1453e1b15fd9c5c12f28fdf854849f0faa7e8ebd8f50a'
 }
 foreach($key in $legacyPlanPins.Keys){if($plan.$key -cne $legacyPlanPins[$key]){throw 'HOST_ADMISSION_V2_REQUIRED: candidate is outside the fixed historical Host/runtime/native tuple'}}
 $legacyFiles=@{
  'PowerControl/fan-host-v2/YeManFanHost.payload.json'='94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6'
  'PowerControl/fan-host-v2/install-fan-host-payload.ps1'='5a6d68f5ae5573f78cc77f147b295af802cc757d1855d0996239b14af6f3fbdf'
  'PowerControl/fan-host-v2/run-emergency-fan-restore.ps1'='06084c19ac912056b6f190a77486021ea33ab517a5f482b8c8f09ec258a600b6'
  'PowerControl/fan-host-v2/YeManFanBackend.dll'='f8fdbd516d2a002fa65e1cad5bc02405f16ec7b04208d4f6a8c2d26f330dea09'
  'PowerControl/fan-host-v2/YeManFanHost.authorization.md'='6b5f652e34bc19d8b6c1183d9d60c176ccba300067d4f30f6e5bd2c6b564db25'
  'PowerControl/fan-host-v2/YeManFanHost.deps.json'='63e2749627e9f623ab0864483c231588e593e41d5637389a4f5c30e8fcd03fec'
  'PowerControl/fan-host-v2/YeManFanHost.dll'='7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f'
  'PowerControl/fan-host-v2/YeManFanHost.exe'='675b2a3f330852cc265acf449d27e3bf39cda23c628047560843dcebfa2c74ab'
  'PowerControl/fan-host-v2/YeManFanHost.json'='b2316017b344c11ed08556a0f6932a2ae6ac6f5e04bbe188d9a89f7596be362e'
  'PowerControl/fan-host-v2/YeManFanHost.runtimeconfig.json'='96bc2946dff8790400d652ec7f8de7bb043e954980d59470c112788b68a1fbd3'
  'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.runtime.json'='6389f7a22b1e294ebaf6c75b05b28bb9839abc7459be0d8898985de27dfe47b8'
  'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll'='08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'
 }
 foreach($relative in $legacyFiles.Keys){
  $path=SafePath $stage $relative
  if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Hash $path) -cne $legacyFiles[$relative]){throw ('HOST_ADMISSION_LEGACY_TUPLE_MISMATCH: '+$relative)}
 }
}
function CheckSourcesAndPreserved {
 if(-not $hostOnly){return}
 $compiledRoot=if($hostOnlyV3){[IO.Path]::GetFullPath($plan.componentSourceRoot).TrimEnd('\')}else{$plan.repoRoot}
 foreach($record in $plan.compiledInputs){if((Hash (SafePath $compiledRoot $record.path)) -cne $record.sha256){throw ('Host compiled input changed: '+$record.path)}}
 foreach($record in $plan.preserved){if((Hash (SafePath $install $record.relative)) -cne $record.sha256){throw ('Preserved component changed: '+$record.relative)}}
}
function NoConsumers {
 if(@(Get-Process -Name YeManFanHost,YeManLightSetter -ErrorAction SilentlyContinue).Count){throw 'Runtime consumer is active; no files changed under a live consumer'}
}
try{
 if($plan.kind -isnot [string] -or $plan.kind -cnotin @('host-only-v1','host-only-v2','host-only-v3')){throw 'HOST_ADMISSION_UNKNOWN_KIND: an explicit reviewed Host-only contract is required'}
 if(-not $hostCapabilityRequired){Assert-HistoricalHostTuple;$report.admissionContract='fixed-historical-tuple-v1'}
 if($hostCapabilityRequired){
  . (Join-Path $PSScriptRoot 'verify-host-capability-evidence.ps1')
  $report.capabilities=Assert-YmccHostCapabilityEvidence -Plan $plan
 }
 $gates=@(Get-ChildItem -LiteralPath $plan.fullGateDirectory -Filter 'run-all-*.json' -File)
 if($gates.Count -ne 1){throw 'Exactly one completed full simulator gate required'}
 $gate=Get-Content -LiteralPath $gates[0].FullName -Raw -Encoding UTF8|ConvertFrom-Json
 if($hostOnlyV3){
  . (Join-Path $PSScriptRoot 'verify-host-nine-source.ps1')
  $report.nineSourceContinuity=Assert-YmccHostCatalogNineSource -Plan $plan -GatePath $gates[0].FullName
 }
 $expected=@('fan_connection','tool_gate','invariants','extremes','regressions','gyro_core','signals','api_contract','cycle')|Sort-Object
 $actual=@($gate.extra.suites|ForEach-Object {$_.id})|Sort-Object
 if($gate.verdict -ne 'ALL_PASS' -or $gate.exitCode -ne 0 -or $actual.Count -ne 9 -or (Compare-Object $expected $actual) -or @($gate.extra.suites|Where-Object {$_.exitCode -ne 0}).Count){throw 'Full simulator gate did not pass all nine required suites'}
 $hostProof=Get-Content -LiteralPath $plan.hostSelfTest -Raw -Encoding UTF8|ConvertFrom-Json
 if($hostProof.status -ne 'PASS' -or $hostProof.processExitCode -ne 0 -or $hostProof.checksPassed -lt 400 -or $hostProof.hostDllSha256 -ne $plan.newHostDllSha256){throw 'Actual rebuilt Host full self-test missing or mismatched'}
 if($gate.realSleepPerformed -ne $false -or $gate.hardwareActionPerformed -ne $false){throw 'Full gate was outside desktop software scope'}
 $fanSuite=@($gate.extra.suites|Where-Object {$_.id -eq 'fan_connection'})[0]
 $argList=@($fanSuite.args);$evidenceIndex=[Array]::IndexOf($argList,'-EvidenceDir')
 if($evidenceIndex -lt 0 -or $evidenceIndex+1 -ge $argList.Count){throw 'Fan input identity evidence missing'}
 $fanEvidence=[IO.Path]::GetFullPath($argList[$evidenceIndex+1])
 if(-not $fanEvidence.StartsWith([IO.Path]::GetFullPath($plan.fullGateDirectory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fan evidence is outside the admitted full gate'}
 $fanRuns=@(Get-ChildItem -LiteralPath $fanEvidence -File -Filter 'sim-fan-*.json')
 if($fanRuns.Count -ne 1){throw 'Exact fan input identity receipt required'}
 $fanRun=Get-Content -LiteralPath $fanRuns[0].FullName -Raw -Encoding UTF8|ConvertFrom-Json
 if($fanRun.verdict -ne 'ALL_PASS' -or $fanRun.counts.checksTotal -ne 5 -or $fanRun.identity.host.sha256 -ne $plan.newHostDllSha256 -or $fanRun.identity.runtimeManifest.sha256 -ne $plan.newRuntimeManifestSha256){throw 'Full gate tested different Host/runtime inputs'}
 if((Hash (Join-Path $stage 'PowerControl\fan-host-v2\YeManFanHost.dll')) -ne $plan.newHostDllSha256){throw 'Staged Host differs from tested input'}
 if($hostOnly){
  if(@($plan.compiledInputs).Count -lt 26 -or @($plan.compiledInputs|Where-Object {$_.path -eq 'FanLab/real-host/MsiFanDeviceLifecycle.cs'}).Count -ne 1){throw 'Host compiled lifecycle provenance is missing'}
  if((Hash $plan.openIsolationEvidence) -cne $plan.openIsolationEvidenceSha256){throw 'Host Open isolation evidence changed'}
  $special=Get-Content -LiteralPath $plan.openIsolationEvidence -Raw -Encoding UTF8|ConvertFrom-Json
  if($special.status -ne 'PASS' -or $special.candidateHostSha256 -cne $plan.newHostDllSha256 -or $special.hardwareCalls -ne 0 -or $special.nativeAttempts -ne 0 -or $special.hostMainStarted -ne $false -or $special.sourceChanges.Count -ne 0 -or @($special.cases).Count -lt 25 -or @($special.cases|Where-Object {$_.status -ne 'PASS'}).Count){throw 'Actual Host fan-only Open isolation gate missing or mismatched'}
  foreach($name in @('candidate.open-repeat-close-reopen.ClawA1M','candidate.open-repeat-close-reopen.ClawA2VM','candidate.open-repeat-close-reopen.ClawBZ2EM','candidate.open-repeat-close-reopen.ClawCG3EM','legacy.shift-read-failure-still-writes','candidate.shift-read-failure-never-enters-shift','candidate.openlib-owned-cleanup-retry')){
   if(@($special.cases|Where-Object {$_.id -ceq $name -and $_.status -eq 'PASS'}).Count -ne 1){throw ('Missing Host lifecycle behavior: '+$name)}
  }
  if((Hash (SafePath $stage 'PowerControl/fan-host-v2/YeManFanBackend.dll')) -cne $plan.newBackendDllSha256 -or (Hash (SafePath $stage 'PowerControl/fan-host-v2/YeManFanHost.payload.json')) -cne $plan.newHostManifestSha256){throw 'Staged Host backend or manifest differs from the plan'}
  CheckSourcesAndPreserved
  if($hostCapabilityRequired){
   . (Join-Path $PSScriptRoot 'verify-host-capability-evidence.ps1')
   $report.capabilities=Assert-YmccHostCapabilityEvidence -Plan $plan
  }
  . (Join-Path $PSScriptRoot 'verify-installed-host-binding.ps1')
  $stageBinding=Assert-YmccInstalledHostBinding -Root $stage -ExpectedHostSha256 $plan.newHostDllSha256 -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256
  $allowed=@('PowerControl/fan-host-v2/YeManFanHost.dll','PowerControl/fan-host-v2/YeManFanBackend.dll','PowerControl/fan-host-v2/YeManFanHost.authorization.md','PowerControl/fan-host-v2/YeManFanHost.payload.json','YeManCC/update-manifest.json','YeManCC/export-identity.json')|Sort-Object
  $planned=@($plan.changes|ForEach-Object {$_.relative.Replace('\','/')})|Sort-Object
  if($planned.Count -ne 6 -or (Compare-Object $allowed $planned)){throw 'Host-only plan exceeds the exact six-file scope'}
 }else{
 $allowed=@(($plan.runtimeRelative+'/SharpDX.dll'),($plan.runtimeRelative+'/SharpDX.DirectInput.dll'),($plan.runtimeRelative+'/SharpDX.Direct3D9.dll'),($plan.runtimeRelative+'/HandheldCompanion.runtime.json'),'PowerControl/fan-host-v2/YeManFanHost.dll','PowerControl/fan-host-v2/YeManFanBackend.dll','PowerControl/fan-host-v2/YeManFanHost.authorization.md','PowerControl/fan-host-v2/YeManFanHost.payload.json','YeManCC/update-manifest.json','YeManCC/export-identity.json')|Sort-Object
 $planned=@($plan.changes|ForEach-Object {$_.relative.Replace('\','/')})|Sort-Object
 if($planned.Count -ne 10 -or (Compare-Object $allowed $planned)){throw 'Connection plan is outside the reviewed component scope'}
 }
 $report.gate=$gates[0].FullName
 if((Hash (Join-Path $install 'YeManCC\YeManCC.exe')) -ne $plan.preservedNativeSha256){throw 'Installed native preview changed; preserve it and re-plan'}
 NoConsumers
 foreach($name in @('Global\YeManFanHost-v2','Local\YeManFanHost-v2')){
  $created=$false;$lease=[Threading.Mutex]::new($false,$name,[ref]$created)
  if(-not $created){$lease.Dispose();throw 'Host lifetime admission already exists; refusing concurrent replacement'}
  $leases+=,$lease
 }
 NoConsumers
 if(-not $hostOnly -and @($plan.changes).Count -ne 10){throw 'Expected exactly eight component files and two metadata files'}
 $resolved=@()
 foreach($change in $plan.changes){
  $source=SafePath $stage $change.sourceRelative
  $target=SafePath $install $change.relative
  if((Hash $source) -ne $change.afterSha256 -or (Hash $target) -ne $change.beforeSha256){throw ('Connection input/preimage drift: '+$change.relative)}
  $backup=SafePath $evidence ('preimage\'+$change.relative)
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup)|Out-Null
  Copy-Item -LiteralPath $target -Destination $backup
  $resolved+=@{source=$source;target=$target;backup=$backup;relative=$change.relative;beforeSha256=$change.beforeSha256;afterSha256=$change.afterSha256}
 }
 if($hostCapabilityRequired){$report.capabilities=Assert-YmccHostCapabilityEvidence -Plan $plan}else{Assert-HistoricalHostTuple}
 foreach($change in $resolved){
  NoConsumers
  if((Hash $change.target) -ne $change.beforeSha256){throw ('Connection preimage changed during apply: '+$change.relative)}
  AtomicReplace $change.source $change.target
  $applied.Add($change)
  if((Hash $change.target) -ne $change.afterSha256){throw ('Connection readback failed: '+$change.relative)}
 }
 $runtimeRoot=Join-Path $install $plan.runtimeRelative
 $hostRoot=Join-Path $install 'PowerControl\fan-host-v2'
 if((Hash (Join-Path $runtimeRoot 'HandheldCompanion.runtime.json')) -ne $plan.newRuntimeManifestSha256 -or (Hash (Join-Path $hostRoot 'YeManFanHost.payload.json')) -ne $plan.newHostManifestSha256){throw 'Connected manifest identity mismatch'}
 foreach($pair in @(@($runtimeRoot,'HandheldCompanion.runtime.json'),@($hostRoot,'YeManFanHost.payload.json'))){
  $manifest=Get-Content -LiteralPath (Join-Path $pair[0] $pair[1]) -Raw -Encoding UTF8|ConvertFrom-Json
  foreach($row in $manifest.files){if((Hash (SafePath $pair[0] $row.path)) -ne $row.sha256){throw ('Connected member mismatch: '+$row.path)}}
 }
 if((Hash (Join-Path $install 'YeManCC\YeManCC.exe')) -ne $plan.preservedNativeSha256){throw 'Unrelated native preview changed during connection'}
 if((Hash (Join-Path $hostRoot 'YeManFanHost.dll')) -ne $plan.newHostDllSha256){throw 'Connected Host is not the tested input'}
 if(-not $hostCapabilityRequired){Assert-HistoricalHostTuple}
 if($hostOnly){
  CheckSourcesAndPreserved
  if($hostCapabilityRequired){$report.capabilities=Assert-YmccHostCapabilityEvidence -Plan $plan}
  $report.binding=Assert-YmccInstalledHostBinding -Root $install -ExpectedHostSha256 $plan.newHostDllSha256 -ExpectedRuntimeManifestSha256 $plan.newRuntimeManifestSha256
  $export=Get-Content -LiteralPath (SafePath $install 'YeManCC/export-identity.json') -Raw -Encoding UTF8|ConvertFrom-Json
  foreach($change in $resolved|Where-Object {$_.relative -ne 'YeManCC/export-identity.json'}){
   $records=@($export.payload|Where-Object {$_.path -ceq $change.relative})
   if($records.Count -ne 1 -or $records[0].sha256 -cne $change.afterSha256 -or $records[0].bytes -ne (Get-Item -LiteralPath $change.target).Length){throw ('Host export row mismatch: '+$change.relative)}
  }
 }
 $report.changedFiles=@($applied|ForEach-Object {$_.relative});$report.status='PASS';$report.passed=$true
 $report.runtimeManifestSha256=$plan.newRuntimeManifestSha256;$report.hostManifestSha256=$plan.newHostManifestSha256;$report.hostDllSha256=$plan.newHostDllSha256;$report.preservedNativeSha256=$plan.preservedNativeSha256
 $report.exportInventoryScope='Changed fan/runtime components and update binding only; unrelated active preview inventory is preserved and not claimed freshly verified.'
}catch{
 $report.error=$_.Exception.Message
 for($i=$applied.Count-1;$i -ge 0;$i--){$change=$applied[$i];try{if((Hash $change.target) -eq $change.afterSha256){AtomicReplace $change.backup $change.target;if((Hash $change.target) -ne $change.beforeSha256){throw 'Rollback readback mismatch'}}else{$report.rollbackConflict=$change.relative}}catch{$report.rollbackError=$_.Exception.Message}}
 $report.rollbackPerformed=$applied.Count -gt 0
}finally{
 foreach($lease in $leases){$lease.Dispose()}
 $report.finishedUtc=[DateTime]::UtcNow.ToString('o')
 [IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($report|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
}
Write-Output ('INSTALLED_COMPONENT_CONNECTION '+$report.status)
if(-not $report.passed){exit 2}
exit 0
