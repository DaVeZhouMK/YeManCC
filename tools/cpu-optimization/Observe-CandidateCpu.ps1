#requires -Version 5.1
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$ExpectedIdentityPath,
  [ValidateRange(2,86400)][int]$DurationSeconds=360,
  [string]$ExpectedImagePath='',
  [string]$MinimumBirthUtc='',
  [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$EvidenceRoot,
  [string]$OutputRoot=''
)
$ErrorActionPreference='Stop'
$OutputEncoding=[Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding=$OutputEncoding
Set-StrictMode -Version 2.0
$taskEvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(-not [IO.Directory]::Exists($taskEvidenceRoot)){throw 'EvidenceRoot must be an existing development evidence directory.'}
$taskOutputRoot=if($OutputRoot){[IO.Path]::GetFullPath($OutputRoot)}else{Join-Path $taskEvidenceRoot 'CandidateCpu'}
$taskEvidencePrefix=$taskEvidenceRoot.TrimEnd([char[]]'\/')+[IO.Path]::DirectorySeparatorChar
if($taskOutputRoot -ine $taskEvidenceRoot -and -not $taskOutputRoot.StartsWith($taskEvidencePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'OutputRoot must stay within the explicit EvidenceRoot.'}
$taskIntervalMs=250.0
$taskWindowMs=1000.0
$taskThresholdPercent=0.1
$taskHandle=[IntPtr]::Zero
$taskWriter=$null
$taskRunPath=$null
$taskSummary=$null
$taskFailure=$null
$taskStatus='PREPARING'
$taskWholeWall=[Diagnostics.Stopwatch]::StartNew()
$taskSelfProcess=[Diagnostics.Process]::GetCurrentProcess()
$taskSelfCpuAtEntry=$taskSelfProcess.TotalProcessorTime.TotalMilliseconds
$taskBootUtc=[DateTime]::UtcNow.ToString('o')
$taskLoopCosts=New-Object 'Collections.Generic.List[double]'
$taskQueryCosts=New-Object 'Collections.Generic.List[double]'
$taskSamples=0;$taskWindowCount=0;$taskThresholdCount=0;$taskMax=$null;$taskMaxSampler=$null;$taskMissedSlots=0
$taskBirthChecks=0;$taskFirst=$null;$taskLast=$null
# Do not rely on Get-FileHash autoloading the matching Utility module when a
# Windows PowerShell 5.1 child inherits PSModulePath from PowerShell 7.
function Get-CandidateFileSha256 {
  param([string]$Path)
  $stream=[IO.File]::OpenRead($Path)
  $sha=[Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose(); $stream.Dispose() }
}
function Get-CpuPercentFromDeltas {
  param([double]$CpuMs,[double]$WallMs,[int]$LogicalProcessors)
  if($CpuMs -lt 0 -or $WallMs -le 0 -or $LogicalProcessors -le 0){throw 'Invalid CPU/window delta; absent or negative values are not zero.'}
  return ($CpuMs/($WallMs*$LogicalProcessors))*100.0
}
function Get-CostStatistics {
  param($Values)
  $sorted=@($Values|Sort-Object)
  if(-not $sorted.Count){return [ordered]@{count=0;totalMs=0;medianMs=$null;p95Ms=$null;maxMs=$null}}
  [double]$sum=0;foreach($v in $sorted){$sum+=[double]$v}
  $n=$sorted.Count
  $middle=if($n%2){[double]$sorted[[int](($n-1)/2)]}else{([double]$sorted[$n/2-1]+[double]$sorted[$n/2])/2.0}
  return [ordered]@{count=$n;totalMs=$sum;medianMs=$middle;p95Ms=[double]$sorted[[int][Math]::Ceiling(.95*$n)-1];maxMs=[double]$sorted[-1]}
}
function Save-CandidateJson {
  param([string]$Path,$Value)
  [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 12)+"`n",[Text.UTF8Encoding]::new($false))
}
function Get-HeldTimes {
  param([IntPtr]$Handle)
  [long]$birth=0;[long]$exit=0;[long]$kernel=0;[long]$user=0
  if(-not [YmccCandidateCpuReadOnlyV1]::GetProcessTimes($Handle,[ref]$birth,[ref]$exit,[ref]$kernel,[ref]$user)){throw ('GetProcessTimes failed: Win32='+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  # A live process has undefined lpExitTime; only the held handle's wait state proves liveness.
  $wait=[YmccCandidateCpuReadOnlyV1]::WaitForSingleObject($Handle,0)
  if($wait -ne 0 -and $wait -ne 258){throw ('Process wait-state query failed: status='+$wait+'; Win32='+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  if($wait -eq 0 -and -not [YmccCandidateCpuReadOnlyV1]::GetProcessTimes($Handle,[ref]$birth,[ref]$exit,[ref]$kernel,[ref]$user)){throw 'Exited process diagnostic re-read failed'}
  return [pscustomobject]@{birth=$birth;exit=$exit;kernel=$kernel;user=$user;alive=($wait -eq 258);waitStatus=$wait;exitFileTimeDefined=($wait -eq 0)}
}
try {
  # Validate the supplied expected bytes before discovering any target.
  $taskExpectedFull=[IO.Path]::GetFullPath($ExpectedIdentityPath)
  $taskExpectedBytes=[IO.File]::ReadAllBytes($taskExpectedFull)
  $taskSha=[Security.Cryptography.SHA256]::Create()
  try{$taskExpectedFileHash=([BitConverter]::ToString($taskSha.ComputeHash($taskExpectedBytes))).Replace('-','').ToLowerInvariant()}finally{$taskSha.Dispose()}
  $taskExpectedText=[Text.Encoding]::UTF8.GetString($taskExpectedBytes).TrimStart([char]0xFEFF)
  $taskExpected=$taskExpectedText|ConvertFrom-Json
  if($null -eq $taskExpected.PSObject.Properties['exeSha256']){throw 'Expected identity must contain exeSha256.'}
  $taskExpectedExeHash=([string]$taskExpected.exeSha256).ToLowerInvariant()
  if($taskExpectedExeHash -notmatch '^[0-9a-f]{64}$'){throw 'Expected exeSha256 must be a full SHA256 hex string.'}
  if(-not ('YmccCandidateCpuReadOnlyV1' -as [type])){
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class YmccCandidateCpuReadOnlyV1 {
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] public static extern bool CloseHandle(IntPtr handle);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern uint GetProcessId(IntPtr handle);
 [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] public static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] public static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref uint size);
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort group);
}
"@
  }
  # Exactly one CIM query. Its rows select a unique root; no periodic process scans.
  $taskCandidates=@(Get-CimInstance -ClassName Win32_Process -Filter "Name='YeManCC.exe'" -ErrorAction Stop)
  $taskCandidateIds=@($taskCandidates|ForEach-Object{[uint32]$_.ProcessId})
  $taskRoots=@($taskCandidates|Where-Object{[uint32]$_.ParentProcessId -notin $taskCandidateIds})
  if($taskRoots.Count -ne 1){throw ('Expected one YeManCC root, found '+$taskRoots.Count+' among '+$taskCandidates.Count+' same-name processes. No sampling started.')}
  $taskRoot=$taskRoots[0]
  $taskTargetId=[uint32]$taskRoot.ProcessId
  $taskHandle=[YmccCandidateCpuReadOnlyV1]::OpenProcess(0x101000,$false,$taskTargetId)
  if($taskHandle -eq [IntPtr]::Zero){throw ('OpenProcess query-limited plus synchronize failed: Win32='+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  if([YmccCandidateCpuReadOnlyV1]::GetProcessId($taskHandle) -ne $taskTargetId){throw 'Held process handle PID mismatch.'}
  $taskBound=Get-HeldTimes $taskHandle
  if(-not $taskBound.alive){throw 'Target already exited before identity binding.'}
  if($null -eq $taskRoot.CreationDate){throw 'CIM creation time unavailable; cannot rule out discovery/open PID reuse.'}
  $taskCimBirth=([datetime]$taskRoot.CreationDate).ToUniversalTime().ToFileTimeUtc()
  $taskCimBirthDifference=[Math]::Abs([double]($taskBound.birth-$taskCimBirth))
  # CIM datetime has microsecond precision; retain the exact native FILETIME.
  if($taskCimBirthDifference -gt 10){throw 'Process birth changed between one-shot CIM discovery and held handle.'}
  $taskImageText=New-Object Text.StringBuilder 32768
  [uint32]$taskImageChars=$taskImageText.Capacity
  if(-not [YmccCandidateCpuReadOnlyV1]::QueryFullProcessImageName($taskHandle,0,$taskImageText,[ref]$taskImageChars)){throw ('QueryFullProcessImageName failed: Win32='+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
  $taskImagePath=[IO.Path]::GetFullPath($taskImageText.ToString())
  if([IO.Path]::GetFileName($taskImagePath) -ine 'YeManCC.exe'){throw 'Bound process image name is not YeManCC.exe.'}
  if($ExpectedImagePath -and $taskImagePath -ine [IO.Path]::GetFullPath($ExpectedImagePath)){throw ('Expected installed image path differs: '+$taskImagePath)}
  if($MinimumBirthUtc){
    $taskMinimumBirth=[DateTime]::Parse($MinimumBirthUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime().ToFileTimeUtc()
    if($taskBound.birth -lt $taskMinimumBirth){throw 'Target predates this installed deployment; old same-path process cannot count as the new export.'}
  }
  $taskDiskHash=(Get-CandidateFileSha256 $taskImagePath)
  if($taskDiskHash -cne $taskExpectedExeHash){throw ('Candidate identity rejected BEFORE SAMPLING. Expected EXE SHA256='+$taskExpectedExeHash+'; running image disk SHA256='+$taskDiskHash+'; image='+$taskImagePath)}
  $taskLogical=[int][YmccCandidateCpuReadOnlyV1]::GetActiveProcessorCount([System.UInt16]65535)
  if($taskLogical -le 0){throw 'Logical processor count unavailable.'}
  $taskAfterHash=Get-HeldTimes $taskHandle
  if($taskAfterHash.birth -ne $taskBound.birth -or -not $taskAfterHash.alive){throw 'Target identity/liveness changed during startup hash.'}
  $taskRunId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
  $taskProposedRunPath=Join-Path $taskOutputRoot $taskRunId
  [IO.Directory]::CreateDirectory($taskOutputRoot)|Out-Null
  New-Item -ItemType Directory -Path $taskProposedRunPath -ErrorAction Stop|Out-Null
  $taskRunPath=$taskProposedRunPath
  $taskIdentity=[ordered]@{schemaVersion=1;status='HELD_ROOT_PID_BIRTH_DISK_HASH_BOUND';atUtc=[DateTime]::UtcNow.ToString('o');pid=$taskTargetId;parentPid=[uint32]$taskRoot.ParentProcessId;birthFileTimeUtc100ns=([string]$taskBound.birth);birthUtc=[DateTime]::FromFileTimeUtc($taskBound.birth).ToString('o');cimBirthUtc=[DateTime]::FromFileTimeUtc($taskCimBirth).ToString('o');cimBirthDifference100ns=$taskCimBirthDifference;cimBirthTolerance100ns=10;imagePath=$taskImagePath;exeSha256=$taskDiskHash;expectedIdentityPath=$taskExpectedFull;expectedIdentityFileSha256=$taskExpectedFileHash;expectedExeSha256=$taskExpectedExeHash;logicalProcessors=$taskLogical;queryLimitedHandleHeldUntilEnd=$true;cimQueries=1;sameNameProcessCountAtDiscovery=$taskCandidates.Count;scope='Only selected native root; WebView and child CPU excluded';configuredExpectedImagePath=$ExpectedImagePath;configuredMinimumBirthUtc=$MinimumBirthUtc;candidateCpuAcceptance=$false;scenarioVisibilityOrInstallAutomaticallyVerified=$false}
  Save-CandidateJson (Join-Path $taskRunPath 'IDENTITY.json') $taskIdentity
  $taskWriter=New-Object IO.StreamWriter ((Join-Path $taskRunPath 'SAMPLES.jsonl'),$false,([Text.UTF8Encoding]::new($false)))
  $taskWriter.AutoFlush=$false
  Write-Output ('Observation output: '+$taskRunPath)
  Write-Output ('Bound root PID '+$taskTargetId+'; '+$taskLogical+' logical processors; duration '+$DurationSeconds+' s; root CPU only.')
  $taskLoopCosts=New-Object 'Collections.Generic.List[double]'
  $taskQueryCosts=New-Object 'Collections.Generic.List[double]'
  $taskQueue=New-Object 'Collections.Generic.List[object]'
  $taskSamples=0;$taskWindowCount=0;$taskThresholdCount=0;$taskMax=$null;$taskMaxSampler=$null;$taskMissedSlots=0
  $taskBirthChecks=0;$taskPreviousCost=$null;$taskCompletedCost=0.0
  $taskSelfHandle=[YmccCandidateCpuReadOnlyV1]::GetCurrentProcess()
  $taskSamplerBirth=(Get-HeldTimes $taskSelfHandle).birth
  $taskClock=[Diagnostics.Stopwatch]::StartNew()
  $taskNextDue=0.0;$taskNextProgress=30000.0
  $taskStatus='OBSERVING'
  $taskFirst=$null;$taskLast=$null
  while($true){
    $taskNow=$taskClock.Elapsed.TotalMilliseconds
    if($taskNow -ge $DurationSeconds*1000.0){break}
    if($taskNow -lt $taskNextDue){[Threading.Thread]::Sleep([int][Math]::Ceiling($taskNextDue-$taskNow));continue}
    $taskWorkStart=$taskClock.Elapsed.TotalMilliseconds
    $taskQueryStart=$taskClock.Elapsed.TotalMilliseconds
    $taskTimes=Get-HeldTimes $taskHandle
    $taskSelfTimes=Get-HeldTimes $taskSelfHandle
    $taskPointMs=$taskClock.Elapsed.TotalMilliseconds
    $taskQueryMs=$taskPointMs-$taskQueryStart
    $taskBirthChecks++
    if($taskTimes.birth -ne $taskBound.birth){throw 'Held target birth no longer matches; CPU deltas rejected.'}
    if($taskSelfTimes.birth -ne $taskSamplerBirth){throw 'Sampler birth mismatch.'}
    if(-not $taskTimes.alive){$taskStatus='TARGET_EXITED_INCOMPLETE';break}
    $taskPoint=[pscustomobject]@{atUtc=[DateTime]::UtcNow.ToString('o');wallMs=$taskPointMs;kernel=$taskTimes.kernel;user=$taskTimes.user;selfKernel=$taskSelfTimes.kernel;selfUser=$taskSelfTimes.user;completedWorkWallMs=$taskCompletedCost}
    if($null -ne $taskLast -and ($taskPoint.kernel -lt $taskLast.kernel -or $taskPoint.user -lt $taskLast.user -or $taskPoint.selfKernel -lt $taskLast.selfKernel -or $taskPoint.selfUser -lt $taskLast.selfUser)){throw 'Negative CPU counter delta; refusing to substitute zero.'}
    if($null -eq $taskFirst){$taskFirst=$taskPoint}
    $taskLast=$taskPoint;$taskQueue.Add($taskPoint)
    while($taskQueue.Count -gt 1 -and [Math]::Abs(($taskPoint.wallMs-$taskQueue[1].wallMs)-$taskWindowMs) -le [Math]::Abs(($taskPoint.wallMs-$taskQueue[0].wallMs)-$taskWindowMs)){$taskQueue.RemoveAt(0)}
    $taskWindow=$null
    $taskBase=$taskQueue[0]
    if(($taskPoint.wallMs-$taskBase.wallMs) -ge ($taskWindowMs-$taskIntervalMs/2.0)){
      $taskWindowWall=$taskPoint.wallMs-$taskBase.wallMs
      $taskDeltaMs=(($taskPoint.kernel-$taskBase.kernel)+($taskPoint.user-$taskBase.user))/10000.0
      $taskSelfDeltaMs=(($taskPoint.selfKernel-$taskBase.selfKernel)+($taskPoint.selfUser-$taskBase.selfUser))/10000.0
      $taskCpu=Get-CpuPercentFromDeltas $taskDeltaMs $taskWindowWall $taskLogical
      $taskSamplerCpu=Get-CpuPercentFromDeltas $taskSelfDeltaMs $taskWindowWall $taskLogical
      $taskWindow=[ordered]@{fromUtc=$taskBase.atUtc;toUtc=$taskPoint.atUtc;actualWallMs=$taskWindowWall;targetCpuMs=$taskDeltaMs;cpuPercentMachine=$taskCpu;samplerCpuMs=$taskSelfDeltaMs;samplerCpuPercentMachine=$taskSamplerCpu;collectorCompletedWorkWallMs=($taskPoint.completedWorkWallMs-$taskBase.completedWorkWallMs);collectorWorkBoundary='completed iterations through previous sample; at most one sample lag';atOrAbove01Percent=($taskCpu -ge $taskThresholdPercent)}
      $taskWindowCount++
      if($taskCpu -ge $taskThresholdPercent){$taskThresholdCount++}
      if($null -eq $taskMax -or $taskCpu -gt $taskMax.cpuPercentMachine){$taskMax=$taskWindow}
      if($null -eq $taskMaxSampler -or $taskSamplerCpu -gt $taskMaxSampler.samplerCpuPercentMachine){$taskMaxSampler=$taskWindow}
    }
    $taskSamples++
    $taskRow=[ordered]@{sample=$taskSamples;atUtc=$taskPoint.atUtc;wallMs=$taskPoint.wallMs;pid=$taskTargetId;birthFileTimeUtc100ns=([string]$taskTimes.birth);birthVerified=$true;kernel100ns=$taskPoint.kernel;user100ns=$taskPoint.user;samplerKernel100ns=$taskPoint.selfKernel;samplerUser100ns=$taskPoint.selfUser;nativeQueriesWallMs=$taskQueryMs;previousIterationWallCostMs=$taskPreviousCost;window=$taskWindow}
    $taskWriter.WriteLine(($taskRow|ConvertTo-Json -Depth 7 -Compress))
    if($taskSamples%16 -eq 0){$taskWriter.Flush()}
    if($taskPointMs -ge $taskNextProgress){Write-Output ('Observed '+[Math]::Round($taskPointMs/1000,1)+'s; windows >=0.1%: '+$taskThresholdCount+'; maximum: '+$(if($null -eq $taskMax){'not enough data'}else{[Math]::Round($taskMax.cpuPercentMachine,6).ToString()+'%'}));$taskNextProgress+=30000.0}
    $taskPreviousCost=$taskClock.Elapsed.TotalMilliseconds-$taskWorkStart
    $taskLoopCosts.Add($taskPreviousCost);$taskQueryCosts.Add($taskQueryMs);$taskCompletedCost+=$taskPreviousCost
    $taskNextDue+=$taskIntervalMs
    $taskAfterWork=$taskClock.Elapsed.TotalMilliseconds
    if($taskNextDue -le $taskAfterWork){$taskSkipped=[int][Math]::Floor(($taskAfterWork-$taskNextDue)/$taskIntervalMs)+1;$taskMissedSlots+=$taskSkipped;$taskNextDue+=$taskSkipped*$taskIntervalMs}
  }
  if($taskStatus -ceq 'OBSERVING'){$taskStatus='DURATION_COMPLETE_OBSERVATION_ONLY'}
} catch {
  $taskFailure=$_.Exception.Message
  $taskStatus=if($null -eq $taskRunPath){'REJECTED_BEFORE_SAMPLING'}else{'FAILED_OR_IDENTITY_LOST_INCOMPLETE'}
} finally {
  try {
  if($null -ne $taskWriter){$taskWriter.Flush();$taskWriter.Dispose()}
  if($null -ne $taskRunPath){
    $taskFinalNativeBirthVerified=$false;$taskFinalExitFileTime=$null;$taskFinalNativeError=$null;$taskFinalWaitStatus=$null;$taskFinalAliveVerified=$false
    try {
      $taskFinalNative=Get-HeldTimes $taskHandle
      $taskBirthChecks++
      $taskFinalNativeBirthVerified=($taskFinalNative.birth -eq $taskBound.birth)
      $taskFinalExitFileTime=[string]$taskFinalNative.exit
      $taskFinalWaitStatus=$taskFinalNative.waitStatus;$taskFinalAliveVerified=$taskFinalNative.alive
      if(-not $taskFinalNativeBirthVerified){throw 'Final held target birth mismatch.'}
      if(-not $taskFinalNative.alive -and $taskStatus -ceq 'DURATION_COMPLETE_OBSERVATION_ONLY'){$taskStatus='TARGET_EXITED_AT_FINAL_CHECK_INCOMPLETE'}
    } catch {$taskFinalNativeError=$_.Exception.Message;if($null -eq $taskFailure){$taskFailure=$taskFinalNativeError;$taskStatus='FINAL_NATIVE_IDENTITY_UNCONFIRMED'}}
    $taskFinalDiskHash=$null;$taskFinalHashError=$null
    try{$taskFinalDiskHash=(Get-CandidateFileSha256 $taskImagePath)}catch{$taskFinalHashError=$_.Exception.Message}
    if($taskFinalDiskHash -cne $taskExpectedExeHash -and $null -eq $taskFailure){$taskStatus='FINAL_DISK_IDENTITY_UNCONFIRMED';$taskFailure='Final disk EXE hash missing or different; observation is not candidate acceptance.'}
    $taskSelfProcess.Refresh();$taskSelfOverallMs=$taskSelfProcess.TotalProcessorTime.TotalMilliseconds-$taskSelfCpuAtEntry
    $taskCoverageWall=if($null -ne $taskFirst -and $null -ne $taskLast){$taskLast.wallMs-$taskFirst.wallMs}else{0.0}
    $taskTargetTotalMs=$null;$taskSamplerSteadyMs=$null;$taskAverage=$null;$taskSamplerAverage=$null
    if($taskCoverageWall -gt 0){$taskTargetTotalMs=(($taskLast.kernel-$taskFirst.kernel)+($taskLast.user-$taskFirst.user))/10000.0;$taskSamplerSteadyMs=(($taskLast.selfKernel-$taskFirst.selfKernel)+($taskLast.selfUser-$taskFirst.selfUser))/10000.0;$taskAverage=Get-CpuPercentFromDeltas $taskTargetTotalMs $taskCoverageWall $taskLogical;$taskSamplerAverage=Get-CpuPercentFromDeltas $taskSamplerSteadyMs $taskCoverageWall $taskLogical}
    $taskSummary=[ordered]@{schemaVersion=1;status=$taskStatus;failure=$taskFailure;outputPath=$taskRunPath;startedAtUtc=$taskBootUtc;endedAtUtc=[DateTime]::UtcNow.ToString('o');pid=$taskTargetId;birthFileTimeUtc100ns=([string]$taskBound.birth);exeSha256=$taskDiskHash;finalDiskExeSha256=$taskFinalDiskHash;finalHashError=$taskFinalHashError;expectedExeSha256=$taskExpectedExeHash;durationRequestedSeconds=$DurationSeconds;coveredActualWallMs=$taskCoverageWall;samples=$taskSamples;slidingWindows=$taskWindowCount;slidingWindowThresholdPercent=$taskThresholdPercent;windowsAtOrAbove01Percent=$taskThresholdCount;maxCpuPercentMachine=$(if($null -eq $taskMax){$null}else{$taskMax.cpuPercentMachine});maxWindow=$taskMax;targetTotalCpuMs=$taskTargetTotalMs;targetAverageCpuPercentMachine=$taskAverage;samplerSteadyCpuMs=$taskSamplerSteadyMs;samplerSteadyAverageCpuPercentMachine=$taskSamplerAverage;samplerMaxWindow=$taskMaxSampler;samplerWholeProcessCpuMs=$taskSelfOverallMs;samplerWholeWallMs=$taskWholeWall.Elapsed.TotalMilliseconds;samplerCpuScope='Collector process through final identity checks; compiler child and summary sorting/serialization/console exit CPU not measured; startup/final hash separate from steady interval';iterationWallCost=Get-CostStatistics $taskLoopCosts;nativeQueriesWallCost=Get-CostStatistics $taskQueryCosts;missed250msSlots=$taskMissedSlots;birthVerifications=$taskBirthChecks;finalNativeBirthVerified=$taskFinalNativeBirthVerified;finalNativeExitFileTimeUtc100ns=$taskFinalExitFileTime;finalNativeWaitStatus=$taskFinalWaitStatus;finalNativeLivenessVerified=$taskFinalAliveVerified;livenessAuthority='HELD_SYNCHRONIZABLE_HANDLE_WAIT';finalNativeIdentityError=$taskFinalNativeError;windowSelection='baseline nearest 1000ms, minimum 875ms at startup; actual denominator retained';cimQueries=1;logicalProcessors=$taskLogical;rootOnly=$true;webViewOrChildCpuIncluded=$false;threadOrPdhSampling=$false;wprStarted=$false;applicationsControlled=$false;candidateCpuAcceptance=$false;scenarioBackgroundInstallOrUserStateAutomaticallyVerified=$false;limitations=@('Sliding windows overlap; count is not count of independent spikes.','CPU counters have finite granularity; measured zero ticks do not prove zero observer cost.','Stopwatch actual intervals used, no assumed one-second denominator.','Window work-wall cost lags by at most one sample; full iteration costs include JSON serialization/file writes.','Root process only; child/WebView CPU excluded.','User scenario/background state and installed-vs-portable operation require user confirmation; no <=0.1% formal verdict.')}
    Save-CandidateJson (Join-Path $taskRunPath 'SUMMARY.json') $taskSummary
  }
  } finally {
  if($taskHandle -ne [IntPtr]::Zero){[void][YmccCandidateCpuReadOnlyV1]::CloseHandle($taskHandle)}
  $taskSelfProcess.Dispose()
  }
}
if($null -ne $taskSummary){$taskSummary|ConvertTo-Json -Depth 10}else{[ordered]@{status=$taskStatus;error=$taskFailure;samplingStarted=$false;candidateCpuAcceptance=$false}|ConvertTo-Json -Depth 5}
if($null -ne $taskFailure){exit 1}
