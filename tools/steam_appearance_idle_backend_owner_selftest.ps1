[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'
foreach($spec in @(
 @{path='steam-appearance.ps1';names=@('Test-BackendOwner')},
 @{path='steam-appearance/idle.ps1';names=@('Invoke-AppearanceIdle','Stop-AppearanceIdle','Test-AppearanceIdleSession')},
 @{path='steam-appearance/resident.ps1';names=@('Assert-SingleAppearanceBackend')}
)){
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root $spec.path),[ref]$tokens,[ref]$errors)
 if($errors.Count){throw $errors[0]}
 foreach($name in $spec.names){
  $f=@($ast.EndBlock.Statements|Where-Object Name -eq $name)
  if($f.Count -ne 1){throw ('Missing production function: '+$name)}
  Invoke-Expression $f[0].Extent.Text
 }
}
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:BackendExe='C:\fixture-css\CssLoader-Standalone-Headless.exe'
function New-Backend([uint32]$Id,[uint32]$Parent,[int]$Second){
 [pscustomobject]@{Id=$Id;Parent=$Parent;StartTime=([datetime]'2026-11-18T12:00:00Z').AddSeconds($Second);HasExited=$false;User=$true;Path=$script:BackendExe}
}
function New-Listener([uint32]$Id,[string]$Address='127.0.0.1'){[pscustomobject]@{OwningProcess=$Id;LocalAddress=$Address}}
function Get-BackendProcesses {$script:Family}
function Get-AppearanceTcpListeners([int]$LocalPort){if($LocalPort -ne 35821){throw 'Unexpected port'};$script:PortReads++;$script:Listeners}
function Get-Process {param([uint32]$Id,$ErrorAction) if(!$Id){throw 'PRIVATE_PROCESS_SCAN_DENIED'};$script:PidReads++;$script:Current[$Id]}
function Test-CurrentUserProcess($Process){$Process.User}
function Get-AppearanceProcessPath($Process){$Process.Path}
function Get-AppearanceParentId($Process){$Process.Parent}
function Assert-CssDesktopNotEditing {}
function Get-AppearanceSteamSession {[pscustomobject]@{pid=123;birth='456';root='C:\fixture-steam'}}
function Get-AppearanceStatus {[pscustomobject]@{backendReady=$true;configuredMatchesBackend=$true;packPrepared=$true;revision='test-revision';targetId='test-target'}}
function Test-AppearanceRendered($Session,$Status){$true}
function Get-ThemeRows {param([switch]$RevisionOnly) @()}
function Get-ThemeRevision($Rows){'test-revision'}
function Get-ThemeTargetId {'test-target'}
function Set-BackendFixture {
 $process=New-Backend 100 0 1;$child=New-Backend 101 100 2
 $script:Family=@($process,$child);$script:Current=@{[uint32]100=$process;[uint32]101=$child}
 $script:Listeners=@((New-Listener 101));$script:PortReads=0;$script:PidReads=0
 $script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0
}
$cases=[Collections.Generic.List[string]]::new()
function Assert($Value,[string]$Name){if(!$Value){throw $Name};$cases.Add($Name)}
function Confirm-Idle {try{(Invoke-AppearanceIdle).ok}catch{$false}}
Set-BackendFixture
$script:Current[[uint32]102]=New-Backend 102 0 3;$script:Listeners=@((New-Listener 102))
Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7 -and $script:AppearanceHelperFlags -eq 7 -and $script:PortReads -eq 1) 'A same-path listener that appeared outside the verified family cannot confirm Idle'
Set-BackendFixture
$script:Current[[uint32]101]=New-Backend 101 100 4
Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7 -and $script:PortReads -eq 1 -and $script:PidReads -eq 1) 'A same-path listener with a reused PID and different birth cannot confirm Idle'
Set-BackendFixture
Assert ((Confirm-Idle) -and $script:AppearanceHelperFlags -eq 31 -and $script:AppearanceHelperBackendCount -eq 2 -and $script:PortReads -eq 1 -and $script:PidReads -eq 1) 'Parent and listening child pass using the existing single listener observation'
Set-BackendFixture
$script:Listeners=@((New-Listener 100))
Assert ((Confirm-Idle) -and $script:AppearanceHelperFlags -eq 31 -and $script:PortReads -eq 1) 'Either verified family member may own the listener'
Set-BackendFixture
$replacement=New-Backend 101 100 2;$replacement.HasExited=$true;$script:Current[[uint32]101]=$replacement
Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7) 'An exited listener observation cannot confirm the backend even with a matching birth'
Set-BackendFixture
$script:Current[[uint32]102]=New-Backend 102 0 3;$script:Listeners=@((New-Listener 101),(New-Listener 102))
Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7 -and $script:PortReads -eq 1) 'Two different IPv4 loopback listener PIDs cannot produce a bound confirmation'
Set-BackendFixture
$script:Listeners=@((New-Listener 101 '0.0.0.0'),(New-Listener 101 '::1'))
Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7 -and $script:PidReads -eq 0) 'Wildcard and IPv6 cannot substitute for the existing IPv4 loopback check'
Set-BackendFixture
$script:Family=@();Assert (!(Confirm-Idle) -and $script:AppearanceFailureCode -eq 7) 'An explicitly empty family is never treated as an unbound lookup'
Set-BackendFixture
$script:Family[1].Parent=0
Assert (!(Confirm-Idle) -and $script:AppearanceHelperFlags -eq 7) 'Listener membership cannot authorize two independent families'
Set-BackendFixture
$script:Current[[uint32]102]=New-Backend 102 0 3;$script:Listeners=@((New-Listener 102))
Assert ((Test-BackendOwner) -and $script:PidReads -eq 1) 'The standalone per-RPC listener check retains its existing behavior'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
"PASS: $($cases.Count) final CSSLoader listener family checks"
