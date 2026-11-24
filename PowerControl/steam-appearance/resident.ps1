# A single same-user/session backend survives bounded helpers and YMCC exit.
# No periodic search, kill/restart fallback, Steam launch, or focus operation.
$script:AppearanceProcessQueryProtocol=3
$script:AppearanceListenerQueryProtocol=1
$script:AppearanceBoundOwnerProbe=1
function Initialize-ResidentAppearanceInterop {
    if('Ymcc.CssResidentProcess' -as [type]){return}
    # The release packer fills this literal from the Add-Type source below.
    $compiledInterop='' # YMCC staged interop
    if($compiledInterop -and $ExecutionContext.SessionState.LanguageMode -eq 'FullLanguage'){
        try{$null=[Reflection.Assembly]::Load([Convert]::FromBase64String($compiledInterop))}
        catch [FormatException],[IO.FileLoadException],[BadImageFormatException],[NotSupportedException]{}
        if('Ymcc.CssResidentProcess' -as [type]){return}
    }
    Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.ComponentModel;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
namespace Ymcc {
 public static class CssResidentProcess {
  private static bool IsOrdinaryThemePath(string path,bool directory) {
   var flags=File.GetAttributes(path);
   return (flags&FileAttributes.ReparsePoint)==0 && ((flags&FileAttributes.Directory)!=0)==directory;
  }
  private static bool IsOrdinaryThemeInput(string path,string dir,string root) {
   return IsOrdinaryThemePath(dir,true)&&IsOrdinaryThemePath(root,true)&&IsOrdinaryThemePath(path,false);
  }
  private static string ReadThemeRevisionDigest(string path,HashAlgorithm sha) {
   using(var stream=File.OpenRead(path)) {
    if(stream.Length>262144)return null;
    return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-",String.Empty);
   }
  }
  // Only ordinary, complete theme pairs qualify for the digest-only batch.
  // The script retains its per-row behavior and diagnostics on any failure.
  public static string[] ReadThemeRevisionDigests(string themeRoot,string[] names) {
   if(names==null||names.Length==0||names.Length>32)return null;
   string root=Path.GetFullPath(themeRoot).TrimEnd('\\');
   string prefix=root+"\\";
   var invalid=Path.GetInvalidFileNameChars();
   var result=new string[2*names.Length];
   using(var sha=SHA256.Create()) {
    for(int i=0;i<names.Length;i++) {
     string name=names[i];
     if(String.IsNullOrEmpty(name)||name.IndexOfAny(invalid)>=0||name.EndsWith(".",StringComparison.Ordinal)||name.EndsWith(" ",StringComparison.Ordinal))return null;
     string dir=Path.GetFullPath(Path.Combine(prefix,name));
     string parent=Path.GetDirectoryName(dir);
     if(parent==null||!String.Equals(parent.TrimEnd('\\'),root,StringComparison.OrdinalIgnoreCase))return null;
     string manifest=Path.Combine(dir,"theme.json"),config=Path.Combine(dir,"config_USER.json");
     if(!IsOrdinaryThemeInput(manifest,dir,parent)||!IsOrdinaryThemeInput(config,dir,parent))return null;
     if((result[2*i]=ReadThemeRevisionDigest(config,sha))==null||(result[2*i+1]=ReadThemeRevisionDigest(manifest,sha))==null)return null;
    }
   }
   return result;
  }
  public sealed class ThemeInputSnapshot {
   public readonly string raw,hash;
   internal ThemeInputSnapshot(string text,string digest){raw=text;hash=digest;}
  }
  private static ThemeInputSnapshot ReadThemeInput(string path,HashAlgorithm sha) {
   using(var stream=File.OpenRead(path)) {
    long size=stream.Length;
    if(size>262144)return null;
    var bytes=new byte[(int)size];
    for(int offset=0;offset<bytes.Length;) {
     int read=stream.Read(bytes,offset,bytes.Length-offset);
     if(read==0)return null;
     offset+=read;
    }
    if(stream.ReadByte()!=-1)return null;
    string digest=BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-",String.Empty);
    using(var buffer=new MemoryStream(bytes,false))
    using(var reader=new StreamReader(buffer,Encoding.UTF8,true))
     return new ThemeInputSnapshot(reader.ReadToEnd(),digest);
   }
  }
  // Callers still validate schema and selections, and independently re-read
  // revisions before accepting results. No file observation is kept here.
  public static ThemeInputSnapshot[] ReadThemeInputSnapshots(string themeRoot,string[] names) {
   if(names==null||names.Length==0||names.Length>32)return null;
   string root=Path.GetFullPath(themeRoot).TrimEnd('\\');
   string prefix=root+"\\";
   var invalid=Path.GetInvalidFileNameChars();
   var result=new ThemeInputSnapshot[2*names.Length];
   using(var sha=SHA256.Create()) {
    for(int i=0;i<names.Length;i++) {
     string name=names[i];
     if(String.IsNullOrEmpty(name)||name.IndexOfAny(invalid)>=0||name.EndsWith(".",StringComparison.Ordinal)||name.EndsWith(" ",StringComparison.Ordinal))return null;
     string dir=Path.GetFullPath(Path.Combine(prefix,name));
     string parent=Path.GetDirectoryName(dir);
     if(parent==null||!String.Equals(parent.TrimEnd('\\'),root,StringComparison.OrdinalIgnoreCase))return null;
     string manifest=Path.Combine(dir,"theme.json"),config=Path.Combine(dir,"config_USER.json");
     if(!IsOrdinaryThemeInput(manifest,dir,parent)||!IsOrdinaryThemeInput(config,dir,parent))return null;
     if((result[2*i]=ReadThemeInput(config,sha))==null||(result[2*i+1]=ReadThemeInput(manifest,sha))==null)return null;
    }
   }
   return result;
  }
  public static bool MatchInstalledPreset(string themeRoot,string[] paths,string[] hashes) {
   return InspectInstalledPreset(themeRoot,paths,hashes,true)==0;
  }
  public static int CountInstalledPresetDrift(string themeRoot,string[] paths,string[] hashes) {
   return InspectInstalledPreset(themeRoot,paths,hashes,false);
  }
  // Count only stable, ordinary files. An incomplete batch has no result;
  // the caller keeps the original per-file diagnostics in that case.
  static int InspectInstalledPreset(string themeRoot,string[] paths,string[] hashes,bool stopOnMismatch) {
   if(paths==null||hashes==null||paths.Length==0||paths.Length>256||paths.Length!=hashes.Length)return -1;
   string root=Path.GetFullPath(themeRoot).TrimEnd('\\');
   string prefix=root+"\\";
   int drift=0;
   using(var sha=SHA256.Create()) {
    for(int i=0;i<paths.Length;i++) {
     string relative=paths[i].Replace('/','\\');
     if(String.Equals(Path.GetFileName(relative),"config_USER.json",StringComparison.OrdinalIgnoreCase))continue;
     if(Path.IsPathRooted(relative)||relative.IndexOf(':')>=0||relative.IndexOf('\0')>=0)return -1;
     foreach(string part in relative.Split('\\'))if(part.Length==0||part=="."||part=="..")return -1;
     string path=Path.GetFullPath(Path.Combine(prefix,relative));
     if(!path.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))return -1;
     for(string parent=Path.GetDirectoryName(path);parent!=null&&parent.Length>=root.Length;parent=Path.GetDirectoryName(parent)) {
      if((File.GetAttributes(parent)&FileAttributes.ReparsePoint)!=0)return -1;
     }
     const FileAttributes denied=FileAttributes.Directory|FileAttributes.ReparsePoint;
     if((File.GetAttributes(path)&denied)!=0)return -1;
     var before=new FileInfo(path);
     var attributes=before.Attributes;
     if(attributes==(FileAttributes)(-1)||(attributes&denied)!=0)return -1;
     long length=before.Length;
     const int maximumBytes=16777216;
     if(length>maximumBytes)return -1;
     var written=before.LastWriteTimeUtc;
     var created=before.CreationTimeUtc;
     bool matches;
     using(var stream=File.OpenRead(path)) {
      if(stream.Length>maximumBytes)return -1;
      string hash=BitConverter.ToString(sha.ComputeHash(stream)).Replace("-",String.Empty);
      matches=String.Equals(hash,hashes[i],StringComparison.OrdinalIgnoreCase);
      if(stopOnMismatch&&!matches)return 1;
     }
     var after=new FileInfo(path);
     after.Refresh();
     if(!after.Exists||after.Attributes!=attributes||after.Length!=length||after.LastWriteTimeUtc!=written||after.CreationTimeUtc!=created)return -1;
     if(!matches)drift++;
    }
   }
   return drift;
  }
  public static bool MatchBundledPreset(string root,string[] paths,string[] hashes,bool[] schema,string[] text) {
   if(paths.Length>256||paths.Length!=hashes.Length||paths.Length!=schema.Length||
      (text!=null&&text.Length!=paths.Length))return false;
   using(var sha=SHA256.Create()) {
    for(int i=0;i<paths.Length;i++) {
     // The caller accepts only checked, normalized manifest paths.
     string path=Path.Combine(root,paths[i]);
     var before=new FileInfo(path);
     if(!before.Exists||(before.Attributes&FileAttributes.ReparsePoint)!=0)return false;
     var attributes=before.Attributes;
     long length=before.Length;
     int limit=schema[i]?262144:16777216;
     if(length>limit)return false;
     var written=before.LastWriteTimeUtc;
     var created=before.CreationTimeUtc;
     byte[] digest;
     using(var stream=File.OpenRead(path)) {
      long size=stream.Length;
      if(size>limit)return false;
      if(schema[i]&&text!=null) {
       using(var buffer=new MemoryStream((int)size)) {
        stream.CopyTo(buffer,4096);
        if(buffer.Length>limit)return false;
        digest=sha.ComputeHash(buffer.GetBuffer(),0,(int)buffer.Length);
        buffer.Position=0;
        using(var reader=new StreamReader(buffer,Encoding.UTF8,true))text[i]=reader.ReadToEnd();
       }
      }else digest=sha.ComputeHash(stream);
     }
     if(!String.Equals(BitConverter.ToString(digest).Replace("-",String.Empty),hashes[i],StringComparison.OrdinalIgnoreCase))return false;
     var after=new FileInfo(path);
     after.Refresh();
     if(!after.Exists||after.Attributes!=attributes||after.Length!=length||after.LastWriteTimeUtc!=written||after.CreationTimeUtc!=created)return false;
    }
   }
   return true;
  }
  // This is a positive observation of one backup journal, not a cached proof.
  public static bool MatchPresetBackup(string stateRoot,string themeRoot,string blobRoot,string[] targets,string[] blobs,string[] hashes) {
   if(targets.Length>2048||targets.Length!=blobs.Length||targets.Length!=hashes.Length)return false;
   long total=0;
   using(var sha=SHA256.Create()) {
    for(int i=0;i<targets.Length;i++) {
     if(!CheckBackupPath(themeRoot,Path.Combine(themeRoot,targets[i]),false))return false;
     if(blobs[i]==null)continue;
     string blob=Path.Combine(blobRoot,blobs[i]);
     if(!CheckBackupPath(stateRoot,blob,true))return false;
     using(var stream=File.OpenRead(blob)) {
      long length=stream.Length;
      if(length>16777216)return false;
      string hash=BitConverter.ToString(sha.ComputeHash(stream)).Replace("-",String.Empty);
      total+=length;
      if(total>134217728||!String.Equals(hash,hashes[i],StringComparison.OrdinalIgnoreCase))return false;
     }
    }
   }
   return true;
  }
  static bool CheckBackupPath(string root,string path,bool requireFile) {
   string full=Path.GetFullPath(path);
   if(!full.StartsWith(root+"\\",StringComparison.OrdinalIgnoreCase))return false;
   for(string current=full;current!=null&&current.Length>=root.Length;current=Path.GetDirectoryName(current)) {
    FileAttributes attributes;
    try{attributes=File.GetAttributes(current);}
    catch(FileNotFoundException){if(requireFile&&current==full)return false;continue;}
    catch(DirectoryNotFoundException){if(requireFile&&current==full)return false;continue;}
    if((attributes&FileAttributes.ReparsePoint)!=0)return false;
    if(requireFile&&current==full&&(attributes&FileAttributes.Directory)!=0)return false;
   }
   return true;
  }
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
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool ProcessIdToSessionId(uint pid,out uint session);
  static IntPtr OpenObservedProcess(uint pid) {
   if(pid==0)throw new ArgumentOutOfRangeException("pid");
   var handle=OpenProcess(0x101000,false,pid); // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
   if(handle!=IntPtr.Zero)return handle;
   int error=Marshal.GetLastWin32Error();
   if(error==87)return IntPtr.Zero;
   throw new Win32Exception(error);
  }
  static long ProcessBirth(IntPtr handle) {
   long birth,exit,kernel,user;
   if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user))throw new Win32Exception(Marshal.GetLastWin32Error());
   return birth;
  }
  static bool ProcessAlive(IntPtr handle) {
   uint state=WaitForSingleObject(handle,0);
   if(state==0||state==258)return state==258;
   throw new InvalidOperationException("Appearance process liveness is unknown");
  }
  public sealed class Candidate {
   readonly long birth;
   public uint Id {get;private set;}
   public uint SessionId {get;private set;}
   public DateTime StartTime {get {return DateTime.FromFileTimeUtc(birth);}}
   public string Path {get {return ReadPath(Id,birth);}}
   public bool HasExited {get {
    // This getter is used only to reject confirmation. Unknown also rejects.
    IntPtr handle=IntPtr.Zero;
    try {
     handle=OpenObservedProcess(Id);
     return handle==IntPtr.Zero||ProcessBirth(handle)!=birth||!ProcessAlive(handle);
    }catch(Win32Exception){return true;}catch(InvalidOperationException){return true;}
    finally{if(handle!=IntPtr.Zero)CloseHandle(handle);}
   }}
   internal Candidate(uint pid,uint session,long created){Id=pid;SessionId=session;birth=created;}
  }
  public static Candidate ObserveProcess(uint pid,long expectedBirth) {
   var handle=OpenObservedProcess(pid);
   if(handle==IntPtr.Zero)return null;
   try {
    long birth=ProcessBirth(handle);
    if(expectedBirth!=0&&birth!=expectedBirth)throw new InvalidOperationException("YMCC candidate birth changed; state unknown");
    if(!ProcessAlive(handle))return null;
    uint session;
    if(!ProcessIdToSessionId(pid,out session)){
     int error=Marshal.GetLastWin32Error();
     if(!ProcessAlive(handle))return null;
     throw new Win32Exception(error);
    }
    return ProcessAlive(handle)?new Candidate(pid,session,birth):null;
   }finally{CloseHandle(handle);}
  }
  static string ImagePath(IntPtr handle) {
   uint size=512;var path=new StringBuilder((int)size);
   if(QueryFullProcessImageName(handle,0,path,ref size))return path.ToString();
   int error=Marshal.GetLastWin32Error();
   if(error!=122)throw new Win32Exception(error);
   // Long paths retry once on the same held process, with the original bound.
   size=32768;path=new StringBuilder((int)size);
   if(!QueryFullProcessImageName(handle,0,path,ref size))throw new Win32Exception(Marshal.GetLastWin32Error());
   return path.ToString();
  }
  public static string ReadPath(uint pid,long expectedBirth) {
   var handle=OpenProcess(0x1000,false,pid);
   if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Appearance process identity is unreadable");
   try {
    long birth,exit,kernel,user;
    if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user))throw new Win32Exception(Marshal.GetLastWin32Error());
    if(birth!=expectedBirth)throw new InvalidOperationException("Appearance process birth changed");
    return ImagePath(handle);
   }finally{CloseHandle(handle);}
  }
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint rights,out IntPtr token);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
  [StructLayout(LayoutKind.Sequential)] struct Basic { public IntPtr reserved,peb,reserved2,reserved3,pid,parent; }
  [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int kind,ref Basic basic,int size,out int returned);
  [DllImport("iphlpapi.dll")] static extern uint GetExtendedTcpTable(IntPtr table,ref int size,bool sort,int family,int kind,uint reserved);
  public static string ReadBoundOwnerPath(Candidate expected,uint session,string sid) {
   if(expected==null||expected.SessionId!=session)return null;
   var handle=OpenObservedProcess(expected.Id);IntPtr token=IntPtr.Zero;
   if(handle==IntPtr.Zero)return null;
   try {
    if(ProcessBirth(handle)!=expected.StartTime.ToFileTimeUtc()||!ProcessAlive(handle))return null;
    uint currentSession;
    if(!ProcessIdToSessionId(expected.Id,out currentSession)){
     int error=Marshal.GetLastWin32Error();
     if(!ProcessAlive(handle))return null;
     throw new Win32Exception(error);
    }
    if(currentSession!=session)return null;
    if(!OpenProcessToken(handle,8,out token))throw new Win32Exception(Marshal.GetLastWin32Error());
    using(var identity=new System.Security.Principal.WindowsIdentity(token)){
     if(!String.Equals(identity.User.Value,sid,StringComparison.Ordinal))return null;
    }
    var path=ImagePath(handle);
    return ProcessAlive(handle)?path:null;
   }finally{if(token!=IntPtr.Zero)CloseHandle(token);CloseHandle(handle);}
  }
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
  static TcpListener[] ReadTcpListeners(int family,int port,ref IntPtr table,ref int capacity) {
   // Four attempts include the old size-probe allowance. Only the buffer, never the rows, is reused.
   for(int attempt=0;attempt<4;attempt++) {
    int size=capacity;
    uint status=GetExtendedTcpTable(table,ref size,false,family,3,0);
    if(status==122) {
     if(size<=capacity||size>8388608)throw new InvalidOperationException("Appearance listener table size is invalid");
     if(attempt==3)break;
     table=Marshal.ReAllocHGlobal(table,new IntPtr(size));capacity=size;
     continue;
    }
    if(status!=0)throw new Win32Exception((int)status,"Appearance listener query failed");
    if(size>capacity)throw new InvalidOperationException("Appearance listener table grew unexpectedly");
    return DecodeTcpListeners(table,size,family,port);
   }
   throw new InvalidOperationException("Appearance listener table changed repeatedly");
  }
  public static TcpListener[] Listeners(int port) {
   return ReadListeners(port,false);
  }
  public static TcpListener[] ReadinessListeners(int port) {
   return ReadListeners(port,true);
  }
  static TcpListener[] ReadListeners(int port,bool requireLoopback) {
   if(port<1||port>65535)throw new ArgumentOutOfRangeException("port");
   int capacity=4096;var table=Marshal.AllocHGlobal(capacity);
   try {
    var ipv4=ReadTcpListeners(2,port,ref table,ref capacity);
    if(requireLoopback) {
     // Absence can only defer readiness, not authorize a start or a write.
     bool ready=false;
     foreach(var row in ipv4)if(row.LocalAddress=="127.0.0.1"){ready=true;break;}
     if(!ready)return new TcpListener[0];
    }
    var rows=new System.Collections.Generic.List<TcpListener>(ipv4);
    rows.AddRange(ReadTcpListeners(23,port,ref table,ref capacity));return rows.ToArray();
   }finally{Marshal.FreeHGlobal(table);}
  }
  public static string[] Identity(uint pid,long expectedBirth) {
   var handle=OpenProcess(0x101000,false,pid);IntPtr token=IntPtr.Zero;
   if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
   try {
    long birth,exit,kernel,user;
    if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user)||birth!=expectedBirth||WaitForSingleObject(handle,0)!=258)throw new InvalidOperationException("Appearance process identity changed");
    var path=ImagePath(handle);
    if(!OpenProcessToken(handle,8,out token))throw new Win32Exception(Marshal.GetLastWin32Error());
    var basic=new Basic();int returned;
    if(NtQueryInformationProcess(handle,0,ref basic,Marshal.SizeOf(basic),out returned)!=0)throw new InvalidOperationException("Appearance process parent is unknown");
    string sid;using(var identity=new System.Security.Principal.WindowsIdentity(token)){sid=identity.User.Value;}
    if(WaitForSingleObject(handle,0)!=258)throw new InvalidOperationException("Appearance process exited");
    return new string[]{path,sid,basic.parent.ToInt64().ToString(),birth.ToString()};
   }finally{if(token!=IntPtr.Zero)CloseHandle(token);CloseHandle(handle);}
  }
  static uint[] ReadReply(System.IO.MemoryMappedFiles.MemoryMappedViewAccessor view,uint serial,uint owner,long ownerBirth,bool inventory,out string response) {
   const int headerWords=32,rowWords=5,capacity=128,outputOffset=68224;
   response=null;
   System.Threading.Thread.MemoryBarrier();
   var result=new uint[headerWords];
   if(view.ReadArray<uint>(0,result,0,headerWords)!=headerWords||result[3]!=serial||result[4]!=1||result[5]>capacity||result[12]>65536||result[14]!=owner||(((long)result[16]<<32)|result[15])!=ownerBirth)throw new InvalidOperationException("YMCC shared process state is unknown ("+result[9]+")");
   // Document and identity clients only consume the JSON payload. Inventory
   // clients need the declared rows, not the unused capacity of the mapping.
   if(inventory&&result[5]!=0) {
    int count=(int)result[5]*rowWords;
    Array.Resize(ref result,headerWords+count);
    if(view.ReadArray<uint>(headerWords*sizeof(uint),result,headerWords,count)!=count)throw new InvalidOperationException("YMCC shared process rows incomplete");
   }
   if(result[12]!=0){var output=new byte[result[12]];view.ReadArray<byte>(outputOffset,output,0,output.Length);response=new System.Text.UTF8Encoding(false,true).GetString(output);}
   return result;
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
       return ReadReply(view,serial,owner,ownerBirth,request==null,out response);
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
    $ids=@{};$roots=@{};$parents=@{}
    $expectedPath=[IO.Path]::GetFullPath($script:BackendExe)
    $directedType='Ymcc.CssResidentProcess+Candidate' -as [type]
    foreach($process in $list){
        $pidKey=[uint32]$process.Id
        if($directedType -and $directedType.IsInstanceOfType($process)){
            if($process.SessionId -ne $script:CurrentSessionId){throw 'CSSLoader owner changed'}
            # Identity reads image, SID and parent on one birth-bound live handle.
            $identity=[Ymcc.CssResidentProcess]::Identity($pidKey,$process.StartTime.ToFileTimeUtc())
            if($identity[1] -ne $script:CurrentUserSid){throw 'CSSLoader owner changed'}
            $image=$identity[0];$parents[$pidKey]=[uint32]$identity[2]
        }else{
            if(!(Test-CurrentUserProcess $process)){throw 'CSSLoader owner changed'}
            $image=Get-AppearanceProcessPath $process
            if($process.HasExited){throw 'CSSLoader process exited during verification'}
        }
        if([IO.Path]::GetFullPath($image) -ine $expectedPath){throw 'CSSLoader path changed'}
        $ids[$pidKey]=$process
    }
    if($ids.Count -ne $list.Count){throw 'CSSLoader process identity changed'}
    # One bounded snapshot at the use boundary. PyInstaller's parent and child
    # are one instance; two independent roots are ambiguous and never replaced.
    foreach($process in $list){
        $pidKey=[uint32]$process.Id
        if(!$parents.ContainsKey($pidKey)){$parents[$pidKey]=Get-AppearanceParentId $process}
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
function Get-AppearancePid([uint32]$Id,[long]$ExpectedBirth=0) {
    # Only a PID supplied by the shared inventory, a listener or this launch.
    Initialize-ResidentAppearanceInterop
    [Ymcc.CssResidentProcess]::ObserveProcess($Id,$ExpectedBirth)
}
function Get-AppearanceProcessCandidates([int]$Kind,[switch]$Refresh) {
    if($Refresh -or !$script:AppearanceProcessReceipt){$null=Update-AppearanceProcessInventory}
    foreach($row in $script:AppearanceProcessReceipt.rows){
        if($row.kind -ne $Kind){continue}
        $birth=[long]$row.birth
        if($birth -le 0){throw 'YMCC candidate birth is invalid; state unknown'}
        $process=Get-AppearancePid -Id $row.pid -ExpectedBirth $birth
        if($process){$process}
    }
}
function Get-AppearanceBackendCandidates([switch]$RequireListener) {
    if($RequireListener){
        $listeners=@(Get-AppearanceTcpListeners -LocalPort 35821 -ReadinessOnly)
        # This snapshot only filters readiness work. Ownership is checked afresh.
        $hasLoopback=$false
        foreach($listener in $listeners){if($listener.LocalAddress -eq '127.0.0.1'){$hasLoopback=$true;break}}
        if(!$hasLoopback){return}
    }
    $known=@{}
    foreach($process in @(Get-AppearanceProcessCandidates 1)){$known[[uint32]$process.Id]=$process}
    # A known CreateProcess result and the fixed port owner are directed PID
    # observations, including the PyInstaller child which appears after startup.
    if(!$RequireListener){$listeners=@(Get-AppearanceTcpListeners -LocalPort 35821)}
    $pids=@(if($script:AppearanceStartedPid){[uint32]$script:AppearanceStartedPid};foreach($listener in $listeners){[uint32]$listener.OwningProcess})
    $observed=@{}
    foreach($processId in $pids){
        if($known.ContainsKey($processId) -or $observed.ContainsKey($processId)){continue}
        $observed[$processId]=$true
        $process=Get-AppearancePid -Id $processId
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
