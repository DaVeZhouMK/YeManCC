[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='',[ValidateSet('All','ControlLock','InitialOwnedStatus','StatusIdentity','AutomaticIdentity','FinalRevision','FinalIdleDom','FinalMainUnavailable','BackupStatusCost','BundleProjection','CorrectionRevision','CorrectionReadRace','CorrectionScope','CorrectionBusy','CorrectionMainUnavailable','CorrectionAlreadyMatched','CorrectionResultScope','CorrectionAdmission','PostCorrectionBackendDrift','PostCorrectionPresetDrift','ReloadEditor','PrepareEditor','ReloadRevision','ReloadCoalesced','InvalidSelection','ReloadFinalScope','OverdueSlots','EnsureRevisionOnly','ManualRefreshRevision','ApplyRenderReceipt','ScopedApply','AutomaticPendingStatus')][string]$Case='All',
 [ValidateSet('document-main-unavailable','document-replaced','document-transport-unavailable','document-targets-refreshed')][string]$DocumentInterruption='document-main-unavailable')
$ErrorActionPreference='Stop'
if($DocumentInterruption -ceq 'document-replaced' -and $Case -cnotin @('CorrectionMainUnavailable','FinalMainUnavailable')){throw 'DocumentInterruption requires a document recheck case'}
if($DocumentInterruption -ceq 'document-transport-unavailable' -and $Case -cne 'FinalMainUnavailable'){throw 'Transport unavailability requires the final read-only recheck case'}
if($DocumentInterruption -ceq 'document-targets-refreshed' -and $Case -cnotin @('CorrectionMainUnavailable','FinalMainUnavailable')){throw 'Target refresh requires a document recheck case'}
$project=Split-Path -Parent $PSScriptRoot
if(!$ComponentRoot){$ComponentRoot=Join-Path $project 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
# Load exact production code with all process/port IO fenced before dot sourcing.
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
foreach($name in @('Invoke-AppearanceControl','Invoke-AutomaticAppearanceRefresh')){
 $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
 if(!$fn){throw ('Missing production function: '+$name)};Invoke-Expression $fn.Extent.Text
}
$SessionPid=123;$SessionBirth='456';$script:CurrentPid=123;$script:CurrentBirth='456'
$script:ThemeRoot=Join-Path $Out 'profile\homebrew\themes';$script:PresetStateBase=Join-Path $Out 'states'
$script:BackendExe=Join-Path $Out 'CssLoader-Standalone-Headless.exe';[IO.File]::WriteAllText($script:BackendExe,'inert placeholder; not executable')
$script:Live=$false;$script:Rows=@();$script:Applied=0;$script:Resets=0;$script:Starts=0;$script:Rendered=$false;$script:Delay=@();$script:Editing=$false;$script:NoReady=$false;$script:StatusCalls=0;$script:FailApply=$false
function Start-Sleep {param($Milliseconds,$Seconds) $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds}}
function Get-AppearanceSteamSession {[pscustomobject]@{pid=$script:CurrentPid;birth=$script:CurrentBirth;root='C:\fixture-steam';backendExe=$script:BackendExe}}
function Get-BackendProcesses {if($script:Live -and !$script:NoReady){[pscustomobject]@{Id=42;Path=$script:BackendExe}}}
function Test-BackendOwner {$script:Live -and !$script:NoReady}
function Test-CssDesktopEditing {$script:Editing}
$script:MissingStyles=$null;$script:Unverifiable=$false;$script:KeepMissing=$false;$script:RenderSamples=0;$script:TransientUnreadable=0;$script:ClearAfterReads=0;$script:ChangeAfterCorrection=$false
function Get-AppearanceRenderedStatus($Session,$Status){
    $script:RenderSamples++
    if($script:ChangeAfterCorrection -and $script:Applied -gt 0){
        $path=Join-Path $script:ThemeRoot 'Round\config_USER.json';$external=[IO.File]::ReadAllText($path)|ConvertFrom-Json;$external.active=$false
        Write-ThemeConfigAtomic $path ($external|ConvertTo-Json -Compress);$script:ChangeAfterCorrection=$false
    }
    if($script:TransientUnreadable -gt 0){$script:TransientUnreadable--;return [pscustomobject]@{checked=$false;confirmed=$false;reason='render-port-missing';mismatched=@()}}
    if($script:ClearAfterReads -gt 0 -and $script:Applied -gt 0){$script:ClearAfterReads--;if(!$script:ClearAfterReads){$script:MissingStyles=@()}}
    if($script:Unverifiable){return [pscustomobject]@{checked=$false;confirmed=$false;reason='render-evaluation-unconfirmed';mismatched=@()}}
    $missing=if($null -ne $script:MissingStyles){@($script:MissingStyles)}elseif($script:Rendered){@()}elseif(!$Status.configuredMatchesBackend){@($Status.themes|Where-Object {$_.backendEnabled -ne $_.configured}|ForEach-Object name)}else{@($Status.themes|Where-Object configured|ForEach-Object name)}
    [pscustomobject]@{checked=$true;confirmed=($missing.Count -eq 0);reason='fixture-render-receipts';mismatched=@($missing)}
}
function Test-AppearanceRendered($Session,$Status){(Get-AppearanceRenderedStatus $Session $Status).confirmed}
function Start-AppearanceBackend {$script:Starts++;throw 'Automatic refresh cannot start any process'}
function Stop-VerifiedIdleProcesses {throw 'Automatic refresh cannot stop a process'}
function Clear-OldLoaderStyles {throw 'No old-style deletion expected'}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
 switch($Method){
  fetch_theme_path {return $script:ThemeRoot}
  get_themes {return $script:Rows}
  reset {$script:Resets++;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});return [pscustomobject]@{fails=@()}}
  set_theme_state {if($script:FailApply){throw 'fixture live apply failed'};$script:Applied++;($script:Rows|Where-Object name -eq $Arguments.name).enabled=$Arguments.state;$script:Rendered=$true;if($null -ne $script:MissingStyles -and !$script:KeepMissing){$script:MissingStyles=@($script:MissingStyles|Where-Object {$_ -ne $Arguments.name})};return [pscustomobject]@{success=$true}}
  default {throw ('Unexpected loader method: '+$Method)}
 }
}
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,[string]$Label){if(!$Value){throw $Label};$cases.Add($Label)}
function Test-FixtureThemePagePlans($Themes){
 if(@($Themes).Count -ne 7){return $false}
 foreach($theme in $Themes){
  $sp=$theme.renderPlan;$menu=$sp.contexts.MainMenu;$quick=$sp.contexts.QuickAccess
  if($sp.contextProtocol -ne 1 -or !$sp.files.Count -or $sp.protocol -ne 4 -or $menu.protocol -ne 4 -or $quick.protocol -ne 4){return $false}
  if($menu.files.Count -ne $sp.files.Count -or $quick.files.Count -ne $sp.files.Count -or
     $menu.variables.Count -ne $sp.variables.Count -or $quick.variables.Count -ne $sp.variables.Count -or
     [object]::ReferenceEquals($sp.files,$menu.files) -or [object]::ReferenceEquals($sp.files,$quick.files) -or
     [object]::ReferenceEquals($menu.files,$quick.files)){return $false}
 }
 return $true
}
function Reject([scriptblock]$Work){try{& $Work|Out-Null;return $false}catch{return $true}}
if($Case -eq 'All'){
Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
$before=@{};foreach($name in $script:ThemeNames){$before[$name]=Get-ConfigHash (Join-Path $script:ThemeRoot ($name+'\config_USER.json'))}
$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 7 -and $script:Rendered) 'Already running CSS with 7/7 matching flags executes the same seven theme submissions as manual Refresh'
Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'Matching resources never start Steam/CSS, reset the JS context or globally reload the CSS engine'
foreach($name in $script:ThemeNames){Assert ((Get-ConfigHash (Join-Path $script:ThemeRoot ($name+'\config_USER.json'))) -eq $before[$name]) ('Automatic refresh preserves exact config and parameter bytes: '+$name)}
$script:Applied=0;$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 0) 'Healthy matching document performs zero theme writes'
$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$cfg=Get-Content -LiteralPath $round -Raw|ConvertFrom-Json;$cfg.active=$false;$cfg.Intensity='30px';Write-ThemeConfigAtomic $round ($cfg|ConvertTo-Json -Compress)
$script:Applied=0;$script:Rendered=$false;$script:Editing=$false;$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 1 -and !($result.snapshot.themes|Where-Object name -eq 'Round').configured) 'Automatic reapply honors a real saved OFF flag and corrects mismatched backend state'
Assert ((Get-Content -LiteralPath $round -Raw|ConvertFrom-Json).Intensity -eq '30px') 'Automatic reapply retains personal effect parameters'
Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'Single-theme reconciliation never cleans up or creates a duplicate backend'
$script:Editing=$true;$script:Rendered=$false;$count=$script:Applied
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Desktop editing postpones automatic reconciliation instead of writing to the editor-owned backend'
Assert ($script:Applied -eq $count -and $script:Starts -eq 0 -and $script:Resets -eq 0) 'Editor deferral performs zero theme writes, backend resets or process launches'
$script:Editing=$false;$script:CurrentBirth='different';$count=$script:Applied
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Reused PID or changed Steam birth is rejected before any reapply'
Assert ($script:Applied -eq $count) 'Stale session produces no loader mutation'
$script:CurrentBirth='456';$script:NoReady=$true;$script:Delay=@()
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Missing backend readiness exits after one finite loading window'
Assert ($script:Delay.Count -eq 4 -and $script:Applied -eq $count -and $script:Starts -eq 0) 'Readiness uses only four spaced waits, never a startup fallback or permanent polling'
$script:NoReady=$false;$script:Rendered=$false;$script:FailApply=$true
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Loader rejection is reported instead of falsely confirming applied appearance'
$script:FailApply=$false
$script:Rows=@($script:Rows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$false}});$script:Rendered=$false
$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Rendered -and @($result.snapshot.themes|Where-Object configured).Count -eq 6) 'A later admitted document reuses the real Refresh path and preserves six enabled and one saved OFF choices'
Assert ($script:Starts -eq 0) 'All production refresh branches performed zero external process launches'

$script:Applied=0;$script:MissingStyles=@('Mini Carousel');$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 1 -and $script:MissingStyles.Count -eq 0) 'One missing theme receipt is repaired with exactly one submission despite all backend flags matching'
$script:Applied=0;$script:MissingStyles=@('Round','NEX-85 Keyboard');$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 2) 'Two missing theme receipts repair only the two related themes including a disabled stale theme'
$script:Applied=0;$script:Unverifiable=$true
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'An unreadable or rejected DOM receipt is not permission to blindly reapply all themes'
Assert ($script:Applied -eq 0) 'Unknown rendering performs zero CSS writes and never reports successful application'
$script:Unverifiable=$false;$script:MissingStyles=@('Mini Carousel');$script:KeepMissing=$true;$script:Applied=0;$samples=$script:RenderSamples;$script:Delay=@()
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Backend success with a still-missing receipt remains unconfirmed instead of masking an injection error'
Assert ($script:Applied -eq 1 -and $script:RenderSamples-$samples -eq 7 -and $script:Delay.Count -eq 4) 'Persistent missing receipt exhausts one correction and seven bounded probes with no retry loop'
Assert ($script:AppearanceHelperPhase -eq 5 -and $script:AppearanceFailureCode -eq 6 -and ($script:AppearanceHelperFlags -band 3) -eq 3 -and !($script:AppearanceHelperFlags -band 4)) 'Persistent missing CSS is logged as actual DOM failure with known backend/config flags, not initialization failure'
$script:MissingStyles=@();$script:KeepMissing=$false

# Explicit manual operations also need actual rendering; backend flags alone
# must not turn a swallowed injection error into a successful UI acknowledgement.
function Get-Process {param($Name,$Id) if($Name -eq 'steam'){[pscustomobject]@{Id=123;SessionId=99}}}
function Test-CurrentUserProcess($Process){$Process -and $Process.SessionId -eq 99}
$script:MissingStyles=@('Mini Carousel');$script:KeepMissing=$true;$script:Applied=0
$manual=Invoke-AppearanceControl 'Refresh'
Assert (!$manual.ok -and $manual.saved -and !$manual.renderConfirmed -and $script:Applied -eq 7) 'Manual Refresh preserves configuration and reports unconfirmed when the engine succeeds but a theme is still missing'
$baseline=Get-AppearanceStatus
$Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(([pscustomobject]@{expectedRevision=$baseline.revision;states=[pscustomobject]@{Round=$true}}|ConvertTo-Json -Depth 5 -Compress)))
$manual=Invoke-AppearanceControl 'Apply'
Assert (!$manual.ok -and $manual.saved -and !$manual.renderConfirmed -and ($manual.snapshot.themes|Where-Object name -eq 'Round').configured) 'Manual Apply truthfully retains the newly saved switch while a separate rendering receipt is unconfirmed'
$Payload='';$script:MissingStyles=@();$script:KeepMissing=$false;$manual=Invoke-AppearanceControl 'Refresh'
Assert ($manual.ok -and $manual.renderConfirmed) 'Manual Refresh reports success only after the per-theme render receipts agree'

$script:MissingStyles=@();$script:TransientUnreadable=2;$script:Applied=0;$script:Delay=@();$samples=$script:RenderSamples
$delayed=Invoke-AutomaticAppearanceRefresh
Assert ($delayed.ok -and $script:Applied -eq 0 -and $script:RenderSamples-$samples -eq 3 -and $script:Delay.Count -eq 2) 'A slowly appearing CDP renderer becomes confirmed through spaced read-only checks without any theme rewrite'
$script:MissingStyles=@('Mini Carousel');$script:KeepMissing=$true;$script:ClearAfterReads=2;$script:Applied=0;$script:Delay=@()
$delayed=Invoke-AutomaticAppearanceRefresh
Assert ($delayed.ok -and $script:Applied -eq 1 -and $script:Delay.Count -eq 1) 'A delayed injection after the one correction is verified later without replaying the transaction'
$script:MissingStyles=@();$script:KeepMissing=$false;$script:Unverifiable=$true;$script:Applied=0;$script:Delay=@();$samples=$script:RenderSamples
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'Permanently unavailable CDP exhausts the fixed verification window'
Assert ($script:RenderSamples-$samples -eq 5 -and $script:Delay.Count -eq 4 -and $script:Applied -eq 0) 'Unknown DOM is capped at five reads and four waits with zero process starts or CSS writes'
$script:Unverifiable=$false

$roundPath=Join-Path $script:ThemeRoot 'Round\config_USER.json';$value=[IO.File]::ReadAllText($roundPath)|ConvertFrom-Json;$value.active=$true;Write-ThemeConfigAtomic $roundPath ($value|ConvertTo-Json -Compress)
($script:Rows|Where-Object name -eq 'Round').enabled=$true
$script:MissingStyles=@('Mini Carousel');$script:KeepMissing=$true;$script:Applied=0;$script:ChangeAfterCorrection=$true
Assert (Reject {Invoke-AutomaticAppearanceRefresh}) 'External configuration changes after the one correction stop all later automatic mutations'
Assert ($script:Applied -eq 1 -and !([IO.File]::ReadAllText($roundPath)|ConvertFrom-Json).active) 'An external user OFF choice survives the late verification and is never overwritten with the old revision'
}
if($Case -eq 'ManualRefreshRevision'){& {
 $script:AppearanceContentManifestFile=Join-Path $Out 'content-index.json'
 Copy-Item -LiteralPath (Join-Path $ComponentRoot 'steam-appearance/manifest.json') -Destination $script:AppearanceContentManifestFile
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload;$script:Live=$true
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $round=Join-Path $script:ThemeRoot 'Round/config_USER.json';$manifest=Join-Path $script:ThemeRoot 'Round/theme.json'
 $oldConfig=[IO.File]::ReadAllBytes($round);$oldManifest=[IO.File]::ReadAllBytes($manifest);$oldIndex=[IO.File]::ReadAllBytes($script:AppearanceContentManifestFile)
 $script:RefreshOriginalRows=${function:Get-ThemeRows};$script:RefreshOriginalParse=${function:Read-AppearanceThemeConfig}
 $script:RefreshOriginalHash=${function:Get-ConfigHash}
 function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
  if($script:RefreshRowCalls -eq 2){$script:RefreshParses++}
  & $script:RefreshOriginalParse @PSBoundParameters
 }
 function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0,$ObservedLength=$null){
  if($script:RefreshRowCalls -eq 2 -and $MaximumBytes -eq 262144){$script:RefreshHashes++}
  & $script:RefreshOriginalHash @PSBoundParameters
 }
 function Get-ThemeRows {
  param([switch]$RevisionOnly,[switch]$DigestOnly,[hashtable]$AutomaticPlanMemo,[switch]$ValidateRenderOnly,[switch]$ReadOnlyProjection)
  $script:RefreshRowCalls++
  if($script:RefreshRowCalls -eq 2){
   if($script:RefreshFault -ceq 'config'){[IO.File]::WriteAllText($round,'{"active":false,"Intensity":"30px"}')}
   if($script:RefreshFault -ceq 'manifest'){[IO.File]::AppendAllText($manifest,' ')}
   if($script:RefreshFault -ceq 'index'){[IO.File]::AppendAllText($script:AppearanceContentManifestFile,' ')}
   if($script:RefreshFault -ceq 'locked'){$script:RefreshLocked=[IO.File]::Open($round,'Open','ReadWrite','None')}
   if($script:RefreshFault -ceq 'missing'){[IO.File]::Delete($round)}
  }
  if($script:RefreshRowCalls -eq 3){
   if($script:RefreshFault -ceq 'final-config'){[IO.File]::WriteAllText($round,'{"active":false,"Intensity":"30px"}')}
   if($script:RefreshFault -ceq 'final-manifest'){[IO.File]::AppendAllText($manifest,' ')}
  }
  & $script:RefreshOriginalRows @PSBoundParameters
 }
 function Reset-ManualRefresh([string]$Fault=''){
  [IO.File]::WriteAllBytes($round,$oldConfig);[IO.File]::WriteAllBytes($manifest,$oldManifest)
  [IO.File]::WriteAllBytes($script:AppearanceContentManifestFile,$oldIndex)
  $script:RefreshFault=$Fault;$script:RefreshRowCalls=0;$script:RefreshParses=0;$script:RefreshHashes=0
  $script:Applied=0;$script:RenderSamples=0;$script:RefreshLocked=$null
 }
 Reset-ManualRefresh
 $result=Invoke-AppearanceControl 'Refresh'
 [pscustomobject]@{parses=$script:RefreshParses;digests=$script:RefreshHashes;rowReads=$script:RefreshRowCalls;writes=$script:Applied;renderReads=$script:RenderSamples;result=$result}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $Out 'manual-refresh-cost.json') -Encoding UTF8
 Assert ($result.ok -and $result.renderConfirmed -and $script:RefreshRowCalls -eq 3 -and $script:Applied -eq 7 -and $script:RenderSamples -eq 1) 'Manual refresh still writes the requested seven theme states and uses an independent final DOM receipt'
 Assert ($script:RefreshParses -eq 0 -and $script:RefreshHashes -eq 14) 'The second read should hash fourteen files without decoding validated JSON a second time'
 foreach($fault in 'config','manifest','index','locked','missing'){
  Reset-ManualRefresh $fault
  $reason=''
  try{Invoke-AppearanceControl 'Refresh'|Out-Null}catch{$reason=$_.Exception.Message}
  finally{if($script:RefreshLocked){$script:RefreshLocked.Dispose()}}
  Assert ($reason -like '*changed before refresh*' -and $script:Applied -eq 0 -and $script:RenderSamples -eq 0) ('Stale or unreadable pre-write bytes never submit an old state: '+$fault)
  if($fault -ceq 'config'){Assert ((Get-Content -LiteralPath $round -Raw|ConvertFrom-Json).Intensity -ceq '30px') 'External config edit is preserved on rejected refresh'}
 }
 Reset-ManualRefresh;$script:MissingStyles=@('Mini Carousel');$script:KeepMissing=$true
 $result=Invoke-AppearanceControl 'Refresh'
 Assert (!$result.ok -and !$result.renderConfirmed -and $result.saved -and $script:RenderSamples -eq 1) 'Applying existing state does not hide a missing final DOM receipt'
 $renderRefresh=${function:Get-AppearanceRenderedStatus}
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:RefreshSawFullPlans=Test-FixtureThemePagePlans $Status.themes
  $render=& $renderRefresh $Session $Status
  $render|Add-Member -NotePropertyName contextComplete -NotePropertyValue ($mode -notin @('partial','unconfirmed'))
  $render
 }
 $summaryJson='{"profile":"ui-summary-v1"}'
 $Action='Refresh'
 foreach($mode in 'legacy','summary','partial','unconfirmed','wrong-case','array','internal'){
  Reset-ManualRefresh
  $script:RefreshSawFullPlans=$false
  $lean=$mode -in @('summary','partial','unconfirmed')
  $script:MissingStyles=if($mode -eq 'unconfirmed'){@('Mini Carousel')}else{@()}
  $script:KeepMissing=$mode -eq 'unconfirmed'
  $Action=if($mode -eq 'internal'){'Automatic'}else{'Refresh'}
  $profile=switch($mode){legacy{''};array{'['+$summaryJson+']'};'wrong-case'{'{"profile":"UI-summary-v1"}'};default{$summaryJson}}
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($profile))
  $wire=Invoke-AppearanceControl 'Refresh'
  Assert ($script:RefreshSawFullPlans -and $script:Applied -eq 7 -and $script:RenderSamples -eq 1 -and $wire.saved -and
    $wire.ok -eq ($mode -ne 'unconfirmed') -and $wire.renderConfirmed -eq $wire.ok) ('Refresh still submits and independently validates full page plans: '+$mode)
  Assert ($wire.renderContextsComplete -is [bool] -and $wire.renderContextsComplete -eq ($mode -notin @('partial','unconfirmed'))) ('Refresh only reports complete contexts from actual render evidence: '+$mode)
  $withPlan=@($wire.snapshot.themes|Where-Object {$_.PSObject.Properties['renderPlan']}).Count
  Assert ($wire.snapshot.themes.Count -eq 7 -and $withPlan -eq (7*[int](!$lean))) ('Refresh omits only unused wire plans for an opted-in caller: '+$mode)
  if($mode -eq 'legacy' -or $mode -eq 'summary'){
   $stdout=[IO.StringWriter]::new();$console=[Console]::Out
   try{[Console]::SetOut($stdout);Write-JsonResult $wire}finally{[Console]::SetOut($console);$stdout.Dispose()}
   [IO.File]::WriteAllText((Join-Path $Out ('refresh-'+$mode+'.json')),$stdout.ToString())
  }
 }
 $Action='Status';$Payload=''
 $observed=(Invoke-AppearanceControl 'Status').snapshot
 Assert ($observed.refreshScopeProtocol -eq 1) 'Status advertises the scope-checking Refresh action'
 $scope=$observed.targetId;$BackendPath=$script:BackendExe
 $script:ScopedRpc=${function:Invoke-LoaderRpc}
 $script:ScopedPathReads=0;$script:ScopedMoveAt=0;$script:ScopedEditBeforeReset=$false
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($Method -ceq 'fetch_theme_path'){
   $script:ScopedPathReads++
   if($script:ScopedMoveAt -gt 0 -and $script:ScopedPathReads -ge $script:ScopedMoveAt){return (Join-Path $Out 'different/themes')}
   if($script:ScopedEditBeforeReset -and $script:ScopedPathReads -eq 2){
    $text=[IO.File]::ReadAllText($round)
    $updated=$text.Replace('"10px"','"30px"')
    $length=[IO.FileInfo]::new($round).Length
    $stamp=[IO.File]::GetLastWriteTimeUtc($round)
    [IO.File]::WriteAllText($round,$updated,[Text.UTF8Encoding]::new($false))
    [IO.File]::SetLastWriteTimeUtc($round,$stamp)
    $script:ScopedEditApplied=$text -cne $updated -and [IO.FileInfo]::new($round).Length -eq $length -and [IO.File]::GetLastWriteTimeUtc($round) -eq $stamp
   }
  }
  & $script:ScopedRpc $Method $Arguments
 }
 function Invoke-ScopedFixture([string]$Target=$scope){
  $Action='ScopedRefresh'
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{profile='ui-summary-v1';targetId=$Target}|ConvertTo-Json -Compress)))
  $script:ScopedPathReads=0
  Invoke-AppearanceControl 'ScopedRefresh'
 }
 Reset-ManualRefresh;$mode='summary';$script:MissingStyles=@();$script:KeepMissing=$false
 [IO.File]::WriteAllText($round,'{"active":false,"Intensity":"30px"}')
 $result=Invoke-ScopedFixture
 Assert ($result.ok -and $result.renderConfirmed -and $result.snapshot.targetId -eq $scope -and $result.snapshot.revision -ne $observed.revision -and
   $script:Applied -eq 7 -and $script:RenderSamples -eq 1 -and !$result.snapshot.themes.Where({$_.name -ceq 'Round'})[0].configured -and
   !$script:Rows.Where({$_.name -ceq 'Round'})[0].enabled) 'Scoped refresh uses the current disk choice regardless of the UI revision'
 foreach($fault in 'wrong-target','moved-before-status','moved-after-status','changed-config','malformed'){
  Reset-ManualRefresh $(if($fault -eq 'changed-config'){'config'}else{''})
  $script:ScopedMoveAt=switch($fault){'moved-before-status'{1};'moved-after-status'{2};default{0}}
  $targetToSend=switch($fault){'wrong-target'{'0123456789abcdef'};malformed{'unknown'};default{$scope}}
  Assert ((Reject {Invoke-ScopedFixture $targetToSend}) -and !$script:Applied -and !$script:RenderSamples) ('Scoped refresh rejects an invalid or changed write scope: '+$fault)
 }
 Reset-ManualRefresh;$script:ScopedMoveAt=3
 Assert ((Reject {Invoke-ScopedFixture}) -and $script:Applied -eq 7 -and !$script:RenderSamples) 'A target that moves after submission cannot return a confirmed scoped receipt'
 Reset-ManualRefresh;$script:ScopedMoveAt=0;$script:Live=$false
 $declined=Invoke-ScopedFixture
 Assert (!$declined.ok -and $declined.refreshPreparationRequired -eq $true -and !$declined.snapshot -and !$declined.refreshReload -and !$script:Applied -and !$script:RenderSamples) 'An absent backend produces only a pre-write decline receipt'
 $script:Live=$true
 $journal=Join-Path (Get-PresetStateDir) 'transaction.json'
 [IO.File]::WriteAllText($journal,'untouched fixture marker')
 Assert ((Reject {Invoke-ScopedFixture}) -and !$script:Applied -and !$script:RenderSamples -and [IO.File]::ReadAllText($journal) -ceq 'untouched fixture marker') 'An unfinished transaction is neither recovered nor accepted by scoped refresh'
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'No backend startup or global reset is necessary'
 # The real batch reader is used only for these post-submission cases; the
 # earlier assertions intentionally exercise the per-file compatibility path.
 Move-Item -LiteralPath $journal -Destination (Join-Path $Out 'untouched-transaction-marker')
 function Set-FixturePendingReload {
  $owner=Read-PresetState;$owner.pendingReload=$true;Save-PresetState $owner
 }
 function Invoke-FixtureScopedReload($Receipt,[string]$Profile='ui-summary-v1'){
  $Action='ScopedReload'
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{profile=$Profile;targetId=$Receipt.targetId;expectedRevision=$Receipt.revision}|ConvertTo-Json -Compress)))
  $script:ScopedPathReads=0
  Invoke-AppearanceControl 'ScopedReload'
 }
 Reset-ManualRefresh;Set-FixturePendingReload
 $script:Resets=0
 $declined=Invoke-ScopedFixture
 $receipt=$declined.refreshReload
 Assert (!$declined.ok -and !$declined.snapshot -and $declined.refreshPreparationRequired -and $receipt.targetId -ceq $scope -and
   $receipt.revision -match '^[a-f0-9]{64}$' -and $receipt.steamPid -eq $SessionPid -and $receipt.steamBirth -ceq $SessionBirth -and
   !$script:Resets -and !$script:Applied -and !$script:RenderSamples) 'A pending reload returns exactly scoped fresh identities before any write'
 foreach($fault in 'editor','different-directory','changed-revision','journal','invalid-profile'){
  $script:Editing=$fault -ceq 'editor';$script:ScopedMoveAt=if($fault -ceq 'different-directory'){2}else{0}
  if($fault -ceq 'changed-revision'){[IO.File]::WriteAllText($round,'{"active":false,"Intensity":"30px"}')}
  if($fault -ceq 'journal'){[IO.File]::WriteAllText($journal,'unfinished reload fixture')}
  Assert ((Reject {Invoke-FixtureScopedReload $receipt $(if($fault -ceq 'invalid-profile'){'unknown'}else{'ui-summary-v1'})}) -and
    !$script:Resets -and !$script:Applied -and !$script:RenderSamples -and (Read-PresetState).pendingReload) ('Scoped reload stops before writing: '+$fault)
  if($fault -ceq 'journal'){
   Assert ([IO.File]::ReadAllText($journal) -ceq 'unfinished reload fixture') 'Scoped reload does not recover the observed unfinished transaction'
   Move-Item -LiteralPath $journal -Destination (Join-Path $Out 'untouched-reload-marker')
  }
  Reset-ManualRefresh
 }
 $script:Editing=$false;$script:ScopedMoveAt=0
 $script:ScopedEditBeforeReset=$true;$script:ScopedEditApplied=$false;$reloadRejected=''
 try{Invoke-FixtureScopedReload $receipt|Out-Null}catch{$reloadRejected=$_.Exception.Message}
 finally{$script:ScopedEditBeforeReset=$false}
 Assert ($script:ScopedEditApplied -and $reloadRejected -like '*settings or target changed*' -and !$script:Resets -and !$script:Applied -and
   !$script:RenderSamples -and (Read-PresetState).pendingReload -and ([IO.File]::ReadAllText($round)|ConvertFrom-Json).Intensity -ceq '30px') 'An equal-size, same-stamp edit during final target read stops before reset and keeps the new setting'
 Reset-ManualRefresh
 $loaded=Invoke-FixtureScopedReload $receipt
 Assert ($loaded.ok -and $loaded.reloaded -and $loaded.snapshot.targetId -ceq $scope -and $loaded.previousRevision -ceq $receipt.revision -and
   !$loaded.snapshot.backendReloadRequired -and $script:Resets -eq 1 -and !$script:RenderSamples -and !(Read-PresetState).pendingReload) 'Scoped reload only rereads the verified resident and does not claim rendering confirmation'
 $already=Invoke-FixtureScopedReload $receipt
 Assert ($already.ok -and !$already.reloaded -and $script:Resets -eq 1) 'An independently completed reload does not repeat the loader reset'
 Reset-ManualRefresh
 $reapplied=Invoke-ScopedFixture
 Assert ($reapplied.ok -and $reapplied.renderConfirmed -and $script:Applied -eq 7 -and $script:RenderSamples -eq 1) 'After scoped reload, final refresh still forces the explicit request and reads the page once'
 Initialize-ResidentAppearanceInterop
 Reset-ManualRefresh 'final-config'
 $result=Invoke-ScopedFixture
 $roundResult=$result.snapshot.themes.Where({$_.name -ceq 'Round'})[0]
 Assert ($result.saved -and !$result.ok -and !$result.snapshot.configuredMatchesBackend -and !$roundResult.configured -and $roundResult.backendEnabled -and
  $script:Applied -eq 7 -and $script:RenderSamples -eq 1) 'Final batch must read an external config change after sync instead of confirming old flags'
 Reset-ManualRefresh 'final-manifest'
 $result=Invoke-ScopedFixture
 Assert ($result.snapshot.packPrepared -eq $false -and $result.snapshot.packRepairRequired -and $script:Applied -eq 7 -and $script:RenderSamples -eq 1) 'Final installed validation must report manifest drift despite reusing bundle schema'
}}
if($Case -eq 'ApplyRenderReceipt'){& {
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload;$script:Live=$true
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $applyBackupPath=(Read-PresetState).originalBackup;$readApplyBackup=${function:Read-PresetBackup}
 function Read-PresetBackup([string]$Directory){
  if($Directory -ieq $applyBackupPath){$script:ApplyBackupReads++}
  & $readApplyBackup $Directory
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:RenderSamples++
  Assert (Test-FixtureThemePagePlans $Status.themes) 'Apply supplies full plans to the independent DOM verifier'
  [pscustomobject]@{checked=$true;confirmed=$confirmed;contextComplete=$complete;reason='fixture-render-receipts';mismatched=@()}
 }
 $Action='Apply'
 foreach($mode in 'complete','partial','mismatch'){
  $confirmed=$mode -ne 'mismatch';$complete=$mode -ne 'partial'
  $old=Get-AppearanceStatus;$enabled=!($old.themes|Where-Object name -eq 'Round').configured
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{profile='ui-summary-v1';expectedRevision=$old.revision;states=@{Round=$enabled}}|ConvertTo-Json -Compress)))
  $script:Applied=0;$script:RenderSamples=0;$script:ApplyBackupReads=0
  $result=Invoke-AppearanceControl 'Apply'
  Assert ($result.saved -and $result.ok -eq $confirmed -and $result.renderConfirmed -is [bool] -and $result.renderConfirmed -eq $confirmed -and $result.renderContextsComplete -is [bool] -and $result.renderContextsComplete -eq $complete) ('Apply returns distinct rendered and complete evidence: '+$mode)
  Assert ($script:Applied -eq 1 -and $script:RenderSamples -eq 1) ('Apply keeps one switch mutation and one final DOM read: '+$mode)
  Assert ($script:ApplyBackupReads -eq 2) ('Apply validates the original backup before writing and once in the final online snapshot: '+$mode)
  Assert (@($result.snapshot.themes|Where-Object {$_.PSObject.Properties['renderPlan']}).Count -eq 0) ('Apply does not send used render plans to the opted-in UI: '+$mode)
 }
 $applyPlan=${function:Get-AppearanceThemeRenderPlan}
 function Get-AppearanceThemeRenderPlan([string]$Name,$Manifest,$Config,$ContentIndex,[string]$Target='SP',[switch]$ValidateOnly){
  if($ValidateOnly){$script:ApplyValidatePlans++}else{$script:ApplyFullPlans++}
  & $applyPlan @PSBoundParameters
 }
 foreach($mode in 'offline','backend-failure','legacy-offline'){
  $script:Live=$mode -eq 'backend-failure';$script:FailApply=$script:Live
  # Retain a deliberately unsupported parameter: lean paths still validate it.
  $configPath=Join-Path $script:ThemeRoot 'Round/config_USER.json'
  [IO.File]::WriteAllText($configPath,'{"active":true,"Intensity":"unsupported"}')
  $old=Get-AppearanceStatus -RevisionOnly
  $body=@{expectedRevision=$old.revision;states=@{'Art Hero'=$false}}
  if($mode -ne 'legacy-offline'){$body.profile='ui-summary-v1'}
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Compress)))
  $script:ApplyValidatePlans=0;$script:ApplyFullPlans=0;$script:RenderSamples=0
  $result=Invoke-AppearanceControl 'Apply'
  $round=$result.snapshot.themes|Where-Object name -eq 'Round'
  Assert ($result.saved -and $result.ok -eq !$script:Live -and !$script:RenderSamples -and $round.error -ceq 'render-patch-selection-invalid') ('Settled Apply still validates a bad saved option without claiming a DOM receipt: '+$mode)
  $legacy=$mode -eq 'legacy-offline'
  Assert ($script:ApplyFullPlans -eq (7*[int]$legacy) -and $script:ApplyValidatePlans -eq (7*[int](!$legacy)) -and
    @($result.snapshot.themes|Where-Object {$_.PSObject.Properties['renderPlan']}).Count -eq (7*[int]$legacy)) ('Only legacy Apply builds wire plans when no page will be read: '+$mode)
  [IO.File]::WriteAllText($configPath,'{"active":true,"Intensity":"10px"}')
 }
 $script:Live=$true;$script:FailApply=$false
 $script:Rows=@(Get-ThemeRows -RevisionOnly|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $old=Get-AppearanceStatus;$round=$old.themes|Where-Object name -eq 'Round'
 $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{expectedRevision=$old.revision;states=@{Round=!$round.configured}}|ConvertTo-Json -Compress)))
 $configPath=Join-Path $script:ThemeRoot 'Round/config_USER.json';$beforeConfig=Get-ConfigHash $configPath
 $originalApplyRoot=$script:ThemeRoot;$otherApplyRoot=Join-Path $Out 'other-themes'
 $invokeApplyRpc=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($script:ChangeApplyTarget -and $Method -ceq 'fetch_theme_path' -and ++$script:ApplyTargetFetches -eq 2){return $otherApplyRoot}
  & $invokeApplyRpc @PSBoundParameters
 }
 $script:ApplyTargetFetches=0;$script:Applied=0;$script:RenderSamples=0;$targetError='';$script:ChangeApplyTarget=$true
 try{Invoke-AppearanceControl 'Apply'|Out-Null}catch{$targetError=$_.Exception.Message}finally{$script:ChangeApplyTarget=$false}
 $marker=Join-Path (Get-PresetStateDir) 'transaction.json'
 Assert ($targetError -like '*target changed*' -and !$script:Applied -and !$script:RenderSamples -and $script:ThemeRoot -ieq $originalApplyRoot -and (Test-Path -LiteralPath $marker)) 'Apply does not synchronize to a changed target or misplace the original transaction'
 Recover-PresetTransaction -Force
 Assert ((Get-ConfigHash $configPath) -ceq $beforeConfig -and !(Test-Path -LiteralPath $marker)) 'Interrupted Apply can restore the original target'
 $journal=Join-Path $applyBackupPath 'journal.json';$beforeJournal=[IO.File]::ReadAllBytes($journal)
 $assertApplyReady=${function:Assert-AppearanceApplyReady}
 function Assert-AppearanceApplyReady($Current){
  & $assertApplyReady $Current
  if($Current.packPrepared){[IO.File]::WriteAllText($journal,'{}');$script:ApplyTampered=$true}
 }
 $newApplyBackup=${function:New-PresetBackup}
 function New-PresetBackup($Paths,[switch]$VerifyAtTransactionStart){$script:ApplyNewBackups++;& $newApplyBackup @PSBoundParameters}
 $script:Applied=0;$script:RenderSamples=0;$script:ApplyBackupReads=0;$script:ApplyNewBackups=0;$script:ApplyTampered=$false;$rejected=$false
 try{Invoke-AppearanceControl 'Apply'|Out-Null}catch{$rejected=$true}
 finally{[IO.File]::WriteAllBytes($journal,$beforeJournal)}
 Assert ($rejected -and $script:ApplyTampered -and $script:ApplyBackupReads -eq 1 -and $script:ApplyNewBackups -eq 0 -and $script:Applied -eq 0 -and $script:RenderSamples -eq 0 -and (Get-ConfigHash $configPath) -ceq $beforeConfig) 'Apply refuses a backup that becomes invalid after preflight, before creating a transaction or writing settings'
 $manifestPath=Join-Path $script:ThemeRoot 'Round/theme.json'
 $indexSource=$script:AppearanceContentManifestFile
 $script:AppearanceContentManifestFile=Join-Path $Out 'apply-content-index.json'
 [IO.File]::Copy($indexSource,$script:AppearanceContentManifestFile)
 [IO.File]::AppendAllText($configPath,"`n")
 $script:ApplyDriftHandle=$null
 function Assert-AppearanceApplyReady($Current){
  & $assertApplyReady $Current
  $script:ApplyDriftInjected=$true
  switch($drift){
   {$_ -in 'config','manifest'} {[IO.File]::WriteAllBytes($driftPath,$driftBytes);[IO.File]::SetLastWriteTimeUtc($driftPath,$driftTime)}
   invalid {[IO.File]::WriteAllText($driftPath,'{"active":"true"}')}
   missing {[IO.File]::Delete($driftPath)}
   locked {$script:ApplyDriftHandle=[IO.File]::Open($driftPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}
   index {[IO.File]::AppendAllText($driftPath,' ')}
   target {$script:ThemeRoot=$otherApplyRoot}
  }
 }
 foreach($drift in 'config','manifest','invalid','missing','locked','index','target'){
  $driftPath=switch($drift){manifest{$manifestPath} index{$script:AppearanceContentManifestFile} default{$configPath}}
  $originalBytes=[IO.File]::ReadAllBytes($driftPath);$driftTime=[IO.File]::GetLastWriteTimeUtc($driftPath)
  $driftBytes=[byte[]]$originalBytes.Clone()
  # Toggle an insignificant whitespace byte at the end. Same valid JSON,
  # length and write time, but a different on-disk revision.
  if($drift -in 'config','manifest'){
   $last=$driftBytes.Length-1
   Assert ($driftBytes[$last] -in 10,13,32) 'Fixture JSON needs trailing whitespace'
   $driftBytes[$last]=9
  }
  $old=Get-AppearanceStatus
  Assert ($old.packPrepared -and $old.backendReady) ('Apply drift fixture starts prepared: '+$drift)
  $enabled=!($old.themes|Where-Object name -eq 'Round').configured
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{expectedRevision=$old.revision;states=@{Round=$enabled}}|ConvertTo-Json -Compress)))
  $script:Applied=0;$script:RenderSamples=0;$script:ApplyNewBackups=0;$script:ApplyDriftInjected=$false
  try{
   $rejected=Reject {Invoke-AppearanceControl 'Apply'}
   Assert ($rejected -and $script:ApplyDriftInjected -and !$script:ApplyNewBackups -and !$script:Applied -and !$script:RenderSamples -and !(Test-Path -LiteralPath $marker)) ('Apply refuses post-admission drift before writes: '+$drift)
   if($drift -in 'config','manifest'){
    Assert ([IO.File]::GetLastWriteTimeUtc($driftPath) -eq $driftTime -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($driftPath))) -ceq ([Convert]::ToBase64String($driftBytes))) ('Apply leaves the concurrent same-timestamp edit intact: '+$drift)
   }
  }finally{
   if($script:ApplyDriftHandle){$script:ApplyDriftHandle.Dispose();$script:ApplyDriftHandle=$null}
   $script:ThemeRoot=$originalApplyRoot
   [IO.File]::WriteAllBytes($driftPath,$originalBytes);[IO.File]::SetLastWriteTimeUtc($driftPath,$driftTime)
  }
 }
 $script:AppearanceContentManifestFile=$indexSource
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'Apply receipt does not start anything or reset the CSS engine'
}}
if($Case -eq 'ScopedApply'){& {
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload;$script:Live=$true
 $script:Rows=@(Get-ThemeRows -RevisionOnly|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $Action='Status';$state=Invoke-AppearanceControl 'Status';$target=$state.snapshot.targetId;$rev=$state.snapshot.revision
 Assert ($state.snapshot.applyScopeProtocol -eq 1) 'Status advertises scoped draft support'
 $Action='ScopedApply';$BackendPath=$script:BackendExe
 function Scoped([string]$Target=$target,[string]$Revision=$rev,$States=@{Round=$false}){
  $body=@{profile='ui-summary-v1';targetId=$Target;expectedRevision=$Revision;states=$States}|ConvertTo-Json -Compress
  $Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($body))
  Invoke-AppearanceControl 'ScopedApply'
 }
 $config=Join-Path $script:ThemeRoot 'Round/config_USER.json';$hash=Get-ConfigHash $config
 $journal=Join-Path (Get-PresetStateDir) 'transaction.json'
 $stale=Scoped -Revision ('e'*64)
 Assert (!$stale.ok -and $stale.applyPreflightChanged -eq $true -and !$stale.snapshot -and !$stale.saved) 'Fresh revision drift returns an explicit no-write receipt'
 Assert (Reject {Scoped -Target ('f'*16)}) 'Wrong directory never grants fallback'
 Assert (Reject {Scoped -States @{Round='false'}}) 'Scoped apply validates literal boolean switches'
 [IO.File]::WriteAllText($journal,'{}');$journalHash=Get-ConfigHash $journal
 Assert ((Reject {Scoped}) -and (Get-ConfigHash $journal) -ceq $journalHash) 'Pending transaction is refused, not recovered or overwritten'
 Remove-Item -LiteralPath $journal -ErrorAction Stop
 $script:Editing=$true
 Assert (Reject {Scoped -Revision ('e'*64)}) 'Open Desktop editor blocks even the preflight retry receipt'
 $script:Editing=$false
 Assert ((Get-ConfigHash $config) -ceq $hash -and !$script:Applied -and !$script:RenderSamples -and !(Test-Path -LiteralPath $journal)) 'Refusal and stale paths do not write any switch or request a page read'
 $rpc=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($Method -ceq 'fetch_theme_path' -and $script:ScopedApplyRace -and ++$script:ScopedPathReads -eq 2){
   if($script:ScopedApplyRace -eq 'target'){return (Join-Path $Out 'other-themes')}
   if($script:ScopedApplyRace -eq 'editor'){$script:Editing=$true}
  }
  & $rpc @PSBoundParameters
 }
 foreach($race in 'target','editor'){
  $script:ScopedPathReads=0;$script:ScopedApplyRace=$race
  try{
   Assert ((Reject {Scoped}) -and $script:ScopedPathReads -eq 2 -and (Get-ConfigHash $config) -ceq $hash -and
     !$script:Applied -and !$script:RenderSamples -and !(Test-Path -LiteralPath $journal)) ('Last prewrite guard refuses late '+$race+' change')
  }finally{$script:ScopedApplyRace='';$script:Editing=$false}
 }
 $script:Applied=0;$script:RenderSamples=0
 $done=Scoped
 Assert ($done.ok -and $done.saved -and $done.renderConfirmed -and $done.snapshot.targetId -ceq $target -and $done.snapshot.steamPid -eq $SessionPid -and $done.snapshot.steamBirth -ceq $SessionBirth) 'Ready scoped Apply returns bound saved and DOM confirmation'
 Assert ($script:Applied -eq 1 -and $script:RenderSamples -eq 1 -and !(Test-Path -LiteralPath $journal) -and
   !($done.snapshot.themes|Where-Object name -eq 'Round').configured -and
   !@($done.snapshot.themes|Where-Object {$_.PSObject.Properties['renderPlan']}).Count) 'Ready Apply makes exactly one switch submission, verifies independently and closes the transaction'
 $script:Unverifiable=$true;$script:Applied=0;$script:RenderSamples=0
 $unconfirmed=Scoped -Revision $done.snapshot.revision -States @{Round=$true}
 Assert (!$unconfirmed.ok -and $unconfirmed.saved -and !$unconfirmed.renderConfirmed -and !$unconfirmed.applyPreflightChanged -and
   ($unconfirmed.snapshot.themes|Where-Object name -eq 'Round').configured -and $script:Applied -eq 1 -and $script:RenderSamples -eq 1 -and !(Test-Path -LiteralPath $journal)) 'Already saved but unconfirmed page is never represented as a no-write retry'
 Assert (!$script:Starts -and !$script:Resets) 'Scoped Apply never starts CSS or resets its engine'
}}
if($Case -eq 'AutomaticPendingStatus'){& {
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload;$script:Live=$true;$script:Rendered=$true
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $script:PendingRowsSource=${function:Get-ThemeRows};$script:PendingMetaSource=${function:Get-PresetMetadata};$script:PendingRpcSource=${function:Invoke-LoaderRpc}
 $script:PendingBackupSource=${function:Read-PresetBackup}
 $script:PendingRoot=$script:ThemeRoot
 function Get-ThemeRows {
  param([switch]$RevisionOnly,[switch]$DigestOnly,[hashtable]$AutomaticPlanMemo,[switch]$ValidateRenderOnly,[switch]$ReadOnlyProjection)
  if($script:Pending){$script:PendingRows++}else{$script:ReadyRows++}
  & $script:PendingRowsSource @PSBoundParameters
 }
 function Get-PresetMetadata($Rows,$BackendThemes=@(),[switch]$OwnedUpdateOnly,$ObservedOwner,[hashtable]$AutomaticPlanMemo,[switch]$DeferObservationBackup){
  if($script:Pending){$script:PendingMeta++}else{$script:ReadyMeta++}
  & $script:PendingMetaSource @PSBoundParameters
 }
 function Read-PresetBackup([string]$Directory){
  if($script:Pending){$script:PendingBackup++}else{$script:ReadyBackup++}
  & $script:PendingBackupSource @PSBoundParameters
 }
 function Get-BackendProcesses {if(!$script:Pending -or $script:PendingRpc){[pscustomobject]@{Id=42;Path=$script:BackendExe}}}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($script:Pending -and $Method -ceq 'get_themes'){throw 'Fixture loader socket not ready'}
  if($script:WrongRoot -and $Method -ceq 'fetch_theme_path'){return (Join-Path $Out 'other/themes')}
  & $script:PendingRpcSource @PSBoundParameters
 }
 function Start-Sleep {param($Milliseconds,$Seconds)
  $script:Delay+=$Milliseconds
  if($script:Delay.Count -ge $script:PendingWaits){$script:Pending=$false}
 }
 function Get-AppearanceSteamSession {
  $script:PendingIdentityReads++
  if($script:PendingRejectAt -and $script:PendingIdentityReads -eq $script:PendingRejectAt){throw 'Fixture shared identity changed'}
  [pscustomobject]@{pid=$script:CurrentPid;birth=$script:CurrentBirth;root='C:\fixture-steam';backendExe=$script:BackendExe}
 }
 function Reset-Pending([int]$Waits=2,[bool]$HasProcess=$false){
  $script:Pending=$true;$script:PendingRpc=$HasProcess;$script:PendingWaits=$Waits
  $script:PendingRows=0;$script:PendingMeta=0;$script:ReadyRows=0;$script:ReadyMeta=0
  $script:PendingBackup=0;$script:ReadyBackup=0
  $script:PendingIdentityReads=0;$script:PendingRejectAt=0;$script:WrongRoot=$false;$script:Delay=@()
  $script:Applied=0;$script:RenderSamples=0;$script:ThemeRoot=$script:PendingRoot
 }
 $metrics=@(foreach($hasProcess in $false,$true){
  Reset-Pending 2 $hasProcess
  $result=Invoke-AutomaticAppearanceRefresh
  Assert ($result.ok -and $result.snapshot.backendReady -and $script:ReadyRows -eq 1 -and $script:ReadyMeta -eq 1 -and $script:RenderSamples -eq 1) 'Ready slot runs the original complete status and one DOM read'
  Assert ($script:Delay.Count -eq 2 -and $script:AppearanceRenderAttempts -eq 3 -and $script:Applied -eq 0 -and $script:PendingIdentityReads -eq 9) 'Two unready slots preserve bounded waits, identity checks and no theme writes'
  [pscustomobject]@{backendPresent=$hasProcess;pendingRows=$script:PendingRows;pendingMetadata=$script:PendingMeta;readyRows=$script:ReadyRows;readyMetadata=$script:ReadyMeta;identityReads=$script:PendingIdentityReads;renderReads=$script:RenderSamples;writes=$script:Applied}
 })
 $metrics|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'pending-cost.json') -Encoding UTF8
 Assert (!($metrics|Where-Object {$_.pendingRows -ne 0 -or $_.pendingMetadata -ne 0})) 'Automatic unready slots do not inspect themes or backups'
 Reset-Pending 99 $true
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh}) -and $script:Delay.Count -eq 4 -and $script:AppearanceRenderAttempts -eq 5 -and $script:AppearanceHelperBackendCount -eq 1 -and $script:PendingRows -eq 0 -and $script:RenderSamples -eq 0 -and $script:Applied -eq 0) 'Unavailable backend terminates on the original five-slot bound with live process count and no theme work'
 Reset-Pending
 $script:PendingRejectAt=3
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh}) -and $script:Delay.Count -eq 0 -and $script:Applied -eq 0) 'The final Steam identity read remains mandatory even for an unready backend'
 Reset-Pending 2 $true
 $offline=(Invoke-AppearanceControl 'Status').snapshot
 Assert (!$offline.backendReady -and $offline.themes.Count -eq 7 -and $offline.canRestore -and $script:PendingRows -eq 1 -and $script:PendingMeta -eq 1) 'Normal Status still reads all seven themes and verifies restore metadata while CSS is offline'
 Reset-Pending 1 $true
 $first=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
 Assert ($first.ok -and $first.snapshot.canRestore -and $script:PendingRows -eq 1 -and $script:PendingBackup -eq 0 -and $script:ReadyBackup -eq 1 -and $script:RenderSamples -eq 1 -and $script:Delay.Count -eq 1) 'Initial prepared unready status retains owned resource checks but defers backup until ready'
 Reset-Pending
 $script:ThemeRoot=Join-Path (Split-Path -Parent $script:PendingRoot) 'unowned'
 $ownedStateBase=$script:PresetStateBase
 try{
  # The preceding ready probe saved an active-target hint for the owned root.
  $script:PresetStateBase=Join-Path $Out 'unowned-states'
  $unowned=Invoke-AppearanceControl 'UpdateOwnedResources' -AutomaticPlanMemo @{}
 }finally{$script:PresetStateBase=$ownedStateBase}
 Assert ($unowned.ok -and !$unowned.saved -and !$unowned.snapshot.backendReady -and !$unowned.snapshot.packOwned -and $script:PendingRows -eq 0 -and $script:PendingBackup -eq 0 -and !$script:RenderSamples) 'Initial unowned unready status cannot take ownership or inspect unused theme rows'
 Reset-Pending 2 $true
 $script:WrongRoot=$true
 Assert ((Reject {Invoke-AppearanceControl 'Status' -CorrectionObservationRoot $script:PendingRoot -AutomaticPlanMemo @{} -AutomaticReadiness}) -and $script:PendingRows -eq 0 -and $script:Applied -eq 0) 'Unready correction observation still rejects a different loaded target before theme work'
 Reset-Pending 1 $true
 $journal=Join-Path (Read-PresetState).originalBackup 'journal.json'
 $journalBytes=[IO.File]::ReadAllBytes($journal)
 try{
  [IO.File]::WriteAllText($journal,'{}')
  $reason='';try{Invoke-AutomaticAppearanceRefresh|Out-Null}catch{$reason=$_.Exception.Message}
  Assert ($reason -like '*Invalid preset backup*' -and $script:Delay.Count -eq 1 -and $script:PendingMeta -eq 0 -and
          $script:ReadyMeta -eq 1 -and $script:Applied -eq 0 -and $script:RenderSamples -eq 0) 'First ready slot rejects a damaged takeover backup before DOM or CSS writes'
 }finally{[IO.File]::WriteAllBytes($journal,$journalBytes)}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:RenderSamples++
  if($script:RenderSamples -eq 1){return [pscustomobject]@{checked=$false;confirmed=$false;reason=$script:PendingDocumentReason;mismatched=@()}}
  [pscustomobject]@{checked=$true;confirmed=$true;reason='render-receipts-confirmed';mismatched=@()}
 }
 foreach($documentReason in 'render-shared-steam-live-busy','render-shared-steam-document-main-unavailable','render-receipt-invalid'){
  Reset-Pending 0 $true;$script:Pending=$false;$script:PendingDocumentReason=$documentReason;$memo=@{}
  $observed=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo
  $nonTransient=$documentReason -ceq 'render-receipt-invalid'
  Assert ($observed.ok -and $observed.snapshot.canRestore -eq $nonTransient -and $script:ReadyBackup -eq [int]$nonTransient -and
          $script:Delay.Count -eq 1 -and $script:RenderSamples -eq 2) "Only known transient document retries defer backup validation: $documentReason"
  $final=Invoke-AutomaticAppearanceIdle $observed.snapshot -AutomaticPlanMemo $memo
  Assert ($final.ok -and $final.resident -and $script:ReadyBackup -eq (1+[int]$nonTransient) -and $script:AppearanceHelperFlags -eq 31 -and
          $script:RenderSamples -eq 3 -and $script:Applied -eq 0) "Final Idle independently validates backup and document: $documentReason"
 }
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'No process startup or global backend reset'
}}
if($Case -in @('All','ControlLock')){
& {
 $script:ContentionCalls=0
 $script:OwnedContentionMutex=New-Object Threading.Mutex($false,('Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.ThemeControl'))
 $entered=New-Object Threading.ManualResetEvent($false)
 $holder=[PowerShell]::Create()
 try {
  $null=$holder.AddScript('param($Mutex,$Entered) $locked=$false;try{$locked=$Mutex.WaitOne(0);if(!$locked){throw "Owned fixture lock failed"};$null=$Entered.Set();Start-Sleep -Milliseconds 3700}finally{if($locked){$Mutex.ReleaseMutex()}}').AddArgument($script:OwnedContentionMutex).AddArgument($entered)
  $pending=$holder.BeginInvoke()
  if(!$entered.WaitOne(5000)){throw 'Owned contention worker did not acquire its mutex'}
  function New-AppearanceMutex($Purpose) {$script:OwnedContentionMutex}
  function Initialize-PresetTargetHint {param($KeepBackendPath)}
  function Invoke-AppearanceIdle {param($ExpectedPid,$ExpectedBirth,$Revision,$Target) $script:ContentionCalls++;[pscustomobject]@{ok=$true}}
  $Payload='';$watch=[Diagnostics.Stopwatch]::StartNew();$answer=Invoke-AppearanceControl 'Idle';$elapsed=$watch.ElapsedMilliseconds
  $null=$holder.EndInvoke($pending)
  Assert ($answer.ok -and $script:ContentionCalls -eq 1 -and $elapsed -ge 3000 -and $elapsed -lt 10000) 'Final confirmation survives a real owned mutex held longer than the old three-second limit and executes exactly once'
 }finally{$holder.Stop();$holder.Dispose();$entered.Dispose();$script:OwnedContentionMutex.Dispose()}
}
& {
 $script:LockWaits=@();$script:LockReleases=0;$script:LockDisposals=0;$script:LockAdmit=$true;$script:IdleCalls=0
 function New-AppearanceMutex($Purpose) {
  $mutex=New-Object PSObject
  $mutex|Add-Member ScriptMethod WaitOne {param($Timeout) $script:LockWaits+=,[int]$Timeout;return $script:LockAdmit}
  $mutex|Add-Member ScriptMethod ReleaseMutex {$script:LockReleases++}
  $mutex|Add-Member ScriptMethod Dispose {$script:LockDisposals++}
  return $mutex
 }
 function Initialize-PresetTargetHint {param($KeepBackendPath)}
 function Invoke-AppearanceIdle {param($ExpectedPid,$ExpectedBirth,$Revision,$Target) $script:IdleCalls++;[pscustomobject]@{ok=$true}}
 function Get-AppearanceStatus {[pscustomobject]@{backendReady=$true}}
 $Payload='';$null=Invoke-AppearanceControl 'Idle';$null=Invoke-AppearanceControl 'Status'
 Assert ($script:LockWaits.Count -eq 2 -and $script:LockWaits[0] -eq 30000 -and $script:LockWaits[1] -eq 3000) 'Final read-only confirmation waits on the existing mutex within 30s while normal operations retain the 3s contention bound'
 Assert ($script:IdleCalls -eq 1 -and $script:LockReleases -eq 2 -and $script:LockDisposals -eq 2) 'Each successful control lock is released and disposed without replaying confirmation'
 $script:LockAdmit=$false;$before=$script:IdleCalls
 Assert (Reject {Invoke-AppearanceControl 'Idle'}) 'An unavailable final confirmation lock still ends with an explicit bounded failure'
 Assert ($script:IdleCalls -eq $before -and $script:LockDisposals -eq 3 -and $script:LockReleases -eq 2 -and $script:AppearanceFailureCode -eq 11) 'Failed lock acquisition disposes its handle and never runs confirmation or releases an unowned mutex'
}
& {
 $script:LockWaits=@();$script:LockDisposals=0;$script:LockReleases=0;$script:LockAdmit=$false;$script:StatusReads=0
 function New-AppearanceMutex($Purpose) {
  $mutex=New-Object PSObject
  $mutex|Add-Member ScriptMethod WaitOne {param($Timeout) $script:LockWaits+=,[int]$Timeout;return $script:LockAdmit}
  $mutex|Add-Member ScriptMethod ReleaseMutex {$script:LockReleases++}
  $mutex|Add-Member ScriptMethod Dispose {$script:LockDisposals++}
  return $mutex
 }
 function Initialize-PresetTargetHint {param($KeepBackendPath) if(!$script:LockAdmit){throw 'Target read before admission'}}
 function Get-AppearanceStatus {param([switch]$OmitRenderPlans) $script:StatusReads++;[pscustomobject]@{summary=[bool]$OmitRenderPlans}}
 function Encode-LockProfile([string]$Text){[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))}
 $Action='Status';$single='{"profile":"ui-summary-v1","lockWait":"single-v1"}';$Payload=Encode-LockProfile $single
 $result=Invoke-AppearanceControl 'Status'
 Assert (!$result.ok -and $result.statusLockTimedOut -eq $true -and !$result.snapshot -and $script:AppearanceFailureCode -eq 11 -and $script:LockWaits[-1] -eq 8000 -and !$script:StatusReads -and $script:LockDisposals -eq 1 -and !$script:LockReleases) 'Negotiated Status timeout emits a terminal receipt and never reads before acquisition'
 $script:LockAdmit=$true;$result=Invoke-AppearanceControl 'Status'
 Assert ($result.ok -and $result.snapshot.summary -and $script:LockWaits[-1] -eq 8000 -and $script:StatusReads -eq 1 -and $script:LockDisposals -eq 2 -and $script:LockReleases -eq 1) 'Negotiated Status reads exactly once after acquisition and keeps the summary profile'
 $script:LockAdmit=$false
 foreach($profile in @('{"profile":"ui-summary-v1"}',('{"profile":"ui-summary-v1","lockWait":["single-v1"]}'),('['+$single+']'),('{"profile":"other","lockWait":"single-v1"}'),('{"profile":"ui-summary-v1","lockWait":"SINGLE-v1"}'),(' '*260+$single))){
  $Payload=Encode-LockProfile $profile
  Assert ((Reject {Invoke-AppearanceControl 'Status'}) -and $script:LockWaits[-1] -eq 3000) ('Unnegotiated Status keeps standard wait: '+$profile)
 }
 $Payload=Encode-LockProfile $single;$Action='Automatic'
 Assert ((Reject {Invoke-AppearanceControl 'Status'}) -and $script:LockWaits[-1] -eq 3000) 'Native automatic Status never uses the card lock wait'
 $Action='Status'
 Assert ((Reject {Invoke-AppearanceControl 'Prepare'}) -and $script:LockWaits[-1] -eq 3000) 'Mutation lock wait is never extended by a Status payload'
}
}
if($Case -eq 'InitialOwnedStatus'){& {
 $dispatch=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses.Count -eq 1 -and $n.Clauses[0].Item1.Extent.Text -ceq '!$VerifyOnly'},$true))
 if($dispatch.Count -ne 1){throw 'EXACT_AUTOMATIC_INITIALIZATION_DISPATCH_REQUIRED'}
 $dispatchText=$dispatch[0].Extent.Text
 $GateTimeoutMs=-1
 $ready=New-Object PSObject
 $ready|Add-Member ScriptMethod Set {$script:InitialSignals++;return $true}
 $gate=New-Object PSObject
 $gate|Add-Member ScriptMethod WaitOne {param($ms) $script:InitialGateWaits++;return $script:InitialGateAdmit}
 $gate|Add-Member ScriptMethod Reset {$script:InitialGateResets++;return $script:InitialGateReset}
 function Write-AppearanceHelperResult {param($Success) if(!$Success){throw 'Unexpected intermediate failure'};$script:InitialReceipts++}
 $script:InitialSnapshot=[pscustomobject]@{backendReady=$true;configuredMatchesBackend=$true;backendProcessCount=2;packPrepared=$true;packRepairRequired=$false;foreignActiveCount=0;backendReloadRequired=$false;packError='';revision=('a'*64);targetId=('b'*16)}
 function Reset-InitialStatus {
  $script:InitialCalls=[Collections.Generic.List[string]]::new();$script:RenderSamples=0;$script:Delay=@();$script:NoReadyFirst=$false;$script:RejectInitialization=$false;$script:MissingStyles=@();$script:CurrentBirth='456';$script:CurrentPid=123
  $StartBackend=$false;$VerifyOnly=$false
  $script:InitialSignals=0;$script:InitialGateWaits=0;$script:InitialGateResets=0;$script:InitialReceipts=0;$script:InitialGateAdmit=$true;$script:InitialGateReset=$true;$script:AppearanceFailureCode=0
 }
 function Invoke-AppearanceControl([string]$ControlAction){
  $script:InitialCalls.Add($ControlAction)
  if($ControlAction -notin @('Ensure','UpdateOwnedResources','Status')){throw ('UNEXPECTED_INITIAL_CONTROL:'+ $ControlAction)}
  if($ControlAction -eq 'UpdateOwnedResources' -and $script:RejectInitialization){return [pscustomobject]@{ok=$false}}
  $snapshot=$script:InitialSnapshot
  if($script:NoReadyFirst -and $script:InitialCalls.Count -eq 1){
   $snapshot=$script:InitialSnapshot.PSObject.Copy();$snapshot.backendReady=$false
  }
  [pscustomobject]@{ok=$true;snapshot=$snapshot}
 }
 Reset-InitialStatus;$StartBackend=$false;$VerifyOnly=$false
 Invoke-Expression $dispatchText
 Assert (($script:InitialCalls -join ',') -ceq 'UpdateOwnedResources' -and $script:RenderSamples -eq 1) 'Actual automatic dispatch probes the initial resource status once without an immediate duplicate Status'
 Assert ($script:Delay.Count -eq 0 -and $script:Starts -eq 0 -and $script:Applied -eq 0 -and $script:Resets -eq 0) 'Healthy first observation has no sleep, backend launch, theme write or reset'
 Reset-InitialStatus
 $value=Invoke-AutomaticAppearanceRefresh
 Assert ($value.ok -and ($script:InitialCalls -join ',') -ceq 'Status') 'Existing direct refresh callers retain their original read-only Status path'
 Reset-InitialStatus;$script:NoReadyFirst=$true;$StartBackend=$false;$VerifyOnly=$false
 Invoke-Expression $dispatchText
 Assert (($script:InitialCalls -join ',') -ceq 'UpdateOwnedResources,Status' -and $script:Delay.Count -eq 1 -and $script:RenderSamples -eq 1) 'A later readiness offset performs a fresh Status and never repeats resource initialization'
 Reset-InitialStatus;$script:RejectInitialization=$true;$StartBackend=$false;$VerifyOnly=$false
 Assert (Reject {Invoke-Expression $dispatchText}) 'Failed initial resource update stops before DOM instead of skipping initialization'
 Assert ($script:AppearanceFailureCode -eq 14 -and $script:RenderSamples -eq 0 -and $script:InitialCalls.Count -eq 1) 'Initialization failure keeps its original typed failure code and one attempt'
 Reset-InitialStatus;$StartBackend=$true;$VerifyOnly=$false
 Invoke-Expression $dispatchText
 Assert (($script:InitialCalls -join ',') -ceq 'Ensure,Status' -and $script:RenderSamples -eq 1) 'Explicit missing-backend initialization still Ensures once then reads fresh status'
 Assert ($script:InitialSignals -eq 1 -and $script:InitialGateWaits -eq 1 -and $script:InitialGateResets -eq 1 -and $script:InitialReceipts -eq 1) 'Missing-backend automatic dispatch parks for exactly one native admission then resets the original idle event'
 Reset-InitialStatus;$StartBackend=$true;$VerifyOnly=$false;$script:InitialGateAdmit=$false
 Assert (Reject {Invoke-Expression $dispatchText}) 'A denied initialization gate stops before status or DOM work'
 Assert (($script:InitialCalls -join ',') -ceq 'Ensure' -and $script:RenderSamples -eq 0 -and $script:AppearanceFailureCode -eq 13 -and $script:InitialGateResets -eq 0) 'Initialization gate timeout keeps its typed failure and never resets or advances'
 Reset-InitialStatus;$StartBackend=$true;$VerifyOnly=$false;$script:InitialGateReset=$false
 Assert (Reject {Invoke-Expression $dispatchText}) 'An initialization gate reset failure cannot advance to DOM work'
 Assert (($script:InitialCalls -join ',') -ceq 'Ensure' -and $script:RenderSamples -eq 0) 'Gate reset failure has no status replay or page acknowledgement'

 Reset-InitialStatus;$StartBackend=$false;$VerifyOnly=$true
 Invoke-Expression $dispatchText
 Assert ($script:InitialCalls.Count -eq 0 -and $script:RenderSamples -eq 0) 'Manual VerifyOnly dispatch continues to skip automatic initialization and correction'
 Reset-InitialStatus;$script:CurrentBirth='changed';$StartBackend=$false;$VerifyOnly=$false
 Assert (Reject {Invoke-Expression $dispatchText}) 'A replaced Steam session is rejected before any initial resource operation'
 Assert ($script:InitialCalls.Count -eq 0 -and $script:RenderSamples -eq 0) 'Stale Steam identity grants neither status initialization nor a page acknowledgement'
}}

if($Case -eq 'FinalRevision'){& {
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $script:Rendered=$true;$script:MissingStyles=@()
 $script:RevisionPlanOriginal=(Get-Item Function:\Get-AppearanceTargetRenderPlan).ScriptBlock
 $script:RevisionRowsOriginal=(Get-Item Function:\Get-ThemeRows).ScriptBlock
 $script:RevisionReadOriginal=(Get-Item Function:\Read-AppearanceThemeConfig).ScriptBlock
 $script:RevisionHashOriginal=(Get-Item Function:\Get-ConfigHash).ScriptBlock
 $script:RevisionPlans=0;$script:RevisionFullReads=0;$script:RevisionShortReads=0
 function Get-AppearanceTargetRenderPlan($Name,$Manifest,$Config,$ContentIndex,$Target,$FileReceipts,$Defaults,[switch]$ValidateOnly){
  $script:RevisionPlans++;&$script:RevisionPlanOriginal @PSBoundParameters
 }
 function Get-ThemeRows {
  param([switch]$RevisionOnly,[switch]$DigestOnly)
  if($RevisionOnly){$script:RevisionShortReads++}else{$script:RevisionFullReads++}
  &$script:RevisionRowsOriginal @PSBoundParameters
 }
 function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
  if($script:RevisionAfterDom -and $Path.StartsWith($script:ThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:RevisionFinalDecodes++}
  &$script:RevisionReadOriginal @PSBoundParameters
 }
 function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0,$ObservedLength=$null){
  if($script:RevisionAfterDom -and $Path.StartsWith($script:ThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){
   if($MaximumBytes -ne 262144){throw 'Unbounded final theme snapshot'}
   $script:RevisionFinalDigests++
  }
  &$script:RevisionHashOriginal @PSBoundParameters
 }
 $full=@(Get-ThemeRows);$fullRevision=Get-ThemeRevision $full
 $script:RevisionPlans=0;$short=@(Get-ThemeRows -RevisionOnly)
 Assert ($script:RevisionPlans -eq 0) 'Final revision read does not rebuild any discarded render plan'
 Assert ($short.Count -eq 7 -and (Get-ThemeRevision $short) -ceq $fullRevision -and @($short|Where-Object {$null -ne $_.renderPlan}).Count -eq 0) 'Revision-only rows keep the exact seven-theme fresh revision without usable render plans'
 $fullShape=@($full|Select-Object name,installed,configured,hash,manifestHash,parameterCount,error,path,raw,config)|ConvertTo-Json -Depth 16 -Compress
 $shortShape=@($short|Select-Object name,installed,configured,hash,manifestHash,parameterCount,error,path,raw,config)|ConvertTo-Json -Depth 16 -Compress
 Assert ($fullShape -ceq $shortShape) 'Revision-only mode preserves every non-plan row field and exact config bytes'
 $again=@(Get-ThemeRows)
 Assert ($script:RevisionPlans -eq 7 -and (Test-FixtureThemePagePlans $again)) 'Default status builds 21 independent page plans in seven traversals after a revision-only read'

 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 $script:RevisionFault='';$script:RevisionHeld=$null
 $script:RevisionConfig=Join-Path $script:ThemeRoot 'Round\config_USER.json'
 $script:RevisionConfigBytes=[IO.File]::ReadAllBytes($script:RevisionConfig)
 $script:RevisionManifest=Join-Path $script:ThemeRoot 'Round\theme.json'
 $script:RevisionManifestBytes=[IO.File]::ReadAllBytes($script:RevisionManifest)
 $script:RevisionIndex=Join-Path $Out 'owned-content-index.json'
 $script:RevisionIndexBytes=[IO.File]::ReadAllBytes($script:AppearanceContentManifestFile)
 $indexText=[Text.Encoding]::UTF8.GetString($script:RevisionIndexBytes)
 $marker=[regex]::Match($indexText,'--ymcc-content-[0-9a-f]{16}').Value
 $newMarker='--ymcc-content-0000000000000000'
 if(!$marker -or $indexText.Contains($newMarker)){throw 'Distinct fixture content marker required'}
 $script:ChangedRevisionIndexBytes=[Text.Encoding]::UTF8.GetBytes($indexText.Replace($marker,$newMarker))
 if($script:ChangedRevisionIndexBytes.Length -ne $script:RevisionIndexBytes.Length){throw 'Fixture index length must not change'}
 [IO.File]::WriteAllBytes($script:RevisionIndex,$script:RevisionIndexBytes)
 $script:AppearanceContentManifestFile=$script:RevisionIndex
 function Replace-ContentIndexAtSameStamp([byte[]]$Bytes){
  $file=[IO.FileInfo]::new($script:RevisionIndex);$length=$file.Length;$created=$file.CreationTimeUtc;$written=$file.LastWriteTimeUtc
  [IO.File]::WriteAllBytes($file.FullName,$Bytes)
  [IO.File]::SetCreationTimeUtc($file.FullName,$created);[IO.File]::SetLastWriteTimeUtc($file.FullName,$written)
  $file.Refresh()
  if($file.Length -ne $length -or $file.CreationTimeUtc -ne $created -or $file.LastWriteTimeUtc -ne $written){throw 'Fixture index metadata changed'}
 }
 function Test-AppearanceRendered($Session,$Status){
  if(!$script:RevisionFault -and !(Test-FixtureThemePagePlans $Status.themes)){throw 'Final DOM check needs all 21 independent page plans'}
  switch($script:RevisionFault){
   config {
    $value=[IO.File]::ReadAllText($script:RevisionConfig)|ConvertFrom-Json
    $value.active=$false;[IO.File]::WriteAllText($script:RevisionConfig,($value|ConvertTo-Json -Depth 8 -Compress))
   }
   locked {$script:RevisionHeld=[IO.File]::Open($script:RevisionConfig,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}
   missing {[IO.File]::Delete($script:RevisionConfig)}
   oversized {[IO.File]::WriteAllBytes($script:RevisionConfig,[byte[]]::new(262145))}
   manifest {
    $value=[IO.File]::ReadAllText($script:RevisionManifest)|ConvertFrom-Json
    $value|Add-Member -NotePropertyName fixtureEdition -NotePropertyValue 'changed'
    [IO.File]::WriteAllText($script:RevisionManifest,($value|ConvertTo-Json -Depth 20 -Compress))
   }
   invalidManifest {[IO.File]::WriteAllText($script:RevisionManifest,'{broken-theme')}
   session {$script:CurrentBirth='457'}
   index {[IO.File]::WriteAllText($script:RevisionIndex,'{broken-index')}
   indexValid {Replace-ContentIndexAtSameStamp $script:ChangedRevisionIndexBytes}
   indexObservedThenReverted {
    Replace-ContentIndexAtSameStamp $script:ChangedRevisionIndexBytes
    $null=Get-AppearanceContentIndex
    Replace-ContentIndexAtSameStamp $script:RevisionIndexBytes
   }
   editor {$script:Editing=$true}
  }
  $script:RevisionAfterDom=$true
  return $true
 }
 function Reset-FinalRevision {
  if($script:RevisionHeld){$script:RevisionHeld.Dispose();$script:RevisionHeld=$null}
  [IO.File]::WriteAllBytes($script:RevisionConfig,$script:RevisionConfigBytes)
  [IO.File]::WriteAllBytes($script:RevisionManifest,$script:RevisionManifestBytes)
  [IO.File]::WriteAllBytes($script:RevisionIndex,$script:RevisionIndexBytes)
  $script:CurrentBirth='456';$script:Editing=$false;$script:RevisionFault=''
  $script:AppearanceHelperPhase=0;$script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0
  $script:RevisionPlans=0;$script:RevisionFullReads=0;$script:RevisionShortReads=0
  $script:RevisionAfterDom=$false;$script:RevisionFinalDecodes=0;$script:RevisionFinalDigests=0
 }
 try {
  Reset-FinalRevision
  $result=Invoke-AppearanceControl 'Idle'
  Assert ($result.ok -and $result.resident -and $script:AppearanceHelperFlags -eq 31) 'Actual final Idle control still confirms the resident backend with all original flags'
  Assert ($script:RevisionFullReads -eq 1 -and $script:RevisionShortReads -eq 1 -and $script:RevisionPlans -eq 7) 'Actual final Idle traverses each theme once for status and performs one separate fresh revision-only read after DOM'
  Assert ($script:RevisionFinalDecodes -eq 0 -and $script:RevisionFinalDigests -eq 14) 'Prepared final confirmation read all fourteen bounded digests with zero repeated JSON decoding'
  foreach($fault in @('config','locked','missing','oversized','session','manifest','invalidManifest')){
   Reset-FinalRevision;$script:RevisionFault=$fault
   $rejected=Reject {Invoke-AppearanceControl 'Idle'}
   Assert ($rejected -and $script:AppearanceFailureCode -eq 9 -and !($script:AppearanceHelperFlags -band 16)) ('Final fresh revision/session still rejects a change after DOM: '+$fault)
  }
  Reset-FinalRevision;$script:RevisionFault='index'
  Assert (Reject {Invoke-AppearanceControl 'Idle'}) 'Revision-only read still validates the current bundled content index after DOM'
  Assert (!($script:AppearanceHelperFlags -band 16)) 'A damaged content index cannot publish the final fresh-confirmation flag'
  foreach($fault in @('indexValid','indexObservedThenReverted')){
   Reset-FinalRevision;$script:RevisionFault=$fault
   $rejected=Reject {Invoke-AppearanceControl 'Idle'}
   Assert ($rejected -and $script:AppearanceFailureCode -eq 9 -and !($script:AppearanceHelperFlags -band 16)) ('Final confirmation rejects a valid changed content index: '+$fault)
  }
  Reset-FinalRevision;$script:RevisionFault='editor'
  Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:AppearanceFailureCode -eq 15) 'Desktop taking edit ownership after DOM still stops final confirmation'
  Assert ($script:Starts -eq 0 -and $script:Applied -eq 0 -and $script:Resets -eq 0) 'Every final-revision scenario performs zero Steam/CSS launches, backend resets or theme submissions'
 }finally{Reset-FinalRevision}
}}

if($Case -eq 'StatusIdentity'){& {
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $SessionBirth='133334445556667778'
 $idleAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance/idle.ps1'),[ref]$null,[ref]$null)
 $sessionReader=$idleAst.EndBlock.Statements|Where-Object {$_.Name -eq 'Get-AppearanceSteamSession'}
 if(!$sessionReader){throw 'Production identity reader unavailable'}
 Invoke-Expression $sessionReader.Extent.Text
 function Reset-StatusIdentity([string]$Fault=''){
  $script:AppearanceSharedIdentityKey='';$script:StatusFault=$Fault;$script:StatusIdentityCalls=0
  $script:StatusEvents=New-Object 'Collections.Generic.List[string]'
 }
 function Invoke-AppearanceSharedSteam($Request){
  if($Request.operation -cne 'identity'){throw 'Unexpected Steam request'}
  $script:StatusEvents.Add('identity');$script:StatusIdentityCalls++
  if($script:StatusFault -ceq 'denied'){throw 'Expired shared identity'}
  $changed=$script:StatusIdentityCalls -gt 1
  [pscustomobject]@{protocol=1;presence=[int]($script:StatusFault -cne 'absent');pid=123;birth='133334445556667778';root='C:\fixture-steam';
   processEpoch='12';accountEpoch=$(if($script:StatusFault -ceq 'already-retired' -or ($changed -and $script:StatusFault -ceq 'account')){'21'}else{'20'});
   powerGeneration=$(if($changed -and $script:StatusFault -ceq 'power'){'32'}else{'31'})}
 }
 function Initialize-PresetTargetHint {param($KeepBackendPath) $script:StatusEvents.Add('hint')}
 function Get-BackendProcesses {$script:StatusEvents.Add('backend');[pscustomobject]@{Id=42;Path=$script:BackendExe}}
 function Invoke-OwnedPresetResourceUpdate($Current){$script:StatusEvents.Add('resources');$Current}
 foreach($action in 'Status','UpdateOwnedResources'){
  Reset-StatusIdentity
  $value=Invoke-AppearanceControl $action
  $expected='hint,identity,backend,identity';if($action -ceq 'UpdateOwnedResources'){$expected+=',resources'}
  Assert ($value.ok -and $value.snapshot.backendReady -and $value.snapshot.configuredMatchesBackend -and $value.snapshot.steamRunning -and
          ($script:StatusEvents -join ',') -ceq $expected) ('Status entry must remove one redundant public identity request: '+$action+' observed='+($script:StatusEvents -join ','))
 }
 $statusReceipt=$value.snapshot|ConvertTo-Json -Depth 10 -Compress|ConvertFrom-Json
 Assert ($statusReceipt.steamPid -eq 123 -and $statusReceipt.steamBirth -is [string] -and $statusReceipt.steamBirth -ceq $SessionBirth) 'Status reuses the existing shared PID and exact decimal birth in its JSON receipt'
 foreach($mode in 'account','power'){
  Reset-StatusIdentity $mode
  Assert ((Reject {Invoke-AppearanceControl 'UpdateOwnedResources'}) -and ($script:StatusEvents -join ',') -ceq 'hint,identity,backend,identity') ('Final status must reject '+$mode+' change before resource update')
 }
 Reset-StatusIdentity 'denied'
 Assert ((Reject {Invoke-AppearanceControl 'UpdateOwnedResources'}) -and ($script:StatusEvents -join ',') -ceq 'hint,identity') 'Expired identity stops before backend or resource work'
 Reset-StatusIdentity 'denied'
 Assert ((Reject {Invoke-AppearanceControl 'Ensure'}) -and ($script:StatusEvents -join ',') -ceq 'identity') 'Other mutation paths retain their original outer admission'
 $script:Rendered=$true
 Reset-StatusIdentity
 $approved=(Invoke-AppearanceControl 'Status').snapshot
 # Retain the already leased identity across the same automatic request.
 $lease=$script:AppearanceSharedIdentityKey
 $reconcileTraces=@(foreach($mode in 'automatic','deferred','manual-denied','already-retired','account','power'){
  Reset-StatusIdentity $mode
  $script:AppearanceSharedIdentityKey=$lease
  $value=$null;$reason=''
  try{
   if($mode -ceq 'manual-denied'){
    $script:StatusFault='denied'
    $value=Invoke-AppearanceControl 'Reconcile'
   }else{$value=Invoke-AppearanceControl 'Reconcile' -AutomaticCorrectionStatus $approved -DeferredCorrection:($mode -ceq 'deferred') -CorrectionObservationRoot $script:ThemeRoot}
  }catch{$reason=$_.Exception.Message}
  [pscustomobject]@{mode=$mode;ok=[bool]$value.ok;confirmed=[bool]$value.renderConfirmed;reason=$reason;events=($script:StatusEvents -join ',')}
 })
 $reconcileTraces|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Out 'reconcile-identity-traces.json') -Encoding UTF8
 foreach($trace in $reconcileTraces){
  $expected=switch -CaseSensitive($trace.mode){
   'automatic' {'identity,backend,identity,resources,identity,identity'}
   'deferred' {'identity,backend,identity,identity,identity'}
   'manual-denied' {'identity'}
   'already-retired' {'identity'}
   default {'identity,backend,identity'}
  }
  $needsSuccess=$trace.mode -in 'automatic','deferred'
  Assert ($trace.ok -eq $needsSuccess -and $trace.confirmed -eq $needsSuccess -and $trace.events -ceq $expected -and
          ($needsSuccess -or $trace.reason -like '*identity*')) ('Reconcile identity boundary: '+($trace|ConvertTo-Json -Compress))
 }
 Reset-StatusIdentity 'absent'
 $SessionPid=0;$SessionBirth=''
 $offline=(Invoke-AppearanceControl 'Status').snapshot
 Assert ($offline.steamRunning -eq $false -and $script:StatusIdentityCalls -eq 2) 'Offline Status reports absent, without using a cached Steam identity'
 Assert (!$offline.PSObject.Properties['steamPid'] -and !$offline.PSObject.Properties['steamBirth']) 'Absent Status carries no stale Steam session identity'
 Reset-StatusIdentity
 $script:AppearanceObservationOnly=$true
 try{Assert ((Invoke-AppearanceControl 'Status').ok -and ($script:StatusEvents -join ',') -ceq 'hint,identity,backend,identity') 'Read-only Status still performs both public identity boundaries'}
 finally{$script:AppearanceObservationOnly=$false}
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0 -and $script:Applied -eq 0) 'Status identity checks do not start a process or write to the backend'
}}

if($Case -eq 'AutomaticIdentity'){& {
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $idleAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance/idle.ps1'),[ref]$null,[ref]$null)
 $sessionReader=$idleAst.EndBlock.Statements|Where-Object {$_.Name -eq 'Get-AppearanceSteamSession'}
 Invoke-Expression $sessionReader.Extent.Text
 function Reset-AutomaticIdentity([string]$Fault=''){
  $script:AppearanceSharedIdentityKey='';$script:IdentityFault=$Fault;$script:IdentityEvents=New-Object 'Collections.Generic.List[string]';$script:IdentitySeen=0
 }
 function Invoke-AppearanceSharedSteam($Request){
  if($Request.operation -cne 'identity'){throw 'Unexpected Steam request'}
  $script:IdentityEvents.Add('identity');$script:IdentitySeen++
  if($script:IdentityFault -ceq 'denied'){throw 'Fixture identity denied'}
  $changedPower=($script:IdentityFault -ceq 'during-wait' -and $script:IdentityEvents.Contains('wait')) -or ($script:IdentityFault -ceq 'after-dom' -and $script:IdentityEvents.Contains('dom'))
  $afterBackend=$script:IdentityEvents.Contains('backend')
  [pscustomobject]@{protocol=1;presence=$(switch($script:IdentityFault){'missing'{0};'unknown'{-1};default{1}});pid=123;birth='456';
   root=$(if($script:IdentityFault -ceq 'root-after-backend' -and $afterBackend){'C:\replacement-steam'}else{'C:\fixture-steam'});processEpoch='12';
   accountEpoch=$(if(($script:IdentityFault -ceq 'before-status' -and $script:IdentitySeen -gt 1) -or ($script:IdentityFault -ceq 'after-backend' -and $afterBackend)){'21'}else{'20'});
   powerGeneration=$(if($changedPower){'32'}else{'31'})}
 }
 function Initialize-PresetTargetHint {param($KeepBackendPath) $script:IdentityEvents.Add('hint')}
 function Get-BackendProcesses {$script:IdentityEvents.Add('backend');[pscustomobject]@{Id=42;Path=$script:BackendExe}}
 function Test-CssDesktopEditing {$script:IdentityEvents.Add('editor');$script:IdentityFault -ceq 'editor'}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Start-Sleep {param($Milliseconds,$Seconds) $script:IdentityEvents.Add('wait')}
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:IdentityEvents.Add('dom')
  $valid=$Session.pid -eq 123 -and $Session.birth -ceq '456' -and $Session.accountEpoch -ceq '20' -and $Status.configuredMatchesBackend -and $script:IdentityFault -cne 'during-wait'
  [pscustomobject]@{checked=$valid;confirmed=$valid;reason='fixture-receipts';mismatched=@()}
 }
 Reset-AutomaticIdentity
 $memo=@{}
 $first=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo
 $firstTrace=$script:IdentityEvents -join ','
 $script:IdentityEvents.Clear()
 $final=Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo
 $finalTrace=$script:IdentityEvents -join ','
 Write-Output ('First='+$firstTrace+'; Final='+$finalTrace)
 Assert ($first.ok -and $firstTrace -ceq 'identity,hint,identity,backend,identity,dom') 'First automatic pass needs admission, both status checks and a DOM read, not a second adjacent admission'
 Assert ($final.ok -and $final.resident -and $script:AppearanceHelperFlags -eq 31 -and $finalTrace -ceq 'hint,editor,identity,backend,identity,dom,backend,editor,identity') 'Final automatic Idle needs one identity before backend, one before DOM and one after DOM'
 $sharedPlans=@($final.snapshot.themes|Where-Object {$_.renderPlan -and [object]::ReferenceEquals($_.renderPlan,$memo[$_.name].plan)})
 $planKeys=@($memo.Keys|Where-Object {$_ -cne ':bundle-schema'})
 Assert ($planKeys.Count -eq 7 -and $sharedPlans.Count -eq 7) 'Scoped final reuses all seven unchanged plans without skipping fresh identity, backend or DOM checks'
 Reset-AutomaticIdentity
 $normal=Invoke-AppearanceControl 'Idle'
 $legacyIdleTrace='identity,hint,editor,identity,identity,backend,identity,dom,backend,editor,identity'
 Assert ($normal.ok -and ($script:IdentityEvents -join ',') -ceq $legacyIdleTrace) 'Manual Idle retains its original trace'
 Reset-AutomaticIdentity
 $script:AppearanceObservationOnly=$true
 try{
  $verified=Invoke-AppearanceControl 'Idle'
  $verifyTrace='identity,hint,editor,identity,backend,identity,dom,backend,editor,identity'
  Assert ($verified.ok -and $verified.resident -and $script:AppearanceHelperFlags -eq 31 -and ($script:IdentityEvents -join ',') -ceq $verifyTrace) 'VerifyOnly retains pre-hint admission and both DOM boundaries without two consecutive identity reads'
  Reset-AutomaticIdentity 'root-after-backend'
  $script:AppearanceHelperFlags=0
  Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and ($script:IdentityEvents -join ',') -ceq 'identity,hint,editor,identity,backend,identity' -and $script:AppearanceHelperFlags -eq 0) 'VerifyOnly rejects a changed Steam root before reading the DOM'
  Reset-AutomaticIdentity 'after-dom'
  Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and ($script:IdentityEvents -join ',') -ceq $verifyTrace -and $script:AppearanceHelperFlags -ne 31) 'VerifyOnly still rejects a power scope change after reading the DOM'
 }finally{$script:AppearanceObservationOnly=$false}
 Reset-AutomaticIdentity 'denied'
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh}) -and ($script:IdentityEvents -join ',') -ceq 'identity') 'Initial automatic admission failure stops before any other operation'
 Reset-AutomaticIdentity 'before-status'
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and ($script:IdentityEvents -join ',') -ceq 'identity,hint,identity') 'A changed account at first status is rejected before backend or DOM'
 Reset-AutomaticIdentity 'during-wait'
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh}) -and ($script:IdentityEvents -join ',') -ceq 'identity,hint,identity,backend,identity,dom,wait,identity') 'Later slots reread identity immediately after waiting and reject a changed power scope'
 Reset-AutomaticIdentity 'denied'
 Assert ((Reject {Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo}) -and ($script:IdentityEvents -join ',') -ceq 'hint,editor,identity') 'Final admission failure stops before backend or DOM even with cached plans'
 foreach($fault in 'missing','unknown'){
  Reset-AutomaticIdentity $fault
  Assert ((Reject {Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo}) -and ($script:IdentityEvents -join ',') -ceq 'hint,editor,identity') "Final $fault identity blocks backend and DOM reads"
 }
 foreach($fault in 'after-backend','root-after-backend'){
  Reset-AutomaticIdentity $fault
  Assert ((Reject {Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo}) -and ($script:IdentityEvents -join ',') -ceq 'hint,editor,identity,backend,identity') "Final $fault change blocks the DOM read"
 }
 Reset-AutomaticIdentity 'after-dom'
 Assert ((Reject {Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo}) -and ($script:IdentityEvents -join ',') -ceq $finalTrace -and $script:AppearanceHelperFlags -ne 31) 'Scope changing during DOM cannot be reported as successful'
 Reset-AutomaticIdentity 'editor'
 Assert ((Reject {Invoke-AutomaticAppearanceIdle $first.snapshot -AutomaticPlanMemo $memo}) -and ($script:IdentityEvents -join ',') -ceq 'hint,editor') 'Desktop editor still blocks final backend work'
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0 -and $script:Applied -eq 0) 'Identity reduction neither starts nor resets CSS and performs no theme RPC write'
}}

if($Case -eq 'FinalIdleDom'){& {
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-AutomaticAppearanceIdle'},$true)
 if($fn){Invoke-Expression $fn.Extent.Text}
 function Invoke-FixtureAutomaticIdle($Scope){if($fn){Invoke-AutomaticAppearanceIdle $Scope}else{Invoke-AppearanceControl 'Idle'}}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 $missing=[pscustomobject]@{checked=$true;confirmed=$false;reason='render-theme-receipts-mismatch';mismatched=@('Mini Carousel')}
 $busy=[pscustomobject]@{checked=$false;confirmed=$false;reason='render-shared-steam-live-busy';mismatched=@()}
 $healthy=[pscustomobject]@{checked=$true;confirmed=$true;reason='render-receipts-confirmed';mismatched=@()}
 $unverifiable=[pscustomobject]@{checked=$false;confirmed=$false;reason='render-receipt-invalid';mismatched=@()}
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:IdleReadOnly+=@([bool]$script:AppearanceObservationOnly)
  $script:IdleDocuments.Dequeue()
 }
 function Start-Sleep {param($Milliseconds,$Seconds)
  $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds}
  switch($script:IdleFault){
   editor {$script:Editing=$true}
   session {$script:CurrentBirth='457'}
   revision {$row=[IO.File]::ReadAllText($config)|ConvertFrom-Json;$row.Intensity='25px';[IO.File]::WriteAllText($config,($row|ConvertTo-Json -Compress))}
   transaction {[IO.File]::WriteAllText($marker,'fixture unfinished transaction')}
  }
 }
 function Set-IdleDocuments([object[]]$Documents,[string]$Fault=''){
  $script:IdleDocuments=New-Object 'Collections.Generic.Queue[object]'
  foreach($document in $Documents){$script:IdleDocuments.Enqueue($document)}
  $script:Delay=@();$script:IdleReadOnly=@();$script:IdleFault=$Fault
  $script:AppearanceHelperPhase=0;$script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0
 }
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$configBytes=[IO.File]::ReadAllBytes($config)
 $marker=Join-Path (Get-PresetStateDir) 'transaction.json'
 $scope=Get-AppearanceStatus
 # Represent a separate interrupted writer with a real rollback journal.
 Set-IdleDocuments @($healthy)
 $owner=Join-Path (Get-PresetStateDir) 'owner.json';$ownerHash=Get-ConfigHash $owner
 $configHash=Get-ConfigHash $config
 try {
  [IO.File]::AppendAllText($config,"`n")
  $undo=New-PresetBackup @($config)
  Begin-PresetTransaction $undo
  $script:PresetTransactionActive=$false
  [IO.File]::WriteAllBytes($config,$configBytes)
  $markerHash=Get-ConfigHash $marker
  $rejected=Reject {Invoke-FixtureAutomaticIdle $scope}
  $markerKept=[IO.File]::Exists($marker);$configKept=(Get-ConfigHash $config) -ceq $configHash
  Assert ($rejected -and $script:AppearanceFailureCode -eq 3 -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1 -and
   $markerKept -and $configKept -and (Get-ConfigHash $marker) -ceq $markerHash -and (Get-ConfigHash $owner) -ceq $ownerHash -and !$script:AppearanceObservationOnly) ('First final DOM observes a settled transaction; it never runs rollback (markerKept='+$markerKept+'; configKept='+$configKept+')')
 }finally{
  $script:PresetTransactionActive=$false
  [IO.File]::WriteAllBytes($config,$configBytes)
  if([IO.File]::Exists($marker)){[IO.File]::Delete($marker)}
 }
 $hint=Get-PresetTargetHintPath
 $hintBytes=[IO.File]::ReadAllBytes($hint)
 try {
  [IO.File]::Delete($hint)
  Set-IdleDocuments @($healthy)
  $value=Invoke-FixtureAutomaticIdle $scope
  Assert ($value.ok -and ($script:IdleReadOnly -join ',') -ceq 'True' -and ![IO.File]::Exists($hint) -and !$script:AppearanceObservationOnly) 'Healthy final observation does not regenerate the mutable target hint'
 }finally{[IO.File]::WriteAllBytes($hint,$hintBytes)}
 # Observe the actual final-idle control path, not just the marker helper.
 $markerProviderReads=& {
  $script:FinalMarkerProviderReads=0
  function Test-Path {
   [CmdletBinding(DefaultParameterSetName='Path')]
   param([Parameter(ParameterSetName='Path',Position=0)][string[]]$Path,
         [Parameter(ParameterSetName='LiteralPath')][string[]]$LiteralPath,
         [Microsoft.PowerShell.Commands.TestPathType]$PathType)
   if($LiteralPath -contains $marker){$script:FinalMarkerProviderReads++}
   Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
  }
  Set-IdleDocuments @($healthy)
  $value=Invoke-FixtureAutomaticIdle $scope
  if(!$value.ok -or $script:IdleDocuments.Count -ne 0){throw 'Expected final observation to confirm a missing transaction marker'}
  $script:FinalMarkerProviderReads
 }
 Assert ($markerProviderReads -eq 0) ('Final observation reuses live marker attributes without a provider existence check; calls='+$markerProviderReads)
 $null=[IO.Directory]::CreateDirectory($marker)
 try {
  Set-IdleDocuments @($healthy)
  Assert ((Reject {Invoke-FixtureAutomaticIdle $scope}) -and $script:AppearanceFailureCode -eq 3 -and
   $script:IdleDocuments.Count -eq 1 -and [IO.Directory]::Exists($marker)) 'Final observation refuses a directory at the marker path and leaves it intact'
 }finally{[IO.Directory]::Delete($marker)}
 Set-IdleDocuments @($missing,$healthy)
 $final=$null;$failure='';try{$final=Invoke-FixtureAutomaticIdle $scope}catch{$failure=$_.Exception.Message}
 Assert ($final.ok -and !$failure -and $final.resident -and $script:Delay.Count -eq 1 -and $script:Delay[0].milliseconds -eq 500 -and ($script:IdleReadOnly -join ',') -ceq 'True,True' -and $script:AppearanceHelperFlags -eq 31 -and !$script:AppearanceObservationOnly) ('Final DOM mismatch recovers with one independent read-only observation: '+$failure)
 Set-IdleDocuments @($busy,$healthy)
 $final=$null;$failure='';try{$final=Invoke-FixtureAutomaticIdle $scope}catch{$failure=$_.Exception.Message}
 Assert ($final.ok -and !$failure -and $final.resident -and $script:Delay.Count -eq 1 -and $script:Delay[0].milliseconds -eq 500 -and ($script:IdleReadOnly -join ',') -ceq 'True,True' -and $script:AppearanceHelperFlags -eq 31 -and !$script:AppearanceObservationOnly) ('Typed final document busy recovers with one independent read-only observation: '+$failure)
 Set-IdleDocuments @($busy,$busy,$healthy)
 Assert ((Reject {Invoke-FixtureAutomaticIdle $scope}) -and $script:AppearanceFailureCode -eq 6 -and $script:Delay.Count -eq 1 -and $script:IdleDocuments.Count -eq 1 -and !$script:AppearanceObservationOnly) 'Persistent final document busy cannot trigger a third read'
 Set-IdleDocuments @($missing,$busy,$healthy)
 Assert ((Reject {Invoke-FixtureAutomaticIdle $scope}) -and $script:AppearanceFailureCode -eq 6 -and $script:Delay.Count -eq 1 -and $script:IdleDocuments.Count -eq 1) 'Style mismatch and busy share one retry budget'
 Set-IdleDocuments @($busy,$healthy)
 Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1) 'Manual Idle does not retry a typed busy result'
 Set-IdleDocuments @($busy,$healthy)
 $script:AppearanceObservationOnly=$true
 try{Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1) 'Verify-only Idle does not retry a typed busy result'}
 finally{$script:AppearanceObservationOnly=$false}
 Set-IdleDocuments @($healthy)
 Assert ((Invoke-FixtureAutomaticIdle $scope).ok -and !$script:Delay.Count -and $script:IdleReadOnly.Count -eq 1) 'Healthy final DOM never waits or duplicates a DOM read'
 Set-IdleDocuments @($missing,$missing,$healthy)
 Assert ((Reject {Invoke-FixtureAutomaticIdle $scope}) -and $script:AppearanceFailureCode -eq 6 -and $script:Delay.Count -eq 1 -and $script:IdleDocuments.Count -eq 1 -and !$script:AppearanceObservationOnly) 'Persistent mismatch is terminal after at most one extra read'
 Set-IdleDocuments @($unverifiable,$healthy)
 Assert ((Reject {Invoke-FixtureAutomaticIdle $scope}) -and $script:AppearanceFailureCode -eq 6 -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1) 'A malformed DOM receipt does not admit retry'
 Set-IdleDocuments @($missing,$healthy)
 Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1) 'Independent manual or verify-only Idle does not wait or retry'
 foreach($fault in 'editor','session','revision','transaction','busy-session','busy-transaction'){
  $initial=if($fault.StartsWith('busy-')){$busy}else{$missing}
  $faultKind=$fault -replace '^busy-',''
  Set-IdleDocuments @($initial,$healthy) $faultKind
  try {
   $rejected=Reject {Invoke-FixtureAutomaticIdle $scope}
   Assert ($rejected -and $script:Delay.Count -eq 1 -and $script:IdleDocuments.Count -eq 1 -and !$script:AppearanceObservationOnly) ('Final retry stops before DOM when scope changes: '+$fault)
   if($faultKind -eq 'transaction'){Assert ([IO.File]::ReadAllText($marker) -ceq 'fixture unfinished transaction') 'Final observation never recovers or rewrites a pending transaction'}
  }finally{
   $script:Editing=$false;$script:CurrentBirth='456'
   [IO.File]::WriteAllBytes($config,$configBytes)
   if($faultKind -eq 'transaction' -and [IO.File]::Exists($marker)){[IO.File]::Delete($marker)}
  }
 }
 $wrongTarget=[pscustomobject]@{revision=$scope.revision;targetId='0000000000000000'}
 Set-IdleDocuments @($healthy)
 Assert ((Reject {Invoke-FixtureAutomaticIdle $wrongTarget}) -and $script:AppearanceFailureCode -eq 4 -and !$script:Delay.Count -and $script:IdleDocuments.Count -eq 1) 'Final confirmation cannot consume a different approved target'
 Assert ($script:Starts -eq 0 -and $script:Applied -eq 0 -and $script:Resets -eq 0) 'Final DOM handling sends zero theme writes, backend resets or process starts'
}}

if($Case -eq 'CorrectionScope'){& {
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $idleAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance/idle.ps1'),[ref]$null,[ref]$null)
 $sessionReader=$idleAst.EndBlock.Statements|Where-Object {$_.Name -eq 'Get-AppearanceSteamSession'}
 Invoke-Expression $sessionReader.Extent.Text
 $originalRpc=${function:Invoke-LoaderRpc}
 $rpcDefinition=$ast.EndBlock.Statements|Where-Object {$_.Name -eq 'Invoke-LoaderRpc'}
 Invoke-Expression $rpcDefinition.Extent.Text
 $events=New-Object 'Collections.Generic.List[string]'
 function Invoke-AppearanceSharedSteam($Request){
  if($Request.operation -cne 'identity'){throw 'Unexpected Steam request'}
  $events.Add('identity')
  $lost=$script:ScopeReadReached -and $script:ScopeFault -ceq 'missing'
  $account=if($script:ScopeReadReached -and $script:ScopeFault -ceq 'account'){'21'}else{'20'}
  $power=if($script:ScopeReadReached -and $script:ScopeFault -ceq 'power'){'32'}else{'31'}
  [pscustomobject]@{protocol=1;presence=[int](!$lost);pid=123;birth='456';root='C:\fixture-steam';processEpoch='12';accountEpoch=$account;powerGeneration=$power}
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $events.Add('dom');$script:ScopeDomReads++
  $matched=$script:Applied -gt 0 -or $script:ScopeFault -ceq 'no-write'
  [pscustomobject]@{checked=$true;confirmed=$matched;reason='fixture-receipts';mismatched=$(if(!$matched){@('Mini Carousel')}else{@()})}
 }
 function Invoke-RestMethod($Uri,$Method,$Body,$ContentType,$TimeoutSec){
  if($Uri -cne 'http://127.0.0.1:35821/req' -or $Method -cne 'Post'){throw 'Unexpected CSS endpoint'}
  $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
  if($request.method -ceq 'get_themes' -and $script:ScopeDomReads -eq 2 -and !$script:ScopeReadReached){
   $script:ScopeReadReached=$true;$events.Add('read-before-write')
  }
  if($request.method -ceq 'set_theme_state'){$events.Add('set')}
  $response=if($request.method -ceq 'get_themes'){@(& $originalRpc $request.method $request.args)}else{& $originalRpc $request.method $request.args}
  [pscustomobject]@{success=$true;res=$response}
 }
 foreach($mode in @('power','account','missing','active','no-write')){
  $events.Clear();$script:ScopeFault=$mode;$script:ScopeDomReads=0;$script:ScopeReadReached=$false
  $script:AppearanceSharedIdentityKey='';$script:Applied=0;$script:Delay=@()
  $value=$null;$failure=''
  try{$value=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $order=$events -join ','
  if($mode -ceq 'active'){
   Assert ($value.ok -and $script:Applied -eq 1 -and $order.Contains('read-before-write,identity,set')) ('Correction must freshly verify scope after the read-only RPC before the first write: '+$order)
  }elseif($mode -ceq 'no-write'){
   Assert ($value.ok -and $script:Applied -eq 0 -and !$script:ScopeReadReached -and $order.EndsWith(',dom')) 'Already-matching first DOM does no correction or extra prewrite check'
  }else{
   Assert ($failure -like '*Steam*' -and $script:ScopeReadReached -and $script:Applied -eq 0 -and $script:AppearanceFailureCode -eq 1 -and $order.EndsWith('read-before-write,identity')) ("Late $mode change must stop before any set (sets=$($script:Applied); events=$order; failure=$failure)")
  }
  Assert ($script:Delay.Count -eq 0 -and $script:Resets -eq 0 -and $script:Starts -eq 0) ('No retry, reset or launch for scope case '+$mode)
 }
}}

if($Case -eq 'CorrectionReadRace'){& {
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 Complete-PresetReload
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json'
 $manifest=Join-Path $script:ThemeRoot 'Round\theme.json'
 $configOriginal=[IO.File]::ReadAllBytes($config);$manifestOriginal=[IO.File]::ReadAllBytes($manifest)
 $originalRpc=${function:Invoke-LoaderRpc};$originalRows=${function:Get-ThemeRows}
 $events=New-Object 'Collections.Generic.List[string]'
 function Get-ThemeRows {
  param([switch]$RevisionOnly)
  if($RevisionOnly){$events.Add('revision')}
  & $originalRows -RevisionOnly:$RevisionOnly
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:RaceDomReads++
  $matched=$script:Applied -gt 0
  [pscustomobject]@{checked=$true;confirmed=$matched;reason='fixture-receipts';mismatched=@(if(!$matched){'Round'})}
 }
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{},[ref]$WriteAttempted){
  if($Method -ceq 'get_themes'){
   $script:RaceReads++
   if($script:RaceDomReads -eq 2 -and !$script:RaceReadReached){
    $script:RaceReadReached=$true;$events.Add('backend-read')
    switch($script:RaceMode){
     'config' {
      $saved=[IO.File]::ReadAllText($config)|ConvertFrom-Json
      $saved.active=$false
      Write-ThemeConfigAtomic $config ($saved|ConvertTo-Json -Depth 20 -Compress)
      $script:RaceSavedHash=Get-ConfigHash $config
     }
     'manifest' {
      $saved=[IO.File]::ReadAllText($manifest)|ConvertFrom-Json
      $saved|Add-Member -NotePropertyName fixtureRevision -NotePropertyValue 'new'
      Write-ConfigAtomic $manifest ($saved|ConvertTo-Json -Depth 20 -Compress)
      $script:RaceSavedHash=Get-ConfigHash $manifest
     }
     'editor' {$script:Editing=$true}
     'empty' {return}
    }
   }
  }
  if($Method -ceq 'set_theme_state'){
   if($Arguments.name -cne 'Round'){throw 'Unexpected fixture theme'}
   $events.Add('set');if($WriteAttempted){$WriteAttempted.Value=$true}
   if($script:RaceMode -ceq 'config'){
    $live=[IO.File]::ReadAllText($config)|ConvertFrom-Json
    $live.active=[bool]$Arguments.state
    Write-ThemeConfigAtomic $config ($live|ConvertTo-Json -Depth 20 -Compress)
   }
  }
  & $originalRpc $Method $Arguments
 }
 $observations=foreach($mode in 'config','manifest','editor','empty','stable'){
  [IO.File]::WriteAllBytes($config,$configOriginal);[IO.File]::WriteAllBytes($manifest,$manifestOriginal)
  $events.Clear();$script:RaceMode=$mode;$script:RaceReads=0;$script:RaceDomReads=0;$script:RaceReadReached=$false;$script:RaceSavedHash=''
  $script:Editing=$false;$script:Applied=0;$script:Delay=@()
  $value=$null;$failure=''
  try{$value=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $savedFile=if($mode -ceq 'manifest'){$manifest}else{$config}
  [pscustomobject]@{mode=$mode;ok=[bool]$value.ok;failure=$failure;events=($events -join ',');reads=$script:RaceReads;dom=$script:RaceDomReads;
   submit=$script:Applied;waits=$script:Delay.Count;readReached=$script:RaceReadReached;preserved=(!$script:RaceSavedHash -or (Get-ConfigHash $savedFile) -ceq $script:RaceSavedHash)}
 }
 $observations|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'race-observations.json') -Encoding UTF8
 foreach($item in $observations){
  $expected=@{config='Appearance changed*no correction submitted';manifest='Appearance changed*no correction submitted';editor='CSSLoader Desktop is open*';empty='Backend theme missing/invalid:*'}[$item.mode]
  if($item.mode -ceq 'stable'){
   Assert ($item.ok -and !$item.failure -and $item.submit -eq 1 -and $item.events -ceq 'backend-read,revision,set' -and $item.reads -eq 5 -and $item.dom -eq 3) ('Stable correction retains exactly one revision read, the same CSS reads and a single set: '+($item|ConvertTo-Json -Compress))
  }else{
   Assert (!$item.ok -and $item.failure -like $expected -and $item.submit -eq 0 -and $item.dom -eq 2 -and $item.reads -eq 3 -and $item.preserved) ('Late change or empty snapshot must stop before set and preserve edits: '+($item|ConvertTo-Json -Compress))
  }
  Assert ($item.readReached -and !$item.waits) ('No retry or fallback when checking the same correction snapshot: '+$item.mode)
 }
 Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'No process launch or full reload is needed to reject the stale write'
}}

if($Case -in @('All','CorrectionRevision')){& {
 $script:Editing=$false;$script:Live=$true;$script:Rendered=$true
 $script:CurrentBirth='456';$script:KeepMissing=$false;$script:Unverifiable=$false
 if($Case -eq 'CorrectionRevision'){Invoke-PresetPrepare|Out-Null;Complete-PresetReload}
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $configPath=Join-Path $script:ThemeRoot 'Round\config_USER.json'
 $original=[IO.File]::ReadAllBytes($configPath)
 $manifestPath=Join-Path $script:ThemeRoot 'Round\theme.json'
 $originalManifest=[IO.File]::ReadAllBytes($manifestPath)
 $originalPlan=${function:Get-AppearanceTargetRenderPlan}
 function Get-AppearanceTargetRenderPlan($Name,$Manifest,$Config,$ContentIndex,$Target,$FileReceipts,$Defaults,[switch]$ValidateOnly){
  $script:CorrectionPlans++
  & $originalPlan @PSBoundParameters
 }
 $script:CorrectionRevisionProbe='';$script:CorrectionRevisionSamples=0
 $script:CorrectionRevisionEditHash=''
 function Write-CorrectionFixtureEdit {
  if($script:CorrectionRevisionProbe -eq 'manifest-before-write'){
   $manifest=[IO.File]::ReadAllText($manifestPath)|ConvertFrom-Json
   $manifest|Add-Member -NotePropertyName fixtureEdition -NotePropertyValue 'changed'
   Write-ConfigAtomic $manifestPath ($manifest|ConvertTo-Json -Depth 20 -Compress)
   $script:CorrectionRevisionEditHash=Get-ConfigHash $manifestPath
  }else{
   $config=[IO.File]::ReadAllText($configPath)|ConvertFrom-Json
   $config.active=!$config.active
   Write-ThemeConfigAtomic $configPath ($config|ConvertTo-Json -Compress)
   $script:CorrectionRevisionEditHash=Get-ConfigHash $configPath
  }
 }
 $originalResourceUpdate=${function:Invoke-OwnedPresetResourceUpdate}
 function Invoke-OwnedPresetResourceUpdate($Current){
  $next=& $originalResourceUpdate $Current
  if($script:CorrectionRevisionProbe -eq 'before-reload' -and $script:CorrectionRevisionSamples -eq 1){
   $next.backendReloadRequired=$true
   Write-CorrectionFixtureEdit
  }
  return $next
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:CorrectionRevisionSamples++
  if($script:CorrectionRevisionProbe -eq 'unchanged' -and !(Test-FixtureThemePagePlans $Status.themes)){throw 'Correction DOM checks need all 21 independent page plans'}
  if(($script:CorrectionRevisionProbe -eq 'before-control' -and $script:CorrectionRevisionSamples -eq 1) -or
     ($script:CorrectionRevisionProbe -in @('before-write','manifest-before-write') -and $script:CorrectionRevisionSamples -eq 2)){
   Write-CorrectionFixtureEdit
  }
  $missing=if(!$script:Applied){@('Mini Carousel')}else{@()}
  [pscustomobject]@{checked=$true;confirmed=(!$missing.Count);reason='fixture-known-style-mismatch';mismatched=$missing}
 }
 try{
  foreach($step in 'before-control','before-reload','before-write','manifest-before-write','unchanged'){
   [IO.File]::WriteAllBytes($configPath,$original)
   [IO.File]::WriteAllBytes($manifestPath,$originalManifest)
   $script:CorrectionPlans=0
   $script:CorrectionRevisionProbe=$step;$script:CorrectionRevisionSamples=0;$script:Applied=0;$script:CorrectionRevisionEditHash=''
   if($step -eq 'unchanged'){
    $value=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
    Assert ($value.ok -and $script:Applied -eq 1 -and $script:CorrectionRevisionSamples -eq 3) 'Matching revision still permits exactly one targeted correction with both DOM checks'
    Assert ($script:CorrectionPlans -eq 7) ('An unchanged automatic correction retains 21 page plans from seven traversals across three independent DOM observations (traversals='+$script:CorrectionPlans+')')
   }else{
    $failure=''
    try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
    $expected=if($step -eq 'before-reload'){'Theme settings or target changed; reread before reloading'}else{'Appearance changed*no correction submitted'}
    Assert ($script:CorrectionRevisionEditHash -and $script:Applied -eq 0 -and $script:Resets -eq 0 -and $failure -like $expected) ("A newer config or manifest at $step rejects stale correction with zero loader writes (applied=$($script:Applied), resets=$($script:Resets), failure=$failure)")
    $updated=if($step -eq 'manifest-before-write'){$manifestPath}else{$configPath}
    Assert ((Get-ConfigHash $updated) -ceq $script:CorrectionRevisionEditHash) ("Newly saved file at $step is preserved byte for byte")
   }
  }
  Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'Stale or current correction never launches or globally resets the backend'
 }finally{[IO.File]::WriteAllBytes($configPath,$original);[IO.File]::WriteAllBytes($manifestPath,$originalManifest)}
}}

if($Case -eq 'CorrectionResultScope'){& {
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload
 $beforeRoot=$script:ThemeRoot
 $otherRoot=Join-Path $Out 'other/homebrew/themes'
 $script:ThemeRoot=$otherRoot
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload
 $script:ThemeRoot=$beforeRoot
 $script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $config=Join-Path $beforeRoot 'Round/config_USER.json';$manifest=Join-Path $beforeRoot 'Round/theme.json'
 $originalConfig=[IO.File]::ReadAllBytes($config);$originalManifest=[IO.File]::ReadAllBytes($manifest)
 $rpc=${function:Invoke-LoaderRpc};$statusFn=${function:Get-AppearanceStatus}
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:PostDom++
  [pscustomobject]@{checked=$true;confirmed=($script:Applied -gt 0);reason='fixture-receipts';mismatched=@(if(!$script:Applied){'Round'})}
 }
 function Invoke-LoaderRpc([string]$Method,$Arguments,[ref]$WriteAttempted,[int]$CorrectionReason){
  if($Method -eq 'fetch_theme_path' -and $script:Applied -gt 0 -and $script:PostMode -eq 'target'){return $otherRoot}
  $result=& $rpc $Method $Arguments
  if($Method -eq 'set_theme_state'){
   if($Arguments.name -cne 'Round'){throw 'Unexpected correction'}
   if($WriteAttempted){$WriteAttempted.Value=$true}
   switch($script:PostMode){
    'formatting' {
     $value=[IO.File]::ReadAllText($config)|ConvertFrom-Json
     $formatted="`r`n"+($value|ConvertTo-Json -Depth 32)+"`r`n"
     [IO.File]::WriteAllText($config,$formatted,[Text.UTF8Encoding]::new($true))
     $script:PostSavedHash=Get-ConfigHash $config
    }
    'config' {
     $value=[IO.File]::ReadAllText($config)|ConvertFrom-Json
     $value.Intensity='25px'
     [IO.File]::WriteAllText($config,($value|ConvertTo-Json -Depth 32 -Compress))
     $script:PostSavedHash=Get-ConfigHash $config
    }
    'manifest' {
     [IO.File]::AppendAllText($manifest,"`r`n")
     $script:PostSavedHash=Get-ConfigHash $manifest
    }
   }
  }
  $result
 }
 $beforeHash=Get-ConfigHash $config
 $observed=foreach($mode in 'unchanged','formatting','config','manifest','target'){
  $script:ThemeRoot=$beforeRoot;Save-PresetTargetHint
  [IO.File]::WriteAllBytes($config,$originalConfig);[IO.File]::WriteAllBytes($manifest,$originalManifest)
  $script:PostMode=$mode;$script:Applied=0;$script:PostDom=0;$script:PostSavedHash='';$script:Delay=@()
  $baseline=& $statusFn
  $value=$null;$failure=''
  try{$value=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $file=if($mode -eq 'manifest'){$manifest}else{$config}
  $sameBytes=!$script:PostSavedHash -or (Get-ConfigHash $file) -ceq $script:PostSavedHash
  [pscustomobject]@{mode=$mode;ok=[bool]$value.ok;failure=$failure;code=$script:AppearanceFailureCode;writes=$script:Applied;dom=$script:PostDom;waits=$script:Delay.Count;savedBytes=$sameBytes;baselineRevision=$baseline.revision;resultRevision=$value.snapshot.revision;changedBytes=((Get-ConfigHash $file) -cne $beforeHash)}
 }
 $observed|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result-scope-observations.json') -Encoding UTF8
 foreach($row in $observed){
  $message=$row|ConvertTo-Json -Compress
  if($row.mode -in @('unchanged','formatting')){
   Assert ($row.ok -and !$row.failure -and $row.dom -eq 3) ('A stable selection still verifies after one CSS correction: '+$message)
   if($row.mode -eq 'formatting'){Assert ($row.changedBytes -and $row.baselineRevision -cne $row.resultRevision) 'A semantic match can confirm the new byte revision after loader formatting'}
  }else{
   Assert (!$row.ok -and $row.failure -like '*changed during automatic CSS correction*' -and $row.code -eq 4 -and $row.dom -eq 2) ('A new selection/target cannot inherit the old correction or cause a post-write DOM read: '+$message)
  }
  Assert ($row.writes -eq 1 -and !$row.waits -and $row.savedBytes) ('No second submission, retry or file overwrite: '+$message)
 }
 # A memo can know the new bytes, but the result comparison still needs the
 # actual config object for anything that differs from the pre-write digests.
 $script:PostMode='unchanged';$script:Applied=0
 $beforeHashes=@{};foreach($entry in Get-ThemeRows -RevisionOnly){$beforeHashes[$entry.name]=$entry.hash}
 $changed=[IO.File]::ReadAllText($config)|ConvertFrom-Json
 $changed.Intensity='25px'
 try{
  [IO.File]::WriteAllText($config,($changed|ConvertTo-Json -Depth 32 -Compress))
  $memo=@{};$null=Get-ThemeRows -AutomaticPlanMemo $memo
  $captured=$null
  $null=Get-AppearanceStatus -ExpectedCorrectionRoot $beforeRoot -CapturedRows ([ref]$captured) -AutomaticPlanMemo $memo -UnchangedConfigHashes $beforeHashes
  $changedRow=$captured|Where-Object name -eq 'Round'
  $unchangedRow=$captured|Where-Object name -eq 'Mini Carousel'
  Assert ($changedRow.hash -cne $beforeHashes.Round -and $changedRow.config.Intensity -ceq '25px' -and $null -eq $unchangedRow.config -and $unchangedRow.hash -ceq $beforeHashes['Mini Carousel']) 'Captured rows project only unchanged configs, even when the memo knows the changed row'
 }finally{[IO.File]::WriteAllBytes($config,$originalConfig)}
 Assert (!$script:Starts -and !$script:Resets) 'Scope checking does not launch, restart or globally reload anything'
}}

if($Case -eq 'CorrectionAdmission'){& {
 $originalRoot=$script:ThemeRoot
 $otherRoot=Join-Path $Out 'alternate/homebrew/themes'
 foreach($root in $originalRoot,$otherRoot){$script:ThemeRoot=$root;Invoke-PresetPrepare|Out-Null;Complete-PresetReload}
 $script:ThemeRoot=$originalRoot
 $rpc=${function:Invoke-LoaderRpc}
 function Install-FixtureInterruptedTransaction {
  $was=$script:ThemeRoot
  try{
   $script:ThemeRoot=$txnRoot
   $undo=New-PresetBackup @($config)
   Begin-PresetTransaction $undo
   $script:PresetTransactionActive=$false
   $selection=[IO.File]::ReadAllText($config)|ConvertFrom-Json
   $selection.active=$false
   [IO.File]::WriteAllText($config,($selection|ConvertTo-Json -Depth 16 -Compress))
   [IO.File]::AppendAllText($owner,"`r`n")
   if($fault -eq 'hint'){Save-PresetTargetHint}
   $script:ScopeHashes=@(Get-ConfigHash $config;Get-ConfigHash $owner;Get-ConfigHash $journal)
  }finally{$script:ThemeRoot=$was}
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:ScopeDom++
  if($phase -eq 'initial' -and $script:ScopeDom -eq 1){
   if($fault -ne 'during-rpc'){Install-FixtureInterruptedTransaction}
   $script:ScopeRaced=$true
  }
  if($phase -eq 'deferred' -and $script:ScopeDom -eq 2){
   return [pscustomobject]@{checked=$false;confirmed=$false;reason='render-shared-steam-document-main-unavailable';mismatched=@()}
  }
  [pscustomobject]@{checked=$true;confirmed=($script:Applied -gt 0);reason='fixture-receipts';mismatched=@(if(!$script:Applied){'Round'})}
 }
 function Start-Sleep {param($Milliseconds,$Seconds)
  $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds}
  if($phase -eq 'deferred' -and !$script:ScopeRaced){
   if($fault -ne 'during-rpc'){Install-FixtureInterruptedTransaction}
   $script:ScopeRaced=$true
  }
 }
 function Invoke-LoaderRpc([string]$Method,$Arguments,[ref]$WriteAttempted,[int]$CorrectionReason){
  if($Method -eq 'fetch_theme_path' -and $script:ScopeRaced){
   if($fault -eq 'backend'){return $otherRoot}
   if($fault -eq 'during-rpc' -and !$script:ScopeHashes.Count){Install-FixtureInterruptedTransaction}
   return $originalRoot
  }
  if($Method -eq 'set_theme_state' -and $WriteAttempted){$WriteAttempted.Value=$true}
  & $rpc $Method $Arguments
 }
 $observed=@(foreach($phase in 'initial','deferred','manual'){
  foreach($fault in $(if($phase -eq 'manual'){'same'}else{'same','during-rpc','backend','hint'})){
   $txnRoot=if($fault -in @('backend','hint')){$otherRoot}else{$originalRoot}
   $script:ThemeRoot=$txnRoot
   $config=Join-Path $txnRoot 'Round/config_USER.json';$stateDir=Get-PresetStateDir
   $owner=Join-Path $stateDir 'owner.json';$journal=Join-Path $stateDir 'transaction.json'
   $preConfig=[IO.File]::ReadAllBytes($config);$preOwner=[IO.File]::ReadAllBytes($owner)
   $oldConfigHash=Get-ConfigHash $config;$oldOwnerHash=Get-ConfigHash $owner
   $script:ThemeRoot=$originalRoot;Save-PresetTargetHint
   $script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
   $script:Applied=0;$script:ScopeDom=0;$script:Delay=@();$script:ScopeRaced=$false;$script:ScopeHashes=@()
   $value=$null;$failure=''
   try{
    try{
     if($phase -eq 'manual'){
      Install-FixtureInterruptedTransaction
      $value=Invoke-AppearanceControl 'Reconcile'
     }else{$value=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}
    }catch{$failure=$_.Exception.Message}
    [pscustomobject]@{phase=$phase;fault=$fault;ok=[bool]$value.ok;failure=$failure;code=$script:AppearanceFailureCode;
     configKept=($script:ScopeHashes.Count -eq 3 -and (Get-ConfigHash $config) -ceq $script:ScopeHashes[0]);
     ownerKept=($script:ScopeHashes.Count -eq 3 -and (Get-ConfigHash $owner) -ceq $script:ScopeHashes[1]);
     journalKept=([IO.File]::Exists($journal) -and (Get-ConfigHash $journal) -ceq $script:ScopeHashes[2]);
     recovered=((Get-ConfigHash $config) -ceq $oldConfigHash -and (Get-ConfigHash $owner) -ceq $oldOwnerHash -and ![IO.File]::Exists($journal));
     dom=$script:ScopeDom;waits=$script:Delay.Count;writes=$script:Applied}
   }finally{
    [IO.File]::WriteAllBytes($config,$preConfig);[IO.File]::WriteAllBytes($owner,$preOwner)
    if([IO.File]::Exists($journal)){[IO.File]::Delete($journal)}
   }
  }
 })
 $observed|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'correction-admission-observations.json') -Encoding UTF8
 foreach($row in $observed){
  $detail=$row|ConvertTo-Json -Compress
  if($row.phase -eq 'manual'){
   Assert ($row.ok -and $row.recovered -and $row.writes -eq 1 -and !$row.waits) ('Explicit manual reconcile retains legacy recovery: '+$detail)
   continue
  }
  Assert ($row.configKept -and $row.ownerKept -and $row.journalKept) ('Automatic recheck cannot recover a later or unrelated transaction: '+$detail)
  Assert ($row.waits -eq [int]($row.phase -eq 'deferred')) ('Scope protection adds no retry or wait: '+$detail)
  if($row.fault -eq 'hint'){
   Assert ($row.ok -and $row.writes -eq 1) ('Later hint cannot redirect an otherwise valid correction: '+$detail)
  }elseif($row.fault -eq 'backend'){
   Assert (!$row.ok -and $row.code -eq 4 -and !$row.writes) ('Different backend target declines the correction: '+$detail)
  }else{
   Assert (!$row.ok -and $row.code -eq 3 -and !$row.writes) ('Unfinished transaction declines the correction: '+$detail)
  }
 }
 Assert (!$script:Resets -and !$script:Starts) 'Rechecking correction scope never reloads or launches the backend'
}}

if($Case -eq 'PostCorrectionBackendDrift'){& {
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload
 $script:Live=$true
 $rpc=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments,[ref]$WriteAttempted,[int]$CorrectionReason){
  if($Method -eq 'fetch_theme_path' -and $script:Applied){$script:PostReads++}
  if($Method -eq 'get_themes'){
   $script:ThemeReads++
   $driftLimit=switch($script:DriftMode){'stable'{0} 'late-recovery'{2} 'persistent'{5} default{1}}
   if($script:PostReads -gt 0 -and $script:PostReads -le $driftLimit){
    return @($script:Rows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=($_.name -cne 'Round')}})
   }
  }
  if($Method -eq 'set_theme_state' -and $WriteAttempted){$WriteAttempted.Value=$true}
  & $rpc $Method $Arguments
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:Samples.Add([bool]$Status.configuredMatchesBackend)
  [pscustomobject]@{checked=$true;confirmed=($script:Applied -gt 0);reason='fixture-receipts';mismatched=@(if(!$script:Applied){'Round'})}
 }
 $observed=foreach($mode in 'stable','drift','manual-drift','late-recovery','persistent'){
  $script:DriftMode=$mode;$script:Applied=0;$script:PostReads=0;$script:ThemeReads=0;$script:Delay=@()
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $script:Samples=[Collections.Generic.List[bool]]::new()
  $result=$null;$errorMessage='';$script:AppearanceFailureCode=0
  try{$result=if($mode -eq 'manual-drift'){Invoke-AppearanceControl 'Reconcile'}else{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}}
  catch{$errorMessage=$_.Exception.Message;if($mode -cne 'persistent'){throw}}
  [pscustomobject]@{mode=$mode;ok=[bool]$result.ok;backend=$result.snapshot.configuredMatchesBackend;renderConfirmed=$result.renderConfirmed;writes=$script:Applied;statusReads=$script:PostReads;cssReads=$script:ThemeReads;dom=$script:Samples.ToArray();waits=$script:Delay.Count;failureCode=$script:AppearanceFailureCode;error=$errorMessage;
    outcomes=@(if($mode -cne 'manual-drift'){for($n=0;$n -lt $script:AppearanceRenderAttempts;$n++){$script:AppearanceRenderTrace[2*$n+1]}})}
 }
 $observed|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'backend-drift-observations.json') -Encoding UTF8
 $stable=$observed[0];$drift=$observed[1];$manual=$observed[2]
 Assert ($stable.ok -and $stable.writes -eq 1 -and $stable.dom.Count -eq 3 -and !$stable.waits -and $stable.statusReads -eq 1) 'When backend flags match, the existing post-write page check returns immediately'
 Assert ($drift.ok -and $drift.writes -eq 1 -and $drift.waits -eq 1 -and $drift.statusReads -eq 2) 'Automatic drift waits for the existing next slot, reads fresh state and never replays correction'
 Assert ($drift.dom.Count -eq 3 -and $false -notin $drift.dom -and $drift.cssReads -eq ($stable.cssReads+1)) ('No post-write DOM read in the known-backend-false slot, no extra CSS reads: '+($drift|ConvertTo-Json -Compress))
 Assert (!$manual.ok -and !$manual.backend -and $manual.renderConfirmed -and $manual.dom.Count -eq 2 -and !$manual.dom[-1] -and !$manual.waits) 'Explicit manual reconciliation still reports fresh rendering independently from the backend flags'
 $late=$observed[3];$persistent=$observed[4]
 Assert ($late.ok -and $late.writes -eq 1 -and $late.statusReads -eq 3 -and $late.cssReads -eq ($stable.cssReads+2) -and $late.dom.Count -eq 3 -and $false -notin $late.dom -and $late.waits -eq 2 -and ($late.outcomes -join ',') -ceq '4,2,5') ('Skip DOM only until backend flags recover; then observe without another wait: '+($late|ConvertTo-Json -Compress))
 Assert (!$persistent.ok -and $persistent.failureCode -eq 3 -and $persistent.error -like '*finite loading window*' -and $persistent.writes -eq 1 -and $persistent.statusReads -eq 5 -and $persistent.cssReads -eq ($stable.cssReads+4) -and $persistent.dom.Count -eq 2 -and $false -notin $persistent.dom -and $persistent.waits -eq 4 -and ($persistent.outcomes -join ',') -ceq '4,2,2,2,2') ('Persistent mismatch spends only the existing status slots, does not read DOM, and fails as backend-unconfirmed: '+($persistent|ConvertTo-Json -Compress))
 Assert (!$script:Resets -and !$script:Starts) 'The read-only retry neither globally resets nor launches anything'
}}

if($Case -eq 'PostCorrectionPresetDrift'){& {
 $null=New-Item -ItemType Directory -Path $script:ThemeRoot -Force
 foreach($dir in Get-ChildItem -LiteralPath (Join-Path $ComponentRoot 'steam-appearance\themes') -Directory){Copy-Item -LiteralPath $dir.FullName -Destination $script:ThemeRoot -Recurse}
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload;$script:Live=$true;Save-PresetTargetHint
 $owner=Read-PresetState;$ownerPath=Join-Path (Get-PresetStateDir) 'owner.json'
 $backup=Read-PresetBackup $owner.originalBackup
 $resource=Join-Path $script:ThemeRoot 'Art Hero\shared.css'
 $blob=Join-Path (Join-Path $backup.directory 'files') (@($backup.journal.entries|Where-Object existed)[0].blob)
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json'
 $marker=Join-Path (Get-PresetStateDir) 'transaction.json'
 $hint=Get-PresetTargetHintPath
 $restore=@{}
 foreach($p in $ownerPath,$resource,$blob,$config,$hint){$restore[$p]=[IO.File]::ReadAllBytes($p)}
 $rpc=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments,[ref]$WriteAttempted,[int]$CorrectionReason){
  if($Method -eq 'get_themes'){$script:ThemeReads++}
  if($Method -eq 'set_theme_state' -and $WriteAttempted){$WriteAttempted.Value=$true}
  $r=& $rpc $Method $Arguments
  if($Method -eq 'set_theme_state' -and $script:Applied -eq 7){
   switch($script:DriftMode){
    backup {[IO.File]::AppendAllText($blob,'broken backup')}
    {$_ -in @('resource','manual-resource')} {[IO.File]::AppendAllText($resource,'/* late edit */')}
    unowned {$changed=Read-PresetState;$changed.owned=$false;Save-PresetState $changed}
   }
  }
  $r
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:RenderSamples++
  $matched=$script:Applied -eq 7 -and ($script:DriftMode -notlike 'late-*' -or $script:Delay.Count -gt 0)
  [pscustomobject]@{checked=$true;confirmed=$matched;reason='fixture-receipts';mismatched=@(if(!$matched){$script:ThemeNames})}
 }
 function Start-Sleep {
  param($Milliseconds,$Seconds)
  $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds}
  if($script:Delay.Count -eq 1){
   switch($script:DriftMode){
    late-resource {[IO.File]::AppendAllText($resource,'/* edit while waiting */')}
    late-unowned {$changed=Read-PresetState;$changed.owned=$false;Save-PresetState $changed}
    late-transaction {
     $newConfig=([IO.File]::ReadAllText($config)|ConvertFrom-Json)
     $newConfig.active=$false
     Write-ThemeConfigAtomic $config ($newConfig|ConvertTo-Json -Compress)
     $undo=New-PresetBackup @($config)
     Begin-PresetTransaction $undo
     $script:PresetTransactionActive=$false
     [IO.File]::WriteAllBytes($config,$restore[$config])
     $script:LateJournalHash=Get-ConfigHash $marker
     $script:LateSelectionHash=Get-ConfigHash $config
    }
    late-hint-removed {[IO.File]::Delete($hint)}
   }
  }
 }
 $observed=foreach($mode in 'stable','backup','resource','unowned','manual-resource','late-stable','late-resource','late-unowned','late-transaction','late-hint-removed'){
  $script:DriftMode=$mode;$script:Applied=0;$script:ThemeReads=0;$script:Delay=@();$script:RenderSamples=0;$script:AppearanceFailureCode=0
  $script:AppearanceTimingClock=[Diagnostics.Stopwatch]::StartNew();$script:AppearanceCorrectionTiming=$null
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $result=$null;$problem=''
  $script:LateJournalHash='';$script:LateSelectionHash=''
  try {
   try{$result=if($mode -eq 'manual-resource'){Invoke-AppearanceControl 'Reconcile'}else{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}}
   catch{$problem=$_.Exception.Message}
   $savedJournal=$script:LateJournalHash -and [IO.File]::Exists($marker) -and (Get-ConfigHash $marker) -ceq $script:LateJournalHash
   $savedSelection=$script:LateSelectionHash -and (Get-ConfigHash $config) -ceq $script:LateSelectionHash
   $savedActive=([IO.File]::ReadAllText($config)|ConvertFrom-Json).active
   $hintExists=[IO.File]::Exists($hint)
   $sameOwner=[Convert]::ToBase64String([IO.File]::ReadAllBytes($ownerPath)) -ceq [Convert]::ToBase64String($restore[$ownerPath])
   $automaticReads=$script:ThemeReads;$failureCode=$script:AppearanceFailureCode
   $explicitRecovery=$false
   if($mode -ceq 'late-transaction' -and $savedJournal){
    $afterRecovery=Invoke-AppearanceControl 'Status'
    $explicitRecovery=$afterRecovery.ok -and ![IO.File]::Exists($marker) -and !([IO.File]::ReadAllText($config)|ConvertFrom-Json).active
   }
  }finally{
   foreach($p in $restore.Keys){[IO.File]::WriteAllBytes($p,$restore[$p])}
   if([IO.File]::Exists($marker)){[IO.File]::Delete($marker)}
  }
  [pscustomobject]@{mode=$mode;ready=[bool]($result -and $result.ok);prepared=$result.snapshot.packPrepared;dom=$script:RenderSamples;writes=$script:Applied;cssReads=$automaticReads;waits=$script:Delay.Count;code=$failureCode;outcome=if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]};error=$problem;
    journalPreserved=[bool]$savedJournal;selectionPreserved=[bool]$savedSelection;active=$savedActive;hintExists=$hintExists;ownerPreserved=$sameOwner;explicitRecovery=$explicitRecovery}
 }
 $script:AppearanceTimingClock=$null
 $observed|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'preset-drift-observations.json') -Encoding UTF8
 $stable=$observed[0];$manual=$observed[4]
 Assert ($stable.ready -and $stable.dom -eq 3 -and $stable.writes -eq 7 -and $stable.outcome -eq 5 -and !$stable.waits) 'Healthy automatic correction still checks current rendering and becomes Ready'
 foreach($case in $observed[1..3]){
  Assert (!$case.ready -and $case.dom -eq 2 -and $case.code -eq 3 -and $case.outcome -eq 4 -and $case.writes -eq 7 -and $case.cssReads -eq $stable.cssReads -and !$case.waits) ('Invalid post-write preset skips DOM and fails before Ready without retry: '+($case|ConvertTo-Json -Compress))
 }
 Assert ($manual.ready -and $manual.dom -eq 2 -and $manual.writes -eq 7 -and !$manual.waits) 'Manual reconciliation still reports observed rendering independently'
 $lateStable=$observed[5]
 Assert ($lateStable.ready -and $lateStable.prepared -and $lateStable.dom -eq 4 -and $lateStable.writes -eq 7 -and $lateStable.waits -eq 1) 'A valid preset with delayed styling still succeeds on the existing next slot'
 foreach($case in $observed[6..7]){
  Assert (!$case.ready -and $case.dom -eq 3 -and $case.code -eq 3 -and $case.outcome -eq 4 -and $case.writes -eq 7 -and $case.cssReads -eq $lateStable.cssReads -and $case.waits -eq 1) ('Preset invalidated between slots must skip DOM without writing or waiting again: '+($case|ConvertTo-Json -Compress))
 }
 $transaction=$observed[8];$removedHint=$observed[9]
 Assert (!$transaction.ready -and $transaction.code -eq 3 -and $transaction.error -like '*transaction unfinished*' -and $transaction.dom -eq 3 -and $transaction.writes -eq 7 -and $transaction.waits -eq 1 -and $transaction.journalPreserved -and $transaction.selectionPreserved -and $transaction.active -and $transaction.ownerPreserved) ('Continuation must not recover a transaction belonging to another write: '+($transaction|ConvertTo-Json -Compress))
 Assert $transaction.explicitRecovery 'Independent normal status retains its existing preset recovery behavior'
 Assert ($removedHint.ready -and !$removedHint.hintExists -and $removedHint.dom -eq 4 -and $removedHint.writes -eq 7 -and $removedHint.waits -eq 1) 'A healthy continuation does not rewrite the target hint'
 Assert (!$script:Resets -and !$script:Starts) 'No backend reset or launch in any post-write scenario'
}}
if($Case -in @('CorrectionBusy','CorrectionMainUnavailable','FinalMainUnavailable')){& {
 # Exercise the production consumer and receipt validator with inert JSON.
 $idleAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance\idle.ps1'),[ref]$tokens,[ref]$errors)
 foreach($fn in $idleAst.EndBlock.Statements|Where-Object {$_.Name -in @('Get-AppearanceRenderedStatus','Test-AppearanceRendered','Get-AppearanceSteamSession')}){Invoke-Expression $fn.Extent.Text}
 function Initialize-ResidentAppearanceInterop {}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 Add-Type -TypeDefinition @"
namespace Ymcc {
    public static class CssResidentProcess {
        public static string Identity;
        public static System.Collections.Generic.Queue<string> Documents = new System.Collections.Generic.Queue<string>();
        public static string SharedSteam(string owner, string request) {
            if (request.Contains("\"operation\":\"identity\"")) return Identity;
            if (request.Contains("\"operation\":\"documents\"")) return Documents.Dequeue();
            throw new System.InvalidOperationException("unexpected fixture operation");
        }
    }
}
"@
 $ProcessQuery='Local\YMCC.CssInventory.0123456789abcdef0123456789abcdef'
 $identity=@{ok=$true;protocol=1;presence=1;pid=123;birth='456';root='C:\fixture-steam';processEpoch='1';accountEpoch='1';powerGeneration='1'}
 [Ymcc.CssResidentProcess]::Identity=$identity|ConvertTo-Json -Compress
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $plan=@(Get-AppearanceRenderExpectation (Get-AppearanceStatus) 'SP')
 function New-FixtureDocument([bool]$Missing){
  $values=@(foreach($row in $plan){if($row.kind -eq 'variable'){$row.value}elseif($row.enabled -and !($Missing -and $row.name -eq 'Mini Carousel')){'1'}else{''}})
  $counts=@(foreach($row in $plan){if($row.kind -eq 'file'){[int]$row.enabled}})
  @{ok=$true;protocol=1;sequential=$true;complete=$false;documents=@(@{id='fixture-page';kind='SP';value=@{protocol=4;documentKey='fixture';ready=$true;styles=12;values=$values;fileCounts=$counts}})}|ConvertTo-Json -Compress -Depth 10
 }
 $missing=New-FixtureDocument $true;$healthy=New-FixtureDocument $false
 $busy='{"ok":false,"reason":"live-busy"}';$broken='{"ok":false,"reason":"read-failed"}'
 $unavailable=@{ok=$false;mutated=$false;complete=$false;reason=$DocumentInterruption;documents=@()}|ConvertTo-Json -Compress
 $notRetryable=if($DocumentInterruption -cne 'document-main-unavailable'){@('{"ok":false,"reason":"document-read-cancelled"}','{"ok":false,"reason":"document-targets-changed"}')}
 function Set-FixtureDocuments([string[]]$Documents){
  $script:Delay=@();$script:Applied=0;[Ymcc.CssResidentProcess]::Documents.Clear()
  foreach($document in $Documents){[Ymcc.CssResidentProcess]::Documents.Enqueue($document)}
 }
 $script:Resets=0
 if($Case -eq 'FinalMainUnavailable'){
  $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-AutomaticAppearanceIdle'},$true)
  Invoke-Expression $fn.Extent.Text
  $scope=(Invoke-AppearanceControl 'Status').snapshot
  Set-FixtureDocuments @($unavailable,$healthy)
  $final=$null;$failure='';try{$final=Invoke-AutomaticAppearanceIdle $scope}catch{$failure=$_.Exception.Message}
  Assert ($final.ok -and $final.resident -and !$failure -and $script:Delay.Count -eq 1 -and $script:Delay[0].milliseconds -eq 500 -and $script:AppearanceHelperFlags -eq 31 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) ('Document read restored before the one read-only confirmation ('+$DocumentInterruption+'): '+$failure)
  Set-FixtureDocuments @($unavailable,$unavailable,$healthy)
  Assert ((Reject {Invoke-AutomaticAppearanceIdle $scope}) -and $script:AppearanceFailureCode -eq 6 -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Persistent missing main document never triggers a third read'
  Set-FixtureDocuments @($busy,$unavailable,$healthy)
  Assert ((Reject {Invoke-AutomaticAppearanceIdle $scope}) -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Busy and unavailable share the same existing recheck'
  foreach($invalid in @(@($broken,'{"ok":false,"reason":"document-source-ambiguous"}',('{"ok":"false","reason":"'+$DocumentInterruption+'"}'))+@($notRetryable|Where-Object {$_}))){
   Set-FixtureDocuments @($invalid,$healthy)
   Assert ((Reject {Invoke-AutomaticAppearanceIdle $scope}) -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Unexpected reasons and malformed replies do not get a retry'
  }
  foreach($verify in @($false,$true)){
   $script:AppearanceObservationOnly=$verify
   try{
    Set-FixtureDocuments @($unavailable,$healthy)
    Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) ('Independent Idle does not retry, verify-only='+$verify)
   }finally{$script:AppearanceObservationOnly=$false}
  }
  Set-FixtureDocuments @($healthy)
  Assert ((Invoke-AutomaticAppearanceIdle $scope).ok -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'A healthy main page takes one read and no wait'
  if($DocumentInterruption -ceq 'document-targets-refreshed'){
   foreach($malformed in @(
     '{"ok":false,"mutated":true,"complete":false,"reason":"document-targets-refreshed","documents":[]}',
     '{"ok":false,"mutated":false,"complete":true,"reason":"document-targets-refreshed","documents":[]}',
     '{"ok":false,"mutated":false,"complete":false,"reason":"document-targets-refreshed","documents":[{}]}',
     '{"ok":false,"reason":"document-targets-refreshed"}'
   )){
    Set-FixtureDocuments @($malformed,$healthy)
    Assert ((Reject {Invoke-AutomaticAppearanceIdle $scope}) -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Target refresh must be a typed, read-only, empty failure batch'
   }
   Set-FixtureDocuments @($unavailable,$healthy)
   Assert ((Reject {Invoke-AppearanceControl 'Reconcile'}) -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Target refresh does not authorize correction or a retry of writes'
  }
  $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$configBytes=[IO.File]::ReadAllBytes($config)
  function Start-Sleep {param($Milliseconds,$Seconds)
   $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds}
   switch($fault){
    editor {$script:Editing=$true}
    account {$identity.accountEpoch='2';[Ymcc.CssResidentProcess]::Identity=$identity|ConvertTo-Json -Compress}
    revision {$row=[IO.File]::ReadAllText($config)|ConvertFrom-Json;$row.active=!$row.active;Write-ThemeConfigAtomic $config ($row|ConvertTo-Json -Compress)}
   }
  }
  foreach($fault in 'editor','account','revision'){
   Set-FixtureDocuments @($unavailable,$healthy)
   try{
    Assert ((Reject {Invoke-AutomaticAppearanceIdle $scope}) -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1 -and !$script:AppearanceObservationOnly) ('Recheck stops before DOM when scope changes: '+$fault)
   }finally{
    $script:Editing=$false;$identity.accountEpoch='1';[Ymcc.CssResidentProcess]::Identity=$identity|ConvertTo-Json -Compress
    [IO.File]::WriteAllBytes($config,$configBytes)
   }
  }
  Assert ($script:Starts -eq 0 -and $script:Applied -eq 0 -and $script:Resets -eq 0) 'Final document retry never starts a process or writes to CSSLoader'
  return
 }
 if($Case -eq 'CorrectionMainUnavailable'){
  $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$configBytes=[IO.File]::ReadAllBytes($config)
  $mini=$script:Rows|Where-Object name -eq 'Mini Carousel'
  function Start-Sleep {param($Milliseconds,$Seconds)
   $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds;writes=$script:Applied}
   switch($fault){
    editor {$script:Editing=$true}
    account {$identity.accountEpoch='2';[Ymcc.CssResidentProcess]::Identity=$identity|ConvertTo-Json -Compress}
    revision {$row=[IO.File]::ReadAllText($config)|ConvertFrom-Json;$row.active=!$row.active;Write-ThemeConfigAtomic $config ($row|ConvertTo-Json -Compress)}
   }
  }
  Set-FixtureDocuments @($missing,$unavailable,$healthy,$healthy)
  $recovered=$null;$failure='';try{$recovered=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $onlyObserved=$recovered.ok -and !$failure -and $script:Delay.Count -eq 1 -and $script:Applied -eq 0 -and $script:AppearanceRenderAttempts -eq 2 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1
  if($onlyObserved){$null=Invoke-AppearanceControl 'Idle';$onlyObserved=$script:AppearanceHelperFlags -eq 31 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0}
  Set-FixtureDocuments @($missing,$unavailable,$missing,$missing,$healthy,$healthy)
  $mini.enabled=$false
  $recovered=$null;$driftFailure='';try{$recovered=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$driftFailure=$_.Exception.Message}
  $earlyWrites=if($script:Delay.Count){$script:Delay[0].writes}else{$script:Applied}
  $deferredWrite=$recovered.ok -and !$driftFailure -and $script:Delay.Count -eq 1 -and $earlyWrites -eq 0 -and $script:Applied -eq 1 -and $script:AppearanceCorrectionAttempt -eq 2 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1
  Assert ($onlyObserved -and $deferredWrite) ('Interrupted locked document read ('+$DocumentInterruption+'): clean recovery='+$onlyObserved+' ('+$failure+'); deferred one write='+$deferredWrite+' ('+$driftFailure+'); writes before next slot='+$earlyWrites)
  Assert ((Invoke-AppearanceControl 'Idle').ok -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'Deferred correction still needs a separate final DOM receipt'
  Set-FixtureDocuments @($missing,$unavailable,$missing,$unavailable,$missing,$missing,$missing)
  $mini.enabled=$false
  Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:AppearanceFailureCode -eq 6 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 4 -and $script:AppearanceCorrectionAttempt -eq 2 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'Repeated missing locked main document permits no third correction or CSS write despite flag drift'
  foreach($unknown in @(@($broken,'{"ok":false,"reason":"document-source-unconfirmed"}','{"ok":false,"reason":"document-transport-unavailable"}','{"ok":true,"protocol":1,"complete":true,"documents":[]}')+@($notRetryable|Where-Object {$_}))){
   Set-FixtureDocuments @($missing,$unknown,$healthy)
   Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Unknown source, bad receipt or transport failure does not admit automatic writes or deferral'
  }
  if($DocumentInterruption -ceq 'document-targets-refreshed'){
   foreach($malformed in @(
     '{"ok":false,"mutated":true,"complete":false,"reason":"document-targets-refreshed","documents":[]}',
     '{"ok":false,"mutated":false,"complete":true,"reason":"document-targets-refreshed","documents":[]}',
     '{"ok":false,"mutated":false,"complete":false,"reason":"document-targets-refreshed","documents":[{}]}',
     '{"ok":false,"reason":"document-targets-refreshed"}'
   )){
    Set-FixtureDocuments @($missing,$malformed,$healthy)
    Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Malformed refreshed targets do not allow deferral or CSS writes'
   }
  }
  Set-FixtureDocuments @($missing,$unavailable,$missing,$broken,$healthy)
  Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'A read failure after typed deferral does not inherit earlier write eligibility'
  $mini.enabled=$true
  foreach($fault in 'editor','account','revision'){
   Set-FixtureDocuments @($missing,$unavailable,$missing,$healthy)
   try{
    Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq $(if($fault -eq 'editor'){1}else{2})) ('No deferred CSS writes after scope change: '+$fault)
   }finally{
    $script:Editing=$false;$identity.accountEpoch='1';[Ymcc.CssResidentProcess]::Identity=$identity|ConvertTo-Json -Compress
    [IO.File]::WriteAllBytes($config,$configBytes)
   }
  }
  Set-FixtureDocuments @($unavailable,$healthy)
  Assert ((Reject {Invoke-AppearanceControl 'Reconcile'}) -and $script:Applied -eq 0 -and !$script:Delay.Count -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Manual reconciliation does not acquire an automatic deferral'
  Assert ($script:Resets -eq 0 -and $script:Starts -eq 0) 'Deferred correction does not reset or launch CSSLoader'
  return
 }
 Set-FixtureDocuments @($missing,$busy,$healthy,$healthy)
 $failure='';$automatic=$null
 try{$automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
 Assert ($automatic.ok -and !$failure -and $script:Delay.Count -eq 1 -and $script:Applied -eq 0 -and $script:AppearanceCorrectionAttempt -eq 1 -and $script:AppearanceRenderAttempts -eq 2 -and $script:AppearanceRenderTrace[1] -eq 4 -and $script:AppearanceRenderTrace[3] -eq 5 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) ('Busy recheck holds writes and takes only the next existing read-only slot, then confirms: '+$failure)
 $idle=Invoke-AppearanceControl 'Idle'
 Assert ($idle.ok -and $idle.resident -and $script:AppearanceHelperFlags -eq 31 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'Deferred automatic success still requires the independent final resident DOM receipt'
 Set-FixtureDocuments @($missing,$busy,$missing,$missing,$healthy,$healthy)
 $script:AppearanceTimingClock=[Diagnostics.Stopwatch]::StartNew()
 $failure='';$automatic=$null
 try{$automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
 Assert ($automatic.ok -and !$failure -and $script:Delay.Count -eq 1 -and $script:Applied -eq 1 -and $script:AppearanceRenderAttempts -eq 2 -and $script:AppearanceCorrectionAttempt -eq 2 -and $script:AppearanceCorrectionTiming[6] -eq 5 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) ('After busy, two fresh mismatching receipts allow one targeted correction in the next existing slot: '+$failure)
 $idle=Invoke-AppearanceControl 'Idle'
 Assert ($idle.ok -and $idle.resident -and $script:AppearanceHelperFlags -eq 31 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'Resumed correction still requires a separate final resident DOM receipt'
 $script:AppearanceTimingClock=$null
 Set-FixtureDocuments @($missing,$busy,$missing,$busy,$missing,$missing,$missing)
 Assert (Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) 'Repeated busy stays unconfirmed when no write can be verified'
 Assert ($script:AppearanceFailureCode -eq 6 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 4 -and $script:AppearanceRenderAttempts -eq 5 -and $script:AppearanceCorrectionAttempt -eq 2 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'Repeated busy allows no third locked correction check and no blind writes'
 Set-FixtureDocuments @($missing,$busy,$missing,$missing,$missing,$missing,$missing,$missing)
 Assert (Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) 'One resumed corrective submission is not confirmation until the DOM matches'
 Assert ($script:Applied -eq 1 -and $script:Delay.Count -eq 4 -and $script:AppearanceRenderAttempts -eq 5 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 0) 'A submitted correction still cannot replay when its DOM proof does not arrive'
 Set-FixtureDocuments @($missing,$busy,$missing,$missing,$healthy)
 $script:FailApply=$true
 try{
  Assert (Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) 'A resumed CSS write rejected by the backend is not replayed'
  Assert ($script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'A failed CSS write is not a fresh read-only busy deferral'
 }finally{$script:FailApply=$false}
 Set-FixtureDocuments @($missing,$broken,$healthy)
 Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and $script:Delay.Count -eq 0 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'A different failed document response keeps the existing rejection'
 Set-FixtureDocuments @($busy,$healthy)
 Assert ((Reject {Invoke-AppearanceControl 'Reconcile'}) -and $script:Applied -eq 0 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Manual reconcile still rejects a busy receipt without retry'
 & {
  Set-FixtureDocuments @($missing,$busy,$missing,$healthy)
  function Start-Sleep {param($Milliseconds,$Seconds) $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds};$script:Editing=$true}
  try{
   Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}) -and $script:Applied -eq 0 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Desktop taking edit ownership during deferral prevents resumed correction'
  }finally{$script:Editing=$false}
 }
 & {
  Set-FixtureDocuments @($missing,$busy,$missing,$healthy)
  $original=Read-PresetState
  $renderFixture=${function:Get-AppearanceRenderedStatus};$script:BusyFixtureReads=0
  function Get-AppearanceRenderedStatus($Session,$Status){
   $result=& $renderFixture $Session $Status
   if((++$script:BusyFixtureReads) -eq 3){$state=Read-PresetState;$state.pendingReload=$true;Save-PresetState $state}
   $result
  }
  try{
   $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
   Assert ($failure -like 'Appearance prerequisites changed before deferred correction*' -and (Read-PresetState).pendingReload -and $script:Resets -eq 0 -and $script:Applied -eq 0 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'Reload becoming pending after the fresh outer read is not replayed by the deferred theme-only check'
  }finally{Save-PresetState $original}
 }
 Set-FixtureDocuments @($missing,$busy,$healthy)
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$configBytes=[IO.File]::ReadAllBytes($config)
 function Start-Sleep {param($Milliseconds,$Seconds) $script:Delay+=@{milliseconds=$Milliseconds;seconds=$Seconds};$selection=[IO.File]::ReadAllText($config)|ConvertFrom-Json;$selection.active=!$selection.active;Write-ThemeConfigAtomic $config ($selection|ConvertTo-Json -Compress)}
 try{
  $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
  Assert ($failure -like 'Appearance changed after the one correction*' -and $script:Applied -eq 0 -and $script:Delay.Count -eq 1 -and [Ymcc.CssResidentProcess]::Documents.Count -eq 1) 'A new selection during deferral is rejected before observing or writing the replacement scope'
 }finally{[IO.File]::WriteAllBytes($config,$configBytes)}
 Assert ($script:Resets -eq 0 -and $script:Starts -eq 0) 'Deferred automatic action never resets or launches the backend'
}}

if($Case -eq 'CorrectionAlreadyMatched'){& {
 $script:AppearanceContentManifestFile=Join-Path $Out 'content-index.json'
 Copy-Item -LiteralPath (Join-Path $script:PackRoot 'manifest.json') -Destination $script:AppearanceContentManifestFile
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$original=[IO.File]::ReadAllBytes($config)
 $themeManifest=Join-Path $script:ThemeRoot 'Round\theme.json';$originalManifest=[IO.File]::ReadAllBytes($themeManifest)
 $originalIndex=[IO.File]::ReadAllBytes($script:AppearanceContentManifestFile)
 $originalStatus=${function:Get-AppearanceStatus};$originalRpc=${function:Invoke-LoaderRpc}
 $originalRows=${function:Get-ThemeRows};$originalConfigRead=${function:Read-AppearanceThemeConfig}
 function Get-AppearanceStatus {$script:AlreadyStatusReads++;& $originalStatus}
 function Get-ThemeRows([switch]$RevisionOnly,[switch]$DigestOnly,[hashtable]$AutomaticPlanMemo,[switch]$ValidateRenderOnly,[switch]$ReadOnlyProjection,[hashtable]$UnchangedConfigHashes){
  if($script:AlreadyTrackReads -and $DigestOnly){$script:AlreadyDigestPasses++}
  & $originalRows @PSBoundParameters
 }
 function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
  if($script:AlreadyTrackReads){$script:AlreadyDecoded++}
  & $originalConfigRead @PSBoundParameters
 }
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($Method -eq 'get_themes'){$script:AlreadyGetThemes++}
  & $originalRpc $Method $Arguments
 }
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Get-AppearanceSteamSession {
  if($script:AlreadyScenario -eq 'identity-change' -and $script:AlreadyDomReads -ge 2){throw 'Shared Steam identity changed during the appearance request'}
  [pscustomobject]@{pid=$script:CurrentPid;birth=$script:CurrentBirth;root='C:\fixture-steam';backendExe=$script:BackendExe}
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:AlreadyDomReads++
  if($script:AlreadyDomReads -eq 2){
   switch($script:AlreadyScenario){
    'config-change' {$next=[IO.File]::ReadAllText($config)|ConvertFrom-Json;$next.active=!$next.active;Write-ThemeConfigAtomic $config ($next|ConvertTo-Json -Compress)}
    'manifest-change' {[IO.File]::AppendAllText($themeManifest,"`n")}
    'index-change' {[IO.File]::AppendAllText($script:AppearanceContentManifestFile,"`n")}
    'locked-config' {$script:AlreadyHeldConfig=[IO.File]::Open($config,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    'desktop-open' {$script:Editing=$true}
   }
   $script:AlreadyTrackReads=$true
  }
  $missing=($script:AlreadyDomReads -eq 1 -and $script:AlreadyScenario -ne 'manual') -or ($script:AlreadyScenario -eq 'final-mismatch' -and $script:AlreadyDomReads -eq 3)
  [pscustomobject]@{checked=$true;confirmed=!$missing;reason='fixture-render-receipts';mismatched=@(if($missing){'Mini Carousel'})}
 }
 function Reset-AlreadyMatched([string]$Scenario){
  if($script:AlreadyHeldConfig){$script:AlreadyHeldConfig.Dispose();$script:AlreadyHeldConfig=$null}
  [IO.File]::WriteAllBytes($config,$original)
  [IO.File]::WriteAllBytes($themeManifest,$originalManifest)
  [IO.File]::WriteAllBytes($script:AppearanceContentManifestFile,$originalIndex)
  $script:AlreadyScenario=$Scenario;$script:AlreadyDomReads=0;$script:AlreadyStatusReads=0;$script:AlreadyGetThemes=0
  $script:AlreadyTrackReads=$false;$script:AlreadyDigestPasses=0;$script:AlreadyDecoded=0
  $script:Editing=$false;$script:Applied=0;$script:Resets=0;$script:Delay=@()
 }
 try{
  Reset-AlreadyMatched 'healthy'
  $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
  Assert ($automatic.ok -and $automatic.renderConfirmed -and $script:Applied -eq 0 -and $script:AlreadyDomReads -eq 2 -and $script:AlreadyStatusReads -eq 2 -and $script:AlreadyGetThemes -eq 2 -and $script:Delay.Count -eq 0) ('A fresh match inside automatic reconciliation needs no sync or third verification before Ready; DOM='+$script:AlreadyDomReads+' status='+$script:AlreadyStatusReads+' get_themes='+$script:AlreadyGetThemes)
  [pscustomobject]@{digestPasses=$script:AlreadyDigestPasses;decoded=$script:AlreadyDecoded;domBeforeReady=$script:AlreadyDomReads;writes=$script:Applied}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'matched-work.json') -Encoding UTF8
  Assert ($script:AlreadyDigestPasses -eq 1 -and $script:AlreadyDecoded -eq 0) 'An already matched correction rechecks bytes without decoding theme files'
  $final=Invoke-AppearanceControl 'Idle'
  Assert ($final.ok -and $final.resident -and $script:AppearanceHelperFlags -eq 31 -and $script:AlreadyDomReads -eq 3 -and $script:AlreadyStatusReads -eq 3) 'Final Idle still performs a fresh independent status and DOM confirmation'
  Reset-AlreadyMatched 'final-mismatch'
  $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
  Assert ($automatic.ok -and (Reject {Invoke-AppearanceControl 'Idle'}) -and $script:AppearanceFailureCode -eq 6 -and $script:Applied -eq 0) 'An early match cannot acknowledge a subsequently mismatched final page'
  foreach($scenario in 'config-change','manifest-change','index-change','locked-config','desktop-open','identity-change'){
   Reset-AlreadyMatched $scenario
   $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
   $expected=@{'config-change'='Appearance changed during DOM verification*';'manifest-change'='Appearance changed during DOM verification*';'index-change'='Appearance changed during DOM verification*';'locked-config'='Appearance changed during DOM verification*';'desktop-open'='CSSLoader Desktop is open*';'identity-change'='Shared Steam identity changed*'}[$scenario]
   Assert ($failure -like $expected -and $script:Applied -eq 0 -and $script:AlreadyDomReads -eq 2) ('Fresh match does not bypass '+$scenario+': '+$failure)
  }
  Reset-AlreadyMatched 'manual'
  $manual=Invoke-AppearanceControl 'Reconcile'
  Assert ($manual.ok -and $script:AlreadyDomReads -eq 2 -and $script:AlreadyStatusReads -eq 2 -and $script:AlreadyGetThemes -eq 3 -and $script:Applied -eq 0) 'Manual reconciliation keeps its original explicit check sequence'
  Assert ($script:AlreadyDigestPasses -eq 0) 'Explicit reconciliation does not use the automatic no-write check'
  Assert ($script:Starts -eq 0 -and $script:Resets -eq 0) 'Assistants never start or globally reload CSS on an already matched document'
 }finally{Reset-AlreadyMatched 'finished'}
}}

if($Case -eq 'ReloadEditor'){& {
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true;$script:Rendered=$true;$script:MissingStyles=@()
 $originalRpc=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  $answer=& $originalRpc $Method $Arguments
  if($Method -eq 'get_themes' -and $script:ReloadScenario -eq 'opens-in-reload' -and (Get-PSCallStack|Where-Object FunctionName -eq 'Invoke-AppearanceReload')){$script:Editing=$true}
  if($Method -eq 'reset' -and $script:ReloadScenario -eq 'opens-after-reset'){
   $script:Editing=$true;($script:Rows|Where-Object name -eq 'Round').enabled=$false
  }
  if($Method -eq 'reset' -and $script:ReloadScenario -in @('opens-before-first-set','opens-between-sets','opens-after-last-set','closed-drift')){
   ($script:Rows|Where-Object name -eq 'Round').enabled=$false
   if($script:ReloadScenario -in @('opens-between-sets','closed-drift')){($script:Rows|Where-Object name -eq 'Mini Carousel').enabled=$false}
  }
  if($Method -eq 'get_themes' -and $script:ReloadScenario -in @('opens-before-first-set','opens-between-sets','opens-after-last-set','opens-without-writes') -and
     $script:Applied -eq [int]($script:ReloadScenario -in @('opens-between-sets','opens-after-last-set')) -and
     (Get-PSCallStack|Where-Object FunctionName -eq 'Sync-AppearanceBackend')){$script:Editing=$true}
  $answer
 }
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Reset-ReloadEditorFixture([string]$Scenario){
  $script:ReloadScenario=$Scenario;$script:Editing=$Scenario -in @('already-open','explicit')
  $script:Resets=0;$script:Applied=0;$script:Delay=@()
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $owner=Read-PresetState;$owner.pendingReload=$true;Save-PresetState $owner
 }
 $failures=New-Object Collections.Generic.List[string]
 $observed=New-Object Collections.Generic.List[object]
 foreach($scenario in 'already-open','opens-in-reload','opens-after-reset','opens-before-first-set','opens-between-sets','opens-after-last-set','opens-without-writes'){
  Reset-ReloadEditorFixture $scenario
  $revision=Get-ThemeRevision @(Get-ThemeRows -RevisionOnly)
  $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
  $expectedResets=[int]($scenario -notin @('already-open','opens-in-reload'))
  $expectedSets=[int]($scenario -in @('opens-between-sets','opens-after-last-set'))
  $pending=(Read-PresetState).pendingReload
  $unchanged=(Get-ThemeRevision @(Get-ThemeRows -RevisionOnly)) -ceq $revision
  $observed.Add([pscustomobject]@{scenario=$scenario;reason=$failure;resets=$script:Resets;sets=$script:Applied;expectedSets=$expectedSets;pending=$pending;unchanged=$unchanged;waits=$script:Delay.Count})
  $label="$scenario requires editor deferral (reason=$failure, resets=$($script:Resets), sets=$($script:Applied))"
  if($failure -like 'CSSLoader Desktop is open*' -and $script:Resets -eq $expectedResets -and $script:Applied -eq $expectedSets -and !$script:Delay.Count -and $pending -and $unchanged){Assert $true $label}else{$failures.Add($label)}
 }
 $observed.ToArray()|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Out 'reload-editor.json') -Encoding UTF8
 if($failures.Count){throw ($failures -join '; ')}
 Reset-ReloadEditorFixture 'closed'
 $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
 $final=Invoke-AppearanceControl 'Idle'
 Assert ($automatic.ok -and $final.ok -and $final.resident -and !$final.snapshot.backendReloadRequired -and $script:Resets -eq 1 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 0) 'Closed editor allows one necessary reset and fresh final confirmation without retries'
 Reset-ReloadEditorFixture 'closed-drift'
 $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
 Assert ($automatic.ok -and !$automatic.snapshot.backendReloadRequired -and $script:Resets -eq 1 -and $script:Applied -eq 2 -and !$script:Delay.Count) 'Closed editor still allows exactly the two real backend corrections'
 Reset-ReloadEditorFixture 'explicit'
 $manual=Invoke-AppearanceControl 'Reload'
 Assert ($manual.ok -and $manual.reloaded -and $script:Resets -eq 1 -and $script:Applied -eq 0 -and $script:Editing) 'Explicit reload retains its existing behavior and does not stop the open editor'
 Assert ($script:Starts -eq 0) 'Editor deferral and recovery perform no process starts'
}}

if($Case -eq 'PrepareEditor'){& {
 $baseRoot=$script:ThemeRoot
 $baseState=$script:PresetStateBase
 $script:PrepareMergeOriginal=${function:Merge-PresetOriginalBackup}
 $writeOriginal=${function:Write-PresetBytes}
 $rpcOriginal=${function:Invoke-LoaderRpc}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($Method -ceq 'fetch_theme_path' -and $ControlAction -ceq 'Prepare' -and !$script:PrepareInjected){
   if($script:PrepareScenario -ceq 'config-before-prepare'){
    [IO.File]::WriteAllText($script:PrepareManaged,$script:PrepareManagedEdit)
    $script:PrepareInjected=$true
   }elseif($script:PrepareScenario -ceq 'target-before-prepare'){
    $script:PrepareInjected=$true
    return $script:PrepareOtherTarget
   }elseif($script:PrepareScenario -ceq 'drift-cleared'){
    [IO.File]::WriteAllText($script:PrepareForeign,'{"active":false,"Tone":"initial"}')
    $script:PrepareInjected=$true
   }
  }
  & $rpcOriginal $Method $Arguments
 }
 function Merge-PresetOriginalBackup([string]$OriginalDirectory,$Transaction){
  $merged=& $script:PrepareMergeOriginal $OriginalDirectory $Transaction
  $script:PrepareMerges++
  if($script:PrepareScenario -eq 'opens-after-merge'){
   $script:Editing=$true
   [IO.File]::WriteAllText($script:PrepareForeign,$script:PrepareUserEdit)
  }elseif($script:PrepareScenario -ceq 'config-after-merge'){
   [IO.File]::WriteAllText($script:PrepareManaged,$script:PrepareManagedEdit)
   $script:PrepareInjected=$true
  }
  $merged
 }
 function Write-PresetBytes([string]$Path,[byte[]]$Bytes){$script:PrepareWrites++;& $writeOriginal $Path $Bytes}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Start-PrepareScenario([string]$Scenario){
  $script:PrepareScenario='';$script:PrepareInjected=$false
  $script:ThemeRoot=Join-Path $baseRoot $Scenario
  $script:PresetStateBase=Join-Path $baseState $Scenario
  $script:Live=$false;$script:Editing=$false
  Invoke-PresetPrepare|Out-Null;Complete-PresetReload
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $script:Live=$true;$script:Rendered=$true;$script:MissingStyles=@();$script:Delay=@()
  $f=Join-Path $script:ThemeRoot 'Fixture Foreign';New-Item -ItemType Directory -Path $f|Out-Null
  $script:PrepareForeign=Join-Path $f 'config_USER.json'
  $script:PrepareOriginal='{"active":true,"Tone":"initial"}'
  $script:PrepareUserEdit='{"active":true,"Tone":"edit-during-merge"}'
  $script:PrepareManaged=Join-Path $script:ThemeRoot 'Round\config_USER.json'
  $managed=[IO.File]::ReadAllText($script:PrepareManaged)|ConvertFrom-Json;$managed.active=$false
  $script:PrepareManagedEdit=$managed|ConvertTo-Json -Compress
  $script:PrepareOtherTarget=Join-Path $script:ThemeRoot 'another-target'
  [IO.File]::WriteAllText($script:PrepareForeign,$script:PrepareOriginal)
  [IO.File]::WriteAllText((Join-Path $f 'theme.json'),'{"name":"Fixture Foreign","manifest_version":1,"inject":{}}')
  $script:PrepareScenario=$Scenario;$script:Editing=$Scenario -in @('already-open','explicit')
  $script:PrepareWrites=0;$script:PrepareMerges=0;$script:Resets=0;$script:Applied=0;$script:RenderSamples=0
 }
 $failures=New-Object Collections.Generic.List[string]
 foreach($scenario in 'already-open','opens-after-merge'){
  Start-PrepareScenario $scenario
  $owner=Join-Path (Get-PresetStateDir) 'owner.json';$ownerHash=Get-ConfigHash $owner
  $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
  $after=[IO.File]::ReadAllText($script:PrepareForeign)
  $expected=if($scenario -eq 'already-open'){$script:PrepareOriginal}else{$script:PrepareUserEdit}
  $expectedMerges=[int]($scenario -eq 'opens-after-merge')
  $label="$scenario defers automatic preparation (reason=$failure, writes=$($script:PrepareWrites), merges=$($script:PrepareMerges), reset=$($script:Resets), config=$after)"
  if($failure -like 'CSSLoader Desktop is open*' -and $after -ceq $expected -and (Get-ConfigHash $owner) -ceq $ownerHash -and !(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json')) -and $script:PrepareWrites -eq 0 -and $script:PrepareMerges -eq $expectedMerges -and $script:Resets -eq 0 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 0){Assert $true $label}else{$failures.Add($label)}
 }
 if($failures.Count){throw ($failures -join '; ')}
 $scopeResults=New-Object Collections.Generic.List[object]
 foreach($scenario in 'config-before-prepare','target-before-prepare','config-after-merge','drift-cleared'){
  Start-PrepareScenario $scenario
  $owner=Join-Path (Get-PresetStateDir) 'owner.json';$ownerHash=Get-ConfigHash $owner
  $journal=Join-Path (Get-PresetStateDir) 'transaction.json'
  $beforeManaged=[IO.File]::ReadAllText($script:PrepareManaged)
  $failure='';$automatic=$null;try{$automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $configPreserved=[IO.File]::ReadAllText($script:PrepareManaged) -ceq $(if($scenario.StartsWith('config-')){$script:PrepareManagedEdit}else{$beforeManaged})
  $foreignPreserved=[IO.File]::ReadAllText($script:PrepareForeign) -ceq $(if($scenario -ceq 'drift-cleared'){'{"active":false,"Tone":"initial"}'}else{$script:PrepareOriginal})
  $ownerPreserved=(Get-ConfigHash $owner) -ceq $ownerHash
  $noJournal=!(Test-Path -LiteralPath $journal)
  $otherUntouched=!(Test-Path -LiteralPath $script:PrepareOtherTarget)
  $expectedMerges=[int]($scenario -ceq 'config-after-merge')
  $rightEnd=if($scenario -ceq 'drift-cleared'){$automatic.ok -and !$failure -and $script:RenderSamples -gt 0}
    elseif($scenario -ceq 'target-before-prepare'){$failure -like 'Appearance target changed*'}
    else{$failure -like 'Appearance changed * automatic preparation*'}
  $ok=$script:PrepareInjected -and $rightEnd -and $configPreserved -and $foreignPreserved -and $ownerPreserved -and $noJournal -and
    $otherUntouched -and !$script:PrepareWrites -and !$script:Resets -and !$script:Applied -and !$script:Delay.Count -and $script:PrepareMerges -eq $expectedMerges
  $scopeResults.Add([pscustomobject]@{scenario=$scenario;ok=[bool]$ok;failure=$failure;injected=$script:PrepareInjected;managedPreserved=$configPreserved;
    foreignPreserved=$foreignPreserved;ownerPreserved=$ownerPreserved;noJournal=$noJournal;otherUntouched=$otherUntouched;writes=$script:PrepareWrites;
    merges=$script:PrepareMerges;resets=$script:Resets;sets=$script:Applied;waits=$script:Delay.Count})
 }
 $scopeResults.ToArray()|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Out 'prepare-scope.json') -Encoding UTF8
 foreach($scope in $scopeResults){Assert $scope.ok ('Automatic prepare must preserve newer scope or skip cleared drift: '+$scope.scenario)}
 Start-PrepareScenario 'closed'
 $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
 $final=Invoke-AppearanceControl 'Idle'
 $foreign=[IO.File]::ReadAllText($script:PrepareForeign)|ConvertFrom-Json
 Assert ($automatic.ok -and $final.ok -and !$foreign.active -and $foreign.Tone -eq 'initial' -and $script:Resets -eq 1 -and $script:Delay.Count -eq 0) 'Closed editor allows normal automatic preparation, reload and final DOM confirmation'
 Start-PrepareScenario 'explicit'
 $manual=Invoke-AppearanceControl 'Prepare'
 Assert ($manual.ok -and $script:Editing -and $script:PrepareWrites -eq 1 -and $script:Resets -eq 0 -and !([IO.File]::ReadAllText($script:PrepareForeign)|ConvertFrom-Json).active) 'Explicit Prepare retains the authorized reversible takeover behavior'
 Assert ($script:Starts -eq 0) 'Automatic and explicit preparation start no Steam or CSS processes'
}}

if($Case -eq 'ReloadRevision'){& {
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true;$script:Rendered=$true;$script:MissingStyles=@()
 $config=Join-Path $script:ThemeRoot 'Round\config_USER.json';$configBytes=[IO.File]::ReadAllBytes($config)
 $css=Join-Path $script:ThemeRoot 'Round\shared.css';$cssBytes=[IO.File]::ReadAllBytes($css)
 $script:ReloadBaseStatus=${function:Get-AppearanceStatus}
 $script:ReloadBaseUpdate=${function:Invoke-OwnedPresetResourceUpdate}
 function Write-ReloadFixtureEdit {
  [IO.File]::WriteAllText($config,'{"active":false,"Intensity":"30px"}')
  $script:ReloadEditHash=Get-ConfigHash $config
 }
 function Get-AppearanceStatus {
  $snapshot=& $script:ReloadBaseStatus
  $script:ReloadStatusReads++
  if(($script:ReloadRace -eq 'between-controls' -and $script:ReloadStatusReads -eq 1) -or
     ($script:ReloadRace -eq 'inside-reload' -and $script:ReloadStatusReads -eq 2)){Write-ReloadFixtureEdit}
  $snapshot
 }
 function Invoke-OwnedPresetResourceUpdate($Current){
  $next=& $script:ReloadBaseUpdate $Current
  if($script:ReloadRace -eq 'after-resource-update' -and $Current.packRepairRequired -and $next.packPrepared){Write-ReloadFixtureEdit}
  $next
 }
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Set-ReloadRevisionScenario([string]$Scenario){
  [IO.File]::WriteAllBytes($config,$configBytes);[IO.File]::WriteAllBytes($css,$cssBytes)
  $script:ReloadRace=$Scenario;$script:ReloadStatusReads=0;$script:ReloadEditHash=''
  $script:Resets=0;$script:Applied=0;$script:Delay=@()
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $owner=Read-PresetState;$owner.pendingReload=$true;Save-PresetState $owner
  if($Scenario -eq 'after-resource-update'){[IO.File]::AppendAllText($css,'/* fixture drift */')}
 }
 $failures=New-Object Collections.Generic.List[string]
 try{
  foreach($scenario in 'between-controls','inside-reload','after-resource-update'){
   Set-ReloadRevisionScenario $scenario
   $failure='';try{Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources|Out-Null}catch{$failure=$_.Exception.Message}
   $label="$scenario retains newer settings without a stale reload (reason=$failure, resets=$($script:Resets), sets=$($script:Applied))"
   if($failure -like 'Theme settings or target changed*' -and $script:ReloadEditHash -and (Get-ConfigHash $config) -ceq $script:ReloadEditHash -and (Read-PresetState).pendingReload -and $script:Resets -eq 0 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 0){Assert $true $label}else{$failures.Add($label)}
  }
  if($failures.Count){throw ($failures -join '; ')}
  Set-ReloadRevisionScenario 'unchanged'
  $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
  $final=Invoke-AppearanceControl 'Idle'
  Assert ($automatic.ok -and $final.ok -and $final.resident -and !$final.snapshot.backendReloadRequired -and $script:Resets -eq 1 -and $script:Applied -eq 0 -and $script:Delay.Count -eq 0) 'Unchanged automatic scope still reloads once and confirms final DOM'
  Assert ($script:Starts -eq 0) 'No reload scenario starts any process'
 }finally{
  [IO.File]::WriteAllBytes($config,$configBytes);[IO.File]::WriteAllBytes($css,$cssBytes)
 }
}}

if($Case -eq 'ReloadCoalesced'){& {
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true;$script:Rendered=$true;$script:MissingStyles=@()
 $script:CoalescedOriginalStatus=${function:Get-AppearanceStatus}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 function Get-AppearanceStatus {
  $status=& $script:CoalescedOriginalStatus
  $script:CoalescedReads++
  if($script:CoalescedReads -eq 1 -and $script:CoalescedScenario -ne 'explicit'){
   if(!$status.backendReloadRequired){throw 'Fixture did not observe a pending reload'}
   Complete-PresetReload
   if($script:CoalescedScenario -eq 'backend-drift'){($script:Rows|Where-Object name -eq 'Round').enabled=$false}
  }
  $status
 }
 function Reset-CoalescedFixture([string]$Scenario){
  $script:CoalescedScenario=$Scenario;$script:CoalescedReads=0;$script:Resets=0;$script:Applied=0;$script:Delay=@();$script:RenderSamples=0
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $owner=Read-PresetState;$owner.pendingReload=$Scenario -ne 'explicit';Save-PresetState $owner
 }
 foreach($scenario in 'matched','backend-drift'){
  Reset-CoalescedFixture $scenario
  $revision=Get-ThemeRevision @(Get-ThemeRows -RevisionOnly)
  $automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources
  $final=Invoke-AppearanceControl 'Idle'
  $expectedWrites=[int]($scenario -eq 'backend-drift')
  Assert ($automatic.ok -and $final.ok -and $final.resident -and $script:AppearanceHelperFlags -eq 31 -and $script:RenderSamples -ge 2 -and $script:Resets -eq 0 -and $script:Applied -eq $expectedWrites -and $script:Delay.Count -eq 0) ("${scenario}: no redundant global reset, targeted writes only for actual drift, independent final DOM (resets=$($script:Resets), sets=$($script:Applied))")
  Assert ((Get-ThemeRevision @(Get-ThemeRows -RevisionOnly)) -ceq $revision -and !(Read-PresetState).pendingReload) 'Coalescing leaves the same seven-theme selection and completed marker in place'
  if($scenario -eq 'matched'){Assert ($script:CoalescedReads -eq 3 -and $script:RenderSamples -eq 2) 'Healthy coalescing stops after existing fresh reload status and independent final status and DOM'}
 }
 Reset-CoalescedFixture 'explicit'
 $manual=Invoke-AppearanceControl 'Reload'
 Assert ($manual.ok -and $manual.reloaded -and $script:Resets -eq 1 -and $script:Applied -eq 0) 'Manual Reload remains an explicit reread even when nothing is pending'
 Assert ($script:Starts -eq 0) 'Neither automatic coalescing nor manual reread launches any process'
}}

if($Case -eq 'OverdueSlots'){& {
 $source=${function:Invoke-AutomaticAppearanceRefresh}.ToString()
 $clockLeaf='$clock=[Diagnostics.Stopwatch]::StartNew()'
 Assert ([regex]::Matches($source,[regex]::Escape($clockLeaf)).Count -eq 1) 'Exactly one monotonic time source is substituted; the original retry logic runs unchanged'
 Set-Item Function:Invoke-AutomaticAppearanceRefresh ([scriptblock]::Create($source.Replace($clockLeaf,'$clock=$script:FakeRefreshClock')))
 function Start-Sleep {param([int]$Milliseconds)
  $script:FakeWaits.Add($Milliseconds)
  $script:FakeRefreshClock.Elapsed.TotalMilliseconds+=$Milliseconds
  if($scenario -ceq 'late-wake' -and $script:FakeWaits.Count -eq 1){$script:FakeRefreshClock.Elapsed.TotalMilliseconds=16000}
 }
 function Invoke-AppearanceControl([string]$ControlAction){
  if($ControlAction -cnotin @('Status','UpdateOwnedResources')){throw ('Unexpected retry side effect: '+$ControlAction)}
  $script:FakeActions.Add($ControlAction)
  $script:FakeReads.Add([int]$script:FakeRefreshClock.Elapsed.TotalMilliseconds)
  $attempt=$script:FakeReads.Count
  if($attempt -eq 1 -and $scenario -ceq 'slow-status'){$script:FakeRefreshClock.Elapsed.TotalMilliseconds=16000}
  $backend=$scenario -cin @('healthy','slow-dom','recovers-on-next-read','single-overdue')
  [pscustomobject]@{ok=$true;snapshot=[pscustomobject]@{backendReady=$backend;configuredMatchesBackend=$true;backendProcessCount=2;packPrepared=$true;revision=('a'*64);targetId=('b'*16)}}
 }
 function Get-AppearanceRenderedStatus($Session,$Status){
  $script:FakeRenders++
  if($script:FakeRenders -eq 1){
   if($scenario -ceq 'slow-dom'){$script:FakeRefreshClock.Elapsed.TotalMilliseconds=25000}
   if($scenario -ceq 'recovers-on-next-read'){$script:FakeRefreshClock.Elapsed.TotalMilliseconds=16000}
   if($scenario -ceq 'single-overdue'){$script:FakeRefreshClock.Elapsed.TotalMilliseconds=3500}
  }
  $ok=$scenario -ceq 'healthy' -or ($scenario -cin @('recovers-on-next-read','single-overdue') -and $script:FakeRenders -eq 2)
  [pscustomobject]@{checked=$ok;confirmed=$ok;reason='render-fixture-document-not-ready'}
 }
 $expected=[ordered]@{healthy='0';normal='0,2000,7000,15000,24000';'slow-status'='0,16000,24000';'late-wake'='0,16000,24000';'slow-dom'='0,25000';'recovers-on-next-read'='0,16000';'single-overdue'='0,3500'}
 $observed=@(foreach($scenario in $expected.Keys){
  $script:FakeRefreshClock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalMilliseconds=0}}
  $script:FakeReads=[Collections.Generic.List[int]]::new();$script:FakeWaits=[Collections.Generic.List[int]]::new();$script:FakeActions=[Collections.Generic.List[string]]::new();$script:FakeRenders=0
  $script:AppearanceTiming=$null;$script:AppearanceTimingClock=$null
  $result=$null;$failure='';try{$result=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$failure=$_.Exception.Message}
  $n=$script:AppearanceRenderAttempts
  [pscustomobject]@{scenario=$scenario;result=[bool]($result -and $result.ok);failureCode=$script:AppearanceFailureCode;reason=$failure;reads=$script:FakeReads.ToArray();waits=$script:FakeWaits.ToArray();actions=$script:FakeActions.ToArray();renders=$script:FakeRenders;trace=$script:AppearanceRenderTrace[0..(2*$n-1)];attempts=$n}
 })
 $observed|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'overdue-observations.json') -Encoding UTF8
 foreach($row in $observed){
  Assert (($row.reads -join ',') -ceq $expected[$row.scenario]) ('Actual observations follow the finite schedule without replaying expired slots: '+$row.scenario)
  $ok=$row.scenario -cin @('healthy','recovers-on-next-read','single-overdue')
  $code=if($ok){0}elseif($row.scenario -ceq 'slow-dom'){6}else{3}
  Assert ($row.result -eq $ok -and $row.failureCode -eq $code -and $row.actions[0] -ceq 'UpdateOwnedResources' -and @($row.actions|Where-Object {$_ -ceq 'UpdateOwnedResources'}).Count -eq 1 -and $row.attempts -eq $row.reads.Count -and $row.trace.Count -eq 2*$row.reads.Count) ('Result and trace still describe exactly the work done: '+$row.scenario)
 }
 Assert (($observed[1].waits -join ',') -ceq '2000,5000,8000,9000' -and ($observed[3].waits -join ',') -ceq '2000,8000') 'Normal waits are unchanged and an oversleep leaves the same future deadline'
}}

if($Case -eq 'ReloadFinalScope'){& {
 $script:AppearanceContentManifestFile=Join-Path $Out 'content-index.json'
 Copy-Item -LiteralPath (Join-Path $script:PackRoot 'manifest.json') -Destination $script:AppearanceContentManifestFile
 $primary=Join-Path $script:ThemeRoot 'primary'
 $alternate=Join-Path $script:ThemeRoot 'alternate'
 foreach($dir in @($primary,$alternate)){
  $script:ThemeRoot=$dir
  Invoke-PresetPrepare|Out-Null
  Complete-PresetReload
 }
 $css=Join-Path $primary 'Round/shared.css'
 $config=Join-Path $primary 'Round/config_USER.json'
 $themeManifest=Join-Path $primary 'Round/theme.json'
 $foreign=Join-Path $primary 'Fixture Foreign'
 New-Item -ItemType Directory -Path $foreign -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $foreign 'theme.json'),'{"name":"Fixture Foreign","manifest_version":1,"inject":{}}')
 $foreignConfig=Join-Path $foreign 'config_USER.json'
 $original=[IO.File]::ReadAllBytes($css)
 $originalConfig=[IO.File]::ReadAllBytes($config)
 $originalManifest=[IO.File]::ReadAllBytes($themeManifest)
 $originalIndex=[IO.File]::ReadAllBytes($script:AppearanceContentManifestFile)
 $completeOriginal=${function:Complete-PresetReload}
 $statusOriginal=${function:Get-AppearanceStatus}
 $rowsOriginal=${function:Get-ThemeRows}
 $rpcOriginal=${function:Invoke-LoaderRpc}
 $readOriginal=${function:Read-AppearanceThemeConfig}
  $canonicalOriginal=${function:Get-AppearanceConfigValue}
  function Get-AppearanceConfigValue($Value,[int]$Depth=0){
   if($Depth -eq 0){$script:ReloadCanonicalCalls++}
   & $canonicalOriginal $Value $Depth
  }
 function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
  if($script:ReloadCheckClock -and $script:ReloadCheckClock.IsRunning){$script:ReloadDecoded++}
  & $readOriginal @PSBoundParameters
 }
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  $reply=& $rpcOriginal $Method $Arguments
  if($Method -ceq 'reset'){
   $script:ReloadCheckClock=[Diagnostics.Stopwatch]::StartNew()
   if($scenario -ceq 'reset-format-only'){
    [IO.File]::WriteAllText($config,("`n"+[IO.File]::ReadAllText($config)),[Text.UTF8Encoding]::new($false))
   }
   if($scenario -cin @('reset-resource-drift','reset-foreign-drift')){
    ($script:Rows|Where-Object name -eq 'Round').enabled=$false
    if($scenario -ceq 'reset-resource-drift'){[IO.File]::AppendAllText($css,'/* changed during reset */')}
    else{[IO.File]::WriteAllText($foreignConfig,'{"active":true}')}
   }
  }
  return $reply
 }
 function Get-ThemeRows([switch]$RevisionOnly,[hashtable]$AutomaticPlanMemo,[switch]$ValidateRenderOnly,[switch]$ReadOnlyProjection,[hashtable]$UnchangedConfigHashes){
  $decodedBefore=$script:ReloadDecoded
  $rows=@(& $rowsOriginal @PSBoundParameters)
  if($script:ReloadCheckClock -and $script:ReloadCheckClock.IsRunning){
   $script:ReloadPlanTrace.Add(@($rows|Where-Object {$null -ne $_.renderPlan}).Count)
   $script:ReloadConfigTrace.Add(@($rows|Where-Object {$null -ne $_.config}).Count)
   $script:ReloadDecodeTrace.Add($script:ReloadDecoded-$decodedBefore)
  }
  return $rows
 }
 function Get-AppearanceStatus([string]$ExpectedCorrectionRoot='',[ref]$CapturedRows,[switch]$RevisionOnly,[hashtable]$AutomaticPlanMemo){
  $status=& $statusOriginal @PSBoundParameters
  if($scenario -clike 'after-status-*' -and $script:ReloadCheckClock -and $script:ReloadCheckClock.IsRunning -and !$script:ReloadInjectedConfig){
   switch -CaseSensitive ($scenario){
    'after-status-config' {
     $cfg=[IO.File]::ReadAllText($config,[Text.Encoding]::UTF8)|ConvertFrom-Json;$cfg.active=$false
     Write-ThemeConfigAtomic $config ($cfg|ConvertTo-Json -Compress)
    }
    'after-status-format' {[IO.File]::AppendAllText($config,"`n")}
     'after-status-reordered' {[IO.File]::WriteAllText($config,'{"Custom":{"a":null,"z":[1,false,"x"]},"Outlines":"Round","Intensity":"10px","active":true}')}
     'after-status-options' {[IO.File]::WriteAllText($config,'{"active":true,"Intensity":"30px","Outlines":"Round"}')}
     'after-status-too-deep' {[IO.File]::WriteAllText($config,$deepInvalid)}
    'after-status-invalid' {[IO.File]::WriteAllText($config,'{"active":"true"}')}
    'after-status-locked' {$script:ReloadHeldConfig=[IO.File]::Open($config,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    'after-status-manifest' {[IO.File]::AppendAllText($themeManifest,"`n")}
    'after-status-index' {[IO.File]::AppendAllText($script:AppearanceContentManifestFile,"`n")}
   }
   $script:ReloadInjectedConfig=$true
  }
  return $status
 }
 function Complete-PresetReload {
  if($script:ReloadCheckClock){$script:ReloadCheckClock.Stop()}
  & $completeOriginal
  switch -CaseSensitive ($scenario){
   'late-drift' {[IO.File]::AppendAllText($css,'/* isolated concurrent resource edit */')}
   'late-target' {$script:ThemeRoot=$alternate}
  }
 }
 $script:Live=$true;$script:Rendered=$true;$script:MissingStyles=@()
  $nested='{"active":true,"Intensity":"10px","Outlines":"Round","Custom":{"z":[1,false,"x"],"a":null}}'
  $deepValid='{"active":true,"Intensity":"10px","Outlines":"Round","Custom":'+('['*63)+'0'+(']'*63)+'}'
  $deepInvalid='{"active":true,"Intensity":"10px","Outlines":"Round","Custom":'+('['*64)+'0'+(']'*64)+'}'
 $observed=@(foreach($channel in 'automatic','manual'){
   foreach($scenario in 'late-drift','late-target','unchanged','reset-format-only','after-status-config','after-status-format','after-status-invalid','after-status-locked','after-status-manifest','after-status-index','reset-resource-drift','reset-foreign-drift','after-status-reordered','after-status-options','after-status-too-deep','depth-limit','depth-overflow'){
   $script:ThemeRoot=$primary
   [IO.File]::WriteAllBytes($css,$original)
   [IO.File]::WriteAllBytes($config,$originalConfig)
    if($scenario -ceq 'after-status-reordered'){[IO.File]::WriteAllText($config,$nested)}
    if($scenario -ceq 'depth-limit'){[IO.File]::WriteAllText($config,$deepValid)}
    if($scenario -ceq 'depth-overflow'){[IO.File]::WriteAllText($config,$deepInvalid)}
   [IO.File]::WriteAllBytes($themeManifest,$originalManifest)
   [IO.File]::WriteAllBytes($script:AppearanceContentManifestFile,$originalIndex)
   [IO.File]::WriteAllText($foreignConfig,'{"active":false}')
   $owner=Read-PresetState;$owner.pendingReload=$true;Save-PresetState $owner
   $before=Get-AppearanceStatus
   $hash=Get-ConfigHash $config
   $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
   $script:Delay=@();$script:Resets=0;$script:Applied=0;$script:RenderSamples=0;$script:Starts=0;$script:AppearanceFailureCode=0
   $script:ReloadCheckClock=$null;$script:ReloadPlanTrace=New-Object 'Collections.Generic.List[int]';$script:ReloadConfigTrace=New-Object 'Collections.Generic.List[int]';$script:ReloadInjectedConfig=$false
   $script:ReloadDecodeTrace=New-Object 'Collections.Generic.List[int]';$script:ReloadDecoded=0
    $script:ReloadCanonicalCalls=0
   $result=$null;$failure=''
   try{$result=if($channel -ceq 'automatic'){Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}else{Invoke-AppearanceControl 'Reload'}}catch{$failure=$_.Exception.Message}
   finally{if($script:ReloadHeldConfig){$script:ReloadHeldConfig.Dispose();$script:ReloadHeldConfig=$null}}
   if($script:ReloadCheckClock){$script:ReloadCheckClock.Stop()}
   $after=Get-AppearanceStatus
   [pscustomobject]@{
    channel=$channel;scenario=$scenario;ok=[bool]($result -and $result.ok);code=$script:AppearanceFailureCode;reason=$failure
    sameTarget=$after.targetId -ceq $before.targetId;prepared=$after.packPrepared;repair=$after.packRepairRequired;flagsMatched=$after.configuredMatchesBackend
    pending=$after.backendReloadRequired;foreignCount=$after.foreignActiveCount
    renders=$script:RenderSamples;resets=$script:Resets;sets=$script:Applied;waits=$script:Delay.Count;starts=$script:Starts
    sameConfig=(Get-ConfigHash $config) -ceq $hash
    savedOff=($after.themes|Where-Object name -eq Round).configured -eq $false
     afterResetPlanRows=$script:ReloadPlanTrace.ToArray();afterResetParsedConfigs=$script:ReloadConfigTrace.ToArray();afterResetMs=$script:ReloadCheckClock.Elapsed.TotalMilliseconds
     canonicalCalls=$script:ReloadCanonicalCalls;option=(Get-Content -LiteralPath $config -Raw -ErrorAction Stop|ForEach-Object {try{($_|ConvertFrom-Json).Intensity}catch{}})
    afterResetDecodes=$script:ReloadDecodeTrace.ToArray()
    fullResultPlans=@($result.snapshot.themes|Where-Object {$_.renderPlan.contexts.MainMenu -and $_.renderPlan.contexts.QuickAccess}).Count
   }
  }
 })
 $observed|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'reload-final-scope.json') -Encoding UTF8
 foreach($item in $observed){
  $label=$item.channel+'/'+$item.scenario
   if($item.scenario -ceq 'depth-overflow'){
    Assert (!$item.ok -and $item.reason -eq 'Theme config is too deeply nested' -and $item.pending -and $item.sameConfig -and !$item.resets -and !$item.sets -and !$item.renders -and !$item.starts) ('Reject excessive nesting before sending reset: '+$label)
    continue
   }
  Assert ($item.resets -eq 1 -and !$item.sets -and !$item.waits -and !$item.starts) ('Only the authorized reset; no additional work: '+$label)
  if($item.scenario -cin @('reset-resource-drift','reset-foreign-drift')){
   Assert (($item.afterResetPlanRows -join ',') -ceq '0') ('Invalid preset stops after the existing first post-reset check: '+$label)
   Assert (($item.afterResetParsedConfigs -join ',') -ceq '0') ('Fresh resource or foreign drift is still seen when managed config bytes match: '+$label)
   Assert (!$item.ok -and $item.code -eq 3 -and !$item.renders -and $item.pending -and $item.sameConfig -and $item.sameTarget) ('Known drift retains the pending marker and configuration: '+$label)
   Assert ((($item.scenario -ceq 'reset-resource-drift') -and $item.repair -and !$item.prepared) -or
       (($item.scenario -ceq 'reset-foreign-drift') -and $item.prepared -and $item.foreignCount -eq 1)) ('Original preset fault observed without altering it: '+$label)
   continue
  }
  Assert (($item.afterResetPlanRows -join ',') -ceq '0,0') ('No discarded page plans in either fresh post-reset check: '+$label)
  $expectedProjection=[int]($item.scenario -ceq 'reset-format-only')
  if($item.scenario -cin @('late-drift','late-target','unchanged','reset-format-only','after-status-config','after-status-format')){
   $expectedLateParses=[int]($item.scenario -cin @('after-status-config','after-status-format','reset-format-only'))
   Assert (($item.afterResetParsedConfigs -join ',') -ceq "$expectedProjection,$expectedLateParses" -and ($item.afterResetDecodes -join ',') -ceq "$($expectedProjection*2),$($expectedLateParses*2)") ('Reopen all files but decode only changed config/manifest pairs: '+$label)
  }
  if($item.scenario -cnotlike 'after-status-*' -and $item.scenario -cne 'reset-format-only'){
   Assert ($item.flagsMatched -and $item.sameConfig) ('Backend state and user config are retained: '+$label)
  }
  switch -CaseSensitive ($item.scenario){
   'late-drift' {Assert ($item.repair -and !$item.prepared -and $item.sameTarget -and !$item.ok -and $item.code -eq 3 -and !$item.renders) ('Reject final resource drift before DOM work: '+$label)}
   'late-target' {Assert (!$item.sameTarget -and $item.prepared -and !$item.ok -and $item.code -eq 4 -and !$item.renders) ('Reject an otherwise healthy different target: '+$label)}
    {$_ -cin @('unchanged','depth-limit')} {Assert ($item.ok -and $item.sameTarget -and $item.prepared -and $item.fullResultPlans -eq 7 -and $item.renders -eq [int]($item.channel -ceq 'automatic')) ('Unchanged scope retains its full confirmation path: '+$label)}
   'reset-format-only' {Assert ($item.ok -and !$item.sameConfig -and $item.flagsMatched -and !$item.savedOff -and !$item.pending -and $item.fullResultPlans -eq 7 -and $item.renders -eq [int]($item.channel -ceq 'automatic')) ('Different JSON bytes with the same choices retain full confirmation: '+$label)}
   'after-status-format' {Assert ($item.ok -and !$item.sameConfig -and !$item.savedOff -and !$item.pending -and $item.fullResultPlans -eq 7) ('Formatting after the earlier snapshot remains allowed: '+$label)}
    'after-status-reordered' {Assert ($item.ok -and !$item.sameConfig -and !$item.savedOff -and !$item.pending -and $item.fullResultPlans -eq 7) ('Nested key order is not a change to user choices: '+$label)}
    'after-status-options' {Assert (!$item.ok -and $item.reason -like 'Theme settings changed during reload*' -and !$item.savedOff -and $item.option -ceq '30px' -and !$item.renders -and $item.pending) ('A later option change with matching flags is preserved: '+$label)}
    'after-status-too-deep' {Assert (!$item.ok -and $item.reason -eq 'Theme config is too deeply nested' -and !$item.renders -and $item.pending) ('Deep late input remains rejected: '+$label)}
   'after-status-config' {Assert (!$item.ok -and $item.reason -like 'Theme settings changed during reload*' -and $item.savedOff -and !$item.sameConfig -and !$item.renders -and $item.pending) ('A later user edit survives and blocks the reload result: '+$label)}
   {$_ -cin @('after-status-invalid','after-status-locked','after-status-manifest','after-status-index')} {
    Assert (!$item.ok -and $item.reason -like 'Theme settings changed during reload*' -and !$item.renders -and $item.pending) ('Fresh boundary rejects invalid, locked, or changed inputs before sync: '+$label)
   }
  }
 }
  foreach($item in $observed){
   $expected=if($item.scenario -cin @('reset-format-only','after-status-config','after-status-format','after-status-reordered','after-status-options','after-status-too-deep')){2}else{0}
   Assert ($item.canonicalCalls -eq $expected) ('Canonicalize only when bytes changed: '+$item.channel+'/'+$item.scenario)
  }
}}

if($Case -eq 'InvalidSelection'){
 $idleAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance/idle.ps1'),[ref]$tokens,[ref]$errors)
 foreach($fn in $idleAst.EndBlock.Statements|Where-Object {$_.Name -ceq 'Get-AppearanceRenderedStatus'}){Invoke-Expression $fn.Extent.Text}
 function Get-AppearanceSteamSession {[pscustomobject]@{pid=123;birth='456';root='C:\fixture-steam';processEpoch='1';accountEpoch='1';powerGeneration='1'}}
 Invoke-PresetPrepare|Out-Null
 $script:Live=$true
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 Complete-PresetReload
 $status=Get-AppearanceStatus
 $plan=@(Get-AppearanceRenderExpectation $status 'SP')
 $values=@(foreach($item in $plan){$item.value})
 $counts=@(foreach($item in $plan){if($item.kind -ceq 'file'){[int]$item.enabled}})
 $healthy=@{protocol=1;sequential=$true;complete=$false;documents=@(@{id='fixture-page';kind='SP';value=@{protocol=4;documentKey='fixture';ready=$true;styles=12;values=$values;fileCounts=$counts}})}|ConvertTo-Json -Compress -Depth 10
 $path=Join-Path $script:ThemeRoot 'Round/config_USER.json'
 $original=[IO.File]::ReadAllText($path)
 $retired=$original|ConvertFrom-Json;$retired.Intensity='no-longer-shipped'
 $retiredText=$retired|ConvertTo-Json -Compress
 function Invoke-AppearanceSharedSteam($request){
  if($request.operation -cne 'documents'){throw 'Unexpected fixture request'}
  $script:RenderSamples++
  if($script:TransientUnreadable -gt 0){
   if($kind -ceq 'invalid-on-next-slot'){[IO.File]::WriteAllText($path,$retiredText);$script:ExpectedConfigHash=(Get-FileHash -LiteralPath $path).Hash}
   $script:TransientUnreadable--;throw 'render-shared-steam-document-main-unavailable'
  }
  $healthy|ConvertFrom-Json
 }
 $observed=@(foreach($kind in 'retired-option','wrong-type','transient-document','invalid-on-next-slot','healthy'){
  $changed=$original|ConvertFrom-Json
  switch($kind){'retired-option' {$changed.Intensity='no-longer-shipped'}; 'wrong-type' {$changed.Intensity=42}}
  $text=if($kind -in @('retired-option','wrong-type')){$changed|ConvertTo-Json -Compress}else{$original}
  [IO.File]::WriteAllText($path,$text)
  $script:ExpectedConfigHash=(Get-FileHash -LiteralPath $path).Hash
  $script:TransientUnreadable=[int]($kind -cin @('transient-document','invalid-on-next-slot'))
  $script:Delay=@();$script:Applied=0;$script:Starts=0;$script:Resets=0;$script:RenderSamples=0
  $result=$null;$message='';try{$result=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}catch{$message=$_.Exception.Message}
  [pscustomobject]@{id=$kind;ok=[bool]($result -and $result.ok);reason=$message;failure=$script:AppearanceFailureCode;observations=$script:AppearanceRenderAttempts;
   waitCount=$script:Delay.Count;waits=@($script:Delay|ForEach-Object {$_.milliseconds});documentReads=$script:RenderSamples;writes=$script:Applied;resets=$script:Resets;starts=$script:Starts;
   sameConfig=((Get-FileHash -LiteralPath $path).Hash -ceq $script:ExpectedConfigHash)}
 })
 $observed|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'invalid-selection-observations.json') -Encoding UTF8
 foreach($entry in $observed){Assert ($entry.sameConfig -and !$entry.writes -and !$entry.resets -and !$entry.starts) ('No config writes, resets or launches: '+$entry.id)}
 foreach($entry in $observed[0,1,3]){
  $prior=[int]($entry.id -ceq 'invalid-on-next-slot')
  Assert (!$entry.ok -and $entry.failure -eq 6 -and $entry.reason -ceq 'Automatic appearance contains an unsupported theme option; configuration retained' -and
   $entry.observations -eq (1+$prior) -and $entry.waitCount -eq $prior -and $entry.documentReads -eq $prior) ('Unsupported saved option fails in the slot that first reads it: '+$entry.id)
 }
 Assert ($observed[2].ok -and $observed[2].observations -eq 2 -and $observed[2].waitCount -eq 1 -and $observed[2].documentReads -eq 2) 'A missing page with valid configuration still uses the original next slot'
 Assert ($observed[4].ok -and $observed[4].observations -eq 1 -and !$observed[4].waitCount -and $observed[4].documentReads -eq 1) 'A repaired configuration still completes with one page query'
}
if($Case -eq 'EnsureRevisionOnly'){& {
 New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
 foreach($dir in Get-ChildItem -LiteralPath (Join-Path $ComponentRoot 'steam-appearance/themes') -Directory){Copy-Item -LiteralPath $dir.FullName -Destination $script:ThemeRoot -Recurse}
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload
 $script:EnsureOriginalPlan=${function:Get-AppearanceTargetRenderPlan}
 $script:EnsureOriginalUpdate=${function:Invoke-OwnedPresetResourceUpdate}
 $script:EnsurePlans=0;$script:EnsureSnapshot=$null
 function Get-AppearanceTargetRenderPlan($Name,$Manifest,$Config,$ContentIndex,$Target,$FileReceipts,$Defaults,[switch]$ValidateOnly){
  $script:EnsurePlans++;& $script:EnsureOriginalPlan @PSBoundParameters
 }
 function Invoke-OwnedPresetResourceUpdate($Current){$script:EnsureSnapshot=$Current;& $script:EnsureOriginalUpdate $Current}
 function Start-AppearanceBackend {$script:Starts++;[pscustomobject]@{ok=$true;boundary='inert-launch-port'}}
 $script:Live=$false;$script:Starts=0
 $script:AppearanceContentIndex=$null;$script:AppearanceContentIndexStamp='';$script:PresetResourceHashes=@{}
 $timer=[Diagnostics.Stopwatch]::StartNew();$result=Invoke-AppearanceControl 'Ensure';$timer.Stop()
 $ensurePlans=$script:EnsurePlans;$snapshot=$script:EnsureSnapshot
 [pscustomobject]@{elapsedMs=$timer.Elapsed.TotalMilliseconds;renderPlans=$ensurePlans;backendReady=$snapshot.backendReady;prepared=$snapshot.packPrepared;restorable=$snapshot.canRestore;launchPortCalls=$script:Starts}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'ensure-cost.json') -Encoding UTF8
 Assert ($result.ok -and !$snapshot.backendReady -and $snapshot.packPrepared -and !$snapshot.canRestore -and $script:Starts -eq 1) 'Cold Ensure checks prepared resources without claiming the deferred backup is restorable'
 Assert ($ensurePlans -eq 0 -and @($snapshot.themes|Where-Object {$null -ne $_.renderPlan}).Count -eq 0) 'Cold Ensure does not build discarded page plans'
 $script:EnsurePlans=0;$full=Invoke-AppearanceControl 'Status';$fullSnapshot=$full.snapshot
 Assert ($full.ok -and $script:EnsurePlans -eq 7 -and (Test-FixtureThemePagePlans $fullSnapshot.themes)) 'The full status builds all 21 independent page plans in seven theme traversals'
 Assert ($fullSnapshot.revision -ceq $snapshot.revision -and $fullSnapshot.targetId -ceq $snapshot.targetId -and $fullSnapshot.packPrepared -eq $snapshot.packPrepared -and $fullSnapshot.canRestore) 'Independent full status still verifies the backup and agrees on settings, target, and prepared resources'
 $round=Join-Path $script:ThemeRoot 'Round\config_USER.json'
 $cfg=[IO.File]::ReadAllText($round)|ConvertFrom-Json;$cfg.Intensity='unsupported-ensure-fixture-choice';Write-ThemeConfigAtomic $round ($cfg|ConvertTo-Json -Compress)
 $result=Invoke-AppearanceControl 'Ensure';$bad=Invoke-AppearanceControl 'Status'
 Assert ($result.ok -and $script:EnsureSnapshot.revision -cne $snapshot.revision -and $script:EnsureSnapshot.revision -ceq $bad.snapshot.revision) 'Fresh config changes are not hidden by revision-only Ensure'
 Assert (($bad.snapshot.themes|Where-Object name -eq Round).error -ceq 'render-patch-selection-invalid' -and $script:Applied -eq 0 -and $script:Resets -eq 0) 'Display checks still reject invalid choices without editing the config or reloading CSS'
}}
if($Case -eq 'BackupStatusCost'){
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 # Exercise complete production Automatic -> final Idle with an original backup
 # containing the actual seven-theme resource payload. Transport leaves are inert.
 New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
 foreach($dir in Get-ChildItem -LiteralPath (Join-Path $ComponentRoot 'steam-appearance/themes') -Directory){Copy-Item -LiteralPath $dir.FullName -Destination $script:ThemeRoot -Recurse}
 Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
 $script:Rendered=$true;$script:MissingStyles=@();$script:Applied=0;$script:Resets=0;$script:Starts=0
 $owner=Read-PresetState;$backup=Read-PresetBackup $owner.originalBackup;$blobCount=@($backup.journal.entries|Where-Object existed).Count
 $script:PresetResourceHashes=@{} # Equal first-use resource validation for both arms.
 $handlesBefore=[Diagnostics.Process]::GetCurrentProcess().HandleCount
 $memo=@{}
 $timer=[Diagnostics.Stopwatch]::StartNew();$automatic=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo;$automaticMs=$timer.Elapsed.TotalMilliseconds;$idle=Invoke-AutomaticAppearanceIdle $automatic.snapshot -AutomaticPlanMemo $memo;$timer.Stop()
 $handlesAfter=[Diagnostics.Process]::GetCurrentProcess().HandleCount
 Assert ($automatic.ok -and $idle.ok -and $idle.resident -and $script:AppearanceHelperFlags -eq 31) 'Full healthy Automatic and final Idle confirm all resident flags'
 Assert ($script:Applied -eq 0 -and $script:Resets -eq 0 -and $script:Starts -eq 0 -and $script:Delay.Count -eq 0) 'Healthy status pipeline adds no theme write/process/reset/wait'
 Assert ($blobCount -ge 14 -and @($idle.snapshot.themes|Where-Object configured).Count -eq 7) 'Cost scope includes a real populated original backup and seven enabled themes'
 Assert (!$automatic.snapshot.canRestore -and $idle.snapshot.canRestore -and $script:RenderSamples -eq 2) 'First observation does not claim restore readiness and final Idle independently confirms the backup and page'
 [pscustomobject]@{elapsedMs=$timer.Elapsed.TotalMilliseconds;automaticMs=$automaticMs;idleMs=($timer.Elapsed.TotalMilliseconds-$automaticMs);blobCount=$blobCount;renderReads=$script:RenderSamples;handlesBefore=$handlesBefore;handlesAfter=$handlesAfter;backendFlags=31;fullAutomaticAndIdle=$true;externalIO='explicit inert Steam/CSS transport leaves'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'cost.json') -Encoding UTF8
 $blob=Join-Path $owner.originalBackup ('files/'+(@($backup.journal.entries|Where-Object existed)[0]).blob)
 $bytes=[IO.File]::ReadAllBytes($blob);[byte[]]$bad=$bytes.Clone();$bad[0]=$bad[0] -bxor 1
 try{
  $script:RenderSamples=0;$memo=@{}
  $observed=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo
  [IO.File]::WriteAllBytes($blob,$bad)
  Assert ((Reject {Invoke-AutomaticAppearanceIdle $observed.snapshot -AutomaticPlanMemo $memo}) -and $script:RenderSamples -eq 1 -and $script:AppearanceFailureCode -eq 3 -and $script:AppearanceHelperFlags -ne 31) 'Final Idle refuses backup bytes changed since the preliminary page read'
  $script:RenderSamples=0;$script:MissingStyles=@('Round');$memo=@{}
  Assert ((Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo}) -and $script:RenderSamples -eq 1 -and !$script:Applied -and !$script:Resets -and !$script:Starts) 'Automatic correction may observe the page, but never writes with an invalid current backup'
 }finally{[IO.File]::WriteAllBytes($blob,$bytes)}

 # Cross the 2s offset during correction without a real five-slot wait.
 # Control, status, preset and final Idle still use production code.
 $clockLeaf='$clock=[Diagnostics.Stopwatch]::StartNew()'
 $refresh=${function:Invoke-AutomaticAppearanceRefresh}.ToString()
 Assert ([regex]::Matches($refresh,[regex]::Escape($clockLeaf)).Count -eq 1) 'Correction cost fixture replaces only the refresh clock'
 Set-Item Function:Invoke-AutomaticAppearanceRefresh ([scriptblock]::Create($refresh.Replace($clockLeaf,'$clock=$script:CorrectionCostClock')))
 $readBackup=${function:Read-PresetBackup}
 function Read-PresetBackup([string]$Directory){
  $script:CorrectionCostBackups++
  & $readBackup $Directory
 }
 function Start-Sleep {param([int]$Milliseconds)
  $script:Delay+=$Milliseconds
  $script:CorrectionCostClock.Elapsed.TotalMilliseconds+=$Milliseconds
 }
 function Get-AppearanceRenderedStatus($Session,$Status,[hashtable]$AutomaticPlanMemo){
  $null=Get-AppearanceDocumentPlan $Status (Get-AppearanceContentIndex) $AutomaticPlanMemo
  $script:RenderSamples++
  if($script:RenderSamples -eq 2){$script:CorrectionCostClock.Elapsed.TotalMilliseconds=1940}
  if($script:RenderSamples -eq 3){$script:CorrectionCostClock.Elapsed.TotalMilliseconds=2260}
  $script:CorrectionCostTimeline.Add([int]$script:CorrectionCostClock.Elapsed.TotalMilliseconds)
  $script:CorrectionCostBackupTrace.Add([int]$script:CorrectionCostBackups)
  # Alter the restore bytes after the post-write status read.
  if($script:RenderSamples -eq 3 -and $scenario -ceq 'corrupt-backup'){[IO.File]::WriteAllBytes($blob,$bad)}
  if($script:RenderSamples -eq 4 -and $scenario -ceq 'desktop'){$script:Editing=$true}
  $readyAt=if($scenario -ceq 'later-style'){5}else{4}
  $ready=$script:Applied -gt 0 -and $script:RenderSamples -ge $readyAt -and $scenario -cne 'never-ready'
  [pscustomobject]@{checked=$true;confirmed=$ready;reason='fixture-receipts';mismatched=@(if(!$ready){'Round'})}
 }
 $costProcess=[Diagnostics.Process]::GetCurrentProcess()
 try{
  $observations=@(foreach($scenario in 'immediate-style','later-style','never-ready','corrupt-backup','desktop'){
   $script:CorrectionCostBackups=0;$script:CorrectionCostBackupTrace=[Collections.Generic.List[int]]::new()
   $script:CorrectionCostTimeline=[Collections.Generic.List[int]]::new()
   $script:CorrectionCostClock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalMilliseconds=0}}
   $script:Applied=0;$script:RenderSamples=0;$script:Delay=@();$script:Editing=$false
   $script:AppearanceObservationOnly=$false;$script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0
   $result=$null;$final=$null;$memo=@{}
   $costProcess.Refresh();$cpuStart=$costProcess.TotalProcessorTime
   $watch=[Diagnostics.Stopwatch]::StartNew()
   $refreshTime=-1;$message=''
   try{
    $result=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo
    $refreshTime=$watch.Elapsed.TotalMilliseconds
    $final=Invoke-AutomaticAppearanceIdle $result.snapshot -AutomaticPlanMemo $memo
   }catch{$message=$_.Exception.Message}
   $watch.Stop();$costProcess.Refresh();$cpu=($costProcess.TotalProcessorTime-$cpuStart).TotalMilliseconds
   [pscustomobject]@{scenario=$scenario;render=[bool]$result.ok;final=[bool]$final.ok;writes=$script:Applied;resets=$script:Resets;starts=$script:Starts;code=$script:AppearanceFailureCode;message=$message;
    reads=$script:CorrectionCostTimeline.ToArray();backupTrace=$script:CorrectionCostBackupTrace.ToArray();backupChecks=$script:CorrectionCostBackups;backupBlobs=$blobCount;
    waits=$script:Delay;refreshMs=$refreshTime;totalMs=$watch.Elapsed.TotalMilliseconds;cpuMs=$cpu}
   if($scenario -ceq 'corrupt-backup'){[IO.File]::WriteAllBytes($blob,$bytes)}
  })
 }finally{$costProcess.Dispose();[IO.File]::WriteAllBytes($blob,$bytes);$script:Editing=$false}
 ConvertTo-Json -InputObject $observations -Depth 6 | Set-Content -LiteralPath (Join-Path $Out 'post-correction-backup-cost.json') -Encoding UTF8
 $instant,$later,$never,$corrupt,$editor=$observations
 Assert (@($observations|Where-Object {$_.writes -ne 1 -or $_.resets -or $_.starts}).Count -eq 0) 'No correction retry can write twice, reset the backend or start a process'
 Assert ($instant.final -and ($instant.reads -join ',') -ceq '0,1940,2260,2260,2260' -and !$instant.waits.Count) 'When styles arrive just after the write, the overdue check and independent final Idle confirm without waiting'
 Assert ($later.final -and ($later.reads -join ',') -ceq '0,1940,2260,2260,7000,7000' -and ($later.waits -join ',') -ceq '4740') 'When styles arrive later, original retry offsets and independent final Idle are retained'
 Assert (!$never.final -and $never.code -eq 6 -and ($never.reads -join ',') -ceq '0,1940,2260,2260,7000,15000,24000') 'Persistent DOM mismatch retains the last scheduled page read'
 Assert ($instant.backupTrace[2] -gt 0 -and $instant.backupTrace[3] -eq $instant.backupTrace[2] -and $instant.backupTrace[4] -eq ($instant.backupTrace[3]+1)) 'Backup is checked at correction and final Idle, not at the immediate read-only retry'
 Assert ($later.backupTrace[4] -eq $later.backupTrace[2] -and $later.backupTrace[5] -eq ($later.backupTrace[4]+1) -and $never.backupTrace[-1] -eq ($never.backupTrace[-2]+1)) 'Additional intermediate retries defer the backup, but the last slot still checks it'
 Assert ($corrupt.render -and !$corrupt.final -and $corrupt.code -eq 3 -and $corrupt.reads.Count -eq 4 -and $corrupt.backupChecks -eq ($corrupt.backupTrace[-1]+1)) 'Final Idle rejects corrupted restore bytes even after an intermediate page match'
 Assert ($editor.render -and !$editor.final -and $editor.code -eq 15 -and $editor.reads.Count -eq 4 -and $editor.backupChecks -eq $editor.backupTrace[-1]) 'Final Idle still yields immediately to CSSLoader Desktop'
}

if($Case -eq 'BundleProjection'){& {
 $originalPack=$script:PackRoot
 $script:PackRoot=Join-Path $Out 'pack'
 Copy-Item -LiteralPath $originalPack -Destination $script:PackRoot -Recurse
 $script:AppearanceContentManifestFile=Join-Path $script:PackRoot 'manifest.json'
 $script:AppearanceContentIndex=$null;$script:AppearanceContentIndexStamp=''
 $null=New-Item -ItemType Directory -Path $script:ThemeRoot -Force
 foreach($dir in Get-ChildItem -LiteralPath (Join-Path $script:PackRoot 'themes') -Directory){Copy-Item -LiteralPath $dir.FullName -Destination $script:ThemeRoot -Recurse}
 Invoke-PresetPrepare|Out-Null;Complete-PresetReload
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1 -or $Processes[0].Id -ne 42){throw 'OWNED_BACKEND_FAMILY_REQUIRED'}}
 $resource=${function:Get-PresetResourceHash}
 function Get-PresetResourceHash([string]$Path,[string]$InvalidFileMessage='',[switch]$InstalledTarget,[ref]$Raw,[switch]$SchemaDigestOnly){
  if($InstalledTarget){$script:ProjectionInstalled++}else{$script:ProjectionBundled++}
  & $resource @PSBoundParameters
 }
 $script:Live=$true;$script:Rendered=$true;$script:KeepMissing=$false;$script:MissingStyles=@()
 $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
 $script:ProjectionBundled=0;$script:ProjectionInstalled=0;$memo=@{}
 $observed=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $memo
 $first=[pscustomobject]@{bundled=$script:ProjectionBundled;installed=$script:ProjectionInstalled}
 $cachedAfterFirst=$memo.ContainsKey(':bundle-schema')
 $final=Invoke-AutomaticAppearanceIdle $observed.snapshot -AutomaticPlanMemo $memo
 $complete=[pscustomobject]@{bundled=$script:ProjectionBundled;installed=$script:ProjectionInstalled}
 $manifestBytes=[IO.File]::ReadAllBytes($script:AppearanceContentManifestFile)
 $manifestText=[IO.File]::ReadAllText($script:AppearanceContentManifestFile)
 $manifest=$manifestText|ConvertFrom-Json
 $entry=@($manifest.files|Where-Object {$_.path -ceq 'Round/shared.css'})[0]
 $bundlePath=Join-Path (Join-Path $script:PackRoot 'themes') $entry.path
 $installedPath=Join-Path $script:ThemeRoot $entry.path
 $asset=[IO.File]::ReadAllBytes($bundlePath);[byte[]]$badAsset=$asset.Clone();$badAsset[0]=$badAsset[0] -bxor 1
 $writeCount=$script:Applied
 try{
  # A proof may observe a healthy install with a damaged source; its final gate must fail.
  [IO.File]::WriteAllBytes($bundlePath,$badAsset)
  $late=$null;$lateMemo=@{}
  try{$late=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources -AutomaticPlanMemo $lateMemo}catch{}
  $finalRejected=$late.ok -and (Reject {Invoke-AutomaticAppearanceIdle $late.snapshot -AutomaticPlanMemo $lateMemo}) -and $script:AppearanceFailureCode -eq 3
  $normal=(Invoke-AppearanceControl 'Status').snapshot
  $script:MissingStyles=@('Round')
  $writeRejected=Reject {Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources}
  $script:MissingStyles=@()
 }finally{[IO.File]::WriteAllBytes($bundlePath,$asset)}
 try{
  [IO.File]::WriteAllBytes($installedPath,$badAsset)
  $script:ProjectionBundled=0;$script:ProjectionInstalled=0
  $drift=(Invoke-AppearanceControl 'Status' -AutomaticReadiness -DeferObservationBackup).snapshot
  $driftChecks=$script:ProjectionBundled
 }finally{[IO.File]::WriteAllBytes($installedPath,$asset)}
 try{
  [IO.File]::WriteAllText($script:AppearanceContentManifestFile,($manifestText+"`n"),[Text.UTF8Encoding]::new($false))
  $script:ProjectionBundled=0;$script:ProjectionInstalled=0
  $replacement=(Invoke-AppearanceControl 'Status' -AutomaticReadiness -DeferObservationBackup).snapshot
  $replacementChecks=$script:ProjectionBundled
 }finally{[IO.File]::WriteAllBytes($script:AppearanceContentManifestFile,$manifestBytes)}
 [pscustomobject]@{preliminary=$first;complete=$complete;bundledAssets=@($manifest.files).Count;firstCachedSchema=$cachedAfterFirst;finalRejected=[bool]$finalRejected;writeRejected=$writeRejected;driftChecks=$driftChecks;replacementChecks=$replacementChecks}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'bundle-projection.json') -Encoding UTF8
 Assert ($observed.ok -and $final.ok -and $first.installed -eq 53 -and $complete.installed -eq 106) 'Preliminary and final phases still validate the currently installed resource bytes'
 Assert (!$first.bundled -and $complete.bundled -eq @($manifest.files).Count -and !$cachedAfterFirst -and $memo.ContainsKey(':bundle-schema')) 'Only the final confirmation reads source files and publishes validated source schema'
 Assert ($finalRejected -and $normal.packError -and $writeRejected -and $script:Applied -eq $writeCount -and !$script:Starts -and !$script:Resets) 'Damaged source never grants final success or authorizes correction, and ordinary status reports it'
 Assert ($drift.packRepairRequired -and $driftChecks -eq @($manifest.files).Count) 'Installed drift returns to the full source admission path'
 Assert ($replacement.packUpdateAvailable -and $replacementChecks -eq @($manifest.files).Count) 'A new manifest digest is never treated as owned and fully validates source assets'
}}

[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);selectedCase=$Case;realSteamOperations=0;realCssLaunches=0;realUserConfigWrites=0;fixtureInitialization=$script:AppearanceFixtureInitialization}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) automatic appearance Refresh consumer checks"
