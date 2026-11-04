[CmdletBinding()]
param(
 [ValidateSet('Preflight','Capture')][string]$Mode='Preflight',
 [ValidateRange(0,2147483647)][int]$TargetProcessId=0,
 [ValidateSet('YeManCC.exe','YeManInputHost.exe')][string]$TargetImageName='YeManCC.exe',
 [switch]$FullDuration,
 [ValidateRange(0.01,100)][double]$TriggerPercent=0.30,
 [ValidateRange(10,600)][int]$MaximumSeconds=360,
 [ValidateRange(1,10)][int]$TailSeconds=1,
 [switch]$FileMode,
 [ValidateSet('Samples','Scheduling','Rog')][string]$ProfileMode='Samples',
 [ValidateRange(0,600)][int]$WarmupSeconds=0,
 [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$ExpectedIdentityPath,
 [string]$ExpectedBirthFileTime='',
 [string]$ExpectedImagePath='',
 [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$EvidenceRoot,
 [string]$OutputRoot=''
)
$ErrorActionPreference='Stop'
$taskEvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(-not [IO.Directory]::Exists($taskEvidenceRoot)){throw 'EvidenceRoot must be an existing development evidence directory.'}
$taskOutputRoot=if($OutputRoot){[IO.Path]::GetFullPath($OutputRoot)}else{Join-Path $taskEvidenceRoot 'StackCapture'}
$taskEvidencePrefix=$taskEvidenceRoot.TrimEnd([char[]]'\/')+[IO.Path]::DirectorySeparatorChar
if($taskOutputRoot -ine $taskEvidenceRoot -and -not $taskOutputRoot.StartsWith($taskEvidencePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'OutputRoot must stay within the explicit EvidenceRoot.'}
if($TargetProcessId -eq 0){
 $taskNativeRows=@(Get-CimInstance Win32_Process -Filter "Name='$TargetImageName'" -ErrorAction Stop)
 $taskNativeIds=@($taskNativeRows|ForEach-Object{[int]$_.ProcessId})
 $taskNativeRoots=@($taskNativeRows|Where-Object{$taskNativeIds -notcontains [int]$_.ParentProcessId})
 if($taskNativeRoots.Count -ne 1){throw 'Cannot select exactly one target root; pass -TargetProcessId explicitly'}
 $TargetProcessId=[int]$taskNativeRoots[0].ProcessId
}
$taskIsAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$taskWpr=(Get-Command wpr.exe -ErrorAction Stop).Source
$taskProfileName=if($ProfileMode -eq 'Rog'){'RogCpu'}elseif($ProfileMode -eq 'Scheduling'){'YmccCpuScheduling'}else{'YmccCpu'}
$taskProfilePath=if($ProfileMode -eq 'Rog'){Join-Path $PSScriptRoot 'rog-field/RogCpu.wprp'}else{Join-Path $PSScriptRoot ($taskProfileName+'.wprp')}
$taskProfileSelector=$taskProfilePath+'!'+$taskProfileName+'.Verbose'
if(!(Test-Path -LiteralPath $taskProfilePath)){throw 'Missing bounded WPR profile'}
Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class YmccStackCaptureReadOnly {
 [DllImport("kernel32.dll",SetLastError=true)]public static extern IntPtr OpenProcess(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern bool QueryProcessCycleTime(IntPtr handle,out ulong cycles);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref uint size);
 [DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);
}
"@
$taskProcessHandle=[YmccStackCaptureReadOnly]::OpenProcess(0x101000,$false,[uint32]$TargetProcessId)
if($taskProcessHandle -eq [IntPtr]::Zero){throw "Cannot query target PID $TargetProcessId; Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
function Read-TargetTimes {
 $taskCounterBeginQpc=[Diagnostics.Stopwatch]::GetTimestamp().ToString()
 [long]$taskBirth=0;[long]$taskExit=0;[long]$taskKernel=0;[long]$taskUser=0
 if(![YmccStackCaptureReadOnly]::GetProcessTimes($taskProcessHandle,[ref]$taskBirth,[ref]$taskExit,[ref]$taskKernel,[ref]$taskUser)){throw "GetProcessTimes failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
 [uint64]$taskCycles=0;$taskCyclesKnown=$false;$taskCycleError=$null
 try{$taskCyclesKnown=[YmccStackCaptureReadOnly]::QueryProcessCycleTime($taskProcessHandle,[ref]$taskCycles);if(-not $taskCyclesKnown){$taskCycleError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}}catch{$taskCycleError='QueryProcessCycleTime-call-failed'}
 $taskWait=[YmccStackCaptureReadOnly]::WaitForSingleObject($taskProcessHandle,0)
 if($taskWait -ne 0 -and $taskWait -ne 258){throw "Process wait-state query failed: status=$taskWait; Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
 if($taskWait -eq 0 -and -not [YmccStackCaptureReadOnly]::GetProcessTimes($taskProcessHandle,[ref]$taskBirth,[ref]$taskExit,[ref]$taskKernel,[ref]$taskUser)){throw 'Exited process diagnostic re-read failed'}
 [ordered]@{birthFileTime=$taskBirth.ToString();exitFileTime=$taskExit.ToString();cpu100ns=$taskKernel+$taskUser;alive=($taskWait -eq 258);waitStatus=$taskWait;exitFileTimeDefined=($taskWait -eq 0);livenessSource='HELD_SYNCHRONIZABLE_HANDLE_WAIT';cpuCyclesKnown=$taskCyclesKnown;cpuCycles=$(if($taskCyclesKnown){$taskCycles.ToString()}else{$null});cpuCyclesError=$taskCycleError;counterReadBeginQpc=$taskCounterBeginQpc;counterReadEndQpc=[Diagnostics.Stopwatch]::GetTimestamp().ToString()}
}
function Write-Json($taskValue,[string]$taskPath){$taskValue|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $taskPath -Encoding utf8}
$taskOwnStarted=$false;$taskRunPath=$null;$taskWriter=$null;$taskFailure=$null;$taskTraceSaved=$false;$taskStopExitCode=$null
try{
 $taskInitial=Read-TargetTimes
 $taskImageText=[Text.StringBuilder]::new(32768);[uint32]$taskImageSize=32768
 if(![YmccStackCaptureReadOnly]::QueryFullProcessImageName($taskProcessHandle,0,$taskImageText,[ref]$taskImageSize)){throw 'Cannot bind target image'}
 $taskImage=$taskImageText.ToString()
 if([IO.Path]::GetFileName($taskImage) -ine $TargetImageName){throw "Target image must be $TargetImageName"}
 if(-not $taskInitial.alive){throw 'Target already exited before profiling'}
 if($ExpectedBirthFileTime -and $taskInitial.birthFileTime -cne $ExpectedBirthFileTime){throw 'Expected native root birth differs; no recording started'}
 if($ExpectedImagePath -and [IO.Path]::GetFullPath($taskImage) -ine [IO.Path]::GetFullPath($ExpectedImagePath)){throw 'Expected installed image path differs; no recording started'}
 $taskExpectedIdentity=$null
 if($ExpectedIdentityPath){
  $taskExpectedIdentity=Get-Content -LiteralPath $ExpectedIdentityPath -Raw|ConvertFrom-Json
  if(([string]$taskExpectedIdentity.exeSha256) -notmatch '^[0-9a-fA-F]{64}$'){throw 'Expected full EXE hash absent'}
  if((Get-FileHash -LiteralPath $taskImage -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskExpectedIdentity.exeSha256.ToLowerInvariant()){throw 'Expected EXE differs; no recording started'}
 }

 $taskRunId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
 $taskRunPath=Join-Path $taskOutputRoot $taskRunId
 [IO.Directory]::CreateDirectory($taskOutputRoot)|Out-Null
 $null=New-Item -ItemType Directory -Path $taskRunPath
 $taskProbe=[ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');mode=$Mode;elevatedAdministrator=$taskIsAdmin;wpr=$taskWpr;pid=$TargetProcessId;startUtc=[DateTime]::FromFileTimeUtc([long]$taskInitial.birthFileTime).ToString('o');birthFileTime=$taskInitial.birthFileTime;imagePath=$taskImage;diskImageSha256=(Get-FileHash -LiteralPath $taskImage -Algorithm SHA256).Hash.ToLowerInvariant();logicalProcessors=[Environment]::ProcessorCount;profile=($taskProfileName+'.Verbose.'+$(if($FileMode){'File'}else{'Memory'}));profileMode=$ProfileMode;schedulingEvents=($ProfileMode -in @('Scheduling','Rog'));interruptEvents=($ProfileMode -in @('Scheduling','Rog'));contextSwitchStackWalk=$false;profilePath=$taskProfilePath;profileSha256=(Get-FileHash -LiteralPath $taskProfilePath -Algorithm SHA256).Hash.ToLowerInvariant();requestedRingBufferBytes=$(if($ProfileMode -eq 'Rog'){83886080}else{67108864});clrJitGcEvents=($ProfileMode -eq 'Rog');wprStatus=@(& $taskWpr -status 2>&1|ForEach-Object{[string]$_});profileDetails=@(& $taskWpr -profiledetails $taskProfileSelector 2>&1|ForEach-Object{[string]$_});privileges=@(& whoami.exe /priv 2>&1|ForEach-Object{[string]$_});requiresSourceMatchedPrivateSymbols=$true;appOrDeviceControlled=$false;traceStarted=$false}
 if($Mode -eq 'Preflight'){
  Write-Json $taskProbe (Join-Path $taskRunPath 'WPR-PREFLIGHT.json')
  [ordered]@{status='READ_ONLY_PREFLIGHT';elevatedAdministrator=$taskIsAdmin;pid=$TargetProcessId;profile=$taskProbe.profile;captureReady=$taskIsAdmin;output=(Join-Path $taskRunPath 'WPR-PREFLIGHT.json')}|ConvertTo-Json
  return
 }
 if(!$taskIsAdmin){throw 'Windows CPU stack profiling requires an elevated Administrator PowerShell here; no elevation or recording was attempted. Run this same script with -Mode Capture from an Administrator PowerShell.'}
 $taskInstance='YMCC-CPU-'+$taskRunId
 $taskProbe['instanceName']=$taskInstance;$taskProbe['triggerPercent']=$TriggerPercent;$taskProbe['maximumSeconds']=$MaximumSeconds;$taskProbe['tailSeconds']=$TailSeconds
 Write-Json $taskProbe (Join-Path $taskRunPath 'IDENTITY.json')
 $taskProbe['loggingMode']=if($FileMode){'File'}else{'Memory'}
 $taskProbe['warmupSeconds']=$WarmupSeconds
 $taskProbe['targetImageName']=$TargetImageName
 $taskProbe['fullDuration']=[bool]$FullDuration
 $taskProbe['expectedIdentityPath']=$ExpectedIdentityPath
 $taskProbe['expectedBirthFileTime']=$ExpectedBirthFileTime
 Write-Json $taskProbe (Join-Path $taskRunPath 'IDENTITY.json')
 $taskStartArgs=@('-start',$taskProfileSelector,'-instancename',$taskInstance)
 if($FileMode){$taskStartArgs+='-filemode'}
 $taskStartOutput=@(& $taskWpr @taskStartArgs 2>&1|ForEach-Object{[string]$_});$taskStartExitCode=$LASTEXITCODE
 Write-Json ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');exitCode=$taskStartExitCode;output=$taskStartOutput;command=($taskStartArgs -join ' ');memoryMode=(-not $FileMode);fileMode=[bool]$FileMode}) (Join-Path $taskRunPath 'WPR-START.json')
 if($taskStartExitCode -ne 0){throw "WPR start failed: $taskStartExitCode; no stop/cancel will be sent"}
 $taskOwnStarted=$true
 $taskClock=[Diagnostics.Stopwatch]::StartNew();$taskObserver=[Diagnostics.Process]::GetCurrentProcess();$taskObserverCpuStart=$taskObserver.TotalProcessorTime.TotalMilliseconds
 $taskWriter=[IO.StreamWriter]::new((Join-Path $taskRunPath 'CPU.jsonl'),$false,[Text.UTF8Encoding]::new($false))
 $taskSamples=[Collections.Generic.Queue[object]]::new();$taskTrigger=$null;$taskReason=if($FullDuration){'FIXED_DURATION'}else{'DEADLINE_WITHOUT_THRESHOLD'};$taskSequence=0
 while($taskClock.Elapsed.TotalSeconds -lt $MaximumSeconds){
  $taskTimes=Read-TargetTimes;$taskNow=$taskClock.Elapsed.TotalMilliseconds
  if($taskTimes.birthFileTime -ne $taskInitial.birthFileTime){throw 'Target birth changed'}
  if(-not $taskTimes.alive){$taskReason='TARGET_EXITED';break}
  $taskRow=[ordered]@{sample=$taskSequence;atUtc=[DateTime]::UtcNow.ToString('o');qpc=[Diagnostics.Stopwatch]::GetTimestamp().ToString();qpcFrequency=[Diagnostics.Stopwatch]::Frequency;wallMs=$taskNow;pid=$TargetProcessId;birthFileTime=$taskTimes.birthFileTime;cpu100ns=$taskTimes.cpu100ns;cpuCyclesKnown=$taskTimes.cpuCyclesKnown;cpuCycles=$taskTimes.cpuCycles;cpuCyclesError=$taskTimes.cpuCyclesError;counterReadBeginQpc=$taskTimes.counterReadBeginQpc;counterReadEndQpc=$taskTimes.counterReadEndQpc;cpuPercentMachine1s=$null;windowMs=$null}
  $taskSamples.Enqueue($taskRow)
  while($taskSamples.Count -gt 5){$null=$taskSamples.Dequeue()}
  if($taskSamples.Count -eq 5){$taskOld=$taskSamples.Peek();$taskWall=$taskNow-$taskOld.wallMs;$taskDelta=[long]$taskTimes.cpu100ns-[long]$taskOld.cpu100ns;if($taskWall -gt 0 -and $taskDelta -ge 0){$taskRow.cpuPercentMachine1s=$taskDelta/10000.0/$taskWall/[Environment]::ProcessorCount*100;$taskRow.windowMs=$taskWall}}
  $taskWriter.WriteLine(($taskRow|ConvertTo-Json -Compress));$taskWriter.Flush()
  if(-not $FullDuration -and $null -eq $taskTrigger -and $taskNow -ge $WarmupSeconds*1000.0 -and $null -ne $taskRow.cpuPercentMachine1s -and $taskRow.cpuPercentMachine1s -ge $TriggerPercent){$taskTrigger=[ordered]@{atUtc=$taskRow.atUtc;sample=$taskSequence;wallMs=$taskNow;windowMs=$taskRow.windowMs;cpuPercentMachine=$taskRow.cpuPercentMachine1s};$taskReason='CPU_THRESHOLD_CAPTURED';Write-Json $taskTrigger (Join-Path $taskRunPath 'TRIGGER.json')}
  if($null -ne $taskTrigger -and $taskNow-$taskTrigger.wallMs -ge $TailSeconds*1000){break}
  ++$taskSequence;Start-Sleep -Milliseconds 250
 }
 $taskFinalIdentity=Read-TargetTimes
 $taskFinalImageSha256=(Get-FileHash -LiteralPath $taskImage -Algorithm SHA256).Hash.ToLowerInvariant()
 $taskObserver.Refresh();$taskCollectorMs=$taskObserver.TotalProcessorTime.TotalMilliseconds-$taskObserverCpuStart
 Write-Json ([ordered]@{finishedUtc=[DateTime]::UtcNow.ToString('o');reason=$taskReason;finalTargetIdentity=$taskFinalIdentity;finalDiskImageSha256=$taskFinalImageSha256;finalHeldBirthVerified=($taskFinalIdentity.birthFileTime -ceq $taskInitial.birthFileTime);finalHeldAliveVerified=$taskFinalIdentity.alive;profileMode=$ProfileMode;sampleCount=$taskSequence+1;elapsedMs=$taskClock.Elapsed.TotalMilliseconds;trigger=$taskTrigger;collectorPid=$taskObserver.Id;collectorCpuMs=$taskCollectorMs;collectorMachineCpuPercent=$taskCollectorMs/$taskClock.Elapsed.TotalMilliseconds/[Environment]::ProcessorCount*100;limitations=@('Own collector CPU excludes ETW kernel/stack-walking and WPR start/stop costs.','Whole-system ETW has observation overhead. Sampled stacks locate running work; context-switch stacks locate scheduling edges.','Memory-mode retention depends on event rate; inspect ETL lost events and peak-window retention before accepting.','A saved ETL is not evidence of source-line resolution until module/PDB identities and sampled frames are checked.');candidateCpuAcceptance=$false}) (Join-Path $taskRunPath 'CAPTURE.json')
}catch{$taskFailure=$_.Exception.Message;throw}
finally{
 if($taskWriter){$taskWriter.Dispose()}
 if($taskOwnStarted){
  $taskTrace=Join-Path $taskRunPath 'YMCC-CPU.etl'
  $taskStopOutput=@(& $taskWpr -stop $taskTrace 'YMCC native idle CPU peak; inspect PID birth and TRIGGER.json' -instancename $taskInstance 2>&1|ForEach-Object{[string]$_});$taskStopExitCode=$LASTEXITCODE
  $taskTraceSaved=$taskStopExitCode -eq 0 -and (Test-Path -LiteralPath $taskTrace)
  Write-Json ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');exitCode=$taskStopExitCode;output=$taskStopOutput;traceSaved=$taskTraceSaved;path=$taskTrace;sha256=if($taskTraceSaved){(Get-FileHash -LiteralPath $taskTrace -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null};onlyOwnInstanceStopped=$taskInstance;failure=$taskFailure;recoveryIfStopFailed="wpr -stop <new-output.etl> -instancename $taskInstance"}) (Join-Path $taskRunPath 'WPR-STOP.json')
 }
 $null=[YmccStackCaptureReadOnly]::CloseHandle($taskProcessHandle)
}
if($Mode -eq 'Capture'){
 if(!$taskTraceSaved){throw "Own WPR stop/save failed ($taskStopExitCode); see WPR-STOP.json. No foreign session was stopped."}
 [ordered]@{status='ETL_SAVED_SOURCE_ANALYSIS_REQUIRED';path=$taskRunPath;trigger=$taskTrigger;appsChanged=$false;candidateCpuAcceptance=$false}|ConvertTo-Json -Depth 5
}
