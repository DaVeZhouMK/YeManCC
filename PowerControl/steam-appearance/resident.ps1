# A single same-user/session backend survives bounded helpers and YMCC exit.
# No periodic search, kill/restart fallback, Steam launch, or focus operation.
$script:AppearanceProcessQueryProtocol=3
$script:AppearanceListenerQueryProtocol=1
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
  [DllImport("iphlpapi.dll")] static extern uint GetExtendedTcpTable(IntPtr table,ref int size,bool sort,int family,int kind,uint reserved);
  public sealed class TcpListener {
   public string LocalAddress {get;private set;}
   public uint OwningProcess {get;private set;}
   public int LocalPort {get;private set;}
   internal TcpListener(string address,uint pid,int port){LocalAddress=address;OwningProcess=pid;LocalPort=port;}
  }
  static TcpListener[] DecodeTcpListeners(IntPtr table,int size,int family,int port) {
   if(table==IntPtr.Zero||size<4||(family!=2&&family!=23))throw new InvalidOperationException("Appearance listener table is invalid");
   int width=family==2?24:56;
   uint count=unchecked((uint)Marshal.ReadInt32(table));
   if(count>(uint)((size-4)/width))throw new InvalidOperationException("Appearance listener table is truncated");
   var rows=new System.Collections.Generic.List<TcpListener>();
   for(int i=0;i<(int)count;i++) {
    var row=IntPtr.Add(table,4+i*width);int portOffset=family==2?8:20;
    int localPort=(Marshal.ReadByte(row,portOffset)<<8)|Marshal.ReadByte(row,portOffset+1);
    if(localPort!=port)continue;
    int stateOffset=family==2?0:48,pidOffset=family==2?20:52;
    if(Marshal.ReadInt32(row,stateOffset)!=2)throw new InvalidOperationException("Appearance listener state changed");
    var address=new byte[family==2?4:16];Marshal.Copy(IntPtr.Add(row,family==2?4:0),address,0,address.Length);
    var ip=family==2?new System.Net.IPAddress(address):new System.Net.IPAddress(address,unchecked((uint)Marshal.ReadInt32(row,16)));
    rows.Add(new TcpListener(ip.ToString(),unchecked((uint)Marshal.ReadInt32(row,pidOffset)),localPort));
   }
   return rows.ToArray();
  }
  static TcpListener[] ReadTcpListeners(int family,int port) {
   int size=0;uint status=GetExtendedTcpTable(IntPtr.Zero,ref size,false,family,3,0);
   if(status!=0&&status!=122)throw new Win32Exception((int)status,"Appearance listener query failed");
   for(int attempt=0;attempt<3;attempt++) {
    if(size<4||size>8388608)throw new InvalidOperationException("Appearance listener table size is invalid");
    int allocated=size;var table=Marshal.AllocHGlobal(allocated);
    try {
     status=GetExtendedTcpTable(table,ref size,false,family,3,0);
     if(status==122)continue;
     if(status!=0)throw new Win32Exception((int)status,"Appearance listener query failed");
     if(size>allocated)throw new InvalidOperationException("Appearance listener table grew unexpectedly");
     return DecodeTcpListeners(table,size,family,port);
    }finally{Marshal.FreeHGlobal(table);}
   }
   throw new InvalidOperationException("Appearance listener table changed repeatedly");
  }
  public static TcpListener[] Listeners(int port) {
   if(port<1||port>65535)throw new ArgumentOutOfRangeException("port");
   var rows=new System.Collections.Generic.List<TcpListener>(ReadTcpListeners(2,port));
   rows.AddRange(ReadTcpListeners(23,port));return rows.ToArray();
  }
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
  static uint[] Exchange(string name,string request,out string response) {
   response=null;
   using(var gate=System.Threading.Mutex.OpenExisting(name+".lock")) {
    bool locked=false;
    try {
     try{locked=gate.WaitOne(3000);}catch(System.Threading.AbandonedMutexException){locked=true;}
     if(!locked)throw new InvalidOperationException("YMCC shared process request is busy");
     using(var map=System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(name))
     using(var view=map.CreateViewAccessor())
     using(var query=System.Threading.EventWaitHandle.OpenExisting(name+".query"))
     using(var reply=System.Threading.EventWaitHandle.OpenExisting(name+".reply")) {
      if(view.ReadUInt32(0)!=0x59435049||view.ReadUInt32(4)!=2)throw new InvalidOperationException("YMCC shared process protocol mismatch");
      uint owner=view.ReadUInt32(56);long ownerBirth=((long)view.ReadUInt32(64)<<32)|view.ReadUInt32(60);
      var parent=new Basic();int returned;
      using(var self=System.Diagnostics.Process.GetCurrentProcess()) {
       if(NtQueryInformationProcess(self.Handle,0,ref parent,Marshal.SizeOf(parent),out returned)!=0||parent.parent.ToInt64()!=owner)throw new InvalidOperationException("YMCC shared request parent mismatch");
      }
      var host=OpenProcess(0x101000,false,owner);
      if(host==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
      try {long born,exited,kernel,user;if(!GetProcessTimes(host,out born,out exited,out kernel,out user)||born!=ownerBirth||WaitForSingleObject(host,0)!=258)throw new InvalidOperationException("YMCC shared owner identity changed");}finally{CloseHandle(host);}
      uint serial=(view.ReadUInt32(8)%2147483646)+1;
      byte[] input=request==null?new byte[0]:new System.Text.UTF8Encoding(false,true).GetBytes(request);
      if(input.Length>65536)throw new InvalidOperationException("YMCC shared request is too large");
      reply.Reset();view.Write(40,request==null?0u:1u);view.Write(44,(uint)input.Length);view.Write(48,0u);
      if(input.Length!=0)view.WriteArray<byte>(2688,input,0,input.Length);
      view.Write(8,serial);System.Threading.Thread.MemoryBarrier();query.Set();
      var watch=System.Diagnostics.Stopwatch.StartNew();
      while(watch.ElapsedMilliseconds<10000) {
       if(!reply.WaitOne((int)Math.Max(1,10000-watch.ElapsedMilliseconds)))break;
       if(view.ReadUInt32(12)!=serial)continue;
       System.Threading.Thread.MemoryBarrier();var result=new uint[32+128*5];view.ReadArray<uint>(0,result,0,result.Length);
       if(result[3]!=serial||result[4]!=1||result[5]>128||result[12]>65536||result[14]!=owner||(((long)result[16]<<32)|result[15])!=ownerBirth)throw new InvalidOperationException("YMCC shared process state is unknown ("+result[9]+")");
       if(result[12]!=0){var output=new byte[result[12]];view.ReadArray<byte>(68224,output,0,output.Length);response=new System.Text.UTF8Encoding(false,true).GetString(output);}
       return result;
      }
      if(view.ReadUInt32(8)==serial){view.Write(8,(serial%2147483646)+1);System.Threading.Thread.MemoryBarrier();}
      throw new InvalidOperationException("YMCC shared process request timed out");
     }
    }finally{if(locked)gate.ReleaseMutex();}
   }
  }
  public static uint[] Inventory(string name){string ignored;return Exchange(name,null,out ignored);}
  public static string SharedSteam(string name,string request){string response;Exchange(name,request,out response);if(String.IsNullOrEmpty(response))throw new InvalidOperationException("YMCC shared Steam response is empty");return response;}
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
        $offset=32+$index*5
        $birth=([uint64]$reply[$offset+3] -shl 32) -bor [uint64]$reply[$offset+2]
        $rows+=,[pscustomobject]@{pid=[uint32]$reply[$offset];parent=[uint32]$reply[$offset+1];birth=[string]$birth;kind=[uint32]$reply[$offset+4]}
    }
    $steamBirth=([uint64]$reply[8] -shl 32) -bor [uint64]$reply[7]
    $script:AppearanceProcessReceipt=[pscustomobject]@{rows=$rows;steamPid=[uint32]$reply[6];steamBirth=[string]$steamBirth}
    $script:AppearanceProcessReceipt
}
function Test-AppearanceDocumentRecheck([string]$Reason,[switch]$FinalReadOnly) {
    # Native still owns a refreshed target set; the discarded batch is not a style proof.
    ($Reason -cin @('render-shared-steam-live-busy','render-shared-steam-document-main-unavailable','render-shared-steam-document-replaced','render-shared-steam-document-targets-refreshed')) -or
        ($FinalReadOnly -and $Reason -ceq 'render-shared-steam-document-transport-unavailable')
}
function Invoke-AppearanceSharedSteam($Request) {
    if($ProcessQuery -cnotmatch '^Local\\YMCC\.CssInventory\.[a-f0-9]{32}$'){throw 'YMCC shared Steam channel unavailable; update the matching native component'}
    Initialize-ResidentAppearanceInterop
    $requestText=$Request|ConvertTo-Json -Depth 12 -Compress
    $text=[Ymcc.CssResidentProcess]::SharedSteam($ProcessQuery,$requestText)
    $result=$text|ConvertFrom-Json
    if(!$result -or $result.ok -isnot [bool]){throw 'YMCC shared Steam response invalid'}
    # Preserve only the typed document reasons eligible for a bounded recheck.
    if(!$result.ok -and $Request.operation -ceq 'documents' -and $result.reason -is [string]){
        $reason='render-shared-steam-'+$result.reason
        if($result.reason -ceq 'document-targets-refreshed' -and (
            $result.mutated -isnot [bool] -or $result.mutated -or $result.complete -isnot [bool] -or $result.complete -or
            $result.documents -isnot [array] -or $result.documents.Count)) {throw 'render-shared-receipt-invalid'}
        if(Test-AppearanceDocumentRecheck $reason -FinalReadOnly){throw $reason}
    }
    if(!$result.ok){throw ('YMCC shared Steam observation unconfirmed: '+[string]$result.reason)}
    return $result
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
    foreach($listener in @(Get-AppearanceTcpListeners -LocalPort 35821)){$pids+=,[uint32]$listener.OwningProcess}
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
