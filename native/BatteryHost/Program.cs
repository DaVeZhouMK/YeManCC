using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;
namespace YeManBatteryHost;
internal static class Program
{
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] static extern bool GetNamedPipeClientProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe,out uint pid);
    [STAThread] static int Main(string[] args)
    {
        YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();
        try
        {
            if(args.Length==1&&args[0]=="--identity"){Console.WriteLine(JsonSerializer.Serialize(RuntimeContract.Identity(),Policy.Json));return 0;}
            string Arg(string key,string fallback=""){int n=Array.IndexOf(args,key);return n>=0&&n+1<args.Length?args[n+1]:fallback;}
            var powerRoot=Path.GetFullPath(Arg("--power-root",Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,".."))));
            var stateDir=Path.GetFullPath(Arg("--state-dir",Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"YeManCC")));
            TraceLog.Configure(stateDir);
            TraceLog.Write("host-start",new {version=typeof(Program).Assembly.ManifestModule.ModuleVersionId,runtime=Environment.Version.ToString(),probe=args.Contains("--probe"),contract=RuntimeContract.Identity(),powerRoot,stateDir},true);
            if(args.Contains("--probe"))
            {
                var identity=WindowsFacts.Identity();var engine=new Controller(identity,new Store(stateDir),route=>Providers.Open(route,powerRoot));
                Console.WriteLine(JsonSerializer.Serialize(engine.Read(),Policy.Json));return 0;
            }
            int parentId=int.Parse(Arg("--parent"));long parentTime=long.Parse(Arg("--parent-start"));
            using var parent=Process.GetProcessById(parentId);
            if(parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=parentTime||parent.HasExited)throw new IOException("父进程身份已变化");
            using var single=new Mutex(false,@"Global\YMCC.BatteryCharge.Owner");
            bool held;try{held=single.WaitOne(0);}catch(AbandonedMutexException){held=true;}
            if(!held){TraceLog.Write("owner-busy",new {parentId},true);return 3;}
            try{Serve(parent,parentTime,powerRoot,stateDir).GetAwaiter().GetResult();}finally{single.ReleaseMutex();}
            return 0;
        }
        catch(Exception e){TraceLog.Error("host-fatal",e);Console.Error.WriteLine("BatteryHost: "+e.Message);return 1;}
    }
    static async Task Serve(Process parent,long parentTime,string powerRoot,string stateDir)
    {
        var identity=WindowsFacts.Identity();
        TraceLog.Write("identity",new {identity.Manufacturer,identity.BoardProduct,identity.SystemProduct,identity.Processor,identity.Bios,identity.EcMajor,identity.EcMinor,route=Policy.Resolve(identity)?.RouteId},true);
        var domains=new HostDomainRouter(
            ()=>new Controller(identity,new Store(stateDir),route=>Providers.Open(route,powerRoot)),
            ready=>new SecondaryDisplayController(identity,new Store(stateDir),ready:ready),()=>!parent.HasExited);
        using var stop=new CancellationTokenSource();
        PowerModeChangedEventHandler power=(_,e)=>{TraceLog.Write("power",new {mode=e.Mode.ToString()},true);if(e.Mode==PowerModes.Suspend)domains.Suspend();else if(e.Mode==PowerModes.Resume)domains.Resume();};
        SessionEndingEventHandler ending=(_,_)=>{domains.Shutdown();stop.Cancel();};
        SystemEvents.PowerModeChanged+=power;SystemEvents.SessionEnding+=ending;
        var monitor=Task.Run(async()=>
        {
            try
            {
                while(!stop.IsCancellationRequested)
                {
                    if(parent.HasExited){stop.Cancel();break;}
                    domains.Tick();await Task.Delay(2000,stop.Token);
                }
            }
            catch(OperationCanceledException){}
            catch(Exception e){TraceLog.Error("monitor-fatal",e);stop.Cancel();}
        });
        try
        {
            // Domains are lazy: a display request must never recover or create charge policy.
            var pipeName=$"YMCC.BatteryCharge.{parent.Id}.{parentTime}";
            while(!stop.IsCancellationRequested)
            {
                await using var pipe=new NamedPipeServerStream(pipeName,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous|PipeOptions.CurrentUserOnly);
                try
                {
                    TraceLog.Write("pipe-ready",new {parentId=parent.Id});
                    await pipe.WaitForConnectionAsync(stop.Token);
                    if(!GetNamedPipeClientProcessId(pipe.SafePipeHandle,out var pid)||pid!=parent.Id)continue;
                    using var requestTimeout=CancellationTokenSource.CreateLinkedTokenSource(stop.Token);requestTimeout.CancelAfter(TimeSpan.FromSeconds(18));
                    var line=new List<byte>();var one=new byte[1];
                    while(line.Count<4096)
                    {
                        if(await pipe.ReadAsync(one,requestTimeout.Token)!=1)break;
                        if(one[0]==10)break;line.Add(one[0]);
                    }
                    if(line.Count==4096)throw new IOException("电池请求过大");
                    using var json=JsonDocument.Parse(line.ToArray());var root=json.RootElement;
                    string op=root.GetProperty("op").GetString()??"";
                    if(op=="hello")
                    {
                        var hello=Encoding.UTF8.GetBytes(JsonSerializer.Serialize(RuntimeContract.Identity(),Policy.Json)+"\n");
                        await pipe.WriteAsync(hello,requestTimeout.Token);await pipe.FlushAsync(requestTimeout.Token);continue;
                    }
                    // Check before any requested read, write, release or startup replay.
                    RuntimeContract.Validate(root);
                    TraceLog.Write("request-start",new {operation=op,target=root.TryGetProperty("target",out var target)?target.ToString():null},op!="get");
                    object result=domains.Dispatch(root);
                    if(result is Snapshot batteryResult)TraceLog.Result(op,batteryResult);
                    else if(result is SecondaryDisplaySnapshot displayResult)TraceLog.SecondaryDisplayResult(op,displayResult);
                    var response=JsonSerializer.SerializeToUtf8Bytes(result,Policy.Json).Concat(new byte[]{10}).ToArray();
                    await pipe.WriteAsync(response,requestTimeout.Token);await pipe.FlushAsync(requestTimeout.Token);
                }
                catch(OperationCanceledException){if(!stop.IsCancellationRequested)TraceLog.Write("request-timeout",new {timeoutSeconds=18},true);}
                catch(Exception e)
                {
                    TraceLog.Error("request-error",e);
                    if(pipe.IsConnected)
                    {
                        try{var data=Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new {error=e.Message},Policy.Json)+"\n");await pipe.WriteAsync(data,stop.Token);}catch{}
                    }
                }
            }
        }
        finally
        {
            stop.Cancel();await monitor;domains.Shutdown();SystemEvents.PowerModeChanged-=power;SystemEvents.SessionEnding-=ending;
            TraceLog.Write("host-stop",new {parentExited=parent.HasExited},true);
        }
    }
    // Keep the existing battery operation bodies shared with the exact-source simulator.
    internal static Snapshot DispatchBattery(Controller controller,JsonElement root)
    {
        string op=root.GetProperty("op").GetString()??"";
        Snapshot result=op switch
        {
            "get"=>controller.Read(),"refresh"=>controller.Acknowledge(),"release"=>controller.Release(),
            "set"=>controller.Apply(root.GetProperty("fence").GetString()??"",root.GetProperty("target").GetInt32()),
            "restore"=>Restore(controller),_=>throw new IOException("未知电池操作")
        };
        return result;
    }
    static Snapshot Restore(Controller controller){controller.StartSaved();return controller.Read();}
}
