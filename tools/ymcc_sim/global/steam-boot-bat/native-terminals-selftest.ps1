param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8)
$api=[regex]::Match($text,'function Initialize-BootProcessApi \{\s*Add-Type -TypeDefinition @"\r?\n([\s\S]*?)\r?\n"@').Groups[1].Value
if(-not $api){throw 'Native process API not found'}
function Extract([string]$Signature){$begin=$api.IndexOf($Signature);if($begin -lt 0){throw ('Missing consumer '+$Signature)};$open=$api.IndexOf('{',$begin);$depth=1;$end=$open+1;while($depth -gt 0 -and $end -lt $api.Length){if($api[$end] -eq '{'){$depth++}elseif($api[$end] -eq '}'){$depth--};$end++};$api.Substring($begin,$end-$begin)}
$row=Extract 'public class Row '
$exact=Extract 'public static bool ExactXbox('
$trusted=Extract 'public static bool TrustedPackage('
$stop=Extract 'public static string StopExact('
$package=Extract 'public static string StopPackage('
# Only process/COM effect terminals are replaced. Identity/recheck/ordering bodies remain verbatim.
$package=$package.Replace('Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("B1AEC16F-2383-4852-B0E9-8F0B1DC66B4D")))','NewFakeCom()').Replace('Marshal.FinalReleaseComObject(instance)','ReleaseFakeCom(instance)')
$fixture=@"
using System;using System.Runtime.InteropServices;using System.Collections.Generic;
public static class YmBootSessions {public static bool Allowed=true;public static bool SingleSession(int s){return Allowed;}}
public static class YmBootProcess {
$row
$exact
$trusted
$stop
$package
public static Row Current;public static bool OpenAllowed=true,TerminateAllowed=true,ExitObserved=true,RetireAfterSession=false;
public static int Closes,Terminations,PackageCalls,ComReleases,ComCreates,SessionHr,PackageHr;public static uint SessionSelected;
public static List<string> Order=new List<string>();
private static IntPtr OpenProcess(uint access,bool inherit,int id){return OpenAllowed?new IntPtr(1):IntPtr.Zero;}
private static Row ReadHandle(IntPtr h,int id){return Current;}
private static bool CloseHandle(IntPtr h){Closes++;return true;}
private static bool TerminateProcess(IntPtr h,uint code){Terminations++;return TerminateAllowed;}
private static uint WaitForSingleObject(IntPtr h,uint ms){return ExitObserved?0u:258u;}
public static Row Read(int id){Order.Add("read");return Current;}
private interface PackageDebug {int SetTargetSessionId(uint session);int TerminateAllProcesses(string p);}
private class FakeCom:PackageDebug {public int SetTargetSessionId(uint session){Order.Add("session");SessionSelected=session;if(RetireAfterSession)Current.Birth++;return SessionHr;}public int TerminateAllProcesses(string p){Order.Add("package");PackageCalls++;return PackageHr;}}
private static object NewFakeCom(){ComCreates++;return new FakeCom();}
private static void ReleaseFakeCom(object x){ComReleases++;}
}
"@
[IO.File]::WriteAllText((Join-Path $Evidence 'native-terminals-generated.cs'),$fixture,[Text.UTF8Encoding]::new($false));Add-Type -TypeDefinition $fixture
$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}
function Reset {
 $r=[YmBootProcess+Row]::new();$r.Id=123;$r.Session=42;$r.Birth=1000;$r.Name='XboxPcAppFT.exe';$r.Readable=$true;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';$r.User='S-1-known';[YmBootProcess]::Current=$r
 [YmBootProcess]::OpenAllowed=$true;[YmBootProcess]::TerminateAllowed=$true;[YmBootProcess]::ExitObserved=$true;[YmBootProcess]::RetireAfterSession=$false
 [YmBootProcess]::Closes=0;[YmBootProcess]::Terminations=0;[YmBootProcess]::PackageCalls=0;[YmBootProcess]::ComReleases=0;[YmBootProcess]::ComCreates=0;[YmBootProcess]::SessionHr=0;[YmBootProcess]::PackageHr=0;[YmBootProcess]::Order.Clear();[YmBootSessions]::Allowed=$true
}
Reset;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Terminations -eq 1 -and [YmBootProcess]::Closes -eq 1 -and $result -like '*exited=True*') 'actual exact PID termination rechecks identity, observes exit and releases owned handle'
Reset;[YmBootProcess]::Current.Birth=1001;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Terminations -eq 0 -and [YmBootProcess]::Closes -eq 1 -and $result -eq 'identity-changed;not-terminated') 'actual PID birth reuse cannot reach termination'
Reset;[YmBootProcess]::Current.Name='svchost.exe';$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Terminations -eq 0 -and [YmBootProcess]::Closes -eq 1) 'actual native terminal cannot terminate changed unrelated executable'
Reset;[YmBootProcess]::Current.Session=99;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Terminations -eq 0) 'actual native terminal rejects changed session'
Reset;[YmBootProcess]::OpenAllowed=$false;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Terminations -eq 0 -and [YmBootProcess]::Closes -eq 0 -and $result -like 'open-failed*') 'open denial retains error without pretending a process was killed'
Reset;[YmBootProcess]::TerminateAllowed=$false;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ([YmBootProcess]::Closes -eq 1 -and $result -like 'terminate-failed*') 'native termination failure releases the handle and reports failure'
Reset;[YmBootProcess]::ExitObserved=$false;$result=[YmBootProcess]::StopExact(123,1000,42)
Assert ($result -like '*exited=False*') 'native request success without exit cannot become a confirmed exit'
Reset;$r=[YmBootProcess]::Current;$result=[YmBootProcess]::StopPackage(123,1000,42,$r.Package,'S-1-known')
Assert ([YmBootProcess]::PackageCalls -eq 1 -and [YmBootProcess]::SessionSelected -eq 42 -and ([YmBootProcess]::Order -join ',') -eq 'read,session,read,package') 'actual package consumer selects session and rechecks identity before termination'
Assert ([YmBootProcess]::ComReleases -eq 1 -and $result -like '*absence-requires-followup*') 'successful package call releases COM and cannot claim persistent absence'
Reset;[YmBootSessions]::Allowed=$false;$r=[YmBootProcess]::Current;$result=[YmBootProcess]::StopPackage(123,1000,42,$r.Package,'S-1-known')
Assert ([YmBootProcess]::ComCreates -eq 0 -and [YmBootProcess]::PackageCalls -eq 0 -and $result -eq 'package-other-or-unknown-session-rejected') 'other or unknown interactive session prevents whole-package side effect'
Reset;[YmBootProcess]::RetireAfterSession=$true;$r=[YmBootProcess]::Current;$result=[YmBootProcess]::StopPackage(123,1000,42,$r.Package,'S-1-known')
Assert ([YmBootProcess]::PackageCalls -eq 0 -and [YmBootProcess]::ComReleases -eq 1 -and $result -eq 'package-identity-retired') 'replacement during session selection cannot inherit whole-package authority'
Reset;[YmBootProcess]::SessionHr=-1;$r=[YmBootProcess]::Current;$result=[YmBootProcess]::StopPackage(123,1000,42,$r.Package,'S-1-known')
Assert ([YmBootProcess]::PackageCalls -eq 0 -and [YmBootProcess]::ComReleases -eq 1 -and $result -like 'package-session-hr=*') 'failed target-session selection never proceeds to package termination'
Reset;$r=[YmBootProcess]::Current;$result=[YmBootProcess]::StopPackage(123,1000,42,$r.Package,'S-1-other')
Assert ([YmBootProcess]::ComCreates -eq 0 -and [YmBootProcess]::PackageCalls -eq 0) 'wrong user cannot create package COM transaction'
[ordered]@{status='PASS';checks=$script:checks;consumer='verbatim production native StopExact and StopPackage with inert process/COM effect terminals';realNativeEffects=$false;sourceSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'native-terminals-result.json')
