<# A job contains only the gated child created by this helper and its descendants.
   If the runner times out or crashes, Windows closes this job and reclaims its tests.
   The child cannot execute a test before successful job assignment. No name-based cleanup. #>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true,ParameterSetName='Script')][string]$ScriptPath,
 [Parameter(Mandatory=$true,ParameterSetName='RawProgram')]
 [Parameter(Mandatory=$true,ParameterSetName='Program')][string]$ProgramPath,
 [Parameter(Mandatory=$true)][string]$ArgumentsPath,
 [Parameter(Mandatory=$true,ParameterSetName='RawProgram')][switch]$RawProgram,
 [Parameter(Mandatory=$true,ParameterSetName='RawProgram')][string]$RawDirectory,
 [Parameter(Mandatory=$true,ParameterSetName='RawProgram')][string]$RawRequestPath,
 [Parameter(Mandatory=$true,ParameterSetName='RawProgram')][string]$RawRequestSha256,
 [switch]$Child,
 [string]$ReadyEvent='',
 [int]$OwnerProcessId=0,
 [long]$OwnerStartTicks=0,
 [long]$DeadlineEpochMs=0,
 [int]$CleanupReserveMs=0,
 [string]$DeadlineReceiptPath=''
)
$ErrorActionPreference='Stop'
# Optional raw producer protocol. The original Script/Program branches below are unchanged.
if($RawProgram){
 $rawLimit=16777216L;$rawStdout=$null;$rawStderr=$null;$rawBegin=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();$rawJob=$null;$rawOwner=$null;$rawImageLock=$null;$rawLease=$null;$rawTerminal=$null
 $rawInputLocks=@();$cancelPin=$null;$rawReason='PREFLIGHT_FAILURE';$rawError=$null;$rawExit=10;$rawRootExit=$null;$rawZero=$false;$rawPinsMatch=$false;$rawLogsClosed=$false;$rawLogPins=$null;$rawOutputBytes=0L;$rawErrorBytes=0L;$rawQueryError=$null;$rawToolPolicy=$null;$rawToolCleanupEligible=$false;$rawToolCleanupVerified=@();$rawMembersBeforeTermination=@();$rawMemberObservationError=$null;$rawProof=$null;$rawRequest=$null;$rawPins=@();$rawPostPins=@();$rawRequestPin=$null;$rawArgsPin=$null;$rawStarted=$null;$rawFinalQuery=$null
 function Raw-Now { return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
 function Raw-Need($condition,[string]$code){if(-not $condition){throw ('YMCC_RAW_'+$code)}}
 function Raw-Plain([string]$file){
  Raw-Need ([IO.Path]::IsPathFullyQualified($file)) 'ABSOLUTE_PATH_REQUIRED'
  $full=[IO.Path]::GetFullPath($file);$item=Get-Item -LiteralPath $full -Force
  while($item){Raw-Need (($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-eq0) ('REPARSE:'+ $full);$item=if($item-is[IO.DirectoryInfo]){$item.Parent}else{$item.Directory}}
  return $full
 }
 function Raw-Hash([string]$file){
  $full=Raw-Plain $file;$stream=[IO.FileStream]::new($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete)
  $hash=[Security.Cryptography.SHA256]::Create()
  try{$buffer=[byte[]]::new(65536);$length=0L;while(($n=$stream.Read($buffer,0,$buffer.Length))-gt0){Raw-Need ((Raw-Now)-lt($DeadlineEpochMs-100)) 'DEADLINE_DURING_HASH';[void]$hash.TransformBlock($buffer,0,$n,$buffer,0);$length+=$n};[void]$hash.TransformFinalBlock([byte[]]@(),0,0);return [ordered]@{path=$full;sha256=([BitConverter]::ToString($hash.Hash)).Replace('-','').ToLowerInvariant();bytes=$length}}
  finally{$hash.Dispose();$stream.Dispose()}
 }
 function Raw-NewFile([string]$file){return [IO.FileStream]::new($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read)}
 function Raw-Write($stream,$value){$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value|ConvertTo-Json -Depth 24 -Compress)+"`n");$stream.SetLength(0);$stream.Position=0;$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}
 function Raw-Publish([string]$file,$value){$stream=Raw-NewFile $file;try{Raw-Write $stream $value}finally{$stream.Dispose()}}
 try{
  Raw-Need ($PSVersionTable.PSVersion.Major-ge7) 'POWERSHELL_7_REQUIRED'
  Raw-Need (-not $Child-and-not $ReadyEvent-and-not $DeadlineReceiptPath) 'RAW_LEGACY_CONTROL_CONFLICT'
  Raw-Need ($OwnerProcessId-gt0-and$OwnerProcessId-ne$PID-and$OwnerStartTicks-gt0) 'EXTERNAL_OWNER_REQUIRED'
  Raw-Need ($DeadlineEpochMs-gt$rawBegin-and$CleanupReserveMs-ge500-and$CleanupReserveMs-lt($DeadlineEpochMs-$rawBegin)) 'BOUNDED_DEADLINE_REQUIRED'
  $rawRunCutoff=$DeadlineEpochMs-$CleanupReserveMs;$rawCleanupCutoff=$DeadlineEpochMs-[Math]::Max(100,[Math]::Min(500,[int]($CleanupReserveMs/4)))
  $rawDirectory=Raw-Plain $RawDirectory;Raw-Need ([IO.Directory]::Exists($rawDirectory)) 'OWNED_DIRECTORY_REQUIRED'
  if(-not('YmccRawProgramJobV1'-as[type])){Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.IO;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
// Raw mode only. No process exists outside the private Job, even at pre-resume admission.
public sealed class YmccRawProgramJobV1 : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct IO { public ulong a,b,c,d,e,f; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC { public long a,b; public uint flags; public UIntPtr min,max; public uint active; public UIntPtr affinity; public uint priority,scheduling; }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED { public BASIC basic; public IO io; public UIntPtr a,b,c,d; }
 [StructLayout(LayoutKind.Sequential)] struct ACCOUNTING { public long a,b,c,d; public uint faults,total,active,terminated; }
 [StructLayout(LayoutKind.Sequential)] struct SECURITY { public int length; public IntPtr descriptor; [MarshalAs(UnmanagedType.Bool)] public bool inherit; }
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct STARTUP { public int size; public string reserved,desktop,title; public int x,y,xs,ys,xc,yc,fill,flags; public short show,reservedSize; public IntPtr reservedPtr,input,output,error; }
 [StructLayout(LayoutKind.Sequential)] struct STARTUP_EX { public STARTUP startup; public IntPtr attributes; }
 [StructLayout(LayoutKind.Sequential)] struct PROCESS { public IntPtr process,thread; public uint pid,tid; }
 [StructLayout(LayoutKind.Sequential)] struct TIMES { public uint low,high; public long Value { get { return ((long)high<<32)|low; } } }
 [StructLayout(LayoutKind.Sequential)] struct FILE_INFO { public uint attributes; public TIMES creation,access,write; public uint volume,sizeHigh,sizeLow,links,indexHigh,indexLow; }
 [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)] static extern IntPtr CreateJobObjectW(IntPtr a,string n);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr h,int c,ref EXTENDED p,uint n);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr h,int c,out ACCOUNTING p,uint n,IntPtr r);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool TerminateJobObject(IntPtr h,uint code);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool IsProcessInJob(IntPtr p,IntPtr j,out bool yes);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr p,int n,int f,ref IntPtr size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr p,uint f,IntPtr key,IntPtr v,IntPtr n,IntPtr old,IntPtr ret);
 [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)] static extern bool CreateProcessW(string app,StringBuilder cmd,IntPtr pa,IntPtr ta,bool inherit,uint flags,IntPtr env,string cwd,ref STARTUP_EX si,out PROCESS pi);
 [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)] static extern IntPtr CreateFileW(string p,uint access,uint share,ref SECURITY sa,uint mode,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(IntPtr h,out FILE_INFO info);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileSizeEx(IntPtr h,out long n);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool FlushFileBuffers(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint ResumeThread(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
 [DllImport("kernel32.dll",EntryPoint="QueryInformationJobObject",SetLastError=true)] static extern bool QueryJobProcessList(IntPtr job,int type,IntPtr data,uint bytes,IntPtr needed);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr h,out uint code);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out TIMES start,out TIMES end,out TIMES kernel,out TIMES user);
 [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)] static extern bool QueryFullProcessImageNameW(IntPtr h,uint flags,StringBuilder text,ref int length);
 IntPtr job,process,thread,stdout,stderr,input; readonly List<IntPtr> directories=new List<IntPtr>();
 public uint Pid { get; private set; }
 public string ActualImage { get; private set; }
 public string StartFileTime { get; private set; }
 public string ExitFileTime { get; private set; }
 public bool AtomicJobAdmission { get; private set; }
 public bool Resumed { get; private set; }
 public bool TerminationRequested { get; private set; }
 public bool TerminationSucceeded { get; private set; }
 public bool OutputsClosed { get; private set; }
 public bool OutputsFlushed { get; private set; }
 public bool JobDisposed { get; private set; }
 public uint QueryCount { get; private set; }
 static void Need(bool ok,string code) { if(!ok)throw new Win32Exception(Marshal.GetLastWin32Error(),"YMCC_RAW_"+code); }
 static bool Valid(IntPtr p) { return p!=IntPtr.Zero&&p!=new IntPtr(-1); }
 static void Close(ref IntPtr p) { if(Valid(p)) { var h=p; p=IntPtr.Zero; Need(CloseHandle(h),"CLOSE_HANDLE"); } }
 public YmccRawProgramJobV1() {
  job=CreateJobObjectW(IntPtr.Zero,null);Need(Valid(job),"CREATE_JOB");
  var limits=new EXTENDED();limits.basic.flags=0x2000;
  try {Need(SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(EXTENDED))),"KILL_ON_CLOSE");}catch{Close(ref job);throw;}
 }
 // Hold every ancestor without FILE_SHARE_DELETE. Do not resolve or follow reparse points.
 public void HoldDirectoryChain(string path) {
  var chain=new List<string>();for(var p=new DirectoryInfo(Path.GetFullPath(path));p!=null;p=p.Parent)chain.Add(p.FullName);chain.Reverse();
  foreach(var p in chain){var sa=new SECURITY{length=Marshal.SizeOf(typeof(SECURITY)),inherit=false};var h=CreateFileW(p,0x80,3,ref sa,3,0x02200000,IntPtr.Zero);Need(Valid(h),"DIRECTORY_HANDLE");directories.Add(h);FILE_INFO info;Need(GetFileInformationByHandle(h,out info),"DIRECTORY_INFO");if((info.attributes&0x400)!=0)throw new InvalidOperationException("YMCC_RAW_REPARSE_DIRECTORY");}
 }
 IntPtr NewOutput(string file) { var sa=new SECURITY{length=Marshal.SizeOf(typeof(SECURITY)),inherit=true};var h=CreateFileW(file,0xC0000000,1,ref sa,1,0x80000000,IntPtr.Zero);Need(Valid(h),"EXCLUSIVE_RAW_OUTPUT");return h; }
 public void OpenOutputs(string output,string error) {
  stdout=NewOutput(output);stderr=NewOutput(error);var sa=new SECURITY{length=Marshal.SizeOf(typeof(SECURITY)),inherit=true};input=CreateFileW("NUL",0x80000000,1,ref sa,3,0,IntPtr.Zero);Need(Valid(input),"NULL_STDIN");
 }
 public long OutputBytes { get { long n; if(!Valid(stdout))return 0;Need(GetFileSizeEx(stdout,out n),"STDOUT_SIZE");return n; } }
 public long ErrorBytes { get { long n;if(!Valid(stderr))return 0;Need(GetFileSizeEx(stderr,out n),"STDERR_SIZE");return n; } }
 static string Quote(string value) { var b=new StringBuilder("\"");int slashes=0;foreach(char c in value){if(c=='\\'){slashes++;continue;}if(c=='\"'){b.Append('\\',slashes*2+1);b.Append(c);}else{b.Append('\\',slashes);b.Append(c);}slashes=0;}b.Append('\\',slashes*2);return b.Append('"').ToString(); }
 public void StartSuspended(string image,string[] args,string cwd,string[] env) {
  if(Valid(process))throw new InvalidOperationException("YMCC_RAW_DUPLICATE_START");
  IntPtr attr=IntPtr.Zero,size=IntPtr.Zero,handles=IntPtr.Zero,jobs=IntPtr.Zero,block=IntPtr.Zero;bool initialized=false;
  try{
   InitializeProcThreadAttributeList(IntPtr.Zero,2,0,ref size);Need(size!=IntPtr.Zero,"ATTRIBUTE_SIZE");attr=Marshal.AllocHGlobal(size);Need(InitializeProcThreadAttributeList(attr,2,0,ref size),"ATTRIBUTE_INIT");initialized=true;
   handles=Marshal.AllocHGlobal(IntPtr.Size*3);Marshal.WriteIntPtr(handles,0,input);Marshal.WriteIntPtr(handles,IntPtr.Size,stdout);Marshal.WriteIntPtr(handles,IntPtr.Size*2,stderr);
   Need(UpdateProcThreadAttribute(attr,0,new IntPtr(0x20002),handles,new IntPtr(IntPtr.Size*3),IntPtr.Zero,IntPtr.Zero),"HANDLE_LIST");
   jobs=Marshal.AllocHGlobal(IntPtr.Size);Marshal.WriteIntPtr(jobs,job);
   // JOB_LIST assigns at creation, not in a vulnerable Start->Assign interval. No fallback.
   Need(UpdateProcThreadAttribute(attr,0,new IntPtr(0x2000D),jobs,new IntPtr(IntPtr.Size),IntPtr.Zero,IntPtr.Zero),"ATOMIC_JOB_LIST_UNSUPPORTED");
   var sorted=(string[])env.Clone();Array.Sort(sorted,StringComparer.OrdinalIgnoreCase);block=Marshal.StringToHGlobalUni(string.Join("\0",sorted)+"\0\0");
   var cmd=new StringBuilder(Quote(image));foreach(var arg in args)cmd.Append(' ').Append(Quote(arg));
   var si=new STARTUP_EX();si.startup.size=Marshal.SizeOf(typeof(STARTUP_EX));si.startup.flags=0x100;si.startup.input=input;si.startup.output=stdout;si.startup.error=stderr;si.attributes=attr;PROCESS pi;
   Need(CreateProcessW(image,cmd,IntPtr.Zero,IntPtr.Zero,true,0x08080404,block,cwd,ref si,out pi),"CREATE_SUSPENDED_IN_JOB");
   process=pi.process;thread=pi.thread;Pid=pi.pid;bool assigned;Need(IsProcessInJob(process,job,out assigned)&&assigned,"ATOMIC_JOB_MEMBERSHIP");AtomicJobAdmission=true;
   var name=new StringBuilder(32768);int length=name.Capacity;Need(QueryFullProcessImageNameW(process,0,name,ref length),"ACTUAL_IMAGE");ActualImage=name.ToString();TIMES start,end,kernel,user;Need(GetProcessTimes(process,out start,out end,out kernel,out user),"ACTUAL_BIRTH");StartFileTime=start.Value.ToString();
  }finally{if(initialized)DeleteProcThreadAttributeList(attr);if(attr!=IntPtr.Zero)Marshal.FreeHGlobal(attr);if(handles!=IntPtr.Zero)Marshal.FreeHGlobal(handles);if(jobs!=IntPtr.Zero)Marshal.FreeHGlobal(jobs);if(block!=IntPtr.Zero)Marshal.FreeHGlobal(block);}
 }
 public void Resume() { if(!AtomicJobAdmission||Resumed)throw new InvalidOperationException("YMCC_RAW_ADMISSION_REQUIRED");Need(ResumeThread(thread)!=uint.MaxValue,"RESUME");Resumed=true;Close(ref thread); }
 public uint ActiveProcesses { get { ACCOUNTING a;Need(QueryInformationJobObject(job,1,out a,(uint)Marshal.SizeOf(typeof(ACCOUNTING)),IntPtr.Zero),"QUERY_JOB");QueryCount++;return a.active; } }
 public bool RootExited { get { if(!Valid(process))return true;var value=WaitForSingleObject(process,0);Need(value!=0xFFFFFFFF,"WAIT_ROOT");return value==0; } }
 public int RootExitCode { get { uint value=0;Need(Valid(process)&&RootExited&&GetExitCodeProcess(process,out value),"EXIT_CODE");return unchecked((int)value); } }
 public void ReadExitTime() { if(!Valid(process)||!RootExited)return;TIMES start,end,kernel,user;Need(GetProcessTimes(process,out start,out end,out kernel,out user),"ACTUAL_EXIT_TIME");ExitFileTime=end.Value.ToString(); }
 public string SnapshotMembers() {
  var rows=new List<object>();const int bytes=65536;var data=Marshal.AllocHGlobal(bytes);
  try{Need(QueryJobProcessList(job,3,data,bytes,IntPtr.Zero),"QUERY_OWNED_MEMBER_LIST");uint count=(uint)Marshal.ReadInt32(data,4);if(count>(bytes-8)/IntPtr.Size)throw new InvalidOperationException("YMCC_RAW_MEMBER_LIMIT");
   for(uint i=0;i<count;i++){uint pid=unchecked((uint)Marshal.ReadIntPtr(data,8+(int)i*IntPtr.Size).ToInt64());var handle=OpenProcess(0x1000,false,pid);if(!Valid(handle)){rows.Add(new {pid,observed=false,reason="PROCESS_QUERY_UNAVAILABLE"});continue;}
    try{bool owned;Need(IsProcessInJob(handle,job,out owned),"QUERY_MEMBER_JOB");if(!owned){rows.Add(new {pid,observed=false,reason="MEMBER_CHANGED_BEFORE_QUERY"});continue;}var image=new StringBuilder(32768);int length=image.Capacity;TIMES start,end,kernel,user;bool imageKnown=QueryFullProcessImageNameW(handle,0,image,ref length),birthKnown=GetProcessTimes(handle,out start,out end,out kernel,out user);rows.Add(new {pid,observed=true,ownedAtQuery=true,image=imageKnown?image.ToString():null,startFileTimeUtc=birthKnown?start.Value.ToString():null});}
    finally{CloseHandle(handle);}
   }
   return System.Text.Json.JsonSerializer.Serialize(rows);
  }finally{Marshal.FreeHGlobal(data);}
 }
 public void Terminate(uint code) { TerminationRequested=true;Need(TerminateJobObject(job,code),"TERMINATE_OWNED_JOB");TerminationSucceeded=true; }
 public void CloseOutputs() {
  bool a=!Valid(stdout)||FlushFileBuffers(stdout),b=!Valid(stderr)||FlushFileBuffers(stderr);OutputsFlushed=a&&b;
  Close(ref stdout);Close(ref stderr);Close(ref input);OutputsClosed=true;if(!OutputsFlushed)throw new IOException("YMCC_RAW_LOG_FLUSH_FAILED");
 }
 public void CloseJob() { Close(ref job);JobDisposed=true; }
 public void Dispose() {
  // KILL_ON_CLOSE remains fail-closed on abrupt helper loss. A close is not proof of zero.
  try{CloseJob();}finally{try{Close(ref thread);}finally{try{Close(ref process);}finally{try{if(!OutputsClosed)CloseOutputs();}finally{foreach(var h in directories)if(Valid(h))CloseHandle(h);directories.Clear();}}}}
 }
}
"@}
  $rawJob=[YmccRawProgramJobV1]::new();$rawJob.HoldDirectoryChain($rawDirectory)
  $rawArguments=Raw-Plain $ArgumentsPath;$rawRequestFile=Raw-Plain $RawRequestPath
  Raw-Need ([string]::Equals($rawArguments,[IO.Path]::Combine($rawDirectory,'arguments.json'),[StringComparison]::OrdinalIgnoreCase)-and[string]::Equals($rawRequestFile,[IO.Path]::Combine($rawDirectory,'request.json'),[StringComparison]::OrdinalIgnoreCase)) 'OWNED_INPUT_PATHS'
  Raw-Need ($RawRequestSha256-cmatch'^[a-f0-9]{64}$') 'REQUEST_SHA_REQUIRED'
  $rawInputLocks+=,[IO.FileStream]::new($rawRequestFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);$rawInputLocks+=,[IO.FileStream]::new($rawArguments,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $rawRequestPin=Raw-Hash $rawRequestFile;Raw-Need ($rawRequestPin.sha256-ceq$RawRequestSha256) 'REQUEST_CHANGED'
  Raw-Need ($rawRequestPin.bytes-le4194304) 'REQUEST_SIZE'
  $rawRequest=[IO.File]::ReadAllText($rawRequestFile)|ConvertFrom-Json
  Raw-Need ($rawRequest.schemaVersion-eq1-and$rawRequest.kind-ceq'ymcc-raw-program-v1'-and$rawRequest.cancelNonce-cmatch'^[a-f0-9]{64}$') 'REQUEST_SCHEMA'
  Raw-Need (@(Get-ChildItem -LiteralPath $rawDirectory -Force|Where-Object{$_.Name-cnotin@('request.json','arguments.json')}).Count-eq0) 'OWNED_PATH_CONFLICT'
  $rawLease=Raw-NewFile ([IO.Path]::Combine($rawDirectory,'launcher.lease.json'))
  $rawTerminal=Raw-NewFile ([IO.Path]::Combine($rawDirectory,'terminal.json'))
  $self=[Diagnostics.Process]::GetCurrentProcess()
  try{$rawLauncher=[ordered]@{pid=$PID;program=Raw-Hash $self.MainModule.FileName;startTicks=$self.StartTime.ToUniversalTime().Ticks.ToString();helper=Raw-Hash $PSCommandPath;arguments=@([Environment]::GetCommandLineArgs());startedEpochMs=$rawBegin}}finally{$self.Dispose()}
  Raw-Write $rawLease ([ordered]@{schemaVersion=1;launcher=$rawLauncher;request=$rawRequestPin;scope='ONE_EXCLUSIVE_RAW_PRODUCER_NOT_GLOBAL_ACCEPTANCE'})
  $rawOwner=[Diagnostics.Process]::GetProcessById($OwnerProcessId);[void]$rawOwner.Handle
  Raw-Need (-not $rawOwner.HasExited-and$rawOwner.StartTime.ToUniversalTime().Ticks-eq$OwnerStartTicks) 'OWNER_PID_BIRTH_MISMATCH'
  $rawProgramFile=Raw-Plain $ProgramPath;Raw-Need ([IO.File]::Exists($rawProgramFile)) 'PROGRAM_REQUIRED'
  $rawCwd=Raw-Plain ([string]$rawRequest.cwd);Raw-Need ([IO.Directory]::Exists($rawCwd)) 'WORKING_DIRECTORY';$rawJob.HoldDirectoryChain($rawCwd)
  Raw-Need ($rawRequest.arguments.sha256-cmatch'^[a-f0-9]{64}$') 'ARGUMENT_PIN_REQUIRED'
  $rawArgsPin=Raw-Hash $rawArguments;Raw-Need ($rawArgsPin.sha256-ceq$rawRequest.arguments.sha256-and$rawArgsPin.bytes-le1048576) 'ARGUMENT_PIN'
  $rawArgv=ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($rawArguments)) -NoEnumerate
  Raw-Need ($rawArgv-is[array]-and$rawArgv.Count-le4096) 'ARGUMENT_ARRAY'
  foreach($v in $rawArgv){Raw-Need ($v-is[string]-and-not$v.Contains([char]0)) 'ARGUMENT_VALUE'}
  $rawArgv=[string[]]$rawArgv
  $rawEnvironment=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
  Raw-Need ($null-ne$rawRequest.environment-and$rawRequest.environment-is[pscustomobject]) 'EXPLICIT_ENVIRONMENT_REQUIRED'
  foreach($p in $rawRequest.environment.PSObject.Properties){Raw-Need ($p.Value-is[string]-and$p.Name.Length-gt0-and-not$p.Name.Contains('=')-and-not$p.Name.Contains([char]0)-and-not$p.Value.Contains([char]0)-and-not$rawEnvironment.ContainsKey($p.Name)) 'ENVIRONMENT_VALUE';$rawEnvironment.Add($p.Name,$p.Value)}
  $rawEnvBlock=[string[]]@($rawEnvironment.GetEnumerator()|ForEach-Object{$_.Key+'='+$_.Value})
  Raw-Need ($rawEnvBlock.Count-gt0-and$rawEnvBlock.Count-le4096) 'ENVIRONMENT_SIZE'
  foreach($role in @('sources','tools')){
   $members=@($rawRequest.$role);Raw-Need ($members.Count-gt0-and$members.Count-le3072) ('DECLARED_'+$role.ToUpperInvariant())
   $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
   foreach($entry in $members){Raw-Need ($entry.path-is[string]-and$entry.sha256-cmatch'^[a-f0-9]{64}$') 'DECLARED_PIN_SCHEMA';$file=Raw-Plain $entry.path;Raw-Need ($seen.Add($file)) 'DUPLICATE_DECLARED_PIN';$p=Raw-Hash $file;Raw-Need ($p.sha256-ceq$entry.sha256) ('DECLARED_PIN_CHANGED:'+ $file);$rawPins+=,[ordered]@{role=$role;path=$p.path;sha256=$p.sha256;bytes=$p.bytes}}
  }
  Raw-Need (@($rawPins|Where-Object{$_.role-eq'tools'-and[string]::Equals($_.path,$rawProgramFile,[StringComparison]::OrdinalIgnoreCase)}).Count-eq1) 'PROGRAM_TOOL_PIN_REQUIRED'
  Raw-Need (@($rawPins|Where-Object{$_.role-eq'tools'-and[string]::Equals($_.path,$rawLauncher.program.path,[StringComparison]::OrdinalIgnoreCase)}).Count-eq1) 'LAUNCHER_TOOL_PIN_REQUIRED'
  Raw-Need (@($rawPins|Where-Object{$_.role-eq'sources'-and[string]::Equals($_.path,$rawLauncher.helper.path,[StringComparison]::OrdinalIgnoreCase)}).Count-eq1) 'EXECUTING_HELPER_SOURCE_PIN_REQUIRED'
  if($null-ne$rawRequest.toolCleanupPolicy){
   $rawToolPolicy=$rawRequest.toolCleanupPolicy
   Raw-Need ($rawToolPolicy.kind-ceq'MSVC_VCTIP_AFTER_ROOT_EXIT'-and$rawToolPolicy.maximumProcesses-eq1) 'TOOL_CLEANUP_POLICY'
   $tool=Raw-Plain ([string]$rawToolPolicy.program.path)
   Raw-Need ([IO.Path]::GetFileName($tool)-ieq'vctip.exe'-and$rawToolPolicy.program.sha256-cmatch'^[a-f0-9]{64}$') 'TOOL_CLEANUP_PROGRAM'
   $declared=@($rawPins|Where-Object{$_.role-eq'tools'-and[string]::Equals($_.path,$tool,[StringComparison]::OrdinalIgnoreCase)-and$_.sha256-ceq$rawToolPolicy.program.sha256})
   Raw-Need ($declared.Count-eq1) 'TOOL_CLEANUP_NOT_DECLARED'
   $compilerPeers=@($rawPins|Where-Object{$_.role-eq'tools'-and[IO.Path]::GetFileName($_.path)-ieq'cl.exe'-and[string]::Equals([IO.Path]::GetDirectoryName($_.path),[IO.Path]::GetDirectoryName($tool),[StringComparison]::OrdinalIgnoreCase)})
   Raw-Need ($compilerPeers.Count-eq1) 'TOOL_CLEANUP_COMPILER_RELATION'
  }
  $rawLimit=16777216L;if($null-ne$rawRequest.maxOutputBytes){$rawLimit=[long]$rawRequest.maxOutputBytes};Raw-Need ($rawLimit-gt0-and$rawLimit-le16777216) 'OUTPUT_LIMIT'
  Raw-Need ((Raw-Now)-lt$rawRunCutoff-and-not$rawOwner.HasExited) 'NO_BUDGET_OR_OWNER_BEFORE_START'
  $rawStdout=[IO.Path]::Combine($rawDirectory,'stdout.raw');$rawStderr=[IO.Path]::Combine($rawDirectory,'stderr.raw');$rawCancel=[IO.Path]::Combine($rawDirectory,'cancel.request.json')
  Raw-Need (-not[IO.File]::Exists($rawCancel)) 'CANCEL_PRESENT_BEFORE_START'
  $rawJob.OpenOutputs($rawStdout,$rawStderr)
  # Keep the actual image immutable through native creation and its held process identity check.
  $rawImageLock=[IO.FileStream]::new($rawProgramFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $rawJob.StartSuspended($rawProgramFile,$rawArgv,$rawCwd,$rawEnvBlock)
  Raw-Need ([string]::Equals([IO.Path]::GetFullPath($rawJob.ActualImage),$rawProgramFile,[StringComparison]::OrdinalIgnoreCase)) 'ACTUAL_IMAGE_MISMATCH'
  $actualPin=Raw-Hash $rawJob.ActualImage;Raw-Need ($actualPin.sha256-ceq(@($rawPins|Where-Object{[string]::Equals($_.path,$rawProgramFile,[StringComparison]::OrdinalIgnoreCase)})[0].sha256)) 'ACTUAL_IMAGE_PIN'
  $rawProof=[ordered]@{pid=$rawJob.Pid;program=$actualPin;arguments=$rawArgv;cwd=$rawCwd;startFileTimeUtc=$rawJob.StartFileTime;startTimeBasis='GetProcessTimes on held native process handle before resume';actualImageBasis='QueryFullProcessImageNameW on held process handle, not argv';atomicJobAdmission=$rawJob.AtomicJobAdmission;environment=$rawRequest.environment;environmentBasis='exact supplied CreateProcessW Unicode environment block; not a post-start environment dump';originalDeadlineEpochMs=$DeadlineEpochMs;runCutoffEpochMs=$rawRunCutoff;cleanupReserveMs=$CleanupReserveMs;effectiveRunRemainingAtAdmissionMs=$rawRunCutoff-(Raw-Now);launchApi='CreateProcessW CREATE_SUSPENDED + atomic JOB_LIST + restricted HANDLE_LIST'}
  Raw-Publish ([IO.Path]::Combine($rawDirectory,'admission.json')) ([ordered]@{schemaVersion=1;state='SUSPENDED_IN_PRIVATE_JOB';business=$rawProof;launcherPid=$PID})
  Raw-Need ((Raw-Now)-lt$rawRunCutoff-and-not$rawOwner.HasExited) 'NO_BUDGET_OR_OWNER_BEFORE_RESUME'
  $rawJob.Resume();$rawStarted=Raw-Now
  Raw-Publish ([IO.Path]::Combine($rawDirectory,'started.json')) ([ordered]@{schemaVersion=1;state='RESUMED';pid=$rawJob.Pid;startFileTimeUtc=$rawJob.StartFileTime;resumedEpochMs=$rawStarted})
  while($true){
   if($rawOwner.HasExited){$rawReason='OWNER_LOST';break}
   if((Raw-Now)-ge$rawRunCutoff){$rawReason='TIMEOUT';break}
   if([IO.File]::Exists($rawCancel)){
    $cancelPin=Raw-Hash $rawCancel;Raw-Need ($cancelPin.bytes-gt0-and$cancelPin.bytes-le4096) 'CANCEL_SIZE'
    $cancel=[IO.File]::ReadAllText((Raw-Plain $rawCancel))|ConvertFrom-Json
    Raw-Need ($cancel.schemaVersion-eq1-and$cancel.kind-ceq'CANCEL'-and$cancel.nonce-ceq$rawRequest.cancelNonce-and$cancel.ownerPid-eq$OwnerProcessId-and[string]$cancel.ownerStartTicks-ceq$OwnerStartTicks.ToString()) 'CANCEL_REQUEST_INVALID'
    $rawReason='CANCELLED';break
   }
   $rawOutputBytes=$rawJob.OutputBytes;$rawErrorBytes=$rawJob.ErrorBytes
   if($rawOutputBytes-gt$rawLimit-or$rawErrorBytes-gt$rawLimit){$rawReason='OUTPUT_LIMIT';break}
   if($rawJob.RootExited){$rawRootExit=$rawJob.RootExitCode;$rawReason='NATURAL';break}
   [Threading.Thread]::Sleep(20)
  }
 }catch{$rawError=$_.Exception.Message;if($rawReason-eq'PREFLIGHT_FAILURE'){$rawReason='LAUNCH_OR_EVIDENCE_FAILURE'}}
 finally{
  if($rawJob){
   try{
    # Permit only scheduler accounting lag, not a new business/cleanup allowance.
    $settle=[Math]::Min($rawCleanupCutoff,(Raw-Now)+100)
    while($rawReason-eq'NATURAL'-and$rawJob.ActiveProcesses-gt0-and(Raw-Now)-lt$settle){[Threading.Thread]::Sleep(5)}
    $active=$rawJob.ActiveProcesses
    if($active-gt0){try{$rawMembersBeforeTermination=ConvertFrom-Json -InputObject ($rawJob.SnapshotMembers()) -NoEnumerate}catch{$rawMemberObservationError=$_.Exception.Message};if($rawReason-eq'NATURAL'){
      if($rawToolPolicy-and-not$rawMemberObservationError-and$rawMembersBeforeTermination.Count-eq1){
       $member=$rawMembersBeforeTermination[0]
       if($member.observed-eq$true-and$member.ownedAtQuery-eq$true-and[string]::Equals($member.image,[string]$rawToolPolicy.program.path,[StringComparison]::OrdinalIgnoreCase)){
        try{$toolPin=Raw-Hash $member.image;if($toolPin.sha256-ceq$rawToolPolicy.program.sha256){$rawToolCleanupEligible=$true;$rawToolCleanupVerified=@([ordered]@{pid=$member.pid;startFileTimeUtc=$member.startFileTimeUtc;program=$toolPin;scope='PINNED_MSVC_TELEMETRY_TOOL_ONLY_NOT_PRODUCT';ownedAtQuery=$true})}}catch{$rawToolCleanupEligible=$false}
       }
      }
      $rawReason=if($rawToolCleanupEligible){'DECLARED_TOOL_CLEANUP'}else{'RESIDUAL_DESCENDANTS'}
     };$rawJob.Terminate([uint32]3758096577)}
    do{$rawFinalQuery=$rawJob.ActiveProcesses;if($rawFinalQuery-eq0){$rawZero=$true;break};if((Raw-Now)-ge$rawCleanupCutoff){break};[Threading.Thread]::Sleep(10)}while($true)
    if($rawJob.Pid-gt0-and$rawJob.RootExited){$rawRootExit=$rawJob.RootExitCode;$rawJob.ReadExitTime()}
    $rawOutputBytes=$rawJob.OutputBytes;$rawErrorBytes=$rawJob.ErrorBytes
    $rawJob.CloseOutputs();$rawLogsClosed=$rawJob.OutputsClosed-and$rawJob.OutputsFlushed
    if($rawZero-and$rawLogsClosed-and$rawStdout-and$rawStderr-and[IO.File]::Exists($rawStdout)-and[IO.File]::Exists($rawStderr)){$rawLogPins=[ordered]@{stdout=Raw-Hash $rawStdout;stderr=Raw-Hash $rawStderr}}
   }catch{$rawQueryError=$_.Exception.Message}
   finally{try{$rawJob.CloseJob()}catch{$rawQueryError=($rawQueryError+';'+$_.Exception.Message)}}
  }
  if($rawImageLock){$rawImageLock.Dispose()}
  try{
   if($rawRequestPin){Raw-Need ((Raw-Hash $rawRequestFile).sha256-ceq$rawRequestPin.sha256) 'REQUEST_CHANGED_AFTER'}
   if($rawArgsPin){Raw-Need ((Raw-Hash $rawArguments).sha256-ceq$rawArgsPin.sha256) 'ARGUMENTS_CHANGED_AFTER'}
   foreach($p in $rawPins){$actual=Raw-Hash $p.path;Raw-Need ($actual.sha256-ceq$p.sha256) ('DECLARED_PIN_CHANGED_AFTER:'+ $p.path);$rawPostPins+=,$actual}
   $rawPinsMatch=$rawPins.Count-gt0-and$rawPostPins.Count-eq$rawPins.Count
  }catch{$rawPinsMatch=$false;$rawError=($rawError+';'+$_.Exception.Message)}
  if($rawOwner-and$rawOwner.HasExited-and$rawReason-eq'NATURAL'){$rawReason='OWNER_LOST'}
  if($rawOutputBytes-gt$rawLimit-or$rawErrorBytes-gt$rawLimit){$rawReason='OUTPUT_LIMIT'}
  $collected=$rawZero-and$rawLogsClosed-and$null-ne$rawLogPins-and$rawPinsMatch-and-not$rawError-and-not$rawQueryError-and(Raw-Now)-lt$DeadlineEpochMs
  $status=if(-not$collected){'UNKNOWN'}elseif($rawReason-eq'NATURAL'-and$rawRootExit-eq0){'EXITED'}elseif($rawReason-eq'DECLARED_TOOL_CLEANUP'){if($rawRootExit-eq0){'EXITED_WITH_TOOL_CLEANUP'}else{'FAILED_WITH_TOOL_CLEANUP'}}elseif($rawReason-in@('CANCELLED','TIMEOUT','OWNER_LOST')){$rawReason}else{'FAILED'}
  $rawExit=if($status-in@('EXITED','EXITED_WITH_TOOL_CLEANUP')){0}elseif($status-eq'TIMEOUT'){124}elseif($status-eq'CANCELLED'){125}elseif($status-eq'OWNER_LOST'){126}elseif($status-eq'UNKNOWN'){10}else{2}
  $terminal=[ordered]@{schemaVersion=1;kind='ymcc-raw-program-terminal-v1';scope='SINGLE_PRODUCER_ONLY_NOT_GLOBAL_BUDGET_OR_BUSINESS_ACCEPTANCE';status=$status;reason=$rawReason;collectionComplete=$collected;businessSucceeded=$status-eq'EXITED';launcher=$rawLauncher;owner=[ordered]@{pid=$OwnerProcessId;expectedStartTicks=$OwnerStartTicks.ToString();handleBound=$null-ne$rawOwner};request=$rawRequestPin;argumentsPin=$rawArgsPin;business=$rawProof;businessExitCode=$rawRootExit;businessExitFileTimeUtc=if($rawJob){$rawJob.ExitFileTime}else{$null};signal=$null;signalObserved=$false;signalSemantics='Windows native exit and explicit Job termination are recorded separately; no POSIX signal inferred';timedOut=$rawReason-eq'TIMEOUT';cancelled=$rawReason-eq'CANCELLED';cancelRequest=$cancelPin;ownerLost=$rawReason-eq'OWNER_LOST';residualDescendants=$rawReason-eq'RESIDUAL_DESCENDANTS';declaredToolCleanup=[ordered]@{requested=$null-ne$rawToolPolicy;policy=$rawToolPolicy;eligible=$rawToolCleanupEligible;verifiedMembers=$rawToolCleanupVerified;treatedAsNaturalExit=$false;productResourcesWaived=$false};job=[ordered]@{privateUnnamed=$true;membersBeforeTermination=$rawMembersBeforeTermination;memberObservationError=$rawMemberObservationError;activeProcessesFinal=$rawFinalQuery;zeroObservedBeforeDispose=$rawZero;queryCount=if($rawJob){$rawJob.QueryCount}else{0};terminateRequested=if($rawJob){$rawJob.TerminationRequested}else{$false};terminateSucceeded=if($rawJob){$rawJob.TerminationSucceeded}else{$false};disposed=if($rawJob){$rawJob.JobDisposed}else{$false};cleanupError=$rawQueryError};logs=$rawLogPins;logsClosedAndFlushed=$rawLogsClosed;stdoutBytes=$rawOutputBytes;stderrBytes=$rawErrorBytes;maxOutputBytes=$rawLimit;outputLimitExceeded=$rawOutputBytes-gt$rawLimit-or$rawErrorBytes-gt$rawLimit;declaredInputsBefore=$rawPins;declaredInputsAfter=$rawPostPins;declaredPinsUnchanged=$rawPinsMatch;undeclaredDependencyGraphProven=$false;startedEpochMs=$rawBegin;resumedEpochMs=$rawStarted;finishedEpochMs=Raw-Now;deadlineEpochMs=$DeadlineEpochMs;runCutoffEpochMs=$rawRunCutoff;cleanupReserveMs=$CleanupReserveMs;helperExitCode=$rawExit;error=$rawError;globalAcceptanceGranted=$false}
  if($rawTerminal){
   try{Raw-Write $rawTerminal $terminal;if((Raw-Now)-gt$DeadlineEpochMs){$terminal.status='UNKNOWN';$terminal.reason='DEADLINE_EXCEEDED_INCLUDING_WRITEBACK';$terminal.collectionComplete=$false;$terminal.businessSucceeded=$false;$terminal.helperExitCode=10;$terminal.finishedEpochMs=Raw-Now;Raw-Write $rawTerminal $terminal;$rawExit=10}}catch{$rawExit=10;Write-Error ('YMCC_RAW_TERMINAL_WRITE_FAILED:'+ $_.Exception.Message) -ErrorAction Continue}
   $rawTerminal.Dispose()
  }else{Write-Error ('YMCC_RAW_NO_TERMINAL_OWNERSHIP:'+ $rawError) -ErrorAction Continue}
  if($rawLease){$rawLease.Dispose()};if($rawJob){$rawJob.Dispose()};foreach($inputLock in $rawInputLocks){$inputLock.Dispose()};if($rawOwner){$rawOwner.Dispose()}
 }
 exit $rawExit
}
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$OutputEncoding=[Text.UTF8Encoding]::new($false)
if($PSVersionTable.PSVersion.Major -lt 7){Write-Error 'POWERSHELL_7_REQUIRED_FOR_OWNED_TEST_JOB';exit 10}
if($Child){
 $ready=$null;$taskGuardOwnerProcess=$null;$taskGuardOwnerWait=$null
 try {
  if($OwnerProcessId -le 0 -or $OwnerStartTicks -le 0){throw 'OWNED_TEST_OWNER_IDENTITY_REQUIRED'}
  $taskGuardOwnerProcess=[Diagnostics.Process]::GetProcessById($OwnerProcessId)
  # Bind the handle before reading birth so a reused PID cannot replace this owner.
  $taskGuardOwnerHandle=$taskGuardOwnerProcess.Handle
  if($taskGuardOwnerProcess.StartTime.ToUniversalTime().Ticks -ne $OwnerStartTicks){throw 'OWNED_TEST_OWNER_IDENTITY_CHANGED'}
  $ready=[Threading.EventWaitHandle]::OpenExisting($ReadyEvent)
  $taskGuardOwnerWait=[Threading.ManualResetEvent]::new($false)
  $taskGuardUnusedEvent=$taskGuardOwnerWait.SafeWaitHandle
  $taskGuardOwnerWait.SafeWaitHandle=[Microsoft.Win32.SafeHandles.SafeWaitHandle]::new($taskGuardOwnerHandle,$false)
  $taskGuardUnusedEvent.Dispose()
  # The process owns the borrowed handle; close this wait wrapper before disposing it.
  $taskGuardSignal=[Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($taskGuardOwnerWait,$ready),30000)
  if($taskGuardSignal -eq 0 -or $taskGuardOwnerProcess.HasExited){throw 'OWNED_TEST_OWNER_EXITED_BEFORE_ADMISSION'}
  if($taskGuardSignal -ne 1){throw 'OWNED_TEST_JOB_NOT_ASSIGNED'}
 } finally {
  if($taskGuardOwnerWait){$taskGuardOwnerWait.Dispose()}
  if($taskGuardOwnerProcess){$taskGuardOwnerProcess.Dispose()}
  if($ready){$ready.Dispose()}
 }
 $testArguments=@(Get-Content -LiteralPath $ArgumentsPath -Raw -Encoding UTF8|ConvertFrom-Json)
 if($ProgramPath){ & $ProgramPath @testArguments }
 else { & (Join-Path $PSHOME 'pwsh.exe') -NoLogo -NoProfile -NonInteractive -File $ScriptPath @testArguments }
 exit $LASTEXITCODE
}
Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class YmccGlobalOwnedJob : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong r,w,o,rb,wb,ob; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC {
  public long perProcess,perJob; public uint flags;
  public UIntPtr minWorking,maxWorking; public uint active;
  public UIntPtr affinity; public uint priority,scheduling;
 }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED {
  public BASIC basic; public IO_COUNTERS io;
  public UIntPtr processMemory,jobMemory,peakProcess,peakJob;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attrs,string name);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref EXTENDED info,uint length);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 IntPtr handle;
 public YmccGlobalOwnedJob() {
  handle=CreateJobObject(IntPtr.Zero,null);
  if(handle==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
  var info=new EXTENDED(); info.basic.flags=0x00002000;
  if(!SetInformationJobObject(handle,9,ref info,(uint)Marshal.SizeOf(typeof(EXTENDED)))) {
   var error=Marshal.GetLastWin32Error(); Dispose(); throw new Win32Exception(error);
  }
 }
 public void Assign(IntPtr process) {if(!AssignProcessToJobObject(handle,process)) throw new Win32Exception(Marshal.GetLastWin32Error());}
 public void Dispose(){if(handle!=IntPtr.Zero){CloseHandle(handle);handle=IntPtr.Zero;}}
}
"@
$eventName='Local\YMCC-Global-Owned-Test-'+[Guid]::NewGuid().ToString('N')
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$eventName)
$job=$null;$process=$null;$code=10;$deadlineExpired=$false;$jobDisposed=$false
try {
 if($DeadlineEpochMs -gt 0 -and [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -ge ($DeadlineEpochMs-$CleanupReserveMs)){throw 'OWNED_TEST_BUDGET_EXHAUSTED_BEFORE_START'}
 $job=[YmccGlobalOwnedJob]::new()
 $info=[Diagnostics.ProcessStartInfo]::new()
 $info.FileName=Join-Path $PSHOME 'pwsh.exe'
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $info.StandardOutputEncoding=[Text.Encoding]::UTF8;$info.StandardErrorEncoding=[Text.Encoding]::UTF8
 $info.WorkingDirectory=(Get-Location).ProviderPath
 $taskGuardSelf=[Diagnostics.Process]::GetCurrentProcess()
 try {$taskGuardOwnerBirth=$taskGuardSelf.StartTime.ToUniversalTime().Ticks} finally {$taskGuardSelf.Dispose()}
 $launchArguments=@('-NoLogo','-NoProfile','-NonInteractive','-File',$PSCommandPath,'-Child','-ReadyEvent',$eventName,'-OwnerProcessId',[string]$PID,'-OwnerStartTicks',[string]$taskGuardOwnerBirth,'-ArgumentsPath',$ArgumentsPath)
 if($ProgramPath){$launchArguments+=@('-ProgramPath',$ProgramPath)}else{$launchArguments+=@('-ScriptPath',$ScriptPath)}
 foreach($arg in $launchArguments){$info.ArgumentList.Add($arg)}
 $process=[Diagnostics.Process]::Start($info)
 $stdout=$process.StandardOutput.ReadToEndAsync()
 $stderr=$process.StandardError.ReadToEndAsync()
 $job.Assign($process.Handle)
 [void]$ready.Set()
 Write-Output ('YMCC_SIM_OWNED_JOB childPid='+$process.Id)
 if($DeadlineEpochMs -gt 0){
  $remaining=[Math]::Max(1,[Math]::Min([int]::MaxValue,$DeadlineEpochMs-$CleanupReserveMs-[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))
  if(-not $process.WaitForExit([int]$remaining)){$deadlineExpired=$true;$code=124}
 }else{$process.WaitForExit()}
 if(-not $deadlineExpired){$code=$process.ExitCode}
 $job.Dispose();$job=$null;$jobDisposed=$true
 if($deadlineExpired){[void]$process.WaitForExit([Math]::Max(1,$CleanupReserveMs))}
 [Console]::Out.Write($stdout.GetAwaiter().GetResult())
 [Console]::Error.Write($stderr.GetAwaiter().GetResult())
 if(-not $deadlineExpired){$code=$process.ExitCode}
} catch {
 Write-Error ('OWNED_TEST_JOB_FAILED: '+$_.Exception.GetType().Name) -ErrorAction Continue
 if($process -and -not $process.HasExited){$process.Kill()}
 $code=if($_.Exception.Message -eq 'OWNED_TEST_BUDGET_EXHAUSTED_BEFORE_START'){124}else{10}
 $deadlineExpired=$deadlineExpired -or ($code -eq 124)
} finally {
 if($job){$job.Dispose();$jobDisposed=$true}
 if($DeadlineReceiptPath){
  [ordered]@{schemaVersion=1;status=if($deadlineExpired){'TIMEOUT'}elseif($code -eq 0){'EXITED'}else{'FAILED'};exitCode=$code;deadlineExpired=$deadlineExpired;jobDisposed=$jobDisposed;childPid=if($process){$process.Id}else{$null};deadlineEpochMs=$DeadlineEpochMs;cleanupReserveMs=$CleanupReserveMs;finishedEpochMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()}|ConvertTo-Json|Set-Content -LiteralPath $DeadlineReceiptPath -Encoding utf8
 }
 if($process){$process.Dispose()}
 $ready.Dispose()
}
exit $code