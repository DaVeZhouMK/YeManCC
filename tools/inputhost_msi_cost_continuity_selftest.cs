using System.Text.Json;
using YeManInputHost.Diagnostics;
using D = YeManInputHost.Diagnostics.MsiCostWindowDiagnostics;

internal sealed class Harness : IDisposable
{
    internal readonly List<JsonElement> Rows = new();
    internal readonly D Diagnostic;
    internal bool Enabled = true, FlagFail, RuntimeFail, SdkFail, AppendFail;
    internal int FlagReads, RuntimeReads, SdkReads, AppendCalls;
    internal Action? RuntimeReadAction;
    internal D.RuntimeSample Current = Sample(1000, 0);
    private ulong sequence;
    private readonly object controller = new();
    internal Harness()
    {
        Diagnostic = new(true,
            () => { FlagReads++; if (FlagFail) throw new IOException("fixture-flag"); return Enabled; },
            () => { RuntimeReads++; if (RuntimeFail) throw new IOException("fixture-runtime"); RuntimeReadAction?.Invoke(); return Current; },
            line => { AppendCalls++; if (AppendFail) throw new IOException("fixture-append"); Rows.Add(JsonDocument.Parse(line).RootElement.Clone()); });
    }
    internal static D.RuntimeSample Sample(long tick, double cpu, D.ThreadCpu[]? threads = null) =>
        new(tick, cpu, tick * 10, tick / 1000d, 4096, 1, 0, 0,
            threads ?? [new(7, cpu / 2), new(8, cpu / 4)], 7);
    internal JsonElement? Post(D.RuntimeSample sample, ulong epoch=1, ulong power=1, long generation=0,
        string run="run", string host="host", string persona="steamdeck")
    {
        Current=sample;
        int before=Rows.Count;
        var prior=Diagnostic.QueueCounters;
        var w=new D.Window(1700000000000+(long)sequence,Environment.TickCount64,1000,
            125,[30,10,20],1250,125,2500,125,900000,125,0,0,0,
            [KeyValuePair.Create("pipe-read",7), KeyValuePair.Create("unrelated",99)],
            "Active",host,run,epoch,power,persona,"fixture-profile",++sequence,generation,
            new(controller),(target,verified)=> { SdkReads++; if(SdkFail)throw new IOException("fixture-sdk");return new {status="fixture-only"}; },true);
        if(!Diagnostic.TryRecord(w))throw new Exception("unexpected queue drop");
        if(!SpinWait.SpinUntil(()=> {var c=Diagnostic.QueueCounters;return c.Completed+c.Failed>prior.Completed+prior.Failed;},5000))
            throw new Exception("worker timeout");
        return Rows.Count==before ? null : Rows[^1];
    }
    public void Dispose()
    {
        Diagnostic.Dispose();
        if(!SpinWait.SpinUntil(()=>Diagnostic.QueueCounters.Stopped,5000))throw new Exception("worker leaked");
    }
}
internal static class Tests
{
    private static int checks;
    private static readonly List<object> results=new();
    private static void Check(bool value,string label) {checks++;if(!value)throw new Exception(label);}
    private static JsonElement Row(JsonElement? r)=>r ?? throw new Exception("missing row");
    private static void Unknown(JsonElement r,string label)
    {
        Check(r.GetProperty("procCpuMsPerSec").ValueKind==JsonValueKind.Null,label+": CPU unknown");
        Check(r.GetProperty("allocBytesPerSec").ValueKind==JsonValueKind.Null,label+": allocations unknown");
        Check(r.GetProperty("gcPauseMsPerSec").ValueKind==JsonValueKind.Null,label+": GC unknown");
        Check(r.GetProperty("byThread").GetArrayLength()==0,label+": no inherited thread rate");
        Check(!r.GetProperty("msiCostWindowDeferred").GetProperty("runtimeDeltaKnown").GetBoolean(),label+": delta unknown");
    }
    private static void Group(string name,Action test)
    {
        int start=checks;try{test();results.Add(new{name,pass=true,checks=checks-start});}
        catch(Exception e){results.Add(new{name,pass=false,checks=checks-start,error=e.Message});}
    }
    private static void GoodRate(JsonElement r,long value,string label)
    {Check(r.GetProperty("procCpuMsPerSec").GetInt64()==value,label);}
    private static void Continuous()
    {
        using var h=new Harness(); Unknown(Row(h.Post(Harness.Sample(1000,100))),"first");
        var row=Row(h.Post(Harness.Sample(2000,150))); GoodRate(row,50,"50ms per second");
        Check(row.GetProperty("submitElapsedUs").GetProperty("p50").GetInt64()==20,"p50");
        Check(row.GetProperty("submitElapsedUs").GetProperty("max").GetInt64()==30,"max");
        Check(row.GetProperty("frames").GetInt64()==125,"frame count");
        Check(row.GetProperty("jsonParseUs").GetProperty("mean").GetInt64()==10,"parse mean");
        Check(row.GetProperty("hidSubmitUs").GetProperty("mean").GetInt64()==20,"submit mean");
        Check(row.GetProperty("pipeReadUs").GetProperty("mean").GetInt64()==7200,"read mean");
        Check(row.GetProperty("byThread")[0].GetProperty("role").GetString()=="msi-cost-diagnostic","sampler role over stale caller");
        GoodRate(Row(h.Post(Harness.Sample(4000,250))),50,"actual 2s runtime denominator");
        GoodRate(Row(h.Post(Harness.Sample(5000,250))),0,"real zero stays known");
    }
    private static void Gate()
    {
        using var h=new Harness(); h.Post(Harness.Sample(1000,100));h.Post(Harness.Sample(2000,150));
        int reads=h.RuntimeReads,sdk=h.SdkReads,appends=h.AppendCalls;
        h.Enabled=false;Check(h.Post(Harness.Sample(2500,155)) is null,"off no row");
        Check(h.RuntimeReads==reads&&h.SdkReads==sdk&&h.AppendCalls==appends,"off no expensive reads or write");
        h.Enabled=true; Unknown(Row(h.Post(Harness.Sample(3000,160))),"reenabled");
        GoodRate(Row(h.Post(Harness.Sample(4000,210))),50,"post-gate recovery");
    }
    private static void Failure(string boundary)
    {
        using var h=new Harness();h.Post(Harness.Sample(1000,100));h.Post(Harness.Sample(2000,150));
        h.FlagFail=boundary=="flag";h.RuntimeFail=boundary=="runtime";h.SdkFail=boundary=="sdk";h.AppendFail=boundary=="append";
        Check(h.Post(Harness.Sample(2500,175)) is null,"failure no row");
        Check(h.Diagnostic.QueueCounters.Failed==1,"failure counted by actual dispatcher");
        h.FlagFail=h.RuntimeFail=h.SdkFail=h.AppendFail=false;
        Unknown(Row(h.Post(Harness.Sample(3000,200))),boundary+" recovery");
        GoodRate(Row(h.Post(Harness.Sample(4000,250))),50,"worker survives "+boundary);
    }
    private static void Gap()
    {
        using var h=new Harness();h.Post(Harness.Sample(1000,100));
        GoodRate(Row(h.Post(Harness.Sample(4000,250))),50,"3000ms included");
        var r=Row(h.Post(Harness.Sample(7001,400)));Unknown(r,"3001ms gap");
        Check(r.GetProperty("cpuObservation").GetProperty("continuityState").GetString()=="observation-gap","explicit gap reason");
        GoodRate(Row(h.Post(Harness.Sample(8001,450))),50,"gap recovery");
    }
    private static void Scope(string field)
    {
        using var h=new Harness();h.Post(Harness.Sample(1000,100));
        if (field=="controller") h.Diagnostic.InvalidateController();
        var r=Row(h.Post(Harness.Sample(2000,150),epoch:field=="epoch"?2UL:1UL,power:field=="power"?2UL:1UL,
            generation:field=="controller"?1:0,run:field=="run"?"run2":"run",host:field=="host"?"host2":"host",
            persona:field=="persona"?"xbox360":"steamdeck"));
        Unknown(r,field+" changed");
        Check(r.GetProperty("cpuObservation").GetProperty("continuityState").GetString()=="scope-changed","explicit scope reason");
    }
    private static void Invalidation(bool duringRead)
    {
        using var h=new Harness();h.Post(Harness.Sample(1000,100));
        if (duringRead) h.RuntimeReadAction=()=>h.Diagnostic.InvalidateController();
        else h.Diagnostic.InvalidateController();
        var r=Row(h.Post(Harness.Sample(2000,150)));
        Unknown(r,"real invalidation");
        Check(r.GetProperty("cpuObservation").GetProperty("continuityState").GetString()=="controller-generation-ended","actual invalidation reason");
        var timing=r.GetProperty("msiCostWindowDeferred");
        Check(!timing.GetProperty("runtimeControllerGenerationCurrent").GetBoolean(),"runtime bounded by actual generation reads");
        Check(timing.GetProperty("runtimeControllerGenerationBefore").GetInt64()==(duringRead?0:1),"generation before");
        Check(timing.GetProperty("runtimeControllerGenerationAfter").GetInt64()==1,"generation after");
        h.RuntimeReadAction=null;
        Unknown(Row(h.Post(Harness.Sample(3000,200),generation:1)),"invalid window never seeds new baseline");
        GoodRate(Row(h.Post(Harness.Sample(4000,250),generation:1)),50,"new generation recovery");
    }
    private static void InvalidClock(long tick)
    {
        using var h=new Harness();h.Post(Harness.Sample(2000,100));Unknown(Row(h.Post(Harness.Sample(tick,150))),"clock invalid");
        GoodRate(Row(h.Post(Harness.Sample(tick+1000,200))),50,"clock recovery");
    }
    private static void Threads()
    {
        using var h=new Harness();h.Post(Harness.Sample(1000,100,[new(7,10),new(8,30)]));
        var r=Row(h.Post(Harness.Sample(2000,150,[new(7,20),new(9,1000)])));
        Check(r.GetProperty("byThread").GetArrayLength()==1,"new thread is baseline");
        Check(r.GetProperty("byThread")[0].GetProperty("tid").GetInt32()==7,"remaining thread");
        r=Row(h.Post(Harness.Sample(3000,200,[new(8,3000),new(9,1020)])));
        Check(r.GetProperty("byThread").GetArrayLength()==1,"departed thread baseline on return");
        Check(r.GetProperty("byThread")[0].GetProperty("tid").GetInt32()==9,"only continuous thread");
        r=Row(h.Post(Harness.Sample(4000,220,[new(8,0),new(9,1020)])));
        Check(r.GetProperty("cpuObservation").GetProperty("threadOtherUnknown").GetInt32()==1,"regressed thread unknown");
    }
    private static void Oracle()
    {
        using var h=new Harness();long tick=1000;decimal cpu=0;h.Post(Harness.Sample(tick,(double)cpu));
        var random=new Random(8601);
        for(int i=0;i<80;i++)
        {int dt=random.Next(20,3000);decimal dc=random.Next(0,4000)/10m;tick+=dt;cpu+=dc;
         long expected=(long)decimal.Round(dc*1000m/dt,0,MidpointRounding.ToEven);
         GoodRate(Row(h.Post(Harness.Sample(tick,(double)cpu))),expected,"independent decimal rate oracle "+i);}
    }
    internal static int Main(string[] args)
    {
        Group("continuous payload/rates/role/real-zero",Continuous);
        Group("disabled and re-enabled",Gate);
        foreach(var boundary in new[]{"flag","runtime","sdk","append"})Group("failure "+boundary,()=>Failure(boundary));
        Group("long gap boundary",Gap);
        foreach(var field in new[]{"epoch","power","controller","run","host","persona"})Group("scope "+field,()=>Scope(field));
        Group("actual invalidation before runtime read",()=>Invalidation(false));
        Group("actual invalidation during runtime read",()=>Invalidation(true));
        Group("same observation tick",()=>InvalidClock(2000));Group("regressed observation tick",()=>InvalidClock(1500));
        Group("thread departure/regression/observer role",Threads);
        Group("independent rate oracle",Oracle);
        int failures=results.Count(r=>!(bool)r.GetType().GetProperty("pass")!.GetValue(r)!);
        var report=new {pass=failures==0,groups=results.Count,checks,failures,results,
            productionRuntimeSamplerCalls=0,deviceCalls=0,sdkFixtureOnly=true,actualDispatcher=true,actualDiagnosticSource=true};
        System.IO.File.WriteAllText(args[0],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true})+Environment.NewLine);
        Console.WriteLine($"groups={results.Count} checks={checks} failures={failures}");
        return failures==0 || args.Contains("--baseline")?0:1;
    }
}
