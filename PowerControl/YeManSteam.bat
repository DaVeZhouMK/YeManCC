@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "YEMAN_STARTUP_SOURCE=%~f0"
set "YEMAN_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "C:\SOFT\NirCmd\nircmd.exe" (
    "C:\SOFT\NirCmd\nircmd.exe" exec hide "%YEMAN_PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command "$s=[IO.File]::ReadAllText($env:YEMAN_STARTUP_SOURCE,[Text.Encoding]::UTF8);$m='### YEMAN_SINGLE_BAT_POWERSHELL ###';& ([scriptblock]::Create($s.Substring($s.LastIndexOf($m)+$m.Length)))" >nul 2>&1
    if not errorlevel 1 exit /b 0
)
"%YEMAN_PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command "$s=[IO.File]::ReadAllText($env:YEMAN_STARTUP_SOURCE,[Text.Encoding]::UTF8);$m='### YEMAN_SINGLE_BAT_POWERSHELL ###';& ([scriptblock]::Create($s.Substring($s.LastIndexOf($m)+$m.Length)))" >nul 2>&1
exit /b %errorlevel%
### YEMAN_SINGLE_BAT_POWERSHELL ###
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath

# Effects are injectable so this exact startup controller can be tested without
# starting Steam/CSS/YMCC or ending any real process.
function Invoke-BootFlow($P) {
    $begin=& $P.Now
    $quietSince=-1L;$steamSeen=-1L;$requestAt=-1L
    $shown=$false;$affinityDone=$false
    & $P.Log 'boot-start' 'limitMs=60000 minWatchMs=60000 pollMs=500 quietMs=8000 focusPolicy=none ymccArguments=--minimized'
    $s=& $P.Snapshot
    if(@($s.Xbox).Count){& $P.CloseXbox $s.Xbox}
    $s=& $P.Snapshot
    if(@($s.Xbox).Count){& $P.CloseXbox $s.Xbox}
    & $P.YMCC
    $s=& $P.Snapshot
    if(@($s.Xbox).Count){& $P.CloseXbox $s.Xbox}
    $coldSteamLaunch=-not $s.SteamRunning
    if($s.SteamRunning){& $P.ShowSteam;$shown=$true;$steamSeen=& $P.Now}else{& $P.StartSteam}
    $requestAt=& $P.Now
    & $P.Log 'steam-launch-admission' 'policy=BAT-STEAM-EARLY-LAUNCH-20261024-R1 inputWaitMs=0 inputReadinessGatesLaunch=False'
    if($coldSteamLaunch -and $P.ContainsKey('InputWarmup')){& $P.InputWarmup}
    while(((& $P.Now)-$begin) -lt 60000) {
        $s=& $P.Snapshot;$now=& $P.Now
        if($P.ContainsKey('Checkpoint')){& $P.Checkpoint 'poll' $s}
        if(@($s.Xbox).Count){$quietSince=-1L;& $P.CloseXbox $s.Xbox}
        elseif($s.SteamWindow){if($quietSince -lt 0){$quietSince=$now}}
        else{$quietSince=-1L}
        if($s.SteamRunning -and $steamSeen -lt 0){$steamSeen=$now}
        if($s.SteamRunning -and -not $s.SteamWindow -and -not $shown -and ($now-$requestAt) -ge 3000){& $P.ShowSteam;$shown=$true}
        if(-not $affinityDone -and $steamSeen -ge 0 -and ($now-$steamSeen) -ge 10000){& $P.Affinity;$affinityDone=$true}
        & $P.Wait
    }
    $s=& $P.Snapshot;$now=& $P.Now
    $complete=$s.SteamWindow -and $s.SteamRunning -and -not @($s.Xbox).Count -and $quietSince -ge 0 -and ($now-$quietSince) -ge 8000 -and $affinityDone
    $stage=if($complete){'complete'}else{'deadline'}
    if($P.ContainsKey('Complete')){& $P.Complete $stage $s}
    & $P.Log ('boot-'+$stage) ('elapsedMs='+((& $P.Now)-$begin)+' focusPolicy=none steamRunning='+$s.SteamRunning+' steamWindow='+$s.SteamWindow)
    if($complete){return 0};return 1
}
function Write-BootLog([string]$Event,[string]$Detail) {
    try {
        $dir=Join-Path $env:LOCALAPPDATA 'YeManCC'
        [IO.Directory]::CreateDirectory($dir)|Out-Null
        $file=Join-Path $dir 'steam-launch.log'
        if([IO.File]::Exists($file) -and ([IO.FileInfo]$file).Length -gt 4194304){[IO.File]::Copy($file,$file+'.1',$true);[IO.File]::WriteAllText($file,'',[Text.UTF8Encoding]::new($false))}
        $line='[{0}] [SingleBat pid={1}] {2} {3}' -f ([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff')),$PID,$Event,($Detail -replace '[\r\n]+',' ')
        [IO.File]::AppendAllText($file,$line+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
    }catch{}
}
function Start-Quiet([string]$Exe,[string]$Arguments='', [switch]$Capture) {
    $si=[Diagnostics.ProcessStartInfo]::new()
    $si.FileName=$Exe;$si.Arguments=$Arguments;$si.WorkingDirectory=[IO.Path]::GetDirectoryName($Exe)
    $si.UseShellExecute=$false;$si.CreateNoWindow=$true
    if($Capture){$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true}
    $p=[Diagnostics.Process]::new();$p.StartInfo=$si
    try {
        if(-not $p.Start()){throw 'Process start returned false'}
        if(-not $Capture){return}
        $a=$p.StandardOutput.ReadToEndAsync();$b=$p.StandardError.ReadToEndAsync()
        if(-not $p.WaitForExit(3000)) {
            # Only this owned command helper is retired, never a target app.
            $helperPid=$p.Id;$retired=$false;$failure=''
            try{$p.Kill();$retired=$p.WaitForExit(500)}catch{$failure=$_.Exception.Message}
            return [pscustomobject]@{Code=-2;Text=('helper-timeout pid='+$helperPid+' retired='+$retired+' error='+$failure)}
        }
        [pscustomobject]@{Code=$p.ExitCode;Text=($a.Result+' '+$b.Result).Trim()}
    }finally{$p.Dispose()}
}
function Find-SteamExe {
    $paths=@();$user=Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if($user.SteamExe){$paths+=[string]$user.SteamExe}
    if($user.SteamPath){$paths+=Join-Path ([string]$user.SteamPath) 'Steam.exe'}
    foreach($key in @('HKLM:\SOFTWARE\WOW6432Node\Valve\Steam','HKLM:\SOFTWARE\Valve\Steam')){
        $x=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if($x.InstallPath){$paths+=Join-Path ([string]$x.InstallPath) 'Steam.exe'}
    }
    foreach($path in $paths){if([IO.File]::Exists($path)){return [IO.Path]::GetFullPath($path)}}
    return ''
}
function Test-BootAdministrator {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function New-BootYmccStartInfo([string]$Path) {
    # Match the original BAT's normal shell launch. Never apply console hiding to YMCC.
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$Path;$info.WorkingDirectory=[IO.Path]::GetDirectoryName($Path)
    $info.UseShellExecute=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal;$info.Arguments='--minimized'
    return $info
}
function Start-BootYmccProcess([string]$Path) {
    $process=[Diagnostics.Process]::new();$process.StartInfo=New-BootYmccStartInfo $Path
    try {
        if(-not $process.Start()){throw 'YMCC shell launch returned false'}
        Write-BootLog 'ymcc-start' ('submitted shell=True windowStyle=Normal arguments=--minimized childPid='+$process.Id)
    }finally{$process.Dispose()}
}
function Start-BootYMCC {
    $found=@(Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})
    if($found.Count){Write-BootLog 'ymcc-skip' 'already-running; original-instance-retained';return}
    # YMCC requires elevation. Keep its original elevated logon task in charge on a medium token.
    # Do not /run that task: its StopExisting policy can replace an instance during a logon race.
    if(-not (Test-BootAdministrator)){Write-BootLog 'ymcc-skip' 'administrator-required; original-elevated-startup-entry-retained';return}
    $result=Start-Quiet (Join-Path $env:SystemRoot 'System32\schtasks.exe') '/query /tn "\野蛮优化整合系统\野蛮控制中心-开机启动"' -Capture
    if($result.Code -ne 0){Write-BootLog 'ymcc-skip' 'startup-task-not-found';return}
    $path='C:\SOFT\YeMan\YeManCC\YeManCC.exe'
    if(-not [IO.File]::Exists($path)){Write-BootLog 'ymcc-skip' 'executable-missing';return}
    # The normal elevated startup can have arrived while the query was running.
    $found=@(Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})
    if($found.Count){Write-BootLog 'ymcc-skip' 'already-running-after-query; original-instance-retained';return}
    try{Start-BootYmccProcess $path}catch{Write-BootLog 'ymcc-start-failed' $_.Exception.Message}
}
function Request-Steam([bool]$Existing) {
    if(-not $script:steamExe){Write-BootLog 'steam-missing' 'install-path-not-found';return}
    $arguments=if($Existing){'steam://open/gamepadui'}else{'-bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui'}
    try{Start-Quiet $script:steamExe $arguments;Write-BootLog 'steam-request' $arguments}catch{Write-BootLog 'steam-start-failed' $_.Exception.Message}
}
function Initialize-WindowApi {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class YmBootWindow {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c,IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] private static extern IntPtr GetLong64(IntPtr h,int n);
 [DllImport("user32.dll",EntryPoint="GetWindowLongW")] private static extern int GetLong32(IntPtr h,int n);
 public static IntPtr GetWindowLongPtr(IntPtr h,int n){return IntPtr.Size==8?GetLong64(h,n):new IntPtr(GetLong32(h,n));}
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern IntPtr LoadLibraryEx(string name,IntPtr file,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)] private static extern IntPtr GetProcAddress(IntPtr module,string name);
 [DllImport("kernel32.dll")] private static extern bool FreeLibrary(IntPtr module);
 [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate int GamingFlag();
 public static string GamingContext() {
  IntPtr module=IntPtr.Zero;
  try {
   module=LoadLibraryEx("api-ms-win-gaming-experience-l1-1-0.dll",IntPtr.Zero,0x800);
   if(module==IntPtr.Zero)return "fseApi=False";
   IntPtr supported=GetProcAddress(module,"IsGamingFullScreenExperienceSupported"),active=GetProcAddress(module,"IsGamingFullScreenExperienceActive");
   if(supported==IntPtr.Zero||active==IntPtr.Zero)return "fseApi=False";
   GamingFlag readSupported=(GamingFlag)Marshal.GetDelegateForFunctionPointer(supported,typeof(GamingFlag));
   GamingFlag readActive=(GamingFlag)Marshal.GetDelegateForFunctionPointer(active,typeof(GamingFlag));
   bool available=readSupported()!=0;
   return "fseApi=True supported="+available+" active="+(available&&readActive()!=0);
  } catch {return "fseApi=unknown";}
  finally {if(module!=IntPtr.Zero)FreeLibrary(module);}
 }
}
"@
}
function Get-BootSnapshot {
    $xbox=@(Get-BootXboxTargets)
    $steam=@(Get-Process -Name 'Steam' -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})
    $owners=@{}
    foreach($helper in @(Get-Process -Name 'steamwebhelper' -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})){
        try{if($script:steamRoot -and $helper.Path.StartsWith(($script:steamRoot+'\bin\cef\'),[StringComparison]::OrdinalIgnoreCase)){$owners[[uint32]$helper.Id]=$helper.StartTime.ToUniversalTime().ToFileTimeUtc()}}catch{}
    }
    $found=[pscustomobject]@{Hwnd=[IntPtr]::Zero;Pid=0;Birth=0L}
    $callback=[YmBootWindow+Callback]{param($handle,$unused)
        if(-not [YmBootWindow]::IsWindowVisible($handle)){return $true}
        $owner=[uint32]0;[YmBootWindow]::GetWindowThreadProcessId($handle,[ref]$owner)|Out-Null
        if(-not $owners.ContainsKey($owner)){return $true}
        $name=[Text.StringBuilder]::new(128);[YmBootWindow]::GetClassName($handle,$name,128)|Out-Null
        $style=[YmBootWindow]::GetWindowLongPtr($handle,-16).ToInt64();$ex=[YmBootWindow]::GetWindowLongPtr($handle,-20).ToInt64()
        if($name.ToString() -eq 'SDL_app' -and ($ex -band 0x40000) -and -not ($style -band 0x40000)){$found.Hwnd=$handle;$found.Pid=$owner;$found.Birth=$owners[$owner];return $false}
        return $true
    }
    [YmBootWindow]::EnumWindows($callback,[IntPtr]::Zero)|Out-Null
    $script:steamWindow=$found.Hwnd;$script:steamWindowPid=$found.Pid;$script:steamWindowBirth=$found.Birth
    [pscustomobject]@{Xbox=$xbox;SteamRunning=($steam.Count -gt 0);SteamWindow=($found.Hwnd -ne [IntPtr]::Zero)}
}
function Get-BootForeground {
    $handle=[YmBootWindow]::GetForegroundWindow();$owner=[uint32]0;$name='unknown';$class=''
    if($handle -ne [IntPtr]::Zero){
        [YmBootWindow]::GetWindowThreadProcessId($handle,[ref]$owner)|Out-Null
        $buffer=[Text.StringBuilder]::new(128);[YmBootWindow]::GetClassName($handle,$buffer,128)|Out-Null;$class=$buffer.ToString()
    }
    $process=$null
    try{if($owner){$process=Get-Process -Id $owner -ErrorAction Stop;$name=$process.ProcessName}}catch{}finally{if($process){$process.Dispose()}}
    [pscustomobject]@{Hwnd=$handle;Pid=$owner;Name=$name;Class=$class}
}
function Set-BootAffinity {
    foreach($name in @('Steam++','Steam++.Accelerator')){
        foreach($p in @(Get-Process -Name $name -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})){
            try{$p.ProcessorAffinity=[IntPtr]0xA0}catch{Write-BootLog 'affinity-skipped' ($name+' '+$_.Exception.Message)}
        }
    }
}
function Initialize-BootProcessApi {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.IO;
public static class YmBootProcess {
 public class Row { public int Id,ParentId,Session=-1,Error; public long Birth; public string Name="",Path="",Package="",Family="",AppId="",User=""; public bool Readable; }
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] private struct Entry { public uint Size,Usage,Id; public IntPtr Heap; public uint Module,Threads,Parent; public int Priority; public uint Flags; [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string Name; }
 [DllImport("kernel32.dll",SetLastError=true)] private static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint id);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern bool Process32FirstW(IntPtr h,ref Entry e);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern bool Process32NextW(IntPtr h,ref Entry e);
 [DllImport("kernel32.dll",SetLastError=true)] private static extern IntPtr OpenProcess(uint access,bool inherit,int id);
 [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] private static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder text,ref int size);
 [DllImport("kernel32.dll",SetLastError=true)] private static extern bool ProcessIdToSessionId(int id,out int session);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern int GetPackageFullName(IntPtr h,ref uint size,StringBuilder text);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern int GetPackageFamilyName(IntPtr h,ref uint size,StringBuilder text);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern int GetApplicationUserModelId(IntPtr h,ref uint size,StringBuilder text);
 [DllImport("advapi32.dll",SetLastError=true)] private static extern bool OpenProcessToken(IntPtr h,uint access,out IntPtr token);
 [DllImport("kernel32.dll",SetLastError=true)] private static extern bool TerminateProcess(IntPtr h,uint code);
 [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [ComImport,Guid("F27C3930-8029-4AD1-94E3-3DBA417810C1"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] private interface PackageDebug {
  [PreserveSig]int EnableDebugging([MarshalAs(UnmanagedType.LPWStr)]string p,[MarshalAs(UnmanagedType.LPWStr)]string d,IntPtr e);
  [PreserveSig]int DisableDebugging([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int Suspend([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int Resume([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int TerminateAllProcesses([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int SetTargetSessionId(uint session);
  [PreserveSig]int EnumerateBackgroundTasks([MarshalAs(UnmanagedType.LPWStr)]string p,out uint count,out IntPtr ids,out IntPtr names);
  [PreserveSig]int ActivateBackgroundTask(IntPtr task);
  [PreserveSig]int StartServicing([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int StopServicing([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int StartSessionRedirection([MarshalAs(UnmanagedType.LPWStr)]string p,uint session);
  [PreserveSig]int StopSessionRedirection([MarshalAs(UnmanagedType.LPWStr)]string p);
  [PreserveSig]int GetPackageExecutionState([MarshalAs(UnmanagedType.LPWStr)]string p,out int state);
  [PreserveSig]int RegisterForPackageStateChanges([MarshalAs(UnmanagedType.LPWStr)]string p,IntPtr notification,out uint cookie);
  [PreserveSig]int UnregisterForPackageStateChanges(uint cookie);
 }
 public static Row[] Inventory() {
  IntPtr h=CreateToolhelp32Snapshot(2,0); if(h==new IntPtr(-1))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
  try {var rows=new List<Row>();var e=new Entry();e.Size=(uint)Marshal.SizeOf(typeof(Entry));if(Process32FirstW(h,ref e))do{rows.Add(new Row{Id=(int)e.Id,ParentId=(int)e.Parent,Name=e.Name});e.Size=(uint)Marshal.SizeOf(typeof(Entry));}while(Process32NextW(h,ref e));return rows.ToArray();}finally{CloseHandle(h);}
 }
 private static Row ReadHandle(IntPtr h,int id){
  var r=new Row{Id=id};long exit,kernel,user;int session;GetProcessTimes(h,out r.Birth,out exit,out kernel,out user);if(ProcessIdToSessionId(id,out session))r.Session=session;
  var text=new StringBuilder(32768);int length=text.Capacity;if(!QueryFullProcessImageName(h,0,text,ref length)){r.Error=Marshal.GetLastWin32Error();return r;}r.Path=text.ToString();r.Name=Path.GetFileName(r.Path);r.Readable=r.Birth>0&&r.Session>=0;
  uint n=2048;var package=new StringBuilder((int)n);if(GetPackageFullName(h,ref n,package)==0)r.Package=package.ToString();n=2048;package=new StringBuilder((int)n);if(GetPackageFamilyName(h,ref n,package)==0)r.Family=package.ToString();n=2048;package=new StringBuilder((int)n);if(GetApplicationUserModelId(h,ref n,package)==0)r.AppId=package.ToString();
  IntPtr token;if(OpenProcessToken(h,8,out token)){try{using(var identity=new WindowsIdentity(token)){if(identity.User!=null)r.User=identity.User.Value;}}catch{}finally{CloseHandle(token);}}return r;
 }
 public static Row Read(int id){IntPtr h=OpenProcess(0x1000,false,id);if(h==IntPtr.Zero)return new Row{Id=id,Error=Marshal.GetLastWin32Error()};try{return ReadHandle(h,id);}finally{CloseHandle(h);}}
 public static bool ExactXbox(Row r,int session){return r!=null&&r.Readable&&r.Session==session&&(String.Equals(r.Name,"XboxPcApp.exe",StringComparison.OrdinalIgnoreCase)||String.Equals(r.Name,"XboxPcAppCE.exe",StringComparison.OrdinalIgnoreCase)||String.Equals(r.Name,"XboxPcAppFT.exe",StringComparison.OrdinalIgnoreCase));}
 public static bool TrustedPackage(Row r,int session,string sid){return ExactXbox(r,session)&&!String.IsNullOrEmpty(sid)&&r.User==sid&&String.Equals(r.Family,"Microsoft.GamingApp_8wekyb3d8bbwe",StringComparison.OrdinalIgnoreCase)&&r.Package.StartsWith("Microsoft.GamingApp_",StringComparison.OrdinalIgnoreCase)&&r.Package.EndsWith("__8wekyb3d8bbwe",StringComparison.OrdinalIgnoreCase);}
 public static string StopExact(int id,long birth,int session){IntPtr h=OpenProcess(0x101001,false,id);if(h==IntPtr.Zero)return "open-failed win32="+Marshal.GetLastWin32Error();try{var r=ReadHandle(h,id);if(!ExactXbox(r,session)||r.Birth!=birth)return "identity-changed;not-terminated";if(!TerminateProcess(h,1))return "terminate-failed win32="+Marshal.GetLastWin32Error();return "terminate-requested exited="+(WaitForSingleObject(h,100)==0);}finally{CloseHandle(h);}}
 public static string StopPackage(int id,long birth,int session,string package,string sid){var r=Read(id);if(!TrustedPackage(r,session,sid)||r.Birth!=birth||r.Package!=package)return "package-identity-rejected";if(!YmBootSessions.SingleSession(session))return "package-other-or-unknown-session-rejected";object instance=null;try{instance=Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("B1AEC16F-2383-4852-B0E9-8F0B1DC66B4D")));var api=(PackageDebug)instance;int selected=api.SetTargetSessionId((uint)session);if(selected<0)return "package-session-hr=0x"+selected.ToString("X8");r=Read(id);if(!TrustedPackage(r,session,sid)||r.Birth!=birth||r.Package!=package)return "package-identity-retired";int hr=api.TerminateAllProcesses(package);return "package-stop-hr=0x"+hr.ToString("X8")+";absence-requires-followup";}catch(Exception e){return "package-stop-exception hr=0x"+e.HResult.ToString("X8");}finally{if(instance!=null)Marshal.FinalReleaseComObject(instance);}}
}
public static class YmBootSessions {
 [StructLayout(LayoutKind.Sequential)] private struct Session { public int Id; public IntPtr Name; public int State; }
 [DllImport("wtsapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern bool WTSEnumerateSessions(IntPtr server,int reserved,int version,out IntPtr sessions,out int count);
 [DllImport("wtsapi32.dll")] private static extern void WTSFreeMemory(IntPtr memory);
 public static bool SingleSession(int session){IntPtr rows;int count;if(!WTSEnumerateSessions(IntPtr.Zero,0,1,out rows,out count))return false;try{int size=Marshal.SizeOf(typeof(Session));bool found=false;for(int i=0;i<count;i++){var row=(Session)Marshal.PtrToStructure(IntPtr.Add(rows,size*i),typeof(Session));if(row.Id==session)found=true;if(row.Id>0&&row.Id!=session)return false;}return found;}finally{WTSFreeMemory(rows);}}
}

"@
}
function Read-BootProcess([int]$Id) { [YmBootProcess]::Read($Id) }
function Get-BootXboxTargets {
    $script:xboxInventory=@([YmBootProcess]::Inventory())
    $script:xboxParents=@{}
    foreach($entry in $script:xboxInventory){$script:xboxParents[$entry.Id]=$entry}
    foreach($entry in $script:xboxInventory){
        if($entry.Name -notin @('XboxPcApp.exe','XboxPcAppCE.exe','XboxPcAppFT.exe')){continue}
        $row=Read-BootProcess $entry.Id
        if($row.Session -eq $script:session -or (-not $row.Readable -and $row.Session -lt 0)){
            $row.ParentId=$entry.ParentId
            if(-not $row.Name){$row.Name=$entry.Name}
            $row
        }
    }
}
function Get-BootProcessLineage($Target) {
    $chain=@();$seen=@{};$parentId=[int]$Target.ParentId;$childBirth=[long]$Target.Birth
    for($depth=0;$depth -lt 4 -and $parentId -gt 0;$depth++){
        if($seen.ContainsKey($parentId)){break};$seen[$parentId]=$true
        $parent=Read-BootProcess $parentId
        # A reused parent PID cannot become a source attribution or a kill target.
        $relation=if(-not $parent.Readable){'unreadable'}elseif($childBirth -gt 0 -and $parent.Birth -gt $childBirth){'parent-pid-reused'}else{'snapshot-parent;not-restart-owner-proof'}
        $chain+=[pscustomobject]@{pid=$parentId;birth=$parent.Birth;name=$parent.Name;path=$parent.Path;package=$parent.Package;session=$parent.Session;relation=$relation;error=$parent.Error}
        if($relation -ne 'snapshot-parent;not-restart-owner-proof' -or -not $script:xboxParents.ContainsKey($parentId)){break}
        $childBirth=$parent.Birth;$parentId=$script:xboxParents[$parentId].ParentId
    }
    return $chain
}
function Invoke-BootPackageStop($Target) {
    # Run COM in an owned hidden helper; Start-Quiet bounds and retires that helper.
    $payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((@{id=$Target.Id;birth=$Target.Birth;session=$script:session;package=$Target.Package;sid=$script:bootUserSid}|ConvertTo-Json -Compress)))
    $command='function Initialize-BootProcessApi {'+${function:Initialize-BootProcessApi}.ToString()+"}`r`nInitialize-BootProcessApi`r`n"+
        '$t=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('''+$payload+'''))|ConvertFrom-Json;[YmBootProcess]::StopPackage([int]$t.id,[long]$t.birth,[int]$t.session,[string]$t.package,[string]$t.sid)'
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    Start-Quiet (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand '+$encoded) -Capture
}
function Close-BootXbox($Processes) {
    $now=$script:bootClock.ElapsedMilliseconds
    foreach($target in @($Processes)){
        if($script:bootClock.ElapsedMilliseconds -ge 60000){break}
        $key=[string]$target.Id+':'+$target.Birth
        if(-not $script:xboxSeen.ContainsKey($key)){
            $script:xboxSeen[$key]=1;$script:xboxObserved++
            $detail=[ordered]@{pid=$target.Id;birth=$target.Birth;session=$target.Session;name=$target.Name;path=$target.Path;package=$target.Package;family=$target.Family;appId=$target.AppId;user=$target.User;readable=$target.Readable;error=$target.Error;observed=$script:xboxObserved;parents=@(Get-BootProcessLineage $target)}
            Write-BootLog 'xbox-observed' ($detail|ConvertTo-Json -Compress -Depth 6)
        }
        if(-not [YmBootProcess]::ExactXbox($target,$script:session)){
            if(-not $script:xboxDenied.ContainsKey($key)){$script:xboxDenied[$key]=$true;Write-BootLog 'xbox-cleanup-refused' ('pid='+$target.Id+' reason=identity-or-session-unverified error='+$target.Error)}
            continue
        }
        $state=$script:xboxAttempts[$key]
        if(-not $state){$state=@{at=-100000L;tries=0};$script:xboxAttempts[$key]=$state}
        if($state.tries -ge 3 -or ($state.tries -gt 0 -and ($now-$state.at) -lt 1000)){continue}
        # Stop the verified GamingApp package at most twice, ten seconds apart.
        # Ordinary per-PID cleanup continues for newly observed known Xbox images.
        $packageState=if($target.Package){$script:xboxPackages[$target.Package]}else{$null}
        if(-not $packageState){$packageState=@{tries=0;at=-100000};if($target.Package){$script:xboxPackages[$target.Package]=$packageState}}
        if([YmBootProcess]::TrustedPackage($target,$script:session,$script:bootUserSid) -and $packageState.tries -lt 2 -and ($now-$packageState.at) -ge 10000){
            $packageState.tries++;$packageState.at=$now
            $result=Invoke-BootPackageStop $target
            Write-BootLog 'xbox-package-stop' ('package='+$target.Package+' sourcePid='+$target.Id+' attempt='+$packageState.tries+' helperExit='+$result.Code+' result='+$result.Text)
        }
        $state.tries++;$state.at=$now
        $result=[YmBootProcess]::StopExact($target.Id,$target.Birth,$script:session)
        Write-BootLog 'xbox-pid-stop' ('pid='+$target.Id+' birth='+$target.Birth+' name='+$target.Name+' attempt='+$state.tries+' result='+$result)
    }
}
function Read-BootTail([string]$Path,[int]$Maximum=131072) {
    if(-not [IO.File]::Exists($Path)){return ''}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try{
        $offset=[Math]::Max(0,$stream.Length-$Maximum);$stream.Position=$offset
        $reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false),$true,4096,$true)
        try{if($offset -gt 0){$reader.ReadLine()|Out-Null};return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}
function Get-BootInputEvidence($Rows,[int]$OwnerPid,[long]$Birth,[string]$Persona,[switch]$RequireCurrentFrame) {
    $started=$null;$ready=$null;$stopped=$null;$source=$null
    foreach($row in $Rows){
        if($row.pid -ne $OwnerPid){continue}
        $at=[DateTime]::MinValue
        if(-not [DateTime]::TryParse([string]$row.time,[ref]$at) -or $at.ToUniversalTime().ToFileTimeUtc() -lt $Birth){continue}
        switch([string]$row.event){
            'input-host-started' {$started=$row;$ready=$null;$stopped=$null}
            'input-host-fresh-neutral-admission' {if($started -and $row.powerGeneration -eq $started.powerGeneration -and $row.status -eq 'admitted'){$ready=$row}}
            'input-host-stop-request' {$stopped=$row}
            'input-host-stop-result' {$stopped=$row}
            'input-host-persona-transition-requested' {$stopped=$row}
            'input-host-fault-cooldown' {$stopped=$row}
            'input-host-frame-evidence' {$source=$row}
        }
    }
    $accepted=$started -and $ready -and -not $stopped -and $started.persona -eq $Persona -and $started.transportUsable -eq $true
    # A long-running YMCC may have rotated its startup rows. A recent actual ACK
    # from the current owner/run can satisfy local readiness without replaying start.
    $receipt=if($source){$source.frameProof.hostReceipt}else{$null};$recentFrame=$false
    if($receipt -and -not $stopped -and $source.hostSubmission -eq 'frame-accepted' -and $source.powerLifecycle -eq 'ready' -and $receipt.hostAckAccepted -eq $true -and $receipt.persona -eq $Persona -and $receipt.runId -and $receipt.epoch -gt 0){
        $frameTime=[DateTime]::MinValue
        if([DateTime]::TryParse([string]$source.time,[ref]$frameTime)){$age=([DateTime]::Now-$frameTime).TotalMilliseconds;$recentFrame=$age -ge 0 -and $age -le 5000}
    }
    if($RequireCurrentFrame){
        $tupleCurrent=$recentFrame -and $receipt.hostInstanceId -and $receipt.tupleCapturedBeforeRequest -eq $true -and $source.source.live -eq $true -and $source.source.powerGeneration -eq $source.powerGeneration
        if($started){
            $startTime=[DateTime]::MinValue
            $afterStart=[DateTime]::TryParse([string]$started.time,[ref]$startTime) -and $frameTime -ge $startTime
            $tupleCurrent=$tupleCurrent -and $afterStart -and $started.runId -eq $receipt.runId -and $started.epoch -eq $receipt.epoch -and $started.powerGeneration -eq $source.powerGeneration -and $started.boundPersona -eq $receipt.persona -and $started.personaDrift -ne $true
        }
        $accepted=[bool]$tupleCurrent
    }else{$accepted=$accepted -or $recentFrame}

    [pscustomobject]@{state=$(if($accepted){'local-target-admitted'}else{'waiting-current-target'});ready=[bool]$accepted;ownerPid=$OwnerPid;persona=$Persona;runId=$(if($recentFrame){$receipt.runId}elseif($started){$started.runId}else{''});powerGeneration=$(if($recentFrame){$source.powerGeneration}elseif($started){$started.powerGeneration}else{0});lastHostEvent=$(if($stopped){$stopped.event}elseif($ready){$ready.event}elseif($started){$started.event}else{''});source=$(if($source){$source.source}else{$null});steamConsumptionProven=$false}
}
function Get-BootFocusOwnerContext($Rows,$Owners,[string]$Persona) {
    # Launcher/helper processes can share the YeManCC name. Only one live candidate
    # with a current PID/birth-bound accepted-frame receipt may own this handoff.
    $matches=@()
    foreach($owner in @($Owners)){
        try{
            $birth=$owner.StartTime.ToUniversalTime().ToFileTimeUtc()
            $candidate=Get-BootInputEvidence $Rows $owner.Id $birth $Persona -RequireCurrentFrame
            if($candidate.ready){$matches+=,$candidate}
        }catch{}
    }
    if($matches.Count -eq 1){return $matches[0]}
    [pscustomobject]@{state='waiting-current-target';ready=$false;persona=$Persona;owners=@($Owners).Count;receiptOwners=$matches.Count;steamConsumptionProven=$false}
}
function Get-BootInputContext([switch]$RequireCurrentFrame) {
    $now=$script:bootClock.ElapsedMilliseconds
    if($RequireCurrentFrame){if($script:focusInputContext -and ($now-$script:focusInputContextAt) -lt 500){return $script:focusInputContext}}
    elseif($script:inputContext -and ($now-$script:inputContextAt) -lt 1000){return $script:inputContext}
    $context=[pscustomobject]@{state='unavailable';ready=$false;persona='unknown';steamConsumptionProven=$false};$owners=@()
    try{
        $dir=Join-Path $env:LOCALAPPDATA 'YeManCC'
        $settings=[IO.File]::ReadAllText('C:\SOFT\YeMan\PowerControl\yeman-settings.json',[Text.Encoding]::UTF8)|ConvertFrom-Json
        $target=$settings.input.outputTarget;$persona=[string]$target.persona
        if($persona -in @('disabled','off','none') -or ($target.buttonMappingEnabled -eq $false -and $target.gyroEnabled -eq $false)){
            $context=[pscustomobject]@{state='not-requested';ready=$true;persona=$persona;steamConsumptionProven=$false}
        }else{
            $owners=@(Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -eq $script:session})
            if($owners.Count -gt 0 -and ($RequireCurrentFrame -or $owners.Count -eq 1)){
                $rows=@();foreach($line in ((Read-BootTail (Join-Path $dir 'native-lifecycle.log')) -split '\r?\n')){if($line -match '"event"\s*:\s*"input-host-(started|fresh-neutral-admission|stop-request|stop-result|persona-transition-requested|fault-cooldown|frame-evidence)"'){try{$rows+=($line|ConvertFrom-Json)}catch{}}}
                if($RequireCurrentFrame){$context=Get-BootFocusOwnerContext $rows $owners $persona}
                else{$owner=$owners[0];$birth=$owner.StartTime.ToUniversalTime().ToFileTimeUtc();$context=Get-BootInputEvidence $rows $owner.Id $birth $persona}
            }else{$context=[pscustomobject]@{state='ymcc-not-unique-or-starting';ready=$false;persona=$persona;owners=$owners.Count;steamConsumptionProven=$false}}
        }
    }catch{$context=[pscustomobject]@{state='unavailable';ready=$false;persona='unknown';error=$_.Exception.Message;steamConsumptionProven=$false}}finally{foreach($ownerProcess in @($owners)){if($ownerProcess -is [Diagnostics.Process]){$ownerProcess.Dispose()}}}
    if($RequireCurrentFrame){$script:focusInputContext=$context;$script:focusInputContextAt=$now}else{$script:inputContext=$context;$script:inputContextAt=$now};return $context
}
function Write-BootCheckpoint([string]$Stage,$Snapshot) {
    $now=$script:bootClock.ElapsedMilliseconds
    $input=Get-BootInputContext
    $signature=$input.state+':'+$input.persona+':'+$input.ownerPid+':'+$input.lastHostEvent
    if($Stage -in @('poll','prewarm') -and $signature -eq $script:lastInputSignature -and ($now-$script:lastInputLogAt) -lt 5000){return}
    $script:lastInputSignature=$signature;$script:lastInputLogAt=$now
    $front=Get-BootForeground
    $detail=[ordered]@{stage=$Stage;input=$input;foreground=@{hwnd=$front.Hwnd.ToInt64();pid=$front.Pid;name=$front.Name;class=$front.Class};fse=[YmBootWindow]::GamingContext();xboxRemaining=@($Snapshot.Xbox|ForEach-Object {@{pid=$_.Id;birth=$_.Birth;name=$_.Name;path=$_.Path;package=$_.Package;parentId=$_.ParentId}});xboxObserved=$script:xboxObserved;steamRunning=$Snapshot.SteamRunning;steamWindow=$Snapshot.SteamWindow;pipeline=(Get-BootPipelineEvidence);scope='read-only sampled state; not physical identity, key delivery or Steam readiness proof'}
    Write-BootLog 'boot-input-context' ($detail|ConvertTo-Json -Compress -Depth 7)
}
function Complete-BootObservation([string]$Stage,$Snapshot) {
    Write-BootCheckpoint $Stage $Snapshot
    Write-BootLog 'xbox-cleanup-result' (([ordered]@{stage=$Stage;remaining=@($Snapshot.Xbox|ForEach-Object {@{pid=$_.Id;name=$_.Name;birth=$_.Birth}});observed=$script:xboxObserved;noKnownImagesObserved=(@($Snapshot.Xbox).Count -eq 0);packageAttempts=@($script:xboxPackages.GetEnumerator()|ForEach-Object {@{package=$_.Key;attempts=$_.Value.tries}});fse=[YmBootWindow]::GamingContext();scope='snapshot-only; not persistent Xbox/FSE removal'}|ConvertTo-Json -Compress -Depth 5))
}
function Invoke-BootInputWarmup($P) {
    # Compatibility entry name; one read-only observation, never a launch gate.
    # A false/unknown local receipt remains diagnostic and cannot delay Steam.
    $start=& $P.Now;$context=& $P.InputContext
    & $P.Log 'input-prewarm' ('stage=observe-only policy=BAT-STEAM-EARLY-LAUNCH-20261024-R1 elapsedMs='+((& $P.Now)-$start)+' state='+$context.state+' localReady='+$context.ready+' limitMs=0 launchBlocked=False steamConsumptionProven=False')
}
function Get-BootPipelineEvidence {
    $result=[ordered]@{physical=$null;publication=$null;sdk=$null;errors=@()}
    $dir=Join-Path $env:LOCALAPPDATA 'YeManCC'
    foreach($file in @('native-detail.log','focus-trace.log','input-host-cost.log')){
        try{
            $tail=Read-BootTail (Join-Path $dir $file) 262144
            $lines=$tail -split '\r?\n'
            for($index=$lines.Length-1;$index -ge 0;$index--){
                $line=$lines[$index]
                if($file -eq 'native-detail.log' -and $line -notmatch '"gamepad-xinput-input"'){continue}
                if($file -eq 'focus-trace.log' -and $line -notmatch '"input-signal-activity"'){continue}
                if($file -eq 'input-host-cost.log' -and $line -notmatch '"steamDeckPadSubmissions"'){continue}
                try{$row=$line|ConvertFrom-Json}catch{continue}
                if($file -eq 'native-detail.log'){
                    $result.physical=[ordered]@{time=$row.time;pid=$row.pid;source=$row.source;connected=$row.connected;slot=$row.primarySlot;admittedMask=$row.admittedMask;buttons=$row.buttonMask;lx=$row.leftX;ly=$row.leftY;rx=$row.rightX;ry=$row.rightY;lt=$row.leftTrigger;rt=$row.rightTrigger}
                }elseif($file -eq 'focus-trace.log'){
                    $result.publication=[ordered]@{time=$row.time;pid=$row.writerPid;runId=$row.hostRunId;epoch=$row.hostEpoch;physical=$row.physicalBeforePublication;attempted=$row.actualSubmitAttempts;accepted=$row.hostAcceptedContent;rejected=$row.hostRejectedAttempts}
                }else{
                    $report=$row.steamDeckPadSubmissions
                    $result.sdk=[ordered]@{atUnixMs=$row.t;hostPid=$row.writerPid;hostInstanceId=$row.hostInstanceId;runId=$row.runId;epoch=$row.epoch;reports=$report.Reports;edges=$report.Edges;omitted=$report.OmittedRows;entries=$report.Entries;lastReport=$report.LastSuccessfulReport;cachedTransport=$row.usbipTransport.snapshot;scope='cumulative report edge ring; deduplicate command/sample; not USB or Steam consumption'}
                }
                break
            }
        }catch{$result.errors+=($file+':'+$_.Exception.Message)}
    }
    return $result
}
function Invoke-BootDiagnosticTail($P) {
    # A bounded read-only tail captures the user's Xbox exit and late recovery.
    # It does not extend cleanup, foreground activation or application launch authority.
    $begin=& $P.Now
    for($round=1;$round -le 6 -and ((& $P.Now)-$begin) -lt 60000;$round++){
        & $P.Wait
        $snapshot=& $P.Snapshot
        & $P.Checkpoint 'post-boot' $snapshot
    }
    & $P.Log 'boot-diagnostics-complete' ('elapsedMs='+((& $P.Now)-$begin)+' readOnly=True')
}

function Start-YeManBoot {
    $script:session=[Diagnostics.Process]::GetCurrentProcess().SessionId
    $mutex=[Threading.Mutex]::new($false,'Local\YeMan.SteamBoot.SingleBat.v1');$held=$false
    try {
        try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true}
        if(-not $held){Write-BootLog 'boot-skip' 'already-active';return 0}
        $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        Write-BootLog 'boot-context' ('administrator='+$admin+' session='+$script:session)
        $script:steamExe=Find-SteamExe
        $script:steamRoot=if($script:steamExe){[IO.Path]::GetDirectoryName($script:steamExe).TrimEnd('\')}else{''}
        $script:steamWindow=[IntPtr]::Zero;$script:steamWindowPid=0;$script:steamWindowBirth=0L;$script:lastClose=@{}
        Initialize-WindowApi
        Initialize-BootProcessApi
        Write-BootLog 'window-context' ([YmBootWindow]::GamingContext())
        $clock=[Diagnostics.Stopwatch]::StartNew();$script:bootClock=$clock
        $script:bootUserSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        $script:xboxSeen=@{};$script:xboxAttempts=@{};$script:xboxPackages=@{};$script:xboxDenied=@{};$script:xboxObserved=0
        $script:inputContext=$null;$script:inputContextAt=-100000L;$script:focusInputContext=$null;$script:focusInputContextAt=-100000L;$script:lastInputSignature='';$script:lastInputLogAt=-100000L
        $ports=@{
            Now={$clock.ElapsedMilliseconds};Wait={Start-Sleep -Milliseconds 500};Log={param($E,$D)Write-BootLog $E $D}
            Snapshot={Get-BootSnapshot};CloseXbox={param($P)Close-BootXbox $P}
            YMCC={Start-BootYMCC};StartSteam={Request-Steam $false};ShowSteam={Request-Steam $true}
            Affinity={Set-BootAffinity}
            Checkpoint={param($Stage,$Snapshot)Write-BootCheckpoint $Stage $Snapshot}
            Complete={param($Stage,$Snapshot)Complete-BootObservation $Stage $Snapshot}
        }
        $warmup=@{Now=$ports.Now;Wait=$ports.Wait;Snapshot=$ports.Snapshot;CloseXbox=$ports.CloseXbox;Log=$ports.Log;Checkpoint=$ports.Checkpoint;InputContext={Get-BootInputContext}}
        $ports.InputWarmup={Invoke-BootInputWarmup $warmup}
        $result=Invoke-BootFlow $ports
        $tail=@{Now=$ports.Now;Wait={Start-Sleep -Seconds 10};Snapshot=$ports.Snapshot;Checkpoint=$ports.Checkpoint;Log=$ports.Log}
        Invoke-BootDiagnosticTail $tail
        return $result
    }catch{Write-BootLog 'boot-error' $_.Exception.Message;return 1}
    finally{if($held){$mutex.ReleaseMutex()};$mutex.Dispose()}
}
exit (Start-YeManBoot)