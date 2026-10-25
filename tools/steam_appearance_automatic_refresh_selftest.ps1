[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
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
function Reject([scriptblock]$Work){try{& $Work|Out-Null;return $false}catch{return $true}}
Invoke-PresetPrepare|Out-Null;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});Complete-PresetReload
$before=@{};foreach($name in $script:ThemeNames){$before[$name]=Get-ConfigHash (Join-Path $script:ThemeRoot ($name+'\config_USER.json'))}
$result=Invoke-AutomaticAppearanceRefresh
Assert ($result.ok -and $script:Applied -eq 8 -and $script:Rendered) 'Already running CSS with 8/8 matching flags executes the same eight theme submissions as manual Refresh'
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
Assert ($result.ok -and $script:Rendered -and @($result.snapshot.themes|Where-Object configured).Count -eq 7) 'A later admitted document reuses the real Refresh path and preserves 7/8 choices'
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
Assert (!$manual.ok -and $manual.saved -and !$manual.renderConfirmed -and $script:Applied -eq 8) 'Manual Refresh preserves configuration and reports unconfirmed when the engine succeeds but a theme is still missing'
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
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssLaunches=0;realUserConfigWrites=0;fixtureInitialization=$script:AppearanceFixtureInitialization}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) automatic appearance Refresh consumer checks"
