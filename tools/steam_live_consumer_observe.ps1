[CmdletBinding()]
param(
 [ValidatePattern('^[A-Za-z0-9_. -]+\.exe$')][string]$GameExecutable='Cyberpunk2077.exe',
 [ValidateRange(60,1800)][int]$DurationSeconds=900,
 [ValidateRange(500,2000)][int]$PollMilliseconds=1000,
 [string]$OutputDirectory='',
 [switch]$SelfTest
)
$ErrorActionPreference='Stop'
function Same-OriginalIdentity($Bound,$Current){
 return $null-ne$Bound -and $null-ne$Current -and $Current.IdentityObserved-eq$true -and $Bound.ProcessId-eq$Current.ProcessId -and $Bound.CreationFiletime-ceq$Current.CreationFiletime -and $Bound.ImagePath-ieq$Current.ImagePath -and $Bound.SessionId-eq$Current.SessionId
}
function Select-UniqueOriginal($Candidates,[string]$Name,[uint32]$Session){
 # An inaccessible candidate in the observer session cannot be silently excluded.
 $relevant=@($Candidates|Where-Object {$_.SessionObserved-ne$true -or $_.SessionId-eq$Session})
 if($relevant.Count-ne1){return $null}
 $candidate=$relevant[0]
 if($candidate.IdentityObserved-eq$true -and $candidate.SessionId-eq$Session -and [IO.Path]::GetFileName($candidate.ImagePath)-ieq$Name){return $candidate}
 return $null
}
function Test-PinnedBinding($Candidate,$Pinned){
 return $null-ne$Pinned -and $Pinned.ExitObserved-ne$true -and (Same-OriginalIdentity $Candidate $Pinned)
}
function Classify-BoundObservation($Bound,$Current){
 if($Current.ExitObserved-eq$true){return 'original-exited'}
 if($Current.IdentityObserved-ne$true){return 'identity-unknown'}
 if(Same-OriginalIdentity $Bound $Current){return 'same-original-observed'}
 return 'original-identity-changed'
}
if($SelfTest){
 $tests=[Collections.Generic.List[object]]::new()
 function Check([string]$Name,[bool]$Value){$tests.Add(@{name=$Name;passed=$Value})}
 $original=[pscustomobject]@{IdentityObserved=$true;ProcessId=100;CreationFiletime='134364220808351763';ImagePath='D:\Fixture\Cyberpunk2077.exe';SessionId=1;SessionObserved=$true;ExitObserved=$false}
 Check 'unique full identity binds' ($null-ne(Select-UniqueOriginal @($original) 'Cyberpunk2077.exe' 1))
 Check 'ambiguous candidates never pick by foreground' ($null-eq(Select-UniqueOriginal @($original,$original) 'Cyberpunk2077.exe' 1))
 Check 'other executable is not a target' ($null-eq(Select-UniqueOriginal @($original) 'Other.exe' 1))
 Check 'other Windows session is not a target' ($null-eq(Select-UniqueOriginal @($original) 'Cyberpunk2077.exe' 2))
 $unknown=$original.PSObject.Copy();$unknown.IdentityObserved=$false
 Check 'partial identity cannot bind' ($null-eq(Select-UniqueOriginal @($unknown) 'Cyberpunk2077.exe' 1))
 Check 'inaccessible duplicate prevents unique binding' ($null-eq(Select-UniqueOriginal @($original,$unknown) 'Cyberpunk2077.exe' 1))
 Check 'same original is recognized' ((Classify-BoundObservation $original $original)-eq'same-original-observed')
 $reused=$original.PSObject.Copy();$reused.CreationFiletime='134364220808351764'
 Check 'one tick PID reuse rejected' ((Classify-BoundObservation $original $reused)-eq'original-identity-changed')
 Check 'reopen PID reuse cannot pin stale candidate' (-not(Test-PinnedBinding $original $reused))
 Check 'verified reopened original can pin' (Test-PinnedBinding $original $original)
 $newPid=$original.PSObject.Copy();$newPid.ProcessId=101
 Check 'new PID cannot replace original' ((Classify-BoundObservation $original $newPid)-eq'original-identity-changed')
 $newPath=$original.PSObject.Copy();$newPath.ImagePath='D:\Other\Cyberpunk2077.exe'
 Check 'same filename different path rejected' ((Classify-BoundObservation $original $newPath)-eq'original-identity-changed')
 $newSession=$original.PSObject.Copy();$newSession.SessionId=2
 Check 'session changed rejected' ((Classify-BoundObservation $original $newSession)-eq'original-identity-changed')
 Check 'unknown observation does not fabricate exit or input' ((Classify-BoundObservation $original $unknown)-eq'identity-unknown')
 $exited=$original.PSObject.Copy();$exited.ExitObserved=$true;$exited.IdentityObserved=$false
 Check 'pinned handle exit is terminal' ((Classify-BoundObservation $original $exited)-eq'original-exited')
 Check 'already exited reopened candidate cannot bind' (-not(Test-PinnedBinding $original $exited))
 $pass=@($tests|Where-Object {-not$_.passed}).Count-eq0
 $report=@{schema='ymcc-live-consumer-observer-selftest-v1';passed=$pass;cases=$tests.Count;tests=@($tests);deviceOperations=0;productOrGameStarted=$false;actualInput='unknown';Continue='unknown'}
 $report|ConvertTo-Json -Depth 5
 if(-not$pass){exit 1};exit 0
}
if(-not('YmccLiveConsumer.ReadOnlyNative'-as[type])){Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'steam_live_consumer_native.cs')))}
if(-not$OutputDirectory){throw 'Explicit output directory required; no historical log discovery is performed.'}
$outputRoot=[IO.Path]::GetFullPath($OutputDirectory);$null=[IO.Directory]::CreateDirectory($outputRoot)
$recordPath=Join-Path $outputRoot 'live-consumer-observations.jsonl'
$stream=[IO.FileStream]::new($recordPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false));$writer.AutoFlush=$true
$observerStartFiletime=[DateTime]::UtcNow.ToFileTimeUtc();$clock=[Diagnostics.Stopwatch]::StartNew();$sequence=0;$steamHandle=[IntPtr]::Zero;$gameHandle=[IntPtr]::Zero;$steamBound=$null;$gameBound=$null;$priorSlots=@{};$endReason='observer-duration-complete'
function Emit([string]$Event,$Data){
 $script:sequence++
 $row=@{schema='ymcc-live-consumer-observer-v1';event=$Event;utc=(Get-Date).ToUniversalTime().ToString('o');elapsedMs=$clock.ElapsedMilliseconds;observationSequence=$script:sequence;data=$Data;readOnly=$true;writesDevices=$false;repairActions=0;gameInputUnverified=$true;ContinueAcceptance='unknown'}
 $writer.WriteLine(($row|ConvertTo-Json -Depth 8 -Compress))
}
function Observe-Candidate([uint32]$ObservedProcessId){
 $handle=[YmccLiveConsumer.ReadOnlyNative]::Open($ObservedProcessId)
 try{return [YmccLiveConsumer.ReadOnlyNative]::Read($handle,$ObservedProcessId)}finally{[YmccLiveConsumer.ReadOnlyNative]::Close($handle)}
}
function Open-VerifiedOriginal($Candidate){
 $handle=[YmccLiveConsumer.ReadOnlyNative]::Open($Candidate.ProcessId)
 $current=[YmccLiveConsumer.ReadOnlyNative]::Read($handle,$Candidate.ProcessId)
 if(Test-PinnedBinding $Candidate $current){return [pscustomobject]@{Handle=$handle;Identity=$current}}
 [YmccLiveConsumer.ReadOnlyNative]::Close($handle)
 return $null
}
function Read-XInputFacts{
 return @([YmccLiveConsumer.ReadOnlyNative]::Slots()|ForEach-Object {
  $slot=$_;$key=[string]$slot.Slot;$values="$($slot.Buttons),$($slot.LeftTrigger),$($slot.RightTrigger),$($slot.LeftX),$($slot.LeftY),$($slot.RightX),$($slot.RightY)";$prior=$priorSlots[$key]
  $changed=$slot.ApiSucceeded-and$null-ne$prior-and$values-cne$prior
  if($slot.ApiSucceeded){$priorSlots[$key]=$values}else{$priorSlots.Remove($key)}
  @{slot=$slot.Slot;apiSucceeded=$slot.ApiSucceeded;result=$slot.Result;packetNumber=$slot.PacketNumber;buttons=$slot.Buttons;leftTrigger=$slot.LeftTrigger;rightTrigger=$slot.RightTrigger;leftX=$slot.LeftX;leftY=$slot.LeftY;rightX=$slot.RightX;rightY=$slot.RightY;havePrior=$null-ne$prior;rawControlValuesChanged=$changed;error=$slot.Error;virtualEndpointIdentity='unknown';SteamControllerIdentity='unknown';actualGameConsumption='unknown'}
 })
}
try{
 $observerFact=Observe-Candidate ([uint32]$PID)
 if(-not$observerFact.IdentityObserved){throw 'Read-only observer self identity unavailable; no target selected.'}
 Emit 'observer-start' @{observerIdentity=$observerFact;expectedGameExecutable=$GameExecutable;durationSeconds=$DurationSeconds;pollMilliseconds=$PollMilliseconds;inputApi='system32/xinput1_4.dll:XInputGetState';inputIdentityScope='observer-local-XInput-slot;not-Steam-controller-or-virtual-endpoint';sleepRepairAuthorityUnchanged=$true;historicLogsRead=$false;accountOrTrackerProof='not-collected;no-repair-authorization'}
 Write-Output "READONLY_OBSERVER_READY expected=$GameExecutable output=$recordPath"
 while($clock.Elapsed.TotalSeconds-lt$DurationSeconds){
  $currentSteam=$null;$steamState='not-yet-bound'
  if($null-eq$steamBound){
   $steamCandidates=@(Get-CimInstance Win32_Process -Filter "Name='steam.exe'"|ForEach-Object {Observe-Candidate $_.ProcessId})
   $selectedSteam=Select-UniqueOriginal $steamCandidates 'steam.exe' $observerFact.SessionId
   if($null-ne$selectedSteam){
    $pinned=Open-VerifiedOriginal $selectedSteam
    if($null-ne$pinned){$steamBound=$pinned.Identity;$steamHandle=$pinned.Handle;$currentSteam=$pinned.Identity;Emit 'steam-original-bound' $steamBound}
    else{Emit 'steam-binding-rejected' @{reason='candidate-to-pinned-identity-unavailable-or-changed';candidatePid=$selectedSteam.ProcessId}}
   }
  }else{$currentSteam=[YmccLiveConsumer.ReadOnlyNative]::Read($steamHandle,$steamBound.ProcessId)}
  if($null-ne$steamBound){
   $steamState=Classify-BoundObservation $steamBound $currentSteam
   Emit 'steam-original-observation' @{identity=$currentSteam;identityStatus=$steamState}
   if($steamState-eq'original-exited'-or$steamState-eq'original-identity-changed'){$endReason='original-Steam-exited-or-changed';break}
  }
  if($steamState-eq'same-original-observed'-and$null-eq$gameBound){
   $gameCandidates=@(Get-CimInstance Win32_Process -Filter "Name='$GameExecutable'"|ForEach-Object {Observe-Candidate $_.ProcessId})
   $selected=Select-UniqueOriginal $gameCandidates $GameExecutable $steamBound.SessionId
   if($null-ne$selected){
    $pinned=Open-VerifiedOriginal $selected
    if($null-ne$pinned){
     $gameBound=$pinned.Identity;$gameHandle=$pinned.Handle
     Emit 'game-original-bound' @{identity=$gameBound;preexistingAtObserverStart=([uint64]$gameBound.CreationFiletime-lt[uint64]$observerStartFiletime);startupCause='not-proven-by-OS-presence;operator-response-required';YMCCSinglePidValidation='not-collected';exactSteamTrackerBinding='not-collected'}
     Write-Output "GAME_ORIGINAL_BOUND pid=$($gameBound.ProcessId) birth=$($gameBound.CreationFiletime)"
    }else{Emit 'game-binding-rejected' @{reason='candidate-to-pinned-identity-unavailable-or-changed';candidatePid=$selected.ProcessId}}
   }
  }
  $slots=Read-XInputFacts
  if($null-ne$gameBound){
   $currentGame=[YmccLiveConsumer.ReadOnlyNative]::Read($gameHandle,$gameBound.ProcessId);$state=Classify-BoundObservation $gameBound $currentGame
   $foreground=[YmccLiveConsumer.ReadOnlyNative]::ForegroundPid();$foregroundClass=if($foreground-eq$gameBound.ProcessId){'original-game'}elseif($foreground-eq$steamBound.ProcessId){'original-Steam'}else{'other-or-unknown'}
   Emit 'original-game-observation' @{identity=$currentGame;identityStatus=$state;originalSteamIdentityStatus=$steamState;foreground=$foregroundClass;xinput=$slots}
   if($state-eq'original-exited'-or$state-eq'original-identity-changed'){$endReason=$state;break}
  }else{Emit 'awaiting-original-game-observation' @{originalSteamIdentityStatus=$steamState;xinput=$slots;gameIdentity='not-yet-bound'}}
  Start-Sleep -Milliseconds $PollMilliseconds
 }
 Emit 'observer-stop' @{reason=$endReason;originalGame=$gameBound;originalSteam=$steamBound;actualGameInput='unknown;operator-response-required';Continue='unknown;real-return-observation-required'}
 Write-Output "READONLY_OBSERVER_STOP reason=$endReason output=$recordPath"
}catch{
 Emit 'observer-failed' @{exceptionType=$_.Exception.GetType().Name;reason=$_.Exception.Message;actualGameInput='unknown';Continue='unknown'}
 throw
}finally{
 [YmccLiveConsumer.ReadOnlyNative]::Close($gameHandle);[YmccLiveConsumer.ReadOnlyNative]::Close($steamHandle);$writer.Dispose()
}
