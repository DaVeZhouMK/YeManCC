<# Development-only explicit PID/birth/path/SHA counter capture. No application control or discovery. #>
#requires -Version 7.0
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][uint32]$ExpectedProcessId,
 [Parameter(Mandatory=$true)][ValidatePattern('\A[1-9][0-9]{0,18}\z')][string]$ExpectedBirthFileTime100ns,
 [Parameter(Mandatory=$true)][string]$ExpectedImagePath,
 [Parameter(Mandatory=$true)][ValidatePattern('\A[0-9a-fA-F]{64}\z')][string]$ExpectedExeSha256,
 [Parameter(Mandatory=$true)][string]$EvidenceRoot,
 [ValidateRange(10,600)][int]$DurationSeconds=90,
 [ValidateRange(100,1000)][int]$IntervalMs=1000
)
$ErrorActionPreference='Stop'
$taskWall=[Diagnostics.Stopwatch]::StartNew()
$taskRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(Test-Path -LiteralPath $taskRoot){throw 'A fresh evidence directory is required'}
[IO.Directory]::CreateDirectory($taskRoot)|Out-Null
$taskUtf8=[Text.UTF8Encoding]::new($false);$taskInvariant=[Globalization.CultureInfo]::InvariantCulture
function Save-NewJson([string]$Path,$Value){$s=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$b=$taskUtf8.GetBytes(($Value|ConvertTo-Json -Depth 16)+[Environment]::NewLine);$s.Write($b);$s.Flush($true)}finally{$s.Dispose()}}
function Qpc-Midpoint($s){return ([bigint]$s.ReadQpcBefore+[Numerics.BigInteger]::Divide(([bigint]$s.ReadQpcAfter-[bigint]$s.ReadQpcBefore),[bigint]2))}
function Join-BufferedCounterSource([string]$BaseText,$Projection) {
 if($Projection.schemaVersion -ne 1 -or $Projection.kind -cne 'DEVELOPMENT_ONLY_SOURCE_PROJECTION' -or $Projection.baseFile -cne 'ReadOnlyCounters.cs' -or @($Projection.replacements).Count -ne 3){throw 'Unsupported buffered source projection'}
 $result=$BaseText
 foreach($replacement in $Projection.replacements){
  $old=[string]$replacement.before;$new=[string]$replacement.after
  if(!$old -or !$new){throw 'Empty projection member'}
  $at=$result.IndexOf($old,[StringComparison]::Ordinal)
  if($at -lt 0 -or $result.IndexOf($old,$at+1,[StringComparison]::Ordinal) -ge 0){throw 'Projection anchor absent or duplicated'}
  $result=$result.Remove($at,$old.Length).Insert($at,$new)
 }
 $reverse=$result
 for($index=$Projection.replacements.Count-1;$index -ge 0;$index--){
  $replacement=$Projection.replacements[$index];$old=[string]$replacement.before;$new=[string]$replacement.after
  $at=$reverse.IndexOf($new,[StringComparison]::Ordinal)
  if($at -lt 0 -or $reverse.IndexOf($new,$at+1,[StringComparison]::Ordinal) -ge 0){throw 'Projection reverse anchor absent or duplicated'}
  $reverse=$reverse.Remove($at,$new.Length).Insert($at,$old)
 }
 if($reverse -cne $BaseText){throw 'Projection changed unselected source bytes'}
 return $result
}
function Hash-BufferedSource([string]$Text) {
 $algorithm=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($algorithm.ComputeHash([Text.UTF8Encoding]::new($false).GetBytes($Text))).Replace('-','').ToLowerInvariant()}finally{$algorithm.Dispose()}
}
$taskSource=Join-Path $PSScriptRoot 'ReadOnlyCounters.cs'
$taskProjectionPath=Join-Path $PSScriptRoot 'BufferedCounterSource.json'
$taskProjectionHash=(Get-FileHash -LiteralPath $taskProjectionPath).Hash.ToLowerInvariant()
$taskCompiledSource=Join-BufferedCounterSource ([IO.File]::ReadAllText($taskSource)) (Get-Content -LiteralPath $taskProjectionPath -Raw -Encoding UTF8|ConvertFrom-Json)
$taskCompiledSourceHash=Hash-BufferedSource $taskCompiledSource
$taskSourceHash=(Get-FileHash -LiteralPath $taskSource).Hash.ToLowerInvariant()
$taskReader=$null;$taskTrace=$null;$taskError=$null;$taskBefore=$null;$taskAfter=$null;$taskActualPath=$null;$taskClosingExeHash=$null;$taskReadClose=$false;$taskCloseAttempted=$false;$taskCloseError=$null
$taskOwn=[Diagnostics.Process]::GetCurrentProcess();$taskOwn.Refresh();$taskOwnCpuAtEntry=$taskOwn.TotalProcessorTime.TotalMilliseconds
$taskCaptureOwnCpuBefore=$null;$taskCaptureOwnCpuAfter=$null
try{
 Add-Type -TypeDefinition $taskCompiledSource
 $taskReader=[Ymcc.RogField.V1.ReadOnlyCounters]::Open($ExpectedProcessId)
 $taskBefore=$taskReader.Read()
 if($taskBefore.Status -cne 'OK' -or $taskBefore.CreationFileTime100ns -cne $ExpectedBirthFileTime100ns){throw 'Exact held PID/birth/liveness binding rejected'}
 $taskActualPath=[IO.Path]::GetFullPath($taskReader.ImagePath())
 if(![string]::Equals($taskActualPath,[IO.Path]::GetFullPath($ExpectedImagePath),[StringComparison]::OrdinalIgnoreCase)){throw 'Held image path differs from expected path'}
 if((Get-FileHash -LiteralPath $taskActualPath).Hash.ToLowerInvariant() -cne $ExpectedExeSha256.ToLowerInvariant()){throw 'Disk EXE SHA differs at binding'}
 $taskAfterBind=$taskReader.Read()
 if($taskAfterBind.Status -cne 'OK' -or $taskAfterBind.CreationFileTime100ns -cne $ExpectedBirthFileTime100ns){throw 'Held identity changed during binding hash'}
 Save-NewJson (Join-Path $taskRoot 'IDENTITY.json') ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');samplerPid=$PID;targetPid=$ExpectedProcessId;birthFileTime=$ExpectedBirthFileTime100ns;imagePath=$taskActualPath;imageSha256=$ExpectedExeSha256.ToLowerInvariant();counterSource=$taskSource;counterSourceSha256=$taskSourceHash;bufferProjectionPath=$taskProjectionPath;bufferProjectionSha256=$taskProjectionHash;compiledCounterSourceSha256=$taskCompiledSourceHash;scriptSourceSha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant();hostPath=(Get-Process -Id $PID).Path;hostSha256=(Get-FileHash -LiteralPath (Get-Process -Id $PID).Path).Hash.ToLowerInvariant();qpcFrequency=[Ymcc.RogField.V1.ReadOnlyCounters]::QpcFrequency();logicalProcessors=[Ymcc.RogField.V1.ReadOnlyCounters]::LogicalProcessors();durationSeconds=$DurationSeconds;intervalMs=$IntervalMs;appOrDeviceActions=$false;threadDiscovery=$false;loopJsonOrFileIO=$false})
 $taskOwn.Refresh();$taskCaptureOwnCpuBefore=$taskOwn.TotalProcessorTime.TotalMilliseconds
 $taskTrace=$taskReader.CaptureBuffered($DurationSeconds*1000,$IntervalMs,$ExpectedBirthFileTime100ns)
 $taskOwn.Refresh();$taskCaptureOwnCpuAfter=$taskOwn.TotalProcessorTime.TotalMilliseconds
 $taskAfter=$taskReader.Read()
 $taskClosingExeHash=(Get-FileHash -LiteralPath $taskActualPath).Hash.ToLowerInvariant()
 if($taskClosingExeHash -cne $ExpectedExeSha256.ToLowerInvariant()){throw 'Disk EXE changed during capture; raw retained'}
 if($taskAfter.Status -cne 'OK' -or $taskAfter.CreationFileTime100ns -cne $ExpectedBirthFileTime100ns){throw 'Held liveness or birth changed at closing; raw retained'}
 if((Get-FileHash -LiteralPath $taskSource).Hash.ToLowerInvariant() -cne $taskSourceHash -or (Get-FileHash -LiteralPath $taskProjectionPath).Hash.ToLowerInvariant() -cne $taskProjectionHash){throw 'Counter base/projection source changed during capture; raw retained'}
 if((Hash-BufferedSource (Join-BufferedCounterSource ([IO.File]::ReadAllText($taskSource)) (Get-Content -LiteralPath $taskProjectionPath -Raw -Encoding UTF8|ConvertFrom-Json))) -cne $taskCompiledSourceHash){throw 'Regenerated compiled source changed; raw retained'}
}catch{$taskError=$_.Exception.ToString()}
finally{
 if($taskReader){$taskReader.Dispose();$taskCloseAttempted=$taskReader.HandleReleaseAttempted;$taskReadClose=$taskReader.HandleReleaseSucceeded;$taskCloseError=$taskReader.HandleReleaseWin32Error}
}
$taskRawStream=[IO.File]::Open((Join-Path $taskRoot 'RAW.jsonl'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$taskRawWriter=[IO.StreamWriter]::new($taskRawStream,$taskUtf8)
$taskMetrics=[Collections.Generic.List[object]]::new();$taskGaps=[Collections.Generic.List[object]]::new();$taskPrevious=$null
$taskFirst=$null;$taskLast=$null;$taskFullMax=$null;$taskAllMax=$null;$taskPartialCount=0
try{
 if($taskTrace){
  for($taskIndex=0;$taskIndex -lt $taskTrace.Count;$taskIndex++){
   $s=$taskTrace.Samples[$taskIndex]
   $taskRawWriter.WriteLine(([ordered]@{sequence=$taskIndex;qpcBefore=$s.ReadQpcBefore.ToString($taskInvariant);qpcAfter=$s.ReadQpcAfter.ToString($taskInvariant);waitQpcBefore=$s.WaitQpcBefore.ToString($taskInvariant);waitQpcAfter=$s.WaitQpcAfter.ToString($taskInvariant);scheduledQpc=$s.ScheduledQpc.ToString($taskInvariant);waitMilliseconds=$s.WaitMilliseconds;scheduleWaitResult=$s.ScheduleWaitResult;scheduleWaitWin32Error=$s.ScheduleWaitWin32Error;birthFileTime=$s.Creation100ns.ToString($taskInvariant);exitFileTimeDiagnostic=$s.Exit100ns.ToString($taskInvariant);exitFileTimeDefined=($s.TimesSucceeded -and $s.WaitAfter -eq 0);kernel100ns=$s.Kernel100ns.ToString($taskInvariant);user100ns=$s.User100ns.ToString($taskInvariant);cycles=$s.Cycles.ToString($taskInvariant);timesSucceeded=$s.TimesSucceeded;cyclesSucceeded=$s.CyclesSucceeded;timesWin32Error=$s.TimesWin32Error;cyclesWin32Error=$s.CyclesWin32Error;waitBefore=$s.WaitBefore;waitAfter=$s.WaitAfter;finalPartial=$s.FinalPartial}|ConvertTo-Json -Compress -Depth 5))
   if($null -eq $taskFirst){$taskFirst=$s};$taskLast=$s
   if($null -ne $taskPrevious){
    $taskWindowMs=[double]((Qpc-Midpoint $s)-(Qpc-Midpoint $taskPrevious))/$taskTrace.Frequency*1000.0
    $taskCpu100ns=([decimal]$s.Kernel100ns-[decimal]$taskPrevious.Kernel100ns)+([decimal]$s.User100ns-[decimal]$taskPrevious.User100ns)
    $taskPercent=if($taskWindowMs -gt 0){[double]$taskCpu100ns/10000.0/$taskWindowMs/[Ymcc.RogField.V1.ReadOnlyCounters]::LogicalProcessors()*100.0}else{$null}
    $taskMetric=[ordered]@{sequence=$taskIndex;windowMs=$taskWindowMs;cpu100ns=$taskCpu100ns.ToString($taskInvariant);cpuPercentMachine=$taskPercent;finalPartial=$s.FinalPartial;waitPhaseMs=([double]($s.WaitQpcAfter-$s.WaitQpcBefore)/$taskTrace.Frequency*1000);readPhaseMs=([double]($s.ReadQpcAfter-$s.ReadQpcBefore)/$taskTrace.Frequency*1000)}
    $taskMetrics.Add($taskMetric)
    if($taskWindowMs -le 0 -or $taskCpu100ns -lt 0){$taskGaps.Add($taskMetric)}
    elseif($s.FinalPartial){$taskPartialCount++}
    elseif($taskWindowMs -lt $IntervalMs*0.75 -or $taskWindowMs -gt $IntervalMs*1.25){$taskGaps.Add($taskMetric)}
    if($null -ne $taskPercent -and ($null -eq $taskAllMax -or $taskPercent -gt $taskAllMax.cpuPercentMachine)){$taskAllMax=$taskMetric}
    if(!$s.FinalPartial -and $null -ne $taskPercent -and ($null -eq $taskFullMax -or $taskPercent -gt $taskFullMax.cpuPercentMachine)){$taskFullMax=$taskMetric}
   }
   $taskPrevious=$s
  }
 }
}finally{$taskRawWriter.Dispose();$taskRawStream.Dispose()}
$taskMean=$null;$taskSpanMs=$null
if($null -ne $taskFirst -and $null -ne $taskLast -and $taskTrace.Count -gt 1){$taskSpanMs=[double]((Qpc-Midpoint $taskLast)-(Qpc-Midpoint $taskFirst))/$taskTrace.Frequency*1000.0;if($taskSpanMs -gt 0){$taskMean=[double](([decimal]$taskLast.Kernel100ns-[decimal]$taskFirst.Kernel100ns)+([decimal]$taskLast.User100ns-[decimal]$taskFirst.User100ns))/10000.0/$taskSpanMs/[Ymcc.RogField.V1.ReadOnlyCounters]::LogicalProcessors()*100.0}}
$taskComplete=$null -eq $taskError -and $null -ne $taskTrace -and $taskTrace.Status -ceq 'COUNTERS_COLLECTED' -and $taskReadClose
$taskOwn.Refresh()
Save-NewJson (Join-Path $taskRoot 'RESULT.json') ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');status=$(if($taskComplete -and !$taskGaps.Count){'BUFFERED_COUNTERS_COLLECTED_CADENCE_IN_RANGE'}elseif($taskComplete){'BUFFERED_COUNTERS_COLLECTED_CADENCE_UNKNOWN'}else{'PARTIAL_OR_ERROR'});error=$taskError;traceStatus=$(if($taskTrace){$taskTrace.Status}else{$null});traceError=$(if($taskTrace){$taskTrace.Error}else{$null});samples=$(if($taskTrace){$taskTrace.Count}else{0});spanMs=$taskSpanMs;meanCpuPercentMachine=$taskMean;fullWindowMaximum=$taskFullMax;allWindowMaximum=$taskAllMax;finalPartialIntervals=$taskPartialCount;cadenceOutliers=$taskGaps;intervals=$taskMetrics;handleReleaseAttempted=$taskCloseAttempted;nativeHandleReleaseSucceeded=$taskReadClose;nativeHandleReleaseError=$taskCloseError;diskExeClosingSha256=$taskClosingExeHash;observerCaptureCpuMs=$(if($null -ne $taskCaptureOwnCpuBefore){$taskCaptureOwnCpuAfter-$taskCaptureOwnCpuBefore}else{$null});observerTotalCpuMs=$taskOwn.TotalProcessorTime.TotalMilliseconds-$taskOwnCpuAtEntry;entryWallMs=$taskWall.Elapsed.TotalMilliseconds;appOrDeviceActions=$false;otherTasksStopped=$false;threadRanking='NOT_COLLECTED_THIS_EXPLICIT_ROOT_COUNTER_MODE';longTermPeakAccepted=$false;cyclesConvertedToTime=$false;sourceCounterHashUnchanged=($null -eq $taskError)})
$taskOwn.Dispose()
[ordered]@{status=$(if($taskComplete -and !$taskGaps.Count){'BUFFERED_COUNTERS_COLLECTED_CADENCE_IN_RANGE'}else{'PARTIAL_OR_UNKNOWN'});evidence=$taskRoot;samples=$(if($taskTrace){$taskTrace.Count}else{0});meanCpuPercentMachine=$taskMean;maximumFullWindow=$taskFullMax;cadenceOutliers=$taskGaps.Count;nativeHandleReleaseSucceeded=$taskReadClose}|ConvertTo-Json -Depth 5
if(!$taskComplete -or $taskGaps.Count){exit 2}
