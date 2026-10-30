param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8)
$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###'
$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('Candidate parse errors: '+($errors.Message -join ';'))}
$definitions=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false))
foreach($fn in $definitions){. ([scriptblock]::Create($fn.Extent.Text))}
$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw $Message}}
function Reset-Flow {
    $script:sim=[pscustomobject]@{Time=0L;Events=[Collections.Generic.List[string]]::new();Kills=[Collections.Generic.List[long]]::new();SteamAt=1000L;WindowAt=2000L;XboxAt=@(0L,1500L,6500L);AlwaysXbox=$false;Denied=$false;Addons=@('addon-a','addon-b');Affinity=0;Shows=0;Cold=0;InputAt=0L;InputState='local-target-admitted';Focus=0;FocusAt=[Collections.Generic.List[long]]::new();FocusStages=[Collections.Generic.List[string]]::new();FocusRounds=[Collections.Generic.List[int]]::new();CSS=0;YMCC=0}
}
function New-Ports {
    @{
      Now={$script:sim.Time};Wait={$script:sim.Time+=500L}
      Log={param($E,$D)$script:sim.Events.Add('log:'+ $E)}
      Snapshot={
        $present=$script:sim.AlwaysXbox -or $script:sim.Denied
        foreach($at in $script:sim.XboxAt){if($script:sim.Time -ge $at -and -not $script:sim.Kills.Contains([long]$at)){$present=$true}}
        [pscustomobject]@{Xbox=$(if($present){@([pscustomobject]@{Id=42})}else{@()});SteamRunning=($script:sim.SteamAt -ge 0 -and $script:sim.Time -ge $script:sim.SteamAt);SteamWindow=($script:sim.WindowAt -ge 0 -and $script:sim.Time -ge $script:sim.WindowAt)}
      }
      CloseXbox={param($Processes)
        $script:sim.Events.Add('xbox')
        if(-not $script:sim.Denied){foreach($at in $script:sim.XboxAt){if($script:sim.Time -ge $at -and -not $script:sim.Kills.Contains([long]$at)){$script:sim.Kills.Add([long]$at)}}}
      }
      CSS={throw 'CSS launch belongs to YMCC; BAT must never call this port'}
      YMCC={$script:sim.YMCC++;$script:sim.Events.Add('ymcc')}
      StartSteam={$script:sim.Cold++;$script:sim.Events.Add('steam-cold')}
      ShowSteam={$script:sim.Shows++;$script:sim.Events.Add('steam-show')}
      Addons={throw 'BAT cannot read linked lists'};StartAddon={throw 'BAT cannot launch linked applications'}
      FocusContext={[pscustomobject]@{ready=($script:sim.InputAt -ge 0 -and $script:sim.Time -ge $script:sim.InputAt);state=$script:sim.InputState}}
      FocusSteam={param($Round,$Stage)$script:sim.Focus++;$script:sim.FocusAt.Add($script:sim.Time);$script:sim.FocusStages.Add($Stage);$script:sim.FocusRounds.Add($Round)};Affinity={$script:sim.Affinity++}
    }
}
Reset-Flow;$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 0) 'cold start succeeds'
$sequence=@($script:sim.Events|Where-Object {$_ -eq 'css' -or $_ -eq 'ymcc' -or $_ -like 'steam-*' -or $_ -like 'addon:*'})
Assert (($sequence -join ',') -eq 'steam-cold') 'Steam BAT leaves YMCC and CSS to their original owners'
Assert ($script:sim.Kills.Count -eq 3) 'all three scheduled Xbox reappearances handled'
Assert ($script:sim.Time -eq 60000) 'startup focus observation covers the full sixty seconds'
Assert ($script:sim.CSS -eq 0 -and $script:sim.YMCC -eq 0 -and $script:sim.Cold -eq 1) 'launch stages each run once'
Assert ($script:sim.Affinity -eq 1 -and $script:sim.Shows -eq 0) 'no duplicate Steam URI once window ready'
Reset-Flow;$script:sim.SteamAt=0;$script:sim.WindowAt=0
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 0 -and $script:sim.Shows -eq 1 -and $script:sim.Cold -eq 0) 'existing Steam shows once, no cold launch'
Reset-Flow;$script:sim.WindowAt=10000
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 0 -and $script:sim.Shows -eq 1 -and $script:sim.Cold -eq 1) 'delayed UI gets one finite URI followup'
Reset-Flow;$script:sim.WindowAt=-1
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 1 -and $script:sim.Time -eq 60000 -and $script:sim.Shows -eq 1) 'window missing bounded60 seconds'
Reset-Flow;$script:sim.SteamAt=-1;$script:sim.WindowAt=-1
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 1 -and $script:sim.Cold -eq 1 -and $script:sim.Shows -eq 0 -and $script:sim.Affinity -eq 0) 'Steam missing no repeated cold launches'
Reset-Flow;$script:sim.Denied=$true
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 1 -and $script:sim.Time -eq 60000) 'access-denied/no-quiet cannot report success'
Reset-Flow;$script:sim.AlwaysXbox=$true
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 1 -and $script:sim.Time -eq 60000) 'persistent relaunch expires without permanent watchdog'
Reset-Flow;$script:sim.XboxAt=@(0L,29500L,35000L)
$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 0 -and $script:sim.Time -eq 60000 -and $script:sim.Kills.Count -eq 3) 'late Xbox appearance resets8 second quiet window'
# Window appearance and virtual completion are separate, one-shot milestones.
Reset-Flow;$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 1 -and $script:sim.FocusAt[0] -eq 2000) 'already-ready virtual output coalesces with the first Steam window focus'
Assert ($script:sim.FocusStages[0] -eq 'steam-window+virtual-ready') 'simultaneous milestones do not issue two consecutive focus groups'
Reset-Flow;$script:sim.InputAt=23000L;$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 2 -and ($script:sim.FocusAt -join ',') -eq '2000,23000') 'Steam window focuses immediately and virtual completion focuses once without an extra delay'
Assert (($script:sim.FocusStages -join ',') -eq 'steam-window,virtual-ready') 'each milestone uses its own one-shot stage'
Reset-Flow;$script:sim.SteamAt=0;$script:sim.WindowAt=0;$script:sim.InputState='not-requested';$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 1 -and $script:sim.FocusAt[0] -eq 0 -and $script:sim.FocusStages[0] -eq 'steam-window') 'disabled virtual output focuses only at Steam window appearance'
Reset-Flow;$script:sim.InputAt=-1;$script:sim.InputState='unavailable';$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 1 -and $script:sim.FocusAt[0] -eq 2000) 'unknown readiness never delays the first focus or invents a timed fallback'
Reset-Flow;$script:sim.InputAt=57000L;$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 2 -and ($script:sim.FocusAt -join ',') -eq '2000,57000') 'a late accepted virtual frame authorizes only the second milestone'
Reset-Flow;$script:sim.AlwaysXbox=$true;$code=Invoke-BootFlow (New-Ports)
Assert ($code -eq 1 -and $script:sim.Focus -eq 1) 'persistent Xbox does not rearm a completed milestone'
Reset-Flow;$script:sim.WindowAt=-1;$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 0 -and $script:sim.Time -eq 60000) 'no verified Steam window receives no focus command'
Reset-Flow;$script:sim.WindowAt=59500;$code=Invoke-BootFlow (New-Ports)
Assert ($script:sim.Focus -eq 1 -and $script:sim.FocusAt[0] -eq 59500) 'late Steam window still coalesces already-completed virtual readiness'
Reset-Flow;$ports=New-Ports;$ports.FocusSteam={param($Round,$Stage)$script:sim.Focus++;return $false};$code=Invoke-BootFlow $ports
Assert ($code -eq 0 -and $script:sim.Focus -eq 1) 'Windows refusal cannot rearm repeated startup focus'
Reset-Flow;$ports=New-Ports;$ports.FocusContext={throw 'inert unavailable receipt'};$code=Invoke-BootFlow $ports
Assert ($code -eq 0 -and $script:sim.Focus -eq 1 -and $script:sim.FocusAt[0] -eq 2000) 'receipt errors preserve immediate first-window focus without a late fallback'
Reset-Flow;$script:sim.InputAt=23000L;$ports=New-Ports;$ports.FocusAllowed={$false};$code=Invoke-BootFlow $ports
Assert ($script:sim.Focus -eq 1) 'second milestone does not steal from a deliberately selected user window'
Reset-Flow;$script:sim.InputAt=66000L;$ports=New-Ports;$code=Invoke-BootFlow $ports
$ports.Wait={$script:sim.Time+=10000L};$ports.Checkpoint={param($Stage,$Snapshot)};Invoke-BootDiagnosticTail $ports
Assert ($script:sim.Focus -eq 2 -and ($script:sim.FocusAt -join ',') -eq '2000,70000') 'late virtual readiness retains the shared second milestone inside the bounded tail'
Reset-Flow;$script:sim.InputAt=120000L;$ports=New-Ports;$code=Invoke-BootFlow $ports
$ports.Wait={$script:sim.Time+=10000L};$ports.Checkpoint={param($Stage,$Snapshot)};Invoke-BootDiagnosticTail $ports
Assert ($script:sim.Focus -eq 1 -and $script:sim.Time -eq 120000) 'no focus action is authorized beyond the complete startup observation boundary'
Assert ($text -notmatch 'Get-BootAddons|Start-BootAddon|\$P\.Addons|\$P\.StartAddon|Prepare-EarlyDecky|ExpectedSha256|YeManSteam2\.bat') 'BAT contains no linked application or duplicate Decky implementation'
Assert ($text -notmatch 'function Start-BootYMCC|function New-BootYmccStartInfo|\$P\.YMCC') 'BAT never starts YMCC even when an inert launch port is present'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual startup controller; CSS exclusively delegated to YMCC';effects='memory-only';secondBatExecuted=$false;applicationsStartedOrStopped=$false;systemSettingsChanged=$false}|ConvertTo-Json -Depth 4|Tee-Object -FilePath (Join-Path $Evidence 'selftest-result.json')