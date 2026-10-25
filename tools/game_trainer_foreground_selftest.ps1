[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BackendPath='')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
if(-not $BackendPath){$BackendPath=Join-Path $repo 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
[IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Check([string]$name,[scriptblock]$body){try{& $body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function Fixture([scriptblock]$test){& $module {param($test,$ValidationRoot)
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
 & ([scriptblock]::Create($test.ToString())) $plan
} $test $ValidationRoot}
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
  function Test-TrainerSteamForeground($AppDirectory){return $false}
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
Check 'old-front-end compatibility requires a real YMCC installation hint' {
 $value=& $module {Test-TrainerSteamForeground ''};Assert (-not $value)
}
$failed=@($checks|Where-Object{-not $_.ok});$report=@{passed=($failed.Count -eq 0);passedChecks=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();realProcessLaunches=0;realRegistryWrites=0;realForegroundChanges=0}
[IO.File]::WriteAllText((Join-Path $ValidationRoot 'trainer-foreground-unit.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ('Trainer foreground unit: '+($checks.Count-$failed.Count)+'/'+$checks.Count)
if($failed.Count){exit 1}
