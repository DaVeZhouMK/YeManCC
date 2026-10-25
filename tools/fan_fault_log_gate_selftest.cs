using System.IO;
using System.Collections;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
internal static class FaultGateChecks
{
    private const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
    private static Type LogType=null!;
    private static MethodInfo Write=null!,Lazy=null!;
    private static string Root="";
    private static readonly List<object> Results=[];
    private static readonly List<string> Failures=[];
    private static void Assert(bool yes,string reason){if(!yes)throw new InvalidOperationException(reason);}
    private static object Log(string name,string? flag=null){var path=Path.Combine(Root,name);Directory.CreateDirectory(path);if(flag is not null)File.WriteAllText(Path.Combine(path,"fan-logging-enabled.flag"),flag);return Activator.CreateInstance(LogType,All,null,[path,0],null)!;}
    private static string Flag(object log)=>Path.Combine(Path.GetDirectoryName((string)LogType.GetProperty("Path",All)!.GetValue(log)!)!,"fan-logging-enabled.flag");
    private static string Fault(object log)=>Path.Combine(Path.GetDirectoryName(Flag(log))!,"fan-host-fault.log");
    private static string Runtime(object log)=>(string)LogType.GetProperty("Path",All)!.GetValue(log)!;
    private static List<JsonElement> Rows(string path)=>!File.Exists(path)?[]:File.ReadAllLines(path).Where(x=>!string.IsNullOrWhiteSpace(x)).Select(x=>JsonDocument.Parse(x.TrimStart('\uFEFF')).RootElement.Clone()).ToList();
    private static void Emit(object log,string name)=>Write.Invoke(log,[name,new {phase="fixture",error="fake-no-device-failure",requestId="fixture-only"}]);
    private static void EmitLazy(object log,string name,Func<object?> value)=>Lazy.Invoke(log,[name,value]);
    private static FileStream HoldFlag(object log){var handle=new FileStream(Flag(log),FileMode.Open,FileAccess.ReadWrite,FileShare.None);var failed=false;try{File.ReadAllText(Flag(log));}catch(IOException){failed=true;}Assert(failed,"precondition: flag was not actually unreadable");return handle;}
    private static void Check(string name,Action action){try{action();Results.Add(new{name,ok=true});Console.WriteLine("PASS "+name);}catch(Exception ex){Failures.Add(name+": "+(ex.InnerException??ex).Message);Results.Add(new{name,ok=false,error=(ex.InnerException??ex).Message});Console.WriteLine("FAIL "+name+": "+(ex.InnerException??ex).Message);}}
    public static int Main(string[] args)
    {
        try{
            var dll=Path.GetFullPath(args[0]);Root=Path.GetFullPath(args[1]);Directory.CreateDirectory(Root);
            AssemblyLoadContext.Default.Resolving+=(_,name)=>{var p=Path.Combine(Path.GetDirectoryName(dll)!,name.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
            var assembly=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);LogType=assembly.GetType("YeManFanHost.HostDiagnosticLog",true)!;Write=LogType.GetMethod("Write",All)!;Lazy=LogType.GetMethod("WriteLazy",All)!;
            var capture=LogType.GetProperty("SelfTestCaptureEnabled",All)!;capture.SetValue(null,false);
            var free=((IEnumerable)LogType.GetField("GateFreeFaultEvents",All)!.GetValue(null)!).Cast<string>().ToArray();Assert(free.Length>20,"fixture must cover actual production responsibility whitelist");
            Check("missing/disabled flag keeps ordinary logs off and both responsibility forms durable",()=>{foreach(var value in new string?[]{null,"disabled"}){var log=Log(value is null?"absent":"disabled",value);var calls=0;Emit(log,"hc-operation.begin");Emit(log,"auth.rejected");EmitLazy(log,"parent-exit-handoff.accepted",()=>{calls++;return new {phase="fixture"};});Assert(calls==1&&Rows(Runtime(log)).Count==0&&Rows(Fault(log)).Count==2,"ordinary gate or fault exemption changed");}});
            Check("enabled flag keeps runtime and responsibility evidence with identical row",()=>{var log=Log("enabled","enabled");Emit(log,"hc-operation.begin");Emit(log,"auth.rejected");EmitLazy(log,"parent-exit-handoff.accepted",()=>new {phase="fixture"});var runtime=Rows(Runtime(log));var fault=Rows(Fault(log));Assert(runtime.Count==3&&fault.Count==2,"enabled row counts changed");Assert(JsonElement.DeepEquals(runtime[1],fault[0])&&JsonElement.DeepEquals(runtime[2],fault[1]),"fault/runtime content diverged");});
            Check("unreadable flag cannot suppress any synchronous responsibility event",()=>{var log=Log("unreadable-sync","enabled");using(var held=HoldFlag(log)){foreach(var name in free)Emit(log,name);}var rows=Rows(Fault(log));Assert(rows.Count==free.Length,"lost responsibility events: expected="+free.Length+" got="+rows.Count);Assert(rows.Select(x=>x.GetProperty("eventName").GetString()).SequenceEqual(free),"responsibility event order changed");Assert(Rows(Runtime(log)).Count==0,"unreadable flag must not enable runtime");});
            Check("unreadable flag cannot skip lazy responsibility materialization or persistence",()=>{var log=Log("unreadable-lazy","enabled");var calls=0;using(var held=HoldFlag(log)){foreach(var name in free)EmitLazy(log,name,()=>{calls++;return new {phase="fixture",scope="no-hardware"};});}Assert(calls==free.Length,"lazy fault factories lost: expected="+free.Length+" got="+calls);Assert(Rows(Fault(log)).Count==free.Length&&Rows(Runtime(log)).Count==0,"lazy fault evidence lost or runtime enabled");});
            Check("unreadable ordinary flag performs no lazy work and does not escape",()=>{var log=Log("unreadable-ordinary","enabled");var calls=0;using(var held=HoldFlag(log)){Emit(log,"hc-operation.begin");EmitLazy(log,"hc.fan-dispatch.begin",()=>{calls++;return new {phase="fixture"};});}Assert(calls==0&&Rows(Runtime(log)).Count==0&&Rows(Fault(log)).Count==0,"ordinary gate weakened");});
            Check("unlocking flag immediately restores ordinary runtime writes",()=>{var log=Log("recover","enabled");using(var held=HoldFlag(log)){Emit(log,"hc-operation.begin");}Emit(log,"hc-operation.begin");Assert(Rows(Runtime(log)).Count==1,"flag failure poisoned later writes");});
            Check("raw runtime gate still reports unreadable input to its existing caller",()=>{var log=Log("raw-gate","enabled");var failed=false;using(var held=HoldFlag(log)){try{LogType.GetMethod("IsRuntimeGateEnabled",All)!.Invoke(log,null);}catch(TargetInvocationException ex)when(ex.InnerException is IOException){failed=true;}}Assert(failed,"global gate exceptions were hidden; maintenance contract changed");});
            Check("selftest capture and responsibility ledger both survive unreadable flag",()=>{var log=Log("capture","enabled");LogType.GetMethod("ResetSelfTestCapture",All)!.Invoke(null,null);capture.SetValue(null,true);try{using(var held=HoldFlag(log)){Emit(log,"auth.rejected");EmitLazy(log,"parent-exit-handoff.accepted",()=>new {phase="fixture"});Emit(log,"hc-operation.begin");}var values=((IEnumerable)LogType.GetProperty("SelfTestCaptured",All)!.GetValue(null)!).Cast<object>().ToArray();Assert(values.Length==3,"self-test capture gate changed");Assert(Rows(Fault(log)).Count==2&&Rows(Runtime(log)).Count==0,"captured faults lost their required ledger");}finally{capture.SetValue(null,false);LogType.GetMethod("ResetSelfTestCapture",All)!.Invoke(null,null);}});
            Check("unwritable ledger failure stays diagnostic-only and next attempt works",()=>{var log=Log("locked-ledger");File.WriteAllText(Fault(log),"");using(var held=new FileStream(Fault(log),FileMode.Open,FileAccess.ReadWrite,FileShare.None)){Emit(log,"auth.rejected");}Emit(log,"auth.rejected");Assert(Rows(Fault(log)).Count==1,"ledger failure escaped/poisoned later writes");});
            Check("throwing lazy payload remains contained without inventing a row",()=>{var log=Log("payload-failure","enabled");EmitLazy(log,"auth.rejected",()=>throw new InvalidOperationException("fixture payload failure"));Assert(Rows(Runtime(log)).Count==0&&Rows(Fault(log)).Count==0,"payload failure produced invented evidence");});
            var result=new{ok=Failures.Count==0,checks=Results.Count,whitelistEvents=free.Length,failures=Failures,results=Results,hardwareCalls=0,cpuBenchmark=false,scope="actual HostDiagnosticLog DLL; temporary files and real FileShare.None flag/ledger contention only; no Host Main, HC, network, native or devices"};var json=JsonSerializer.Serialize(result);File.WriteAllText(args[2],json);Console.WriteLine(json);return Failures.Count==0?0:1;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
