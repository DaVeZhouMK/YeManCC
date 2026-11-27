using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

// CE-only adapter: process identity and bounded file IPC, never scans/reads/writes game memory.
// CE's own main-form handlers retain its configured scan and pause/resume behavior.
internal sealed class MemoryScanHost {
    const uint IdentityAccess=0x100000|0x1000;
    // Only the CE child returned by this launch is restored; never search for other processes.
    [StructLayout(LayoutKind.Sequential)]
    struct CePowerThrottlingState { public uint Version, ControlMask, StateMask; }
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool GetProcessInformation(IntPtr process,int informationClass,ref CePowerThrottlingState information,uint informationSize);
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool SetProcessInformation(IntPtr process,int informationClass,ref CePowerThrottlingState information,uint informationSize);
    static void RestoreCeScheduling(Process child){
        try{child.PriorityClass=ProcessPriorityClass.Normal;}catch(Exception){}
        try{
            IntPtr handle=child.Handle;var state=new CePowerThrottlingState{Version=1};
            if(GetProcessInformation(handle,4,ref state,12)&&(state.StateMask&1u)!=0){
                state.ControlMask|=1u;state.StateMask&=~1u;
                SetProcessInformation(handle,4,ref state,12);
            }
        }catch(Exception){}
    }

    readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=131072,RecursionLimit=16};
    static readonly CultureInfo Inv=CultureInfo.InvariantCulture;
    string root,token,imagePath,created,snapshotPath;int processId,revision;
    IntPtr gameHandle=IntPtr.Zero,ownerHandle=IntPtr.Zero;
    string ownerBirth,ownerPath;Process ce;Dictionary<string,object> state;
    FileStream sessionGate,ceGate;bool ownsSessionOutputs,ownsCeRecord,launchUnconfirmed,reconcileOnly;
    string ceRecordPath,cePriorDirectory;byte[] ceRecordImage;Dictionary<string,object> ceIdentity;
    readonly Stopwatch startup=Stopwatch.StartNew();
    DateTime lastPublish=DateTime.MinValue,lastCeSeen=DateTime.MinValue;bool closing;long heartbeat,lastHeartbeatAt=-1;
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int id);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h,uint ms);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref int count);
    [DllImport("kernel32.dll")] static extern bool ProcessIdToSessionId(int pid,out uint session);
    [StructLayout(LayoutKind.Sequential)] struct SessionProcess { public uint session,pid; public IntPtr name,sid; }
    [StructLayout(LayoutKind.Sequential)] struct CommandString { public ushort length,maximum; public IntPtr buffer; }
    [DllImport("wtsapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool WTSEnumerateProcessesW(IntPtr server,uint reserved,uint version,out IntPtr processes,out uint count);
    [DllImport("wtsapi32.dll")] static extern void WTSFreeMemory(IntPtr memory);
    [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int kind,IntPtr buffer,int length,out int returned);
    [DllImport("shell32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CommandLineToArgvW(string command,out int count);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);

    static Dictionary<int,string> SessionProcesses(uint session){
        IntPtr memory=IntPtr.Zero;uint count;
        if(!WTSEnumerateProcessesW(IntPtr.Zero,0,1,out memory,out count))throw new Win32Exception(Marshal.GetLastWin32Error(),"无法核验旧CE所在用户会话");
        try{
            if(count>4096)throw new InvalidOperationException("进程列表超限，旧CE会话未自动归还");
            var result=new Dictionary<int,string>();int size=Marshal.SizeOf(typeof(SessionProcess));
            for(int i=0;i<count;i++){
                var process=(SessionProcess)Marshal.PtrToStructure(IntPtr.Add(memory,i*size),typeof(SessionProcess));
                if(process.session!=session||process.pid<=4)continue;
                if(process.pid>Int32.MaxValue)throw new InvalidOperationException("CE会话进程标识无效");
                // UseShellExecute=false launches CE under this user's token.
                // WTS system processes without a user SID (e.g. CSRSS) cannot
                // be that child; they must not disable every user's recovery.
                if(process.sid==IntPtr.Zero)continue;
                result.Add((int)process.pid,new System.Security.Principal.SecurityIdentifier(process.sid).Value);
            }
            return result;
        }finally{if(memory!=IntPtr.Zero)WTSFreeMemory(memory);}
    }
    static string ProcessCommand(IntPtr handle){
        // This kernel metadata query does not open the target for VM_READ or
        // inspect its address space. Refuse unsupported/denied queries rather
        // than interpreting unreadable metadata as absence of an old CE.
        const int capacity=131072;IntPtr memory=Marshal.AllocHGlobal(capacity);
        try{
            int returned;int result=NtQueryInformationProcess(handle,60,memory,capacity,out returned);
            if(result<0)throw new Win32Exception(result,"进程启动参数未确认（NTSTATUS："+unchecked((uint)result).ToString("X8",Inv)+"）");
            if(returned<Marshal.SizeOf(typeof(CommandString))||returned>capacity)throw new InvalidDataException("进程启动参数长度无效");
            var command=(CommandString)Marshal.PtrToStructure(memory,typeof(CommandString));long offset=command.buffer.ToInt64()-memory.ToInt64();
            if(command.length==0||(command.length&1)!=0||command.length>command.maximum||offset<Marshal.SizeOf(typeof(CommandString))||offset+command.length>returned)throw new InvalidDataException("进程启动参数不可读");
            return Marshal.PtrToStringUni(command.buffer,command.length/2);
        }finally{Marshal.FreeHGlobal(memory);}
    }
    static string[] CommandArguments(string command){
        int count;IntPtr arguments=CommandLineToArgvW(command,out count);
        if(arguments==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"无法解析旧CE启动参数");
        try{
            if(count<1||count>4096)throw new InvalidDataException("进程参数数量无效");
            var result=new string[count];for(int i=0;i<count;i++)result[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(arguments,i*IntPtr.Size));return result;
        }finally{LocalFree(arguments);}
    }
    static bool SameArgumentPath(string left,string right){return String.Equals(left.Replace('/','\\'),right.Replace('/','\\'),StringComparison.OrdinalIgnoreCase);}
    static bool HasSessionArgument(string command,string trainer){var args=CommandArguments(command);for(int i=1;i<args.Length;i++)if(SameArgumentPath(args[i],trainer))return true;return false;}
    static bool ValidReleaseOutput(string session,string output){
        if(!Absolute(session)||!Absolute(output))return false;
        string name=Path.GetFileName(output);return Key(Path.GetDirectoryName(output))==Key(session)&&name.StartsWith("release-",StringComparison.Ordinal)&&name.EndsWith(".json",StringComparison.Ordinal)&&name.Length==45&&Hex(name.Substring(8,32),32);
    }
    static bool IsReleaseOnlyCommand(string command,string session){
        var args=CommandArguments(command);if(args.Length!=4||args[1]!="--release"||!SameArgumentPath(args[2],session))return false;
        try{return ValidReleaseOutput(session,args[3]);}catch(ArgumentException){return false;}catch(NotSupportedException){return false;}catch(PathTooLongException){return false;}
    }
    static string Key(string value){return Path.GetFullPath(value).TrimEnd('\\').ToLowerInvariant();}
    static long Now(){return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();}
    static string Text(Dictionary<string,object> o,string key){object x;if(o==null||!o.TryGetValue(key,out x)||x==null)return "";if(!(x is string))throw new InvalidDataException("CE会话文本类型无效: "+key);return (string)x;}
    static long Num(Dictionary<string,object> o,string key){object x;if(o==null||!o.TryGetValue(key,out x))return 0;if(!(x is int||x is long))throw new InvalidDataException("CE会话数字类型无效: "+key);return Convert.ToInt64(x,Inv);}
    static bool Hex(string x,int length){if(x==null||x.Length!=length)return false;foreach(char c in x)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
    static Dictionary<string,object> Obj(params object[] pairs){var x=new Dictionary<string,object>();for(int i=0;i<pairs.Length;i+=2)x[(string)pairs[i]]=pairs[i+1];return x;}
    static void NoLinks(string path){for(string p=Path.GetFullPath(path);!String.IsNullOrEmpty(p);p=Path.GetDirectoryName(p))if((File.Exists(p)||Directory.Exists(p))&&(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("CE会话路径不能经过链接");}
  sealed class SessionParser {
   readonly string text; int at;
   internal SessionParser(string value) { text=value; }
   void Space() { while(at<text.Length && (text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n')) ++at; }
   bool Take(char c) { Space(); if(at<text.Length && text[at]==c) { ++at; return true; } return false; }
   void Need(char c) { if(!Take(c)) throw new FormatException("JSON token missing"); }
   char Unit() { if(at+4>text.Length) throw new FormatException("JSON escape truncated"); ushort n; if(!ushort.TryParse(text.Substring(at,4),NumberStyles.AllowHexSpecifier,CultureInfo.InvariantCulture,out n)) throw new FormatException("JSON escape invalid"); at+=4; return (char)n; }
   string String() {
    Space(); if(at>=text.Length||text[at++]!='"') throw new FormatException("JSON string missing"); var result=new StringBuilder();
    while(at<text.Length) {
     char c=text[at++]; if(c=='"') return result.ToString(); if(c<32) throw new FormatException("JSON control character");
     bool escaped=false;
     if(c=='\\') {
      if(at>=text.Length) throw new FormatException("JSON escape truncated"); c=text[at++];
      switch(c) {
       case '"': case '\\': case '/': break;
       case 'b': c='\b'; break; case 'f': c='\f'; break; case 'n': c='\n'; break; case 'r': c='\r'; break; case 't': c='\t'; break;
       case 'u': c=Unit(); escaped=true; break;
       default: throw new FormatException("JSON escape invalid");
      }
     }
     if(char.IsHighSurrogate(c)) {
      char low;
      if(escaped) { if(at+2>text.Length||text[at++]!='\\'||text[at++]!='u') throw new FormatException("JSON surrogate missing"); low=Unit(); }
      else { if(at>=text.Length) throw new FormatException("JSON surrogate missing"); low=text[at++]; }
      if(!char.IsLowSurrogate(low)) throw new FormatException("JSON surrogate invalid"); result.Append(c); result.Append(low);
     } else { if(char.IsLowSurrogate(c)) throw new FormatException("JSON surrogate invalid"); result.Append(c); }
    }
    throw new FormatException("JSON string truncated");
   }
   object Number() {
    int begin=at; if(text[at]=='-') ++at;
    if(at>=text.Length) throw new FormatException("JSON number truncated");
    if(text[at]=='0') ++at; else { if(text[at]<'1'||text[at]>'9') throw new FormatException("JSON number invalid"); while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; }
    bool integral=true;
    if(at<text.Length&&text[at]=='.') { integral=false; ++at; int digits=at; while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; if(digits==at) throw new FormatException("JSON fraction invalid"); }
    if(at<text.Length&&(text[at]=='e'||text[at]=='E')) { integral=false; ++at; if(at<text.Length&&(text[at]=='+'||text[at]=='-')) ++at; int digits=at; while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; if(digits==at) throw new FormatException("JSON exponent invalid"); }
    string token=text.Substring(begin,at-begin); long integer; if(integral&&long.TryParse(token,NumberStyles.Integer,CultureInfo.InvariantCulture,out integer)) return integer;
    double number; if(!double.TryParse(token,NumberStyles.Float,CultureInfo.InvariantCulture,out number)||double.IsNaN(number)||double.IsInfinity(number)) throw new FormatException("JSON number not finite"); return number;
   }
   object Value(int depth) {
    Space(); if(at>=text.Length) throw new FormatException("JSON value missing"); char c=text[at];
    if(c=='{'||c=='[') { if(depth>16) throw new FormatException("JSON depth exceeded"); }
    if(c=='{') {
     ++at; var value=new Dictionary<string,object>(StringComparer.Ordinal); if(Take('}')) return value;
     do { string key=String(); Need(':'); if (value.ContainsKey(key)) throw new FormatException("Duplicate JSON field"); value[key]=Value(depth+1); if(Take('}')) return value; Need(','); } while(true);
    }
    if(c=='[') { ++at; var values=new List<object>(); if(Take(']')) return values; do { values.Add(Value(depth+1)); if(Take(']')) return values; Need(','); } while(true); }
    if(c=='"') return String();
    foreach(string token in new [] {"true","false","null"}) { if(at+token.Length<=text.Length && string.CompareOrdinal(text,at,token,0,token.Length)==0) { at+=token.Length; if(token=="null") return null; return token=="true"; } }
    if(c=='-'||(c>='0'&&c<='9')) return Number(); throw new FormatException("JSON value invalid");
   }
   internal Dictionary<string,object> Root() { var value=Value(0) as Dictionary<string,object>; Space(); if(value==null||at!=text.Length) throw new FormatException("JSON root or tail invalid"); return value; }
  }

    static void Supported(Dictionary<string,object> document){object schema;if(document==null)throw new InvalidDataException("CE会话根必须为对象");if(document.TryGetValue("schemaVersion",out schema)&&(!(schema is int||schema is long)||Convert.ToInt64(schema)!=1))throw new InvalidDataException("CE会话版本未知，原件保留");}
    Dictionary<string,object> Read(string path,int limit){
        for(int attempt=0;;attempt++){
            try{return ReadCore(path,limit);}catch(IOException ex){if((!SharingViolation(ex)&&!(ex is FileNotFoundException))||attempt>=5)throw;Thread.Sleep(10);}
        }
    }
    Dictionary<string,object> ReadCore(string path,int limit){
        NoLinks(path);byte[] data;
        using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
            if(file.Length>limit)throw new InvalidDataException("CE会话文件超限");data=new byte[(int)file.Length];int at=0;
            while(at<data.Length){int count=file.Read(data,at,data.Length-at);if(count<=0)throw new IOException("CE会话读取未完成");at+=count;}if(file.ReadByte()!=-1)throw new IOException("CE会话读取期间发生改变");
        }
        int skip=data.Length>=3&&data[0]==239&&data[1]==187&&data[2]==191?3:0;
        var result=new SessionParser(new UTF8Encoding(false,true).GetString(data,skip,data.Length-skip)).Root();Supported(result);return result;
    }

    static byte[] AtomicSource(string path){
        NoLinks(path);using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
            if(file.Length>262144)throw new InvalidDataException("CE写入前像超限");var bytes=new byte[(int)file.Length];int at=0;
            while(at<bytes.Length){int count=file.Read(bytes,at,bytes.Length-at);if(count<=0)throw new IOException("CE前像读取未完成");at+=count;}if(file.ReadByte()!=-1)throw new IOException("CE前像已改变");return bytes;
        }
    }
    static bool SameBytes(byte[] left,byte[] right){if(left==null||right==null)return left==right;if(left.Length!=right.Length)return false;for(int i=0;i<left.Length;i++)if(left[i]!=right[i])return false;return true;}
    static void Atomic(string path,string data){
        AtomicCore(path,data,false);
    }
    static void Handoff(string path,string data){
        AtomicCore(path,data,true);
    }
    static void AtomicCore(string path,string data,bool handoff){
        NoLinks(path);byte[] bytes=new UTF8Encoding(false,true).GetBytes(data);if(bytes.Length>262144)throw new InvalidDataException("CE会话写入超限");
        byte[] expected=null;try{expected=AtomicSource(path);}catch(FileNotFoundException){}
        if(handoff&&expected!=null)throw new InvalidDataException("已有CE消息尚未确认，未覆盖原件");
        if(expected!=null&&Path.GetExtension(path).Equals(".json",StringComparison.OrdinalIgnoreCase)){
            int skip=expected.Length>=3&&expected[0]==239&&expected[1]==187&&expected[2]==191?3:0;
            var old=new SessionParser(new UTF8Encoding(false,true).GetString(expected,skip,expected.Length-skip)).Root();Supported(old);
            var next=new SessionParser(data).Root();object tokenValue,previousToken;
            if(next.TryGetValue("token",out tokenValue)&&(!old.TryGetValue("token",out previousToken)||!(previousToken is string)||!(tokenValue is string)||(string)previousToken!=(string)tokenValue))throw new InvalidDataException("CE写入前像所有权未知");
        }
        // A request filename already contains a full ID. Appending another ID
        // can exceed MAX_PATH although every published IPC path is usable.
        // Sibling names retain the same 128-bit randomness and atomic volume.
        string nonce=Guid.NewGuid().ToString("N"),directory=Path.GetDirectoryName(path);
        string temp=Path.Combine(directory,nonce+".tmp"),backup=Path.Combine(directory,nonce+".old"),rollback=Path.Combine(directory,nonce+".undo");bool committed=false,confirmed=false,ownsTemp=false;
        try{using(var file=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){ownsTemp=true;file.Write(bytes,0,bytes.Length);file.Flush(true);}
            if(handoff&&!SameBytes(AtomicSource(temp),bytes))throw new IOException("CE消息提交前回读不一致");
            for(int i=0;;i++){
                byte[] current=null;try{current=AtomicSource(path);}catch(FileNotFoundException){}
                if(!SameBytes(current,expected))throw new InvalidDataException("CE写入前像已更替，未覆盖新原件");
                try{if(expected!=null){if(File.Exists(backup)||Directory.Exists(backup))throw new InvalidDataException("CE前像归档路径未知");File.Replace(temp,path,backup);}else File.Move(temp,path);committed=true;break;}catch(IOException){if(i>=5)throw;Thread.Sleep(10);}
            }
            // A consumer may rename/remove a committed message immediately.
            // Only the exact command/result receipt confirms its operation;
            // rereading the publication path here races a healthy claim.
            if(handoff){confirmed=true;return;}
            if(expected!=null&&!SameBytes(AtomicSource(backup),expected)){
                if(SameBytes(AtomicSource(path),bytes)){if(File.Exists(rollback)||Directory.Exists(rollback))throw new InvalidDataException("CE回退路径已有未知原件");File.Replace(backup,path,rollback);}
                throw new InvalidDataException("CE并发原件已保留，未确认写入");
            }
            if(!SameBytes(AtomicSource(path),bytes))throw new IOException("CE会话写后回读不一致");confirmed=true;
        }finally{if(ownsTemp&&File.Exists(temp))File.Delete(temp);if(committed&&confirmed&&File.Exists(backup))File.Delete(backup);}
    }
    void Save(string path,object value){
        SaveDocument(path,value,false);
    }
    void SaveHandoff(string path,object value){
        SaveDocument(path,value,true);
    }
    void SaveDocument(string path,object value,bool handoff){
        var document=value as Dictionary<string,object>;if(document==null)throw new InvalidDataException("CE会话写入根无效");Supported(document);
        if(File.Exists(path))Read(path,131072); // An unknown/future preimage is not write permission.
        var next=new Dictionary<string,object>(document);next["schemaVersion"]=1;string serialized=json.Serialize(next);if(new UTF8Encoding(false,true).GetByteCount(serialized)>131072)throw new InvalidDataException("CE JSON结果超过消费限制");if(handoff)Handoff(path,serialized);else Atomic(path,serialized);
    }
    static void ValidateSessionConfig(Dictionary<string,object> config){
        Supported(config);string t=Text(config,"token"),image=Text(config,"path"),birth=Text(config,"processCreated");long pid=Num(config,"pid"),owner=Num(config,"ownerPid");
        if(!Hex(t,64)||pid<=4||pid>Int32.MaxValue||owner<=4||owner>Int32.MaxValue||pid==owner||!Absolute(image)||!Birth(birth))throw new InvalidDataException("CE配置身份未确认");
        if(config.ContainsKey("ownerDirectory")&&!Absolute(Text(config,"ownerDirectory")))throw new InvalidDataException("CE所有者目录无效");
        if(config.ContainsKey("ownerPath")&&!Absolute(Text(config,"ownerPath")))throw new InvalidDataException("CE所有者路径无效");
        if(config.ContainsKey("ownerCreated")&&!Birth(Text(config,"ownerCreated")))throw new InvalidDataException("CE所有者出生身份无效");
        if(!config.ContainsKey("ownerDirectory")&&(!config.ContainsKey("ownerPath")||!config.ContainsKey("ownerCreated")))throw new InvalidDataException("CE所有者身份不完整");
    }
    static bool Absolute(string value){return !String.IsNullOrEmpty(value)&&Path.IsPathRooted(value)&&value.IndexOfAny(new[]{'\0','\r','\n'})<0;}
    static bool Birth(string value){long number;return value.Length>0&&value.Length<=19&&value[0]!='0'&&Int64.TryParse(value,NumberStyles.None,Inv,out number)&&number>0;}
    void ValidateLease(Dictionary<string,object> lease){Supported(lease);long age=Now()-Num(lease,"at");if(Text(lease,"token")!=token||!lease.ContainsKey("at")||age<0||age>8000)throw new InvalidDataException("CE连接租约未确认");}
    void ValidateRequest(Dictionary<string,object> request,string id){
        Supported(request);string action=Text(request,"action"),value=Text(request,"value");
        if(!Hex(id,32)||Text(request,"id")!=id||Text(request,"token")!=token||!request.ContainsKey("revision")||Num(request,"revision")!=revision)throw new InvalidDataException("CE请求身份或代次无效");
        if(action!="first"&&action!="next"&&action!="writeAll")throw new InvalidDataException("CE操作未授权");
        int number;if(value.Length<1||value.Length>12||value.Trim()!=value||!Int32.TryParse(value,NumberStyles.AllowLeadingSign,Inv,out number))throw new InvalidDataException("CE数值必须为4字节整数字符串");
        if(request.ContainsKey("dataType")&&Text(request,"dataType")!="int32")throw new InvalidDataException("CE数值类型未授权");
    }
    string ClaimRequest(string request){NoLinks(request);if(Key(Path.GetDirectoryName(request))!=Key(Path.Combine(root,"requests"))||!Hex(Path.GetFileNameWithoutExtension(request),32))throw new InvalidDataException("CE请求路径无效");string claim=Path.Combine(Path.GetDirectoryName(request),Guid.NewGuid().ToString("N")+".claim");File.Move(request,claim);return claim;}
    void FinishClaim(string claim,bool success){if(String.IsNullOrEmpty(claim))return;NoLinks(claim);if(Key(Path.GetDirectoryName(claim))!=Key(Path.Combine(root,"requests"))||!Hex(Path.GetFileNameWithoutExtension(claim),32)||Path.GetExtension(claim)!=".claim")throw new InvalidDataException("CE领取文件身份未知");if(success)File.Delete(claim);else if(File.Exists(claim))File.Move(claim,Path.ChangeExtension(claim,"rejected"));}

    internal string ResolveCE(string settingsFile){return ResolveCE(settingsFile,@"C:\SOFT\Cheat Engine");}
    internal string ResolveCE(string settingsFile,string defaultDirectory){
        string selected=File.Exists(settingsFile)?Text(Read(settingsFile,65536),"cePath"):"";
        selected=Environment.ExpandEnvironmentVariables(selected.Trim().Trim('"'));
        // A stale/relative GCM preference is not proof that the default CE is absent.
        // Discovery remains read-only and never resolves against the helper's CWD.
        string program=Absolute(selected)?ResolveCeProgram(selected):"";
        return program!=""?program:ResolveCeProgram(defaultDirectory);
    }
    static string ResolveCeProgram(string selected){
        if(!Absolute(selected))return "";
        if(Directory.Exists(selected)){
            // Bypass the architecture/setup launcher; never install or download CE.
            foreach(string name in new[]{"cheatengine-x86_64.exe","cheatengine-x86_64-SSE4-AVX2.exe"}){string file=Path.Combine(selected,name);if(File.Exists(file))return Path.GetFullPath(file);}return "";
        }
        if(Path.GetExtension(selected).ToLowerInvariant()!=".exe"||!File.Exists(selected))return "";
        if(Path.GetFileName(selected).Equals("Cheat Engine.exe",StringComparison.OrdinalIgnoreCase))return ResolveCeProgram(Path.GetDirectoryName(selected));
        return Path.GetFullPath(selected);
    }
    // CE's own FormShow reads this one DWORD before showing language/tutorial
    // dialogs. Only an explicit launch initializes it; discovery is read-only.
    // Never import defaults or touch scan, pause, language or debugger settings.
    internal static bool EnsureCeFirstRun(Microsoft.Win32.RegistryKey settings){
        object first=settings.GetValue("First Time User",null,Microsoft.Win32.RegistryValueOptions.DoNotExpandEnvironmentNames);
        if(first!=null&&(settings.GetValueKind("First Time User")!=Microsoft.Win32.RegistryValueKind.DWord||!(first is int)||((int)first!=0&&(int)first!=1)))throw new InvalidDataException("CE首次启动标记无效，已保留原设置");
        if(first!=null&&(int)first==0)return false;
        settings.SetValue("First Time User",0,Microsoft.Win32.RegistryValueKind.DWord);settings.Flush();
        if(!Object.Equals(settings.GetValue("First Time User"),0))throw new IOException("无法确认CE首次启动初始化");
        return true;
    }
    static string GcmSettings(){return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),"GCM Settings","settings.json");}
    static bool SharingViolation(IOException error){int code=error.HResult&65535;return code==32||code==33;}
    static FileStream Exclusive(string path){NoLinks(path);return new FileStream(path,FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);}
    static Dictionary<string,object> Member(Dictionary<string,object> value,string key){object item;if(!value.TryGetValue(key,out item)||!(item is Dictionary<string,object>))throw new InvalidDataException("CE所有权记录不完整: "+key);return (Dictionary<string,object>)item;}
    static void ValidateCeIdentity(Dictionary<string,object> identity){
        long pid=Num(identity,"pid");if(pid<=4||pid>Int32.MaxValue||!Birth(Text(identity,"processCreated"))||!Absolute(Text(identity,"path"))||!Path.GetExtension(Text(identity,"path")).Equals(".exe",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("旧CE进程身份未确认");
    }
    static void ValidateCeRecord(Dictionary<string,object> record){
        Supported(record);string phase=Text(record,"phase");if(Text(record,"engine")!="cheat-engine"||!Hex(Text(record,"session"),32)||(phase!="pending"&&phase!="active"&&phase!="released"))throw new InvalidDataException("CE所有权记录未知，原件保留");
        var owner=Member(record,"owner");if(!Hex(Text(owner,"token"),64)||Num(owner,"pid")<=4||Num(owner,"pid")>Int32.MaxValue||!Birth(Text(owner,"processCreated"))||!Absolute(Text(owner,"path")))throw new InvalidDataException("旧CE会话身份未确认");
        object child;if(!record.TryGetValue("ce",out child)||(child!=null&&!(child is Dictionary<string,object>))||(phase=="active"&&child==null))throw new InvalidDataException("旧CE启动结果未知");if(child!=null)ValidateCeIdentity((Dictionary<string,object>)child);
    }
    static bool SameSessionOwner(Dictionary<string,object> value,Dictionary<string,object> owner){
        return Text(value,"token")==Text(owner,"token")&&Num(value,"pid")==Num(owner,"pid")&&Text(value,"processCreated")==Text(owner,"processCreated")&&Key(Text(value,"path"))==Key(Text(owner,"path"));
    }
    bool ExactCeExited(Dictionary<string,object> identity){
        ValidateCeIdentity(identity);int pid=(int)Num(identity,"pid");IntPtr handle=OpenProcess(IdentityAccess,false,pid);
        if(handle==IntPtr.Zero){
            int error=Marshal.GetLastWin32Error();if(error!=87)throw new InvalidOperationException("无法核验旧CE进程，未启动替代会话（Windows错误："+error.ToString(Inv)+"）");
            // A failed open is not generally an exit receipt. Confirm that this
            // PID is absent; permission and enumeration faults remain unknown.
            try{using(var observed=Process.GetProcessById(pid)){throw new InvalidOperationException("旧CE进程仍存在但身份无法读取");}}catch(ArgumentException){return true;}
        }
        try{
            uint wait=WaitForSingleObject(handle,0);if(wait==0)return true;if(wait!=258)throw new InvalidOperationException("旧CE进程状态未知");
            long b,e,k,u;if(!GetProcessTimes(handle,out b,out e,out k,out u))throw new InvalidOperationException("无法核验旧CE出生身份");
            if(b.ToString(Inv)!=Text(identity,"processCreated"))return true; // PID was reused; the exact old child is gone.
            var path=new StringBuilder(32768);int count=path.Capacity;if(!QueryFullProcessImageName(handle,0,path,ref count)||Key(path.ToString())!=Key(Text(identity,"path")))throw new InvalidOperationException("旧CE路径身份未确认");return false;
        }finally{CloseHandle(handle);}
    }
    bool PreviousSessionReleased(string directory,Dictionary<string,object> owner,Dictionary<string,object> child,bool requireCapability){
        cePriorDirectory=directory;
        NoLinks(directory);var config=Read(Path.Combine(directory,"config.json"),16384);ValidateSessionConfig(config);if(!SameSessionOwner(config,owner))throw new InvalidDataException("旧CE所有权与会话配置不一致");
        if(child!=null)return ExactCeExited(child);
        string processFile=Path.Combine(directory,"ce-process.json");
        if(File.Exists(processFile)){var recorded=Read(processFile,4096);if(requireCapability&&Text(recorded,"token")!=Text(owner,"token"))throw new InvalidDataException("旧CE创建回执不属于该会话");return ExactCeExited(recorded);}
        string closedFile=Path.Combine(directory,"closed.json");
        if(File.Exists(closedFile)){var closed=Read(closedFile,4096);object exited;if(Text(closed,"engine")!="cheat-engine"||Text(closed,"stage")!="closed"||!SameSessionOwner(closed,owner)||!closed.TryGetValue("ceExited",out exited)||!(exited is bool))throw new InvalidDataException("旧CE关闭回执未确认");if((bool)exited)return true;}
        if(ConfirmNeverLaunched(directory,owner))return true;
        return ConfirmOrphanReleased(directory,owner);
    }
    bool ConfirmOrphanReleased(string directory,Dictionary<string,object> owner){
        // A durable single-use host gate excludes a late/remaining helper.
        // Never infer release just from elapsed time, a missing CE name, or
        // the currently configured installation directory.
        string gatePath=Path.Combine(directory,".host.lock");NoLinks(gatePath);
        var oldConfig=Read(Path.Combine(directory,"config.json"),16384);ValidateSessionConfig(oldConfig);
        if(!SameSessionOwner(oldConfig,owner))throw new InvalidDataException("旧CE配置身份改变");
        if(!File.Exists(gatePath)&&!LegacyDispatcherExited(oldConfig))throw new InvalidOperationException("旧CE缺少主机回执，原YMCC仍在运行；请正常退出并重新打开YMCC后重试");
        FileStream gate;
        try{gate=Exclusive(gatePath);}catch(IOException ex){if(SharingViolation(ex))return false;throw;}
        using(gate){
            string configPath=Path.Combine(directory,"config.json");byte[] configImage=AtomicSource(configPath);
            var config=Read(configPath,16384);ValidateSessionConfig(config);
            if(!SameSessionOwner(config,owner))throw new InvalidDataException("旧CE配置在进程核验时改变");
            uint session;if(!ProcessIdToSessionId(Process.GetCurrentProcess().Id,out session))throw new Win32Exception(Marshal.GetLastWin32Error(),"无法读取CE用户会话");
            string sid=System.Security.Principal.WindowsIdentity.GetCurrent().User.Value;
            var snapshot=SessionProcesses(session);int checkedCount=0;Dictionary<string,object> found=null;
            string trainer=Path.Combine(directory,"YMCC-Session.cetrainer");NoLinks(trainer);
            foreach(var entry in snapshot){
                if(entry.Value!=sid)continue;
                if(reconcileOnly&&entry.Key==Process.GetCurrentProcess().Id)continue;
                if(startup.ElapsedMilliseconds>=15000)throw new InvalidOperationException("旧CE进程核验超时，本次已取消，可以重新打开重试");
                Guard();IntPtr handle=OpenProcess(IdentityAccess,false,entry.Key);
                if(handle==IntPtr.Zero){
                    int error=Marshal.GetLastWin32Error();
                    if(error==87){using(var observed=ProcessOrNull(entry.Key)){if(observed==null)continue;}}
                    throw new Win32Exception(error,"旧CE会话进程核验受阻（PID："+entry.Key.ToString(Inv)+"）");
                }
                try{
                    uint wait=WaitForSingleObject(handle,0);if(wait==0)continue;if(wait!=258)throw new InvalidOperationException("旧CE会话进程状态未知");
                    string command;
                    try{command=ProcessCommand(handle);}
                    catch(Win32Exception ex){
                        if(WaitForSingleObject(handle,0)==0)continue;
                        // Windows can report STATUS_PROCESS_IS_TERMINATING
                        // before the held handle becomes signalled. Preserve
                        // the unknown observation and retry inside the existing
                        // deadline, rather than blocking recovery permanently.
                        if(ex.NativeErrorCode==unchecked((int)0xC000010A)){if(WaitForSingleObject(handle,100)==0)continue;return false;}
                        throw;
                    }
                    catch{if(WaitForSingleObject(handle,0)==0)continue;throw;}
                    checkedCount++;
                    // A legacy helper without the single-use gate can still
                    // be in flight. Its exact session argument prevents release
                    // until a later snapshot observes that helper gone too.
                    if(HasSessionArgument(command,directory)){
                        // A peer running the same adapter's exact release-only
                        // command cannot launch CE. Distinguish it from a
                        // legacy dispatch using the held process image, never
                        // just the executable filename or a flag substring.
                        if(IsReleaseOnlyCommand(command,directory)){
                            var peerImage=new StringBuilder(32768);int peerLength=peerImage.Capacity;
                            if(!QueryFullProcessImageName(handle,0,peerImage,ref peerLength)){if(WaitForSingleObject(handle,0)==0)continue;throw new InvalidOperationException("CE退出核验进程路径未确认");}
                            if(Key(peerImage.ToString())==Key(typeof(MemoryScanHost).Assembly.Location))continue;
                        }
                        return false;
                    }
                    if(!HasSessionArgument(command,trainer))continue;
                    long b,e,k,u;var path=new StringBuilder(32768);int length=path.Capacity;
                    if(!GetProcessTimes(handle,out b,out e,out k,out u)||!QueryFullProcessImageName(handle,0,path,ref length))throw new InvalidOperationException("无法确认旧CE的精确进程身份");
                    if(found!=null)throw new InvalidOperationException("同一旧CE会话存在多个进程，未创建替代CE");
                    found=Obj("token",Text(owner,"token"),"pid",entry.Key,"processCreated",b.ToString(Inv),"path",path.ToString(),"recoveryBasis","exact-session-argument");
                }finally{CloseHandle(handle);}
            }
            if(!SameBytes(configImage,AtomicSource(configPath)))throw new InvalidDataException("旧CE配置在进程核验期间改变");
            Guard();
            if(found!=null){
                // Recover only the exact child receipt, then use the existing
                // handle/birth/path exit check. Do not close or adopt manual CE.
                SaveHandoff(Path.Combine(directory,"ce-process.json"),found);
                return ExactCeExited(found);
            }
            var receipt=new Dictionary<string,object>(owner);receipt["engine"]="cheat-engine";receipt["basis"]="sealed-session-no-live-command";receipt["checkedProcesses"]=checkedCount;receipt["at"]=Now();
            Save(Path.Combine(directory,"admission-recovery.json"),receipt);
            return true;
        }
    }
    static Process ProcessOrNull(int pid){try{return Process.GetProcessById(pid);}catch(ArgumentException){return null;}}
    bool LegacyDispatcherExited(Dictionary<string,object> config){
        int pid=(int)Num(config,"ownerPid");IntPtr handle=OpenProcess(IdentityAccess,false,pid);
        if(handle==IntPtr.Zero){
            int error=Marshal.GetLastWin32Error();if(error!=87)throw new Win32Exception(error,"无法核验旧YMCC进程");
            using(var process=ProcessOrNull(pid))return process==null;
        }
        try{
            uint wait=WaitForSingleObject(handle,0);if(wait==0)return true;if(wait!=258)throw new InvalidOperationException("旧YMCC进程状态未知");
            string birth=Text(config,"ownerCreated");
            if(birth!=""){
                long b,e,k,u;if(!GetProcessTimes(handle,out b,out e,out k,out u))throw new InvalidOperationException("旧YMCC出生身份未确认");
                if(b.ToString(Inv)!=birth)return true;
            }
            var path=new StringBuilder(32768);int length=path.Capacity;
            if(!QueryFullProcessImageName(handle,0,path,ref length))throw new InvalidOperationException("旧YMCC路径未确认");
            string ownerImage=Text(config,"ownerPath"),ownerDirectory=Text(config,"ownerDirectory");
            if(ownerImage!=""&&Key(ownerImage)!=Key(path.ToString()))return true;
            if(ownerDirectory!=""&&Key(ownerDirectory)!=Key(Path.GetDirectoryName(path.ToString())))return true;
            return false;
        }finally{CloseHandle(handle);}
    }
    Dictionary<string,object> DiagnosticFile(string path,bool inspect){
        var result=Obj("file",Path.GetFileName(path));
        try{
            NoLinks(path);var file=new FileInfo(path);if(!file.Exists){result["observation"]="missing-or-unreadable";return result;}
            result["bytes"]=file.Length;result["observation"]="present";
            if(!inspect)return result;
            var document=Read(path,131072);var fields=new Dictionary<string,object>();
            // Explicit metadata allowlist: never export capability tokens,
            // trainer contents, scanner rows, searched values or game memory.
            foreach(string name in new[]{"schemaVersion","engine","session","phase","startupPhase","stage","pid","processCreated","path","ownerPid","ownerPath","ownerCreated","ownerDirectory","at","ceExited","basis","checkedProcesses","recoveryBasis"}){
                object value;if(document.TryGetValue(name,out value)&&(value is string||value is bool||value is int||value is long))fields[name]=value;
            }
            foreach(string name in new[]{"owner","ce"}){
                object value;if(!document.TryGetValue(name,out value)||!(value is Dictionary<string,object>))continue;
                var nested=(Dictionary<string,object>)value;var summary=new Dictionary<string,object>();
                foreach(string field in new[]{"pid","processCreated","path"})if(nested.TryGetValue(field,out value)&&(value is string||value is int||value is long))summary[field]=value;
                fields[name]=summary;
            }
            result["metadata"]=fields;
        }catch(Exception ex){result["observation"]="read-error";result["error"]=ex.Message;}
        return result;
    }
    void RecordFailure(Exception failure){
        try{
            if(root==null||!ownsSessionOutputs)return;
            string directory=Path.GetDirectoryName(root);var report=Obj("engine","cheat-engine","at",Now(),"attemptSession",Path.GetFileName(root),"error",failure.Message,"startupPhase",state==null?"":Text(state,"startupPhase"),"rawCapabilityTokensIncluded",false,"scanValuesIncluded",false,"scope","failure-time-file-metadata-not-atomic-process-exit-proof");
            report["journal"]=DiagnosticFile(Path.Combine(directory,"ce-owner.json"),true);
            if(cePriorDirectory!=null){
                report["previousSession"]=Path.GetFileName(cePriorDirectory);var files=new List<object>();
                foreach(string name in new[]{"config.json","lease.json","ce-process.json","closed.json","status.json","admission-recovery.json","ce-state.json","YMCC-Session.cetrainer","ce-lease.txt",".host.lock"})files.Add(DiagnosticFile(Path.Combine(cePriorDirectory,name),name.EndsWith(".json",StringComparison.Ordinal)));
                report["previousFiles"]=files;
            }
            Save(Path.Combine(directory,"ce-session-diagnostics.json"),report);
        }catch{/* Diagnostic failure never changes the original refusal. */}
    }
    bool ConfirmNeverLaunched(string directory,Dictionary<string,object> owner){
        // A renderer can stage config.json and disappear before dispatching the
        // helper. Claim its single-use gate so a late dispatch cannot launch it.
        // A free gate alone is NOT proof that a CE already dispatched has exited.
        foreach(string name in new[]{"YMCC-Session.cetrainer","ce-lease.txt","ce-process.json","ce-state.json","ce-command.txt"}){
            string file=Path.Combine(directory,name);NoLinks(file);if(File.Exists(file)||Directory.Exists(file))return false;
        }
        string gatePath=Path.Combine(directory,".host.lock");FileStream gate;
        try{gate=Exclusive(gatePath);}catch(IOException ex){if(SharingViolation(ex))return false;throw;}
        using(gate){
            var config=Read(Path.Combine(directory,"config.json"),16384);ValidateSessionConfig(config);
            if(!SameSessionOwner(config,owner))throw new InvalidDataException("旧CE配置在恢复时改变");
            // Every supported helper commits the trainer before Process.Start.
            // Retain uncertainty if any launch/engine evidence exists, even when
            // no matching CE process name is visible now.
            foreach(string name in new[]{"YMCC-Session.cetrainer","ce-lease.txt","ce-process.json","ce-state.json","ce-command.txt"}){
                string file=Path.Combine(directory,name);NoLinks(file);if(File.Exists(file)||Directory.Exists(file))return false;
            }
            string statusFile=Path.Combine(directory,"status.json");
            if(File.Exists(statusFile)){
                var status=Read(statusFile,131072);string phase=Text(status,"startupPhase");
                if(Text(status,"engine")!="cheat-engine"||Text(status,"stage")!="closed"||!SameSessionOwner(status,owner)||
                    (phase!="target-session"&&phase!="owner-identity"&&phase!="game-snapshot"&&phase!="ce-discovery"&&phase!="ce-ownership"&&phase!="ce-create"))return false;
            }else if(Directory.Exists(statusFile))return false;
            string receiptPath=Path.Combine(directory,"admission-recovery.json");
            if(File.Exists(receiptPath)){
                var receipt=Read(receiptPath,4096);
                if(Text(receipt,"engine")!="cheat-engine"||Text(receipt,"basis")!="never-dispatched"||!SameSessionOwner(receipt,owner))throw new InvalidDataException("旧CE恢复回执未确认");
            }else{
                var receipt=new Dictionary<string,object>(owner);receipt["engine"]="cheat-engine";receipt["basis"]="never-dispatched";receipt["at"]=Now();Save(receiptPath,receipt);
            }
            return true;
        }
    }
    void WaitForPrevious(string directory,Dictionary<string,object> owner,Dictionary<string,object> child,bool requireCapability){
        while(!PreviousSessionReleased(directory,owner,child,requireCapability)){Guard();if(startup.ElapsedMilliseconds>=20000)throw new InvalidOperationException("旧CE仍未退出，请稍后再打开");Thread.Sleep(100);}
    }
    void SaveCeRecord(string phase){
        if(ceGate==null)throw new InvalidOperationException("未持有CE所有权锁");
        byte[] current=null;try{current=AtomicSource(ceRecordPath);}catch(FileNotFoundException){}
        if(!SameBytes(current,ceRecordImage))throw new InvalidDataException("CE所有权记录已改变，原件保留");
        var record=Obj("engine","cheat-engine","session",Path.GetFileName(root),"owner",Obj("token",token,"pid",processId,"processCreated",created,"path",imagePath),"phase",phase,"ce",ceIdentity,"at",Now());
        ValidateCeRecord(record);Save(ceRecordPath,record);ceRecordImage=AtomicSource(ceRecordPath);ownsCeRecord=true;
    }
    void AcquireCeOwnership(){
        string directory=Path.GetDirectoryName(root),gatePath=Path.Combine(directory,".ce-owner.lock");ceRecordPath=Path.Combine(directory,"ce-owner.json");
        for(;;){Guard();if(startup.ElapsedMilliseconds>=20000)throw new InvalidOperationException("旧CE会话仍在使用，请稍后再打开");try{ceGate=Exclusive(gatePath);break;}catch(IOException ex){if(!SharingViolation(ex))throw;Thread.Sleep(100);}}
        if(File.Exists(ceRecordPath)){
            var previous=Read(ceRecordPath,16384);ValidateCeRecord(previous);ceRecordImage=AtomicSource(ceRecordPath);
            object child;previous.TryGetValue("ce",out child);WaitForPrevious(Path.Combine(directory,Text(previous,"session")),Member(previous,"owner"),child as Dictionary<string,object>,true);
        }else{
            // First adoption also respects older helper sessions that predate
            // the journal. Never use a process-name scan to guess ownership.
            string[] sessions=Directory.GetDirectories(directory);if(sessions.Length>256)throw new InvalidOperationException("旧CE会话记录过多，暂不启动新会话");
            foreach(string previous in sessions){if(Key(previous)==Key(root)||!Hex(Path.GetFileName(previous),32))continue;string config=Path.Combine(previous,"config.json");if(!File.Exists(config))continue;var owner=Read(config,16384);ValidateSessionConfig(owner);WaitForPrevious(previous,owner,null,false);}
        }
        Guard();SaveCeRecord("pending");
    }
    void AssertIdentity(IntPtr handle,string birth,string path){long b,e,k,u;var image=new StringBuilder(32768);int n=image.Capacity;if(handle==IntPtr.Zero||WaitForSingleObject(handle,0)!=258||!GetProcessTimes(handle,out b,out e,out k,out u)||b.ToString(Inv)!=birth||!QueryFullProcessImageName(handle,0,image,ref n)||Key(image.ToString())!=Key(path))throw new InvalidOperationException("锁定进程已退出或改变");}
    void Guard(){
        // Explicit release reconciliation performs no game/CE launch or memory
        // command. It remains valid after the old game or renderer has ended.
        if(reconcileOnly){if(startup.ElapsedMilliseconds>=12000)throw new InvalidOperationException("旧CE退出核验超时，请稍后重新打开");return;}
        if(File.Exists(Path.Combine(root,"stop")))throw new OperationCanceledException("CE会话已结束");
        var lease=Read(Path.Combine(root,"lease.json"),4096);ValidateLease(lease);
        AssertIdentity(ownerHandle,ownerBirth,ownerPath);AssertIdentity(gameHandle,created,imagePath);
    }
    void ValidateInitialTarget(){
        // Recognition admits a session once. Afterwards the held game process,
        // its birth time and image path are authoritative until stop or exit.
        // A foreground/recognition update must never rebind or cancel this CE.
        var target=Read(snapshotPath,65536);long age=Now()-Num(target,"lastSeen");if((!target.ContainsKey("valid")||!(target["valid"] is bool)||!(bool)target["valid"])||age<0||age>10000||Num(target,"generation")<=0||Num(target,"pid")!=processId||Text(target,"processCreated")!=created||Key(Text(target,"path"))!=Key(imagePath))throw new InvalidOperationException("YMCC锁定游戏已改变，CE搜索已取消");
    }
    void Open(string session){
        root=Path.GetFullPath(session);if(!Hex(Path.GetFileName(root),32))throw new InvalidOperationException("CE会话目录无效");NoLinks(root);
        // A duplicate helper for one capability cannot publish closed/stop on
        // behalf of the first helper, even when its own startup is refused.
        string sessionLock=Path.Combine(root,".host.lock");NoLinks(sessionLock);
        // Each capability is single-use even after a crash before the CE PID
        // receipt. Reusing it could falsely certify an unknown launch as closed.
        sessionGate=new FileStream(sessionLock,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None);
        foreach(string name in new[]{"status.json","ce-process.json","closed.json","ce-stop","ce-state.json"})if(File.Exists(Path.Combine(root,name)))throw new InvalidOperationException("CE会话已使用，请创建新的会话");
        ownsSessionOutputs=true;
        snapshotPath=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","game-target.json"));NoLinks(snapshotPath);
        var config=Read(Path.Combine(root,"config.json"),16384);ValidateSessionConfig(config);token=Text(config,"token");processId=(int)Num(config,"pid");created=Text(config,"processCreated");imagePath=Text(config,"path");
        // A valid capability may report a refusal before either process handle
        // is acquired. It must never publish ready until CE itself handshakes.
        state=ClosedState();state["startupPhase"]="target-session";
        int ownerId=(int)Num(config,"ownerPid");uint own,other;if(!Hex(token,64)||processId<=4||ownerId<=4||ownerId==processId||!Path.IsPathRooted(imagePath)||!ProcessIdToSessionId(Process.GetCurrentProcess().Id,out own)||!ProcessIdToSessionId(processId,out other)||own!=other||!ProcessIdToSessionId(ownerId,out other)||own!=other)throw new InvalidOperationException("CE目标身份无效");
        state["startupPhase"]="owner-identity";
        gameHandle=OpenProcess(IdentityAccess,false,processId);ownerHandle=OpenProcess(IdentityAccess,false,ownerId);
        long b,e,k,u;var image=new StringBuilder(32768);int count=image.Capacity;if(ownerHandle==IntPtr.Zero||!GetProcessTimes(ownerHandle,out b,out e,out k,out u)||!QueryFullProcessImageName(ownerHandle,0,image,ref count))throw new InvalidOperationException("无法核验YMCC进程");ownerBirth=b.ToString(Inv);ownerPath=image.ToString();
        if((Text(config,"ownerPath")!=""&&Key(Text(config,"ownerPath"))!=Key(ownerPath))||(Text(config,"ownerCreated")!=""&&Text(config,"ownerCreated")!=ownerBirth)||(Text(config,"ownerDirectory")!=""&&Key(Text(config,"ownerDirectory"))!=Key(Path.GetDirectoryName(ownerPath))))throw new InvalidOperationException("YMCC会话身份已改变");
        state["startupPhase"]="game-snapshot";Guard();ValidateInitialTarget();
    }
    void Publish(bool force){if(!ownsSessionOutputs||state==null)return;if(!force&&(DateTime.UtcNow-lastPublish).TotalMilliseconds<300)return;var result=new Dictionary<string,object>(state);result["token"]=token;result["at"]=Now();result["engine"]="cheat-engine";result["pid"]=processId;result["processCreated"]=created;result["path"]=imagePath;result["revision"]=revision;result["offset"]=0;result["pageSize"]=40;result["selected"]="";result["selectedValue"]=null;result["frozen"]=false;result["frozenValue"]=null;result["skippedBlocks"]=0;result["bulkLimit"]=32;Save(Path.Combine(root,"status.json"),result);lastPublish=DateTime.UtcNow;}
    void Heartbeat(){
        try{Atomic(Path.Combine(root,"ce-lease.txt"),token+"\n"+(++heartbeat).ToString(Inv));lastHeartbeatAt=startup.ElapsedMilliseconds;}
        catch(IOException ex){
            // Only Windows sharing/lock collisions on this heartbeat may retry.
            // Never soften command/state/identity errors or manufacture a renewal.
            int code=ex.HResult&0xffff;if((code!=32&&code!=33)||lastHeartbeatAt<0)throw;
            if(startup.ElapsedMilliseconds-lastHeartbeatAt>=2000)throw new IOException("CE心跳文件持续被占用，后台连接已停止，请稍后重新开始",ex);
        }
    }
    void LaunchCE(string executable){
        state["startupPhase"]="ce-create";
        Guard();
        using(var settings=Microsoft.Win32.Registry.CurrentUser.CreateSubKey(@"Software\Cheat Engine"))EnsureCeFirstRun(settings);
        Guard();
        string scriptFile=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"CeScanEngine.lua");NoLinks(scriptFile);if(!File.Exists(scriptFile))throw new InvalidOperationException("缺少后台CE连接脚本");
        string lua="YMCC_CE_SESSION={root="+Lua(root)+",token="+Lua(token)+",pid="+processId.ToString(Inv)+"}\n"+File.ReadAllText(scriptFile,Encoding.UTF8);
        string trainer=Path.Combine(root,"YMCC-Session.cetrainer");Atomic(trainer,"<?xml version=\"1.0\" encoding=\"utf-8\"?><CheatTable CheatEngineTableVersion=\"45\"><LuaScript>"+SecurityElement.Escape(lua)+"</LuaScript></CheatTable>");
        Heartbeat();Guard();var info=new ProcessStartInfo(executable,"NOAUTORUN \""+trainer+"\""){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,WorkingDirectory=Path.GetDirectoryName(executable)};
        launchUnconfirmed=true;try{ce=Process.Start(info);}catch(System.ComponentModel.Win32Exception){launchUnconfirmed=false;throw;}
        if(ce==null)throw new InvalidOperationException("CE创建结果未确认");launchUnconfirmed=false;RestoreCeScheduling(ce);ceIdentity=Obj("pid",ce.Id,"path",executable,"processCreated",ce.StartTime.ToUniversalTime().ToFileTimeUtc().ToString(Inv));
        var receipt=new Dictionary<string,object>(ceIdentity);receipt["token"]=token;Save(Path.Combine(root,"ce-process.json"),receipt);SaveCeRecord("active");
        state["startupPhase"]="ce-handshake";
        while(startup.ElapsedMilliseconds<20000){Guard();Heartbeat();if(File.Exists(Path.Combine(root,"ce-state.json"))){try{state=Read(Path.Combine(root,"ce-state.json"),131072);ValidateCeState(state);lastCeSeen=DateTime.UtcNow;if(Text(state,"token")==token){if(Text(state,"stage")=="closed")throw new InvalidOperationException(Text(state,"message"));return;}}catch(FileNotFoundException){}}if(ce.HasExited)throw new InvalidOperationException("已找到CE，但程序启动后退出（退出码："+ce.ExitCode.ToString(Inv)+"），请检查CE能否单独打开");Thread.Sleep(100);}throw new InvalidOperationException("已找到CE，但后台连接未就绪，已取消操作");
    }
    static string Lua(string value){return "\""+value.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r","\\r").Replace("\n","\\n")+"\"";}

    void ValidateCeState(Dictionary<string,object> next){
        Supported(next);if(Text(next,"token")!=token||Text(next,"engine")!="cheat-engine")throw new InvalidDataException("CE回执身份未知");
        string stage=Text(next,"stage");if(stage!="idle"&&stage!="scanning"&&stage!="writing"&&stage!="ready"&&stage!="failed"&&stage!="closed")throw new InvalidDataException("CE状态未知");
        foreach(string field in new[]{"count","scanRound","lastWriteCount","scannedBytes","totalBytes","scanElapsedMs"})if(!next.ContainsKey(field)||Num(next,field)<0)throw new InvalidDataException("CE进度类型无效");
        if(Num(next,"lastWriteCount")>32||Text(next,"dataType")!="int32")throw new InvalidDataException("CE结果类型无效");
        foreach(string field in new[]{"temporarilyPaused","pauseRestored"})if(!next.ContainsKey(field)||!(next[field] is bool))throw new InvalidDataException("CE暂停状态未知");
        foreach(string field in new[]{"message","searchValue","lastWriteValue","commandId"})if(!next.ContainsKey(field)||!(next[field] is string))throw new InvalidDataException("CE结果文本类型无效");
        string command=Text(next,"commandId");if(command!=""&&!Hex(command,32))throw new InvalidDataException("CE命令身份无效");
        object rows;var list=next.TryGetValue("rows",out rows)?rows as System.Collections.IList:null;if(list==null||list.Count>40||Num(next,"count")<list.Count)throw new InvalidDataException("CE结果列表无效");
        foreach(object item in list){var row=item as Dictionary<string,object>;if(row==null)throw new InvalidDataException("CE结果行无效");string address=Text(row,"address");if(address.Length!=16)throw new InvalidDataException("CE地址回执无效");foreach(char c in address)if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))throw new InvalidDataException("CE地址回执无效");object value;if(!row.TryGetValue("value",out value)||(value!=null&&!(value is string)))throw new InvalidDataException("CE结果值类型无效");}
        // Legacy internal CE receipts bind through their unique64hex capability.
        // If richer identity is present it must match, never be coerced/ignored.
        if(next.ContainsKey("pid")&&Num(next,"pid")!=processId)throw new InvalidDataException("CE进程回执失配");
        if(next.ContainsKey("processCreated")&&Text(next,"processCreated")!=created)throw new InvalidDataException("CE进程出生回执失配");
        if(next.ContainsKey("path")&&(!Absolute(Text(next,"path"))||Key(Text(next,"path"))!=Key(imagePath)))throw new InvalidDataException("CE路径回执失配");
    }
    Dictionary<string,object> ClosedState(){return Obj("stage","closed","message","CE会话状态未知，请显式重新打开","count",0,"rows",new object[0],"scanRound",0,"searchValue","","lastWriteCount",0,"lastWriteValue","","scannedBytes",0,"totalBytes",0,"scanElapsedMs",0,"temporarilyPaused",false,"pauseRestored",true,"dataType","int32");}
    void CloseState(string message){
        var previous=state;state=ClosedState();state["message"]=message;
        // Results lose authority at failure. CE's last reported pause state is
        // retained as evidence; closing the adapter is not a resume receipt.
        object value;if(previous!=null){foreach(string name in new[]{"temporarilyPaused","pauseRestored"})if(previous.TryGetValue(name,out value)&&value is bool)state[name]=value;
            if(previous.TryGetValue("startupPhase",out value)&&value is string&&((string)value).Length<=32)state["startupPhase"]=value;}
    }
    bool Refresh(){
        try{var next=Read(Path.Combine(root,"ce-state.json"),131072);ValidateCeState(next);state=next;lastCeSeen=DateTime.UtcNow;
            // A terminal CE receipt revokes admission before Run reads another
            // queued request. The receipt, not adapter teardown, proves pause cleanup.
            if(Text(state,"stage")=="closed"){closing=true;throw new InvalidOperationException(Text(state,"message")==""?"后台CE会话已结束":Text(state,"message"));}return true;}
        catch(FileNotFoundException ex){if((DateTime.UtcNow-lastCeSeen).TotalMilliseconds<=8000)return false;CloseState(ex.Message);closing=true;throw;}
        catch(DirectoryNotFoundException ex){CloseState(ex.Message);closing=true;throw;}
        catch(Exception ex){CloseState(ex.Message);closing=true;throw;}
    }
    void Command(string requestFile){string claim=null;string id=Path.GetFileNameWithoutExtension(requestFile);if(!Hex(id,32))return;bool ok=false;string error="";try{
        Guard();if(File.Exists(Path.Combine(root,"responses",id+".json")))throw new InvalidDataException("CE请求已存在回执，拒绝重放");claim=ClaimRequest(requestFile);var request=Read(claim,16384);ValidateRequest(request,id);if(Text(request,"id")!=id||Text(request,"token")!=token)throw new InvalidOperationException("CE请求身份无效");string action=Text(request,"action"),value=Text(request,"value");if(action!="first"&&Num(request,"revision")!=revision)throw new InvalidOperationException("搜索结果已更新，请重试");if(action!="first"&&action!="next"&&action!="writeAll")throw new InvalidOperationException("该功能只接受CE搜索和确认修改");int number;if(value.Length>12||!Int32.TryParse(value,NumberStyles.Integer,Inv,out number))throw new InvalidOperationException("请输入4字节整数范围内的数字");
        if(!Refresh())throw new InvalidDataException("CE状态未确认，未派发操作");
        Handoff(Path.Combine(root,"ce-command.txt"),id+"\n"+token+"\n"+action+"\n"+value+"\n");var wait=Stopwatch.StartNew();
        while(wait.ElapsedMilliseconds<600000){Guard();Heartbeat();if(ce.HasExited)throw new InvalidOperationException("后台CE已退出");Refresh();Publish(false);string stage=Text(state,"stage");if(Text(state,"commandId")==id&&stage!="scanning"&&stage!="writing"){revision++;if(stage=="failed"||stage=="closed")throw new InvalidOperationException(Text(state,"message"));ok=true;break;}Thread.Sleep(100);}
        if(!ok)throw new InvalidOperationException("后台CE长时间未返回，请取消搜索");
    }catch(Exception ex){error=ex.Message;if(claim==null||ex is IOException||ex is FormatException||Text(state,"stage")=="scanning"||ex is OperationCanceledException){Atomic(Path.Combine(root,"ce-stop"),"stop");closing=true;}if(closing)CloseState(error);else{state["message"]=error;state["stage"]="failed";}}
    finally{try{FinishClaim(claim,ok);}catch{}Publish(true);if(!File.Exists(Path.Combine(root,"responses",id+".json")))SaveHandoff(Path.Combine(root,"responses",id+".json"),Obj("id",id,"token",token,"ok",ok,"error",error,"state",Read(Path.Combine(root,"status.json"),131072)));}}
    void Run(string session){Open(session);state["startupPhase"]="ce-discovery";string executable=ResolveCE(GcmSettings());if(executable=="")throw new InvalidOperationException("未安装CE，简单改数值功能不可用");state["startupPhase"]="ce-ownership";AcquireCeOwnership();LaunchCE(executable);Publish(true);while(!closing){Guard();Heartbeat();if(ce.HasExited)throw new InvalidOperationException("后台CE已退出");Refresh();string[] requests=Directory.GetFiles(Path.Combine(root,"requests"),"*.json");Array.Sort(requests,StringComparer.Ordinal);if(requests.Length>0)Command(requests[0]);Publish(false);Thread.Sleep(100);}}
    bool ExactReleasedReceipt(string path,Dictionary<string,object> owner){
        try{var closed=Read(path,4096);object exited;return Text(closed,"engine")=="cheat-engine"&&Text(closed,"stage")=="closed"&&SameSessionOwner(closed,owner)&&closed.TryGetValue("ceExited",out exited)&&exited is bool&&(bool)exited;}
        catch{return false;}
    }
    int ReconcileRelease(string session,string output){
        root=Path.GetFullPath(session);output=Path.GetFullPath(output);NoLinks(root);NoLinks(output);
        if(!Hex(Path.GetFileName(root),32)||!ValidReleaseOutput(root,output))throw new InvalidDataException("CE退出核验路径无效");
        var config=Read(Path.Combine(root,"config.json"),16384);ValidateSessionConfig(config);
        token=Text(config,"token");processId=(int)Num(config,"pid");created=Text(config,"processCreated");imagePath=Text(config,"path");reconcileOnly=true;
        bool released=false;string reason="";
        try{
            while(!released){
                Guard();Dictionary<string,object> child=null;
                string journalPath=Path.Combine(Path.GetDirectoryName(root),"ce-owner.json");
                if(File.Exists(journalPath)){
                    var journal=Read(journalPath,16384);ValidateCeRecord(journal);
                    if(Text(journal,"session")==Path.GetFileName(root)){
                        if(!SameSessionOwner(config,Member(journal,"owner")))throw new InvalidDataException("旧CE记录与退出核验身份不一致");
                        object value;journal.TryGetValue("ce",out value);child=value as Dictionary<string,object>;
                    }
                }
                released=PreviousSessionReleased(root,config,child,true);
                if(!released)Thread.Sleep(100);
            }
            // This receipt proves process release only. It never asserts CE's
            // pause cleanup, revives a scan, or changes the global CE journal.
            string closedPath=Path.Combine(root,"closed.json");
            if(!ExactReleasedReceipt(closedPath,config)){
                try{Save(closedPath,Obj("engine","cheat-engine","stage","closed","token",token,"pid",processId,"processCreated",created,"path",imagePath,"at",Now(),"ceExited",true,"releaseBasis","explicit-owned-session-reconciliation"));}
                catch{if(!ExactReleasedReceipt(closedPath,config))throw;} // A peer may have committed the exact same release proof.
            }
        }catch(Exception ex){released=false;reason=ex.Message;}
        SaveHandoff(output,Obj("engine","cheat-engine","token",token,"pid",processId,"processCreated",created,"path",imagePath,"released",released,"message",reason,"at",Now()));
        return released?0:1;
    }
    void Shutdown(){
        if(ownsSessionOutputs){try{Atomic(Path.Combine(root,"ce-stop"),"stop");}catch{}}
        bool exited=ce==null&&!launchUnconfirmed;
        if(ce!=null){try{exited=ce.WaitForExit(10000);}catch{}ce.Dispose();}
        if(gameHandle!=IntPtr.Zero){CloseHandle(gameHandle);gameHandle=IntPtr.Zero;}
        if(ownerHandle!=IntPtr.Zero){CloseHandle(ownerHandle);ownerHandle=IntPtr.Zero;}
        // A closed UI/status is not process release. Publish only after waiting
        // for this session's exact owned CE, without killing any other trainer.
        if(ownsSessionOutputs&&Hex(token,64)){try{Save(Path.Combine(root,"closed.json"),Obj("engine","cheat-engine","stage","closed","token",token,"pid",processId,"processCreated",created,"path",imagePath,"at",Now(),"ceExited",exited));}catch{}}
        if(ownsCeRecord&&exited){try{SaveCeRecord("released");}catch{}}
        if(ceGate!=null){ceGate.Dispose();ceGate=null;}if(sessionGate!=null){sessionGate.Dispose();sessionGate=null;}ownsSessionOutputs=false;
    }
    [STAThread]static int Main(string[] args){YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();var host=new MemoryScanHost();try{
        if(args.Length==3&&args[0]=="--release")return host.ReconcileRelease(args[1],args[2]);
        if(args.Length==2&&args[0]=="--probe"){
            Dictionary<string,object> result;
            try{string executable=host.ResolveCE(GcmSettings());result=Obj("engine","cheat-engine","available",executable!="","executable",executable,"code",executable!=""?"ready":"ce-not-found","message",executable!=""?"CE已找到":"未安装CE，简单改数值功能不可用（默认目录：C:\\SOFT\\Cheat Engine）");}
            catch(Exception ex){result=Obj("engine","cheat-engine","available",false,"code","ce-config-unreadable","message","无法读取CE路径配置："+ex.Message);}
            host.SaveHandoff(Path.GetFullPath(args[1]),result);return 0;
        }
        if(args.Length!=1)return 2;host.Run(args[0]);return 0;
    }catch(Exception ex){host.RecordFailure(ex);try{if(host.state!=null){host.CloseState(ex.Message);host.Publish(true);}}catch{}return 1;}finally{host.Shutdown();}}
}

// Shared startup policy; this file also stays compatible with .NET Framework csc 4.
namespace YeManCC
{
    using System;
    using System.Runtime.InteropServices;
    using System.Threading;

    internal static class ProcessEfficiency
    {
        private const int ProcessPowerThrottling = 4;
        private const uint ExecutionSpeed = 1;
        private const uint IdlePriorityClass = 0x40;
        private static int applied;

        [StructLayout(LayoutKind.Sequential)]
        private struct PowerThrottlingState
        {
            public uint Version;
            public uint ControlMask;
            public uint StateMask;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetProcessInformation(IntPtr process, int informationClass,
            ref PowerThrottlingState information, uint informationSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetProcessInformation(IntPtr process, int informationClass,
            ref PowerThrottlingState information, uint informationSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetPriorityClass(IntPtr process, uint priorityClass);

        internal static void ApplyCurrentProcessOnce()
        {
            if (Interlocked.Exchange(ref applied, 1) != 0)
                return;

            // GetCurrentProcess pseudo-handle: no other process is opened or changed.
            IntPtr process = new IntPtr(-1);
            try
            {
                var state = new PowerThrottlingState { Version = 1 };
                if (GetProcessInformation(process, ProcessPowerThrottling, ref state, 12))
                {
                    // A failed read must not replace unknown control/state bits.
                    state.ControlMask |= ExecutionSpeed;
                    state.StateMask |= ExecutionSpeed;
                    SetProcessInformation(process, ProcessPowerThrottling, ref state, 12);
                }
            }
            catch (Exception)
            {
                // Unsupported APIs (including old Windows entry points) are optional.
            }

            try
            {
                // Independent of EcoQoS support or failure.
                SetPriorityClass(process, IdlePriorityClass);
            }
            catch (Exception)
            {
                // Startup must continue even if the priority request is unavailable.
            }
        }
    }
}
