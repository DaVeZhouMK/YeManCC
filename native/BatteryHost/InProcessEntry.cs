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
    public static void NotifySystemPower()
    {
        try{runtime.NotifySystemPower();}catch{ /* Notifications never cross the ABI with an exception. */ }
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
    readonly Func<string,string,Func<bool>,Func<(bool Present,bool Ac,int? Percent)>,HostDomainRouter> createDomains;
    readonly NativePowerReader powerReader=new();
    readonly NativeSystemPowerReader systemPowerReader=new();
    readonly bool useSharedSystemPower;
    readonly Action<Action> queueSystemPower;
    sealed record PowerAdmission(ulong Generation,ulong Revision,ulong SuspendSerial,ulong NotificationSequence);
    volatile PowerAdmission? powerAdmission;
    ulong highestSystemRevision,completedSuspendSerial;
    bool pauseCompleted;
    int systemWorkQueued,systemWorkVersion,monitoring;
    readonly object systemWorkGate=new();
    int systemWorkOutstanding,systemWorkRunning;
    internal int SystemPowerWorkOutstanding{get{lock(systemWorkGate)return systemWorkOutstanding;}}
    internal int SystemPowerWorkRunning{get{lock(systemWorkGate)return systemWorkRunning;}}
    bool WaitSystemPowerWork(TimeSpan timeout)
    {
        long until=Environment.TickCount64+(long)timeout.TotalMilliseconds;
        lock(systemWorkGate)
        {
            while(systemWorkOutstanding!=0)
            {
                long left=until-Environment.TickCount64;if(left<=0)return false;
                Monitor.Wait(systemWorkGate,TimeSpan.FromMilliseconds(left));
            }
            return true;
        }
    }
    NativeRequestOwner? requestOwner;
    internal bool RequestOwnerCurrent()=>requestOwner?.Current()??true;
    void RequireRequestOwner(){if(!RequestOwnerCurrent())throw new IOException("BOOT_CHARGE_REQUEST_EXPIRED");}
    bool DomainReady()=>Ready()&&RequestOwnerCurrent();
    readonly bool requiresNativePowerReader;
    readonly Func<Action,TimeSpan,IDisposable> schedule;
    readonly Func<Action<PowerModes>,Action,IDisposable> subscribe;
    readonly TimeSpan shutdownTimeout;
    readonly Func<IDisposable> acquireOwner;
    IDisposable? owner;
    string? powerRoot,stateRoot;
    HostDomainRouter? domains;
    IDisposable? timer,events;
    Task<string?>? cleanup;
    volatile bool stopped,suspended;

    internal InProcessRuntime(Func<string,string,Func<bool>,HostDomainRouter>? createDomains=null,
        Func<Action,TimeSpan,IDisposable>? schedule=null,
        Func<Action<PowerModes>,Action,IDisposable>? subscribe=null,TimeSpan? shutdownTimeout=null,
        Func<IDisposable>? acquireOwner=null,
        Func<string,string,Func<bool>,Func<(bool Present,bool Ac,int? Percent)>,HostDomainRouter>? createPowerDomains=null,
        bool? useSharedSystemPower=null,Action<Action>? queueSystemPower=null)
    {
        // Legacy fixtures replace all facts themselves. Production and shared
        // reader fixtures must bind before owner acquisition or domain creation.
        this.createDomains=createPowerDomains??(createDomains is null?CreateDomains:(power,state,ready,_)=>createDomains(power,state,ready));
        requiresNativePowerReader=createDomains is null||createPowerDomains is not null;
        this.acquireOwner=acquireOwner??(()=>InProcessOwnerLease.Acquire());
        this.schedule=schedule??((tick,interval)=>new System.Threading.Timer(_=>tick(),null,interval,interval));
        this.useSharedSystemPower=useSharedSystemPower??(createDomains is null&&createPowerDomains is null);
        this.queueSystemPower=queueSystemPower??(work=>{if(!ThreadPool.QueueUserWorkItem(static value=>((Action)value!).Invoke(),work))throw new IOException("公共睡眠任务未排入线程池");});
        this.subscribe=subscribe??((_,ending)=>new PowerSubscription(ending));
        this.shutdownTimeout=shutdownTimeout??ShutdownTimeout;
        if(this.shutdownTimeout<TimeSpan.Zero||this.shutdownTimeout>ShutdownTimeout)throw new ArgumentOutOfRangeException(nameof(shutdownTimeout));
    }

    HostDomainRouter CreateDomains(string powerRoot,string stateRoot,Func<bool> ready,
        Func<(bool Present,bool Ac,int? Percent)> power)
    {
        TraceLog.Configure(stateRoot);
        var identity=WindowsFacts.Identity();
        TraceLog.Write("in-process-start",new {identity.Manufacturer,identity.BoardProduct,identity.SystemProduct,identity.Processor,identity.Bios,identity.EcMajor,identity.EcMinor,
            powerRoot,stateRoot,pollingIntervalMs=(int)PollingInterval.TotalMilliseconds},true);
        return new HostDomainRouter(
            ()=>new Controller(identity,new Store(stateRoot,RequestOwnerCurrent),route=>Providers.Open(route,powerRoot),power,ready,RequestOwnerCurrent),
            domainReady=>new SecondaryDisplayController(identity,new Store(stateRoot,RequestOwnerCurrent),ready:domainReady),ready);
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
        var admittedOwner=NativeRequestOwner.Capture(envelope);admittedOwner?.RequireCurrent();
        string power=Root(envelope,"powerRoot"),state=Root(envelope,"stateRoot");
        lock(stateGate)
        {
            RequireRunning();CheckRoots(power,state);powerReader.Bind(envelope,requiresNativePowerReader);
            if(useSharedSystemPower)systemPowerReader.Bind(envelope);
            powerRoot??=power;stateRoot??=state;
        }
        lock(operationGate)
        {
            requestOwner=admittedOwner;
            try
            {
            RequireRequestOwner();RequireRunning();
            // WindowsFacts/WMI and all domain locks stay outside stateGate so
            // shutdown can seal immediately even if an operation never returns.
            ReconcileSystemPower();
            if(useSharedSystemPower&&domains is null&&!Ready())throw new IOException("公共睡眠层尚未就绪，未创建设备域");
            owner??=acquireOwner();
            RequireRunning();
            RequireRequestOwner();
            domains??=createDomains(power,state,DomainReady,powerReader.Read);
            RequireRunning();
            StartMonitoring();
            RequireRunning();
            var result=domains.Dispatch(request);
            RequireRequestOwner();RequireRunning(); // A slow admitted WMI read cannot publish a stopped or expired request.
            if(useSharedSystemPower&&!Ready())throw new IOException("公共睡眠代次已变化，旧请求结果未发布");
            if(result is Snapshot battery)TraceLog.Result(op,battery);
            else if(result is SecondaryDisplaySnapshot display)TraceLog.SecondaryDisplayResult(op,display);
            return result;
            }
            finally{requestOwner=null;} // Cleared under operationGate before native Invoke returns.
        }
    }

    void StartMonitoring()
    {
        lock(stateGate)
        {
            RequireRunning();if(timer is not null)return;
            if(events is not null)throw new IOException("原事件注册尚未归还，不能创建新监控代次");
        }
        // Own each returned registration before any later operation can fail.
        var registration=subscribe(PowerChanged,()=>{_ = Shutdown();});
        lock(stateGate){events=registration;}
        try
        {
            RequireRunning();var scheduled=schedule(Tick,PollingInterval);
            lock(stateGate){timer=scheduled;RequireRunning();Volatile.Write(ref monitoring,1);}
            if(useSharedSystemPower)NotifySystemPower();
        }
        catch
        {
            _=CloseRegistrations(); // Failed objects remain exact cleanup debt.
            throw;
        }
    }
    List<string> CloseRegistrations()
    {
        IDisposable? oldTimer,oldEvents;
        lock(stateGate){oldTimer=timer;oldEvents=events;}
        var errors=new List<string>();
        var registrations=new List<IDisposable>();
        if(oldTimer is not null)registrations.Add(oldTimer);
        if(oldEvents is not null&&!ReferenceEquals(oldEvents,oldTimer))registrations.Add(oldEvents);
        foreach(var registration in registrations)
        {
            try
            {
                registration.Dispose();
                lock(stateGate)
                {
                    if(ReferenceEquals(timer,registration))timer=null;
                    if(ReferenceEquals(events,registration))events=null;
                }
            }
            catch(Exception error){errors.Add(error.Message);}
        }
        return errors;
    }
    internal void Tick()
    {
        // Do not queue overlapping/late timer callbacks behind slow WMI.
        if(stopped||(!useSharedSystemPower&&suspended)||!Monitor.TryEnter(operationGate))return;
        try{if(!stopped){ReconcileSystemPower();if(Ready())domains?.Tick();}}
        catch(Exception error){TraceLog.Error("in-process-tick-error",error);}
        finally{Monitor.Exit(operationGate);}
    }
    internal void PowerChanged(PowerModes mode)
    {
        if(useSharedSystemPower){NotifySystemPower();return;} // Legacy injected fixtures only; production has no OS power subscription.
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

    bool Ready()
    {
        if(stopped||suspended)return false;
        if(!useSharedSystemPower)return true;
        var admitted=powerAdmission;if(admitted is null)return false;
        var current=systemPowerReader.Read();
        return current.Ready&&current.Generation==admitted.Generation&&current.SuspendSerial==admitted.SuspendSerial&&
            current.ObservedNotificationSequence==admitted.NotificationSequence&&
            current.PublicationRevision>=Math.Max(admitted.Revision,highestSystemRevision);
    }
    // Called only under operationGate. Public revocation is checked directly by
    // Ready even while this consumer waits behind slow WMI; no OS Resume grants permission.
    void ReconcileSystemPower(bool fromPublication=false)
    {
        if(!useSharedSystemPower||stopped)return;
        var current=systemPowerReader.Read();
        if(current.Valid&&current.PublicationRevision>highestSystemRevision)highestSystemRevision=current.PublicationRevision;
        if(!current.Known||current.PublicationRevision<highestSystemRevision)
        {
            suspended=true;
            if(!pauseCompleted){domains?.Suspend();pauseCompleted=true;}
            return;
        }
        highestSystemRevision=current.PublicationRevision;
        var previous=powerAdmission;
        bool changed=previous is not null&&(previous.Generation!=current.Generation||previous.SuspendSerial!=current.SuspendSerial);
        if(!current.Ready||changed)
        {
            suspended=true;
            if(!pauseCompleted||completedSuspendSerial!=current.SuspendSerial)
            {
                pauseCompleted=false;domains?.Suspend();
                completedSuspendSerial=current.SuspendSerial;pauseCompleted=true;
            }
        }
        if(!current.Ready)return;
        bool resume=suspended&&pauseCompleted;
        powerAdmission=new(current.Generation,current.PublicationRevision,current.SuspendSerial,current.ObservedNotificationSequence);
        suspended=false;
        // Cleanup may have blocked. Never resume from its earlier snapshot.
        if(!Ready()){suspended=true;return;}
        if(resume)domains?.Resume();
        else if(fromPublication)domains?.ContinueResumeAfterPowerAdmission();
        pauseCompleted=false;
    }
    internal bool SystemPowerWorkPending=>Volatile.Read(ref systemWorkQueued)!=0;
    internal void NotifySystemPower()
    {
        if(stopped||!useSharedSystemPower)return;
        Interlocked.Increment(ref systemWorkVersion);
        ScheduleSystemPower();
    }
    void ScheduleSystemPower()
    {
        int attemptedVersion;
        lock(systemWorkGate)
        {
            if(stopped||Volatile.Read(ref monitoring)==0||systemWorkQueued!=0)return;
            attemptedVersion=Volatile.Read(ref systemWorkVersion);
            systemWorkQueued=1;systemWorkOutstanding++;
        }
        while(true)
        {
            try{queueSystemPower(DrainSystemPower);return;}
            catch
            {
                lock(systemWorkGate)
                {
                    // A newer notification may have joined the failed submission.
                    // Keep its reservation until it gets a submission of its own.
                    int latest=Volatile.Read(ref systemWorkVersion);
                    if(!stopped&&latest!=attemptedVersion){attemptedVersion=latest;continue;}
                    systemWorkQueued=0;systemWorkOutstanding--;Monitor.PulseAll(systemWorkGate);
                    return; // A later event, request or existing tick can reconcile.
                }
            }
        }
    }
    void DrainSystemPower()
    {
        lock(systemWorkGate)systemWorkRunning++;
        int observed=Volatile.Read(ref systemWorkVersion);
        try{lock(operationGate){if(!stopped)ReconcileSystemPower(fromPublication:true);}}
        catch(Exception error){TraceLog.Error("in-process-system-power-error",error);}
        finally
        {
            try
            {
                lock(systemWorkGate)systemWorkQueued=0;
                if(!stopped&&observed!=Volatile.Read(ref systemWorkVersion))ScheduleSystemPower();
            }
            finally{lock(systemWorkGate){systemWorkRunning--;systemWorkOutstanding--;Monitor.PulseAll(systemWorkGate);}}
        }
    }

    internal object Shutdown()
    {
        Task<string?> pending;
        lock(stateGate)
        {
            lock(systemWorkGate){stopped=true;suspended=true;Volatile.Write(ref monitoring,0);}
            powerReader.Stop();systemPowerReader.Stop();
            if(cleanup is null||cleanup.IsCompleted&&(cleanup.IsFaulted||cleanup.IsCanceled||cleanup.Result is not null))
                cleanup=Task.Run(Cleanup);
            pending=cleanup;
        }
        long waitStart=Environment.TickCount64;
        bool completed;
        try{completed=pending.Wait(shutdownTimeout);}catch{completed=true;}
        string? cleanupError=null;
        if(completed)
        {
            try{cleanupError=pending.GetAwaiter().GetResult();}
            catch(Exception error){cleanupError=error.Message;}
        }
        // Never wait for a pool callback while holding operationGate: it may need that gate to return.
        var left=TimeSpan.FromMilliseconds(Math.Max(0,shutdownTimeout.TotalMilliseconds-(Environment.TickCount64-waitStart)));
        bool workDrained=WaitSystemPowerWork(left);
        completed=completed&&workDrained;
        // Controller.Shutdown can retain unresolved restoration debt internally.
        // Its return is never proof of hardware restoration or journal deletion.
        return new {ok=completed&&cleanupError is null,execution="in-process",stopped=true,
            cleanupCompleted=completed&&cleanupError is null,cleanupPending=!completed||cleanupError is not null,cleanupError=completed?cleanupError:null,
            hardwareRestoration="not-confirmed",journalPolicy="preserve-unconfirmed",
            shutdownTimeoutMs=(int)shutdownTimeout.TotalMilliseconds};
    }
    string? Cleanup()
    {
        var errors=new List<string>();
        // Drain an already admitted operation before taking registrations. A
        // slow subscription/schedule may return an owned object after stop.
        lock(operationGate)
        {
            errors.AddRange(CloseRegistrations());
            try{domains?.Shutdown();domains=null;}catch(Exception error){errors.Add(error.Message);}
            if(errors.Count==0)
            {
                try{owner?.Dispose();owner=null;}catch(Exception error){errors.Add(error.Message);}
            }
        }
        string? cleanupError=errors.Count==0?null:string.Join("；",errors);
        TraceLog.Write("in-process-stop",new {cleanupError,hardwareRestoration="not-confirmed",journalPolicy="preserve-unconfirmed"},true);
        return cleanupError;
    }

    sealed class PowerSubscription : IDisposable
    {
        readonly SessionEndingEventHandler ending;
        public PowerSubscription(Action onEnding)
        {
            ending=(_,_)=>onEnding();SystemEvents.SessionEnding+=ending;
        }
        public void Dispose(){SystemEvents.SessionEnding-=ending;}
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
