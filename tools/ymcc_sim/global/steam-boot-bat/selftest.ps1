param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
if(-not $Candidate){$Candidate=Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path 'PowerControl\YeManSteam.bat'}
if(-not $Evidence){throw 'Explicit owned evidence folder is required'}
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###';$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('BAT parse failed: '+($errors.Message -join ';'))}
$definitions=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false));$functions=@{}
foreach($fn in $definitions){$functions[$fn.Name]=$fn.Extent.Text;. ([scriptblock]::Create($fn.Extent.Text))}
# Replace just the file-existence terminal. The actual launch decision body remains otherwise verbatim.
. ([scriptblock]::Create($functions['Start-BootYMCC'].Replace('[IO.File]::Exists($path)','(Test-InertFileExists $path)')))
$script:checks=0;$script:session=42
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}
function Reset-Launch {
 $script:state=@{Admin=$true;Existing=$false;Arrives=$false;OtherSession=$false;QueryCode=0;FileExists=$true;Queries=0;Launches=0;AdminReads=0;ProcessReads=0;LaunchThrows=$false;Logs=[Collections.Generic.List[string]]::new()}
}
function Write-BootLog {param($E,$D)$script:state.Logs.Add($E+':'+$D)}
function Test-BootAdministrator {$script:state.AdminReads++;return $script:state.Admin}
function Test-InertFileExists {param($Path)$script:state.FileExists}
function Start-Quiet {param($Exe,$Arguments,[switch]$Capture)
 $script:state.Queries++;Assert ($Arguments -like '/query /tn *' -and $Capture) 'only a read-only task query is allowed'
 if($script:state.Arrives){$script:state.Existing=$true};[pscustomobject]@{Code=$script:state.QueryCode;Text='inert task query'}
}
function Start-BootYmccProcess {param($Path)$script:state.Launches++;if($script:state.LaunchThrows){throw 'inert launch failure'}}
function Get-Process {param($Name,$Id,$ErrorAction)
 $script:state.ProcessReads++
 if($Id){if($script:state.LookupThrows){throw 'inert process lookup failure'};return $script:state.FrontProcess}
 if($script:state.Existing){[pscustomobject]@{SessionId=42;Id=123}}
 elseif($script:state.OtherSession){[pscustomobject]@{SessionId=99;Id=124}}
}
$info=New-BootYmccStartInfo 'C:\inert folder\YeManCC.exe'
Assert ($info.UseShellExecute -and -not $info.CreateNoWindow -and $info.WindowStyle -eq [Diagnostics.ProcessWindowStyle]::Normal) 'YMCC uses normal shell launch, never hidden console launch'
Assert ($info.Arguments -eq '' -and $info.Verb -eq '' -and -not $info.RedirectStandardOutput -and -not $info.RedirectStandardError) 'YMCC arguments, elevation verb and streams are not overridden'
Assert ($info.WorkingDirectory -eq 'C:\inert folder') 'YMCC launch retains its own working folder'
Reset-Launch;$script:state.Admin=$false;Start-BootYMCC
Assert ($script:state.Queries -eq 0 -and $script:state.Launches -eq 0 -and $script:state.Logs[0] -like '*administrator-required*') 'ordinary token cannot attempt YMCC launch or restart its scheduled task'
Reset-Launch;$script:state.Existing=$true;$script:state.Admin=$false;Start-BootYMCC
Assert ($script:state.AdminReads -eq 0 -and $script:state.Queries -eq 0 -and $script:state.Launches -eq 0) 'existing YMCC is preserved before any privilege or task action'
Reset-Launch;Start-BootYMCC
Assert ($script:state.Queries -eq 1 -and $script:state.Launches -eq 1) 'elevated caller retains exactly one normal launch after startup opt-in'
Reset-Launch;$script:state.OtherSession=$true;Start-BootYMCC
Assert ($script:state.Launches -eq 1) 'another user session does not impersonate current YMCC'
Reset-Launch;$script:state.QueryCode=1;Start-BootYMCC
Assert ($script:state.Launches -eq 0 -and $script:state.Logs[0] -like '*startup-task-not-found*') 'missing startup opt-in cannot be bypassed'
Reset-Launch;$script:state.FileExists=$false;Start-BootYMCC
Assert ($script:state.Launches -eq 0 -and $script:state.Logs[0] -like '*executable-missing*') 'missing executable remains a reported skip'
Reset-Launch;$script:state.Arrives=$true;Start-BootYMCC
Assert ($script:state.Launches -eq 0 -and $script:state.Logs[0] -like '*already-running-after-query*') 'normal startup arriving during query is retained'
Reset-Launch;$script:state.LaunchThrows=$true;Start-BootYMCC
Assert ($script:state.Launches -eq 1 -and $script:state.Logs[0] -like '*inert launch failure*') 'launch error is recorded without restart or forced recovery'
# Foreground observation is read-only and current-session scoped. No Win32 APIs run.
# User replaced the old permanent-yield policy: Steam is the only startup focus target.
$script:flow=@{Time=0L;Focus=0;Shows=0;Logs=[Collections.Generic.List[string]]::new()}
$ports=@{Now={$script:flow.Time};Wait={$script:flow.Time+=500};Log={param($E,$D)$script:flow.Logs.Add($E)};Snapshot={[pscustomobject]@{Xbox=@();SteamRunning=$true;SteamWindow=$true}};CloseXbox={};YMCC={};StartSteam={};ShowSteam={$script:flow.Shows++};Affinity={};FocusContext={[pscustomobject]@{ready=$true;state='not-requested'}};FocusSteam={param($N,$S)$script:flow.Focus++};YieldSteamFocus={throw 'obsolete surrender port must not be read'}}
$code=Invoke-BootFlow $ports
Assert ($code -eq 0 -and $script:flow.Focus -eq 1 -and $script:flow.Time -eq 60000) 'startup hands off to Steam once without a surrender port'
Assert (-not $functions.ContainsKey('Test-BootFocusYield') -and $body -notmatch 'bootFocusYielded|focusYielded') 'permanent foreground surrender latch is removed'
$script:focusCalls=0
function Invoke-SteamFocus {param($P,$N,$S)$script:focusCalls++;return $true}
$script:bootFocusState=New-BootFocusState 0;$script:bootClock=[Diagnostics.Stopwatch]::StartNew()
Focus-Steam 1 'startup-guard'|Out-Null
Assert ($script:focusCalls -eq 1) 'focus wrapper always forwards the verified Steam target without foreground suppression'
Assert ($functions['Start-BootYMCC'] -notmatch 'Start-Quiet\s+\$path|/run\s+/tn|Stop-Process|\.Kill\(') 'YMCC startup cannot use hidden helper or restart the scheduled instance'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual BAT launch, foreground yield and scheduling';effects='memory-only process/window/clock terminals';realApplicationsStartedStopped=$false;realForegroundChanged=$false;fieldAcceptance=$false;candidateSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'ymcc-recall-selftest.json')
