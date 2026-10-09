[CmdletBinding()]
param([string]$CollectorPath='',[string]$OutputPath='')
$ErrorActionPreference='Stop'
if (-not $CollectorPath) { $CollectorPath=Join-Path $PSScriptRoot '..\Collect-YMCC-CPU.ps1' }
$CollectorPath=[IO.Path]::GetFullPath($CollectorPath)
$validation=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\validation'))
if (-not $OutputPath) { $OutputPath=Join-Path $validation ('collector-validation-ps{0}-20261004.json' -f $PSVersionTable.PSVersion.Major) }
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetDirectoryName($OutputPath) -ine $validation -or [IO.Path]::GetFileName($OutputPath) -notmatch '^collector-validation[^\\/]*\.json$') { throw 'VALIDATION_OUTPUT_WRITESET_VIOLATION' }
$checks=New-Object Collections.ArrayList; $cases=New-Object Collections.ArrayList
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($CollectorPath,[ref]$tokens,[ref]$errors)
$parseErrorCount=@($errors).Count
$testTokens=$null; $testErrors=$null
[void][Management.Automation.Language.Parser]::ParseFile($PSCommandPath,[ref]$testTokens,[ref]$testErrors)
$testParseErrorCount=@($testErrors).Count
if ($parseErrorCount -or $testParseErrorCount) { throw 'PARSER_VALIDATION_FAILED' }
$source=[IO.File]::ReadAllText($CollectorPath,[Text.Encoding]::UTF8)
$fns=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false))
# Import actual AST helper definitions, NOT the live capture dispatcher.
foreach ($fn in $fns) { . ([scriptblock]::Create($fn.Extent.Text)) }
$target='C:\ScopeFixture\Install-A\YeManCC.exe'; $foreign='D:\ScopeFixture\Install-B\YeManCC.exe'
$t0='2026-10-04T00:00:00.0000000Z'; $t1='2026-10-04T00:00:01.0000000Z'; $t2='2026-10-04T00:00:02.0000000Z'
function Assert([bool]$Condition,[string]$Name) {
  [void]$script:checks.Add([pscustomobject]@{name=$Name;passed=$Condition})
  if (-not $Condition) { throw ('ASSERTION_FAILED: '+$Name) }
}
function Near([object]$A,[double]$B) { return ($null -ne $A -and [math]::Abs([double]$A-$B) -lt .000001) }
function Throws([scriptblock]$Body) { try { & $Body | Out-Null; return $false } catch { return $true } }
function New-Fixture([int]$Id,[int]$Parent,[string]$Name,[string]$Path,[string]$Cmd,[string]$Created) {
  return [pscustomobject]@{pid=$Id;ppid=$Parent;name=$Name;path=$Path;commandLine=$Cmd;creationTimeUtc=$Created;cpuMs=1.0;cpuReadOk=$true}
}
function Reset-Fixture {
  $script:ExePath=$script:target; $script:OutDir='C:\FixtureOutput'; $script:isOfflineFixture=$true
  $script:scopeRootIdentity=''; $script:scopeExcluded=@{}; $script:identityMap=@{}; $script:lastPidIdentity=@{}; $script:processCreationFileTimes=@{}; $script:rootObserved=$false
  $script:inventoryCount=0; $script:originMs=0.0; $script:fakeMs=0.0; $script:fakeWallDrift=0.0; $script:fakeReadCost=0.0; $script:fakeCimCost=0.0
  $script:fakeLogMetadata=@{}; $script:fakeLogCalls=0; $script:LogFile=@(); $script:SkipLogManifest=$false; $script:fakeInventoryThrow=$false; $script:fakeHostThrow=$false; $script:fakeSelfDenied=$false; $script:fakeProcessDenied=@(); $script:fakeNullProcessCounter=@(); $script:fakeWrongProcessCreation=@()
  $script:fakeRawInventory=$null; $script:fakeOutputInitFail=$false; $script:fakeWriteFail=$false; $script:fakeLogical=8; $script:fakeSystemNull=$false
  $script:fakeTimeline=$null; $script:fakeCreationOffsetTicks=@{}; $script:fakeRegressionPid=0; $script:fakeChurn=$false; $script:fakePidReuse=$false
  $script:fakeRate=.1; $script:fakeRates=@{}; $script:fakeEvidenceLife=[pscustomobject]@{exists=$false;records=@();parseErrors=@()}
  $script:fakeEvidenceVirtual=[pscustomobject]@{exists=$false;records=@();parseErrors=@()}; $script:fakeFiles=@{}; $script:fakeCalls=New-Object Collections.ArrayList
  $script:fakeCimCalls=0; $script:fakeProcessCalls=0; $script:fakeSleepCalls=0; $script:fakeSystemCalls=0
  $script:fakeRows=@((New-Fixture 100 0 'YeManCC.exe' $script:target '--token ROOT_PRIVATE' $script:t0))
  foreach($n in @('inventoryErrors','readErrors','pidReuse','rawSamples','systemRows','collectorSelfMetrics','inventoryEvents')) { Set-Variable -Name $n -Scope Script -Value (New-Object Collections.ArrayList) }
  $script:previousKeys=New-Object 'Collections.Generic.HashSet[string]'; $script:churn=New-Object 'Collections.Generic.HashSet[string]'
  $script:inventoryCompletedMs=0.0; $script:skippedSlots=0; $script:systemCpuPrevious=$null; $script:hostInfo=[pscustomobject]@{logicalProcessors=8}
  $script:FanState='unknown'; $script:GamepadState='unknown'; $script:GyroState='unknown'; $script:ExpectedSessionId=''; $script:RequireControlEvidence=$false; $script:EvidenceMaxAgeSeconds=600
  $script:SkipSystemCpu=$true; $script:scriptVersion='offline-helpers'; $script:realCapturePerformed=$false
}
function Case([string]$Name,[scriptblock]$Body) {
  Reset-Fixture; $before=$script:checks.Count
  try { & $Body; $pass=$true; $failure=$null } catch { $pass=$false; $failure=$_.Exception.Message; $location=$_.InvocationInfo.ScriptLineNumber }
  [void]$script:cases.Add([pscustomobject]@{name=$Name;passed=$pass;checks=($script:checks.Count-$before);failure=$failure;failureLine=$(if($pass){$null}else{$location})})
}
# Strict fakes: unknown calls throw; never fall through to native/real process APIs.
$mockDefinitions=@'
$script:isOfflineFixture=$true
function Get-NowUtc { return [datetime]::SpecifyKind([datetime]'2026-10-04T04:00:00',[DateTimeKind]::Utc).AddMilliseconds($script:fakeMs+$script:fakeWallDrift) }
function Get-MonotonicMilliseconds { return $script:fakeMs }
function Wait-Collector([double]$Milliseconds) { $script:fakeSleepCalls++; if($Milliseconds -gt 0){$script:fakeMs+=$Milliseconds} }
function Initialize-Output { if($script:fakeOutputInitFail){throw 'private init failure --token INIT_SECRET'}; $script:OutDir='C:\FixtureOutput'; $script:outputReady=$true }
function Write-Utf8([string]$Path,[string]$Text) { if($script:fakeWriteFail){throw 'private write failure Authorization: OUTPUT_SECRET'}; $script:fakeFiles[[IO.Path]::GetFileName($Path)]=$Text }
function Get-ExecutableInfo([string]$Path) { return [pscustomobject]@{available=$true;path=$Path;sha256='FIXTURE_FILE_HASH';lengthBytes=100} }
function Read-EvidenceJsonl([string]$Path,[string]$Kind) { if($Kind -eq 'lifecycle'){return $script:fakeEvidenceLife}; if($Kind -eq 'virtual-handshake'){return $script:fakeEvidenceVirtual}; throw 'UNEXPECTED_EVIDENCE_KIND' }
function Get-FakeActiveRows {
  $r=$script:fakeRows
  if($script:fakeTimeline){$r=@(& $script:fakeTimeline $script:fakeMs)}
  if($script:fakeChurn -and $script:fakeMs -ge 500){$r=@($r | Where-Object {$_.pid -ne 120})}
  if($script:fakePidReuse -and $script:fakeMs -ge 500){$r=@(foreach($item in $r){if($item.pid -eq 120){$copy=$item|Select-Object *; $copy.creationTimeUtc=$script:t2; $copy}else{$item}})}
  return ,@($r)
}
function Get-CimInstance {
  param($ClassName,$Filter,$ErrorAction)
  $script:fakeCimCalls++; [void]$script:fakeCalls.Add('CIM:'+ $ClassName); $script:fakeMs+=$script:fakeCimCost
  if($ClassName -eq 'Win32_ComputerSystem') { if($script:fakeHostThrow){throw 'host private failure HOST_SECRET'}; return [pscustomobject]@{NumberOfLogicalProcessors=$script:fakeLogical} }
  if($ClassName -eq 'Win32_PerfFormattedData_PerfOS_Processor') { return [pscustomobject]@{PercentProcessorTime=$(if($script:fakeSystemNull){$null}else{17})} }
  if($ClassName -ne 'Win32_Process') { throw ('UNEXPECTED_CIM_CLASS:'+ $ClassName) }
  if($script:fakeInventoryThrow){throw 'inventory private command --token INVENTORY_SECRET'}
  if($null -ne $script:fakeRawInventory){return $script:fakeRawInventory}
  foreach($r in (Get-FakeActiveRows)){
    [pscustomobject]@{ProcessId=$r.pid;ParentProcessId=$r.ppid;Name=$r.name;ExecutablePath=$r.path;CommandLine=$r.commandLine;CreationDate=$r.creationTimeUtc;
      UserModeTime=$(if($r.cpuReadOk){10000}else{$null});KernelModeTime=$(if($r.cpuReadOk){10000}else{$null})}
  }
}
function Get-Process {
  param($Id,$ErrorAction)
  $script:fakeProcessCalls++; [void]$script:fakeCalls.Add('PROCESS:'+ $Id); $script:fakeMs+=$script:fakeReadCost
  if($Id -eq $PID){
    if($script:fakeSelfDenied){throw 'self private failure SELF_SECRET'}
    return [pscustomobject]@{TotalProcessorTime=[pscustomobject]@{TotalMilliseconds=($script:fakeMs*.03)};WorkingSet64=8192;PrivateMemorySize64=4096}
  }
  if($Id -in $script:fakeProcessDenied){throw 'access denied --lease PRIVATE_LEASE Authorization: PRIVATE_AUTH'}
  $rows=@((Get-FakeActiveRows) | Where-Object {$_.pid -eq $Id})
  if($rows.Count -ne 1){throw 'FAKE_PROCESS_NOT_UNIQUE_OR_ABSENT'}
  $r=$rows[0]; $created=Get-ScopeCreationTime $r.creationTimeUtc
  if($script:fakeCreationOffsetTicks.ContainsKey([int]$Id)){$created=$created.AddTicks($script:fakeCreationOffsetTicks[[int]$Id])}
  if($Id -in $script:fakeWrongProcessCreation){$created=$created.AddSeconds(1)}
  $rate=$script:fakeRate; if($script:fakeRates.ContainsKey([int]$Id)){$rate=$script:fakeRates[[int]$Id]}
  $cpu=$script:fakeMs*$rate
  if($script:fakeRegressionPid -eq $Id -and $script:fakeMs -ge 500 -and $script:fakeMs -lt 1000){$cpu=0.0}
  $time=[pscustomobject]@{TotalMilliseconds=$cpu}; if($Id -in $script:fakeNullProcessCounter){$time=$null}
  return [pscustomobject]@{ProcessName=[IO.Path]::GetFileNameWithoutExtension($r.name);StartTime=$created;TotalProcessorTime=$time}
}
function Read-ProcessSnapshot([int]$ProcessId) {
  $p=Get-Process -Id $ProcessId -ErrorAction Stop
  $creation=$null; if($null -ne $p.StartTime){$creation=$p.StartTime.ToUniversalTime().ToFileTimeUtc()}
  return [pscustomobject]@{creationFileTime=$creation;cpuMs=$(if($null -ne $p.TotalProcessorTime){$p.TotalProcessorTime.TotalMilliseconds}else{$null});readAtMs=$script:fakeMs}
}
function Get-LogFileMetadata([string]$Path) {
  $script:fakeLogCalls++
  if(-not $script:fakeLogMetadata.ContainsKey($Path)){throw 'UNEXPECTED_FAKE_LOG_PATH'}
  return $script:fakeLogMetadata[$Path]
}
function Read-SystemCpuTicks {
  $script:fakeSystemCalls++; [void]$script:fakeCalls.Add('SYSTEM_TICKS'); $v=[long]$script:fakeMs
  return ,@([long](100+$v*200),[long](200+$v*500),[long](300+$v*500))
}
'@
. ([scriptblock]::Create($mockDefinitions))
function Invoke-FullFixture([hashtable]$Overrides=@{}) {
  # Execute the FULL saved collector param block, definitions, dispatcher, serialization and catch.
  # Only external reads, time, output IO and final exit are intercepted. No production algorithm rewrite.
  $entry=@($ast.EndBlock.Statements | Where-Object {$_.Extent.Text -match '^\$collectorOutput\s*='})
  $exit=@($ast.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.ExitStatementAst]})
  if($entry.Count -ne 1 -or $exit.Count -ne 1){throw 'FULL_PATH_ENTRY_AST_CHANGED'}
  $text=$source.Substring(0,$exit[0].Extent.StartOffset)+'$script:interceptedExitCode=$collectorExitCode'+$source.Substring($exit[0].Extent.EndOffset)
    # A dynamic ScriptBlock has its caller's script scope, unlike a real -File invocation.
  # Mirror the real bound param variables into that scope; otherwise fixture arguments are ignored.
  $forward=@($ast.ParamBlock.Parameters | ForEach-Object { '$script:'+ $_.Name.VariablePath.UserPath +'=$'+ $_.Name.VariablePath.UserPath }) -join "`r`n"
  $text=$text.Insert($entry[0].Extent.StartOffset,$forward+"`r`n"+$mockDefinitions+"`r`n")
  $args=@{ExePath=$script:target;OutDir='C:\FixtureOutput';WarmupSeconds=0;Seconds=1;SampleMilliseconds=500;InventoryMilliseconds=250;SkipSystemCpu=$true;SkipLogManifest=$true}
  foreach($k in $Overrides.Keys){$args[$k]=$Overrides[$k]}
  $script:interceptedExitCode=$null
  $stdout=@(& ([scriptblock]::Create($text)) @args)
  $summary=$null
  if($script:fakeFiles.ContainsKey('summary.json')){$summary=$script:fakeFiles['summary.json']|ConvertFrom-Json}
  return [pscustomobject]@{exitCode=$script:interceptedExitCode;summary=$summary;stdout=($stdout -join "`n");files=$script:fakeFiles;calls=$script:fakeCalls.ToArray()}
}
function Assert-OffLine([object]$Result,[string]$Name) {
  Assert ($Result.summary.validationScope -eq 'SOFTWARE_TOOL_VALIDATION_ONLY' -and -not $Result.summary.rogRegressionPassed -and -not $Result.summary.realCapturePerformed -and -not $Result.summary.comparisonEligible) ($Name+'-never-signs-real-ROG-comparison')
}
function New-Sample([double]$At,[hashtable]$Cpu,[bool]$Complete=$true) {
  [pscustomobject]@{elapsedMs=$At;sampleIntervalMs=$(if($At -gt 0){$At}else{$null});latenessMs=0;scopeComplete=$Complete;
    processes=@(foreach($k in $Cpu.Keys){[pscustomobject]@{key=$k;readOk=($null -ne $Cpu[$k]);cpuMs=$Cpu[$k];readAtElapsedMs=$At}})}
}
Case 'parser-and-path-contract' {
  Assert ($parseErrorCount -eq 0 -and $testParseErrorCount -eq 0) 'collector-and-tests-parse'
  $bytes=[IO.File]::ReadAllBytes($CollectorPath)
  Assert ($bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) 'PS51-UTF8-BOM'
  foreach($path in @('','YeManCC.exe','.\YeManCC.exe','C:\X\*.exe','C:\X\app.txt')){Assert (-not (Test-ExactExePath $path)) 'invalid-exe-path'}
  Assert (Test-ExactExePath $target) 'fully-qualified-exe-accepted'
}
Case 'microsecond-creation-boundaries-and-full-value-pin' {
  foreach($ticks in @(0,1,9)){
    $script:processCreationFileTimes=@{}; $script:fakeCreationOffsetTicks=@{100=[long]$ticks}
    $r=Get-ProcessCpu 100 'YeManCC.exe' $t0
    Assert $r.ok ('same-1us-bucket-positive-'+$ticks+'-100ns-ticks')
    $again=Get-ProcessCpu 100 'YeManCC.exe' $t0
    Assert $again.ok 'unchanged-full-creation-value-reread-accepted'
  }
  foreach($ticks in @(-1,10,1000,20000)){
    $script:processCreationFileTimes=@{}; $script:fakeCreationOffsetTicks=@{100=[long]$ticks}
    $r=Get-ProcessCpu 100 'YeManCC.exe' $t0
    Assert (-not $r.ok -and $null -eq $r.cpuMs -and $r.error -eq 'CPU_PROCESS_IDENTITY_CHANGED') ('different-bucket-rejected-'+$ticks+'-100ns-ticks')
  }
  $script:processCreationFileTimes=@{}; $script:fakeCreationOffsetTicks=@{100=[long]1}
  $first=Get-ProcessCpu 100 'YeManCC.exe' $t0
  $script:fakeCreationOffsetTicks=@{100=[long]2}; $next=Get-ProcessCpu 100 'YeManCC.exe' $t0
  Assert ($first.ok -and -not $next.ok) 'even-same-bucket-creation-change-after-pin-is-rejected'
}
Case 'inventory-null-counter-regression' {
  foreach($pair in @(@($null,10000),@(10000,$null),@($null,$null),@('',0),@(-1,0),@('NaN',0),@('Infinity',0))){
    $script:fakeRawInventory=@([pscustomobject]@{ProcessId=100;ParentProcessId=0;Name='YeManCC.exe';ExecutablePath=$target;CreationDate=$t0;UserModeTime=$pair[0];KernelModeTime=$pair[1];CommandLine='--token MUST_STAY_IN_MEMORY'})
    $r=Get-Inventory
    Assert ($r.ok -and -not $r.rows[0].cpuReadOk -and $null -eq $r.rows[0].cpuMs) 'null-invalid-CIM-counter-is-not-successful-zero'
  }
  $script:fakeRawInventory[0].UserModeTime=0; $script:fakeRawInventory[0].KernelModeTime=0
  $r=Get-Inventory
  Assert ($r.rows[0].cpuReadOk -and $r.rows[0].cpuMs -eq 0) 'explicit-zero-CIM-counter-remains-valid'
}
Case 'full-tree-accounting-privacy-and-self-cost' {
  $script:fakeRows+=@(
    (New-Fixture 110 100 'msedgewebview2.exe' '' 'x --embedded-browser-webview=1 --lease OWNED_LEASE' $t1),
    (New-Fixture 111 110 'msedgewebview2.exe' '' 'x --type=renderer --user-data-dir="C:\PRIVATE_DATA_ROOT" --confirm PRIVATE_CONFIRM' $t2),
    (New-Fixture 112 110 'msedgewebview2.exe' '' 'x --type=gpu-process' $t2),
    (New-Fixture 113 110 'msedgewebview2.exe' '' 'x --type=utility' $t2),
    (New-Fixture 114 110 'msedgewebview2.exe' '' 'x --type=crashpad-handler' $t2),
    (New-Fixture 120 100 'YeManFanHost.exe' '' '--token PRIVATE_TOKEN' $t1),
    (New-Fixture 121 100 'YeManInputHost.exe' '' '--authorization PRIVATE_AUTH' $t1),
    (New-Fixture 122 100 'YeManRecoveryService.exe' '' '' $t1),
    (New-Fixture 123 100 'YeManTdpCtl.exe' '' '' $t1),
    (New-Fixture 124 100 'YeManLightSetter.exe' '' '' $t1),
    (New-Fixture 130 120 'fan-child.exe' '' '--password PRIVATE_PASSWORD' $t2),
    (New-Fixture 131 121 'input-child.exe' '' 'YeMan' $t2))
  $script:fakeReadCost=2; $script:fakeCimCost=7
  $r=Invoke-FullFixture @{FanState='on';GamepadState='on';GyroState='on'}
  Assert ($r.exitCode -eq 0 -and $r.summary.measurementValid) 'complete-full-script-path-succeeds'
  Assert (Near $r.summary.groups.ymccTotalUnique.cpuCores 1.3) '13-unique-identities-with-actual-read-times'
  Assert (Near $r.summary.groups.webview2Total.cpuCores .5) 'complete-five-process-webview-ledger'
  Assert (Near $r.summary.groups.ymccTotalUnique.cpuPercentMachine 16.25) 'machine-percent-normalized-by-eight'
  Assert (Near $r.summary.groups.ymccTotalUnique.cpuPercentOneCore 130) 'one-core-percent-is-distinct'
  foreach($kind in @('browser','renderer','gpu-process','utility','crashpad-handler')){Assert ($r.summary.webview2Breakdown.$kind.processCount -eq 1) ('webview-'+$kind)}
  Assert ($r.summary.webview2BreakdownQuality.countsMatchTotal -and $r.summary.webview2BreakdownQuality.classificationComplete) 'classification-survives-privacy-projection'
  Assert ($r.summary.featureAttribution.gamepadPids.Count -eq 1 -and $r.summary.featureAttribution.gyroPids.Count -eq 1) 'gyro-and-gamepad-share-once-counted-PID'
  Assert (-not $r.summary.scenarioVerified -and -not $r.summary.state.requestedStatesAreRuntimeEvidence) 'on-request-is-not-runtime-evidence'
  $exports=$r.files.Values -join "`n"
  foreach($secret in @('ROOT_PRIVATE','OWNED_LEASE','PRIVATE_CONFIRM','PRIVATE_TOKEN','PRIVATE_AUTH','PRIVATE_PASSWORD','PRIVATE_DATA_ROOT','--embedded-browser-webview','--type=renderer')){Assert (-not $exports.Contains($secret)) ('no-raw-output-'+$secret)}
  Assert ($r.summary.processes[1].PSObject.Properties.Name -notcontains 'commandLine') 'owned-rows-no-commandline-field'
  Assert ($r.summary.window.inventoryCount -ge 2 -and $r.summary.window.inventoryLatencyMs.max -eq 7) 'inventory-count-and-latency'
  Assert ($r.summary.window.sampleCollectionMs.max -gt 0 -and $r.summary.window.collectorSelfMetrics.Count -eq 3) 'collector-cost-not-placeholder'
  Assert-OffLine $r 'full-positive'
}
Case 'foreign-instance-privacy' {
  $script:fakeRows+=@((New-Fixture 200 100 'YeManCC.exe' $foreign '--token FOREIGN_ROOT_SECRET' $t1),
    (New-Fixture 210 200 'msedgewebview2.exe' 'D:\PRIVATE_FOREIGN_PATH\msedgewebview2.exe' '--type=renderer --title=YeMan --lease FOREIGN_LEASE' $t2),
    (New-Fixture 220 200 'YeManFanHost.exe' '' '--confirm FOREIGN_CONFIRM' $t2),
    (New-Fixture 221 200 'YeManInputHost.exe' '' 'Authorization: Bearer FOREIGN_AUTH' $t2))
  $r=Invoke-FullFixture
  Assert ($r.exitCode -eq 0 -and $r.summary.measurementValid -and $r.summary.processes.Count -eq 1) 'foreign-root-barrier-does-not-pollute-target-total'
  Assert ($r.summary.quality.foreignScopeCount -eq 4) 'foreign-tree-excluded'
  foreach($e in $r.summary.scopeExcludedCandidates){Assert ((@($e.PSObject.Properties.Name | Sort-Object) -join ',') -eq 'creationTimeUtc,exclusionReason,name,pid') 'foreign-four-field-whitelist'}
  $text=$r.files.Values -join "`n"
  foreach($secret in @('FOREIGN_ROOT_SECRET','PRIVATE_FOREIGN_PATH','FOREIGN_LEASE','FOREIGN_CONFIRM','FOREIGN_AUTH',$foreign)){Assert (-not $text.Contains($secret)) ('foreign-privacy-'+$secret)}
}
foreach($spec in @(@('msedgewebview2.exe','--title=YeManCC'),@('msedgewebview2.exe','--user-data-dir=C:\ScopeFixture\Install-A\YeManCC.WebView2'),@('YeManFanHost.exe',''),@('YeManInputHost.exe',''),@('YeManRecoveryService.exe',''),@('YeManTdpCtl.exe',''),@('YeManLightSetter.exe',''))){
  $caseExe=$spec[0]; $caseCmd=$spec[1]
  Case ('unresolved-'+$caseExe+'-'+$caseCmd) {
    $script:fakeRows+=(New-Fixture 210 999 $caseExe 'C:\ScopeFixture\Install-A\helper.exe' $caseCmd $t1)
    $r=Invoke-FullFixture
    Assert ($r.exitCode -eq 3 -and -not $r.summary.measurementValid) 'unresolved-nonzero-invalid'
    Assert ($null -eq $r.summary.groups.ymccTotalUnique.cpuCores) 'unresolved-total-null'
    $role=Get-BaseRole $script:fakeRows[1]
    Assert ($null -eq $r.summary.groups.$role.cpuCores -and -not $r.summary.groups.$role.verifiedAbsent) 'unresolved-component-not-zero'
    Assert-OffLine $r 'unresolved'
  }
}
foreach($time in @($t2,'','not-a-time','2026-10-04T00:00:01')){
  $edgeTime=$time
  Case ('invalid-parent-time-'+$time) {
    $script:fakeRows[0].creationTimeUtc=$edgeTime
    $script:fakeRows+=(New-Fixture 110 100 'msedgewebview2.exe' '' '--type=renderer' $t1)
    $r=Invoke-FullFixture
    Assert ($r.exitCode -eq 3 -and $null -eq $r.summary.groups.webview2Total.cpuCores) 'newer-missing-malformed-zoneless-parent-rejected'
  }
}
Case 'equal-time-case-insensitive-path' {
  $script:fakeRows[0].path=$target.ToUpperInvariant(); $script:fakeRows[0].name='yemancc.EXE'
  $script:fakeRows+=(New-Fixture 120 100 'YeManFanHost.exe' '' '' $t0)
  $r=Invoke-FullFixture
  Assert ($r.exitCode -eq 0 -and $r.summary.processes.Count -eq 2) 'equal-creation-parent-edge-accepted'
}
Case 'intermediate-edge-cycle-duplicate-and-root-pin' {
  foreach($when in @($t2,'')){
    $script:scopeRootIdentity=''; $script:scopeExcluded=@{}
    $s=Get-Candidates @($script:fakeRows[0],(New-Fixture 150 100 'worker.exe' '' '' $when),(New-Fixture 210 150 'msedgewebview2.exe' '' '' $t1))
    Assert (@($s.rows | Where-Object {$_.pid -eq 210}).Count -eq 0 -and $s.unresolvedCount -gt 0) 'reused-or-missing-intermediate-creation-rejected'
  }
  $script:scopeRootIdentity=''; $script:scopeExcluded=@{}
  $s=Get-Candidates @($script:fakeRows[0],(New-Fixture 210 211 'msedgewebview2.exe' '' 'YeMan' $t1),(New-Fixture 211 210 'worker.exe' '' '' $t1))
  Assert ($s.unresolvedCount -gt 0) 'cycle-fails-closed'
  $s=Get-Candidates @($script:fakeRows[0],(New-Fixture 150 100 'worker.exe' '' '' $t1),(New-Fixture 150 999 'worker.exe' '' '' $t1),(New-Fixture 210 150 'msedgewebview2.exe' '' '' $t2))
  Assert (@($s.rows | Where-Object {$_.pid -eq 150 -or $_.pid -eq 210}).Count -eq 0) 'duplicate-PID-unproven'
  $s=Get-Candidates @((New-Fixture 200 0 'YeManCC.exe' $target '' $t1))
  Assert (-not $s.rootValid -and $s.rows.Count -eq 0) 'root-instance-never-repinned'
}
Case 'ambiguous-same-path-roots' {
  $script:fakeRows+=(New-Fixture 200 0 'YeManCC.exe' $target '' $t1)
  $r=Invoke-FullFixture
  Assert ($r.exitCode -eq 3 -and $r.summary.processes.Count -eq 0) 'same-path-instances-not-unioned'
}
Case 'unknown-helper-invalid-edge' {
  $script:fakeRows+=(New-Fixture 150 100 'unlisted-worker.exe' '' '' '')
  $r=Invoke-FullFixture
  Assert ($r.exitCode -eq 3 -and $r.summary.quality.unresolvedScopeCount -eq 1) 'generic-descendant-not-silently-lost'
}
Case 'clean-and-unverified-absence' {
  $r=Invoke-FullFixture
  Assert ($r.exitCode -eq 0 -and $r.summary.groups.fanHost.verifiedAbsent -and $r.summary.groups.fanHost.cpuCores -eq 0) 'clean-observed-absence-is-zero'
  $m=Metric @() 1 8 $true $false @((New-Sample 0 @{}),(New-Sample 1000 @{}))
  Assert ($null -eq $m.cpuCores -and -not $m.verifiedAbsent) 'no-rows-is-not-absence-proof'
}
Case 'statistics-sum-before-percentiles-and-null' {
  $samples=@((New-Sample 0 @{a=0;b=0}),(New-Sample 1000 @{a=1000;b=0}),(New-Sample 2000 @{a=1000;b=1000}))
  $r=Get-IntervalStats @('a','b','a') $samples 8
  Assert ($r.valid -and (Near $r.cpuCores.p95 1) -and (Near $r.cpuCores.max 1)) 'anti-correlated-P95-total-one-not-two'
  $a=Get-IntervalStats @('a') $samples 8; $b=Get-IntervalStats @('b') $samples 8
  Assert (Near ($a.cpuCores.p95+$b.cpuCores.p95) 2) 'component-P95-sum-not-total-P95'
  $stats=New-Stats @($null,0) 8
  Assert (-not $stats.valid -and $null -eq $stats.cpuCores.mean -and $stats.validSampleCount -eq 1) 'null-neither-cast-to-zero-nor-silently-dropped'
  foreach($end in @(@{a=$null},@{a=-1},@{})){
    $bad=Get-IntervalStats @('a') @((New-Sample 0 @{a=100}),(New-Sample 1000 $end)) 8
    Assert (-not $bad.valid -and $null -eq $bad.cpuCores.p95) 'bad-interval-not-low-CPU'
  }
  $r=Get-IntervalStats @('a') @((New-Sample 0 @{a=0}),(New-Sample 1300 @{a=130}),(New-Sample 2800 @{a=280})) 8
  Assert (Near $r.cpuCores.mean .1) 'uses-actual-monotonic-span'
}
Case 'coverage-count-deficit-not-lost-sample-proof' {
  $s=@(for($i=0;$i -le 55;$i++){[pscustomobject]@{elapsedMs=($i*60000.0/55);sampleIntervalMs=$(if($i -gt 0){60000.0/55}else{$null});latenessMs=($i*60000.0/55-$i*1000)}})
  $c=Get-Coverage $s 60000 1000 0
  Assert ($c.actualIntervalCount -eq 55 -and $c.expectedIntervalCount -eq 60 -and $c.intervalCountDeficit -eq 5 -and $c.missedSampleCount -eq 0) 'actual55-expected60-does-not-imply-five-lost-samples'
  Assert ($c.expectedSampleCount -eq 61 -and $c.actualSampleCount -eq 56) 'baseline-and-final-boundary-definition'
  Assert ($c.absoluteJitterMs.max -gt 90 -and $c.intervalsMs.max -gt 1000) 'jitter-retains-cadence-gap'
}
Case 'deadline-scheduler-explicit-skips' {
  $script:fakeReadCost=320; $script:fakeCimCost=10
  $r=Invoke-FullFixture @{Seconds=2;SampleMilliseconds=100}
  Assert ($r.exitCode -eq 3 -and $r.summary.window.coverage.scheduledSlotsSkipped -gt 0) 'slow-collector-explicitly-skips-slots'
}
foreach($mode in @('cim-null','process-null','access-denied','identity-changed','regression','churn','pid-reuse','inventory-denied','self-denied','host-failure')){
  $fault=$mode
  Case ('full-path-failure-'+$mode) {
    $script:fakeRows+=(New-Fixture 120 100 'YeManFanHost.exe' '' '' $t1)
    switch($fault){
      'cim-null' {$script:fakeRows[1].cpuReadOk=$false}
      'process-null' {$script:fakeNullProcessCounter=@(120)}
      'access-denied' {$script:fakeProcessDenied=@(120)}
      'identity-changed' {$script:fakeWrongProcessCreation=@(120)}
      'regression' {$script:fakeRegressionPid=120; $script:fakeReadCost=1}
      'churn' {$script:fakeChurn=$true}
      'pid-reuse' {$script:fakePidReuse=$true}
      'inventory-denied' {$script:fakeInventoryThrow=$true}
      'self-denied' {$script:fakeSelfDenied=$true}
      'host-failure' {$script:fakeHostThrow=$true}
    }
    $r=Invoke-FullFixture
    Assert ($r.exitCode -gt 0 -and -not $r.summary.measurementValid) ($fault+'-structured-nonzero')
    Assert ($null -eq $r.summary.groups -or $null -eq $r.summary.groups.ymccTotalUnique.cpuCores) ($fault+'-not-low-CPU')
    foreach($private in @('PRIVATE_LEASE','PRIVATE_AUTH','INVENTORY_SECRET','SELF_SECRET','HOST_SECRET')){Assert (-not (($r.files.Values -join "`n")+$r.stdout).Contains($private)) ($fault+'-safe-exception-'+$private)}
    Assert-OffLine $r $fault
  }
}
Case 'missing-path-control-preflight-and-output-errors' {
  $r=Invoke-FullFixture @{ExePath=''}
  Assert ($r.exitCode -eq 2 -and -not $r.summary.measurementStarted -and $script:fakeCimCalls -eq 0 -and $script:fakeProcessCalls -eq 0) 'missing-path-before-system-read'
  $r=Invoke-FullFixture @{RequireControlEvidence=$true;FanState='on';GamepadState='off';GyroState='off';ExpectedSessionId='PRIVATE_SESSION'}
  Assert ($r.exitCode -eq 7 -and -not $r.summary.measurementStarted -and -not $r.summary.scenarioVerified -and $script:fakeCimCalls -eq 0) 'states-alone-not-control-evidence'
  $script:fakeOutputInitFail=$true; $r=Invoke-FullFixture; $j=$r.stdout|ConvertFrom-Json
  Assert ($r.exitCode -eq 2 -and -not $j.measurementValid -and -not $r.stdout.Contains('INIT_SECRET')) 'output-init-structured-safe-failure'
  $script:fakeOutputInitFail=$false; $script:fakeWriteFail=$true; $r=Invoke-FullFixture; $j=$r.stdout|ConvertFrom-Json
  Assert ($r.exitCode -eq 1 -and $j.failure.code -eq 'FAILURE_OUTPUT_UNAVAILABLE' -and -not $r.stdout.Contains('OUTPUT_SECRET')) 'output-write-never-success'
}
Case 'offline-system-counter-arithmetic' {
  $previous=@(100,200,300)
  foreach($c in @(@{v=@(400,500,300);p=0},@{v=@(100,400,500);p=100},@{v=@(700,800,500);p=25},@{v=@(300,500,400);p=50})){
    $r=Get-SystemCpuInterval $previous $c.v
    Assert (Near $r.percent $c.p) 'system-kernel-includes-idle'
    Assert ($r.intervalTicks.Count -eq 3) 'system-raw-deltas'
  }
  foreach($bad in @(@(99,201,301),@(100,199,301),@(100,201,299),@(100,200,300),@(500,300,400),@(1,2))){Assert (Throws {Get-SystemCpuInterval $previous $bad}) 'invalid-system-delta'}
  $rng=New-Object Random 172910
  for($i=0;$i -lt 512;$i++){
    $kernel=$rng.Next(1,100000);$idle=$rng.Next(0,$kernel+1);$user=$rng.Next(0,100000)
    $r=Get-SystemCpuInterval $previous @((100+$idle),(200+$kernel),(300+$user))
    Assert (Near $r.percent (100.0*($kernel+$user-$idle)/($kernel+$user))) ('random-counter-'+$i)
  }
  $first=System-Cpu
  Assert (-not $first.ok -and $null -eq $first.percent -and $first.error -eq 'baseline-required') 'fake-system-baseline-not-zero'
  $script:fakeMs=500; $r=System-Cpu
  Assert ($r.ok -and (Near $r.percent 80) -and $script:fakeSystemCalls -eq 2) 'fake-system-interval'
  $script:hostInfo.logicalProcessors=128; $r=System-Cpu
  Assert ($r.ok -and $r.percent -eq 17) 'fake-over64-CIM-fallback'
  $script:fakeSystemNull=$true; $r=System-Cpu
  Assert (-not $r.ok -and $null -eq $r.percent) 'null-system-percent-not-zero'
}
Case 'full-path-fake-system-monotonic-clock' {
  $script:fakeReadCost=3; $script:fakeWallDrift=-3600000
  $r=Invoke-FullFixture @{SkipSystemCpu=$false}
  Assert ($r.exitCode -eq 0 -and (Near $r.summary.groups.ymccTotalUnique.cpuCores .1)) 'CPU-rates-independent-of-wall-time-offset'
  Assert ($script:fakeSystemCalls -eq 4 -and $r.files.ContainsKey('system-cpu.jsonl')) 'complete-system-path-faked'
  Assert-OffLine $r 'fake-system'
}
Case 'explicit-log-metadata-without-real-log-IO' {
  Assert ((Log-Manifest).status -eq 'not-requested') 'empty-log-parameter-not-requested'
  $script:SkipLogManifest=$true; Assert ((Log-Manifest).status -eq 'skipped') 'explicit-log-skip'
  $script:SkipLogManifest=$false
  $paths=@('C:\FixtureLogs\cpu.log','C:\FixtureLogs\empty.log','C:\FixtureLogs\missing.log','C:\FixtureLogs\denied.log','C:\FixtureLogs\link.log','C:\FixtureLogs\old.log')
  foreach($path in $paths){$script:fakeLogMetadata[$path]=[pscustomobject]@{status='observed';lengthBytes=123;lastWriteTimeUtc='2026-10-04T03:59:59.0000000Z';readOpenOk=$true;code=$null}}
  $script:fakeLogMetadata[$paths[1]].lengthBytes=0
  $script:fakeLogMetadata[$paths[2]]=[pscustomobject]@{status='missing';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code='LOG_FILE_MISSING'}
  $script:fakeLogMetadata[$paths[3]].status='error'; $script:fakeLogMetadata[$paths[3]].readOpenOk=$false; $script:fakeLogMetadata[$paths[3]].code='LOG_READ_OPEN_UNAVAILABLE'
  $script:fakeLogMetadata[$paths[4]]=[pscustomobject]@{status='rejected';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code='LOG_REPARSE_POINT_REJECTED'}
  $script:fakeLogMetadata[$paths[5]].lastWriteTimeUtc='2026-10-04T00:00:00.0000000Z'
  $script:LogFile=$paths; $r=Log-Manifest
  Assert ($r.status -eq 'partial' -and $r.files.Count -eq 6 -and $script:fakeLogCalls -eq 6) 'explicit-log-metadata-bounded-to-six-fakes'
  Assert ($r.files[0].readOpenOk -and $r.files[0].lengthBytes -eq 123 -and $r.files[0].basename -eq 'cpu.log') 'normal-log-metadata'
  Assert ($r.files[1].isEmpty -and $r.files[2].status -eq 'missing') 'empty-and-missing-distinct'
  Assert (-not $r.files[3].readOpenOk -and $r.files[4].code -eq 'LOG_REPARSE_POINT_REJECTED') 'denial-and-reparse-not-hidden'
  Assert ($r.files[5].freshness -eq 'stale' -and -not $r.contentsRead -and -not $r.contentsCopied -and -not $r.recursiveScan) 'stale-without-content-read-or-disabled-inference'
  $text=$r|ConvertTo-Json -Depth 12
  Assert (-not $text.Contains('C:\FixtureLogs') -and -not $text.Contains('empty.log') -and $r.files[1].basename -eq 'other-log') 'log-path-and-nonallowlisted-basename-hidden'
  $script:fakeLogCalls=0; $script:LogFile=@('C:\FixtureLogs\settings.json','C:\FixtureLogs\token.log','C:\FixtureLogs\lease.log','C:\FixtureLogs\profile.log','C:\FixtureLogs\*.log')
  $r=Log-Manifest
  Assert ($script:fakeLogCalls -eq 0 -and @($r.files|Where-Object{$_.status -eq 'rejected'}).Count -eq 5) 'sensitive-and-wildcard-logs-rejected-before-IO'
  $script:LogFile=@(1..33|ForEach-Object{'C:\FixtureLogs\cpu.log'}); $r=Log-Manifest
  Assert ($r.status -eq 'error' -and $script:fakeLogCalls -eq 0) 'explicit-log-list-maximum-32'
}
Case 'native-wrapper-compile-only-no-native-call' {
  $native=@($fns | Where-Object Name -EQ 'Read-ProcessSnapshot')[0]
  # Compile the real declaration only. Do NOT execute its native Read method or any process call.
  & ([scriptblock]::Create($native.Body.EndBlock.Statements[0].Extent.Text))
  Assert ($null -ne ('YmccCpu.ReadOnlyProcessTimes' -as [type])) 'native-wrapper-compiles-in-this-runtime-without-process-read'
  Assert ($script:fakeProcessCalls -eq 0 -and $script:fakeSystemCalls -eq 0 -and $script:fakeCimCalls -eq 0) 'compile-only-is-not-real-capture'
  $converted=Creation-Utc '20261004000000.123456+000'
  Assert ($converted -eq '2026-10-04T00:00:00.1234560Z') 'CIM-six-digit-microseconds-preserved'
}
Case 'full-path-explicit-log-manifest-serialization' {
  $path='C:\FixtureLogs\cpu.log'
  $script:fakeLogMetadata[$path]=[pscustomobject]@{status='observed';lengthBytes=0;lastWriteTimeUtc='2026-10-04T03:59:59.0000000Z';readOpenOk=$true;code=$null}
  $r=Invoke-FullFixture @{LogFile=@($path);SkipLogManifest=$false}
  $j=$r.files['log-manifest.json']|ConvertFrom-Json
  Assert ($r.exitCode -eq 0 -and $j.status -eq 'completed' -and $j.files[0].isEmpty -and $j.files[0].readOpenOk) 'full-script-explicit-empty-log-is-observed-not-not-collected'
  Assert ($script:fakeLogCalls -eq 1 -and -not ($r.files.Values -join "`n").Contains('FixtureLogs')) 'full-log-manifest-no-private-path-or-real-log-IO'
  $script:fakeLogMetadata[$path].status='error';$script:fakeLogMetadata[$path].readOpenOk=$false;$script:fakeLogMetadata[$path].code='LOG_READ_OPEN_UNAVAILABLE'
  $r=Invoke-FullFixture @{LogFile=@($path);SkipLogManifest=$false}
  Assert ($r.exitCode -eq 4 -and $r.summary.measurementValid -and $r.summary.logManifest.status -eq 'partial') 'log-metadata-failure-nonzero-separate-from-CPU-validity'
  Assert-OffLine $r 'full-log'
}
Case 'evidence-whitelist-does-not-export-secrets' {
  $assessment=Get-ControlEvidenceAssessment $script:fakeEvidenceLife $script:fakeEvidenceVirtual 'PUBLIC-CORRELATION'
  $assessment.target=[ordered]@{pid=100;path='D:\PRIVATE_TARGET_PATH\YeManCC.exe';creationTimeUtc='PRIVATE_INVALID_BIRTH'}
  $assessment.virtual.features.fan.evidenceSource='Authorization: Bearer PRIVATE_EVIDENCE_SECRET'
  $assessment.virtual.features.fan.stateReadAtUtc='PRIVATE_TIMESTAMP'
  $script:ExpectedSessionId='PRIVATE_SESSION_SECRET'
  $public=Get-PublicControlEvidence $assessment
  $text=$public|ConvertTo-Json -Depth 20
  foreach($secret in @('PRIVATE_TARGET_PATH','PRIVATE_INVALID_BIRTH','PRIVATE_EVIDENCE_SECRET','PRIVATE_TIMESTAMP','PRIVATE_SESSION_SECRET')) { Assert (-not $text.Contains($secret)) ('evidence-not-exported-'+$secret) }
  Assert (-not $public.scenarioVerified) 'unknown-evidence-never-verifies-runtime'
}
$failedCases=@($cases.ToArray() | Where-Object {-not $_.passed}); $failedChecks=@($checks.ToArray() | Where-Object {-not $_.passed})
$exitCode=0; if($failedCases.Count -or $failedChecks.Count){$exitCode=1}
$result=[ordered]@{schema='ymcc-cpu-collector-offline-validation-v1';validationScope='SOFTWARE_TOOL_VALIDATION_ONLY';rogRegressionPassed=$false;realCapturePerformed=$false;comparisonEligible=$false;
  capturedAtUtc=[datetime]::UtcNow.ToString('o');status=$(if($exitCode -eq 0){'PASS'}else{'FAIL'});exitCode=$exitCode;runtime=[ordered]@{version=$PSVersionTable.PSVersion.ToString();edition=$PSVersionTable.PSEdition};
  parser=[ordered]@{collectorErrors=$parseErrorCount;testErrors=$testParseErrorCount;functionCount=$fns.Count};collectorPath=$CollectorPath;collectorSha256=(Get-FileHash -LiteralPath $CollectorPath -Algorithm SHA256).Hash;
  testPath=$PSCommandPath;testSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;
  isolation='AST helpers + FULL saved script param/dispatcher/serialization/catch/exit. CIM, process/native snapshots, counters, clocks, sleeps, capture-file IO and log-file metadata/open are strict fakes. Only validation JSON output is real file IO.';
  caseCount=$cases.Count;passedCaseCount=($cases.Count-$failedCases.Count);failedCaseCount=$failedCases.Count;checkCount=$checks.Count;failedCheckCount=$failedChecks.Count;cases=$cases.ToArray();checks=$checks.ToArray()}
[IO.Directory]::CreateDirectory($validation)|Out-Null
[IO.File]::WriteAllText($OutputPath,($result|ConvertTo-Json -Depth 30),(New-Object Text.UTF8Encoding($false)))
Write-Output ('status={0}; runtime={1}; parserErrors={2}/{3}; cases={4}; checks={5}; failedCases={6}; exitCode={7}; output={8}' -f $result.status,$result.runtime.version,$parseErrorCount,$testParseErrorCount,$cases.Count,$checks.Count,$failedCases.Count,$exitCode,$OutputPath)
foreach($c in $failedCases){Write-Output ('FAILED '+$c.name+' :: '+$c.failure+' line='+$c.failureLine)}
exit $exitCode
