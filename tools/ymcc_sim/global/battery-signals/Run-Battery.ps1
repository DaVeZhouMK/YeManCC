<# Existing battery tests, real Controller/provider code with injected transports,
   and the real component consuming actual Controller JSON. No production Main. #>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $evidence){throw 'FRESH_BATTERY_EVIDENCE_REQUIRED'}
if($evidence.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'EVIDENCE_MUST_BE_OUTSIDE_SOURCE'}
New-Item -ItemType Directory -Path $evidence | Out-Null
$dotnet=(Get-Command dotnet.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$node=(Get-Command node.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$inputs=@('tools/ymcc_sim/global/battery-signals/Run-Battery.ps1','tools/ymcc_sim/global/battery-signals/NativeBoundary.cs','tools/ymcc_sim/global/battery-signals/ControllerBridge.cs','tools/ymcc_sim/global/battery-signals/extract-dispatch.mjs','tools/ymcc_sim/global/battery-signals/controller-proxy.mjs','tools/ymcc_sim/global/battery-signals/live-browser-cases.mjs','tools/ymcc_sim/global/battery-signals/BatteryHostSimulation.csproj','native/BatteryHostTests/Program.cs','native/BatteryHostTests/MsiFixture.cs','native/ProcessEfficiency.cs','native/battery_charge_client.h','native/battery_host_contract.h','tools/battery-charge-contract.mjs','tools/battery_charge_contract_selftest.mjs','tools/battery_charge_browser_selftest.mjs','src/bridge/batteryCharge.ts','src/bridge/uiLifecycle.ts','src/components/BatteryChargeSettings.vue','deps/ymcc-shared-runtime/bin/LibreHardwareMonitorLib.dll','deps/ymcc-shared-runtime/bin/RAMSPDToolkit-NDD.dll')
$inputs+=@('capabilities/HcDeviceCapabilityCatalog.cs','capabilities/HcDeviceCapabilityCatalog.generated.cs','capabilities/HcBatteryCapabilityCatalog.cs','capabilities/HcBatteryCapabilityCatalog.generated.cs','capabilities/hc-device-catalog.json','capabilities/hc-battery-policy.json','docs/battery-charge-capabilities.json','tools/battery-charge-generate-routes.mjs','tools/lib/hc_battery_catalog.mjs')
$inputs+=@(Get-ChildItem -LiteralPath (Join-Path $repo 'native/BatteryHost') -File | Where-Object {$_.Extension -in '.cs','.csproj','.json'} | ForEach-Object {'native/BatteryHost/'+$_.Name})
function Snapshot-Sources {
 $hashes=[ordered]@{}
 foreach($name in ($inputs|Sort-Object -Unique)){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $repo $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
 return $hashes
}
function Invoke-Owned([string]$Exe,[string[]]$Arguments,[string]$Name,[hashtable]$Extra=@{},[int]$Timeout=90){
 $stageDir=Join-Path $evidence $Name
 New-Item -ItemType Directory -Path $stageDir | Out-Null
 $temp=Join-Path $stageDir 'temp';$local=Join-Path $stageDir 'local';$roaming=Join-Path $stageDir 'roaming'
 New-Item -ItemType Directory -Path $temp,$local,$roaming | Out-Null
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $start.WorkingDirectory=$repo;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($item in $Arguments){$start.ArgumentList.Add($item)}
 $start.Environment['TEMP']=$temp;$start.Environment['TMP']=$temp;$start.Environment['LOCALAPPDATA']=$local;$start.Environment['APPDATA']=$roaming
 foreach($key in $Extra.Keys){$start.Environment[$key]=[string]$Extra[$key]}
 $child=$null;$forced=$false
 try{
  $child=[Diagnostics.Process]::Start($start);$birth=$child.StartTime.ToUniversalTime();[ordered]@{pid=$child.Id;creationTimeUtc=$birth.ToString('o');executable=$Exe;stage=$Name}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $stageDir 'process-start.json') -Encoding UTF8;$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew()
  while(-not $child.WaitForExit(250)){if($watch.Elapsed.TotalSeconds -gt $Timeout){if(-not $child.HasExited -and $child.StartTime.ToUniversalTime() -eq $birth){$child.Kill($true);$forced=$true;$child.WaitForExit(5000)|Out-Null};throw 'OWNED_BATTERY_TEST_TIMEOUT'}}
  $text=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText((Join-Path $stageDir 'stdout.log'),$text,[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $stageDir 'stderr.log'),$errorText,[Text.UTF8Encoding]::new($false))
  return [pscustomobject]@{exitCode=$child.ExitCode;naturalExit=-not $forced;pid=$child.Id;creationTimeUtc=$birth.ToString('o');forcedCleanup=$forced;stdout=$text;stage=$Name}
 }finally{if($child){if(-not $child.HasExited -and $child.StartTime.ToUniversalTime() -eq $birth){$child.Kill($true);$forced=$true;$child.WaitForExit(5000)|Out-Null};[ordered]@{pid=$child.Id;exited=$child.HasExited;forcedCleanup=$forced;exitCode=$(if($child.HasExited){$child.ExitCode}else{$null})}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $stageDir 'process-exit.json') -Encoding UTF8;$child.Dispose()}}
}
$before=Snapshot-Sources;$status='FAIL';$stage='build';$failure=$null;$cases=[Collections.Generic.List[object]]::new();$positive=$null;$negative=$null;$ui=$null;$passes=@();$executions=[Collections.Generic.List[object]]::new()
try{
 $build=Join-Path $evidence 'build';$feed=Join-Path $evidence 'empty-nuget-feed';New-Item -ItemType Directory -Path $feed | Out-Null
 $compiled=Invoke-Owned $dotnet @('build',(Join-Path $PSScriptRoot 'BatteryHostSimulation.csproj'),'-c','Release','-r','win-x64','--artifacts-path',$build,'--disable-build-servers','-p:NuGetAudit=false',('-p:RestoreSources='+$feed),'-v','quiet') 'build-process'
 $executions.Add($compiled)
 if($compiled.exitCode -ne 0){throw 'BATTERY_TEST_BUILD_FAILED'}
 $dll=Join-Path $build 'bin/BatteryHostSimulation/release_win-x64/BatteryHostTests.dll'
 if(-not(Test-Path -LiteralPath $dll -PathType Leaf)){throw 'BATTERY_TEST_DLL_MISSING'}
 $stage='controller';$positiveFile=Join-Path $evidence 'controller-boundary.json'
 $run=Invoke-Owned $dotnet @($dll) 'controller-process' @{YMCC_BATTERY_SIM_RECEIPT=$positiveFile;YMCC_BATTERY_SIM_GUARD_NEGATIVE='0'}
 $executions.Add($run)
 if($run.exitCode -ne 0 -or -not $run.naturalExit){throw 'BATTERY_CONTROLLER_TEST_FAILED'}
 $positive=Get-Content -LiteralPath $positiveFile -Raw -Encoding UTF8|ConvertFrom-Json
 $passes=@([regex]::Matches($run.stdout,'(?m)^PASS (.+)\r?$')|ForEach-Object {$_.Groups[1].Value.Trim()})
 $completed=[regex]::Match($run.stdout,'(?m)^(\d+) groups passed; fake providers only, no hardware writes\.\r?$')
 if($positive.status -ne 'PASS' -or $positive.nativeAttempts.Count -ne 0 -or -not $completed.Success -or $passes.Count -ne [int]$completed.Groups[1].Value -or $passes.Count -lt 30){throw 'BATTERY_CONTROLLER_COMPLETION_UNCONFIRMED'}
 $cases.Add(@{id='controller.original-tests';passed=$true;status='PASS';groups=$passes.Count;names=$passes})
 $stage='native-negative';$negativeFile=Join-Path $evidence 'native-guard-negative.json'
 $bad=Invoke-Owned $dotnet @($dll) 'native-negative-process' @{YMCC_BATTERY_SIM_RECEIPT=$negativeFile;YMCC_BATTERY_SIM_GUARD_NEGATIVE='1'}
 $executions.Add($bad);$negative=Get-Content -LiteralPath $negativeFile -Raw -Encoding UTF8|ConvertFrom-Json
 if($bad.exitCode -ne 2 -or $negative.status -ne 'FAIL' -or -not $negative.negativeProbeRejected -or $negative.nativeAttempts.Count -ne 1){throw 'BATTERY_NATIVE_GUARD_NEGATIVE_NOT_REJECTED'}
 $cases.Add(@{id='controller.native-import-rejected';passed=$true;status='PASS';observedChildExitCode=$bad.exitCode})
 $stage='contract';$contract=Invoke-Owned $node @((Join-Path $repo 'tools/battery_charge_contract_selftest.mjs')) 'contract-process'
 $executions.Add($contract);if($contract.exitCode -ne 0){throw 'BATTERY_SOURCE_CONTRACT_REJECTION_FAILED'}
 $cases.Add(@{id='contract.stale-provider-route-client-rejected';passed=$true;status='PASS'})
 $stage='dispatch-source';$dispatchSource=Join-Path $evidence 'generated/ProductionDispatch.g.cs'
 $extracted=Invoke-Owned $node @((Join-Path $PSScriptRoot 'extract-dispatch.mjs'),$dispatchSource,(Join-Path $evidence 'dispatch-source.json')) 'dispatch-extraction-process'
 $executions.Add($extracted);if($extracted.exitCode -ne 0){throw 'BATTERY_CURRENT_DISPATCH_EXTRACTION_FAILED'}
 $stage='bridge-build';$bridgeBuild=Join-Path $evidence 'bridge-build'
 $bridgeCompiled=Invoke-Owned $dotnet @('build',(Join-Path $PSScriptRoot 'BatteryHostSimulation.csproj'),'-c','Release','-r','win-x64','--artifacts-path',$bridgeBuild,'--disable-build-servers','-p:BatteryBridge=true',('-p:BatteryDispatchSource='+$dispatchSource),'-p:NuGetAudit=false',('-p:RestoreSources='+$feed),'-v','quiet') 'bridge-build-process'
 $executions.Add($bridgeCompiled);if($bridgeCompiled.exitCode -ne 0){throw 'BATTERY_LIVE_BRIDGE_BUILD_FAILED'}
 $bridgeDll=Join-Path $bridgeBuild 'bin/BatteryHostSimulation/release_win-x64/BatteryHostTests.dll'
 $stage='browser';$uiDir=Join-Path $evidence 'ui'
 $browser=Invoke-Owned $node @((Join-Path $repo 'tools/battery_charge_browser_selftest.mjs')) 'browser-process' @{YMCC_BATTERY_TEST_OUT=$uiDir;YMCC_BATTERY_FEEDBACK_FILE=$positiveFile;YMCC_BATTERY_BRIDGE_DLL=$bridgeDll;YMCC_BATTERY_TEST_DOTNET=$dotnet} 120
 $executions.Add($browser);$ui=Get-Content -LiteralPath (Join-Path $uiDir 'result.json') -Raw -Encoding UTF8|ConvertFrom-Json
 if($browser.exitCode -ne 0 -or -not $ui.ok -or -not $ui.ownServerClosed -or $ui.sourceChanges.Count -ne 0 -or $ui.liveController.status -ne 'PASS' -or $ui.liveController.cases.Count -lt 6 -or $ui.liveControllerProcess.status -ne 'PASS'){throw 'BATTERY_REAL_COMPONENT_REGRESSION_FAILED'}
 $cases.Add(@{id='bridge.actual-controller-feedback';passed=$true;status='PASS';consumer=$positive.controllerFeedback.consumer;input=$positive.controllerFeedback.input;before=$positive.controllerFeedback.before;after=$positive.controllerFeedback.after;uiCases=@($ui.cases)})
 $status='PASS'
}catch{$failure=[ordered]@{stage=$stage;code=$(if($_.Exception.Message -match '^[A-Z0-9_]+$'){$_.Exception.Message}else{'BATTERY_SIGNAL_REGRESSION_EXECUTION_FAILED'})}}
$after=Snapshot-Sources;$changes=@($before.Keys|Where-Object {$before[$_] -ne $after[$_]})
if($changes.Count){$status='FAIL';$failure=@{stage='source';code='BATTERY_SOURCE_CHANGED_DURING_RUN'}}
if($ui){foreach($entry in $ui.sourceHashes.PSObject.Properties){$before[$entry.Name]=$entry.Value}}
$record=[ordered]@{schemaVersion=1;kind='battery-signals-regression';status=$status;cases=@($cases);failure=$failure;sourceHashes=$before;sourceChanges=$changes;executedProductModules=@('native/BatteryHost/Controller.cs','native/BatteryHost/Policy.cs','native/BatteryHost/Providers.cs','native/BatteryHost/MsiWmi.cs','native/BatteryHost/MsiApProvider.cs','native/BatteryHost/RuntimeContract.cs','native/BatteryHost/TraceLog.cs','src/bridge/batteryCharge.ts','src/bridge/uiLifecycle.ts','src/components/BatteryChargeSettings.vue');summary=@{controllerGroups=$passes.Count;uiCases=$(if($ui){$ui.cases.Count}else{0});liveControllerCases=$(if($ui){$ui.liveController.cases.Count}else{0});rootCases=$cases.Count};processes=@($executions|Select-Object stage,pid,creationTimeUtc,exitCode,naturalExit,forcedCleanup);targetNativeAttempts=$(if($positive){@($positive.nativeAttempts)}else{@()});controllerFeedback=$(if($positive){$positive.controllerFeedback}else{$null});scope=@{providerTransports='injected fixtures from original tests';productionMainInvoked=$false;operatingSystemSleepPerformed=$false;physicalHardwareValidated=$false;installedApplicationMutated=$false;browserUsesActualControllerJson=$true;liveComponentUsesSameController=$true;nativeBatteryPipeExercised=$false;hostOwnershipAcquired=$false};executedOperations=$(if($ui){@($ui.executedOperations)}else{@()});operationEvidence=$(if($ui){@($ui.inputConsumerStateFeedback)}else{@()});compatibility=@{hcRuntimeUsed=$false;compiledCurrentBatterySources=$true;sourceContractChecked=$true;physicalReadbackNotClaimed=$true}}
$record|ConvertTo-Json -Depth 35|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
Write-Output ('BATTERY_SIGNALS '+$status+' controllerGroups='+$passes.Count+' uiCases='+$(if($ui){$ui.cases.Count}else{0}))
if($status -ne 'PASS'){exit 2}
exit 0