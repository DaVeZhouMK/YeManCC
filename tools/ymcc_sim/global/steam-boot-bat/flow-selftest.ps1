param([string]$Candidate,[string]$Evidence,[ValidateSet('final-admission','final-healthy','final-request','final-flow','final-contract','network-task')][string]$CaseGroup)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8)
$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###'
$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('Candidate parse errors: '+($errors.Message -join ';'))}
$definitions=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false))
foreach($fn in $definitions){
    $code=$fn.Extent.Text
    if($CaseGroup -and $CaseGroup -ne 'network-task') {        if($fn.Name -eq 'Invoke-BootSteamDispatch') {
            $literal="'Local\YeManCC-UnifiedSettings'"
            if(($code.Split(@($literal),[StringSplitOptions]::None).Length-1) -ne 1){throw 'one original settings mutex literal required'}
            $code=$code.Replace($literal,"`$script:fixtureMutex")
            if(($code.Split(@('$submitted=$Process.Start()'),[StringSplitOptions]::None).Length-1) -ne 1){throw 'one actual final Process.Start required'}
            $code=$code.Replace('$submitted=$Process.Start()','$submitted=Invoke-ObservedBootProcessStart $Process')
        }
        if($fn.Name -eq 'Start-Quiet') {
            if(($code.Split(@('if(-not $p.Start())'),[StringSplitOptions]::None).Length-1) -ne 1){throw 'one original generic Process.Start required'}
            $code=$code.Replace('if(-not $p.Start())','if(-not (Invoke-ObservedBootProcessStart $p))')
        }
    }
    . ([scriptblock]::Create($code))
}
$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw $Message}}if($CaseGroup -eq 'network-task') {
    $legacy=[IO.File]::ReadAllText((Join-Path (Split-Path -Parent $Candidate) 'Steamcommunity_302.xml'))
    $v15=$legacy.Replace('</Command>','</Command><Arguments>--start-service</Arguments>')
    $folder=[pscustomobject]@{Tasks=@{}}
    $folder | Add-Member ScriptMethod GetTask {
        param($Name)
        if(-not $this.Tasks.ContainsKey($Name)){throw [Runtime.InteropServices.COMException]::new('Missing',-2147024894)}
        if($this.Tasks[$Name] -eq 'DENIED'){throw [Runtime.InteropServices.COMException]::new('Denied',-2147024891)}
        [pscustomobject]@{Xml=$this.Tasks[$Name]}
    }
    $old='Steamcommunity_302';$new='Steamcommunity302 V15'
    Assert (-not (Select-BootNetworkTask $folder).ready) 'absent tasks cannot start anything'
    $folder.Tasks[$old]=$legacy
    Assert ((Select-BootNetworkTask $folder).task -ceq "\$old") 'legacy alone with no arguments remains supported'
    $folder.Tasks[$new]=$v15
    Assert ((Select-BootNetworkTask $folder).task -ceq "\$new") 'V15 wins when both are enabled'
    $folder.Tasks[$new]=$v15.Replace('</Settings>','<Enabled>false</Enabled></Settings>').Replace('<Enabled>true</Enabled>','')
    Assert ((Select-BootNetworkTask $folder).task -ceq "\$old") 'disabled V15 falls back to enabled legacy'
    $folder.Tasks[$new]='DENIED';$denied=$false
    try{$null=Select-BootNetworkTask $folder}catch{$denied=$true}
    Assert $denied 'access denied never falls back to launch a second owner'
    foreach($bad in @($v15.Replace('--start-service','--unexpected'),$v15.Replace('IgnoreNew','Parallel'),$v15.Replace('>C:\SOFT\steamcommunity</WorkingDirectory>','>C:\Windows</WorkingDirectory>'))) {
        $folder.Tasks[$new]=$bad
        Assert (-not (Select-BootNetworkTask $folder).ready) 'malformed V15 definition must not authorize fallback'
    }
    $folder.Tasks[$old]=$v15;$folder.Tasks.Remove($new)
    Assert (-not (Select-BootNetworkTask $folder).ready) 'legacy alias still rejects V15-only arguments'
    $folder.Tasks[$old]=$legacy;$folder.Tasks[$new]=$v15
    $state=@{Reads=0;Requests=@();Sequence=@('absent','absent');Log=@()}
    $ports=@{Task={Select-BootNetworkTask $folder};Process={ [pscustomobject]@{state=$state.Sequence[$state.Reads++];pid=42;birth=1} }
        Request={param($Name)$state.Requests+=@($Name);[pscustomobject]@{Code=0;Text=''}}
        Log={param($Event,$Detail)$state.Log+=@($Event+' '+$Detail)}}
    Invoke-BootNetworkAcceleration $ports
    Assert ($state.Reads -eq 2 -and $state.Requests.Count -eq 1 -and $state.Requests[0] -ceq "\$new") 'selected name reaches the one request after two absence checks'
    $state.Reads=0;$state.Requests=@();$state.Sequence=@('absent','running')
    Invoke-BootNetworkAcceleration $ports
    Assert ($state.Reads -eq 2 -and $state.Requests.Count -eq 0) 'a concurrently started process prevents submission'
    $raw=[ordered]@{status='PASS';selection=$CaseGroup;checks=$script:checks;realTaskQueries=0;realTaskRuns=0;realProcessScans=0}
    $json=$raw|ConvertTo-Json
    $stream=[IO.File]::Open((Join-Path $Evidence 'network-task-result.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes($json);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    $json
    return
}

function Reset-Flow {
    $script:sim=[pscustomobject]@{Time=0L;Events=[Collections.Generic.List[string]]::new();Kills=[Collections.Generic.List[long]]::new();SteamAt=1000L;WindowAt=2000L;XboxAt=@(0L,1500L,6500L);AlwaysXbox=$false;Denied=$false;Addons=@('addon-a','addon-b');Affinity=0;Shows=0;Cold=0;InputAt=0L;InputState='local-target-admitted';Focus=0;FocusAt=[Collections.Generic.List[long]]::new();FocusStages=[Collections.Generic.List[string]]::new();FocusRounds=[Collections.Generic.List[int]]::new();CSS=0;YMCC=0;Network=0;Closes=0;Snapshots=0;AdmissionReads=0;AdmissionReader=$null;AdmissionSettings=('{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":true}}}'|ConvertFrom-Json)}
}
function New-Ports {
    @{
      StartupAdmission={$script:sim.AdmissionReads++;if($null -ne $script:sim.AdmissionReader){return (& $script:sim.AdmissionReader)};Get-BootSteamAdmission $script:sim.AdmissionSettings}
      NetworkAcceleration={$script:sim.Network++;$script:sim.Events.Add('network')}
      Now={$script:sim.Time};Wait={$script:sim.Time+=500L}
      Log={param($E,$D)$script:sim.Events.Add('log:'+ $E)}
      Snapshot={
        $script:sim.Snapshots++
        $present=$script:sim.AlwaysXbox -or $script:sim.Denied
        foreach($at in $script:sim.XboxAt){if($script:sim.Time -ge $at -and -not $script:sim.Kills.Contains([long]$at)){$present=$true}}
        [pscustomobject]@{Xbox=$(if($present){@([pscustomobject]@{Id=42})}else{@()});SteamRunning=($script:sim.SteamAt -ge 0 -and $script:sim.Time -ge $script:sim.SteamAt);SteamWindow=($script:sim.WindowAt -ge 0 -and $script:sim.Time -ge $script:sim.WindowAt)}
      }
      CloseXbox={param($Processes)
        $script:sim.Closes++
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
if($CaseGroup) {
    $root=[IO.Path]::GetFullPath($Evidence)
    [IO.Directory]::CreateDirectory($root)|Out-Null
    $script:fixtureRoot=$root;$script:fixtureMutex='Local\YMCC.BootSteam.Final.'+[guid]::NewGuid().ToString('N')
    $script:fixtureSettings=Join-Path $root 'yeman-settings.json'
    $script:fixtureQueue=Join-Path $root 'queued-settings.json';$script:fixtureDelay=Join-Path $root 'hold-ms.txt'
    $script:fixtureReply=Join-Path $root 'mutex-probe.txt'
    $script:kernelStarts=0;$script:probeChecks=0;$script:probeUnheld=0;$script:fixtureChildren=[Collections.Generic.List[object]]::new()
    $script:fixtureLogs=[Collections.Generic.List[string]]::new();$script:logThrows=$false
    $cases=[Collections.Generic.List[object]]::new();$failed=0;$failure='';$childReturnErrors=[Collections.Generic.List[string]]::new()
    $script:checks=0;$script:probeHelper=$null;$events=@{};$sourceBefore=$env:YEMAN_STARTUP_SOURCE
    $script:fixtureCmd=[IO.Path]::GetFullPath($env:ComSpec);$script:fixtureMissing=Join-Path $root 'absent-owned-command.exe'
    function Write-BootLog {param($Event,$Detail)if($script:logThrows){throw 'controlled-log-failure'};$script:fixtureLogs.Add($Event+':'+$Detail)}
    function Write-OwnedJson($Value) {[IO.File]::WriteAllText($script:fixtureSettings,($Value|ConvertTo-Json -Depth 15 -Compress),[Text.UTF8Encoding]::new($false))}
    function New-OwnedSettings {
        return ('{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":true,"current":"global","revision":20,"preparation":{"schema":1,"mode":"steam","enabled":true,"revision":11,"observedBoot":"same-boot","status":"prepared","steps":{}}}},"input":{"revision":3,"appliedRevision":1},"theme":"dark"}'|ConvertFrom-Json)
    }
    function Reset-OwnedRequest {
        $env:YEMAN_STARTUP_SOURCE=Join-Path $script:fixtureRoot 'YeManSteam.bat'
        $script:ownedSettings=New-OwnedSettings;Write-OwnedJson $script:ownedSettings
        $script:bootSteamRequest=(Read-BootSteamAdmission -CaptureRequest).Request
        $script:steamExe=$script:fixtureCmd;$script:logThrows=$false
    }
    function New-OwnedProcess([bool]$Missing=$false) {
        $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=if($Missing){$script:fixtureMissing}else{$script:fixtureCmd}
        $start.Arguments='/d /c exit /b 0';$start.WorkingDirectory=$script:fixtureRoot;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $process=[Diagnostics.Process]::new();$process.StartInfo=$start;return $process
    }
    function Invoke-ObservedBootProcessStart([Diagnostics.Process]$Process) {
        if($Process.StartInfo.FileName -cne $script:fixtureCmd -and $Process.StartInfo.FileName -cne $script:fixtureMissing){throw 'foreign-process-start-forbidden'}
        if($Process.StartInfo.Arguments -cnotin @('/d /c exit /b 0','-bigpicture -noverifyfiles -nobootstrapupdate -skipinitialbootstrap','steam://open/gamepadui')){throw 'unknown-owned-command-arguments'}
        $events.Reply.Reset()|Out-Null;$events.Probe.Set()|Out-Null
        if(-not $events.Reply.WaitOne(3000)){throw 'owned-mutex-probe-timeout'}
        $held=[IO.File]::ReadAllText($script:fixtureReply) -ceq 'held'
        $script:probeChecks++;if(-not $held){$script:probeUnheld++}
        # Only the controlled leaf argv changes: record original fixed Steam
        # request, execute the owned cmd as a finite inert exit instead.
        $requestedArguments=$Process.StartInfo.Arguments
        if($Process.StartInfo.FileName -ceq $script:fixtureCmd){$Process.StartInfo.Arguments='/d /c exit /b 0'}
        try{$started=$Process.Start()}finally{$Process.StartInfo.Arguments=$requestedArguments}
        if($started) {
            $script:kernelStarts++
            $identity=[ordered]@{pid=$Process.Id;birth=$null;held=$held;returned=$false;exit=$null;metadataError='';requestedArguments=$requestedArguments;inertArguments='/d /c exit /b 0';process=$null}
            try {
                $identity.birth=$Process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
                try {
                    $copy=[Diagnostics.Process]::GetProcessById($Process.Id)
                    if($copy.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -cne $identity.birth){$copy.Dispose();throw 'owned-pid-reused'}
                    $identity.process=$copy
                }catch [ArgumentException]{$identity.returned=$true}
            }catch{
                $identity.metadataError=$_.Exception.Message
                try{if($Process.WaitForExit(0)){$identity.returned=$true;$identity.exit=$Process.ExitCode}}catch{}
            }
            $script:fixtureChildren.Add($identity)
        }
        return $started
    }
    function Invoke-OwnedSubmission([bool]$Missing=$false) {
        $p=New-OwnedProcess $Missing
        try {return Invoke-BootSteamDispatch $script:bootSteamRequest $p}finally{$p.Dispose()}
    }
    function Start-OwnedMutexHold([int]$Milliseconds,$NewSettings=$null) {
        $events.Held.Reset()|Out-Null;$events.Released.Reset()|Out-Null
        [IO.File]::WriteAllText($script:fixtureDelay,$Milliseconds.ToString([Globalization.CultureInfo]::InvariantCulture))
        if($null -eq $NewSettings){if([IO.File]::Exists($script:fixtureQueue)){[IO.File]::Delete($script:fixtureQueue)}}
        else {[IO.File]::WriteAllText($script:fixtureQueue,($NewSettings|ConvertTo-Json -Depth 15 -Compress),[Text.UTF8Encoding]::new($false))}
        $events.Hold.Set()|Out-Null;Assert ($events.Held.WaitOne(3000)) 'owned writer acquired mutex before parent dispatch'
    }
    function Finish-OwnedMutexHold {Assert ($events.Released.WaitOne(3000)) 'owned writer released mutex'}
    function Invoke-FinalCase([string]$Name,[scriptblock]$Action) {
        $before=$script:kernelStarts;$observed=$script:probeChecks;$unheld=$script:probeUnheld;$initial=$script:checks
        $item=[ordered]@{name=$Name;passed=$false;checks=0;kernelStarts=0;mutexProbes=0;unheldStarts=0;error=''}
        try {Reset-OwnedRequest;& $Action;$item.passed=$true}
        catch {$item.error=$_.Exception.Message;throw}
        finally {$item.checks=$script:checks-$initial;$item.kernelStarts=$script:kernelStarts-$before;$item.mutexProbes=$script:probeChecks-$observed;$item.unheldStarts=$script:probeUnheld-$unheld;$cases.Add([pscustomobject]$item)}
    }
    try {
        $names=@{};foreach($name in @('Probe','Reply','Stop','Hold','Held','Released')) {
            $names[$name]=$script:fixtureMutex+'.'+$name
            $events[$name]=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::AutoReset,$names[$name])
        }
        $probeFile=Join-Path $root 'mutex-probe-helper.ps1'
        $probeSource=@'
param($MutexName,$ProbeName,$ReplyName,$StopName,$HoldName,$HeldName,$ReleasedName,$Settings,$Queue,$Delay,$Receipt)
$ErrorActionPreference='Stop';$mutex=$null;$handles=@();$held=$false
try {
 $mutex=[Threading.Mutex]::new($false,$MutexName)
 foreach($name in @($StopName,$ProbeName,$HoldName,$ReplyName,$HeldName,$ReleasedName)){$handles+=[Threading.EventWaitHandle]::OpenExisting($name)}
 while($true) {
  $which=[Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($handles[0],$handles[1],$handles[2]),15000)
  if($which -eq 0){break};if($which -eq [Threading.WaitHandle]::WaitTimeout){throw 'owned-probe-idle-timeout'}
  if($which -eq 1) {
   $acquired=$false;try{$acquired=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
   if($acquired){$mutex.ReleaseMutex()}
   [IO.File]::WriteAllText($Receipt,$(if($acquired){'free'}else{'held'}));$handles[3].Set()|Out-Null
  } else {
   try{$held=$mutex.WaitOne(1000)}catch [Threading.AbandonedMutexException]{$held=$true}
   if(-not $held){throw 'owned-holder-timeout'}
   try {
    $handles[4].Set()|Out-Null;Start-Sleep -Milliseconds ([int][IO.File]::ReadAllText($Delay))
    if([IO.File]::Exists($Queue)){[IO.File]::WriteAllText($Settings,[IO.File]::ReadAllText($Queue),[Text.UTF8Encoding]::new($false))}
   }finally{$mutex.ReleaseMutex();$held=$false;$handles[5].Set()|Out-Null}
  }
 }
}finally {if($held){$mutex.ReleaseMutex()};foreach($h in $handles){$h.Dispose()};if($null -ne $mutex){$mutex.Dispose()}}
'@
        [IO.File]::WriteAllText($probeFile,$probeSource,[Text.UTF8Encoding]::new($false))
        $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Join-Path $PSHOME 'powershell.exe');$start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $arguments=@($probeFile,$script:fixtureMutex,$names.Probe,$names.Reply,$names.Stop,$names.Hold,$names.Held,$names.Released,$script:fixtureSettings,$script:fixtureQueue,$script:fixtureDelay,$script:fixtureReply)
        $start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File '+(($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ')
        $script:probeHelper=[Diagnostics.Process]::Start($start)
        $selected=[ordered]@{
          'healthy-no-resident-ymcc'={
            $r=Invoke-OwnedSubmission;Assert ($r.Current -and $r.Submitted) 'healthy original selection starts owned child without resident YMCC';Assert ($script:probeUnheld -eq 0) 'actual Process.Start is inside original settings mutex'
          }
          'new-selection-request'={
            $script:ownedSettings.extensions.bootModes.preparation.revision=12;Write-OwnedJson $script:ownedSettings
            $n=$script:kernelStarts;$r=Invoke-OwnedSubmission;Assert (-not $r.Current -and -not $r.Submitted -and $r.Reason -ceq 'startup-request-changed') 'new preparation cannot grant the captured request';Assert ($script:kernelStarts -eq $n) 'new request has zero actual Process.Start'
          }
          'other-steam-mode'={
            $script:ownedSettings.extensions.bootModes.boot='xbox-steam';$script:ownedSettings.extensions.bootModes.preparation.mode='xbox-steam';Write-OwnedJson $script:ownedSettings
            $r=Invoke-OwnedSubmission;Assert (-not $r.Current -and -not $r.Submitted) 'another allowed Steam mode does not replace captured selection'
          }
          'ordinary-save-and-input-ack'={
            $script:ownedSettings.theme='light';$script:ownedSettings.input.appliedRevision=3;$script:ownedSettings.extensions.bootModes.revision=21;$script:ownedSettings.extensions.bootModes.current='steam';Write-OwnedJson $script:ownedSettings
            $r=Invoke-OwnedSubmission;Assert ($r.Current -and $r.Submitted) 'unconsumed ordinary revision/runtime/input ACK changes preserve same selection'
          }
          'same-preparation-completion'={
            $script:ownedSettings.extensions.bootModes.preparation.status='preparing';$script:ownedSettings.extensions.bootModes.preparation.steps=[pscustomobject]@{steam=[pscustomobject]@{state='confirmed'}};Write-OwnedJson $script:ownedSettings
            $r=Invoke-OwnedSubmission;Assert ($r.Current -and $r.Submitted) 'same preparation completion keeps immutable request identity'
          }
          'settings-primary-missing'={
            [IO.File]::Delete($script:fixtureSettings);$r=Invoke-OwnedSubmission;Assert (-not $r.Current -and -not $r.Submitted) 'missing primary never grants launch'
          }
          'settings-primary-malformed'={
            [IO.File]::WriteAllText($script:fixtureSettings,'{"extensions":');$r=Invoke-OwnedSubmission;Assert (-not $r.Current -and -not $r.Submitted -and $r.Reason -ceq 'settings-read-error') 'malformed primary refuses with actual read phase'
          }
          'source-path-replaced'={
            $other=Join-Path $root 'other source';[IO.Directory]::CreateDirectory($other)|Out-Null;[IO.File]::WriteAllText((Join-Path $other 'yeman-settings.json'),($script:ownedSettings|ConvertTo-Json -Depth 15 -Compress));$env:YEMAN_STARTUP_SOURCE=Join-Path $other 'YeManSteam.bat'
            $r=Invoke-OwnedSubmission;Assert (-not $r.Current -and -not $r.Submitted -and $r.Reason -ceq 'startup-source-changed') 'another valid source path cannot replace captured settings'
          }
          'explicit-null-request'={
            $r=Start-Quiet $script:fixtureCmd '/d /c exit /b 0' -BootRequest $null;Assert (-not $r.Current -and -not $r.Submitted) 'explicit null request never falls back to generic Process.Start'
          }
          'diagnostic-throws-after-start'={
            $script:logThrows=$true;try{$r=Invoke-OwnedSubmission;Assert ($r.Current -and $r.Submitted) 'failed final diagnostic preserves actual Process.Start success'}finally{$script:logThrows=$false}
          }
          'os-start-failure'={
            $n=$script:kernelStarts;$r=Invoke-OwnedSubmission $true;Assert ($r.Current -and -not $r.Submitted -and $r.Reason -ceq 'process-start-error') 'creation failure remains distinct from revoked request';Assert ($script:kernelStarts -eq $n) 'failed creation has no owned child'
          }
          'property-order-and-whitespace'={
            [IO.File]::WriteAllText($script:fixtureSettings,'{ "theme":"other", "extensions":{"bootModes":{"preparation":{"observedBoot":"same-boot","enabled":true,"mode":"steam","revision":11,"schema":1},"bootEnabled":true,"boot":"steam","schema":1}}}')
            $r=Invoke-OwnedSubmission;Assert ($r.Current -and $r.Submitted) 'semantic source identity ignores JSON property order and whitespace'
          }
          'writer-change-during-mutex-wait'={
            $next=New-OwnedSettings;$next.extensions.bootModes.preparation.revision=12;Start-OwnedMutexHold 350 $next
            $r=Invoke-OwnedSubmission;Finish-OwnedMutexHold;Assert (-not $r.Current -and -not $r.Submitted -and $r.Reason -ceq 'startup-request-changed') 'writer mutation while waiting cannot use stale pre-lock settings'
          }
          'bounded-mutex-timeout'={
            Start-OwnedMutexHold 2500;$r=Invoke-OwnedSubmission;Finish-OwnedMutexHold;Assert (-not $r.Current -and -not $r.Submitted -and $r.Reason -ceq 'settings-lock-timeout') 'occupied original mutex has bounded refusal and no launch'
          }
          'flow-preparation-revokes-cold-start'={
            $script:flowTime=0;$script:flowWarm=0;$errorText=''
            $ports=@{StartupAdmission={Read-BootSteamAdmission -ExpectedRequest $script:bootSteamRequest};Now={$script:flowTime};Wait={$script:flowTime+=500};Log={param($E,$D)};Snapshot={[pscustomobject]@{Xbox=@();SteamRunning=$false;SteamWindow=$false}};CloseXbox={throw 'unexpected Xbox action'};NetworkAcceleration={$script:ownedSettings.extensions.bootModes.preparation.revision=12;Write-OwnedJson $script:ownedSettings};StartSteam={Request-Steam $false};ShowSteam={Request-Steam $true};InputWarmup={$script:flowWarm++};Affinity={throw 'unexpected affinity'}}
            $n=$script:kernelStarts;try{$null=Invoke-BootFlow $ports}catch{$errorText=$_.Exception.Message}
            Assert ($errorText -ceq 'BOOT_STEAM_REQUEST_EXPIRED' -and $script:kernelStarts -eq $n -and $script:flowTime -eq 0 -and $script:flowWarm -eq 0) 'source revoked during preparation stops cold action before observer/warmup'
          }
          'flow-late-show-cannot-revive'={
            $script:flowTime=0;$errorText='';$n=$script:kernelStarts
            $ports=@{StartupAdmission={Read-BootSteamAdmission -ExpectedRequest $script:bootSteamRequest};Now={$script:flowTime};Wait={$script:flowTime+=500};Log={param($E,$D)};Snapshot={[pscustomobject]@{Xbox=@();SteamRunning=($script:flowTime -gt 0);SteamWindow=$false}};CloseXbox={throw 'unexpected Xbox action'};StartSteam={Request-Steam $false};ShowSteam={Request-Steam $true};Affinity={throw 'unexpected affinity'};Checkpoint={param($Stage,$Snapshot)if($script:flowTime -ge 3000){$script:ownedSettings.extensions.bootModes.preparation.revision=12;Write-OwnedJson $script:ownedSettings}}}
            try{$null=Invoke-BootFlow $ports}catch{$errorText=$_.Exception.Message}
            Assert ($errorText -ceq 'BOOT_STEAM_REQUEST_EXPIRED' -and $script:kernelStarts -eq ($n+1) -and $script:flowTime -eq 3000) 'late show refuses changed request without a second actual start or observation replay'
          }
        }
        $selection=switch($CaseGroup){'final-contract'{@('healthy-no-resident-ymcc','flow-late-show-cannot-revive')};'final-healthy'{@('healthy-no-resident-ymcc')};'final-request'{@('new-selection-request')};'final-flow'{@('flow-preparation-revokes-cold-start','flow-late-show-cannot-revive')};default{@($selected.Keys)}}
        foreach($name in $selection){Invoke-FinalCase $name $selected[$name]}
    } catch {$failed=1;$failure=$_.Exception.Message}
    finally {
        if($events.ContainsKey('Stop')){$events.Stop.Set()|Out-Null}
        if($null -ne $script:probeHelper){try{if(-not $script:probeHelper.WaitForExit(4000)){$childReturnErrors.Add('owned-probe-still-running')}elseif($script:probeHelper.ExitCode -ne 0){$childReturnErrors.Add('owned-probe-exit-'+$script:probeHelper.ExitCode)}}finally{$script:probeHelper.Dispose()}}
        foreach($child in $script:fixtureChildren){if($null -ne $child.process){try{if($child.process.WaitForExit(2000)){$child.returned=$true;$child.exit=$child.process.ExitCode}else{$childReturnErrors.Add('owned-child-still-running-'+$child.pid)}}finally{$child.process.Dispose();$child.process=$null}}elseif(-not $child.returned){$childReturnErrors.Add('owned-child-return-unproven-'+$child.pid)}}
        foreach($event in $events.Values){$event.Dispose()}
        $env:YEMAN_STARTUP_SOURCE=$sourceBefore
    }
    if($childReturnErrors.Count){$failed=1;$failure+=('; '+($childReturnErrors -join '; '))}
    $assemblies=@([AppDomain]::CurrentDomain.GetAssemblies()|Where-Object {-not $_.IsDynamic -and $_.Location}|ForEach-Object {$_.Location}|Sort-Object -Unique)
    $modules=@(Get-Module|Where-Object {$_.Path}|ForEach-Object {$_.Path}|Sort-Object -Unique)
    $raw=[ordered]@{status=$(if($failed){'FAIL'}else{'PASS'});selection=$CaseGroup;cases=@($cases);checks=$script:checks;failed=$failed;error=$failure;kernelStarts=$script:kernelStarts;mutexProbes=$script:probeChecks;unheldStarts=$script:probeUnheld;ownedChildren=@($script:fixtureChildren);childReturnErrors=@($childReturnErrors);probeHelperReturned=($childReturnErrors -notcontains 'owned-probe-still-running');actualSettingsReader=$true;actualMutexAcrossProcesses=$true;actualProcessStart=$true;adaptations=@('one exact original settings mutex literal redirected to owned name','actual Process.Start observed by wrapper which calls real .NET Process.Start on exact owned cmd.exe only','original Steam argv recorded; only inert cmd execution substitutes finite /d /c exit /b 0');managedAssemblies=$assemblies;scriptModules=$modules;nativeLoaderGraphProven=$false;realSteamActions=0;defaultGroupExecuted=$false}
    $json=$raw|ConvertTo-Json -Depth 12
    $stream=[IO.File]::Open((Join-Path $root 'final-admission-result.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes($json);$stream.Write($bytes,0,$bytes.Length);$stream.Flush()}finally{$stream.Dispose()}
    $json
    exit $failed
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

# Read the real Start definition as syntax only; never acquire its named mutex.
$startDefinitions=@($definitions|Where-Object {$_.Name -eq 'Start-YeManBoot'})
Assert ($startDefinitions.Count -eq 1) 'one actual Start-YeManBoot definition is available for AST-only admission checks'
$startDefinition=$startDefinitions[0]
$startTry=@($startDefinition.Body.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})
Assert ($startTry.Count -eq 1) 'startup admission belongs to the original guarded Start body'
$startRead=@($startTry[0].Body.Statements|Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and
    $_.Left.Extent.Text -eq '$admission' -and $_.Right.Extent.Text -eq 'Read-BootSteamAdmission -CaptureRequest'
})
Assert ($startRead.Count -eq 1) 'Start reads actual settings directly before dispatch instead of relying only on the flow port'
$startDeny=@($startTry[0].Body.Statements|Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and
    $_.Clauses.Count -eq 1 -and $null -eq $_.ElseClause -and
    $_.Clauses[0].Item1.Extent.Text -eq '-not $admission.Allowed' -and
    $_.Clauses[0].Item2.Statements.Count -eq 1 -and
    $_.Clauses[0].Item2.Statements[0] -is [Management.Automation.Language.ReturnStatementAst] -and
    $_.Clauses[0].Item2.Statements[0].Pipeline.Extent.Text -eq '0'
})
Assert ($startDeny.Count -eq 1 -and $startRead[0].Extent.EndOffset -lt $startDeny[0].Extent.StartOffset) 'Start returns immediately on denied admission before any later startup action'
foreach($name in @('Find-SteamExe','Initialize-WindowApi','Initialize-BootProcessApi','Close-BootXbox','Start-BootNetworkAcceleration','Request-Steam','Invoke-BootFlow')) {
    $commands=@($startDefinition.Body.FindAll({
        param($node)
        $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq $name
    },$true))
    Assert ($commands.Count -gt 0) ('AST locates the original startup command '+$name)
    foreach($command in $commands) {
        Assert ($command.Extent.StartOffset -gt $startDeny[0].Extent.EndOffset) ('settings admission precedes '+$name+' in Start without executing it')
    }
}
Assert ($startTry[0].CatchClauses.Count -eq 1) 'startup read errors remain inside the original catch'
$startFailureLog=@($startTry[0].CatchClauses[0].Body.FindAll({
    param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Write-BootLog' -and
    $node.CommandElements.Count -ge 2 -and $node.CommandElements[1].Extent.Text -eq "'boot-error'"
},$true))
Assert ($startFailureLog.Count -eq 1) 'Start retains a failure log for malformed or unreadable settings without executing its catch'

function Assert-AdmissionFlow([string]$Label,[bool]$Allowed,[bool]$ReadThrows=$false) {
    $ports=New-Ports;$code=$null;$failure=$null
    try {$code=Invoke-BootFlow $ports} catch {$failure=$_}
    Assert ($script:sim.AdmissionReads -eq 1) ($Label+': flow consumes its actual admission port once')
    if($ReadThrows) {
        Assert ($null -ne $failure) ($Label+': malformed settings propagate a failure instead of granting startup')
    } else {
        Assert ($null -eq $failure -and $code -eq 0) ($Label+': allowed flow or intentional skip returns normally')
        Assert (@($script:sim.Events|Where-Object {$_ -eq 'log:boot-mode-admission'}).Count -eq 1) ($Label+': admission is recorded once')
    }
    if($Allowed) {
        Assert ($script:sim.Cold -eq 1 -and $script:sim.Shows -eq 0 -and $script:sim.Network -eq 1) ($Label+': admitted flow keeps one original cold dispatch and network request')
        Assert ($script:sim.Closes -eq 3 -and $script:sim.Kills.Count -eq 3 -and $script:sim.Focus -eq 1 -and $script:sim.Affinity -eq 1) ($Label+': admitted flow retains original cleanup, focus and affinity')
        Assert ($script:sim.Time -eq 60000 -and $script:sim.Snapshots -gt 0) ($Label+': admitted flow preserves the original bounded observation')
    } else {
        Assert ($script:sim.Cold -eq 0 -and $script:sim.Shows -eq 0 -and $script:sim.Closes -eq 0 -and $script:sim.Kills.Count -eq 0) ($Label+': denied flow has zero cold, show and close actions')
        Assert ($script:sim.Focus -eq 0 -and $script:sim.Network -eq 0 -and $script:sim.Affinity -eq 0 -and $script:sim.CSS -eq 0 -and $script:sim.YMCC -eq 0) ($Label+': denied flow has zero focus, network or companion actions')
        Assert ($script:sim.Time -eq 0 -and $script:sim.Snapshots -eq 0 -and -not $ports.ContainsKey('FocusState')) ($Label+': denied flow does not enter observation or create focus state')
        Assert (@($script:sim.Events|Where-Object {$_ -notlike 'log:*'}).Count -eq 0) ($Label+': denied flow records no effect events')
    }
}

$admissionCases=@(
    [pscustomobject]@{Name='no extensions';Json='{}';Allowed=$false;Mode='global';Reason='unconfigured-global'},
    [pscustomobject]@{Name='no bootModes';Json='{"extensions":{}}';Allowed=$false;Mode='global';Reason='unconfigured-global'},
    [pscustomobject]@{Name='null bootModes';Json='{"extensions":{"bootModes":null}}';Allowed=$false;Mode='global';Reason='unconfigured-global'},
    [pscustomobject]@{Name='empty bootModes';Json='{"extensions":{"bootModes":{}}}';Allowed=$false;Mode='global';Reason='unconfigured-global'},
    [pscustomobject]@{Name='no boot selection ignores current Steam';Json='{"extensions":{"bootModes":{"schema":1,"current":"steam","bootEnabled":true}}}';Allowed=$false;Mode='global';Reason='selected-non-steam-mode'}
)
foreach($mode in @('global','xbox','steam','xbox-steam')) {
    foreach($enabled in @($false,$true)) {
        $allowed=$enabled -and $mode -in @('steam','xbox-steam')
        $reason=if(-not $enabled){'disabled-scene-global'}elseif($allowed){'selected-steam-mode'}else{'selected-non-steam-mode'}
        $current=if($mode -in @('steam','xbox-steam')){'global'}else{'steam'}
        $settings=[ordered]@{extensions=[ordered]@{bootModes=[ordered]@{schema=1;boot=$mode;current=$current;bootEnabled=$enabled}};future=[ordered]@{keep='unchanged'}}
        $admissionCases+=[pscustomobject]@{Name=($mode+' enabled='+$enabled);Json=($settings|ConvertTo-Json -Depth 6 -Compress);Allowed=[bool]$allowed;Mode=$(if($enabled){$mode}else{'global'});Reason=$reason}
    }
}
foreach($case in $admissionCases) {
    Reset-Flow
    $script:sim.AdmissionSettings=$case.Json|ConvertFrom-Json
    $observed=Get-BootSteamAdmission $script:sim.AdmissionSettings
    Assert ($observed.Allowed -is [bool] -and $observed.Allowed -eq $case.Allowed -and $observed.Mode -ceq $case.Mode -and $observed.Reason -ceq $case.Reason) ($case.Name+': actual settings resolver matches the four-mode admission contract')
    Assert-AdmissionFlow $case.Name $case.Allowed
}
$malformedAdmissionCases=@(
    [pscustomobject]@{Name='null settings';Json='null'},
    [pscustomobject]@{Name='array settings';Json='[]'},
    [pscustomobject]@{Name='single object array settings';Json='[{}]'},
    [pscustomobject]@{Name='Steam document array settings';Json='[{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":true}}}]'},
    [pscustomobject]@{Name='scalar settings';Json='"invalid"'},
    [pscustomobject]@{Name='null extensions';Json='{"extensions":null}'},
    [pscustomobject]@{Name='array extensions';Json='{"extensions":[]}'},
    [pscustomobject]@{Name='scalar bootModes';Json='{"extensions":{"bootModes":true}}'},
    [pscustomobject]@{Name='array bootModes';Json='{"extensions":{"bootModes":[]}}'},
    [pscustomobject]@{Name='missing schema';Json='{"extensions":{"bootModes":{"boot":"steam","bootEnabled":true}}}'},
    [pscustomobject]@{Name='future schema';Json='{"extensions":{"bootModes":{"schema":2,"boot":"steam","bootEnabled":true}}}'},
    [pscustomobject]@{Name='string schema';Json='{"extensions":{"bootModes":{"schema":"1","boot":"steam","bootEnabled":true}}}'},
    [pscustomobject]@{Name='boolean schema';Json='{"extensions":{"bootModes":{"schema":true,"boot":"steam","bootEnabled":true}}}'},
    [pscustomobject]@{Name='string false enabled';Json='{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":"false"}}}'},
    [pscustomobject]@{Name='numeric enabled';Json='{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":1}}}'},
    [pscustomobject]@{Name='null enabled';Json='{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":null}}}'},
    [pscustomobject]@{Name='unknown boot';Json='{"extensions":{"bootModes":{"schema":1,"boot":"unknown","bootEnabled":true}}}'},
    [pscustomobject]@{Name='wrong-case boot';Json='{"extensions":{"bootModes":{"schema":1,"boot":"Steam","bootEnabled":true}}}'},
    [pscustomobject]@{Name='numeric boot';Json='{"extensions":{"bootModes":{"schema":1,"boot":3,"bootEnabled":true}}}'}
)
foreach($case in $malformedAdmissionCases) {
    Reset-Flow
    # PS5 pipeline enumeration must not change the root type in this pure call.
    # The real file group below supplies the original JSON to the actual reader.
    if($case.Json.TrimStart().StartsWith('[')) {
        $script:sim.AdmissionSettings=@($case.Json|ConvertFrom-Json)
        Assert ($script:sim.AdmissionSettings -is [array]) ($case.Name+': pure admission receives the original array root type')
    } else {
        $script:sim.AdmissionSettings=$case.Json|ConvertFrom-Json
    }
    $failure=$null
    try {$null=Get-BootSteamAdmission $script:sim.AdmissionSettings} catch {$failure=$_}
    Assert ($null -ne $failure) ($case.Name+': malformed admission data is rejected')
    Assert-AdmissionFlow $case.Name $false $true
}

# Only isolated fixture settings are created. No BAT, app or native API is run.
$settingsDirectory=Join-Path ([IO.Path]::GetFullPath($Evidence)) 'BAT adjacent settings with spaces'
Assert (-not [IO.Directory]::Exists($settingsDirectory)) 'isolated settings fixture directory must be fresh'
[IO.Directory]::CreateDirectory($settingsDirectory)|Out-Null
$settingsFile=Join-Path $settingsDirectory 'yeman-settings.json'
$settingsBackup=$settingsFile+'.bak'
$sourceBefore=[Environment]::GetEnvironmentVariable('YEMAN_STARTUP_SOURCE','Process')
try {
    $env:YEMAN_STARTUP_SOURCE=Join-Path $settingsDirectory 'YeManSteam.bat'
    Assert ((Get-BootSettingsPath) -ceq $settingsFile) 'settings path comes from the BAT adjacent directory including spaces'
    # An old ON backup must not turn a missing or malformed primary into permission.
    $backupBytes=[Text.UTF8Encoding]::new($false).GetBytes('{"extensions":{"bootModes":{"schema":1,"boot":"steam","bootEnabled":true}},"backupSentinel":"preserve"}')
    [IO.File]::WriteAllBytes($settingsBackup,$backupBytes)
    $missing=Read-BootSteamAdmission
    Assert (-not $missing.Allowed -and $missing.Mode -ceq 'global' -and $missing.Reason -ceq 'settings-missing-global') 'missing adjacent settings inherit global and cannot authorize Steam from a backup'
    Reset-Flow;$script:sim.AdmissionReader={Read-BootSteamAdmission}
    Assert-AdmissionFlow 'missing adjacent settings' $false
    Assert (-not [IO.File]::Exists($settingsFile)) 'missing settings are not initialized by admission'
    foreach($case in $admissionCases) {
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($case.Json)
        [IO.File]::WriteAllBytes($settingsFile,$bytes)
        $observed=Read-BootSteamAdmission
        Assert ($observed.Allowed -eq $case.Allowed -and $observed.Mode -ceq $case.Mode -and $observed.Reason -ceq $case.Reason) ($case.Name+': actual reader uses the BAT adjacent settings')
        Reset-Flow;$script:sim.AdmissionReader={Read-BootSteamAdmission}
        Assert-AdmissionFlow ('adjacent file '+$case.Name) $case.Allowed
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsFile)) -ceq [Convert]::ToBase64String($bytes)) ($case.Name+': reading and flow preserve every settings byte')
    }
    foreach($case in $malformedAdmissionCases) {
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($case.Json)
        [IO.File]::WriteAllBytes($settingsFile,$bytes)
        $failure=$null
        try {$null=Read-BootSteamAdmission} catch {$failure=$_}
        Assert ($null -ne $failure) ($case.Name+': actual file reader rejects malformed data')
        Reset-Flow;$script:sim.AdmissionReader={Read-BootSteamAdmission}
        Assert-AdmissionFlow ('malformed adjacent file '+$case.Name) $false $true
        Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsFile)) -ceq [Convert]::ToBase64String($bytes)) ($case.Name+': malformed settings bytes remain unchanged')
    }
    $badJson=[Text.UTF8Encoding]::new($false).GetBytes('{"extensions":{"bootModes":')
    [IO.File]::WriteAllBytes($settingsFile,$badJson)
    $entriesBefore=@([IO.Directory]::GetFileSystemEntries($settingsDirectory)|Sort-Object)
    $failure=$null
    try {$null=Read-BootSteamAdmission} catch {$failure=$_}
    Assert ($null -ne $failure) 'bad JSON cannot grant startup through the actual reader'
    Reset-Flow;$script:sim.AdmissionReader={Read-BootSteamAdmission}
    Assert-AdmissionFlow 'bad adjacent JSON' $false $true
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsFile)) -ceq [Convert]::ToBase64String($badJson)) 'bad JSON remains byte-identical without repair or truncation'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsBackup)) -ceq [Convert]::ToBase64String($backupBytes)) 'admission preserves the old backup without replacing or replaying it'
    Assert ((@([IO.Directory]::GetFileSystemEntries($settingsDirectory)|Sort-Object) -join '|') -ceq ($entriesBefore -join '|')) 'admission creates, deletes or renames no settings files on bad JSON'
    $env:YEMAN_STARTUP_SOURCE=$null
    $failure=$null
    try {$null=Get-BootSettingsPath} catch {$failure=$_}
    Assert ($null -ne $failure) 'unknown BAT source cannot fall back to another settings directory'
} finally {
    [Environment]::SetEnvironmentVariable('YEMAN_STARTUP_SOURCE',$sourceBefore,'Process')
}
[ordered]@{status='PASS';checks=$script:checks;consumer='actual startup controller; CSS exclusively delegated to YMCC';effects='memory-only effect terminals; isolated settings files';secondBatExecuted=$false;applicationsStartedOrStopped=$false;systemSettingsChanged=$false;admissionCases=$admissionCases.Count;malformedAdmissionCases=$malformedAdmissionCases.Count;startEntry='AST-only; never invoked';isolatedSettingsPreserved=$true}|ConvertTo-Json -Depth 4|Tee-Object -FilePath (Join-Path $Evidence 'selftest-result.json')