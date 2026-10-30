param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$source=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###';$body=$source.Substring($source.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors);if($errors.Count){throw ($errors.Message -join ';')}
foreach($name in @('Initialize-BootProcessApi','Initialize-WindowApi')){$fn=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)[0];. ([scriptblock]::Create($fn.Extent.Text))}
Initialize-BootProcessApi;Initialize-WindowApi
$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}
function Good {
 $r=[YmBootProcess+Row]::new();$r.Id=321;$r.Birth=123456;$r.Session=42;$r.Readable=$true;$r.Name='XboxPcAppFT.exe';$r.Path='C:\Program Files\WindowsApps\Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe\XboxPcAppFT.exe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.User='S-1-known';return $r
}
$r=Good;Assert ([YmBootProcess]::ExactXbox($r,42)) 'actual native exact-name current-session gate admits a valid Xbox identity'
$r.Session=99;Assert (-not [YmBootProcess]::ExactXbox($r,42)) 'native gate excludes another session'
$r=Good;$r.Readable=$false;Assert (-not [YmBootProcess]::ExactXbox($r,42)) 'native gate refuses unreadable identity'
foreach($name in @('explorer.exe','svchost.exe','GamingServices.exe','GameBar.exe','XboxPcAppFT.exe.evil','myXboxPcApp.exe','YeManCC.exe','Steam.exe')){$r=Good;$r.Name=$name;Assert (-not [YmBootProcess]::ExactXbox($r,42)) ('native gate excludes unrelated '+$name)}
foreach($name in @('XboxPcApp.exe','XboxPcAppCE.exe','xboxpcappft.EXE')){$r=Good;$r.Name=$name;Assert ([YmBootProcess]::ExactXbox($r,42)) ('native gate retains known image '+$name)}
$r=Good;Assert ([YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'actual GamingApp package is eligible only with the current caller SID'
$r=Good;$r.User='S-1-other';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another user package is refused'
$r=Good;$r.Family='Microsoft.GamingApp_lookalike';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'lookalike package family is refused'
$r=Good;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__different';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another package publisher is refused'
$r=Good;$r.Package='AnotherPackage_1.0.0.0_x64__8wekyb3d8bbwe';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another package under the same publisher is refused'
$r=Good;Assert (-not [YmBootProcess]::TrustedPackage($r,42,'')) 'unknown caller SID cannot authorize whole-package termination'
# Exact COM slot order matches the SDK header read in this task. No COM instance is created.
$flags=[Reflection.BindingFlags]::NonPublic -bor [Reflection.BindingFlags]::Public
$type=[YmBootProcess].GetNestedType('PackageDebug',$flags)
$slots=@($type.GetMethods()|Sort-Object MetadataToken|ForEach-Object Name)
$expected=@('EnableDebugging','DisableDebugging','Suspend','Resume','TerminateAllProcesses','SetTargetSessionId','EnumerateBackgroundTasks','ActivateBackgroundTask','StartServicing','StopServicing','StartSessionRedirection','StopSessionRedirection','GetPackageExecutionState','RegisterForPackageStateChanges','UnregisterForPackageStateChanges')
Assert (($slots -join ',') -ceq ($expected -join ',')) 'COM interface preserves all15 SDK slots in their actual order'
Assert ($type.GUID.ToString() -eq 'f27c3930-8029-4ad1-94e3-3dba417810c1') 'COM interface uses the SDK IID'
$command='function Initialize-BootProcessApi {'+${function:Initialize-BootProcessApi}.ToString()+"}`r`nInitialize-BootProcessApi`r`n"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));Assert ($encoded.Length -lt 29000) 'complete process API fits bounded Windows encoded-command helper'
# Execute the production package helper encoder with a memory-only launch terminal.
$fn=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-BootPackageStop'},$false)[0];. ([scriptblock]::Create($fn.Extent.Text))
$script:session=42;$script:bootUserSid='S-1-known';$script:encodedArguments=''
function Start-Quiet {param($Exe,$Arguments,[switch]$Capture)$script:encodedArguments=$Arguments;[pscustomobject]@{Code=0;Text='inert encoded helper'}}
$r=Good;$receipt=Invoke-BootPackageStop $r
Assert ($receipt.Code -eq 0 -and $script:encodedArguments.Length -lt 32000 -and $script:encodedArguments -like '*-WindowStyle Hidden -EncodedCommand *') 'actual package helper is hidden, captured and within Windows command line bound'
$encoded=$script:encodedArguments.Substring($script:encodedArguments.LastIndexOf(' ')+1);$decoded=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded));$childTokens=$null;$childErrors=$null
[Management.Automation.Language.Parser]::ParseInput($decoded,[ref]$childTokens,[ref]$childErrors)|Out-Null
Assert ($childErrors.Count -eq 0 -and $decoded.Contains('[YmBootProcess]::StopPackage([int]$t.id,[long]$t.birth,[int]$t.session,[string]$t.package,[string]$t.sid)')) 'encoded helper preserves exact PID, birth, session, package and SID operands without shell interpolation'
# The shipping BPM selector runs with only its process/window effect ports
# replaced. Do not enumerate or focus any real window during this suite.
$selectorAst=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-BootSnapshot'},$false))
Assert ($selectorAst.Count -eq 1) 'exactly one production BPM window selector is bound'
$selectorSource=$selectorAst[0].Extent.Text
$selectorBody=$selectorSource.Replace('[YmBootWindow+Callback]','[YmBootWindowFixture+Callback]').Replace('[YmBootWindow]','[YmBootWindowFixture]')
. ([scriptblock]::Create($selectorBody))
Add-Type -TypeDefinition @"
using System;using System.Text;using System.Collections.Generic;
public static class YmBootWindowFixture {
 public class Window {public long Handle;public uint Pid;public string Class="SDL_app";public bool Visible=true;public long Style=0;public long ExStyle=0x40000;}
 public delegate bool Callback(IntPtr handle,IntPtr unused);
 public static List<Window> Windows=new List<Window>();public static int Enumerations;
 private static Window Find(IntPtr h){return Windows.Find(w=>w.Handle==h.ToInt64());}
 public static bool EnumWindows(Callback callback,IntPtr data){Enumerations++;foreach(var w in Windows){if(!callback(new IntPtr(w.Handle),data))break;}return true;}
 public static bool IsWindowVisible(IntPtr h){var w=Find(h);return w!=null&&w.Visible;}
 public static uint GetWindowThreadProcessId(IntPtr h,out uint pid){var w=Find(h);pid=w==null?0:w.Pid;return 1;}
 public static int GetClassName(IntPtr h,StringBuilder name,int n){var w=Find(h);if(w==null)return 0;name.Append(w.Class);return w.Class.Length;}
 public static IntPtr GetWindowLongPtr(IntPtr h,int n){var w=Find(h);return new IntPtr(w==null?0:n==-16?w.Style:w.ExStyle);}
}
"@
$script:fixtureSteam=@();$script:fixtureHelpers=@();$script:processQueries=0
function Get-Process {param([string[]]$Name,$ErrorAction)
 $script:processQueries++
 if(@($Name).Count -eq 1 -and $Name[0] -ieq 'Steam'){return $script:fixtureSteam}
 if(@($Name).Count -eq 1 -and $Name[0] -ieq 'steamwebhelper'){return $script:fixtureHelpers}
 throw 'Unexpected process query escaped the fixture'
}
function Get-BootXboxTargets {return @()}
function Reset-WindowFixture {
 $script:session=42;$script:steamRoot='G:\FixtureSteam'
 $script:fixtureSteam=@([pscustomobject]@{Id=11;SessionId=42;Path='G:\FixtureSteam\steam.exe'})
 $script:fixtureHelpers=@([pscustomobject]@{Id=88;SessionId=42;Path='G:\FixtureSteam\bin\cef\cef.win64\steamwebhelper.exe';StartTime=[DateTime]::FromFileTimeUtc(134000000000000000L)})
 $script:processQueries=0;[YmBootWindowFixture]::Windows.Clear();[YmBootWindowFixture]::Enumerations=0
}
function Add-WindowFixture([long]$Handle,[uint32]$Owner,[bool]$Visible=$true,[string]$Class='SDL_app',[long]$Style=0,[long]$ExStyle=262144) {
 $row=[YmBootWindowFixture+Window]::new();$row.Handle=$Handle;$row.Pid=$Owner;$row.Visible=$Visible;$row.Class=$Class;$row.Style=$Style;$row.ExStyle=$ExStyle;[YmBootWindowFixture]::Windows.Add($row)
}
$script:windowCases=0
Reset-WindowFixture;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert ($selected.SteamRunning -and $selected.SteamWindow -and $script:steamWindow.ToInt64() -eq 100) 'actual BPM selector admits visible SDL_app from current Steam helper'
Assert ($script:steamWindowPid -eq 88 -and $script:steamWindowBirth -eq 134000000000000000L) 'selected HWND retains its actual helper PID and birth for later revalidation'
Assert ([YmBootWindowFixture]::Enumerations -eq 1 -and $script:processQueries -eq 2) 'one snapshot uses one bounded window walk and two fixture process queries'
Reset-WindowFixture;Add-WindowFixture 200 999;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow -and $script:steamWindow.ToInt64() -eq 0) 'monitor or unrelated SDL_app never becomes Steam merely through window class and styles'
Reset-WindowFixture;Add-WindowFixture 200 999;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert ($selected.SteamWindow -and $script:steamWindow.ToInt64() -eq 100) 'monitor enumerated before Steam cannot divert the selected target'
Reset-WindowFixture;$script:fixtureHelpers[0].SessionId=99;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'another-session Steam helper cannot become the boot target'
Reset-WindowFixture;$script:fixtureSteam[0].SessionId=99;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamRunning) 'another-session Steam process does not prove current-session readiness'
Reset-WindowFixture;$script:fixtureHelpers[0].Path='G:\FixtureSteam-Other\bin\cef\cef.win64\steamwebhelper.exe';Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'adjacent installation path cannot provide a Steam target'
Reset-WindowFixture;$script:fixtureHelpers[0].Path='G:\FixtureSteam\bin\cef-Other\steamwebhelper.exe';Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'CEF path prefix requires its directory separator'
Reset-WindowFixture;$script:fixtureHelpers[0].Path='G:\Unknown\steamwebhelper.exe';Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'process basename alone cannot authorize a focus target'
Reset-WindowFixture;$script:fixtureHelpers[0].Path=$null;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'unreadable helper path stays unverified without an activation fallback'
Reset-WindowFixture;Add-WindowFixture 100 88 $false;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'hidden Steam window does not trigger first-visible handoff'
Reset-WindowFixture;Add-WindowFixture 100 88 $true 'Chrome_WidgetWin_0';$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'non-BPM CEF window cannot trigger the SDL target handoff'
Reset-WindowFixture;Add-WindowFixture 100 88 $true 'SDL_app' 262144 262144;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'resizable Steam window is not accepted as the pinned BPM style'
Reset-WindowFixture;Add-WindowFixture 100 88 $true 'SDL_app' 0 0;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'missing APPWINDOW receipt does not authorize target selection'
Reset-WindowFixture;Add-WindowFixture 100 88;[void](Get-BootSnapshot);$script:fixtureHelpers=@();$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow -and $script:steamWindow.ToInt64() -eq 0 -and $script:steamWindowPid -eq 0 -and $script:steamWindowBirth -eq 0) 'lost helper clears every previously selected target field'
Reset-WindowFixture;Add-WindowFixture 100 88;[void](Get-BootSnapshot);$script:fixtureHelpers[0].StartTime=[DateTime]::FromFileTimeUtc(134000000000000001L);[void](Get-BootSnapshot);$script:windowCases++
Assert ($script:steamWindowBirth -eq 134000000000000001L) 'reused numeric helper PID is rebound to the newly observed process birth'
Reset-WindowFixture;$script:steamRoot=$null;Add-WindowFixture 100 88;$selected=Get-BootSnapshot;$script:windowCases++
Assert (-not $selected.SteamWindow) 'unknown Steam root cannot select a target from a merely matching process name'
[IO.File]::WriteAllText((Join-Path $Evidence 'window-selector-source.ps1'),$selectorSource,[Text.UTF8Encoding]::new($false))
[ordered]@{status='PASS';checks=$script:checks;consumer='actual native Xbox identity/package gates, SDK COM ABI and production BPM window selector';windowSelectorCases=$script:windowCases;nativeApisCompiled=$true;realNativeProcessOrComEffects=$false;sourceSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'native-gates-result.json')
