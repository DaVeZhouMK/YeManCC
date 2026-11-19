<# Development-only passive HWND/foreground scene evidence. Never starts or controls the target app. #>
#requires -Version 7.0
[CmdletBinding(DefaultParameterSetName='Observe')]
param(
 [Parameter(Mandatory=$true,ParameterSetName='Observe')][uint32]$ExpectedProcessId,
 [Parameter(Mandatory=$true,ParameterSetName='Observe')][ValidatePattern('\A[1-9][0-9]{0,18}\z')][string]$ExpectedBirthFileTime100ns,
 [Parameter(Mandatory=$true,ParameterSetName='Observe')][string]$ExpectedImagePath,
 [Parameter(Mandatory=$true,ParameterSetName='Observe')][ValidatePattern('\A[0-9a-fA-F]{64}\z')][string]$ExpectedExeSha256,
 [Parameter(Mandatory=$true,ParameterSetName='Observe')][ValidateRange(1,[long]::MaxValue)][long]$ExpectedWindowHandle,
 [Parameter(ParameterSetName='Observe')][switch]$RequireHiddenBackground,
 [Parameter(Mandatory=$true)][string]$EvidenceRoot,
 [Parameter(ParameterSetName='Observe')][ValidateRange(10,600)][int]$DurationSeconds=300,
 [Parameter(Mandatory=$true,ParameterSetName='SelfTest')][switch]$SelfTest
)
$ErrorActionPreference='Stop'
$taskWall=[Diagnostics.Stopwatch]::StartNew()
$taskRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(Test-Path -LiteralPath $taskRoot){throw 'Fresh evidence directory required'}
[IO.Directory]::CreateDirectory($taskRoot)|Out-Null
$taskUtf8=[Text.UTF8Encoding]::new($false)
function Save-NewJson([string]$Path,$Value){
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{$bytes=$taskUtf8.GetBytes(($Value|ConvertTo-Json -Depth 12)+[Environment]::NewLine);$stream.Write($bytes);$stream.Flush($true)}finally{$stream.Dispose()}
}
$taskSource=Join-Path $PSScriptRoot 'WindowScene.cs'
$taskSourceHash=(Get-FileHash -LiteralPath $taskSource).Hash.ToLowerInvariant()
$taskScriptHash=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
$taskSourceText=[IO.File]::ReadAllText($taskSource)
$taskTrace=$null;$taskFailure=$null;$taskTest=$null;$taskIdentity=$null;$taskClosingExeHash=$null
$taskOwn=[Diagnostics.Process]::GetCurrentProcess();$taskCpuStart=$taskOwn.TotalProcessorTime.TotalMilliseconds
$taskCaptureCpu=$null;$taskInitialScene=$null
try{
 if($SelfTest){$taskSourceText="#define YMCC_SCENE_SELFTEST`n"+$taskSourceText}
 Add-Type -TypeDefinition $taskSourceText
 if($SelfTest){
  $taskTest=[Ymcc.CpuScene.V1.Observer]::SelfTest()
  if($taskTest.Status -cne 'PASS'){throw 'Scene self-test did not pass'}
 }else{
  $taskIdentity=[Ymcc.CpuScene.V1.Observer]::ReadIdentity($ExpectedProcessId)
  if($taskIdentity.Error -or !$taskIdentity.Alive -or !$taskIdentity.HandleCloseAttempted -or !$taskIdentity.HandleClosed -or
     $taskIdentity.BirthFileTime -cne $ExpectedBirthFileTime100ns -or
     ![string]::Equals([IO.Path]::GetFullPath($taskIdentity.ImagePath),[IO.Path]::GetFullPath($ExpectedImagePath),[StringComparison]::OrdinalIgnoreCase)){
   throw 'Held PID/birth/image binding rejected'
  }
  if((Get-FileHash -LiteralPath $taskIdentity.ImagePath).Hash.ToLowerInvariant() -cne $ExpectedExeSha256.ToLowerInvariant()){throw 'Initial disk EXE differs'}
  if($RequireHiddenBackground){
   $taskInitialScene=[Ymcc.CpuScene.V1.Observer]::ReadWindowSample($ExpectedProcessId,$ExpectedWindowHandle)
   Save-NewJson (Join-Path $taskRoot 'INITIAL-SCENE.json') $taskInitialScene
   if(!$taskInitialScene.WindowExists -or $taskInitialScene.WindowProcessId -ne $ExpectedProcessId -or
      $taskInitialScene.Visible -or $taskInitialScene.ForegroundProcessId -eq $ExpectedProcessId){
    throw 'Requested hidden-background scene rejected before observer-ready identity; no window control performed'
   }
  }
  Save-NewJson (Join-Path $taskRoot 'IDENTITY.json') ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');target=$taskIdentity;expectedWindowHandle=$ExpectedWindowHandle;expectedExeSha256=$ExpectedExeSha256.ToLowerInvariant();durationSeconds=$DurationSeconds;sourcePath=$taskSource;sourceSha256=$taskSourceHash;scriptSha256=$taskScriptHash;selfTest=$false;targetApplicationsControlled=$false;inputDataOrWindowTitlesCollected=$false})
  $taskOwn.Refresh();$taskCpuBefore=$taskOwn.TotalProcessorTime.TotalMilliseconds
  $taskTrace=[Ymcc.CpuScene.V1.Observer]::Capture($ExpectedProcessId,$ExpectedBirthFileTime100ns,$taskIdentity.ImagePath,$ExpectedWindowHandle,$DurationSeconds*1000)
  $taskOwn.Refresh();$taskCaptureCpu=$taskOwn.TotalProcessorTime.TotalMilliseconds-$taskCpuBefore
  $taskClosingExeHash=(Get-FileHash -LiteralPath $taskIdentity.ImagePath).Hash.ToLowerInvariant()
  if($taskTrace.Status -cne 'SCENE_COLLECTED' -or !$taskTrace.HandleClosed -or @($taskTrace.HookClosed|Where-Object {!$_}).Count){throw ('Scene observation incomplete: '+$taskTrace.Status+' '+$taskTrace.Error)}
  if($taskClosingExeHash -cne $ExpectedExeSha256.ToLowerInvariant()){throw 'Disk image changed during scene capture'}
 }
 if((Get-FileHash -LiteralPath $taskSource).Hash.ToLowerInvariant() -cne $taskSourceHash -or
    (Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant() -cne $taskScriptHash){throw 'Observer source changed during execution'}
}catch{$taskFailure=$_.Exception.ToString()}
if($taskTrace){
 $stream=[IO.File]::Open((Join-Path $taskRoot 'SCENE.jsonl'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 $writer=[IO.StreamWriter]::new($stream,$taskUtf8)
 try{
  for($index=0;$index -lt $taskTrace.Count;$index++){
   $s=$taskTrace.Samples[$index]
   $row=[ordered]@{sequence=$index;kind=$s.Kind;eventId=$s.EventId;eventThreadId=$s.EventThreadId;eventTimeMs32=$s.EventTimeMs;objectId=$s.ObjectId;childId=$s.ChildId;beforeQpc=$s.BeforeQpc.ToString();afterQpc=$s.AfterQpc.ToString();foregroundWindow=$s.ForegroundWindow.ToString();foregroundPid=$s.ForegroundProcessId;windowPid=$s.WindowProcessId;windowTid=$s.WindowThreadId;windowExists=$s.WindowExists;visible=$s.Visible;minimized=$s.Minimized}
   $writer.WriteLine(($row|ConvertTo-Json -Compress))
  }
 }finally{$writer.Dispose();$stream.Dispose()}
}
$taskOwn.Refresh()
$taskResult=[ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');status=$(if($taskFailure){'SCENE_OBSERVATION_NOT_ACCEPTED'}elseif($SelfTest){'SCENE_OWNED_SELFTEST_PASSED'}else{'PASSIVE_SCENE_COLLECTED_NOT_CPU_ACCEPTANCE'});error=$taskFailure;selfTestResult=$taskTest;identity=$taskIdentity;hiddenBackgroundRequiredAtEntry=[bool]$RequireHiddenBackground;initialScene=$taskInitialScene;traceStatus=$(if($taskTrace){$taskTrace.Status}else{$null});sampleCount=$(if($taskTrace){$taskTrace.Count}else{0});frequency=$(if($taskTrace){$taskTrace.Frequency}else{$null});startedQpc=$(if($taskTrace){$taskTrace.StartedQpc.ToString()}else{$null});endedQpc=$(if($taskTrace){$taskTrace.EndedQpc.ToString()}else{$null});anchorBeforeQpc=$(if($taskTrace){$taskTrace.AnchorBeforeQpc.ToString()}else{$null});anchorAfterQpc=$(if($taskTrace){$taskTrace.AnchorAfterQpc.ToString()}else{$null});anchorUtcTicks=$(if($taskTrace){$taskTrace.AnchorUtcTicks.ToString()}else{$null});capacityExceeded=$(if($taskTrace){$taskTrace.CapacityExceeded}else{$false});rootHandleClosed=$(if($taskTrace){$taskTrace.HandleClosed}else{$null});hookCloseAttempted=$(if($taskTrace){$taskTrace.HookCloseAttempted}else{$null});hookClosed=$(if($taskTrace){$taskTrace.HookClosed}else{$null});diskExeClosingSha256=$taskClosingExeHash;observerCaptureCpuMs=$taskCaptureCpu;observerTotalCpuMs=$taskOwn.TotalProcessorTime.TotalMilliseconds-$taskCpuStart;entryWallMs=$taskWall.Elapsed.TotalMilliseconds;sourcePath=$taskSource;sourceSha256=$taskSourceHash;scriptSha256=$taskScriptHash;noJsonOrFileIOInsideCapture=$true;applicationDeviceOrSleepControlled=$false;windowTitlesOrInputPayloadCollected=$false;cpuAcceptance=$false;limits='Passive selected-HWND/foreground observations only. No continuous hardware/input-state proof, lost-event proof, exact CPU attribution, or automatic background acceptance. EventTimeMs32 and callback QPC are distinct clocks; callbacks can be delayed.'}
$taskOwn.Dispose()
Save-NewJson (Join-Path $taskRoot 'RESULT.json') $taskResult
$taskResult|ConvertTo-Json -Depth 12
if($taskFailure){exit 1}
