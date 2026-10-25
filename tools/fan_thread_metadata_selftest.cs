using System.Text.Json;

// Substitutes expose independent expected data and count actual getter calls.
// They never enumerate a real process or access any device.
internal sealed class ProcessThread(int tid, double cpuMs, bool cpuFails = false, bool startFails = false, bool stateFails = false)
{
    internal int Tid = tid;
    internal double CpuMs = cpuMs;
    internal bool CpuFails = cpuFails, StartFails = startFails, StateFails = stateFails;
    internal int StartReads, StateReads;
    internal string ExpectedStart => StartFails ? "unknown" : new DateTime(2026, 10, 16, 1, 2, 3).AddMilliseconds(Tid).ToString("HH:mm:ss.fff");
    internal string ExpectedState => StateFails ? "unknown" : "Wait";
    public int Id => Tid;
    public TimeSpan TotalProcessorTime => CpuFails ? throw new InvalidOperationException("cpu inaccessible") : TimeSpan.FromMilliseconds(CpuMs);
    public DateTime StartTime { get { StartReads++; return StartFails ? throw new InvalidOperationException("start inaccessible") : new DateTime(2026, 10, 16, 1, 2, 3).AddMilliseconds(Tid); } }
    public string ThreadState { get { StateReads++; return StateFails ? throw new InvalidOperationException("state inaccessible") : "Wait"; } }
}
internal sealed record Process(ProcessThread[] Threads);
internal sealed record CpuOracle(ProcessThread Thread, double Delta, bool IsNew);
internal sealed partial class FanThreadChecks
{
    private static readonly List<object> Results = [];
    private static int assertions;
    private static void Assert(bool yes, string message) { assertions++; if (!yes) throw new InvalidOperationException(message); }
    private static string Json(object value) => JsonSerializer.Serialize(value);
    private void Case(string name, ProcessThread[] input, bool selected)
    {
        var prior = new Dictionary<int, double>(lastThreadCpuMs);
        var priorIds = new HashSet<int>(lastThreadIds);
        var readable = input.Where(t => !t.CpuFails).ToArray();
        var oracle = readable.Select(t => new CpuOracle(t, prior.TryGetValue(t.Tid, out var before) ? Math.Round(Math.Max(0, t.CpuMs-before),1) : 0d, !prior.ContainsKey(t.Tid))).ToArray();
        var wantedHot = oracle.OrderByDescending(x=>x.Delta).Take(8).ToArray();
        var wantedNew = oracle.Where(x=>x.IsNew).Take(12).ToArray();
        var expectedHot = wantedHot.Select(x=>(object)new { tid=x.Thread.Tid,cpuDeltaMs=x.Delta,cpuTotalMs=Math.Round(x.Thread.CpuMs,1),startedAt=x.Thread.ExpectedStart,state=x.Thread.ExpectedState }).ToArray();
        var expectedNew = wantedNew.Select(x=>(object)new { tid=x.Thread.Tid,startedAt=x.Thread.ExpectedStart,state=x.Thread.ExpectedState }).ToArray();
        var actual = CollectThreadCpuAttribution(new Process(input));
        Assert(Json(actual.HotThreads)==Json(expectedHot),name+": hot ranking/labels/values changed");
        Assert(Json(actual.ThreadsAdded)==Json(expectedNew),name+": new-thread order/labels changed");
        Assert(actual.ThreadsRemovedCount==priorIds.Count(id=>!readable.Any(t=>t.Tid==id)),name+": removed-thread count changed");
        var selectedIds=wantedHot.Concat(wantedNew).Select(x=>x.Thread.Tid).ToHashSet();
        foreach(var thread in input)
        {
            var wanted=thread.CpuFails ? 0 : selected ? (selectedIds.Contains(thread.Tid) ? 1 : 0) : 1;
            Assert(thread.StartReads==wanted && thread.StateReads==wanted,name+": redundant/missing metadata getter for tid="+thread.Tid+" expected="+wanted+" actual="+thread.StartReads+"/"+thread.StateReads);
        }
        Assert(readable.All(t=>lastThreadCpuMs[t.Tid]==t.CpuMs),name+": non-emitted CPU baseline was lost");
        Assert(lastThreadIds.SetEquals(readable.Select(t=>t.Tid)),name+": live IDs changed");
        if (lastThreadCpuMs.Count>256) Assert(lastThreadCpuMs.Keys.All(id=>readable.Any(t=>t.Tid==id)),name+": stale history not pruned");
        Results.Add(new { name,readableThreads=readable.Length,emittedUniqueThreads=selectedIds.Count,startTimeReads=input.Sum(x=>x.StartReads),threadStateReads=input.Sum(x=>x.StateReads),output=new {actual.HotThreads,actual.ThreadsAdded,actual.ThreadsRemovedCount} });
        Console.WriteLine("PASS "+name+" readable="+readable.Length+" metadata="+input.Sum(x=>x.StartReads)+" emitted="+selectedIds.Count);
    }
    private static ProcessThread[] Threads(int first,int count,Func<int,double> cpu) => Enumerable.Range(first,count).Select(id=>new ProcessThread(id,cpu(id))).ToArray();
    public static int Main(string[] args)
    {
        try
        {
            var selected=args[0]=="selected";
            var checks=new FanThreadChecks();
            checks.Case("empty-first-sample",[],selected);
            checks.Case("first-40-new-overlapping-hot-and-new",Threads(1,40,id=>id*10),selected);
            checks.Case("all-40-known-select-eight",Threads(1,40,id=>id*10+(41-id)*3),selected);
            checks.Case("known-hot-and-new-disjoint-max-twenty",Threads(1,40,id=>id*10+(41-id)*7).Concat(Threads(101,40,id=>id)).ToArray(),selected);
            checks.Case("stable-ties-and-cpu-counter-decrease",Threads(1,40,_=>0),selected);
            checks.Case("metadata-failures-stay-independent",[new(1,1000,false,true,false),new(2,1000,false,false,true),new(3,1000,false,true,true),new(4,0,true),new(5,1000)],selected);
            checks.Case("unemitted-thread-becomes-hottest",[new(30,10000),new(1,1001),new(2,1001)],selected);
            checks.Case("all-threads-exited",[],selected);
            checks.Case("large-population-history",Threads(1000,300,id=>id),selected);
            checks.Case("large-population-prunes-dead",Threads(1000,2,id=>id+5),selected);
            var result=new {ok=true,assertions,mode=args[0],scope="exact extracted diagnostic collector; fake read-only thread getters; no CPU benchmark or hardware",cases=Results};
            File.WriteAllText(args[1],JsonSerializer.Serialize(result,new JsonSerializerOptions {WriteIndented=true}));
            Console.WriteLine("PASS assertions="+assertions);
            return 0;
        }
        catch(Exception ex) {Console.Error.WriteLine(ex);return 1;}
    }
}
