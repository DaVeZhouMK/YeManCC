using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

internal static class Checks
{
    private const BindingFlags All = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static Assembly host = null!;
    private static Type backendType = null!, engineType = null!, logType = null!;
    private static readonly List<object> results = [];
    private static readonly List<string> failures = [];
    private static readonly List<Thread> ownedStaThreads = [];
    private static string root = "";
    [DllImport("kernel32.dll", EntryPoint="GetCurrentThreadId", ExactSpelling=true)]
    private static extern uint ActualOsThreadId();
    private static object? Field(object target, string name) => target.GetType().GetField(name, All)!.GetValue(target);
    private static void Set(object target, string name, object? value) => target.GetType().GetField(name, All)!.SetValue(target, value);
    private static object? Call(object target, string name, params object?[] args)
    {
        try { return target.GetType().GetMethods(All).Single(m => m.Name==name && m.GetParameters().Length==args.Length && !m.IsGenericMethod).Invoke(target,args); }
        catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
    }
    private static void Require(bool value, string why) { if (!value) throw new InvalidOperationException(why); }
    private static void Check(string name, Action work)
    {
        try { work(); results.Add(new {name,ok=true}); Console.WriteLine("PASS "+name); }
        catch (Exception ex) { failures.Add(name+": "+ex.Message); results.Add(new {name,ok=false,error=ex.Message}); Console.WriteLine("FAIL "+name+": "+ex.Message); }
    }
    private static object? Identity(object backend) => backendType.GetProperty("StaThreadIdentityForDiagnostics",All)?.GetValue(backend);
    private static T Property<T>(object value,string name) => (T)value.GetType().GetProperty(name,All)!.GetValue(value)!;
    private static object? Capture(Func<uint> reader) => backendType.GetMethod("TryCaptureStaThreadIdentity",All)!.Invoke(null,[reader]);
    private sealed class Fixture : IDisposable
    {
        internal readonly object Engine, Backend, Log;
        internal readonly string DirectoryPath;
        internal Fixture()
        {
            DirectoryPath=Path.Combine(root,Guid.NewGuid().ToString("N")); Directory.CreateDirectory(DirectoryPath);
            var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",All)!.Invoke(null,[new[]{"--real-backend"}]);
            Log=Activator.CreateInstance(logType,All,null,[DirectoryPath,0],null)!;
            Engine=Activator.CreateInstance(engineType,All,null,[options,Log],null)!;
            Backend=Field(Engine,"realBackend")!;
            (Field(Engine,"runtimeDiagnosticsTimer") as Timer)?.DisposeAsync().AsTask().GetAwaiter().GetResult(); Set(Engine,"runtimeDiagnosticsTimer",null);
            Require(Field(Backend,"assembly") is null && Field(Backend,"device") is null,"fixture must never load HC or construct a device");
        }
        internal object Work(Func<object> action)
        {
            try { var value = backendType.GetMethods(All).Single(m=>m.Name=="OnStaBounded"&&m.IsGenericMethodDefinition).MakeGenericMethod(typeof(object)).Invoke(Backend,[action,"thread-identity-metadata-only-check"])!; ownedStaThreads.Add((Thread)Field(Backend,"staThread")!); return value; }
            catch(TargetInvocationException ex) when(ex.InnerException is not null) {throw ex.InnerException;}
        }
        internal void Tick() => Call(Engine,"WriteRuntimeDiagnostics");
        internal void EnableLogs() => File.WriteAllText(Path.Combine(DirectoryPath,"fan-logging-enabled.flag"),"enabled");
        public void Dispose()
        {
            logType.GetProperty("SelfTestCaptureEnabled",All)!.SetValue(null,false);
            (Field(Engine,"runtimeDiagnosticsTimer") as Timer)?.DisposeAsync().AsTask().GetAwaiter().GetResult(); Set(Engine,"runtimeDiagnosticsTimer",null);
            ((IDisposable)Backend).Dispose();
            Require(Field(Backend,"assembly") is null && Field(Backend,"device") is null,"disposal must remain device-free");
        }
    }
    private static List<JsonElement> CapturedSamples()
    {
        var values=(IEnumerable)logType.GetProperty("SelfTestCaptured",All)!.GetValue(null)!;
        var samples=new List<JsonElement>();
        foreach(var value in values)
        {
            if((string)value.GetType().GetField("Item1")!.GetValue(value)! != "host.runtime-sample") continue;
            using var doc=JsonDocument.Parse((string)value.GetType().GetField("Item2")!.GetValue(value)!); samples.Add(doc.RootElement.Clone());
        }
        return samples;
    }
    private static void CaptureLogs()
    {
        logType.GetMethod("ResetSelfTestCapture",All)!.Invoke(null,null);
        logType.GetProperty("SelfTestCaptureEnabled",All)!.SetValue(null,true);
    }
    private sealed record ThreadProbe(uint Os, int Managed, string? Name, ApartmentState Apartment);
    public static int Main(string[] args)
    {
        var dll=Path.GetFullPath(args[0]); root=Path.GetFullPath(args[1]);Directory.CreateDirectory(root);
        var resolver=new AssemblyDependencyResolver(dll);
        AssemblyLoadContext.Default.Resolving+=(_,name)=>resolver.ResolveAssemblyToPath(name) is {} found?AssemblyLoadContext.Default.LoadFromAssemblyPath(found):null;
        host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
        backendType=host.GetType("YeManFanHost.RealHcBackend",true)!;engineType=host.GetType("YeManFanHost.FanHostEngine",true)!;logType=host.GetType("YeManFanHost.HostDiagnosticLog",true)!;
        Check("no STA identity is invented before the existing dispatcher starts",()=>{using var f=new Fixture();Require(Identity(f.Backend) is null && Field(f.Backend,"staThread") is null,"unstarted STA has an identity or thread");});
        Check("capture records the supplied OS ID rather than substituting a managed ID",()=>{
            var reads=0;var identity=Capture(()=>{reads++;return 0x6fff1234;})!;
            Require(reads==1&&Property<uint>(identity,"OsThreadId")==0x6fff1234,"OS ID missing, repeated or replaced");
            Require(Property<int>(identity,"ManagedThreadId")==Environment.CurrentManagedThreadId,"managed ID lost");
            Require(Property<long>(identity,"CapturedAtUnixMs")>0,"capture time is missing");
        });
        Check("unavailable or zero OS ID is honest unknown and cannot throw",()=>{Require(Capture(()=>0) is null,"zero ID fabricated");Require(Capture(()=>throw new DllNotFoundException("fixture")) is null,"capture failure fabricated an ID");});
        Check("real existing STA has the independently observed Windows and managed IDs",()=>{
            using var f=new Fixture();var before=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            var actual=(ThreadProbe)f.Work(()=>new ThreadProbe(ActualOsThreadId(),Environment.CurrentManagedThreadId,Thread.CurrentThread.Name,Thread.CurrentThread.GetApartmentState()));
            var identity=Identity(f.Backend)!;Require(identity is not null,"running STA has no identity");
            Require(Property<uint>(identity!,"OsThreadId")==actual.Os,"CPU OS tid cannot be joined to the STA");
            Require(Property<int>(identity!,"ManagedThreadId")==actual.Managed,"managed log thread cannot be joined");
            Require(actual.Name=="YeManFanHost.HC.STA"&&actual.Apartment==ApartmentState.STA,"production STA changed");
            Require(Property<long>(identity!,"CapturedAtUnixMs")>=before,"identity belongs to an older lifecycle");
        });
        Check("later dispatcher actions reuse one captured identity without a new worker",()=>{
            using var f=new Fixture();f.Work(()=>true);var first=Identity(f.Backend)!;var thread=Field(f.Backend,"staThread");
            for(var i=0;i<3;i++){var observed=(uint)f.Work(()=>ActualOsThreadId());Require(ReferenceEquals(first,Identity(f.Backend)),"identity recaptured on an operation");Require(observed==Property<uint>(first,"OsThreadId"),"STA OS ID drifted");Require(ReferenceEquals(thread,Field(f.Backend,"staThread")),"a second worker was created");}
        });
        Check("actual runtime sample serializes the matching live STA identity",()=>{
            using var f=new Fixture();f.Work(()=>true);f.EnableLogs();CaptureLogs();f.Tick();
            var samples=CapturedSamples();Require(samples.Count==1,"existing runtime sample disappeared or duplicated");
            var identity=Identity(f.Backend)!;var row=samples[0].GetProperty("hcStaThread");
            Require(row.GetProperty("osThreadId").GetUInt32()==Property<uint>(identity,"OsThreadId"),"runtime sample cannot join OS thread");
            Require(row.GetProperty("managedThreadId").GetInt32()==Property<int>(identity,"ManagedThreadId"),"runtime sample cannot join managed log");
            Require(samples[0].GetProperty("threadCpuIdKind").GetString()=="os-thread-id","sample ID kind changed");
        });
        Check("sampling without a started STA neither starts HC nor fabricates a role",()=>{
            using var f=new Fixture();f.EnableLogs();CaptureLogs();f.Tick();var sample=CapturedSamples().Single();
            Require(!sample.TryGetProperty("hcStaThread",out var value)||value.ValueKind==JsonValueKind.Null,"missing STA labeled as a worker");
            Require(Field(f.Backend,"staThread") is null,"sampling started a dispatcher");
        });
        Check("closed logging retains its gate and current control facts",()=>{
            using var f=new Fixture();f.Work(()=>true);var identity=Identity(f.Backend);var state=Field(f.Engine,"state")!;
            for(var i=0;i<4;i++)f.Tick();
            Require(ReferenceEquals(identity,Identity(f.Backend)),"sample recaptured thread identity");
            Require((long)Field(f.Engine,"runtimeLoopMaintenanceRuns")! ==4,"existing maintenance skipped");
            Require(Property<bool>(state,"HardwareWritesEnabled")==false&&state.GetType().GetProperty("Lease",All)!.GetValue(state) is null,"diagnostics granted control or a lease");
            Require(!Directory.GetFiles(f.DirectoryPath,"*runtime*").Any(p=>File.ReadAllText(p).Contains("host.runtime-sample")),"closed gate wrote runtime samples");
        });
        Check("STA exit clears its identity before a native ID can later be reused",()=>{
            using var f=new Fixture();f.Work(()=>true);var thread=(Thread)Field(f.Backend,"staThread")!;Require(Identity(f.Backend) is not null,"no live identity");
            ((IDisposable)f.Backend).Dispose();Require(!thread.IsAlive,"owned dispatcher did not exit");Require(Identity(f.Backend) is null,"ended STA retained a stale OS role");
        });
        Check("all checks leave HC unloaded and no own HC STA remains alive",()=>{ Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC loaded during diagnostic checks"); Require(ownedStaThreads.All(thread=>!thread.IsAlive),"a test-owned STA was left alive"); });
        Console.WriteLine(JsonSerializer.Serialize(new{ok=failures.Count==0,checks=results.Count,results,failures,hardwareCalls=0,cpuBenchmark=false,scope="actual Host runtime sampler and WPF STA, independent Windows thread ID observation; no Host Main, HC assembly, device, physical sleep, user process or CPU benchmark"}));
        return failures.Count==0?0:1;
    }
}