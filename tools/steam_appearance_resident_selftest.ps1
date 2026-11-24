[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[ValidateSet('All','LateCandidate','UnboundPid','ReusedPid','ColdWait','Admission')][string]$Case='All',[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(!$ComponentRoot){$ComponentRoot=Join-Path $repo 'PowerControl'}
New-Item -ItemType Directory -Path $Out -Force|Out-Null
function Load-Function($Path,$Name){$t=$null;$e=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$t,[ref]$e);if($e.Count){throw $e[0]};$f=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $Name},$true);if(!$f){throw "Missing $Name"};$f.Extent.Text}
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance.ps1') 'Start-AppearanceBackend')
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance.ps1') 'Get-BackendProcesses')
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance.ps1') 'Test-BackendOwner')
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\resident.ps1') 'Assert-SingleAppearanceBackend')
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\resident.ps1') 'Get-AppearanceProcessPath')
Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\preset.ps1') 'Get-PresetResourceHash')
$script:BackendExe=Join-Path $Out 'CssLoader-Standalone-Headless.exe';[IO.File]::WriteAllText($script:BackendExe,'inert placeholder')
$script:AppearanceFixtureMutexPrefix='Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.'
function New-AppearanceMutex([string]$Purpose) {
 if($Purpose -notin @('Launch','ThemeControl')){throw 'APPEARANCE_FIXTURE_MUTEX_PURPOSE_DENIED'}
 New-Object Threading.Mutex($false,($script:AppearanceFixtureMutexPrefix+$Purpose))
}
$script:Rows=@();$script:Ready=$true;$script:Editing=$false;$script:Port=$false;$script:Spawns=0;$script:Waits=0
$script:CandidateReads=0;$script:LateCandidate=$false;$script:ListenerProcess=$null
function Row($Id,$Parent){[pscustomobject]@{Id=$Id;Path=$script:BackendExe;StartTime=([datetime]'2026-10-23').AddSeconds($Id);Parent=$Parent;HasExited=$false}}
function Read-PresetState {$null}
function Test-CssDesktopEditing {$script:Editing}
function Assert-CssDesktopNotEditing {if($script:Editing){throw 'editor-active'}}
function Test-CurrentUserProcess($Process){$true}
function Update-AppearanceProcessInventory {}
function Get-AppearanceBackendCandidates {
 $script:CandidateReads++
 if($script:LateCandidate){
  if($script:CandidateReads -eq 2){$script:Rows=@((Row 100 0),(Row 101 100));$script:Ready=$false}
  if($script:CandidateReads -eq 3){$script:Ready=$true}
 }
 $script:Rows
}
function Get-AppearanceParentId($Process){$Process.Parent}
function Get-Process {param([int]$Id,$ErrorAction)
 if(!$Id){throw 'PRIVATE_PROCESS_ENUMERATION_DENIED'}
 if($script:ListenerProcess -and $script:ListenerProcess.Id -eq $Id){return $script:ListenerProcess}
 $script:Rows|Where-Object Id -eq $Id
}
function Get-AppearancePid([uint32]$Id,[long]$ExpectedBirth=0){
 $process=Get-Process -Id $Id
 if(!$process){return}
 if($ExpectedBirth -and $process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $ExpectedBirth){throw 'YMCC candidate birth changed; state unknown'}
 if(!$ExpectedBirth -or !$process.HasExited){$process}
}
function Get-AppearanceTcpListeners {param($LocalPort)
 if($LocalPort -ne 35821){throw 'Unexpected fixture listener query'}
 $pidObserved=if($script:Port){999}elseif($script:ListenerProcess){$script:ListenerProcess.Id}elseif($script:Ready -and $script:Rows.Count){$script:Rows[-1].Id}
 if($pidObserved){[pscustomobject]@{OwningProcess=$pidObserved;LocalAddress='127.0.0.1'}}
}
function Get-CimInstance {throw 'PRIVATE_CIM_ENUMERATION_DENIED'}
function Start-ResidentAppearanceBackend {$script:Spawns++;$script:Rows=@((Row 100 0),(Row 101 100));$script:Ready=$true}
function Start-Sleep {param($Milliseconds) $script:Waits++;throw 'bounded-not-ready-fixture'}
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Name){if(!$Value){throw $Name};$cases.Add($Name)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
if($Case -eq 'All'){
$script:Rows=@((Row 100 0),(Row 101 100));$result=Start-AppearanceBackend
Assert ($result.ok -and !$result.started -and $script:Spawns -eq 0) 'Packaged parent and child are reused as one instance'
$script:Rows=@((Row 100 0),(Row 200 0));Assert (Reject {Start-AppearanceBackend}) 'Independent existing roots are rejected without killing or replacing either'
$script:Rows=@([pscustomobject]@{Id=100;Path='D:\other\CssLoader-Standalone-Headless.exe';Parent=0});Assert (Reject {Start-AppearanceBackend}) 'Another installation still opening its port blocks creation'
$script:Rows=@();$script:Port=$true;Assert (Reject {Start-AppearanceBackend}) 'Occupied CSS port blocks creation even with no matching executable'
$script:Port=$false;$script:Editing=$true;Assert (Reject {Start-AppearanceBackend}) 'Desktop editing blocks creation of a missing backend'
$script:Editing=$false;$script:Ready=$false;$script:Rows=@((Row 100 0));Assert (Reject {Start-AppearanceBackend}) 'Unresponsive existing backend is retained instead of restarted'
Assert ($script:Waits -eq 1 -and $script:Spawns -eq 0) 'No retry launch occurs while an existing process is unready'
$script:Rows=@();$script:Ready=$false;$result=Start-AppearanceBackend;$again=Start-AppearanceBackend
Assert ($result.ok -and $result.started -and $again.ok -and !$again.started -and $script:Spawns -eq 1) 'Missing backend starts exactly once and the next Ensure reuses it'
$script:Editing=$true;$again=Start-AppearanceBackend;Assert ($again.ok -and $script:Spawns -eq 1) 'Existing editor-owned backend is preserved and reused'
$hashFile=Join-Path $Out 'resource.css';[IO.File]::WriteAllText($hashFile,'first');$script:HashReads=0
function Get-ConfigHash($Path){$script:HashReads++;(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
$a=Get-PresetResourceHash $hashFile;$b=Get-PresetResourceHash $hashFile
Assert ($a -eq $b -and $script:HashReads -eq 2) 'Repeated status stages verify current bytes even if metadata is unchanged'
$mtime=[IO.File]::GetLastWriteTimeUtc($hashFile);[IO.File]::WriteAllText($hashFile,'other');[IO.File]::SetLastWriteTimeUtc($hashFile,$mtime)
$c=Get-PresetResourceHash $hashFile
Assert ($c -ne $a -and $script:HashReads -eq 3) 'A same-length replacement at the same last-write time cannot reuse the old digest'
}
if($Case -in 'All','LateCandidate'){
 $script:Editing=$false;$script:Rows=@();$script:Ready=$true;$script:CandidateReads=0;$script:LateCandidate=$true
 $spawnsBefore=$script:Spawns
 $result=Start-AppearanceBackend
 Assert ($result.ok -and !$result.started -and $script:Spawns -eq $spawnsBefore) 'Same-path backend first visible during the last candidate read is retained without a second spawn'
 $script:LateCandidate=$false
}
foreach($scenario in 'UnboundPid','ReusedPid'){
 if($Case -notin 'All',$scenario){continue}
 $script:Rows=@((Row 100 0),(Row 101 100));$script:Ready=$true
 $script:ListenerProcess=if($scenario -eq 'UnboundPid'){Row 102 0}else{Row 101 100}
 if($scenario -eq 'ReusedPid'){$script:ListenerProcess.StartTime=$script:ListenerProcess.StartTime.AddSeconds(1)}
 $spawnsBefore=$script:Spawns;$waitsBefore=$script:Waits
 $reason='';try{Start-AppearanceBackend|Out-Null}catch{$reason=$_.Exception.Message}
 Assert ($reason -ceq 'bounded-not-ready-fixture' -and $script:Waits -eq ($waitsBefore+1) -and $script:Spawns -eq $spawnsBefore) "Readiness cannot use a listener outside the verified PID and birth snapshot: $scenario"
}
$coldWait=@()
if($Case -in 'All','ColdWait'){$coldWait=@(& {
 # Use the real candidate function and replace only process/listener/sleep ports.
 Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\resident.ps1') 'Get-AppearanceBackendCandidates')
 function Get-AppearanceProcessCandidates([int]$Kind){
  if($Kind -ne 1){throw 'Unexpected candidate kind'}
  if($script:ColdStarted){$script:ColdCandidateSlots.Add($script:ColdSlot);$script:Rows}
 }
 function Get-AppearanceTcpListeners([int]$LocalPort,[switch]$ReadinessOnly){
  if($LocalPort -ne 35821){throw 'Unexpected listener port'}
  $script:ColdListenerKinds.Add([bool]$ReadinessOnly)
  if(!$script:ColdStarted){return}
  $script:ColdListenerSlots.Add($script:ColdSlot);$script:ColdQueries++
  if($scenario -eq 'Unknown'){throw 'fixture-listener-unknown'}
  if($script:ColdSlot -ge $readyAt){[pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=101};return}
  if($scenario -eq 'IPv6Only'){[pscustomobject]@{LocalAddress='::1';OwningProcess=100}}
  if($scenario -eq 'OwnerChanged'){
   [pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=$(if($script:ColdQueries -eq 1){101}else{102})}
  }
 }
 function Start-ResidentAppearanceBackend {
  if($script:ColdStarted){throw 'Unexpected second launch'}
  $script:Spawns++;$script:ColdStarted=$true;$script:AppearanceStartedPid=100
  $script:Rows=@((Row 100 0),(Row 101 100))
 }
 function Start-Sleep([int]$Milliseconds){
  if($Milliseconds -ne 500 -or $script:ColdSlot -ge $readyAt){throw 'Readiness retry cadence changed'}
  $script:ColdSlot++;$script:ColdQueries=0
 }
 foreach($scenario in 'NoListener','IPv6Only','OwnerChanged','Immediate','Unknown'){
  $readyAt=switch($scenario){'NoListener'{8};'IPv6Only'{8};'OwnerChanged'{1};default{0}}
  $script:Rows=@();$script:Editing=$false;$script:ListenerProcess=$null;$script:AppearanceStartedPid=0
  $script:ColdStarted=$false;$script:ColdSlot=0;$script:ColdQueries=0
  $script:ColdCandidateSlots=[Collections.Generic.List[int]]::new()
  $script:ColdListenerSlots=[Collections.Generic.List[int]]::new()
  $script:ColdListenerKinds=[Collections.Generic.List[bool]]::new()
  $before=$script:Spawns;$result=$null;$reason=''
  try{$result=Start-AppearanceBackend}catch{$reason=$_.Exception.Message}
  Assert ($script:Spawns -eq ($before+1) -and $script:ColdSlot -eq $readyAt) "Existing bounded wait and single launch: $scenario"
  $candidateSlots=$script:ColdCandidateSlots.ToArray() -join ','
  $listenerSlots=$script:ColdListenerSlots.ToArray() -join ','
  $kinds=$script:ColdListenerKinds.ToArray()
  $readinessCount=@($kinds|Where-Object {$_}).Count
  $fullCount=@($kinds|Where-Object {!$_}).Count
  $expectedFull=switch($scenario){'Unknown'{3};'OwnerChanged'{5};default{4}}
  Assert (!$kinds[0] -and $readinessCount -eq ($readyAt+1) -and $fullCount -eq $expectedFull) "Only readiness slots may skip unused IPv6 checks: $scenario"
  if($scenario -eq 'Unknown'){
   Assert (!$result -and $reason -ceq 'fixture-listener-unknown' -and !$candidateSlots -and $listenerSlots -eq '0') 'Unknown listener stays a failure, not absence or readiness'
  }else{
   Assert ($result.ok -and $result.started -and $result.resident) "Ready only after a current owner check: $scenario"
   $expected=switch($scenario){'OwnerChanged'{'0,1'};default{[string]$readyAt}}
   $expectedListeners=switch($scenario){'OwnerChanged'{'0,0,1,1'};default{((0..$readyAt)+$readyAt) -join ','}}
   Assert ($candidateSlots -ceq $expected -and $listenerSlots -ceq $expectedListeners) "Skip only negative listener slots and recheck live ownership: $scenario"
  }
  [pscustomobject]@{case=$scenario;waitSlots=$script:ColdSlot;candidateSlots=$script:ColdCandidateSlots.ToArray();listenerSlots=$script:ColdListenerSlots.ToArray();listenerKinds=$kinds}
 }
})}
$admission=@()
if($Case -in 'All','Admission'){$admission=@(& {
 foreach($name in 'Get-AppearanceBackendCandidates','Get-AppearanceProcessCandidates'){
  Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\resident.ps1') $name)
 }
 foreach($name in 'Test-CssDesktopEditing','Assert-CssDesktopNotEditing','Stop-AppearanceIdle'){
  Invoke-Expression (Load-Function (Join-Path $ComponentRoot 'steam-appearance\idle.ps1') $name)
 }
 function Update-AppearanceProcessInventory {
  $script:AdmissionInventories++
  if($script:AdmissionInventories -eq 2){
   if($scenario -in 'LateSame','LateOther'){$script:Rows=@(Row 100 0);if($scenario -ceq 'LateOther'){$script:Rows[0].Path='D:\other\CssLoader-Standalone-Headless.exe'}}
   if($scenario -ceq 'Editing'){$script:Rows=@(Row 150 0);$script:Rows[0].Path='C:\fixture\CSSLoader Desktop.exe'}
  }
  $observation=@(foreach($p in $script:Rows){
   if($p.Id -ne 102){[pscustomobject]@{kind=$(if($p.Id -eq 150){2}else{1});pid=[uint32]$p.Id;birth=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}}
  })
  $script:AppearanceProcessReceipt=[pscustomobject]@{rows=$observation}
  $script:AppearanceProcessReceipt
 }
 function Get-AppearanceTcpListeners([int]$LocalPort){
  if($LocalPort -ne 35821){throw 'Unexpected listener port'}
  $script:AdmissionListeners++
  if($scenario -ceq 'PortAtLaunch' -and $script:AdmissionListeners -eq 3){[pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=999};return}
  if($scenario -in 'Warm','WarmOther' -or ($scenario -ceq 'LateSame' -and $script:AdmissionWaits)){
   [pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=100}
   if($scenario -ceq 'WarmOther' -and $script:AdmissionListeners -ge 2){[pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=102}}
  }
 }
 function Get-AppearanceProcessPath($Process){
  if($scenario -ceq 'Unreadable'){throw 'fixture-image-unreadable'}
  $Process.Path
 }
 function Start-Sleep([int]$Milliseconds){
  if($Milliseconds -ne 500 -or $script:AdmissionWaits){throw 'Unexpected wait'}
  $script:AdmissionWaits++
 }
 function Start-ResidentAppearanceBackend {$script:AdmissionSpawns++;throw 'Unexpected launch at admission boundary'}
 foreach($scenario in 'LateSame','LateOther','Warm','WarmOther','Editing','PortAtLaunch','Unreadable'){
  $script:AdmissionInventories=0;$script:AdmissionListeners=0;$script:AdmissionWaits=0;$script:AdmissionSpawns=0
  $script:AppearanceStartedPid=0;$script:AppearanceProcessReceipt=$null;$script:ListenerProcess=$null;$script:Rows=@()
  if($scenario -in 'Warm','WarmOther','Unreadable'){$script:Rows=@(Row 100 0)}
  if($scenario -ceq 'WarmOther'){$extra=Row 102 0;$extra.Path='D:\other\CssLoader-Standalone-Headless.exe';$script:Rows+=,$extra}
  $result=$null;$reason=''
  try{$result=Start-AppearanceBackend}catch{$reason=$_.Exception.Message}
  Assert (!$script:AdmissionSpawns) "No launch on $scenario (actual $($script:AdmissionSpawns)): $reason"
  switch($scenario){
   LateSame {Assert ($result.ok -and !$result.started -and $script:AdmissionInventories -eq 2 -and $script:AdmissionWaits -eq 1) 'A new shared inventory member without a listener is preserved and verified after the bounded wait'}
   LateOther {Assert (!$result -and $reason -like 'Another CSSLoader path*' -and $script:AdmissionInventories -eq 2 -and !$script:AdmissionWaits) 'A competing installation present in the refreshed inventory blocks launch even without a listener'}
   Warm {Assert ($result.ok -and !$result.started -and $script:AdmissionInventories -eq 1 -and $script:AdmissionListeners -eq 3 -and !$script:AdmissionWaits) 'A healthy resident omits one duplicate candidate observation but retains a fresh owner check'}
   WarmOther {Assert (!$result -and $reason -like 'Another CSSLoader path*' -and $script:AdmissionListeners -eq 2) 'A competing process observed by the readiness listener is not filtered away'}
   Editing {Assert (!$result -and $reason -like 'CSSLoader Desktop is open*' -and $script:AdmissionInventories -eq 2 -and $script:AdmissionListeners -eq 1) 'Desktop still owns the launch boundary'}
   PortAtLaunch {Assert (!$result -and $reason -like 'CSSLoader port 35821 is occupied*' -and $script:AdmissionListeners -eq 3) 'The final port guard still detects a late port owner'}
   Unreadable {Assert (!$result -and $reason -ceq 'fixture-image-unreadable' -and !$script:AdmissionWaits) 'Unreadable candidate paths fail closed'}
  }
  [pscustomobject]@{scenario=$scenario;ok=[bool]($result -and $result.ok);reason=$reason;inventories=$script:AdmissionInventories;listeners=$script:AdmissionListeners;waits=$script:AdmissionWaits;spawns=$script:AdmissionSpawns}
 }
})}
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0;fixtureSpawns=$script:Spawns;hashReads=$script:HashReads;coldWait=$coldWait;admission=$admission}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) resident singleton and resource budget checks"
