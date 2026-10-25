# A single same-user/session backend survives bounded helpers and YMCC exit.
# No periodic search, kill/restart fallback, Steam launch, or focus operation.
$script:AppearanceProcessQueryProtocol=1
function Initialize-ResidentAppearanceInterop {
    if('Ymcc.CssResidentProcess' -as [type]){return}
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace Ymcc {
 public static class CssResidentProcess {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  struct Startup { public int cb; public string reserved, desktop, title; public int x,y,xsize,ysize,xchars,ychars,fill,flags; public short show,reserved2; public IntPtr reservedPtr,input,output,error; }
  [StructLayout(LayoutKind.Sequential)]
  struct Info { public IntPtr process,thread; public uint pid,tid; }
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern bool CreateProcess(string app,StringBuilder command,IntPtr pa,IntPtr ta,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out Info info);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint rights,bool inherit,uint pid);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process,uint flags,StringBuilder path,ref uint size);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long birth,out long exit,out long kernel,out long user);
  public static string ReadPath(uint pid,long expectedBirth) {
   var handle=OpenProcess(0x1000,false,pid);
   if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Appearance process identity is unreadable");
   try {
    long birth,exit,kernel,user;
    if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user))throw new Win32Exception(Marshal.GetLastWin32Error());
    if(birth!=expectedBirth)throw new InvalidOperationException("Appearance process birth changed");
    uint size=32768;var path=new StringBuilder((int)size);
    if(!QueryFullProcessImageName(handle,0,path,ref size))throw new Win32Exception(Marshal.GetLastWin32Error());
    return path.ToString();
   }finally{CloseHandle(handle);}
  }
  public static uint Start(string executable,string directory) {
   var startup=new Startup();startup.cb=Marshal.SizeOf(startup);startup.flags=1;startup.show=0;
   Info child;
   // Only this fixed backend escapes the helper Job; the helper and all other
   // children retain bounded ownership. Fail closed if an outer Job denies it.
   if(!CreateProcess(executable,new StringBuilder("\""+executable+"\""),IntPtr.Zero,IntPtr.Zero,false,0x01000000u|0x08000000u,IntPtr.Zero,directory,ref startup,out child))
    throw new Win32Exception(Marshal.GetLastWin32Error(),"Resident CSS launch denied; no fallback instance started");
   try{return child.pid;}finally{CloseHandle(child.thread);CloseHandle(child.process);}
  }
 }
}
"@
}
function Get-AppearanceProcessPath($Process) {
    if(!$Process){throw 'Appearance process missing'}
    $image=$null;try{$image=[string]$Process.Path}catch{}
    if($image){return $image}
    # .NET MainModule/Path can be unavailable across bitness/elevation even when
    # QueryFullProcessImageName is permitted. Pin birth on the same limited handle.
    $birth=$Process.StartTime.ToUniversalTime().ToFileTimeUtc()
    Initialize-ResidentAppearanceInterop
    [Ymcc.CssResidentProcess]::ReadPath([uint32]$Process.Id,[long]$birth)
}
function Start-ResidentAppearanceBackend {
    $exe=[IO.Path]::GetFullPath($script:BackendExe)
    if([IO.Path]::GetFileName($exe) -ine 'CssLoader-Standalone-Headless.exe' -or !(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'CSSLoader executable missing or invalid'}
    for($cursor=$exe;$cursor;$cursor=[IO.Path]::GetDirectoryName($cursor)){
        if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'CSSLoader executable link denied'}
        if([IO.Path]::GetDirectoryName($cursor) -eq $cursor){break}
    }
    Initialize-ResidentAppearanceInterop
    $null=[Ymcc.CssResidentProcess]::Start($exe,[IO.Path]::GetDirectoryName($exe))
}
function Assert-SingleAppearanceBackend($Processes) {
    $list=@($Processes)
    if(!$list.Count -or $list.Count -gt 128){throw 'CSSLoader instance identity unconfirmed; no second instance started'}
    $ids=@{};$roots=@{}
    foreach($process in $list){
        if(!(Test-CurrentUserProcess $process)){throw 'CSSLoader owner changed'}
        $image=Get-AppearanceProcessPath $process
        if([IO.Path]::GetFullPath($image) -ine [IO.Path]::GetFullPath($script:BackendExe)){throw 'CSSLoader path changed'}
        if($process.HasExited){throw 'CSSLoader process exited during verification'}
        $ids[[uint32]$process.Id]=$process
    }
    if($ids.Count -ne $list.Count){throw 'CSSLoader process identity changed'}
    # One bounded snapshot at the use boundary. PyInstaller's parent and child
    # are one instance; two independent roots are ambiguous and never replaced.
    $parents=@{}
    foreach($row in @(Get-CimInstance Win32_Process -Filter "Name = 'CssLoader-Standalone-Headless.exe'" -OperationTimeoutSec 2 -ErrorAction Stop)){$parents[[uint32]$row.ProcessId]=[uint32]$row.ParentProcessId}
    foreach($process in $list){
        $cursor=[uint32]$process.Id;$seen=@{}
        if(!$parents.ContainsKey($cursor) -or $process.HasExited){throw 'CSSLoader process snapshot changed'}
        while($parents.ContainsKey($cursor) -and $ids.ContainsKey([uint32]$parents[$cursor])){
            if($seen.ContainsKey($cursor)){throw 'CSSLoader process ancestry invalid'}
            $seen[$cursor]=$true;$parent=[uint32]$parents[$cursor]
            if($ids[$parent].StartTime.ToUniversalTime() -gt $ids[$cursor].StartTime.ToUniversalTime()){throw 'CSSLoader process ancestry changed'}
            $cursor=$parent
        }
        $roots[$cursor]=$true
    }
    if($roots.Count -ne 1){throw 'Multiple CSSLoader instances detected; all retained and no new instance started'}
}
