param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###'
$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length);$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors.Message -join ';')}
foreach($fn in @($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false))){. ([scriptblock]::Create($fn.Extent.Text))}
$script:checks=0
function Assert($Value,$Message){$script:checks++;if(-not $Value){throw $Message}}
function Reset-Focus {
    $script:sim=[pscustomobject]@{Target=[pscustomobject]@{Hwnd=[IntPtr]256;Pid=888;Birth=123L};Front=[pscustomobject]@{Hwnd=[IntPtr]512;Pid=777;Name='XboxPcAppFT';Class='inert'};KeyboardKnown=$true;KeyboardMatches=$true;Time=0L;State=(New-BootFocusState 0);Calls=0;Waits=0;Logs=[Collections.Generic.List[string]]::new();Accept=$true;Move=$true;Steal=$false;Stale=$false;Throw=$false;TargetReads=0;Mode='fseApi=True supported=True active=True'}
}
function New-FocusPorts {
    @{
        State=$script:sim.State;Now={$script:sim.Time};Context={$script:sim.Mode}
        Target={$script:sim.TargetReads++;if($script:sim.Stale -and $script:sim.TargetReads -gt 1){return $null};$script:sim.Target}
        Foreground={$script:sim.Front}
        KeyboardFocus={param($H)[pscustomobject]@{Known=$script:sim.KeyboardKnown;Matches=$script:sim.KeyboardMatches;Hwnd=$H.ToInt64();Pid=888}}
        Request={param($Target)$script:sim.Calls++;if($script:sim.Throw){throw 'inert activation request failure'};if($script:sim.Move){$script:sim.Front=[pscustomobject]@{Hwnd=$Target.Hwnd;Pid=888;Name='steamwebhelper';Class='SDL_app'}};return $script:sim.Accept}
        Wait={$script:sim.Waits++;if($script:sim.Steal){$script:sim.Front=[pscustomobject]@{Hwnd=[IntPtr]1024;Pid=666;Name='other';Class='inert'}}}
        Log={param($E,$D)$script:sim.Logs.Add($E+' '+$D)}
    }
}
Reset-Focus;$ok=Invoke-SteamFocus (New-FocusPorts) 1 'startup-guard'
Assert ($ok -and $script:sim.Calls -eq 1 -and $script:sim.Waits -eq 1) 'actual focus consumer requests Steam activation once and reads live foreground ownership'
Assert ($script:sim.Logs[0] -like '*check=1 stage=startup-guard route=windows-nircmd-activate-focus*submitted=True*confirmed=True*fseApi=True supported=True active=True') 'FSE API fact and actual foreground result are logged together'
Reset-Focus;$script:sim.Front=[pscustomobject]@{Hwnd=[IntPtr]256;Pid=888};$ok=Invoke-SteamFocus (New-FocusPorts) 2 'startup-guard'
Assert ($ok -and $script:sim.Calls -eq 1 -and $script:sim.Waits -eq 1) 'already-foreground Steam still receives Windows activation and focus commands'
Reset-Focus;$script:sim.Target=$null;$ok=Invoke-SteamFocus (New-FocusPorts) 1 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 0 -and $script:sim.Logs[0] -like '*reason=target-unavailable*') 'unverified target cannot receive an activation request'
Reset-Focus;$script:sim.Accept=$false;$script:sim.Move=$false;$ok=Invoke-SteamFocus (New-FocusPorts) 3 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 1 -and $script:sim.Logs[0] -like '*submitted=False*confirmed=False*') 'request refusal is recorded without claiming success or retrying inside a check'
Reset-Focus;$script:sim.Move=$false;$ok=Invoke-SteamFocus (New-FocusPorts) 3 'startup-guard'
Assert (-not $ok -and $script:sim.Logs[0] -like '*submitted=True*confirmed=False*') 'request submission alone is not a foreground receipt'
Reset-Focus;$script:sim.Accept=$false;$ok=Invoke-SteamFocus (New-FocusPorts) 3 'startup-guard'
Assert ($ok -and $script:sim.Logs[0] -like '*submitted=False*confirmed=True*') 'observed foreground can succeed independently of request submission'
Reset-Focus;$script:sim.Steal=$true;$ok=Invoke-SteamFocus (New-FocusPorts) 2 'startup-guard'
Assert (-not $ok -and $script:sim.Logs[0] -like '*after=0x400/666*confirmed=False*') 'foreground stolen during handoff is observed rather than concealed'
Reset-Focus;$script:sim.Stale=$true;$ok=Invoke-SteamFocus (New-FocusPorts) 2 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 0) 'retired target is rejected again immediately before dispatch'
Reset-Focus;$script:sim.Mode='fseApi=False';$ok=Invoke-SteamFocus (New-FocusPorts) 1 'startup-guard'
Assert ($ok -and $script:sim.Calls -eq 1 -and $script:sim.Logs[0] -like '*fseApi=False') 'ordinary or API-absent Windows follows the same verified request consumer'
Reset-Focus;$script:sim.Throw=$true;$ok=Invoke-SteamFocus (New-FocusPorts) 3 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 1 -and $script:sim.Logs[0] -like '*focus-check-failed*') 'focus exception does not escape or add unbounded retry'
Reset-Focus;$ports=New-FocusPorts;Invoke-SteamFocus $ports 1 'startup-guard'|Out-Null
$script:sim.Time=24000;$script:sim.Front=[pscustomobject]@{Hwnd=[IntPtr]512;Pid=777;Name='YeManCC'};$ok=Invoke-SteamFocus $ports 25 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 1) 'a completed startup handoff never steals focus back from later YMCC interaction'
Reset-Focus;$ports=New-FocusPorts;$script:sim.Move=$false
foreach($at in @(0,1000,2000,3000)){$script:sim.Time=$at;Invoke-SteamFocus $ports 1 'startup-guard'|Out-Null}
Assert ($script:sim.Calls -eq 1 -and $script:sim.State.Requests -eq 1) 'the Windows command route itself enforces one request even if scheduled repeatedly'
Reset-Focus;$ports=New-FocusPorts;$script:sim.Move=$false
foreach($at in 0..59){$script:sim.Time=$at*1000;Invoke-SteamFocus $ports $at 'startup-guard'|Out-Null}
$script:sim.Time=60000;Invoke-SteamFocus $ports 61 'handoff'|Out-Null
Assert ($script:sim.Calls -eq 1 -and $script:sim.Logs[-1] -like '*reason=startup-ended*') 'persistent loss still has only one request and no action beyond startup'
Reset-Focus;$ports=New-FocusPorts;$script:sim.Front=[pscustomobject]@{Hwnd=[IntPtr]256;Pid=888}
foreach($at in 0..9){$script:sim.Time=$at*1000;Invoke-SteamFocus $ports $at 'startup-guard'|Out-Null}
Assert ($script:sim.Calls -eq 1) 'visible Steam receives the effective command once and is left alone afterward'

# Exact target-validation and snapshot consumers use an inert Win32 boundary.
Add-Type -TypeDefinition @"
using System;
using System.Text;
public static class YmBootWindow {
 public delegate bool Callback(IntPtr h,IntPtr p);
 public static bool Visible=true,Valid=true;
 public static uint Owner=888;
 public static string Class="SDL_app";
 public static long Style=0,Extended=0x40000;
 public static bool IsWindow(IntPtr h){return Valid;}
 public static bool IsWindowVisible(IntPtr h){return Visible;}
 public static uint GetWindowThreadProcessId(IntPtr h,out uint owner){owner=Owner;return 1;}
 public static int GetClassName(IntPtr h,StringBuilder text,int n){text.Append(Class);return Class.Length;}
 public static IntPtr GetWindowLongPtr(IntPtr h,int index){return new IntPtr(index==-16?Style:Extended);}
 public static bool EnumWindows(Callback callback,IntPtr p){callback(new IntPtr(256),p);return true;}
}
"@
function Get-BootXboxTargets { @() }
$script:session=42;$script:steamRoot='C:\inert Steam';$script:steamWindow=[IntPtr]256;$script:steamWindowPid=888
$script:birth=[DateTime]::UtcNow.AddMinutes(-1);$script:steamWindowBirth=$script:birth.ToFileTimeUtc()
function Reset-Target {
    [YmBootWindow]::Visible=$true;[YmBootWindow]::Valid=$true;[YmBootWindow]::Owner=888;[YmBootWindow]::Class='SDL_app';[YmBootWindow]::Style=0;[YmBootWindow]::Extended=0x40000
    $script:fake=[pscustomobject]@{Id=888;SessionId=42;ProcessName='steamwebhelper';Path='C:\inert Steam\bin\cef\cef.win64\steamwebhelper.exe';StartTime=$script:birth}
    $script:fake|Add-Member ScriptMethod Dispose {}
}
function Get-Process {param($Id,$Name,$ErrorAction)if($Id -or $Name -eq 'steamwebhelper'){$script:fake}elseif($Name -eq 'Steam'){[pscustomobject]@{SessionId=42;Id=999}}}
Reset-Target;$target=Get-SteamFocusTarget
Assert ($target.Hwnd -eq [IntPtr]256 -and $target.Pid -eq 888 -and $target.Birth -eq $script:steamWindowBirth) 'target binds exact visible Steam helper HWND PID and birth'
$script:fake.SessionId=99;Assert ($null -eq (Get-SteamFocusTarget)) 'another Windows session is rejected'
Reset-Target;$script:fake.Path='C:\inert Steam\bin\cef-spoof\steamwebhelper.exe';Assert ($null -eq (Get-SteamFocusTarget)) 'similar prefix outside verified Steam CEF root is rejected'
Reset-Target;$script:fake.ProcessName='Other';Assert ($null -eq (Get-SteamFocusTarget)) 'foreign process name is rejected'
Reset-Target;$script:fake.StartTime=$script:birth.AddSeconds(1);Assert ($null -eq (Get-SteamFocusTarget)) 'PID reuse or process replacement is rejected'
Reset-Target;[YmBootWindow]::Owner=999;Assert ($null -eq (Get-SteamFocusTarget)) 'reused HWND with another process is rejected'
Reset-Target;[YmBootWindow]::Visible=$false;Assert ($null -eq (Get-SteamFocusTarget)) 'hidden window does not receive focus'
Reset-Target;[YmBootWindow]::Valid=$false;Assert ($null -eq (Get-SteamFocusTarget)) 'destroyed window does not receive focus'
Reset-Target;[YmBootWindow]::Class='Other';Assert ($null -eq (Get-SteamFocusTarget)) 'unrelated window class is rejected'
Reset-Target;[YmBootWindow]::Style=0x40000;Assert ($null -eq (Get-SteamFocusTarget)) 'ordinary resizable Steam window is not a Big Picture candidate'
Reset-Target;[YmBootWindow]::Extended=0;Assert ($null -eq (Get-SteamFocusTarget)) 'non-app Steam helper window is rejected'
Reset-Target;$snapshot=Get-BootSnapshot
Assert ($snapshot.SteamRunning -and $snapshot.SteamWindow -and $script:steamWindowPid -eq 888 -and $script:steamWindowBirth -eq $script:birth.ToFileTimeUtc()) 'actual snapshot records exact process birth for the subsequent focus check'
$focusText=($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-SteamFocus'},$false))[0].Extent.Text
Assert ($body -notmatch 'SetForegroundWindow|ShowWindowAsync|SendInput|keybd_event|AttachThreadInput|SetWindowPos|SendKeys|Test-BootFocusYield') 'startup focus contains no legacy foreground API, input injection or surrender latch'
# Compile the actual Win32 declarations under an isolated type name; never call them.
$api=($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Initialize-WindowApi'},$false))[0].Extent.Text
. ([scriptblock]::Create($api.Replace('Initialize-WindowApi','Initialize-InertDeclarations').Replace('YmBootWindow','YmBootWindowDeclarationsOnly')))
Initialize-InertDeclarations
Assert ($null -ne ('YmBootWindowDeclarationsOnly' -as [type])) 'actual Windows foreground and GUI focus declarations compile without real window operations'
$script:requests=[Collections.Generic.List[string]]::new();$script:commandCode=0;$script:toolExists=$true
function Start-Quiet {param($Executable,$Arguments,[switch]$Capture)$script:requests.Add($Executable+'|'+$Arguments);[pscustomobject]@{Code=$script:commandCode;Text='inert command result'}}
function Test-InertToolExists {param($Path)$script:toolExists}
$command=($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Request-SteamActivation'},$false))[0].Extent.Text
. ([scriptblock]::Create($command.Replace('[IO.File]::Exists($tool)','(Test-InertToolExists $tool)')))
Reset-Target;$target=Get-SteamFocusTarget;$submitted=Request-SteamActivation $target
Assert ($submitted -and $script:requests.Count -eq 2 -and $script:requests[0] -eq 'C:\SOFT\NirCmd\nircmd.exe|win activate handle 0x100' -and $script:requests[1] -eq 'C:\SOFT\NirCmd\nircmd.exe|win focus handle 0x100') 'actual command consumer activates and focuses only the exact verified Steam HWND'
$target.Birth++;$submitted=Request-SteamActivation $target
Assert (-not $submitted -and $script:requests.Count -eq 2) 'changed process birth cannot dispatch a window command'
Reset-Target;$script:commandCode=-2;$submitted=Request-SteamActivation (Get-SteamFocusTarget)
Assert (-not $submitted -and $script:requests.Count -eq 3) 'failed or timed out activation does not run another focus helper'
$script:toolExists=$false;$submitted=Request-SteamActivation (Get-SteamFocusTarget)
Assert (-not $submitted -and $script:requests.Count -eq 3) 'missing focus tool is explicit failure without URI or process-name fallback'
Reset-Focus;$script:sim.Front=[pscustomobject]@{Hwnd=[IntPtr]256;Pid=888};$script:sim.KeyboardMatches=$false;$ok=Invoke-SteamFocus (New-FocusPorts) 1 'startup-guard'
Assert (-not $ok -and $script:sim.Calls -eq 1 -and $script:sim.Logs[0] -like '*foregroundConfirmed=True*keyboardFocusMatches=False*confirmed=False*') 'Steam visible in foreground cannot fake Windows keyboard focus'
Reset-Focus;$script:sim.KeyboardKnown=$false;$ok=Invoke-SteamFocus (New-FocusPorts) 1 'startup-guard'
Assert (-not $ok -and $script:sim.Logs[0] -like '*keyboardFocusKnown=False*confirmed=False*') 'unknown keyboard focus is not reported as confirmed'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual focus readback, target verification and window snapshot';effects='memory Win32/process ports';realForegroundChanged=$false;targetApplicationsStartedOrStopped=$false}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'focus-result.json')