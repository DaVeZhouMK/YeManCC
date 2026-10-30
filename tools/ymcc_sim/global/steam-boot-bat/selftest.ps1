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

$script:checks=0;$script:session=42

function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}

Assert (-not $functions.ContainsKey('Start-BootYMCC') -and -not $functions.ContainsKey('Start-BootYmccProcess') -and -not $functions.ContainsKey('New-BootYmccStartInfo')) 'BAT has no YMCC launcher or custom startup implementation'
Assert ($body -notmatch '\$P\.YMCC|YMCC=\{Start-Boot|YeManCC\.exe') 'BAT cannot dispatch a second YMCC executable or startup task'
# Foreground observation is read-only and current-session scoped. No Win32 APIs run.

# User replaced the old permanent-yield policy: Steam is the only startup focus target.

$script:flow=@{Time=0L;Focus=0;Shows=0;Logs=[Collections.Generic.List[string]]::new()}

$ports=@{Now={$script:flow.Time};Wait={$script:flow.Time+=500};Log={param($E,$D)$script:flow.Logs.Add($E)};Snapshot={[pscustomobject]@{Xbox=@();SteamRunning=$true;SteamWindow=$true}};CloseXbox={};YMCC={};StartSteam={};ShowSteam={$script:flow.Shows++};Affinity={};FocusContext={[pscustomobject]@{ready=$true;state='not-requested'}};FocusSteam={param($N,$S)$script:flow.Focus++};YieldSteamFocus={throw 'obsolete surrender port must not be read'}}

$code=Invoke-BootFlow $ports

Assert ($code -eq 0 -and $script:flow.Focus -eq 1 -and $script:flow.Time -eq 60000) 'startup hands off to Steam once without a surrender port'

Assert (-not $functions.ContainsKey('Test-BootFocusYield') -and $body -notmatch 'bootFocusYielded|focusYielded') 'permanent foreground surrender latch is removed'

$script:focusCalls=0

function Invoke-SteamFocus {param($P,$N,$S)$script:focusCalls++;return $true}

$script:bootFocusStates=@{};$script:bootClock=[Diagnostics.Stopwatch]::StartNew()

Focus-Steam 1 'startup-guard'|Out-Null

Assert ($script:focusCalls -eq 1) 'focus wrapper always forwards the verified Steam target without foreground suppression'

Assert ($body -match 'ymccLaunch=disabled') 'startup log explicitly declares standard-entry-owned YMCC'

[ordered]@{status='PASS';checks=$script:checks;consumer='actual BAT launch, foreground yield and scheduling';effects='memory-only process/window/clock terminals';realApplicationsStartedStopped=$false;realForegroundChanged=$false;fieldAcceptance=$false;candidateSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'ymcc-recall-selftest.json')

