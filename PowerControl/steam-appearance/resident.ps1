# A single same-user/session backend survives bounded helpers and YMCC exit.
# No periodic search, kill/restart fallback, Steam launch, or focus operation.
$script:AppearanceProcessQueryProtocol=2
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
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint rights,out IntPtr token);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
  [StructLayout(LayoutKind.Sequential)] struct Basic { public IntPtr reserved,peb,reserved2,reserved3,pid,parent; }
  [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int kind,ref Basic basic,int size,out int returned);
  public static string[] Identity(uint pid,long expectedBirth) {
   var handle=OpenProcess(0x101000,false,pid);IntPtr token=IntPtr.Zero;
   if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
   try {
    long birth,exit,kernel,user;
    if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user)||birth!=expectedBirth||WaitForSingleObject(handle,0)!=258)throw new InvalidOperationException("Appearance process identity changed");
    uint size=32768;var path=new StringBuilder((int)size);
    if(!QueryFullProcessImageName(handle,0,path,ref size)||!OpenProcessToken(handle,8,out token))throw new Win32Exception(Marshal.GetLastWin32Error());
    var basic=new Basic();int returned;
    if(NtQueryInformationProcess(handle,0,ref basic,Marshal.SizeOf(basic),out returned)!=0)throw new InvalidOperationException("Appearance process parent is unknown");
    string sid;using(var identity=new System.Security.Principal.WindowsIdentity(token)){sid=identity.User.Value;}
    if(WaitForSingleObject(handle,0)!=258)throw new InvalidOperationException("Appearance process exited");
    return new string[]{path.ToString(),sid,basic.parent.ToInt64().ToString(),birth.ToString()};
   }finally{if(token!=IntPtr.Zero)CloseHandle(token);CloseHandle(handle);}
  }
  public static uint[] Inventory(string name) {
   using(var gate=System.Threading.Mutex.OpenExisting(name+".lock")) {
    bool locked=false;
    try {
     try{locked=gate.WaitOne(3000);}catch(System.Threading.AbandonedMutexException){locked=true;}
     if(!locked)throw new InvalidOperationException("YMCC shared process request is busy");
     using(var map=System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(name))
     using(var view=map.CreateViewAccessor())
     using(var query=System.Threading.EventWaitHandle.OpenExisting(name+".query"))
     using(var reply=System.Threading.EventWaitHandle.OpenExisting(name+".reply")) {
      if(view.ReadUInt32(0)!=0x59435049||view.ReadUInt32(4)!=1)throw new InvalidOperationException("YMCC shared process protocol mismatch");
      uint serial=(view.ReadUInt32(8)%2147483646)+1;
      reply.Reset();view.Write(8,serial);System.Threading.Thread.MemoryBarrier();query.Set();
      var watch=System.Diagnostics.Stopwatch.StartNew();
      while(watch.ElapsedMilliseconds<5000) {
       if(!reply.WaitOne((int)Math.Max(1,5000-watch.ElapsedMilliseconds)))break;
       if(view.ReadUInt32(12)!=serial)continue;
       System.Threading.Thread.MemoryBarrier();var result=new uint[16+128*5];view.ReadArray<uint>(0,result,0,result.Length);
       if(result[3]!=serial||result[4]!=1||result[5]>128)throw new InvalidOperationException("YMCC shared process state is unknown ("+result[9]+")");
       return result;
      }
      throw new InvalidOperationException("YMCC shared process request timed out");
     }
    }finally{if(locked)gate.ReleaseMutex();}
   }
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
    $script:AppearanceStartedPid=[Ymcc.CssResidentProcess]::Start($exe,[IO.Path]::GetDirectoryName($exe))
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
    foreach($process in $list){
        $parents[[uint32]$process.Id]=Get-AppearanceParentId $process
    }
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

# Only the native YMCC process owns discovery. Helpers receive filtered candidates
# through its existing use worker and recheck exact identities locally.
function Update-AppearanceProcessInventory {
    if(!$ProcessQuery -or $ProcessQuery -notmatch '^Local\\YMCC\.CssInventory\.[a-f0-9]{32}$'){throw 'YMCC shared process channel unavailable; no private process scan permitted'}
    Initialize-ResidentAppearanceInterop
    $script:AppearanceProcessReceipt=$null # A failed fresh request revokes prior absence.
    $reply=[Ymcc.CssResidentProcess]::Inventory($ProcessQuery)
    $rows=@()
    for($index=0;$index -lt [int]$reply[5];$index++){
        $offset=16+$index*5
        $birth=([uint64]$reply[$offset+3] -shl 32) -bor [uint64]$reply[$offset+2]
        $rows+=,[pscustomobject]@{pid=[uint32]$reply[$offset];parent=[uint32]$reply[$offset+1];birth=[string]$birth;kind=[uint32]$reply[$offset+4]}
    }
    $steamBirth=([uint64]$reply[8] -shl 32) -bor [uint64]$reply[7]
    $script:AppearanceProcessReceipt=[pscustomobject]@{rows=$rows;steamPid=[uint32]$reply[6];steamBirth=[string]$steamBirth}
    $script:AppearanceProcessReceipt
}
function Get-AppearanceProcessCandidates([int]$Kind,[switch]$Refresh) {
    if($Refresh -or !$script:AppearanceProcessReceipt){$null=Update-AppearanceProcessInventory}
    foreach($row in @($script:AppearanceProcessReceipt.rows|Where-Object kind -eq $Kind)){
        $process=$null
        try{$process=Get-Process -Id $row.pid -ErrorAction Stop}catch{
            if($_.FullyQualifiedErrorId -like 'NoProcessFoundForGivenId*'){continue}
            throw
        }
        if($process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne $row.birth){throw 'YMCC candidate birth changed; state unknown'}
        if(!$process.HasExited){$process}
    }
}
function Get-AppearanceBackendCandidates {
    $known=@{}
    foreach($process in @(Get-AppearanceProcessCandidates 1)){$known[[uint32]$process.Id]=$process}
    # A known CreateProcess result and the fixed port owner are directed PID
    # observations, including the PyInstaller child which appears after startup.
    $pids=@()
    if($script:AppearanceStartedPid){$pids+=,[uint32]$script:AppearanceStartedPid}
    foreach($listener in @(Get-NetTCPConnection -State Listen -LocalPort 35821 -ErrorAction SilentlyContinue)){$pids+=,[uint32]$listener.OwningProcess}
    foreach($processId in @($pids|Select-Object -Unique)){
        if($known.ContainsKey($processId)){continue}
        $process=Get-Process -Id $processId -ErrorAction SilentlyContinue
        if(!$process){continue}
        if([IO.Path]::GetFileName((Get-AppearanceProcessPath $process)) -ieq 'CssLoader-Standalone-Headless.exe'){$known[$processId]=$process}
    }
    @($known.Values)
}

function Get-AppearanceParentId($Process) {
    Initialize-ResidentAppearanceInterop
    $identity=[Ymcc.CssResidentProcess]::Identity([uint32]$Process.Id,[long]$Process.StartTime.ToUniversalTime().ToFileTimeUtc())
    [uint32]$identity[2]
}
