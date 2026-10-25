public static class MsiReplayIoPorts
{
    public sealed record ArmRow(int Timer,int Due,int Period,int Thread);
    public sealed record FireRow(int Timer,int Thread,bool IsThreadPool,string Apartment);
    sealed record Entry(Timer Timer,TimerCallback Callback,object? State,int Id);
    static readonly List<Entry> Timers=new();
    public static readonly List<ArmRow> Arms=new();public static readonly List<FireRow> Fires=new();
    public static bool Enabled,PowerOnline=true;public static int PowerReads;public static Thread? LastCallbackThread;
    public static void Reset(){Enabled=true;PowerOnline=true;PowerReads=0;LastCallbackThread=null;Timers.Clear();Arms.Clear();Fires.Clear();}
    public static bool Direct(object owner,Func<bool> callback,string operation)=>callback();
    public static bool Online(){PowerReads++;OriginalConstructorPorts.Mark("msi-replay.power-read");return PowerOnline;}
    public static Timer Create(TimerCallback callback,object? state,int due,int period)
    {
        if(!Enabled||due!=Timeout.Infinite||period!=Timeout.Infinite)throw new InvalidOperationException("REPLAY_UNDECLARED_TIMER_CREATION");var timer=new Timer(callback,state,due,period);Timers.Add(new(timer,callback,state,Timers.Count+1));OriginalConstructorPorts.Mark("msi-replay.original-timer-created");return timer;
    }
    public static bool Arm(Timer timer,int due,int period)
    {
        var entry=Timers.SingleOrDefault(r=>ReferenceEquals(r.Timer,timer))??throw new InvalidOperationException("REPLAY_FOREIGN_TIMER");if(!Enabled||due<0||period!=Timeout.Infinite)throw new InvalidOperationException("REPLAY_PERIODIC_OR_INVALID_ARM");Arms.Add(new(entry.Id,due,period,Environment.CurrentManagedThreadId));OriginalConstructorPorts.Mark("msi-replay.timer-arm:"+due);return true;
    }
    public static int LatestId=>Timers.Last().Id;
    public static Task FireAsync(int? timer=null)
    {
        var entry=timer is int id?Timers.Single(r=>r.Id==id):Timers.Last();LastCallbackThread=null;return Task.Run(()=>{LastCallbackThread=Thread.CurrentThread;Fires.Add(new(entry.Id,Environment.CurrentManagedThreadId,Thread.CurrentThread.IsThreadPoolThread,Thread.CurrentThread.GetApartmentState().ToString()));OriginalConstructorPorts.Mark("msi-replay.original-timer-callback");entry.Callback(entry.State);});
    }
    public static void Fire(int? timer=null)
    {
        var task=FireAsync(timer);if(!task.Wait(TimeSpan.FromSeconds(10)))throw new InvalidOperationException("REPLAY_CALLBACK_TIMEOUT");task.GetAwaiter().GetResult();
    }
}
