param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###';$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors.Message -join ';')}
$functions=@{};foreach($fn in $ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){$functions[$fn.Name]=$fn.Extent.Text;. ([scriptblock]::Create($fn.Extent.Text))}
# All native effects are inert. The production PowerShell consumers execute verbatim.
Add-Type -TypeDefinition @"
using System;using System.Collections.Generic;
public static class YmBootProcess {
 public class Row {public int Id,ParentId,Session,Error;public long Birth;public string Name="XboxPcAppFT.exe",Path="C:\\inert\\XboxPcAppFT.exe",Package="",Family="",AppId="",User="S-1";public bool Readable=true;}
 public static List<string> Calls=new List<string>();public static Row[] Rows=new Row[0];
 public static Row Read(int id){foreach(var r in Rows)if(r.Id==id)return r;return new Row{Id=id,Readable=false,Error=87};}
 public static Row[] Inventory(){return Rows;}
 public static bool ExactXbox(Row r,int session){return r.Readable&&r.Session==session&&(r.Name=="XboxPcApp.exe"||r.Name=="XboxPcAppCE.exe"||r.Name=="XboxPcAppFT.exe");}
 public static bool TrustedPackage(Row r,int session,string sid){return ExactXbox(r,session)&&r.User==sid&&r.Family=="Microsoft.GamingApp_8wekyb3d8bbwe"&&r.Package.StartsWith("Microsoft.GamingApp_")&&r.Package.EndsWith("__8wekyb3d8bbwe");}
 public static string StopExact(int id,long birth,int session){Calls.Add(id+":"+birth+":"+session);return "terminate-requested exited=True";}
}
public static class YmBootWindow {public static string GamingContext(){return "fseApi=True supported=True active=True";}}
"@
$script:checks=0;$script:session=42;$script:bootUserSid='S-1'
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}
function Reset-Cleanup {
 $script:bootClock=[pscustomobject]@{ElapsedMilliseconds=0L};$script:xboxSeen=@{};$script:xboxAttempts=@{};$script:xboxPackages=@{};$script:xboxDenied=@{};$script:xboxObserved=0;$script:logs=[Collections.Generic.List[string]]::new();$script:packages=0
 [YmBootProcess]::Calls.Clear();$script:xboxParents=@{};[YmBootProcess]::Rows=@()
}
function Write-BootLog {param($E,$D)$script:logs.Add($E+' '+$D)}
function Invoke-BootPackageStop {param($Target)$script:packages++;[pscustomobject]@{Code=0;Text='package-stop-hr=0x00000000;absence-requires-followup'}}
function Row([int]$Id=100,[int]$Session=42,[long]$Birth=1000,[string]$Name='XboxPcAppFT.exe'){$r=[YmBootProcess+Row]::new();$r.Id=$Id;$r.Session=$Session;$r.Birth=$Birth;$r.Name=$Name;return $r}
Reset-Cleanup;$r=Row;Close-BootXbox @($r)
Assert ([YmBootProcess]::Calls.Count -eq 1 -and $script:xboxObserved -eq 1) 'exact observed Xbox PID and birth are used for one termination request'
Close-BootXbox @($r)
Assert ([YmBootProcess]::Calls.Count -eq 1 -and @($script:logs|Where-Object {$_ -like 'xbox-observed*'}).Count -eq 1) 'same identity is throttled and ancestry logged once'
foreach($ms in @(1000L,2000L,3000L,4000L)){$script:bootClock.ElapsedMilliseconds=$ms;Close-BootXbox @($r)}
Assert ([YmBootProcess]::Calls.Count -eq 3) 'denied or persistent same identity gets at most three bounded attempts'
$r2=Row 100 42 2000;Close-BootXbox @($r2)
Assert ([YmBootProcess]::Calls.Count -eq 4 -and $script:xboxObserved -eq 2) 'PID reuse is a new observed identity and still carries its own birth'
Reset-Cleanup;$foreign=Row 101 99;$other=Row 102 42 2000 'explorer.exe';$unread=Row 103;$unread.Readable=$false;$unread.Error=5;Close-BootXbox @($foreign,$other,$unread)
Assert ([YmBootProcess]::Calls.Count -eq 0 -and $script:packages -eq 0) 'foreign-session unrelated and unreadable identities are never cleanup targets'
Assert (@($script:logs|Where-Object {$_ -like 'xbox-cleanup-refused*'}).Count -eq 3) 'refusals retain exact identity and access errors'
Reset-Cleanup;$r=Row;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';Close-BootXbox @($r)
Assert ($script:packages -eq 1 -and [YmBootProcess]::Calls.Count -eq 1) 'verified Xbox package stop precedes exact PID fallback'
$r=Row 104 42 2000;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';$script:bootClock.ElapsedMilliseconds=500;Close-BootXbox @($r)
Assert ($script:packages -eq 1) 'same Xbox package reappearance does not rerun package helper before ten seconds'
$script:bootClock.ElapsedMilliseconds=10000;Close-BootXbox @($r);$script:bootClock.ElapsedMilliseconds=20000;$r=Row 105 42 3000;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';Close-BootXbox @($r)
Assert ($script:packages -eq 2) 'package helper has a two-attempt boot budget despite unlimited fresh PIDs in observer window'
Reset-Cleanup;$r=Row;$r.Package='Unrelated_1.0.0.0_x64__8wekyb3d8bbwe';$r.Family='Unrelated_8wekyb3d8bbwe';Close-BootXbox @($r)
Assert ($script:packages -eq 0 -and [YmBootProcess]::Calls.Count -eq 1) 'unverified package family never expands cleanup beyond the known image'
Reset-Cleanup;$r=Row;$r.ParentId=200;$parent=Row 200 42 500 'svchost.exe';$parent.ParentId=300;$root=Row 300 0 100 'services.exe';[YmBootProcess]::Rows=@($r,$parent,$root);$script:xboxParents=@{200=$parent;300=$root};Close-BootXbox @($r)
Assert ([YmBootProcess]::Calls.Count -eq 1 -and $script:logs[0] -like '*svchost.exe*services.exe*not-restart-owner-proof*') 'parent ancestry is logged and never terminated merely for being a parent'
$script:xboxParents=@{200=$parent};$parent.Birth=2000;$chain=@(Get-BootProcessLineage $r)
Assert ($chain.Count -eq 1 -and $chain[0].relation -eq 'parent-pid-reused') 'reused parent PID breaks provenance chain'
# Existing observation cases retain false/unknown readiness without a launch wait.
function WarmPorts([long]$ReadyAt) {
 $script:warm=@{Time=0L;ReadyAt=$ReadyAt;Waits=0;Kills=0;Logs=[Collections.Generic.List[string]]::new()}
 @{Now={$script:warm.Time};Wait={$script:warm.Time+=500;$script:warm.Waits++};InputContext={[pscustomobject]@{ready=$script:warm.ReadyAt -ge 0 -and $script:warm.Time -ge $script:warm.ReadyAt;state='inert'}};Snapshot={[pscustomobject]@{Xbox=@(1)}};CloseXbox={param($P)$script:warm.Kills++};Checkpoint={param($Stage,$S)};Log={param($E,$D)$script:warm.Logs.Add($D)}}
}
$p=WarmPorts 14000;Invoke-BootInputWarmup $p
Assert ($script:warm.Time -eq 0 -and $script:warm.Waits -eq 0 -and $script:warm.Kills -eq 0 -and $script:warm.Logs[-1] -like '*localReady=False*') 'pending target stays unready without delaying Steam or adding cleanup effects'
$p=WarmPorts -1;Invoke-BootInputWarmup $p
Assert ($script:warm.Time -eq 0 -and $script:warm.Waits -eq 0 -and $script:warm.Logs[-1] -like '*localReady=False*' -and $script:warm.Logs[-1] -like '*launchBlocked=False*') 'unknown readiness never gates Steam and is never claimed ready'
$p=WarmPorts 0;Invoke-BootInputWarmup $p
Assert ($script:warm.Time -eq 0) 'already ready or no requested target adds no warmup delay'
# Actual log readiness consumer, with process birth and per-generation admission checks.
$birth=([DateTime]'2026-10-23T08:00:00').ToUniversalTime().ToFileTimeUtc()
function Event([string]$Name,[string]$Time='2026-10-23T08:00:01',[int]$PidValue=100,[int]$Power=1){[pscustomobject]@{event=$Name;time=$Time;pid=$PidValue;powerGeneration=$Power;persona='steamdeck';transportUsable=$true;status='admitted';runId='run-current'}}
$started=Event 'input-host-started';$ready=Event 'input-host-fresh-neutral-admission' '2026-10-23T08:00:02';$ctx=Get-BootInputEvidence @($started,$ready) 100 $birth 'steamdeck'
Assert ($ctx.ready -and -not $ctx.steamConsumptionProven) 'current local target admission is recognized without Steam-consumption claim'
$ctx=Get-BootInputEvidence @($started) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'target object alone cannot bypass fresh-neutral gate'
$ctx=Get-BootInputEvidence @($started,$ready,(Event 'input-host-stop-request' '2026-10-23T08:00:03')) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'later stop invalidates prior admission'
$ctx=Get-BootInputEvidence @($started,$ready) 101 $birth 'steamdeck';Assert (-not $ctx.ready) 'another YMCC PID cannot satisfy readiness'
$ctx=Get-BootInputEvidence @($started,$ready) 100 $birth 'elite';Assert (-not $ctx.ready) 'another persona cannot satisfy readiness'
$ready.powerGeneration=2;$ctx=Get-BootInputEvidence @($started,$ready) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'different power generation cannot satisfy readiness'
$started.time='2026-10-23T07:59:00';$ready.time='2026-10-23T07:59:01';$ctx=Get-BootInputEvidence @($started,$ready) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'old reused-PID log history before current process birth is excluded'
$frame=Event 'input-host-frame-evidence' ([DateTime]::Now.ToString('yyyy-MM-ddTHH:mm:ss.fff'))
$frame|Add-Member NoteProperty hostSubmission 'frame-accepted';$frame|Add-Member NoteProperty powerLifecycle 'ready';$frame|Add-Member NoteProperty frameProof @{hostReceipt=@{hostAckAccepted=$true;persona='steamdeck';runId='existing-run';epoch=1}}
$ctx=Get-BootInputEvidence @($frame) 100 $birth 'steamdeck'
Assert ($ctx.ready -and $ctx.runId -eq 'existing-run') 'recent accepted current-owner frame allows long-running YMCC without its rotated startup rows'
$frame.frameProof.hostReceipt.hostAckAccepted=$false;$ctx=Get-BootInputEvidence @($frame) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'an unaccepted frame cannot satisfy existing-target warmup'
$frame.frameProof.hostReceipt.hostAckAccepted=$true;$frame.time=([DateTime]::Now.AddSeconds(-10)).ToString('yyyy-MM-ddTHH:mm:ss.fff');$ctx=Get-BootInputEvidence @($frame) 100 $birth 'steamdeck';Assert (-not $ctx.ready) 'stale frame cannot substitute for rotated startup readiness'
# Strict focus readiness consumes the same production function; ordinary diagnostics above stay unchanged.
function New-FocusEvidence {
 $at=[DateTime]::Now.ToString('yyyy-MM-ddTHH:mm:ss.fff')
 $start=Event 'input-host-started' $at
 $start|Add-Member NoteProperty epoch 7;$start|Add-Member NoteProperty boundPersona 'steamdeck';$start|Add-Member NoteProperty personaDrift $false
 $neutral=Event 'input-host-fresh-neutral-admission' $at
 $current=Event 'input-host-frame-evidence' $at
 $current|Add-Member NoteProperty hostSubmission 'frame-accepted';$current|Add-Member NoteProperty powerLifecycle 'ready'
 $current|Add-Member NoteProperty source @{live=$true;powerGeneration=1}
 $current|Add-Member NoteProperty frameProof @{hostReceipt=@{hostAckAccepted=$true;persona='steamdeck';runId='run-current';epoch=7;hostInstanceId='host-current';tupleCapturedBeforeRequest=$true}}
 [pscustomobject]@{Started=$start;Neutral=$neutral;Frame=$current}
}
$focus=New-FocusEvidence
$ctx=Get-BootInputEvidence @($focus.Started,$focus.Neutral) 100 $birth 'steamdeck' -RequireCurrentFrame
Assert (-not $ctx.ready) 'strict focus cannot reuse the diagnostic start plus fresh-neutral route without a current accepted frame'
$ctx=Get-BootInputEvidence @($focus.Started,$focus.Neutral,$focus.Frame) 100 $birth 'steamdeck' -RequireCurrentFrame
Assert ($ctx.ready -and $ctx.ownerPid -eq 100 -and $ctx.runId -eq 'run-current' -and $ctx.powerGeneration -eq 1 -and -not $ctx.steamConsumptionProven) 'strict focus accepts a fresh current-owner frame with matching run epoch power bound persona and captured Host tuple'
$ctx=Get-BootInputEvidence @($focus.Frame) 100 $birth 'steamdeck' -RequireCurrentFrame
Assert ($ctx.ready -and $ctx.runId -eq 'run-current') 'strict focus accepts a complete fresh frame when the long-running owner startup rows have rotated'
$ctx=Get-BootInputEvidence @($focus.Started,$focus.Neutral,$focus.Frame) 101 $birth 'steamdeck' -RequireCurrentFrame
Assert (-not $ctx.ready) 'strict focus rejects an accepted frame belonging to another YMCC owner'
$focusCases=@(
 @{Name='old run';Mutate={param($E)$E.Frame.frameProof.hostReceipt.runId='run-old'}}
 @{Name='old epoch';Mutate={param($E)$E.Frame.frameProof.hostReceipt.epoch=6}}
 @{Name='missing Host instance';Mutate={param($E)$E.Frame.frameProof.hostReceipt.Remove('hostInstanceId')}}
 @{Name='missing tuple capture';Mutate={param($E)$E.Frame.frameProof.hostReceipt.Remove('tupleCapturedBeforeRequest')}}
 @{Name='source not live';Mutate={param($E)$E.Frame.source.live=$false}}
 @{Name='source power mismatch';Mutate={param($E)$E.Frame.source.powerGeneration=2}}
 @{Name='started power mismatch';Mutate={param($E)$E.Started.powerGeneration=2}}
 @{Name='bound persona mismatch';Mutate={param($E)$E.Started.boundPersona='elite'}}
 @{Name='persona drift';Mutate={param($E)$E.Started.personaDrift=$true}}
 @{Name='unaccepted frame';Mutate={param($E)$E.Frame.frameProof.hostReceipt.hostAckAccepted=$false}}
 @{Name='expired recent frame';Mutate={param($E)$E.Frame.time=([DateTime]::Now.AddSeconds(-10)).ToString('yyyy-MM-ddTHH:mm:ss.fff')}}
)
foreach($case in $focusCases){
 $focus=New-FocusEvidence;& $case.Mutate $focus
 $ctx=Get-BootInputEvidence @($focus.Started,$focus.Neutral,$focus.Frame) 100 $birth 'steamdeck' -RequireCurrentFrame
 Assert (-not $ctx.ready) ('strict focus rejects '+$case.Name+' even when diagnostic startup admission is present')
}
foreach($stopEvent in @('input-host-stop-request','input-host-stop-result','input-host-persona-transition-requested','input-host-fault-cooldown')){
 $focus=New-FocusEvidence;$stop=Event $stopEvent ([DateTime]::Now.ToString('yyyy-MM-ddTHH:mm:ss.fff'))
 $ctx=Get-BootInputEvidence @($focus.Started,$focus.Neutral,$focus.Frame,$stop) 100 $birth 'steamdeck' -RequireCurrentFrame
 Assert (-not $ctx.ready) ('strict focus invalidates a fresh accepted frame after '+$stopEvent)
}
# Focus owner selection uses process-shaped inert candidates, never Get-Process or real applications.
$focusOwnerStart=[DateTime]::FromFileTimeUtc($birth).ToLocalTime()
$focusOwners=@([pscustomobject]@{Id=100;StartTime=$focusOwnerStart},[pscustomobject]@{Id=200;StartTime=$focusOwnerStart})
$focus=New-FocusEvidence;$focusRows=@($focus.Started,$focus.Neutral,$focus.Frame)
$ctx=Get-BootFocusOwnerContext $focusRows $focusOwners 'steamdeck'
Assert ($ctx.ready -and $ctx.ownerPid -eq 100 -and -not $ctx.steamConsumptionProven) 'two same-name candidates permit owner 100 when only that current PID and birth have fresh accepted input evidence'
$secondFocus=New-FocusEvidence
foreach($ownerRow in @($secondFocus.Started,$secondFocus.Neutral,$secondFocus.Frame)){$ownerRow.pid=200;$ownerRow.runId='run-second'}
$secondFocus.Started.epoch=8;$secondFocus.Frame.frameProof.hostReceipt.runId='run-second';$secondFocus.Frame.frameProof.hostReceipt.epoch=8;$secondFocus.Frame.frameProof.hostReceipt.hostInstanceId='host-second'
$focusRows=@($focus.Started,$focus.Neutral,$focus.Frame,$secondFocus.Started,$secondFocus.Neutral,$secondFocus.Frame)
$ctx=Get-BootFocusOwnerContext $focusRows $focusOwners 'steamdeck'
Assert (-not $ctx.ready) 'two distinct current input owners with valid Host tuples remain unknown instead of selecting the first same-name process'
$expiredAt=([DateTime]::Now.AddSeconds(-10)).ToString('yyyy-MM-ddTHH:mm:ss.fff')
$focus.Frame.time=$expiredAt;$secondFocus.Frame.time=$expiredAt
$ctx=Get-BootFocusOwnerContext $focusRows $focusOwners 'steamdeck'
Assert (-not $ctx.ready) 'owner selection rejects expired frames for both candidates even when their diagnostic start plus neutral evidence remains'
$focus=New-FocusEvidence;$oldOwnerAt=([DateTime]::Now.AddSeconds(-2)).ToString('yyyy-MM-ddTHH:mm:ss.fff')
foreach($ownerRow in @($focus.Started,$focus.Neutral,$focus.Frame)){$ownerRow.time=$oldOwnerAt}
$reusedFocusOwners=@([pscustomobject]@{Id=100;StartTime=[DateTime]::Now.AddSeconds(-1)},[pscustomobject]@{Id=200;StartTime=$focusOwnerStart})
$ctx=Get-BootFocusOwnerContext @($focus.Started,$focus.Neutral,$focus.Frame) $reusedFocusOwners 'steamdeck'
Assert (-not $ctx.ready) 'owner selection rejects still-fresh evidence predating the current process birth after PID reuse'
# Input report bytes are captured before source traces can rotate; no keyboard samples or injection.
$script:streamFixtures=@{
 'native-detail.log'='{"time":"2026-10-23T08:00:01","event":"gamepad-xinput-input","pid":100,"buttonMask":4096,"primarySlot":0,"connected":true}';
 'focus-trace.log'='{"event":"input-signal-activity","writerPid":100,"hostRunId":"run","physicalBeforePublication":{"samples":123},"actualSubmitAttempts":{"samples":123},"hostAcceptedContent":{"samples":123}}';
 'input-host-cost.log'='{"t":1,"writerPid":200,"hostInstanceId":"host","runId":"run","steamDeckPadSubmissions":{"Reports":10,"Edges":2,"OmittedRows":0,"Entries":[{"CommandSequence":15,"SampleSequence":1,"ButtonBits":8192}],"LastSuccessfulReport":{"ButtonBits":0}},"usbipTransport":{"snapshot":{"TransferCompletedProven":false}}}'
}
function Read-BootTail {param($Path,$Maximum)$script:streamFixtures[[IO.Path]::GetFileName($Path)]}
$pipeline=Get-BootPipelineEvidence
Assert ($pipeline.physical.buttons -eq 4096 -and $pipeline.publication.physical.samples -eq 123 -and $pipeline.sdk.entries[0].ButtonBits -eq 8192) 'checkpoint captures physical, publication and SDK ring separately'
Assert ($pipeline.sdk.scope -like '*not USB or Steam consumption*' -and $pipeline.sdk.reports -eq 10) 'cumulative counters remain raw and are not promoted to input receipt'
# The actual startup dispatches Steam before diagnostic observation, even if an old observation hook waits.
function New-StartupPorts([bool]$SteamRunning) {
 $script:flow=@{Time=0L;Running=$SteamRunning;Events=[Collections.Generic.List[string]]::new();SteamAt=-1L;Checkpoints=0}
 @{Now={$script:flow.Time};Wait={$script:flow.Time+=500L};Log={param($E,$D)}
 Snapshot={[pscustomobject]@{Xbox=@();SteamRunning=$script:flow.Running;SteamWindow=$script:flow.Running}}
 CSS={throw 'BAT cannot start CSS'};YMCC={$script:flow.Events.Add('ymcc')}
 InputWarmup={$script:flow.Events.Add('warmup');$script:flow.Time+=14000L}
 CloseXbox={param($P)};YieldSteamFocus={$false};ShowSteam={$script:flow.Events.Add('show')}
 StartSteam={$script:flow.Events.Add('steam');$script:flow.SteamAt=$script:flow.Time;$script:flow.Running=$true}
 FocusContext={[pscustomobject]@{ready=$true;state='not-requested'}}
 FocusSteam={param($R,$S)};Affinity={};Checkpoint={param($Stage,$Snapshot)$script:flow.Checkpoints++};Complete={param($Stage,$Snapshot)$script:flow.Events.Add('complete')}
 }
}
$p=New-StartupPorts $false;$code=Invoke-BootFlow $p
Assert (($script:flow.Events -join ',') -eq 'ymcc,steam,warmup,complete' -and $script:flow.SteamAt -eq 0L -and $script:flow.Time -eq 60000L) 'Steam dispatch precedes even a delayed diagnostic hook while the bounded sixty-second startup observer remains'
$p=New-StartupPorts $true;$code=Invoke-BootFlow $p
Assert (($script:flow.Events -join ',') -eq 'ymcc,show,complete' -and $script:flow.Time -eq 60000L) 'already-running Steam is neither delayed by warmup nor relaunched'
Assert ($script:flow.Checkpoints -gt 0) 'actual startup emits input checkpoints during its existing observer'
# Read-only tail persists long enough to observe late user recovery, with no cleanup ports.
$script:tail=@{Time=60000L;Snapshots=0;Checkpoints=0;Logs=0}
$tailPorts=@{Now={$script:tail.Time};Wait={$script:tail.Time+=10000L};Snapshot={$script:tail.Snapshots++;[pscustomobject]@{Xbox=@()}};Checkpoint={param($Stage,$Snapshot)if($Stage -ne 'post-boot'){throw 'unexpected diagnostic stage'};$script:tail.Checkpoints++};Log={param($E,$D)$script:tail.Logs++}}
Invoke-BootDiagnosticTail $tailPorts
Assert ($script:tail.Time -eq 120000 -and $script:tail.Snapshots -eq 6 -and $script:tail.Checkpoints -eq 6 -and $script:tail.Logs -eq 1) 'post-boot diagnostics stop after six read-only checks and sixty seconds without cleanup or activation ports'
$tailText=$functions['Invoke-BootDiagnosticTail'];Assert ($tailText -notmatch '\$P\.(CloseXbox|StartSteam|ShowSteam|FocusSteam|YMCC)') 'diagnostic tail has no application or foreground action route'
# Execute actual checkpoint throttling and preserve raw row source time/run identities.
Reset-Cleanup;$script:inputContext=$null;$script:lastInputLogAt=-100000L;$script:lastInputSignature=''
function Get-BootInputContext {[pscustomobject]@{state='local-target-admitted';ready=$true;persona='steamdeck';ownerPid=100;lastHostEvent='admitted'}}
function Get-BootForeground {[pscustomobject]@{Hwnd=[IntPtr]123;Pid=100}}
$snapshot=[pscustomobject]@{Xbox=@();SteamRunning=$true;SteamWindow=$true}
Write-BootCheckpoint 'poll' $snapshot;Write-BootCheckpoint 'poll' $snapshot
Assert ($script:logs.Count -eq 1 -and $script:logs[0] -like '*"buttons":4096*"entries"*') 'checkpoint retains ordinary input and SDK edges while suppressing unchanged half-second spam'
$script:bootClock.ElapsedMilliseconds=5000;Write-BootCheckpoint 'prewarm' $snapshot
Assert ($script:logs.Count -eq 2) 'checkpoint refreshes at five seconds even without input-state transition'
# End status reflects actual remaining known images even if prior requests succeeded.
Reset-Cleanup;function Write-BootCheckpoint {param($Stage,$Snapshot)}
Complete-BootObservation 'deadline' ([pscustomobject]@{Xbox=@((Row));SteamRunning=$true;SteamWindow=$true})
Assert ($script:logs[-1] -like '*"noKnownImagesObserved":false*') 'cleanup cannot claim clean when a known Xbox image remains at deadline'
Complete-BootObservation 'complete' ([pscustomobject]@{Xbox=@();SteamRunning=$true;SteamWindow=$true})
Assert ($script:logs[-1] -like '*"noKnownImagesObserved":true*' -and $script:logs[-1] -like '*snapshot-only*') 'observed absence is labeled snapshot-only rather than permanent removal'
Assert ($body -notmatch 'taskkill\.exe|/IM\s+Xbox|Stop-Process|Disable-ScheduledTask|SetGamingFullScreenExperience\(') 'cleanup has no image-wide taskkill, service/task disabling or FSE mode write'
Assert ($body.Contains("'C:\SOFT\YeMan\PowerControl\yeman-settings.json'")) 'warmup reads the production settings authority rather than the exported log copy'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual bounded Xbox cleanup, lineage, input warmup/readiness and checkpoint consumers';sourceSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant();realApplicationsStartedStopped=$false;realForegroundChanged=$false;fieldAcceptance=$false}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'cleanup-result.json')
