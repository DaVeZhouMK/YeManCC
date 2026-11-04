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
    DateTime lastPublish=DateTime.MinValue,lastCeSeen=DateTime.MinValue;bool closing;long heartbeat;
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int id);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h,uint ms);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref int count);
    [DllImport("kernel32.dll")] static extern bool ProcessIdToSessionId(int pid,out uint session);
    static string Key(string value){return Path.GetFullPath(value).TrimEnd('\\').ToLowerInvariant();}
    static long Now(){return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();}
    static string Text(Dictionary<string,object> o,string key){object x;if(o==null||!o.TryGetValue(key,out x)||x==null)return "";if(!(x is string))throw new InvalidDataException("CE会话文本类型无效: "+key);return (string)x;}
    static long Num(Dictionary<string,object> o,string key){object x;if(o==null||!o.TryGetValue(key,out x))return 0;if(!(x is int||x is long))throw new InvalidDataException("CE会话数字类型无效: "+key);return Convert.ToInt64(x,Inv);}
    static bool Hex(string x,int length){if(x.Length!=length)return false;foreach(char c in x)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
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
        NoLinks(path);byte[] data;
        using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete)){
            if(file.Length>limit)throw new InvalidDataException("CE会话文件超限");data=new byte[(int)file.Length];int at=0;
            while(at<data.Length){int count=file.Read(data,at,data.Length-at);if(count<=0)throw new IOException("CE会话读取未完成");at+=count;}if(file.ReadByte()!=-1)throw new IOException("CE会话读取期间发生改变");
        }
        int skip=data.Length>=3&&data[0]==239&&data[1]==187&&data[2]==191?3:0;
        var result=new SessionParser(new UTF8Encoding(false,true).GetString(data,skip,data.Length-skip)).Root();Supported(result);return result;
    }

    static byte[] AtomicSource(string path){
        NoLinks(path);using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete)){
            if(file.Length>262144)throw new InvalidDataException("CE写入前像超限");var bytes=new byte[(int)file.Length];int at=0;
            while(at<bytes.Length){int count=file.Read(bytes,at,bytes.Length-at);if(count<=0)throw new IOException("CE前像读取未完成");at+=count;}if(file.ReadByte()!=-1)throw new IOException("CE前像已改变");return bytes;
        }
    }
    static bool SameBytes(byte[] left,byte[] right){if(left==null||right==null)return left==right;if(left.Length!=right.Length)return false;for(int i=0;i<left.Length;i++)if(left[i]!=right[i])return false;return true;}
    static void Atomic(string path,string data){
        NoLinks(path);byte[] bytes=new UTF8Encoding(false,true).GetBytes(data);if(bytes.Length>262144)throw new InvalidDataException("CE会话写入超限");
        byte[] expected=null;try{expected=AtomicSource(path);}catch(FileNotFoundException){}
        if(expected!=null&&Path.GetExtension(path).Equals(".json",StringComparison.OrdinalIgnoreCase)){
            int skip=expected.Length>=3&&expected[0]==239&&expected[1]==187&&expected[2]==191?3:0;
            var old=new SessionParser(new UTF8Encoding(false,true).GetString(expected,skip,expected.Length-skip)).Root();Supported(old);
            var next=new SessionParser(data).Root();object tokenValue,previousToken;
            if(next.TryGetValue("token",out tokenValue)&&(!old.TryGetValue("token",out previousToken)||!(previousToken is string)||!(tokenValue is string)||(string)previousToken!=(string)tokenValue))throw new InvalidDataException("CE写入前像所有权未知");
        }
        string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp",backup=temp+".previous";bool committed=false,confirmed=false;
        try{using(var file=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){file.Write(bytes,0,bytes.Length);file.Flush(true);}
            for(int i=0;;i++){
                byte[] current=null;try{current=AtomicSource(path);}catch(FileNotFoundException){}
                if(!SameBytes(current,expected))throw new InvalidDataException("CE写入前像已更替，未覆盖新原件");
                try{if(expected!=null){if(File.Exists(backup)||Directory.Exists(backup))throw new InvalidDataException("CE前像归档路径未知");File.Replace(temp,path,backup);}else File.Move(temp,path);committed=true;break;}catch(IOException){if(i>=5)throw;Thread.Sleep(10);}
            }
            if(expected!=null&&!SameBytes(AtomicSource(backup),expected)){
                if(SameBytes(AtomicSource(path),bytes))File.Replace(backup,path,backup+".rollback");
                throw new InvalidDataException("CE并发原件已保留，未确认写入");
            }
            if(!SameBytes(AtomicSource(path),bytes))throw new IOException("CE会话写后回读不一致");confirmed=true;
        }finally{if(File.Exists(temp))File.Delete(temp);if(committed&&confirmed&&File.Exists(backup))File.Delete(backup);}
    }
    void Save(string path,object value){
        var document=value as Dictionary<string,object>;if(document==null)throw new InvalidDataException("CE会话写入根无效");Supported(document);
        if(File.Exists(path))Read(path,131072); // An unknown/future preimage is not write permission.
        var next=new Dictionary<string,object>(document);next["schemaVersion"]=1;string serialized=json.Serialize(next);if(new UTF8Encoding(false,true).GetByteCount(serialized)>131072)throw new InvalidDataException("CE JSON结果超过消费限制");Atomic(path,serialized);
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
    string ClaimRequest(string request){NoLinks(request);if(Key(Path.GetDirectoryName(request))!=Key(Path.Combine(root,"requests"))||!Hex(Path.GetFileNameWithoutExtension(request),32))throw new InvalidDataException("CE请求路径无效");string claim=request+"."+Guid.NewGuid().ToString("N")+".claim";File.Move(request,claim);return claim;}
    void FinishClaim(string claim,bool success){if(String.IsNullOrEmpty(claim))return;NoLinks(claim);if(success)File.Delete(claim);else if(File.Exists(claim))File.Move(claim,claim+".rejected");}

    internal string ResolveCE(string settingsFile){
        if(!File.Exists(settingsFile))return "";
        var settings=Read(settingsFile,65536);string selected=Environment.ExpandEnvironmentVariables(Text(settings,"cePath").Trim().Trim('"'));
        if(!Path.IsPathRooted(selected))return "";
        if(Directory.Exists(selected)){
            // Bypass the architecture/setup launcher; never install or download CE.
            foreach(string name in new[]{"cheatengine-x86_64.exe","cheatengine-x86_64-SSE4-AVX2.exe"}){string file=Path.Combine(selected,name);if(File.Exists(file))return Path.GetFullPath(file);}return "";
        }
        if(Path.GetExtension(selected).ToLowerInvariant()!=".exe"||!File.Exists(selected))return "";
        if(Path.GetFileName(selected).Equals("Cheat Engine.exe",StringComparison.OrdinalIgnoreCase)){string file=Path.Combine(Path.GetDirectoryName(selected),"cheatengine-x86_64.exe");return File.Exists(file)?file:"";}
        return Path.GetFullPath(selected);
    }
    static string GcmSettings(){return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),"GCM Settings","settings.json");}
    void AssertIdentity(IntPtr handle,string birth,string path){long b,e,k,u;var image=new StringBuilder(32768);int n=image.Capacity;if(handle==IntPtr.Zero||WaitForSingleObject(handle,0)!=258||!GetProcessTimes(handle,out b,out e,out k,out u)||b.ToString(Inv)!=birth||!QueryFullProcessImageName(handle,0,image,ref n)||Key(image.ToString())!=Key(path))throw new InvalidOperationException("锁定进程已退出或改变");}
    void Guard(){
        if(File.Exists(Path.Combine(root,"stop")))throw new OperationCanceledException("CE会话已结束");
        var lease=Read(Path.Combine(root,"lease.json"),4096);ValidateLease(lease);long age;
        AssertIdentity(ownerHandle,ownerBirth,ownerPath);AssertIdentity(gameHandle,created,imagePath);
        var target=Read(snapshotPath,65536);age=Now()-Num(target,"lastSeen");if((!target.ContainsKey("valid")||!(target["valid"] is bool)||!(bool)target["valid"])||age<0||age>10000||Num(target,"generation")<=0||Num(target,"pid")!=processId||Text(target,"processCreated")!=created||Key(Text(target,"path"))!=Key(imagePath))throw new InvalidOperationException("YMCC锁定游戏已改变，CE搜索已取消");
    }
    void Open(string session){
        root=Path.GetFullPath(session);if(!Hex(Path.GetFileName(root),32))throw new InvalidOperationException("CE会话目录无效");NoLinks(root);
        snapshotPath=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","game-target.json"));NoLinks(snapshotPath);
        var config=Read(Path.Combine(root,"config.json"),16384);ValidateSessionConfig(config);token=Text(config,"token");processId=(int)Num(config,"pid");created=Text(config,"processCreated");imagePath=Text(config,"path");
        int ownerId=(int)Num(config,"ownerPid");uint own,other;if(!Hex(token,64)||processId<=4||ownerId<=4||ownerId==processId||!Path.IsPathRooted(imagePath)||!ProcessIdToSessionId(Process.GetCurrentProcess().Id,out own)||!ProcessIdToSessionId(processId,out other)||own!=other||!ProcessIdToSessionId(ownerId,out other)||own!=other)throw new InvalidOperationException("CE目标身份无效");
        gameHandle=OpenProcess(IdentityAccess,false,processId);ownerHandle=OpenProcess(IdentityAccess,false,ownerId);
        long b,e,k,u;var image=new StringBuilder(32768);int count=image.Capacity;if(ownerHandle==IntPtr.Zero||!GetProcessTimes(ownerHandle,out b,out e,out k,out u)||!QueryFullProcessImageName(ownerHandle,0,image,ref count))throw new InvalidOperationException("无法核验YMCC进程");ownerBirth=b.ToString(Inv);ownerPath=image.ToString();
        if((Text(config,"ownerPath")!=""&&Key(Text(config,"ownerPath"))!=Key(ownerPath))||(Text(config,"ownerCreated")!=""&&Text(config,"ownerCreated")!=ownerBirth)||(Text(config,"ownerDirectory")!=""&&Key(Text(config,"ownerDirectory"))!=Key(Path.GetDirectoryName(ownerPath))))throw new InvalidOperationException("YMCC会话身份已改变");
        state=Obj("stage","idle","message","正在连接后台CE","count",0,"rows",new object[0],"scanRound",0,"searchValue","","lastWriteCount",0,"lastWriteValue","","scannedBytes",0,"totalBytes",0,"scanElapsedMs",0,"temporarilyPaused",false,"pauseRestored",true,"dataType","int32");Guard();
    }
    void Publish(bool force){if(state==null)return;if(!force&&(DateTime.UtcNow-lastPublish).TotalMilliseconds<300)return;var result=new Dictionary<string,object>(state);result["token"]=token;result["at"]=Now();result["engine"]="cheat-engine";result["pid"]=processId;result["processCreated"]=created;result["path"]=imagePath;result["revision"]=revision;result["offset"]=0;result["pageSize"]=40;result["selected"]="";result["selectedValue"]=null;result["frozen"]=false;result["frozenValue"]=null;result["skippedBlocks"]=0;result["bulkLimit"]=32;Save(Path.Combine(root,"status.json"),result);lastPublish=DateTime.UtcNow;}
    void Heartbeat(){Atomic(Path.Combine(root,"ce-lease.txt"),token+"\n"+(++heartbeat).ToString(Inv));}
    void LaunchCE(string executable){
        // Read only: never dismiss first-run setup by rewriting CE's registry flag.
        using(var settings=Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Cheat Engine",false)){
            object first=settings==null?null:settings.GetValue("First Time User");
            if(first==null||Convert.ToInt32(first,Inv)!=0)throw new InvalidOperationException("请先在CE中完成首次启动设置，再使用简单改数值");
        }
        string scriptFile=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"CeScanEngine.lua");NoLinks(scriptFile);if(!File.Exists(scriptFile))throw new InvalidOperationException("缺少后台CE连接脚本");
        string lua="YMCC_CE_SESSION={root="+Lua(root)+",token="+Lua(token)+",pid="+processId.ToString(Inv)+"}\n"+File.ReadAllText(scriptFile,Encoding.UTF8);
        string trainer=Path.Combine(root,"YMCC-Session.cetrainer");Atomic(trainer,"<?xml version=\"1.0\" encoding=\"utf-8\"?><CheatTable CheatEngineTableVersion=\"45\"><LuaScript>"+SecurityElement.Escape(lua)+"</LuaScript></CheatTable>");
        Heartbeat();var info=new ProcessStartInfo(executable,"NOAUTORUN \""+trainer+"\""){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,WorkingDirectory=Path.GetDirectoryName(executable)};ce=Process.Start(info);RestoreCeScheduling(ce);Save(Path.Combine(root,"ce-process.json"),Obj("pid",ce.Id,"path",executable,"processCreated",ce.StartTime.ToUniversalTime().ToFileTimeUtc().ToString(Inv)));
        var wait=Stopwatch.StartNew();while(wait.ElapsedMilliseconds<20000){Guard();Heartbeat();if(File.Exists(Path.Combine(root,"ce-state.json"))){try{state=Read(Path.Combine(root,"ce-state.json"),131072);ValidateCeState(state);lastCeSeen=DateTime.UtcNow;if(Text(state,"token")==token){if(Text(state,"stage")=="closed")throw new InvalidOperationException(Text(state,"message"));return;}}catch(FileNotFoundException){}}if(ce.HasExited)throw new InvalidOperationException("后台CE未启动，请检查CE安装");Thread.Sleep(100);}throw new InvalidOperationException("后台CE未就绪，已取消操作");
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
    bool Refresh(){
        try{var next=Read(Path.Combine(root,"ce-state.json"),131072);ValidateCeState(next);state=next;lastCeSeen=DateTime.UtcNow;return true;}
        catch(FileNotFoundException){if((DateTime.UtcNow-lastCeSeen).TotalMilliseconds<=8000)return false;state=ClosedState();closing=true;throw;}
        catch(DirectoryNotFoundException){state=ClosedState();closing=true;throw;}
        catch{state=ClosedState();closing=true;throw;}
    }
    void Command(string requestFile){string claim=null;string id=Path.GetFileNameWithoutExtension(requestFile);if(!Hex(id,32))return;bool ok=false;string error="";try{
        Guard();if(File.Exists(Path.Combine(root,"responses",id+".json")))throw new InvalidDataException("CE请求已存在回执，拒绝重放");claim=ClaimRequest(requestFile);var request=Read(claim,16384);ValidateRequest(request,id);if(Text(request,"id")!=id||Text(request,"token")!=token)throw new InvalidOperationException("CE请求身份无效");string action=Text(request,"action"),value=Text(request,"value");if(action!="first"&&Num(request,"revision")!=revision)throw new InvalidOperationException("搜索结果已更新，请重试");if(action!="first"&&action!="next"&&action!="writeAll")throw new InvalidOperationException("该功能只接受CE搜索和确认修改");int number;if(value.Length>12||!Int32.TryParse(value,NumberStyles.Integer,Inv,out number))throw new InvalidOperationException("请输入4字节整数范围内的数字");
        if(!Refresh())throw new InvalidDataException("CE状态未确认，未派发操作");
        Atomic(Path.Combine(root,"ce-command.txt"),id+"\n"+token+"\n"+action+"\n"+value+"\n");var wait=Stopwatch.StartNew();
        while(wait.ElapsedMilliseconds<600000){Guard();Heartbeat();if(ce.HasExited)throw new InvalidOperationException("后台CE已退出");Refresh();Publish(false);string stage=Text(state,"stage");if(Text(state,"commandId")==id&&stage!="scanning"&&stage!="writing"){revision++;if(stage=="failed"||stage=="closed")throw new InvalidOperationException(Text(state,"message"));ok=true;break;}Thread.Sleep(100);}
        if(!ok)throw new InvalidOperationException("后台CE长时间未返回，请取消搜索");
    }catch(Exception ex){error=ex.Message;if(claim==null||ex is IOException||ex is FormatException||Text(state,"stage")=="scanning"||ex is OperationCanceledException){Atomic(Path.Combine(root,"ce-stop"),"stop");closing=true;}state["message"]=error;state["stage"]=closing?"closed":"failed";}
    finally{try{FinishClaim(claim,ok);}catch{}Publish(true);if(!File.Exists(Path.Combine(root,"responses",id+".json")))Save(Path.Combine(root,"responses",id+".json"),Obj("id",id,"token",token,"ok",ok,"error",error,"state",Read(Path.Combine(root,"status.json"),131072)));}}
    void Run(string session){Open(session);string executable=ResolveCE(GcmSettings());if(executable=="")throw new InvalidOperationException("未安装CE，简单改数值功能不可用");LaunchCE(executable);Publish(true);while(!closing){Guard();Heartbeat();if(ce.HasExited)throw new InvalidOperationException("后台CE已退出");Refresh();string[] requests=Directory.GetFiles(Path.Combine(root,"requests"),"*.json");Array.Sort(requests,StringComparer.Ordinal);if(requests.Length>0)Command(requests[0]);Publish(false);Thread.Sleep(100);}}
    void Shutdown(){if(root!=null){try{Atomic(Path.Combine(root,"ce-stop"),"stop");}catch{}}if(ce!=null){try{ce.WaitForExit(10000);}catch{}ce.Dispose();}if(gameHandle!=IntPtr.Zero)CloseHandle(gameHandle);if(ownerHandle!=IntPtr.Zero)CloseHandle(ownerHandle);}
    [STAThread]static int Main(string[] args){YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();var host=new MemoryScanHost();try{
        if(args.Length==2&&args[0]=="--probe"){string executable=host.ResolveCE(GcmSettings());host.Save(Path.GetFullPath(args[1]),Obj("engine","cheat-engine","available",executable!="","executable",executable));return 0;}
        if(args.Length!=1)return 2;host.Run(args[0]);return 0;
    }catch(Exception ex){try{if(host.state!=null){host.state["stage"]="closed";host.state["message"]=ex.Message;host.Publish(true);}}catch{}return 1;}finally{host.Shutdown();}}
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
