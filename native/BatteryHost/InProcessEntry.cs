using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Win32;
namespace YeManBatteryHost;

// hostfxr loads this library in YMCC. The caller owns the input and frees each
// successful output exactly once with Free, including JSON error responses.
public static unsafe class InProcessEntry
{
    internal const int MaxRequestBytes=64*1024;
    static readonly InProcessRuntime runtime=new();

    [UnmanagedCallersOnly(CallConvs=new[]{typeof(CallConvCdecl)})]
    public static int Invoke(byte* input,int length,byte** output,int* outputLength)
    {
        if(output!=null)*output=null;
        if(outputLength!=null)*outputLength=0;
        if(output==null||outputLength==null||input==null||length<=0||length>MaxRequestBytes)return 1;
        nint memory=0;
        try
        {
            var response=runtime.Invoke(new ReadOnlySpan<byte>(input,length).ToArray());
            memory=Marshal.AllocHGlobal(response.Length);
            Marshal.Copy(response,0,memory,response.Length);
            *output=(byte*)memory;*outputLength=response.Length;
            return 0;
        }
        catch
        {
            try{if(memory!=0)Marshal.FreeHGlobal(memory);}catch{}
            return 2;
        }
    }

    [UnmanagedCallersOnly(CallConvs=new[]{typeof(CallConvCdecl)})]
    public static void Free(byte* memory)
    {
        try{if(memory!=null)Marshal.FreeHGlobal((nint)memory);}catch{ /* No managed exception may cross the ABI. */ }
    }
}

// Injection uses the real router/controllers; fixtures replace only facts,
// transports, storage, scheduling and event registration. Never poll a parent.
internal sealed class InProcessRuntime
{
    internal static readonly TimeSpan PollingInterval=TimeSpan.FromSeconds(60);
    internal static readonly TimeSpan ShutdownTimeout=TimeSpan.FromMilliseconds(1500);
    readonly object stateGate=new(),operationGate=new();
    readonly Func<string,string,Func<bool>,HostDomainRouter> createDomains;
    readonly Func<Action,TimeSpan,IDisposable> schedule;
    readonly Func<Action<PowerModes>,Action,IDisposable> subscribe;
    readonly TimeSpan shutdownTimeout;
    readonly Func<IDisposable> acquireOwner;
    IDisposable? owner;
    string? powerRoot,stateRoot;
    HostDomainRouter? domains;
    IDisposable? timer,events;
    Task? cleanup;
    string? cleanupError;
    volatile bool stopped,suspended;

    internal InProcessRuntime(Func<string,string,Func<bool>,HostDomainRouter>? createDomains=null,
        Func<Action,TimeSpan,IDisposable>? schedule=null,
        Func<Action<PowerModes>,Action,IDisposable>? subscribe=null,TimeSpan? shutdownTimeout=null,
        Func<IDisposable>? acquireOwner=null)
    {
        this.createDomains=createDomains??CreateDomains;
        this.acquireOwner=acquireOwner??(()=>InProcessOwnerLease.Acquire());
        this.schedule=schedule??((tick,interval)=>new System.Threading.Timer(_=>tick(),null,interval,interval));
        this.subscribe=subscribe??((power,ending)=>new PowerSubscription(power,ending));
        this.shutdownTimeout=shutdownTimeout??ShutdownTimeout;
        if(this.shutdownTimeout<TimeSpan.Zero||this.shutdownTimeout>ShutdownTimeout)throw new ArgumentOutOfRangeException(nameof(shutdownTimeout));
    }

    static HostDomainRouter CreateDomains(string powerRoot,string stateRoot,Func<bool> ready)
    {
        TraceLog.Configure(stateRoot);
        var identity=WindowsFacts.Identity();
        TraceLog.Write("in-process-start",new {identity.Manufacturer,identity.BoardProduct,identity.SystemProduct,
            powerRoot,stateRoot,pollingIntervalMs=(int)PollingInterval.TotalMilliseconds},true);
        return new HostDomainRouter(
            ()=>new Controller(identity,new Store(stateRoot),route=>Providers.Open(route,powerRoot)),
            domainReady=>new SecondaryDisplayController(identity,new Store(stateRoot),ready:domainReady),ready);
    }

    internal byte[] Invoke(ReadOnlyMemory<byte> input)
    {
        try
        {
            using var document=JsonDocument.Parse(input);
            return JsonSerializer.SerializeToUtf8Bytes(Dispatch(document.RootElement),Policy.Json);
        }
        catch(Exception error)
        {
            TraceLog.Error("in-process-request-error",error);
            return JsonSerializer.SerializeToUtf8Bytes(new {error=error.Message},Policy.Json);
        }
    }

    static object Hello()
    {
        var identity=JsonSerializer.SerializeToElement(RuntimeContract.Identity(),Policy.Json);
        var response=identity.EnumerateObject().ToDictionary(p=>p.Name,p=>(object?)p.Value);
        response["execution"]="in-process";
        response["pollingIntervalMs"]=(int)PollingInterval.TotalMilliseconds;
        response["processId"]=Environment.ProcessId;
        return response;
    }
    static string Root(JsonElement envelope,string name)
    {
        var path=envelope.GetProperty(name).GetString();
        if(string.IsNullOrWhiteSpace(path)||!Path.IsPathFullyQualified(path))throw new IOException(name+" 必须是绝对路径");
        return Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
    }
    void CheckRoots(string power,string state)
    {
        if(powerRoot is not null&&(!string.Equals(powerRoot,power,StringComparison.OrdinalIgnoreCase)||
            !string.Equals(stateRoot,state,StringComparison.OrdinalIgnoreCase)))throw new IOException("进程内充电组件已绑定其他根目录，不能切换会话");
    }
    void RequireRunning(){if(stopped)throw new IOException("进程内充电组件已关闭");}

    object Dispatch(JsonElement envelope)
    {
        var request=envelope.GetProperty("request");
        string op=request.GetProperty("op").GetString()??"";
        if(op=="hello")return Hello(); // No roots, device facts, stores, timers or event subscriptions.
        if(op=="shutdown")return Shutdown(); // Terminal cleanup also works with a request-only envelope.
        RuntimeContract.Validate(request);
        if(op is not ("get" or "refresh" or "release" or "set" or "restore" or
            "secondaryDisplay.get" or "secondaryDisplay.set" or "secondaryDisplay.toggle" or "secondaryDisplay.release"))throw new IOException("未知进程内操作");
        string power=Root(envelope,"powerRoot"),state=Root(envelope,"stateRoot");
        lock(stateGate){RequireRunning();CheckRoots(power,state);powerRoot??=power;stateRoot??=state;}
        lock(operationGate)
        {
            RequireRunning();
            // WindowsFacts/WMI and all domain locks stay outside stateGate so
            // shutdown can seal immediately even if an operation never returns.
            owner??=acquireOwner();
            RequireRunning();
            domains??=createDomains(power,state,()=>!stopped&&!suspended);
            RequireRunning();
            StartMonitoring();
            RequireRunning();
            var result=domains.Dispatch(request);
            if(result is Snapshot battery)TraceLog.Result(op,battery);
            else if(result is SecondaryDisplaySnapshot display)TraceLog.SecondaryDisplayResult(op,display);
            return result;
        }
    }

    void StartMonitoring()
    {
        lock(stateGate){RequireRunning();if(timer is not null)return;}
        var registration=subscribe(PowerChanged,()=>{_ = Shutdown();});
        IDisposable? scheduled=null;
        try
        {
            RequireRunning();scheduled=schedule(Tick,PollingInterval);
            lock(stateGate)
            {
                RequireRunning();events=registration;timer=scheduled;
            }
        }
        catch
        {
            try{scheduled?.Dispose();}finally{registration.Dispose();}
            throw;
        }
    }
    internal void Tick()
    {
        // Do not queue overlapping/late timer callbacks behind slow WMI.
        if(stopped||suspended||!Monitor.TryEnter(operationGate))return;
        try{if(!stopped&&!suspended)domains?.Tick();}
        catch(Exception error){TraceLog.Error("in-process-tick-error",error);}
        finally{Monitor.Exit(operationGate);}
    }
    internal void PowerChanged(PowerModes mode)
    {
        if(stopped)return;
        if(mode==PowerModes.Suspend)suspended=true;
        try
        {
            lock(operationGate)
            {
                if(stopped)return;
                if(mode==PowerModes.Suspend)domains?.Suspend();
                else if(mode==PowerModes.Resume){suspended=false;domains?.Resume();}
            }
        }
        catch(Exception error){TraceLog.Error("in-process-power-error",error);}
    }

    internal object Shutdown()
    {
        Task pending;
        lock(stateGate)
        {
            stopped=true;suspended=true;
            pending=cleanup??=Task.Run(Cleanup);
        }
        bool completed=pending.Wait(shutdownTimeout);
        // Controller.Shutdown can retain unresolved restoration debt internally.
        // Its return is never proof of hardware restoration or journal deletion.
        return new {ok=completed&&cleanupError is null,execution="in-process",stopped=true,
            cleanupCompleted=completed,cleanupPending=!completed,cleanupError=completed?cleanupError:null,
            hardwareRestoration="not-confirmed",journalPolicy="preserve-unconfirmed",
            shutdownTimeoutMs=(int)shutdownTimeout.TotalMilliseconds};
    }
    void Cleanup()
    {
        IDisposable? oldTimer,oldEvents;
        lock(stateGate){oldTimer=timer;oldEvents=events;timer=null;events=null;}
        var errors=new List<string>();
        try{oldTimer?.Dispose();}catch(Exception error){errors.Add(error.Message);}
        try{oldEvents?.Dispose();}catch(Exception error){errors.Add(error.Message);}
        // Keep ownership until the actual domain cleanup returns. A timed out
        // caller leaves this worker and the lease alive with recovery debt intact.
        lock(operationGate)
        {
            try{domains?.Shutdown();}catch(Exception error){errors.Add(error.Message);}
            try{owner?.Dispose();}catch(Exception error){errors.Add(error.Message);}
        }
        if(errors.Count!=0)cleanupError=string.Join("；",errors);
        TraceLog.Write("in-process-stop",new {cleanupError,hardwareRestoration="not-confirmed",journalPolicy="preserve-unconfirmed"},true);
    }

    sealed class PowerSubscription : IDisposable
    {
        readonly PowerModeChangedEventHandler power;
        readonly SessionEndingEventHandler ending;
        public PowerSubscription(Action<PowerModes> onPower,Action onEnding)
        {
            power=(_,e)=>onPower(e.Mode);ending=(_,_)=>onEnding();
            SystemEvents.PowerModeChanged+=power;
            try{SystemEvents.SessionEnding+=ending;}catch{SystemEvents.PowerModeChanged-=power;throw;}
        }
        public void Dispose(){try{SystemEvents.PowerModeChanged-=power;}finally{SystemEvents.SessionEnding-=ending;}}
    }
}

// Mutex ownership belongs to this thread from WaitOne through ReleaseMutex.
// The thread sleeps on one stop event; it never probes hardware or a parent PID.
internal sealed class InProcessOwnerLease : IDisposable
{
    // The startup task owns no wait handle. Only the owner thread disposes stop,
    // including late startup after the caller has already timed out.
    readonly TaskCompletionSource<Exception?> ready=new(TaskCreationOptions.RunContinuationsAsynchronously);
    readonly TaskCompletionSource finished=new(TaskCreationOptions.RunContinuationsAsynchronously);
    readonly ManualResetEventSlim stop=new(false);
    readonly object stopGate=new();
    readonly Thread thread;
    readonly TimeSpan releaseTimeout;
    bool stopDisposed;
    Exception? releaseError;
    internal Task Completion=>finished.Task;
    internal InProcessOwnerLease(string name,Action? beforeAcquire=null,TimeSpan? releaseTimeout=null)
    {
        this.releaseTimeout=releaseTimeout??TimeSpan.FromSeconds(2);
        if(this.releaseTimeout<TimeSpan.Zero||this.releaseTimeout>TimeSpan.FromSeconds(2))throw new ArgumentOutOfRangeException(nameof(releaseTimeout));
        thread=new Thread(()=>Run(name,beforeAcquire)){IsBackground=true,Name="YMCC battery owner"};
    }
    internal static InProcessOwnerLease Acquire(string name=@"Global\YMCC.BatteryCharge.Owner")
    {
        var lease=new InProcessOwnerLease(name);
        lease.Start(TimeSpan.FromSeconds(2));
        return lease;
    }
    internal void Start(TimeSpan handshakeTimeout)
    {
        if(handshakeTimeout<TimeSpan.Zero||handshakeTimeout>TimeSpan.FromSeconds(2))throw new ArgumentOutOfRangeException(nameof(handshakeTimeout));
        thread.Start();
        if(!ready.Task.Wait(handshakeTimeout))
        {
            RequestStop();
            throw new IOException("充电组件所有权握手超时，未创建设备域");
        }
        if(ready.Task.Result is { } error)
        {
            Dispose();
            throw new IOException("owner-busy：充电组件所有权不可用，未创建设备域",error);
        }
    }
    void RequestStop(){lock(stopGate){if(!stopDisposed)stop.Set();}}
    void Run(string name,Action? beforeAcquire)
    {
        try
        {
            beforeAcquire?.Invoke();
            if(stop.IsSet)throw new IOException("充电组件所有权启动已取消");
            using var mutex=new Mutex(false,name);
            bool held;
            try{held=mutex.WaitOne(0);}catch(AbandonedMutexException){held=true;}
            if(!held)throw new IOException("已有 YMCC 或旧充电辅助进程持有充电组件");
            try{ready.TrySetResult(null);stop.Wait();}
            finally{try{mutex.ReleaseMutex();}catch(Exception error){releaseError=error;}}
        }
        catch(Exception error){ready.TrySetResult(error);}
        finally
        {
            lock(stopGate){stop.Dispose();stopDisposed=true;}
            finished.TrySetResult();
        }
    }
    public void Dispose()
    {
        RequestStop();
        // A timed-out Join does not mark disposal complete: later callers can
        // retry, while the original thread still owns its eventual self-cleanup.
        if(!thread.Join(releaseTimeout))throw new IOException("充电所有权线程尚未结束，归还未确认");
        if(releaseError is not null)throw new IOException("充电所有权归还未确认",releaseError);
    }
}