[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BackendPath='',[string]$CaseFilter='')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
if(-not $BackendPath){$BackendPath=Join-Path $repo 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
[IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new();$skipped=[Collections.Generic.List[string]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Check([string]$name,[scriptblock]$body){if($CaseFilter -and -not $name.Contains($CaseFilter)){$skipped.Add($name);return};try{& $body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function Fixture([scriptblock]$test,$FixtureOptions=$null){& $module {param($test,$ValidationRoot,$FixtureOptions)
 $script:calls=0;$script:gets=0;$script:releases=0;$script:finds=0;$script:foreground=$true;$script:throwDispatch=$false;$script:missing=$false;$script:knownProcess=$true
 $process=New-Object PSObject -Property @{Id=42;HasExited=$false;MainWindowHandle=[IntPtr]1};$process|Add-Member ScriptMethod Refresh {}
 $script:fixtureProcess=$process
 $shell=New-Object PSObject
 $shell|Add-Member ScriptMethod ShellExecute {param($program,$arguments,$directory,$verb,$show)$script:calls++;$script:actual=@($program,$arguments,$directory,$verb,$show);if($script:throwDispatch){throw 'reply lost after Shell accepted'}}
 $script:fixtureShell=$shell
 function Get-TrainerDesktopShell {$script:gets++;if($script:missing){throw 'no desktop Shell'};return $script:fixtureShell}
 function Close-TrainerDesktopShell($Shell){$script:releases++}
 function Find-TrainerRunningProcess($Program){return $null}
 function Find-TrainerCanonicalRunningProcess($Program){return $null}
 function Find-TrainerShellProcess($Program,$NotBefore){$script:finds++;if($script:knownProcess){return $script:fixtureProcess};return $null}
 function Get-TrainerProcessForeground($ProcessId){return $script:foreground}
 function Test-TrainerProcessWindowReady($ProcessId){return $true}
 function Show-TrainerProcess($Process){$script:focusRequests++}
 function Start-Sleep {param($Milliseconds)}
 $never=Join-Path $ValidationRoot ('nonexistent-'+[guid]::NewGuid().ToString('N')+'.exe');if([IO.File]::Exists($never)){throw 'Unsafe fixture path'}
 $plan=@{program=$never;arguments='"C:\Fixture\table.ct"';workingDirectory='C:\Fixture';verb='runas';safePath=$false;mode='Trainer'}
 & ([scriptblock]::Create($test.ToString())) $plan $FixtureOptions
} $test $ValidationRoot $FixtureOptions}
Check 'interactive path commits exactly one Shell launch and preserves every argument and elevation setting' {
 Fixture {param($plan)$r=Start-TrainerLaunchPlan $plan -InteractiveShell;if($script:calls -ne 1 -or $r.pid -ne 42 -or $r.launchBackend -ne 'desktop-shell' -or $r.window -ne 'foreground' -or $script:releases -ne 1){throw 'one Shell launch receipt'};if($script:actual[0] -cne $plan.program -or $script:actual[1] -cne $plan.arguments -or $script:actual[2] -cne $plan.workingDirectory -or $script:actual[3] -cne 'runas' -or $script:actual[4] -ne 1){throw 'launch arguments changed'}}
}
Check 'uncertain Shell reply never falls through to a second Process.Start' {
 Fixture {param($plan)$script:throwDispatch=$true;$failed=$false;try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$failed=$_.Exception.Message -like '*不会重复启动*'};if(-not $failed -or $script:calls -ne 1 -or $script:releases -ne 1){throw 'uncertain dispatch replayed'}}
}
Check 'missing process acknowledgement has one bounded observation period and no replay' {
 Fixture {param($plan)$script:knownProcess=$false;$failed=$false;try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$failed=$_.Exception.Message -like '*不会重复启动*'};if(-not $failed -or $script:calls -ne 1 -or $script:finds -ne 25){throw 'bounded missing process receipt'}}
}
Check 'process success and real foreground success are distinct receipts' {
 Fixture {param($plan)$script:foreground=$false;$r=Start-TrainerLaunchPlan $plan -InteractiveShell;if($r.pid -ne 42 -or $r.window -ne 'unconfirmed' -or $r.notice -notlike '*前台窗口尚未确认*' -or $script:calls -ne 1){throw 'foreground falsely acknowledged'}}
 foreach($stage in @('process','window','foreground')){
  foreach($fault in @('cancel','binding','terminal','target','healthy')){
   Fixture {param($plan,$case)
    $script:focusRequests=0;$script:postStage=$case.stage;$script:postFault=$case.fault;$script:postTargetChanged=$false;$script:postTriggered=$false
    $request=Join-Path $ValidationRoot ('post-dispatch-'+[guid]::NewGuid().ToString('N')+'.json');$script:postRequest=$request
    $binding=@{requestProtocol=1;requestToken='post-dispatch-token';sourceSession='post-dispatch-source'}
    $pending=@{requestClaimed=$true;pending=$true;requestId='launch-post-dispatch-fixture'};foreach($key in $binding.Keys){$pending[$key]=$binding[$key]};Write-TrainerJson $request $pending
    $plan.lifecycleGuard=@{resultPath=$request;requestId=$pending.requestId;binding=$binding;path='';pid=0;created='';sha256=''}
    function Assert-TrainerLaunchTarget {param($ExecutablePath,$ProcessId,$Created,$ExpectedSha256)if($script:postTargetChanged){throw '目标游戏已退出或更换'}}
    function Set-PostDispatchFault([string]$Stage) {
     if($Stage -cne $script:postStage -or $script:postTriggered){return};$script:postTriggered=$true
     switch($script:postFault){
      'cancel'{[IO.File]::WriteAllText(($script:postRequest+'.cancel'),'cancel')}
      'binding'{$v=Read-TrainerJson $script:postRequest;$v.requestToken='replaced';Write-TrainerJson $script:postRequest $v}
      'terminal'{$v=Read-TrainerJson $script:postRequest;$v.pending=$false;Write-TrainerJson $script:postRequest $v}
      'target'{$script:postTargetChanged=$true}
     }
    }
    function Find-TrainerShellProcess($Program,$NotBefore){$script:finds++;Set-PostDispatchFault 'process';return $script:fixtureProcess}
    function Get-TrainerProcessForeground($ProcessId){Set-PostDispatchFault 'foreground';return $script:postStage -eq 'foreground'}
    function Test-TrainerProcessWindowReady($ProcessId){Set-PostDispatchFault 'window';return $true}
    $r=Start-TrainerLaunchPlan $plan -InteractiveShell
    if(-not $script:postTriggered -or $r.pid -ne 42 -or $r.launchBackend -cne 'desktop-shell' -or $script:calls -ne 1 -or $script:releases -ne 1){throw 'Post-dispatch continuation lost its actual process or replayed the Shell action'}
    if($script:postFault -eq 'healthy'){
     $expectedFocus=if($script:postStage -eq 'foreground'){0}else{1}
     if($script:focusRequests -ne $expectedFocus -or $r.focusRequested -ne ($expectedFocus -eq 1) -or $r.window -cne $(if($script:postStage -eq 'foreground'){'foreground'}else{'unconfirmed'})){throw 'Current post-dispatch request lost its normal foreground behavior'}
    }elseif($script:focusRequests -ne 0 -or $r.focusRequested -ne $false -or $r.window -cne 'unconfirmed' -or $r.notice -notlike '*停止窗口*'){
     throw ('Retired '+$script:postStage+'/'+$script:postFault+' still focused or acknowledged a current foreground: focuses='+$script:focusRequests+' window='+$r.window)
    }
    # Consume the actual returned receipt through the original durable request
    # producer: an already-created PID must survive its unsuccessful UI result.
    $script:postReceipt=$r;$script:postTargetChanged=$false
    $cache=Join-Path $ValidationRoot ('post-producer-'+[guid]::NewGuid().ToString('N'));$id='launch-post-producer-fixture';$result=Join-Path $cache ('requests/'+$id+'.json')
    Write-TrainerJson $result @{requestProtocol=1;requestToken='post-producer-token';sourceSession='post-producer-source';gameExecutablePath='';gameProcessId=0;gameProcessCreated='';requestId=$id;ok=$false;pending=$true}
    function Resolve-TrainerLaunchPlan {param($Mode,$TrainerPath,$TrainerFolder,$ExpectedSha256,$TrainerGameName,$TrainerOrigin,$GcmResourceId,$AppDirectory,$GcmSettingsRoot,$GcmDefaultRoot)return @{program='inert-post-dispatch-contract';arguments='';trainer=$null}}
    function Start-TrainerLaunchPlan {param($Plan,[switch]$InteractiveShell)return $script:postReceipt}
    function Test-TrainerBackgroundOwner($AppDirectory){return $false}
    Invoke-TrainerLaunch -Mode Trainer -CacheRoot $cache -ResultPath $result -RequestId $id -InteractiveShell
    $published=Read-TrainerJson $result
    if($published.pid -ne 42 -or $published.launched -ne $true -or $published.requestToken -cne 'post-producer-token' -or $published.pending -ne $false -or $published.requestClaimed -ne $true -or $published.focusRequested -ne $r.focusRequested -or $published.window -cne $r.window){throw 'Durable producer lost the real launched process or exact request binding'}
    if($script:postFault -ne 'healthy' -and ($published.ok -ne $false -or $published.code -cne 'trainer-window-unconfirmed' -or $published.error -notlike '*停止窗口*')){throw 'Retired launch was misreported as a successful current foreground'}
   } @{stage=$stage;fault=$fault}
  }
 }
}
Check 'same program singleton is restored through the original owner without any Shell launch' {
 Fixture {param($plan)$plan.arguments='';function Find-TrainerRunningProcess($Program){return $script:fixtureProcess};function Show-TrainerProcess($Process){$script:shown=$Process.Id};$r=Start-TrainerLaunchPlan $plan -InteractiveShell;if(-not $r.reused -or $r.pid -ne 42 -or $script:shown -ne 42 -or $script:calls -ne 0 -or $script:gets -ne 0){throw 'singleton duplicated'}}
}
Check 'compatibility alias reuses the same final executable before any Shell dispatch' {
 Fixture {param($plan)$plan.arguments='';function Find-TrainerCanonicalRunningProcess($Program){return $script:fixtureProcess};function Show-TrainerProcess($Process){$script:shown=$Process.Id};$r=Start-TrainerLaunchPlan $plan -InteractiveShell;if(-not $r.reused -or $r.pid -ne 42 -or $script:shown -ne 42 -or $script:calls -ne 0 -or $script:gets -ne 0){throw 'compatibility alias duplicated a live process'}}
}
Check 'legacy launch never touches desktop Shell' {
 Fixture {param($plan)$failed=$false;try{Start-TrainerLaunchPlan $plan|Out-Null}catch{$failed=$true};if(-not $failed -or $script:gets -ne 0 -or $script:calls -ne 0){throw 'legacy path was changed'}}
}
Check 'Shell acquisition failure may use the original launcher before any dispatch only' {
 Fixture {param($plan)$script:missing=$true;$failed=$false;try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$failed=$true};if(-not $failed -or $script:gets -ne 1 -or $script:calls -ne 0 -or $script:releases -ne 0){throw 'invalid pre-dispatch fallback'}}
}
Check 'production receipt never reports foreground success for an already-started background process' {
 & $module {param($scope)
  function Resolve-TrainerLaunchPlan {param($Mode,$TrainerPath,$TrainerFolder,$ExpectedSha256,$TrainerGameName,$TrainerOrigin,$GcmResourceId,$AppDirectory,$GcmSettingsRoot,$GcmDefaultRoot)return @{program='inert-contract';arguments='';trainer=$null}}
  function Assert-TrainerLaunchTarget {param($ExecutablePath,$ProcessId,$Created,$ExpectedSha256)}
  function Start-TrainerLaunchPlan {param($Plan,[switch]$InteractiveShell)return @{pid=42;reused=$false;launchBackend='desktop-shell';window='unconfirmed';focusRequested=$true;notice='process started, window unconfirmed'}}
  $cache=Join-Path $scope 'receipt-contract';$id='launch-window-contract-123456';$result=Join-Path $cache ('requests/'+$id+'.json')
  Invoke-TrainerLaunch -Mode Trainer -CacheRoot $cache -ResultPath $result -RequestId $id -InteractiveShell
  $receipt=Read-TrainerJson $result
  if($receipt.ok -or -not $receipt.launched -or $receipt.pid -ne 42 -or $receipt.code -ne 'trainer-window-unconfirmed'){throw 'background process falsely reported as foreground success'}
 } $ValidationRoot
}
Check 'background owner enables the route while visible or unknown owners keep the original launcher' {
 & $module {
  foreach($state in @(@{found=$true;visible=$false},@{found=$true;visible=$true},@{found=$false;visible=$false})){
   $script:ownerFixture=$state
   function Get-TrainerLaunchOwnerState($AppDirectory){return $script:ownerFixture}
   $actual=Test-TrainerBackgroundOwner 'inert-exact-owner'
   if($actual -ne ($state.found -and -not $state.visible)){throw 'visible or unrelated owner changed startup mode'}
  }
 }
}
Check 'cancelled lifecycle cannot dispatch a trainer or reuse a window' {
 Fixture {param($plan)
  $request=Join-Path $ValidationRoot ('cancel-'+[guid]::NewGuid().ToString('N')+'.json');[IO.File]::WriteAllText(($request+'.cancel'),'cancel')
  $plan.lifecycleGuard=@{resultPath=$request};$failed=$false
  try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$failed=$_.Exception.Message -like '*已取消*'}
  if(-not $failed -or $script:calls -ne 0 -or $script:gets -ne 0){throw 'Cancelled request reached a launch effect'}
 }
 foreach($kind in @('Trainer','Manager','CheatEngine')){
  Fixture {param($plan,$options)
   $names=@('Resolve-TrainerLaunchPlan','Assert-TrainerLaunchTarget','Start-TrainerLaunchPlan','Test-TrainerBackgroundOwner');$originals=@{}
   foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
   $script:preCancelEffects=0
   Set-Item Function:script:Resolve-TrainerLaunchPlan {$script:preCancelEffects++;return @{program='fixture-never-executed.exe';arguments='';trainer=$null}}
   Set-Item Function:script:Assert-TrainerLaunchTarget {}
   Set-Item Function:script:Start-TrainerLaunchPlan {$script:preCancelEffects++;return @{pid=42;reused=$false}}
   Set-Item Function:script:Test-TrainerBackgroundOwner {return $false}
   try{
    $cache=Join-Path $ValidationRoot ('precancel-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory((Join-Path $cache 'requests'))|Out-Null
    $id='precancel-'+$options.mode.ToLowerInvariant();$request=Join-Path $cache ('requests/'+$id+'.json')
    $binding=@{requestProtocol=1;requestToken='pre-cancel-token-0001';sourceSession='pre-cancel-source-0001';gameExecutablePath='';gameProcessId=0;gameProcessCreated='';requestId=$id;ok=$false;pending=$true}
    Write-TrainerJson $request $binding;[IO.File]::WriteAllText(($request+'.cancel'),'cancel')
    Invoke-TrainerLaunch -Mode $options.mode -CacheRoot $cache -ResultPath $request -RequestId $id
    $receipt=Read-TrainerJson $request
    if($receipt.ok -ne $false -or $receipt.error -notlike '*已取消*' -or $script:preCancelEffects -ne 0){throw ('Pre-cancelled '+$options.mode+' reached program/configuration resolution')}
    if($receipt.requestClaimed -ne $true -or $receipt.requestToken -cne $binding.requestToken -or $receipt.sourceSession -cne $binding.sourceSession){throw 'Pre-cancelled request lost its claimed identity'}
    $rejected=$false;try{Invoke-TrainerLaunch -Mode $options.mode -CacheRoot $cache -ResultPath $request -RequestId $id}catch{$rejected=$true}
    if(-not $rejected -or $script:preCancelEffects -ne 0){throw 'Pre-cancelled request replay reached plan resolution'}
    $freshId=$id+'-fresh';$fresh=Join-Path $cache ('requests/'+$freshId+'.json');$next=@{};foreach($key in $binding.Keys){$next[$key]=$binding[$key]};$next.requestId=$freshId;$next.requestToken='fresh-cancel-token-0002'
    Write-TrainerJson $fresh $next
    Invoke-TrainerLaunch -Mode $options.mode -CacheRoot $cache -ResultPath $fresh -RequestId $freshId
    if((Read-TrainerJson $fresh).ok -ne $true -or $script:preCancelEffects -ne 2){throw 'A fresh explicit request could not recover from the cancelled predecessor'}
    if(-not [IO.File]::Exists(($request+'.cancel'))){throw 'New request removed the old cancellation'}
   }finally{foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
  } @{mode=$kind}
 }
}
Check 'claimed launch continuation accepts its exact durable binding before one dispatch' {
 Fixture {param($plan)
  $request=Join-Path $ValidationRoot ('bound-'+[guid]::NewGuid().ToString('N')+'.json');$binding=@{requestProtocol=1;requestToken='fixture-token';sourceSession='fixture-source'}
  $pending=@{requestClaimed=$true;pending=$true;requestId='launch-bound-fixture'};foreach($key in $binding.Keys){$pending[$key]=$binding[$key]};Write-TrainerJson $request $pending
  $plan.lifecycleGuard=@{resultPath=$request;requestId=$pending.requestId;binding=$binding;path='';pid=0;created='';sha256=''}
  $receipt=Start-TrainerLaunchPlan $plan -InteractiveShell
  if($script:calls -ne 1 -or $script:releases -ne 1 -or $receipt.pid -ne 42){throw 'Exact claimed continuation was not dispatched once'}
 }
}
Check 'changed claimed bindings or terminal requests cannot dispatch again' {
 Fixture {param($plan)
  foreach($fault in @('token','protocol-type','pending','claimed')){
   $request=Join-Path $ValidationRoot ('changed-'+[guid]::NewGuid().ToString('N')+'.json');$binding=@{requestProtocol=1;requestToken='fixture-token';sourceSession='fixture-source'}
   $pending=@{requestClaimed=$true;pending=$true;requestId='launch-changed-fixture'};foreach($key in $binding.Keys){$pending[$key]=$binding[$key]}
   switch($fault){'token'{$pending.requestToken='changed'} 'protocol-type'{$pending.requestProtocol='1'} 'pending'{$pending.pending=$false} 'claimed'{$pending.requestClaimed=$false}}
   Write-TrainerJson $request $pending;$plan.lifecycleGuard=@{resultPath=$request;requestId=$pending.requestId;binding=$binding;path='';pid=0;created='';sha256=''}
   $failed=$false;try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$failed=$_.Exception.Message -match '绑定|声明'}
   if(-not $failed -or $script:calls -ne 0){throw 'Changed claim reached dispatch'}
  }
 }
}
Check 'late cancellation after Shell lookup releases it without dispatch' {
 Fixture {param($plan)
  $request=Join-Path $ValidationRoot ('late-'+[guid]::NewGuid().ToString('N')+'.json');$script:lateCancel=$request+'.cancel'
  $plan.lifecycleGuard=@{resultPath=$request;requestId='launch-late-cancel-fixture';binding=@{};path='';pid=0;created='';sha256=''}
  Write-TrainerJson $request @{requestClaimed=$true;pending=$true;requestId='launch-late-cancel-fixture'}
  function Get-TrainerDesktopShell {$script:gets++;[IO.File]::WriteAllText($script:lateCancel,'cancel');return $script:fixtureShell}
  $failed=$false;$why='';try{Start-TrainerLaunchPlan $plan -InteractiveShell|Out-Null}catch{$why=$_.Exception.Message;$failed=$why -like '*已取消*'}
  if(-not $failed -or $script:calls -ne 0 -or $script:releases -ne 1){throw ('Late cancellation dispatched or leaked Shell ownership: calls='+$script:calls+' releases='+$script:releases+' reason='+$why)}
 }
 foreach($lookup in @('direct','canonical')){
  foreach($fault in @('cancel','binding','terminal','target','healthy')){
   Fixture {param($plan,$case)
    $plan.arguments='';$script:focusRequests=0
    $request=Join-Path $ValidationRoot ('reuse-'+[guid]::NewGuid().ToString('N')+'.json')
    $script:reuseRequest=$request;$script:reuseFault=$case.fault;$script:reuseLookup=$case.lookup;$script:reuseTargetChanged=$false
    $binding=@{requestProtocol=1;requestToken='reuse-fixture-token';sourceSession='reuse-fixture-source'}
    $pending=@{requestClaimed=$true;pending=$true;requestId='launch-reuse-cancel-fixture'};foreach($key in $binding.Keys){$pending[$key]=$binding[$key]};Write-TrainerJson $request $pending
    $plan.lifecycleGuard=@{resultPath=$request;requestId=$pending.requestId;binding=$binding;path='';pid=0;created='';sha256=''}
    function Assert-TrainerLaunchTarget {param($ExecutablePath,$ProcessId,$Created,$ExpectedSha256)if($script:reuseTargetChanged){throw '目标游戏已退出或更换'}}
    function Set-ReuseLookupFault {
     switch($script:reuseFault){
      'cancel'{[IO.File]::WriteAllText(($script:reuseRequest+'.cancel'),'cancel')}
      'binding'{$v=Read-TrainerJson $script:reuseRequest;$v.requestToken='replaced';Write-TrainerJson $script:reuseRequest $v}
      'terminal'{$v=Read-TrainerJson $script:reuseRequest;$v.pending=$false;Write-TrainerJson $script:reuseRequest $v}
      'target'{$script:reuseTargetChanged=$true}
     }
    }
    function Find-TrainerRunningProcess($Program){if($script:reuseLookup -eq 'direct'){Set-ReuseLookupFault;return $script:fixtureProcess};return $null}
    function Find-TrainerCanonicalRunningProcess($Program){if($script:reuseLookup -eq 'canonical'){Set-ReuseLookupFault;return $script:fixtureProcess};return $null}
    $failed=$false;$receipt=$null;try{$receipt=Start-TrainerLaunchPlan $plan -InteractiveShell}catch{$failed=$_.Exception.Message -match '取消|绑定|声明|更换'}
    if($script:reuseFault -eq 'healthy'){
     if($failed -or -not $receipt.reused -or $receipt.pid -ne 42 -or $script:focusRequests -ne 1 -or $script:calls -ne 0 -or $script:gets -ne 0){throw 'Current exact claimed request lost healthy singleton reuse'}
    }elseif(-not $failed -or $script:focusRequests -ne 0 -or $script:calls -ne 0 -or $script:gets -ne 0){throw ('Late '+$script:reuseLookup+'/'+$script:reuseFault+' reused a window after its authority changed: focuses='+$script:focusRequests+' shell='+$script:calls)}
   } @{fault=$fault;lookup=$lookup}
  }
 }
}
Check 'worker cannot rediscover Steam for old-front-end compatibility' {
 $value=& $module {Get-Command Test-TrainerSteamForeground -CommandType Function -ErrorAction SilentlyContinue};Assert (-not $value)
}
if($checks.Count -eq 0){throw 'No original foreground cases match the requested filter'}
$failed=@($checks|Where-Object{-not $_.ok});$report=@{passed=($failed.Count -eq 0);passedChecks=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();caseFilter=$CaseFilter;skipped=$skipped.ToArray();realProcessLaunches=0;realRegistryWrites=0;realForegroundChanges=0}
[IO.File]::WriteAllText((Join-Path $ValidationRoot 'trainer-foreground-unit.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ('Trainer foreground unit: '+($checks.Count-$failed.Count)+'/'+$checks.Count)
if($failed.Count){exit 1}
