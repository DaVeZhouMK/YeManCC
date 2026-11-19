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
    internal Harness(Func<int, D.ThreadIdentity>? identityReader = null)
    {
        Diagnostic = new(true,
            () => { FlagReads++; if (FlagFail) throw new IOException("fixture-flag"); return Enabled; },
            () => { RuntimeReads++; if (RuntimeFail) throw new IOException("fixture-runtime"); RuntimeReadAction?.Invoke(); return Current; },
            line => { AppendCalls++; if (AppendFail) throw new IOException("fixture-append"); Rows.Add(JsonDocument.Parse(line).RootElement.Clone()); }, readThreadIdentity: identityReader);
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
        using var h=new Harness();
        Check(!h.Diagnostic.TryWriteSignalLog("fixture-no-writer"), "missing signal sink rejects admission");
        var signal = JsonSerializer.SerializeToElement(h.Diagnostic.SnapshotSignalLogCounters());
        var cost = Row(h.Post(Harness.Sample(1000,100))).GetProperty("signalLogWriter");
        Check(signal.GetProperty("version").GetInt32()==1 && signal.GetProperty("scope").GetString()!.Contains("Admission is not file-write success", StringComparison.Ordinal),
            "standalone signal writer retains inline interpretation");
        Check(cost.GetProperty("version").GetInt32()==2 && !cost.TryGetProperty("scope",out _) &&
            cost.EnumerateObject().Count()==signal.EnumerateObject().Count()-1, "cost writer references the guide");
        foreach (var field in signal.EnumerateObject())
        {
            if (field.Name is "version" or "scope" or "sharedQueue") continue;
            Check(JsonElement.DeepEquals(field.Value,cost.GetProperty(field.Name)), "signal writer preserves "+field.Name);
        }
        Check(cost.GetProperty("offered").GetInt64()==1 && cost.GetProperty("invalid").GetInt64()==1 &&
            cost.GetProperty("discardedOnClose").ValueKind==JsonValueKind.Null, "invalid offer visible without inferring a close discard");
        h.Post(Harness.Sample(2000,150));
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
        var failure=h.Post(Harness.Sample(2500,175));
        if(boundary=="append")Check(failure is null,"failed writer cannot prove its own failure receipt");
        else
        {
            var row=Row(failure);
            Check(row.GetProperty("kind").GetString()=="cost-window-failed","failure is an explicit diagnostic summary, never a measured cost row");
            var summary=row.GetProperty("diagnosticBudget");
            var expectedStage=boundary=="flag"?"flag-read":boundary=="runtime"?"runtime-read":"sdk-read";
            Check(summary.GetProperty("state").GetString()=="cost-window-failed"&&summary.GetProperty("reason").GetString()==expectedStage,"failure summary records exact boundary");
            Check(summary.GetProperty("errorType").GetString()=="IOException"&&!summary.GetProperty("completePayloadRetained").GetBoolean(),"failed observation stays explicitly incomplete");
            Check(!row.TryGetProperty("procCpuMsPerSec",out _)&&!row.TryGetProperty("allocBytesPerSec",out _)&&!row.TryGetProperty("byThread",out _),"failure summary cannot fabricate CPU or allocation measurement");
        }
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
        var deferred=r.GetProperty("msiCostWindowDeferred");
        Check(deferred.GetProperty("version").GetInt32()==3 &&
            !deferred.TryGetProperty("hostInstanceId",out _) && !deferred.TryGetProperty("epoch",out _) &&
            !deferred.TryGetProperty("powerGeneration",out _), "window identity is written once");
        Check(r.GetProperty("hostInstanceId").GetString()==(field=="host"?"host2":"host") &&
            r.GetProperty("epoch").GetUInt64()==(field=="epoch"?2UL:1UL) &&
            r.GetProperty("powerGeneration").GetUInt64()==(field=="power"?2UL:1UL) &&
            deferred.GetProperty("runId").GetString()==(field=="run"?"run2":"run"), "captured identity survives scope change");
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

#if YMCC_COST_THREAD_IDENTITY
    private static readonly List<object> IdentityWitnesses = new();
    [System.Runtime.InteropServices.DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern uint GetCurrentThreadId();
    [System.Runtime.InteropServices.DllImport("kernel32.dll", ExactSpelling = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern int SetThreadDescription(nint thread, string description);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern nint GetCurrentThread();

    private static void IdentityProjection()
    {
        var queried = new List<int>();
        var caller = Environment.CurrentManagedThreadId;
        using var h = new Harness(id =>
        {
            Check(Environment.CurrentManagedThreadId != caller, "identity query stays on original worker");
            queried.Add(id);
            if (id == 7) throw new IOException("identity-only fault");
            if (id == 8) return new("owner-mismatch", OwnerPid: 999);
            return new("observed-current-thread", "134392124036916571", new string('\u0001', 600), OwnerPid: (uint)Environment.ProcessId);
        });
        var baseThreads = Enumerable.Range(7, 7).Select(id => new D.ThreadCpu(id, 0)).ToArray();
        Unknown(Row(h.Post(Harness.Sample(1000, 0, baseThreads))), "identity baseline");
        Check(queried.Count == 0, "baseline no OS identity query");
        var active = Enumerable.Range(7, 7).Select(id => new D.ThreadCpu(id, (14-id)*10)).ToArray();
        var row = Row(h.Post(Harness.Sample(2000, 300, active) with { WriterBirthFileTime = "134392124036916571" }));
        Check(queried.SequenceEqual(new[]{7,8,9,10}), "exact top four only");
        Check(row.GetProperty("byThread").GetArrayLength() == 4, "four original rate rows");
        Check(row.GetProperty("byThread")[0].GetProperty("role").GetString() == "msi-cost-diagnostic", "description cannot replace original role");
        Check(row.GetProperty("byThread")[0].GetProperty("identity").GetProperty("state").GetString() == "query-unavailable", "query fault stays unknown");
        Check(row.GetProperty("byThread")[1].GetProperty("identity").GetProperty("state").GetString() == "owner-mismatch", "foreign owner stays explicit");
        Check(row.GetProperty("byThread")[1].GetProperty("identity").GetProperty("description").ValueKind == JsonValueKind.Null, "foreign owner no name");
        var named = row.GetProperty("byThread")[2].GetProperty("identity");
        Check(named.GetProperty("birthFileTime").GetString() == "134392124036916571", "FILETIME remains exact string");
        Check(named.GetProperty("description").GetString()!.Length == 256 && named.GetProperty("descriptionTruncated").GetBoolean(), "escaped names bounded");
        Check(row.GetProperty("writerBirthFileTime").GetString() == "134392124036916571", "writer birth exact projection");
        Check(!row.GetProperty("threadIdentityObservation").GetProperty("atomicWithCpuSample").GetBoolean(), "no atomic attribution claim");
        Check(row.GetProperty("threadIdentityObservation").GetProperty("attempted").GetInt32() == 4, "attempt counter bounded");
        var threadObservation = row.GetProperty("threadIdentityObservation");
        var deferred = row.GetProperty("msiCostWindowDeferred");
        Check(threadObservation.GetProperty("version").GetInt32() == 2 && !threadObservation.TryGetProperty("scope", out _) &&
            deferred.GetProperty("version").GetInt32() == 3 && !deferred.TryGetProperty("scope", out _) &&
            !deferred.TryGetProperty("queueScope", out _), "periodic scope text uses the export guide schema");
        Check(!deferred.GetProperty("queueSnapshotAtomic").GetBoolean() && deferred.GetProperty("queue").ValueKind == JsonValueKind.Object &&
            !deferred.GetProperty("controllerWrites").GetBoolean() && !deferred.GetProperty("pipeWrites").GetBoolean(), "queue counters and original observation boundaries survive compact text");
        Check(row.GetProperty("msiCostWindowDeferred").GetProperty("stageTimingUs").GetProperty("threadIdentityRead").GetInt64() >= 0, "query overhead emitted");
        GoodRate(row, 300, "identity failure preserves process CPU");
        Check(h.Diagnostic.QueueCounters.Failed == 0, "metadata failure does not fail cost window");
        h.Enabled = false; h.Post(Harness.Sample(3000, 400, active));
        Check(queried.Count == 4, "OFF zero new metadata queries");
        h.Enabled = true; Unknown(Row(h.Post(Harness.Sample(4000, 450, active))), "metadata reenable baseline");
        Check(queried.Count == 4, "reenable baseline has no inherited query");
        var changed = active.Select(t => t with { TotalMs = t.TotalMs + 10 }).ToArray();
        Unknown(Row(h.Post(Harness.Sample(5000, 500, changed), power:2)), "metadata power boundary");
        Check(queried.Count == 4, "scope reset no identity query");
        IdentityWitnesses.Add(row);
    }

    private static void IdentityBudget()
    {
        foreach (var variant in new[] { "observed", "unsupported", "bounded-malformed", "empty-primary", "missing-primary", "invalid-primary",
            "button-null", "button-string", "button-negative", "button-overflow" })
        {
            using var h = new Harness(id => new("observed-current-thread", "134392124036916571", new string('\u0001', 600)));
            h.Post(Harness.Sample(1000, 0, Enumerable.Range(1, 4).Select(id => new D.ThreadCpu(id, 0)).ToArray()));
            h.Current = Harness.Sample(2000, 100, Enumerable.Range(1, 4).Select(id => new D.ThreadCpu(id, 20)).ToArray());
            var prior=h.Diagnostic.QueueCounters;
            object progress = variant == "bounded-malformed" ? new
            {
                Status = new string('\u0001', 5000), InputWaitHandlePresent = (bool?)null,
                PumpAcceptedSharedSequenceAfter = new[] { 1, 2 }, BulkMustNotCopy = new string('z', 16000)
            } : new
            {
                Version = 1, Status = "observed", Detail = "sequential-managed-fields", BeforeTick = 1L, AfterTick = 2L,
                ProducerSharedSequenceBefore = uint.MaxValue, ProducerSharedSequenceAfter = 0U,
                PumpAcceptedSharedSequenceBefore = uint.MaxValue, PumpAcceptedSharedSequenceAfter = 0U,
                SequenceValuesUnchangedAcrossCapture = false, IdleThreadIsAlive = false,
                IdleThreadObservation = "is-alive-metadata", InputWaitHandlePresent = true
            };
            object[] endpoints = variant switch
            {
                "empty-primary" => [new { Address = 131, PendingCount = 0, OldestSequence = (uint?)null }],
                "missing-primary" or "button-null" or "button-string" or "button-negative" or "button-overflow" =>
                    [new { Address = 129, PendingCount = 1, OldestSequence = 41U }],
                "invalid-primary" => [new { Address = 131, PendingCount = 1, OldestSequence = "41" }],
                "bounded-malformed" => [new { Address = 131, PendingCount = 1, OldestSequence = 20U },
                    new { Address = 131, PendingCount = 1, OldestSequence = 21U }],
                _ => [new { Address = 129, PendingCount = 2, OldestSequence = 41U },
                    new { Address = 131, PendingCount = 2, OldestSequence = uint.MaxValue }]
            };
            object cachedReport = variant switch
            {
                "observed" or "empty-primary" => new { Status = "decoded-cached-steamdeck-report", PacketCounter = uint.MaxValue,
                    ButtonBits = variant == "observed" ? 0x80000000000A0200UL : 0UL,
                    LeftTouch = variant == "observed", RightTouch = false, LeftClick = variant == "observed", RightClick = false,
                    Extra = new string('s', 16000) },
                "missing-primary" => new { Status = "decoded-cached-steamdeck-report", PacketCounter = 1U,
                    LeftTouch = false, RightTouch = false, LeftClick = false, RightClick = false },
                "button-null" or "button-string" or "button-negative" or "button-overflow" => new
                {
                    Status = "decoded-cached-steamdeck-report", PacketCounter = 1U,
                    ButtonBits = variant switch { "button-string" => (object)"512", "button-negative" => -1,
                        "button-overflow" => 18446744073709551616m, _ => null },
                    LeftTouch = false, RightTouch = false, LeftClick = false, RightClick = false
                },
                "bounded-malformed" => new { Status = "decoded-cached-steamdeck-report", PacketCounter = uint.MaxValue + 1UL,
                    LeftTouch = true, RightTouch = false, LeftClick = false, RightClick = false },
                "invalid-primary" => new { Status = "decoded-cached-steamdeck-report", PacketCounter = uint.MaxValue,
                    LeftTouch = "false", RightTouch = false, LeftClick = false, RightClick = false },
                _ => new string('s', 16000)
            };
            object submittedReport = variant switch
            {
                "observed" or "empty-primary" => new { PacketCounter = 0U, TouchMask = variant == "observed" ? 2 : 0,
                    ClickMask = 0, SourceTick = 140UL, SampleSequence = ulong.MaxValue, NeutralCommand = variant == "empty-primary",
                    Extra = new string('p', 16000) },
                "bounded-malformed" => new { PacketCounter = 0U, TouchMask = 4, ClickMask = 0,
                    SourceTick = 140UL, SampleSequence = 8UL, NeutralCommand = false },
                "invalid-primary" => new { PacketCounter = 0U, TouchMask = 2, ClickMask = 0,
                    SourceTick = -1, SampleSequence = 8UL, NeutralCommand = false },
                _ => new string('p', 16000)
            };
            object snapshot = variant == "unsupported" ? new { Status = "unsupported", Detail = "sdk-asset-unverified" } : new
            {
                Status = "observed", Detail = "observed", ObserverId = "fixture-observer", DeviceIdentity = 7L,
                StopFlag = false, InputThreadIsAlive = true, ProducerProgress = progress,
                QueueObservation = "observed", PrimaryInEndpoint = 131, Endpoints = endpoints,
                InputReportCacheObservation = "observed-primary-report-cache", LastSteamDeckPadReport = cachedReport
            };
            var controller=new object();
            var w=new D.Window(1700000000000,Environment.TickCount64,1000,1,[],0,0,0,0,0,0,0,0,0,[],
                "Active","host","run",1,1,"steamdeck","fixture-profile",2,0,new(controller),(_,_)=>{h.SdkReads++;return snapshot;},true)
                {SteamDeckPadSubmissions=new { LastSuccessfulReport = submittedReport, Entries = new[] { new string('x', 16000) }}};
            Check(h.Diagnostic.TryRecord(w), variant+": oversize identity row admitted");
            Check(SpinWait.SpinUntil(()=>h.Diagnostic.QueueCounters.Completed+h.Diagnostic.QueueCounters.Failed>prior.Completed+prior.Failed,5000), variant+": budget worker returned");
            GC.KeepAlive(controller);
            var row=h.Rows[^1];
            Check(System.Text.Encoding.UTF8.GetByteCount(row.GetRawText())<=D.DiagnosticRecordMaxBytes, variant+": original 16KiB limit");
            Check(row.GetProperty("byThread").GetArrayLength()==4, variant+": fallback retains four identities");
            Check(row.GetProperty("steamDeckPadSubmissions").ValueKind==JsonValueKind.Null && row.GetProperty("usbipTransport").ValueKind==JsonValueKind.Null, variant+": original bulk shedding policy");
            Check(row.GetProperty("byThread")[0].GetProperty("identity").GetProperty("description").GetString()!.Length==256, variant+": name cap survives budget fallback");
            Check(row.TryGetProperty("usbipTransportSummary",out var compact), variant+": SDK progress retained after bulk shedding");
            Check(!compact.GetProperty("completePayloadRetained").GetBoolean(), variant+": omitted SDK remains incomplete");
            Check(compact.GetProperty("version").GetInt32()==2 && !compact.TryGetProperty("scope",out _),
                variant+": versioned SDK summary uses the export guide");
            var budget = row.GetProperty("diagnosticBudget");
            Check(budget.GetProperty("schemaVersion").GetInt32()==2 && budget.GetProperty("state").GetString()=="bulk-evidence-omitted" &&
                !budget.TryGetProperty("scope",out _) && !budget.GetProperty("completePayloadRetained").GetBoolean(),
                variant+": versioned budget receipt reports omitted evidence");
            var signalWriter = row.GetProperty("signalLogWriter");
            Check(signalWriter.GetProperty("version").GetInt32()==2 && !signalWriter.TryGetProperty("scope",out _) &&
                signalWriter.GetProperty("snapshotAtomic").ValueKind==JsonValueKind.False &&
                signalWriter.GetProperty("sharedQueue").ValueKind==JsonValueKind.Object,
                variant+": budget fallback preserves compact signal and shared queue evidence");
            var sdk=compact.GetProperty("snapshot");
            Check(!sdk.TryGetProperty("LastSteamDeckPadReport",out _), variant+": large report excluded");
            Check(!sdk.TryGetProperty("Endpoints",out _), variant+": no endpoint array retained");
            var cached = sdk.TryGetProperty("CachedPadWitness", out var c) ? c : default;
            var submitSummary = row.GetProperty("steamDeckPadSubmissionSummary");
            var submitted = submitSummary.TryGetProperty("SubmittedPadWitness", out var s) ? s : default;
            Check(!submitSummary.TryGetProperty("LastSuccessfulReport", out _) && !submitSummary.TryGetProperty("Entries", out _) &&
                submitSummary.GetProperty("entriesOmittedByRecordBudget").GetInt32() == 1, variant+": report payload and history stay omitted");
            if (variant is "observed" or "empty-primary")
            {
                bool active = variant == "observed";
                Check(cached.ValueKind == JsonValueKind.Object && cached.GetProperty("PacketCounter").GetUInt32() == uint.MaxValue &&
                    cached.GetProperty("TouchMask").GetByte() == (active ? 1 : 0) && cached.GetProperty("ClickMask").GetByte() == (active ? 1 : 0) &&
                    cached.EnumerateObject().Count() == 4, variant+": cached report bounded to four raw scalars");
                Check(cached.GetProperty("ButtonBitsHex").GetString() == (active ? "80000000000A0200" : "0000000000000000"),
                    variant+": exact cached 64-bit mask, including known zero");
                Check(submitted.ValueKind == JsonValueKind.Object && submitted.GetProperty("PacketCounter").GetUInt32() == 0 &&
                    submitted.GetProperty("TouchMask").GetByte() == (active ? 2 : 0) && submitted.GetProperty("ClickMask").GetByte() == 0 &&
                    submitted.GetProperty("SourceTick").GetUInt64() == 140 && submitted.GetProperty("SampleSequence").GetUInt64() == ulong.MaxValue &&
                    submitted.GetProperty("NeutralCommand").GetBoolean() == !active && submitted.EnumerateObject().Count() == 6,
                    variant+": submit report remains independent of the cached report, including known neutral and counter wrap");
            }
            else
            {
                if (variant == "missing-primary" || variant.StartsWith("button-", StringComparison.Ordinal))
                    Check(cached.ValueKind == JsonValueKind.Object && cached.GetProperty("PacketCounter").GetUInt32() == 1 &&
                        cached.GetProperty("ButtonBitsHex").ValueKind == JsonValueKind.Null && cached.EnumerateObject().Count() == 4,
                        variant+": missing or malformed mask stays unknown without losing valid counter");
                else Check(cached.ValueKind == JsonValueKind.Null, variant+": unsupported or malformed cache stays unknown");
                Check(submitted.ValueKind == JsonValueKind.Null, variant+": no fabricated submitted report");
            }
            Check(sdk.TryGetProperty("PrimaryEndpointRequest",out var primary), variant+": bounded primary endpoint witness present");
            if (variant is "observed" or "empty-primary")
            {
                Check(primary.GetProperty("Address").GetByte() == 131, variant+": no substitution of other endpoint");
                Check(primary.GetProperty("PendingCount").GetInt32() == (variant == "observed" ? 2 : 0), variant+": exact pending count");
                var oldest = primary.GetProperty("OldestSequence");
                Check(variant == "observed" ? oldest.GetUInt32() == uint.MaxValue : oldest.ValueKind == JsonValueKind.Null,
                    variant+": sequence wrap or witnessed empty queue retained without inference");
            }
            else Check(primary.ValueKind == JsonValueKind.Null, variant+": absent, ambiguous, or malformed primary stays unknown");
            var p=sdk.GetProperty("ProducerProgress");
            if(variant=="observed")
            {
                Check(p.GetProperty("ProducerSharedSequenceBefore").GetUInt32()==uint.MaxValue && p.GetProperty("ProducerSharedSequenceAfter").GetUInt32()==0, "exact sequence values, no derived wrap interpretation");
                Check(p.GetProperty("IdleThreadIsAlive").GetBoolean()==false && p.GetProperty("InputWaitHandlePresent").GetBoolean(), "already observed booleans retained");
                Check(sdk.GetProperty("InputThreadIsAlive").GetBoolean() && !sdk.GetProperty("StopFlag").GetBoolean(), "input thread metadata preserved");
                Check(compact.GetProperty("stringsTruncated").GetInt32()==0 && compact.GetProperty("invalidScalarsOmitted").GetInt32()==0, "valid scalars unmodified");
            }
            else if(variant=="unsupported")
            {
                Check(sdk.GetProperty("Status").GetString()=="unsupported" && p.ValueKind==JsonValueKind.Null, "unsupported remains unknown, not false");
                Check(!sdk.TryGetProperty("InputThreadIsAlive",out _), "missing thread metadata not invented");
            }
            else if(variant=="bounded-malformed")
            {
                Check(p.GetProperty("Status").GetString()!.Length==96 && compact.GetProperty("stringsTruncated").GetInt32()==1, "hostile strings bounded and counted");
                Check(!p.TryGetProperty("PumpAcceptedSharedSequenceAfter",out _) && compact.GetProperty("invalidScalarsOmitted").GetInt32()==1, "non-scalar omitted and counted");
                Check(p.GetProperty("InputWaitHandlePresent").ValueKind==JsonValueKind.Null && !p.TryGetProperty("BulkMustNotCopy",out _), "unknown and whitelist retained");
            }
            Check(h.SdkReads==2 && h.Diagnostic.QueueCounters.Failed==0, variant+": one original SDK capture, no retry");
            IdentityWitnesses.Add(row);
        }
    }

    private static void NativeIdentity()
    {
        using var entered=new ManualResetEventSlim(); using var release=new ManualResetEventSlim();
        int tid=0, hr=-1; var description="YMCC owned metadata \n\""+new string('q',300);
        var thread=new Thread(()=> {tid=(int)GetCurrentThreadId();hr=SetThreadDescription(GetCurrentThread(),description);entered.Set();release.Wait();});
        thread.Start();
        try
        {
            Check(entered.Wait(5000),"owned thread entered"); Check(hr>=0,"self-owned description set");
            var identity=D.ReadThreadIdentity(tid);
            Check(identity.State=="observed-current-thread" && identity.OwnerPid==(uint)Environment.ProcessId,"held handle owner verified");
            Check(ulong.TryParse(identity.BirthFileTime,out var birth)&&birth>0,"exact nonzero thread FILETIME");
            Check(identity.Description==description[..256] && identity.DescriptionTruncated,"native description exact bounded prefix");
            using var process=System.Diagnostics.Process.GetCurrentProcess();
            for(int i=0;i<8;i++)D.ReadThreadIdentity(tid);
            process.Refresh();int before=process.HandleCount;
            var start=System.Diagnostics.Stopwatch.GetTimestamp();
            for(int i=0;i<64;i++)Check(D.ReadThreadIdentity(tid).State=="observed-current-thread","repeated held query "+i);
            var elapsed=System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            process.Refresh();int after=process.HandleCount;
            Check(after==before,"all 64 query handles returned");
            Check(D.ReadThreadIdentity(0).State=="invalid-tid","invalid TID rejected");
            var missing=D.ReadThreadIdentity(int.MaxValue);
            Check(missing.State=="open-failed" && missing.Win32Error is >0,"unavailable TID explicit error");
            IdentityWitnesses.Add(new{identity, missing, beforeHandles=before,afterHandles=after,queryCalls=64,elapsedMs=elapsed,
                scope="One owned named thread; no SDK, product, external process or CPU benefit observation"});
        }
        finally { release.Set();Check(thread.Join(5000),"owned named thread actually returned"); }
    }

    private static void ProductionIdentity()
    {
        using var entered=new ManualResetEventSlim(); using var go=new ManualResetEventSlim();
        using var done=new ManualResetEventSlim(); using var release=new ManualResetEventSlim();
        int tid=0; const string description="YMCC owned cost hot thread";
        var thread=new Thread(()=>
        {
            tid=(int)GetCurrentThreadId();SetThreadDescription(GetCurrentThread(),description);entered.Set();go.Wait();
            var start=System.Diagnostics.Stopwatch.GetTimestamp();
            while(System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds<80)Thread.SpinWait(100);
            done.Set();release.Wait();
        });
        var root=Path.Combine(Path.GetDirectoryName(Environment.GetCommandLineArgs()[0])!,"owned-production-metadata-"+Environment.ProcessId);
        Directory.CreateDirectory(root);File.WriteAllText(Path.Combine(root,"input-host-cost-enabled.flag"),"enabled");
        thread.Start(); D? diagnostic=null;
        try
        {
            Check(entered.Wait(5000),"production owned thread entered");diagnostic=new D(true,root);
            D.Window Window(ulong sequence)=>new(1700000000000+(long)sequence,Environment.TickCount64,1000,1,[],0,0,0,0,0,0,0,0,0,[],
                "Active","owned-production","owned-run",1,1,"xbox360","fixture-only",sequence,0,null,null,false);
            Check(diagnostic.TryRecord(Window(1)),"production baseline admitted");
            Check(SpinWait.SpinUntil(()=>diagnostic.QueueCounters.Completed+diagnostic.QueueCounters.Failed==1,5000),"production baseline settled");
            go.Set();Check(done.Wait(5000),"owned bounded CPU work completed");
            Check(diagnostic.TryRecord(Window(2)),"production measured window admitted");
            Check(SpinWait.SpinUntil(()=>diagnostic.QueueCounters.Completed+diagnostic.QueueCounters.Failed==2,5000),"production measured window settled");
            var rows=File.ReadAllLines(Path.Combine(root,"input-host-cost.log")).Select(v=>JsonDocument.Parse(v).RootElement.Clone()).ToArray();
            Check(rows.Length==2&&diagnostic.QueueCounters.Failed==0,"actual constructor sampler and file writer");
            var row=rows[1];var top=row.GetProperty("byThread").EnumerateArray().Single(r=>r.GetProperty("tid").GetInt32()==tid);
            Check(top.GetProperty("cpuMsPerSec").GetInt64()>0,"owned hot thread actual counter");
            Check(top.GetProperty("identity").GetProperty("description").GetString()==description,"production callback queried exact owned hot name");
            using var process=System.Diagnostics.Process.GetCurrentProcess();
            Check(row.GetProperty("writerBirthFileTime").GetString()==process.StartTime.ToFileTimeUtc().ToString(System.Globalization.CultureInfo.InvariantCulture),"production exact process FILETIME");
            Check(row.GetProperty("threadIdentityObservation").GetProperty("attempted").GetInt32()<=4,"production query cap");
            Check(!row.GetProperty("threadIdentityObservation").GetProperty("atomicWithCpuSample").GetBoolean(),"production attribution remains non-atomic");
            IdentityWitnesses.Add(row);
        }
        finally
        {
            if(diagnostic is not null){diagnostic.Dispose();Check(SpinWait.SpinUntil(()=>diagnostic.QueueCounters.Stopped,5000),"production diagnostic worker actually returned");}
            go.Set();release.Set();Check(thread.Join(5000),"production named thread actually returned");
        }
    }

    private static int IdentityMain(string[] args)
    {
#if YMCC_COST_THREAD_IDENTITY_BUDGET
        const string selection="record-budget";
#else
        const string selection="thread-identity";
#endif
        if(args.Length!=2 || args[0]!="--"+selection)return 64;
#if YMCC_COST_THREAD_IDENTITY_BUDGET
        var plan=new[]{"metadata record budget"};
        Action[] actions=[IdentityBudget];
#elif YMCC_COST_THREAD_IDENTITY_RESUME
        var plan=new[]{"metadata record budget","owned native thread metadata","production constructor metadata on owned process"};
        Action[] actions=[IdentityBudget,NativeIdentity,ProductionIdentity];
#else
        var plan=new[]{"disabled and re-enabled","thread departure/regression/observer role","bounded metadata projection","metadata record budget","owned native thread metadata","production constructor metadata on owned process"};
        Action[] actions=[Gate,Threads,IdentityProjection,IdentityBudget,NativeIdentity,ProductionIdentity];
#endif
        for(int i=0;i<actions.Length;i++)
        {Group(plan[i],actions[i]);if(!(bool)results[^1].GetType().GetProperty("pass")!.GetValue(results[^1])!)break;}
        int failures=results.Count(r=>!(bool)r.GetType().GetProperty("pass")!.GetValue(r)!);
        var report=new{pass=failures==0&&results.Count==plan.Length,selected=selection,expectedGroups=plan,groups=results.Count,checks,failures,results,
            witnesses=IdentityWitnesses,productionRuntimeSamplerCalls=selection=="record-budget"?0:2,actualNativeOwnedThreadQuery=selection!="record-budget",productHostOrSdkExecuted=false,deviceCalls=0,
            actualDispatcher=true,actualDiagnosticSource=true};
        using(var file=new FileStream(args[1],FileMode.CreateNew,FileAccess.Write,FileShare.None))JsonSerializer.Serialize(file,report,new JsonSerializerOptions{WriteIndented=true});
        Console.WriteLine(JsonSerializer.Serialize(new{report.pass,report.groups,checks,failures}));return report.pass?0:1;
    }
#endif

#if YMCC_COST_APPEND_RECEIPT
    // Explicit selected mode of this original fixture. Own files/locks only;
    // production default constructors sample this fixture process, never a device.
    private static class AppendReceiptProbe
    {
        private static readonly List<object> Checks = new();
        private static readonly List<string> WorkerReturns = new();
        private static void Check(string name, bool pass)
        {
            Checks.Add(new { name, pass });
            if (!pass) throw new InvalidOperationException(name);
        }
        private static void Until(Func<bool> state, string name)
        {
            if (!SpinWait.SpinUntil(state, 5000)) throw new TimeoutException(name);
        }
        private static string Root(string root, string name, bool enabled = true)
        {
            var path = Path.Combine(root, name); Directory.CreateDirectory(path);
            File.WriteAllText(Path.Combine(path, "input-host-cost-enabled.flag"), enabled ? "enabled" : "disabled");
            return path;
        }
        private static D.Window Window(ulong sequence) => new(
            1700000000000 + (long)sequence, Environment.TickCount64, 1000, 5, [30, 10, 20],
            40, 5, 30, 5, 9000, 5, 0, 0, 0, [], "Active", "fixture-cost-host", "fixture-run", 1, 1,
            "xbox360", "fixture-only", sequence, 0, null, null, false);
        private static YeManInputHost.Diagnostics.Baseline.MsiCostWindowDiagnostics.Window BeforeWindow() => new(
            1700000000000, Environment.TickCount64, 1000, 5, [30, 10, 20],
            40, 5, 30, 5, 9000, 5, 0, 0, 0, [], "Active", "fixture-cost-host", "fixture-run", 1, 1,
            "xbox360", "fixture-only", 1, 0, null, null, false);
        private static JsonElement Snapshot() => JsonSerializer.SerializeToElement(HostCostLog.Snapshot());
        private static void Post(D d, ulong sequence, long settled)
        {
            Check("candidate-admission-" + sequence, d.TryRecord(Window(sequence)));
            Until(() => d.QueueCounters.Completed + d.QueueCounters.Failed == settled, "cost callback settlement");
        }
        private static void Stop(D d, string label)
        {
            d.Dispose(); Until(() => d.QueueCounters.Stopped, "actual diagnostic thread return " + label);
            Until(() => !JsonSerializer.SerializeToElement(HostCostArchive.Snapshot()).GetProperty("workerQueued").GetBoolean(), "owned cleanup return " + label);
            WorkerReturns.Add(label);
        }
        private static void BeforeFalseCompletion(string root)
        {
            var path = Root(root, "before-locked"); var file = Path.Combine(path, "input-host-cost.log");
            const string sentinel = "{\"oldOwnedRow\":true}\n"; File.WriteAllText(file, sentinel);
            var d = new YeManInputHost.Diagnostics.Baseline.MsiCostWindowDiagnostics(true, path);
            try
            {
                using (var held = new FileStream(file, FileMode.Open, FileAccess.ReadWrite, FileShare.Read))
                {
                    Check("before-production-constructor-admitted", d.TryRecord(BeforeWindow()));
                    Until(() => d.QueueCounters.Completed + d.QueueCounters.Failed == 1, "old callback settlement");
                    Check("before-false-append-reproduces-completed-callback-without-write", d.QueueCounters.Completed == 1 && d.QueueCounters.Failed == 0);
                }
            }
            finally
            {
                d.Dispose(); Until(() => d.QueueCounters.Stopped, "old diagnostic thread return");
                Until(() => !JsonSerializer.SerializeToElement(YeManInputHost.Diagnostics.Baseline.HostCostArchive.Snapshot()).GetProperty("workerQueued").GetBoolean(), "old cleanup return");
                WorkerReturns.Add("before-locked");
            }
            Check("before-file-byte-sentinel-unchanged-despite-completed", File.ReadAllText(file) == sentinel);
        }
        private static void FailureThenRecovery(string root, bool mutexHeld)
        {
            var label = mutexHeld ? "candidate-contended" : "candidate-locked";
            var path = Root(root, label); var file = Path.Combine(path, "input-host-cost.log");
            const string sentinel = "{\"oldOwnedRow\":true}\n"; File.WriteAllText(file, sentinel);
            var d = new D(true, path);
            try
            {
                var before = Snapshot();
                if (mutexHeld)
                {
                    using var held = new Mutex(false, @"Local\YeManCC.CostLog." + HostCostLog.LockId(path));
                    if (!held.WaitOne(0)) throw new InvalidOperationException("fixture mutex already owned");
                    try { Post(d, 1, 1); }
                    finally { held.ReleaseMutex(); }
                }
                else
                {
                    using var held = new FileStream(file, FileMode.Open, FileAccess.ReadWrite, FileShare.Read);
                    Post(d, 1, 1);
                }
                var after = Snapshot(); var field = mutexHeld ? "contended" : "ioFailures";
                Check(label + "-writer-rejection-observed", after.GetProperty(field).GetInt64() > before.GetProperty(field).GetInt64());
                Check(label + "-callback-failed-and-not-completed", d.QueueCounters.Failed == 1 && d.QueueCounters.Completed == 0);
                Check(label + "-original-file-unchanged", File.ReadAllText(file) == sentinel);
                Post(d, 2, 2);
                var rows = File.ReadAllLines(file).Select(text => JsonDocument.Parse(text).RootElement.Clone()).ToArray();
                var cost = rows.Where(row => row.TryGetProperty("frames", out _)).ToArray();
                Check(label + "-later-new-window-only-one-write-no-replay", cost.Length == 1 &&
                    cost[0].GetProperty("msiCostWindowDeferred").GetProperty("requestedSequence").GetUInt64() == 2);
                Check(label + "-failed-window-baseline-reset-retains-unknown-rate", cost[0].GetProperty("cpuObservation").GetProperty("state").GetString() == "baseline" &&
                    cost[0].GetProperty("procCpuMsPerSec").ValueKind == JsonValueKind.Null);
                Check(label + "-subsequent-callback-completed-original-failure-preserved", d.QueueCounters.Completed == 1 && d.QueueCounters.Failed == 1);
                Check(label + "-20MB-single-active-no-ZIP", new FileInfo(file).Length <= HostCostLog.FileMaxBytes && !Directory.GetFiles(path, "*.zip").Any());
            }
            finally { Stop(d, label); }
        }
        private static void HealthyAndOff(string root, bool enabled)
        {
            var label = enabled ? "candidate-healthy" : "candidate-off";
            var path = Root(root, label, enabled); var file = Path.Combine(path, "input-host-cost.log");
            var d = new D(enabled, path);
            try
            {
                var before = Snapshot(); Post(d, 1, 1);
                Check(label + "-callback-normal-completion", d.QueueCounters.Completed == 1 && d.QueueCounters.Failed == 0);
                if (enabled)
                {
                    var rows = File.ReadAllLines(file);
                    Check("healthy-original-cost-payload-written", rows.Length == 1 && JsonDocument.Parse(rows[0]).RootElement.GetProperty("frames").GetInt64() == 5);
                }
                else
                {
                    var after = Snapshot();
                    Check("off-no-log-file-or-writer-failure", !File.Exists(file) &&
                        after.GetProperty("ioFailures").GetInt64() == before.GetProperty("ioFailures").GetInt64() &&
                        after.GetProperty("contended").GetInt64() == before.GetProperty("contended").GetInt64());
                }
            }
            finally { Stop(d, label); }
        }
        internal static int Run(string root)
        {
            Directory.CreateDirectory(root);
            try
            {
                BeforeFalseCompletion(root); FailureThenRecovery(root, false); FailureThenRecovery(root, true);
                HealthyAndOff(root, true); HealthyAndOff(root, false);
                Check("all-five-real-diagnostic-workers-returned", WorkerReturns.Count == 5);
                var assemblies = AppDomain.CurrentDomain.GetAssemblies().Where(a => !a.IsDynamic && !string.IsNullOrEmpty(a.Location))
                    .Select(a => new { name = a.GetName().Name, path = a.Location }).ToArray();
                var report = new { pass = true, checks = Checks, workerReturns = WorkerReturns,
                    actualProductionDefaultConstructors = true, actualDispatcherAndFileWriter = true,
                    runtimeObservationScope = "ReadRuntime observes only this owned fixture process;no controller/SDK/device instance",
                    originalRecordsReplayed = false, productHostOrDeviceExecuted = false, assemblies };
                File.WriteAllText(Path.Combine(root, "RESULT.json"), JsonSerializer.Serialize(report));
                Console.WriteLine(JsonSerializer.Serialize(new { pass = true, checks = Checks.Count, workersReturned = WorkerReturns.Count })); return 0;
            }
            catch (Exception error)
            {
                File.WriteAllText(Path.Combine(root, "RESULT.json"), JsonSerializer.Serialize(new { pass = false, checks = Checks, workerReturns = WorkerReturns, error = error.ToString() }));
                Console.Error.WriteLine(error); return 1;
            }
        }
    }
#endif

    internal static int Main(string[] args)
    {
#if YMCC_COST_THREAD_IDENTITY
        return IdentityMain(args);
#elif YMCC_COST_APPEND_RECEIPT
        if (args.Length != 2 || args[0] != "--append-write-result") return 64;
        return AppendReceiptProbe.Run(Path.GetFullPath(args[1]));
#else
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
#endif
    }
}
