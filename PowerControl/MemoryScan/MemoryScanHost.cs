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
    DateTime lastPublish=DateTime.MinValue;bool closing;long heartbeat;
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int id);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h,uint ms);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref int count);
    [DllImport("kernel32.dll")] static extern bool ProcessIdToSessionId(int pid,out uint session);
    static string Key(string value){return Path.GetFullPath(value).TrimEnd('\\').ToLowerInvariant();}
    static long Now(){return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();}
    static string Text(Dictionary<string,object> o,string key){object x;return o!=null&&o.TryGetValue(key,out x)&&x!=null?Convert.ToString(x,Inv):"";}
    static long Num(Dictionary<string,object> o,string key){long n;return long.TryParse(Text(o,key),NumberStyles.Integer,Inv,out n)?n:0;}
    static bool Hex(string x,int length){if(x.Length!=length)return false;foreach(char c in x)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
    static Dictionary<string,object> Obj(params object[] pairs){var x=new Dictionary<string,object>();for(int i=0;i<pairs.Length;i+=2)x[(string)pairs[i]]=pairs[i+1];return x;}
    static void NoLinks(string path){for(string p=Path.GetFullPath(path);!String.IsNullOrEmpty(p);p=Path.GetDirectoryName(p))if((File.Exists(p)||Directory.Exists(p))&&(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("CE会话路径不能经过链接");}
    Dictionary<string,object> Read(string path,int limit){using(var f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){if(f.Length>limit)throw new InvalidOperationException("CE会话文件超限");using(var r=new StreamReader(f,Encoding.UTF8))return json.Deserialize<Dictionary<string,object>>(r.ReadToEnd());}}
    static void Atomic(string path,string data){string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";try{File.WriteAllText(temp,data,new UTF8Encoding(false));for(int i=0;;i++){try{if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);break;}catch(IOException){if(i>=5)throw;Thread.Sleep(10);}}}finally{if(File.Exists(temp))File.Delete(temp);}}
    void Save(string path,object value){Atomic(path,json.Serialize(value));}
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
        var lease=Read(Path.Combine(root,"lease.json"),4096);long age=Now()-Num(lease,"at");if(Text(lease,"token")!=token||age<0||age>8000)throw new InvalidOperationException("YMCC连接中断，CE搜索已取消");
        AssertIdentity(ownerHandle,ownerBirth,ownerPath);AssertIdentity(gameHandle,created,imagePath);
        var target=Read(snapshotPath,65536);age=Now()-Num(target,"lastSeen");if(Text(target,"valid").ToLowerInvariant()!="true"||age<0||age>10000||Num(target,"generation")<=0||Num(target,"pid")!=processId||Text(target,"processCreated")!=created||Key(Text(target,"path"))!=Key(imagePath))throw new InvalidOperationException("YMCC锁定游戏已改变，CE搜索已取消");
    }
    void Open(string session){
        root=Path.GetFullPath(session);if(!Hex(Path.GetFileName(root),32))throw new InvalidOperationException("CE会话目录无效");NoLinks(root);
        snapshotPath=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","game-target.json"));NoLinks(snapshotPath);
        var config=Read(Path.Combine(root,"config.json"),16384);token=Text(config,"token");processId=(int)Num(config,"pid");created=Text(config,"processCreated");imagePath=Text(config,"path");
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
        var wait=Stopwatch.StartNew();while(wait.ElapsedMilliseconds<20000){Guard();Heartbeat();if(File.Exists(Path.Combine(root,"ce-state.json"))){try{state=Read(Path.Combine(root,"ce-state.json"),131072);if(Text(state,"token")==token){if(Text(state,"stage")=="closed")throw new InvalidOperationException(Text(state,"message"));return;}}catch(IOException){}catch(ArgumentException){}}if(ce.HasExited)throw new InvalidOperationException("后台CE未启动，请检查CE安装");Thread.Sleep(100);}throw new InvalidOperationException("后台CE未就绪，已取消操作");
    }
    static string Lua(string value){return "\""+value.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r","\\r").Replace("\n","\\n")+"\"";}
    bool Refresh(){try{var next=Read(Path.Combine(root,"ce-state.json"),131072);if(Text(next,"token")!=token)throw new InvalidOperationException("CE回执身份无效");state=next;return true;}catch(IOException){return false;}catch(ArgumentException){return false;}}
    void Command(string requestFile){string id=Path.GetFileNameWithoutExtension(requestFile);if(!Hex(id,32))return;bool ok=false;string error="";try{
        Guard();NoLinks(requestFile);var request=Read(requestFile,16384);if(Text(request,"id")!=id||Text(request,"token")!=token)throw new InvalidOperationException("CE请求身份无效");string action=Text(request,"action"),value=Text(request,"value");if(action!="first"&&Num(request,"revision")!=revision)throw new InvalidOperationException("搜索结果已更新，请重试");if(action!="first"&&action!="next"&&action!="writeAll")throw new InvalidOperationException("该功能只接受CE搜索和确认修改");int number;if(value.Length>12||!Int32.TryParse(value,NumberStyles.Integer,Inv,out number))throw new InvalidOperationException("请输入4字节整数范围内的数字");
        Atomic(Path.Combine(root,"ce-command.txt"),id+"\n"+token+"\n"+action+"\n"+value+"\n");var wait=Stopwatch.StartNew();
        while(wait.ElapsedMilliseconds<600000){Guard();Heartbeat();if(ce.HasExited)throw new InvalidOperationException("后台CE已退出");Refresh();Publish(false);string stage=Text(state,"stage");if(Text(state,"commandId")==id&&stage!="scanning"&&stage!="writing"){revision++;if(stage=="failed"||stage=="closed")throw new InvalidOperationException(Text(state,"message"));ok=true;break;}Thread.Sleep(100);}
        if(!ok)throw new InvalidOperationException("后台CE长时间未返回，请取消搜索");
    }catch(Exception ex){error=ex.Message;if(Text(state,"stage")=="scanning"||ex is OperationCanceledException){Atomic(Path.Combine(root,"ce-stop"),"stop");closing=true;}state["message"]=error;state["stage"]=closing?"closed":"failed";}
    finally{try{File.Delete(requestFile);}catch{}Publish(true);Save(Path.Combine(root,"responses",id+".json"),Obj("id",id,"token",token,"ok",ok,"error",error,"state",Read(Path.Combine(root,"status.json"),131072)));}}
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
