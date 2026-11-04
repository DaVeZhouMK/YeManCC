using System.IO;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

// Exercises the production engine, not a duplicate curve-selection function.
// All engines use the explicit mock backend. Initial curve and successful Close
// are fixtures; no HC/WMI/ACPI calls or native IPC; loopback HTTP is exercised below.
var evidence = Path.GetFullPath(args[0]);
Directory.CreateDirectory(evidence);
Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", Path.Combine(evidence, "logs"));
var assembly = args.Length > 1
    ? new HostTestLoadContext(Path.GetFullPath(args[1])).LoadFromAssemblyPath(Path.GetFullPath(args[1]))
    : Assembly.Load("YeManFanHost");
if (args.Length > 1 && !string.Equals(assembly.Location, Path.GetFullPath(args[1]), StringComparison.OrdinalIgnoreCase))
    throw new InvalidOperationException("Test loaded a different Host assembly than the requested payload");
Type T(string name) => assembly.GetType("YeManFanHost." + name, true)!;
const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
object? Call(object obj, string name, params object?[] arguments)
{
    try {
        var method = obj.GetType().GetMethods(flags).Single(m => m.Name == name && m.GetParameters().Length >= arguments.Length && m.GetParameters().Skip(arguments.Length).All(p => p.IsOptional));
        var supplied = arguments.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - arguments.Length)).ToArray();
        return method.Invoke(obj, supplied);
    }
    catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
}
object? Field(object obj, string name) => obj.GetType().GetField(name, flags)!.GetValue(obj);
void SetField(object obj, string name, object? value) => obj.GetType().GetField(name, flags)!.SetValue(obj, value);
void SetProperty(object obj, string name, object? value) => obj.GetType().GetProperty(name, flags)!.SetValue(obj, value);
string P(object obj, string name) => obj.GetType().GetProperty(name, flags)!.GetValue(obj)?.ToString() ?? "null";
T("Program").GetProperty("IsSelfTestProcess", flags)!.SetValue(null, true);
var diagnosticType = T("HostDiagnosticLog");
diagnosticType.GetProperty("SelfTestCaptureEnabled", flags)!.SetValue(null, true);
List<string> Events() => ((IEnumerable)diagnosticType.GetProperty("SelfTestCaptured", flags)!.GetValue(null)!).Cast<object>().Select(x => (string)x.GetType().GetField("Item1")!.GetValue(x)!).ToList();
void ResetEvents() => diagnosticType.GetMethod("ResetSelfTestCapture", flags)!.Invoke(null, null);
Array Curve(params double[] duties)
{
    var result = Array.CreateInstance(T("FanNode"), duties.Length);
    var temps = new double[] { 0, 44, 78, 100 };
    for (int i = 0; i < duties.Length; i++) result.SetValue(Activator.CreateInstance(T("FanNode"), temps[i], duties[i]), i);
    return result;
}
string CurveJson(object? curve) => JsonSerializer.Serialize(curve, curve?.GetType() ?? typeof(object), new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase });
var checks = new List<object>();
bool ok = true;
void Check(string name, bool passed)
{
    checks.Add(new { name, passed });
    if (!passed) ok = false;
    Console.WriteLine((passed ? "PASS " : "FAIL ") + name);
}
(object engine, object options) NewEngine(int delay = 0)
{
    var argv = new[] { "--mock-handshake", "--mock-resume-rebuild-delay-ms", delay.ToString() };
    var options = T("HostOptions").GetMethod("Parse", flags)!.Invoke(null, new object[] { argv })!;
    var diagnostics = Activator.CreateInstance(diagnosticType, flags, null, new object[] { Path.Combine(evidence, "logs"), 0 }, null)!;
    var engine = Activator.CreateInstance(T("FanHostEngine"), flags, null, new[] { options, diagnostics }, null)!;
    return (engine, options);
}
void SeedActive(object engine, Array curve)
{
    SetField(engine, "activeCurve", curve);
    var state = Field(engine, "state")!;
    SetProperty(state, "ActiveCurve", curve);
    SetProperty(state, "State", "Ready");
    SetProperty(state, "PowerState", "On");
}
// The mock enable records simulated writes. Before Close, declare the fixture's
// actual zero hardware writes, allowing the existing safe Close boundary.
void SafeCloseFixture(object engine) => SetProperty(Field(engine, "state")!, "HardwareWritesObserved", false);
bool Wait(Func<bool> predicate, int ms = 4000)
{
    var timer = Stopwatch.StartNew();
    while (timer.ElapsedMilliseconds < ms) { if (predicate()) return true; Thread.Sleep(10); }
    return predicate();
}
void DisposeFixture(object engine)
{
    SafeCloseFixture(engine);
    SetProperty(Field(engine, "state")!, "HardwareWritesEnabled", false);
    Call(engine, "Dispose");
}

// Journal-only extension of this original production-engine consumer. No Host
// Main, HTTP server, Node, HC runtime, native API or device is started here.
if (args.Length > 4 && args[4] == "journals")
{
    var failures=new List<object>();var results=new List<object>();
    var nativeAttempts=new List<string>();
    System.Runtime.InteropServices.NativeLibrary.SetDllImportResolver(assembly,(name,_,_)=>{lock(nativeAttempts)nativeAttempts.Add(name);throw new DllNotFoundException("FAN_JOURNAL_NATIVE_FORBIDDEN");});
    void Assert(bool value,string message="assertion failed"){if(!value)throw new Exception(message);}
    void Case(string name,Action body)
    {
        try{body();results.Add(new {name,passed=true});Console.WriteLine("PASS journal "+name);}
        catch(Exception e){failures.Add(new {name,error=e.ToString()});results.Add(new {name,passed=false,error=e.Message});Console.WriteLine("FAIL journal "+name+": "+e.Message);}
    }
    (object engine,string dir) JournalEngine(string name,bool takeover=false)
    {
        var dir=Path.Combine(evidence,name+"-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
        Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",dir);
        var argv=new List<string>{"--mock-handshake","--parent-pid",Environment.ProcessId.ToString()};
        if(takeover){argv.Add("--successor-takeover");argv.Add("--predecessor-instance");argv.Add("old-host");}
        var opt=T("HostOptions").GetMethod("Parse",flags)!.Invoke(null,new object[]{argv.ToArray()})!;
        var diag=Activator.CreateInstance(diagnosticType,flags,null,new object[]{dir,0},null)!;
        var e=Activator.CreateInstance(T("FanHostEngine"),flags,null,new[]{opt,diag},null)!;
        return(e,dir);
    }
    string CyclePath(string dir)=>Path.Combine(dir,"YeManFanHost.recovery-cycle.json");
    string IntentPath(string dir)=>Path.Combine(dir,"YeManFanHost.control-intent.json");
    string Cycle(int attempts=7,int replacements=0)=>JsonSerializer.Serialize(new {cycleId="journal-cycle",parentPid=Environment.ProcessId,startedAtUtc=DateTime.UtcNow.AddSeconds(-1),attempts,replacements,terminal=false,completed=false,lastReason="fixture",lastUpdatedAtUtc=DateTime.UtcNow});
    string Intent()=>JsonSerializer.Serialize(new {cycleId="journal-cycle",predecessorInstanceId="old-host",curve=new[]{new {tempC=0,dutyPercent=34},new {tempC=44,dutyPercent=52},new {tempC=78,dutyPercent=71},new {tempC=100,dutyPercent=93}},curveNodes=4,hardwareWritesObserved=true,persistedAtUtc=DateTimeOffset.UtcNow});
    object? Decode(string type,string text)=>JsonSerializer.Deserialize(text,T("FanHostEngine").GetNestedType(type,BindingFlags.NonPublic)!,new JsonSerializerOptions(JsonSerializerDefaults.Web));
    bool Ensure(object e)=>(bool)Call(e,"EnsureRecoveryCycleLocked","journal-consumer")!;
    bool Bound(object e){object?[] a={null,null};return (bool)T("FanHostEngine").GetMethod("TryReadBoundTakeoverIntent",flags)!.Invoke(e,a)!;}
    void NoWrites(object e){Assert(Field(e,"realBackend") is null);Assert(P(Field(e,"state")!,"HardwareWritesObserved")=="False");Assert(nativeAttempts.Count==0);}
    Dictionary<string,string> Bad(string valid,string[] fields)
    {
        var bad=new Dictionary<string,string>{{"empty",""},{"whitespace"," \r\n"},{"truncated",valid[..^2]},{"garbage","{"},{"null","null"},{"wrong-root","[]"},{"empty-object","{}"},{"too-large",new string(' ',1024*1024+1)}};
        foreach(var field in fields)
        {
            var n=System.Text.Json.Nodes.JsonNode.Parse(valid)!.AsObject();n.Remove(field);bad["missing-"+field]=n.ToJsonString();
            n=System.Text.Json.Nodes.JsonNode.Parse(valid)!.AsObject();n[field]=new System.Text.Json.Nodes.JsonArray(1);bad["type-"+field]=n.ToJsonString();
        }
        foreach(var (field,value) in new[]{("schemaVersion","2"),("schemaVersion","\"1\""),("schema","2")}){var n=System.Text.Json.Nodes.JsonNode.Parse(valid)!.AsObject();n[field]=System.Text.Json.Nodes.JsonNode.Parse(value);bad[field+value]=n.ToJsonString();}
        return bad;
    }
    void Stop(object e){Call(e,"StopRecoveryRetry");SafeCloseFixture(e);try{Call(e,"Dispose");}catch{}}
    foreach(var pair in Bad(Cycle(),new[]{"cycleId","parentPid","startedAtUtc","attempts","replacements","terminal","completed","lastUpdatedAtUtc"}))
    Case("cycle-"+pair.Key,()=>{var(e,dir)=JournalEngine("bad-cycle");try{File.WriteAllText(CyclePath(dir),pair.Value);var bytes=File.ReadAllBytes(CyclePath(dir));SetField(e,"recoveryRetryAttempt",19);
        for(int i=0;i<8;i++){SetField(e,"recoveryCycleExplicitRetryRequested",i%2==0);Assert(!Ensure(e),"bad record reopened budget");Assert(P(Field(e,"state")!,"UnknownState")=="True");Assert(P(Field(e,"state")!,"State")=="FaultLocked");Assert((int)Field(e,"recoveryRetryAttempt")!>=19);}
        Assert(File.ReadAllBytes(CyclePath(dir)).SequenceEqual(bytes),"bad cycle original replaced");NoWrites(e);
    }finally{Stop(e);}});
    foreach(var pair in Bad(Intent(),new[]{"cycleId","predecessorInstanceId","curve","curveNodes","hardwareWritesObserved","persistedAtUtc"}))
    Case("intent-"+pair.Key,()=>{var(e,dir)=JournalEngine("bad-intent",true);try{File.WriteAllText(CyclePath(dir),Cycle());File.WriteAllText(IntentPath(dir),pair.Value);var bytes=File.ReadAllBytes(IntentPath(dir));
        Assert(!Bound(e),"invalid intent was authorized");Call(e,"RunTakeoverRecovery");Assert(P(Field(e,"state")!,"UnknownState")=="True","malformed intent was not unknown");Assert(Field(e,"activeCurve") is null);
        Call(e,"CancelControlIntentUnlocked","journal-test");Assert(File.ReadAllBytes(IntentPath(dir)).SequenceEqual(bytes),"invalid intent deleted during OFF");Assert(Field(e,"controlIntentCancellationError") is not null);NoWrites(e);
    }finally{Stop(e);}});
    foreach(var (field,value) in new[]{("attempts","-1"),("attempts","2147483647"),("replacements","-1"),("replacements","2147483647"),("startedAtUtc",JsonSerializer.Serialize(DateTime.UtcNow.AddHours(1))),("cycleId","\"\""),("parentPid","-1")})
    Case("cycle-semantic-"+field+(field=="startedAtUtc"?"-future-clock":value),()=>{var(e,dir)=JournalEngine("semantic");try{var n=System.Text.Json.Nodes.JsonNode.Parse(Cycle())!.AsObject();n[field]=System.Text.Json.Nodes.JsonNode.Parse(value);File.WriteAllText(CyclePath(dir),n.ToJsonString());var bytes=File.ReadAllBytes(CyclePath(dir));Assert(!Ensure(e),"invalid budget accepted");Assert(File.ReadAllBytes(CyclePath(dir)).SequenceEqual(bytes));NoWrites(e);}finally{Stop(e);}});
    Case("old host intent is rejected before any Open",()=>{var(e,dir)=JournalEngine("old-owner",true);try{File.WriteAllText(CyclePath(dir),Cycle());var n=System.Text.Json.Nodes.JsonNode.Parse(Intent())!.AsObject();n["predecessorInstanceId"]="other-host";File.WriteAllText(IntentPath(dir),n.ToJsonString());Assert(!Bound(e));Call(e,"RunTakeoverRecovery");Assert(Field(e,"activeCurve") is null);NoWrites(e);}finally{Stop(e);}});
    Case("locked record cannot become missing or reset attempts",()=>{var(e,dir)=JournalEngine("locked");try{File.WriteAllText(CyclePath(dir),Cycle());using(var held=new FileStream(CyclePath(dir),FileMode.Open,FileAccess.ReadWrite,FileShare.None))Assert(!Ensure(e));SetField(e,"recoveryCycleExplicitRetryRequested",true);Assert(Ensure(e),"readable explicit retry did not recover");NoWrites(e);}finally{Stop(e);}});
    Case("valid legacy cycle inherits attempts and unknown fields",()=>{var(e,dir)=JournalEngine("legacy");try{var n=System.Text.Json.Nodes.JsonNode.Parse(Cycle())!.AsObject();n["futureAnnotation"]=new System.Text.Json.Nodes.JsonObject{{"keep",true}};File.WriteAllText(CyclePath(dir),n.ToJsonString(),new System.Text.UTF8Encoding(true));Assert(Ensure(e));using var doc=JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));Assert(doc.RootElement.GetProperty("attempts").GetInt32()==7);Assert(doc.RootElement.GetProperty("replacements").GetInt32()==1);Assert(doc.RootElement.TryGetProperty("futureAnnotation",out _),"unknown cycle data lost");NoWrites(e);}finally{Stop(e);}});
    Case("exhausted cycle cannot restart automatically",()=>{var(e,dir)=JournalEngine("exhausted");try{File.WriteAllText(CyclePath(dir),Cycle(30));for(int i=0;i<5;i++)Assert(!Ensure(e));Assert((int)Field(e,"recoveryRetryAttempt")!<=30);NoWrites(e);}finally{Stop(e);}});
    Case("live memory cannot ignore a newly corrupt disk journal",()=>{var(e,dir)=JournalEngine("new-corrupt");try{File.WriteAllText(CyclePath(dir),Cycle());Assert(Ensure(e));var held=Field(e,"recoveryCycle");File.WriteAllText(CyclePath(dir),"{");Assert(!Ensure(e),"in-memory cycle ignored new corruption");Assert(ReferenceEquals(held,Field(e,"recoveryCycle")),"known debt was discarded");Assert(File.ReadAllText(CyclePath(dir))=="{");NoWrites(e);}finally{Stop(e);}});
    Case("failed publish cannot admit inheritance or zero remaining budget",()=>{var(e,dir)=JournalEngine("write-failure");try{File.WriteAllText(CyclePath(dir),Cycle());using(var held=new FileStream(CyclePath(dir),FileMode.Open,FileAccess.Read,FileShare.Read))Assert(!Ensure(e),"failed durable write reported successful adoption");Assert(Field(e,"recoveryCycle") is not null);NoWrites(e);}finally{Stop(e);}});
    Case("intent writer preserves valid extension data",()=>{var(e,dir)=JournalEngine("intent-extra");try{var node=System.Text.Json.Nodes.JsonNode.Parse(Intent())!.AsObject();node["predecessorInstanceId"]=Field(e,"engineInstanceId")!.ToString();node["futureAnnotation"]="keep";File.WriteAllText(IntentPath(dir),node.ToJsonString());SetField(e,"recoveryCycle",Decode("RecoveryCycleRecord",Cycle()));SetField(e,"activeCurve",Curve(34,52,71,93));Call(e,"PersistControlIntentForSuccessor");using var doc=JsonDocument.Parse(File.ReadAllText(IntentPath(dir)));Assert(doc.RootElement.TryGetProperty("futureAnnotation",out _),"intent extension data lost");Assert(doc.RootElement.GetProperty("curveNodes").GetInt32()==4);NoWrites(e);}finally{Stop(e);}});
    Case("intent writer cannot overwrite malformed original",()=>{var(e,dir)=JournalEngine("intent-write-bad");try{File.WriteAllText(IntentPath(dir),"{");SetField(e,"recoveryCycle",Decode("RecoveryCycleRecord",Cycle()));SetField(e,"activeCurve",Curve(34,52,71,93));Call(e,"PersistControlIntentForSuccessor");Assert(File.ReadAllText(IntentPath(dir))=="{","intent writer replaced damaged original");NoWrites(e);}finally{Stop(e);}});
    Case("locked OFF reports cancellation failure and preserves intent",()=>{var(e,dir)=JournalEngine("off-lock");try{var n=System.Text.Json.Nodes.JsonNode.Parse(Intent())!.AsObject();n["predecessorInstanceId"]=Field(e,"engineInstanceId")!.ToString();File.WriteAllText(IntentPath(dir),n.ToJsonString());var bytes=File.ReadAllBytes(IntentPath(dir));using(var held=new FileStream(IntentPath(dir),FileMode.Open,FileAccess.Read,FileShare.Read)){Call(e,"CancelControlIntentUnlocked","test");Assert(Field(e,"controlIntentCancellationError") is not null);Assert(File.ReadAllBytes(IntentPath(dir)).SequenceEqual(bytes));}Call(e,"CancelControlIntentUnlocked","explicit-retry");Assert(!File.Exists(IntentPath(dir)));NoWrites(e);}finally{Stop(e);}});
    Case("malformed explicit retry storm preserves budget and original",()=>{var(e,dir)=JournalEngine("storm");try{File.WriteAllText(CyclePath(dir),"{");SetField(e,"recoveryRetryAttempt",30);SetField(e,"recoveryRetryWindowTerminal",true);for(int i=0;i<64;i++){SetField(e,"recoveryCycleExplicitRetryRequested",true);Call(e,"BeginExplicitRecoveryRetryWindow");Assert(!Ensure(e));Assert((int)Field(e,"recoveryRetryAttempt")!>=30,"retry storm reset attempts");}Assert(File.ReadAllText(CyclePath(dir))=="{");NoWrites(e);}finally{Stop(e);}});
    Case("missing after known or unknown debt cannot start a fresh cycle",()=>{foreach(bool corrupt in new[]{false,true}){var(e,dir)=JournalEngine("missing-debt");try{File.WriteAllText(CyclePath(dir),corrupt?"{":Cycle());if(corrupt)Assert(!Ensure(e));else Assert(Ensure(e));File.Delete(CyclePath(dir));SetField(e,"recoveryCycleExplicitRetryRequested",true);Assert(!Ensure(e),"missing file erased unresolved debt");NoWrites(e);}finally{Stop(e);}}});
    Case("valid bound takeover remains the exact requested curve",()=>{var(e,dir)=JournalEngine("valid-takeover",true);try{File.WriteAllText(CyclePath(dir),Cycle());File.WriteAllText(IntentPath(dir),Intent());Assert(Bound(e));Assert(Ensure(e));object?[] a={null};var restored=(bool)T("FanHostEngine").GetMethod("RestoreControlIntentAfterTakeover",flags)!.Invoke(e,a)!;Assert(restored&&CurveJson(Field(e,"activeCurve"))==CurveJson(Curve(34,52,71,93)));Assert(nativeAttempts.Count==0);}finally{Stop(e);}});
    Case("foreign valid intent cannot be overwritten or deleted",()=>{var(e,dir)=JournalEngine("foreign-intent");try{File.WriteAllText(IntentPath(dir),Intent());var bytes=File.ReadAllBytes(IntentPath(dir));SetField(e,"recoveryCycle",Decode("RecoveryCycleRecord",Cycle()));SetField(e,"activeCurve",Curve(34,52,71,93));Call(e,"PersistControlIntentForSuccessor");Assert(File.ReadAllBytes(IntentPath(dir)).SequenceEqual(bytes));Call(e,"CancelControlIntentUnlocked","explicit-off");Assert(File.ReadAllBytes(IntentPath(dir)).SequenceEqual(bytes)&&Field(e,"controlIntentCancellationError") is not null);NoWrites(e);}finally{Stop(e);}});
    Case("read-only cycle blocks new publication and retains consumed budget",()=>{var(e,dir)=JournalEngine("readonly-cycle");try{File.WriteAllText(CyclePath(dir),Cycle());var bytes=File.ReadAllBytes(CyclePath(dir));File.SetAttributes(CyclePath(dir),FileAttributes.ReadOnly);try{Assert(!Ensure(e));Assert(File.ReadAllBytes(CyclePath(dir)).SequenceEqual(bytes));Assert(Field(e,"recoveryCycle") is not null);Assert(!Directory.EnumerateFiles(dir,"*.new").Any());}finally{File.SetAttributes(CyclePath(dir),FileAttributes.Normal);}SetField(e,"recoveryCycleExplicitRetryRequested",true);Assert(Ensure(e));NoWrites(e);}finally{Stop(e);}});
    Case("directory journal is not absence",()=>{var(e,dir)=JournalEngine("directory-cycle");try{Directory.CreateDirectory(CyclePath(dir));Assert(!Ensure(e)&&Directory.Exists(CyclePath(dir)));NoWrites(e);}finally{Stop(e);}});
    Case("first attempt is durable before the retry timer exists",()=>{var(e,dir)=JournalEngine("first-attempt");try{SetField(e,"recoveryRetryWindowStartedMonoMs",Environment.TickCount64);File.WriteAllText(CyclePath(dir),Cycle(0));SetField(e,"recoveryCycle",Decode("RecoveryCycleRecord",File.ReadAllText(CyclePath(dir))));Call(e,"ScheduleRecoveryRetry");using var doc=JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));Assert(doc.RootElement.GetProperty("attempts").GetInt32()==1,"first attempt was not durably accounted");Assert((int)Field(e,"recoveryRetryAttempt")! == 1);NoWrites(e);}finally{Stop(e);}});
    Case("restart cannot regain a previously consumed replacement",()=>{var(e,dir)=JournalEngine("replacement-cap");try{File.WriteAllText(CyclePath(dir),Cycle(29,1));Assert(!Ensure(e));using var doc=JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));Assert(doc.RootElement.GetProperty("attempts").GetInt32()==29&&doc.RootElement.GetProperty("replacements").GetInt32()==1);NoWrites(e);}finally{Stop(e);}});
    Case("legacy node annotations and UTF16 BOM survive intent publication",()=>{var(e,dir)=JournalEngine("legacy-nodes");try{var n=System.Text.Json.Nodes.JsonNode.Parse(Intent())!.AsObject();n["predecessorInstanceId"]=Field(e,"engineInstanceId")!.ToString();n["curve"]![1]!["futureNode"]=42;string text=n.ToJsonString().Replace("\"curve\":","\"Curve\":");File.WriteAllText(IntentPath(dir),text,System.Text.Encoding.Unicode);SetField(e,"recoveryCycle",Decode("RecoveryCycleRecord",Cycle()));SetField(e,"activeCurve",Curve(34,52,71,93));Assert(Call(e,"PersistControlIntentForSuccessor") is not bool written||written);using var doc=JsonDocument.Parse(File.ReadAllText(IntentPath(dir)));Assert(doc.RootElement.GetProperty("curve")[1].GetProperty("futureNode").GetInt32()==42);NoWrites(e);}finally{Stop(e);}});
    Case("partial temporary files cannot become an inherited cycle",()=>{var(e,dir)=JournalEngine("partial-temp");try{string path=CyclePath(dir)+".interrupted.new";File.WriteAllText(path,"{");Assert(Ensure(e));Assert(File.ReadAllText(path)=="{");using var doc=JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));Assert(doc.RootElement.GetProperty("attempts").GetInt32()==0);NoWrites(e);}finally{Stop(e);}});
    Case("bad journal does not erase physical return evidence or crash snapshot reads",()=>{var(e,dir)=JournalEngine("debt");try{var state=Field(e,"state")!;SetProperty(state,"HardwareWritesObserved",true);SetProperty(state,"OemRestoreEvidence","pending-owned-return");File.WriteAllText(CyclePath(dir),"{");Assert(!Ensure(e));var snapshot=Call(e,"SnapshotForRead")!;Assert(P(snapshot,"UnknownState")=="True"&&P(snapshot,"State")=="FaultLocked");Assert(P(snapshot,"HardwareWritesObserved")=="True"&&P(snapshot,"OemRestoreEvidence")=="pending-owned-return");Assert(Field(e,"activeCurve") is null&&nativeAttempts.Count==0);}finally{Stop(e);}});
    foreach (var failure in new[] { "corrupt", "missing", "locked", "read-only", "foreign-cycle" })
    Case("uncommitted completion retains debt: " + failure, () =>
    {
        var (e, dir) = JournalEngine("completion-" + failure);
        FileStream? held = null;
        try
        {
            File.WriteAllText(CyclePath(dir), Cycle());
            SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", File.ReadAllText(CyclePath(dir))));
            var state = Field(e, "state")!;
            SetProperty(state, "HardwareWritesObserved", true);
            SetProperty(state, "OemRestoreEvidence", "pending-owned-return");
            if (failure == "corrupt") File.WriteAllText(CyclePath(dir), "{");
            if (failure == "missing") File.Delete(CyclePath(dir));
            if (failure == "foreign-cycle") File.WriteAllText(CyclePath(dir), Cycle().Replace("journal-cycle", "foreign-cycle"));
            if (failure == "locked") held = new FileStream(CyclePath(dir), FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            if (failure == "read-only") File.SetAttributes(CyclePath(dir), FileAttributes.ReadOnly);
            var before = held is null && File.Exists(CyclePath(dir)) ? File.ReadAllBytes(CyclePath(dir)) : null;
            ResetEvents();
            Call(e, "CompleteRecoveryCycle", "journal-consumer");
            Assert(P(Field(e, "recoveryCycle")!, "Completed") == "False", "failed save marked cycle completed");
            Assert(!Events().Contains("recovery.cycle-completed"), "uncommitted completion was reported");
            Assert(P(state, "UnknownState") == "True" && P(state, "State") == "FaultLocked");
            Assert(P(state, "HardwareWritesObserved") == "True" && P(state, "OemRestoreEvidence") == "pending-owned-return");
            if (before is not null) Assert(File.ReadAllBytes(CyclePath(dir)).SequenceEqual(before), "original completion proof replaced");
            if (failure == "missing") Assert(!File.Exists(CyclePath(dir)), "missing debt was silently recreated as complete");
            Assert(nativeAttempts.Count == 0);
        }
        finally
        {
            held?.Dispose();
            if (File.Exists(CyclePath(dir))) File.SetAttributes(CyclePath(dir), FileAttributes.Normal);
            Stop(e);
        }
    });
    Case("valid completion publishes before the completed event", () =>
    {
        var (e, dir) = JournalEngine("valid-completion");
        try
        {
            File.WriteAllText(CyclePath(dir), Cycle());
            SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", File.ReadAllText(CyclePath(dir))));
            ResetEvents();
            Call(e, "CompleteRecoveryCycle", "journal-consumer");
            using var doc = JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));
            Assert(doc.RootElement.GetProperty("completed").GetBoolean());
            Assert(doc.RootElement.GetProperty("attempts").GetInt32() == 7 && Events().Contains("recovery.cycle-completed"));
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    foreach (var intent in new[] { false, true })
    foreach (var defect in new[] { "invalid-utf8", "duplicate-case", "one-over-limit" })
    Case((intent ? "intent-" : "cycle-") + defect, () =>
    {
        var (e, dir) = JournalEngine("encoded-" + defect, intent);
        try
        {
            File.WriteAllText(CyclePath(dir), Cycle());
            var path = intent ? IntentPath(dir) : CyclePath(dir);
            var valid = intent ? Intent() : Cycle();
            byte[] bytes = defect switch
            {
                "invalid-utf8" => System.Text.Encoding.UTF8.GetBytes(valid[..^1] + ",\"opaque\":\"").Concat(new byte[] { 0xff }).Concat(System.Text.Encoding.UTF8.GetBytes("\"}")).ToArray(),
                "duplicate-case" => System.Text.Encoding.UTF8.GetBytes(valid[..^1] + ",\"CycleId\":\"duplicate\"}"),
                _ => System.Text.Encoding.UTF8.GetBytes(valid.PadRight(64 * 1024 + 1)),
            };
            File.WriteAllBytes(path, bytes);
            Assert(intent ? !Bound(e) : !Ensure(e), "invalid actual bytes accepted");
            Assert(File.ReadAllBytes(path).SequenceEqual(bytes));
            Assert(P(Field(e, "state")!, "UnknownState") == "True");
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    foreach (var failure in new[] { "locked", "read-only" })
    Case("intent publication " + failure + " preserves old proof", () =>
    {
        var (e, dir) = JournalEngine("intent-publish-" + failure);
        FileStream? held = null;
        try
        {
            SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", Cycle()));
            SetField(e, "activeCurve", Curve(34, 52, 71, 93));
            File.WriteAllText(IntentPath(dir), Intent().Replace("old-host", Field(e, "engineInstanceId")!.ToString()));
            var bytes = File.ReadAllBytes(IntentPath(dir));
            if (failure == "locked") held = new FileStream(IntentPath(dir), FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            else File.SetAttributes(IntentPath(dir), FileAttributes.ReadOnly);
            Assert(Call(e, "PersistControlIntentForSuccessor") is not bool saved || !saved, "unconfirmed intent publication accepted");
            held?.Dispose(); held = null;
            Assert(File.ReadAllBytes(IntentPath(dir)).SequenceEqual(bytes));
            Assert(P(Field(e, "state")!, "UnknownState") == "True", "failed intent publication did not enter unknown state");
            Assert(!Directory.EnumerateFiles(dir, "*.new").Any());
            NoWrites(e);
        }
        finally
        {
            held?.Dispose();
            File.SetAttributes(IntentPath(dir), FileAttributes.Normal);
            Stop(e);
        }
    });
    Case("explicit nonterminal retry cannot roll back consumed attempts", () =>
    {
        var (e, dir) = JournalEngine("explicit-high-water");
        try
        {
            File.WriteAllText(CyclePath(dir), Cycle(17));
            Assert(Ensure(e));
            SetField(e, "recoveryCycleExplicitRetryRequested", true);
            Call(e, "BeginExplicitRecoveryRetryWindow");
            Call(e, "ScheduleRecoveryRetry");
            using var doc = JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));
            Assert(doc.RootElement.GetProperty("attempts").GetInt32() == 18, "explicit retry reset an active cycle's attempts");
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    Case("durable higher budget cannot be overwritten by stale memory", () =>
    {
        var (e, dir) = JournalEngine("durable-high-water");
        try
        {
            var initial = Cycle(7);
            File.WriteAllText(CyclePath(dir), initial);
            SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", initial));
            SetField(e, "recoveryRetryAttempt", 7);
            var n = System.Text.Json.Nodes.JsonNode.Parse(initial)!.AsObject(); n["attempts"] = 23; n["replacements"] = 1;
            File.WriteAllText(CyclePath(dir), n.ToJsonString());
            Call(e, "ScheduleRecoveryRetry");
            using var doc = JsonDocument.Parse(File.ReadAllText(CyclePath(dir)));
            Assert(doc.RootElement.GetProperty("attempts").GetInt32() >= 24 && doc.RootElement.GetProperty("replacements").GetInt32() == 1, "higher durable budget was lost");
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    Case("same cycle text cannot replace the retained parent identity or clock", () =>
    {
        var (e, dir) = JournalEngine("retained-parent");
        try
        {
            var initial = Cycle();
            File.WriteAllText(CyclePath(dir), initial);
            SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", initial));
            var n = System.Text.Json.Nodes.JsonNode.Parse(initial)!.AsObject(); n["parentPid"] = Environment.ProcessId + 1;
            File.WriteAllText(CyclePath(dir), n.ToJsonString());
            Assert(!Ensure(e), "changed parent identity was accepted as the same cycle");
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    Case("accepted successor can explicitly cancel its inherited intent", () =>
    {
        var (e, dir) = JournalEngine("inherited-off", true);
        try
        {
            File.WriteAllText(CyclePath(dir), Cycle());
            File.WriteAllText(IntentPath(dir), Intent());
            Call(e, "RunTakeoverRecovery");
            Assert(Field(e, "activeCurve") is not null, "valid mock takeover did not adopt the curve");
            SafeCloseFixture(e);
            Call(e, "CancelControlIntentUnlocked", "explicit-off");
            Assert(!File.Exists(IntentPath(dir)) && Field(e, "controlIntentCancellationError") is null, "accepted inherited OFF was refused as a foreign owner");
            Assert(File.Exists(CyclePath(dir)), "OFF discarded the cycle debt");
            NoWrites(e);
        }
        finally { Stop(e); }
    });
    foreach (var cold in new[] { false, true })
    foreach (var failWrite in new[] { false, true })
    Case($"legitimate terminal explicit retry cold={cold} writeFails={failWrite}", () =>
    {
        var (e, dir) = JournalEngine("terminal-explicit");
        try
        {
            var original = Cycle(30, 1);
            File.WriteAllText(CyclePath(dir), original);
            if (!cold) SetField(e, "recoveryCycle", Decode("RecoveryCycleRecord", original));
            SetField(e, "recoveryRetryAttempt", 30);
            SetField(e, "recoveryCycleExplicitRetryRequested", true);
            if (failWrite) File.SetAttributes(CyclePath(dir), FileAttributes.ReadOnly);
            Call(e, "BeginExplicitRecoveryRetryWindow");
            Assert(Ensure(e) == !failWrite);
            var retained = Field(e, "recoveryCycle")!;
            if (failWrite)
            {
                Assert(P(retained, "CycleId") == "journal-cycle" && P(retained, "Attempts") == "30" && P(retained, "Replacements") == "1", "failed new episode erased the retained old debt");
                Assert(File.ReadAllText(CyclePath(dir)) == original);
            }
            else
            {
                Assert(P(retained, "CycleId") != "journal-cycle" && P(retained, "Attempts") == "0" && P(retained, "Replacements") == "0");
                Assert((int)Field(e, "recoveryRetryAttempt")! == 0, "valid new episode could not retry");
            }
            NoWrites(e);
        }
        finally { File.SetAttributes(CyclePath(dir), FileAttributes.Normal); Stop(e); }
    });
    var journalResult=new {ok=failures.Count==0,count=results.Count,checks=results,failures,nativeAttempts,physicalWrites=0,networkUsed=false,productionHostMainInvoked=false,hostAssemblyPath=assembly.Location,hostAssemblySha256=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(assembly.Location))).ToLowerInvariant(),scope="journal-only; original reflection consumer and production engine mock backend; owned paths; no HTTP/Node/native/device"};
    File.WriteAllText(Path.Combine(evidence,"result.json"),JsonSerializer.Serialize(journalResult,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"JOURNALS ok={journalResult.ok} checks={journalResult.count} failed={failures.Count}");return journalResult.ok?0:1;
}

try
{
    var desired = Curve(34, 52, 71, 93);
    var (engine, options) = NewEngine(400);
    SeedActive(engine, desired);
    Call(engine, "SuspendForSystemPower");
    Check("first-sleep-freezes-exact-user-curve", CurveJson(Field(engine, "suspendedCurve")) == CurveJson(desired));
    Check("sleep-clears-live-curve", Field(engine, "activeCurve") is null);
    ResetEvents();
    Call(engine, "ResumeForSystemPower");
    Check("real-resume-worker-enters-delayed-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    Check("interrupted-resume-has-no-live-curve", Field(engine, "activeCurve") is null);
    Call(engine, "SuspendForSystemPower");
    Check("resleep-keeps-frozen-user-curve", CurveJson(Field(engine, "suspendedCurve")) == CurveJson(desired));
    Check("resleep-keeps-write-gate-suspended", P(Call(engine, "Snapshot")!, "State") == "Suspended");
    var firstWorker = (Task)Field(engine, "resumeTask")!;
    Check("superseded-worker-terminates-within-bound", firstWorker.Wait(4000));
    Check("old-worker-cannot-publish-ready", P(Call(engine, "Snapshot")!, "State") == "Suspended");
    Check("superseded-worker-leaves-no-running-flag", Equals(Field(engine, "automaticResumeInProgress"), false));
    // Remove both mock synthetic-curve creation and delay for the next cycle.
    // A missing real retained curve must now fail, rather than be hidden by the
    // existing mock-delay fallback curve.
    SetProperty(options, "MockResumeRebuildDelayMs", 0);
    ResetEvents();
    Call(engine, "ResumeForSystemPower");
    Check("latest-resume-rebuilds-without-renderer", Wait(() => Events().Contains("power.resume-auto-rebuild.success")));
    var after = Call(engine, "SnapshotForRead")!;
    Check("latest-cycle-restores-ready", P(after, "State") == "Ready");
    Check("latest-cycle-restores-exact-user-curve", CurveJson(Field(engine, "activeCurve")) == CurveJson(desired));
    Check("latest-cycle-not-silent-await-user", !Events().Contains("power.resume-sync-completed"));
    Check("committed-resume-clears-frozen-copy", Field(engine, "suspendedCurve") is null);
    Check("committed-resume-revokes-own-placeholder-lease", P(after, "Lease") == "null");
    DisposeFixture(engine);

    var (latest, _) = NewEngine();
    var older = Curve(30, 45, 70, 100);
    SetField(latest, "suspendedCurve", older);
    SeedActive(latest, desired);
    Call(latest, "SuspendForSystemPower");
    Check("newer-live-curve-wins-over-old-frozen-curve", CurveJson(Field(latest, "suspendedCurve")) == CurveJson(desired));
    Check("frozen-copy-does-not-alias-live-array", !ReferenceEquals(Field(latest, "suspendedCurve"), desired));
    var frozen = Field(latest, "suspendedCurve");
    Call(latest, "SuspendForSystemPower");
    Check("duplicate-suspend-is-read-only", ReferenceEquals(frozen, Field(latest, "suspendedCurve")));
    DisposeFixture(latest);

    foreach (var endpoint in new[] { "Disable", "Restore", "Release", "Close" })
    {
        var (off, _) = NewEngine();
        SetField(off, "suspendedCurve", desired);
        var beforeOffState = Field(off, "state")!;
        SetProperty(beforeOffState, "State", "Ready");
        SetProperty(beforeOffState, "PowerState", "On");
        using var empty = JsonDocument.Parse("{}");
        if (endpoint == "Close") Call(off, endpoint); else Call(off, endpoint, empty.RootElement);
        Check(endpoint + "-clears-retained-intent", Field(off, "suspendedCurve") is null);
        if (endpoint != "Close")
        {
            Call(off, "SuspendForSystemPower");
            Call(off, "ResumeForSystemPower");
            Check(endpoint + "-cannot-auto-reenable", P(Call(off, "Snapshot")!, "PowerOperationOutcome") == "await-user" && Field(off, "activeCurve") is null);
        }
        DisposeFixture(off);
    }
    foreach (var endpoint in new[] { "Disable", "Restore", "Release" })
    {
        var (cancelled, _) = NewEngine(200);
        SeedActive(cancelled, desired);
        Call(cancelled, "SuspendForSystemPower");
        ResetEvents();
        Call(cancelled, "ResumeForSystemPower");
        Check(endpoint + "-fixture-enters-pending-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
        using var empty = JsonDocument.Parse("{}");
        Call(cancelled, endpoint, empty.RootElement);
        Check(endpoint + "-returns-with-no-retained-intent", Field(cancelled, "suspendedCurve") is null);
        Check(endpoint + "-old-worker-terminates", ((Task)Field(cancelled, "resumeTask")!).Wait(4000));
        Check(endpoint + "-old-worker-cannot-reenable", Field(cancelled, "activeCurve") is null && !Events().Contains("api.enable.success"));
        Check(endpoint + "-old-worker-cannot-report-restored", P(Call(cancelled, "Snapshot")!, "PowerOperationOutcome") != "restored");
        DisposeFixture(cancelled);
    }
    var (sleepOff, _) = NewEngine();
    SeedActive(sleepOff, desired);
    Call(sleepOff, "SuspendForSystemPower");
    ResetEvents();
    var sleepOffBefore = Call(sleepOff, "Snapshot")!;
    Call(sleepOff, "CancelControlIntent");
    var sleepOffAfter = Call(sleepOff, "Snapshot")!;
    Check("sleep-off-preserves-sleep-phase", P(sleepOffAfter, "State") == "Suspended" && P(sleepOffAfter, "PowerState") == "Suspended");
    Check("sleep-off-performs-no-restore-or-hc-action", !Events().Any(e => e.StartsWith("restore.") || e.StartsWith("hc-operation.") || e == "api.enable.success"));
    Check("sleep-off-does-not-invent-oem-proof", P(sleepOffBefore, "OemRestoreConfirmed") == P(sleepOffAfter, "OemRestoreConfirmed"));
    Call(sleepOff, "ResumeForSystemPower");
    Check("sleep-off-next-wake-does-not-reenable", P(Call(sleepOff, "Snapshot")!, "PowerOperationOutcome") == "await-user" && Field(sleepOff, "activeCurve") is null);
    DisposeFixture(sleepOff);

    var (manual, _) = NewEngine(200);
    SeedActive(manual, desired);
    Call(manual, "SuspendForSystemPower");
    ResetEvents();
    Call(manual, "ResumeForSystemPower");
    Check("manual-restart-fixture-enters-pending-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var oldManualWorker = (Task)Field(manual, "resumeTask")!;
    var cancelledAttempt = long.Parse(P(Call(manual, "Snapshot")!, "PowerOperationAttempt"));
    Call(manual, "CancelControlIntent");
    Call(manual, "Open");
    Call(manual, "OpenEvents");
    var newLease = Call(manual, "AcquireControl")!;
    var newCurve = Curve(20, 35, 60, 85);
    using (var body = JsonDocument.Parse("{\"nodes\":" + CurveJson(newCurve) + ",\"leaseId\":" + JsonSerializer.Serialize(P(newLease, "LeaseId")) + "}"))
        Call(manual, "Enable", body.RootElement);
    Check("manual-restart-is-reachable-before-old-worker-exits", CurveJson(Field(manual, "activeCurve")) == CurveJson(newCurve));
    Check("manual-restart-old-worker-terminates", oldManualWorker.Wait(4000));
    Check("old-worker-does-not-overwrite-new-manual-curve", CurveJson(Field(manual, "activeCurve")) == CurveJson(newCurve));
    Check("old-worker-does-not-fault-new-manual-control", P(Call(manual, "Snapshot")!, "State") == "Ready" && P(Call(manual, "Snapshot")!, "UnknownState") == "False");
    Check("new-manual-control-publishes-admissible-receipt", P(Call(manual, "Snapshot")!, "ControlAccepting") == "True");
    var cancelledBinding = T("ResumeWaitBinding").GetMethod("Pinned", flags)!.Invoke(null, new object[] { cancelledAttempt })!;
    var cancelledWaiterRejected = false;
    try { ((Task)Call(manual, "WaitForAutomaticResumeAsync", 1000, cancelledBinding)!).GetAwaiter().GetResult(); }
    catch (Exception e) { cancelledWaiterRejected = P(e, "Code") == "HC_RESUME_SUPERSEDED"; }
    Check("old-bound-waiter-cannot-borrow-new-manual-success", cancelledWaiterRejected);
    DisposeFixture(manual);
    var (leaseCancel, _) = NewEngine(1);
    SeedActive(leaseCancel, desired);
    Call(leaseCancel, "SuspendForSystemPower");
    using var leaseStage = new ManualResetEventSlim(false);
    using var releaseLeaseStage = new ManualResetEventSlim(false);
    int stageCaptured = 0;
    Func<object?> pauseAtOwnedLease = () =>
    {
        if (Field(Field(leaseCancel, "diagnostics")!, "contextProvider") is not null
            && Field(leaseCancel, "automaticResumePlaceholderLeaseId") is not null
            && Interlocked.CompareExchange(ref stageCaptured, 1, 0) == 0)
        {
            leaseStage.Set();
            if (!releaseLeaseStage.Wait(4000)) throw new InvalidOperationException("fixture owned-lease pause expired");
        }
        return null;
    };
    Call(Field(leaseCancel, "diagnostics")!, "SetContextProvider", pauseAtOwnedLease);
    ResetEvents();
    Call(leaseCancel, "ResumeForSystemPower");
    Check("cancel-after-acquire-reaches-owned-placeholder-stage", leaseStage.Wait(4000));
    var afterAcquireWorker = (Task)Field(leaseCancel, "resumeTask")!;
    var offAfterAcquire = Task.Run(() => Call(leaseCancel, "CancelControlIntent"));
    Check("cancel-after-acquire-revokes-only-worker-placeholder", Wait(() => Field(leaseCancel, "state")!.GetType().GetProperty("Lease")!.GetValue(Field(leaseCancel, "state")) is null, 500));
    releaseLeaseStage.Set();
    Check("cancel-after-acquire-and-old-worker-both-terminate", offAfterAcquire.Wait(4000) && afterAcquireWorker.Wait(4000));
    Check("cancel-after-acquire-never-enables-old-curve", Field(leaseCancel, "activeCurve") is null && !Events().Contains("api.enable.success"));
    var freshLeaseAvailable = false;
    try { Call(leaseCancel, "AcquireControl"); freshLeaseAvailable = true; } catch { }
    Check("manual-owner-can-acquire-without-waiting-old-15s-lease", freshLeaseAvailable);
    DisposeFixture(leaseCancel);

    var (announcedSleep, _) = NewEngine(200);
    SeedActive(announcedSleep, desired);
    Call(announcedSleep, "SuspendForSystemPower");
    ResetEvents();
    Call(announcedSleep, "ResumeForSystemPower");
    Check("announced-sleep-enters-real-delayed-resume", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var sleepInterruptedWorker = (Task)Field(announcedSleep, "resumeTask")!;
    var oldSleepAttempt = long.Parse(P(Call(announcedSleep, "Snapshot")!, "PowerOperationAttempt"));
    var announceTimer = Stopwatch.StartNew();
    Call(announcedSleep, "MarkSystemSuspendPending");
    Check("sleep-announcement-does-not-wait-for-worker", announceTimer.ElapsedMilliseconds < 100);
    Check("announced-sleep-stops-old-worker-within-bound", sleepInterruptedWorker.Wait(4000));
    Check("announced-sleep-old-worker-performs-no-enable", !Events().Contains("api.enable.success"));
    Check("announced-sleep-does-not-leave-false-running-flag", Equals(Field(announcedSleep, "automaticResumeInProgress"), false));
    Check("announced-sleep-retires-old-operation-owner", Field(announcedSleep, "activeResumeOperation") is null);
    var oldSleepBinding = T("ResumeWaitBinding").GetMethod("Pinned", flags)!.Invoke(null, new object[] { oldSleepAttempt })!;
    var oldSleepWaitRejected = false;
    try { ((Task)Call(announcedSleep, "WaitForAutomaticResumeAsync", 150, oldSleepBinding)!).GetAwaiter().GetResult(); }
    catch (Exception error) { oldSleepWaitRejected = P(error, "Code") == "HC_RESUME_SUPERSEDED"; }
    Check("announced-sleep-bound-waiter-gets-own-superseded", oldSleepWaitRejected);
    Call(announcedSleep, "SuspendForSystemPower");
    Check("executed-sleep-still-preserves-user-curve", CurveJson(Field(announcedSleep, "suspendedCurve")) == CurveJson(desired));
    Check("executed-sleep-keeps-suspended-phase", P(Call(announcedSleep, "Snapshot")!, "State") == "Suspended");
    DisposeFixture(announcedSleep);

    // Directly inject a later owner to test only the retired worker's field
    // fencing. Production ingress serializes ownership separately; this does
    // not claim that two simultaneous real resume workers are supported.
    var (laterOwner, _) = NewEngine(200);
    SeedActive(laterOwner, desired);
    Call(laterOwner, "SuspendForSystemPower");
    ResetEvents();
    Call(laterOwner, "ResumeForSystemPower");
    Check("later-owner-fixture-pauses-old-real-worker", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var retiredWorker = (Task)Field(laterOwner, "resumeTask")!;
    var replacementOwner = Activator.CreateInstance(T("FanHostEngine+ResumeOperationIdentity"), flags, null, new object[] { 9L, 900, 999L, 999L, 999L }, null)!;
    SetField(laterOwner, "activeResumeOperation", replacementOwner);
    SetProperty(Field(laterOwner, "state")!, "PowerOperationAttempt", 900);
    SetProperty(Field(laterOwner, "state")!, "PowerOperationStatus", "pending");
    Check("retired-old-worker-exits-without-running-new-work", retiredWorker.Wait(4000));
    Check("old-worker-keeps-later-owner-reference", ReferenceEquals(Field(laterOwner, "activeResumeOperation"), replacementOwner));
    Check("old-worker-keeps-later-owner-running-flag", Equals(Field(laterOwner, "automaticResumeInProgress"), true));
    Check("old-worker-keeps-later-operation-status", P(Call(laterOwner, "Snapshot")!, "PowerOperationStatus") == "pending");
    Call(laterOwner, "CancelControlIntent");
    DisposeFixture(laterOwner);

    var (newClientOwner, _) = NewEngine();
    var protectedLease = Activator.CreateInstance(T("FanLease"), "fixture-new-client", 99L, Environment.TickCount64 + 15000L, "fixture-client")!;
    SetProperty(Field(newClientOwner, "state")!, "Lease", protectedLease);
    SetField(newClientOwner, "automaticResumePlaceholderLeaseId", "fixture-old-placeholder");
    SetField(newClientOwner, "automaticResumePlaceholderLeaseGeneration", 2L);
    var oldOperation = Activator.CreateInstance(T("FanHostEngine+ResumeOperationIdentity"), flags, null, new object[] { 2L, 1, 0L, 0L, 0L }, null)!;
    SetField(newClientOwner, "activeResumeOperation", oldOperation);
    Call(newClientOwner, "CancelControlIntent");
    Check("cancel-old-resume-cannot-revoke-new-client-lease", ReferenceEquals(protectedLease, Field(newClientOwner, "state")!.GetType().GetProperty("Lease")!.GetValue(Field(newClientOwner, "state"))));
    DisposeFixture(newClientOwner);

    if (!(args.Length > 4 && args[4] == "engine-only"))
    {
    // Real HttpListener and production authentication/dispatch, explicit mock
    // engine, ephemeral loopback port. No external service or Host process.
    var (httpEngine, httpOptions) = NewEngine();
    SeedActive(httpEngine, desired);
    Call(httpEngine, "SuspendForSystemPower");
    using var portProbe = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
    portProbe.Start();
    var port = ((System.Net.IPEndPoint)portProbe.LocalEndpoint).Port;
    portProbe.Stop();
    SetProperty(httpOptions, "Port", port);
    var token = new string('a', 64);
    SetProperty(httpOptions, "SessionToken", token);
    var httpDiagnostics = Activator.CreateInstance(diagnosticType, flags, null, new object[] { Path.Combine(evidence, "http-logs"), 0 }, null)!;
    var server = Activator.CreateInstance(T("LocalFanHostServer"), flags, null, new[] { httpEngine, httpOptions, httpDiagnostics }, null)!;
    var serving = (Task)Call(server, "RunAsync")!;
    try
    {
        Check("cancel-http-listener-ready", Wait(() => Events().Contains("listener.ready")));
        using var client = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(2) };
        System.Net.Http.HttpResponseMessage Request(string? credential)
        {
            using var message = new System.Net.Http.HttpRequestMessage(System.Net.Http.HttpMethod.Post, $"http://127.0.0.1:{port}/api/cancel-control-intent");
            if (credential is not null) message.Headers.Add("X-YeMan-Fan-Session", credential);
            message.Content = new System.Net.Http.StringContent("{}", System.Text.Encoding.UTF8, "application/json");
            return client.Send(message);
        }
        using var missing = Request(null);
        Check("cancel-http-missing-session-is-401", (int)missing.StatusCode == 401);
        using var wrong = Request(new string('b', 64));
        Check("cancel-http-wrong-session-is-401", (int)wrong.StatusCode == 401);
        Check("unauthorized-cancel-keeps-retained-intent", CurveJson(Field(httpEngine, "suspendedCurve")) == CurveJson(desired));
        ResetEvents();
        using var accepted = Request(token);
        using var receipt = JsonDocument.Parse(accepted.Content.ReadAsStringAsync().GetAwaiter().GetResult());
        Check("authenticated-cancel-http-200-explicit-receipt", (int)accepted.StatusCode == 200 && receipt.RootElement.GetProperty("controlIntentCancelled").GetBoolean());
        Check("authenticated-cancel-clears-retained-intent", Field(httpEngine, "suspendedCurve") is null);
        Check("authenticated-cancel-http-has-no-hardware-action", !Events().Any(e => e.StartsWith("restore.") || e.StartsWith("hc-operation.") || e == "api.enable.success"));
        if (args.Length > 2)
        {
            SetField(httpEngine, "suspendedCurve", desired);
            SetProperty(httpOptions, "MockResumeRebuildDelayMs", 400);
            ResetEvents();
            Call(httpEngine, "ResumeForSystemPower", 2L);
            Check("bridge-http-chain-enters-real-pending-resume", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
            if (args.Length > 4 && args[4].StartsWith("empty-", StringComparison.Ordinal))
            {
                Call(httpEngine, "CancelControlIntent");
                ((Task)Field(httpEngine, "resumeTask")!).Wait(4000);
                SetProperty(httpOptions, "MockResumeRebuildDelayMs", 0);
                Call(httpEngine, "SuspendForSystemPower");
                Call(httpEngine, "ResumeForSystemPower", 3L);
                Check("bridge-http-empty-resume-has-no-old-control-intent", Field(httpEngine, "suspendedCurve") is null && P(Call(httpEngine, "Snapshot")!, "PowerOperationOutcome") == "await-user");
            }
            var info = new System.Diagnostics.ProcessStartInfo(args[2]) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            info.ArgumentList.Add(args[3]);
            info.ArgumentList.Add($"http://127.0.0.1:{port}");
            if (args.Length > 4) info.ArgumentList.Add(args[4]);
            using var node = System.Diagnostics.Process.Start(info)!;
            var stdout = node.StandardOutput.ReadToEndAsync();
            var stderr = node.StandardError.ReadToEndAsync();
            var finished = node.WaitForExit(10000);
            if (!finished) { node.Kill(entireProcessTree: false); node.WaitForExit(2000); }
            File.WriteAllText(Path.Combine(evidence, "bridge-host-chain.out"), stdout.GetAwaiter().GetResult() + stderr.GetAwaiter().GetResult());
            Check(args.Length > 4 ? "actual-bridge-control-contract-" + args[4] : "actual-bridge-off-then-manual-on-is-reachable", finished && node.ExitCode == 0);
        }
    }
    finally
    {
        Call(server, "Dispose");
        Check("cancel-http-listener-stops-within-bound", serving.Wait(4000));
        DisposeFixture(httpEngine);
    }
    }
    var (cold, _) = NewEngine();
    Call(cold, "SuspendForSystemPower");
    Call(cold, "ResumeForSystemPower");
    Check("never-enabled-session-does-not-invent-curve", P(Call(cold, "Snapshot")!, "State") == "AwaitingControl" && Field(cold, "activeCurve") is null);
    DisposeFixture(cold);
}
catch (Exception e) { Check("runner-no-unexpected-exception", false); Console.Error.WriteLine(e); }
var result = new { ok, count = checks.Count, checks, hostAssemblyPath = assembly.Location, hostAssemblySha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(assembly.Location))).ToLowerInvariant(), scope = args.Length > 4 && args[4] == "engine-only" ? "original production-engine software checks only; HTTP/Node explicitly NOT_RUN; no Host process/native/devices" : "production Host engine with explicit mock backend; safe Close and initial curve are fixtures; real loopback HTTP; no HC/native IPC/device proof" };
File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"ok={ok} checks={checks.Count}");
return ok ? 0 : 1;

sealed class HostTestLoadContext(string hostPath) : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver resolver = new(hostPath);
    protected override Assembly? Load(AssemblyName name)
    {
        var path = resolver.ResolveAssemblyToPath(name);
        return path is null ? null : LoadFromAssemblyPath(path);
    }
}