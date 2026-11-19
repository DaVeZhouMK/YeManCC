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
    private static string[] PowerBoundaryTerminalContextProperties=[];
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
            foreach(var enabled in new[]{false,true})
                Check("power boundary keeps fresh compact terminal and original lanes (gate "+enabled+")",()=>{
                    var log=Log(enabled?"boundary-on":"boundary-off",enabled?"enabled":null);
                    var engineType=assembly.GetType("YeManFanHost.FanHostEngine",true)!;
                    var stateType=assembly.GetType("YeManFanHost.HostState",true)!;
                    var optionsType=assembly.GetType("YeManFanHost.HostOptions",true)!;
                    var state=Activator.CreateInstance(stateType,nonPublic:true)!;
                    // Only the production context functions are invoked; no engine constructor or backend.
                    var engine=System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(engineType);
                    engineType.GetField("state",All)!.SetValue(engine,state);
                    engineType.GetField("options",All)!.SetValue(engine,optionsType.GetMethod("Parse",All)!.Invoke(null,[Array.Empty<string>()]));
                    var full=engineType.GetMethod("BuildDiagnosticContext",All)!;
                    var terminal=engineType.GetMethod("BuildPowerBoundaryTerminalContext",All)!;
                    var fullCalls=0;var terminalCalls=0;
                    LogType.GetMethod("SetContextProvider",All)!.Invoke(log,[(Func<object?>)(()=>{fullCalls++;return full.Invoke(engine,null);})]);
                    LogType.GetMethod("SetPowerBoundaryTerminalContextProvider",All)!.Invoke(log,[(Func<object?>)(()=>{
                        terminalCalls++;
                        var context=terminal.Invoke(engine,null)!;
                        PowerBoundaryTerminalContextProperties=context.GetType().GetProperties().Select(p=>JsonNamingPolicy.CamelCase.ConvertName(p.Name)).ToArray();
                        return context;
                    })]);
                    const string name="oem.power-boundary";
                    var metadata=new {fanScope="fixture-power-boundary",callSite="OpenHcDevice"};
                    stateType.GetProperty("State",All)!.SetValue(state,"Ready");
                    Write.Invoke(log,[name,new {state="entered",metadata}]);
                    stateType.GetProperty("State",All)!.SetValue(state,"FaultLocked");
                    stateType.GetProperty("LastError",All)!.SetValue(state,"fixture-terminal-error");
                    Write.Invoke(log,[name,new {state="scope-ended",metadata,leafEvidence="not-observed"}]);
                    const string sample="oem.power-boundary-sample";
                    Write.Invoke(log,[sample,new {state="entered",metadata}]);
                    stateType.GetProperty("LastError",All)!.SetValue(state,"fixture-later-sample-error");
                    Write.Invoke(log,[sample,new {state="scope-ended",metadata,leafEvidence="not-observed"}]);
                    var fault=Rows(Fault(log));var runtime=Rows(Runtime(log));
                    Assert(fault.Count==2&&runtime.Count==(enabled?4:0),"boundary rows went to the wrong lane");
                    Assert(fullCalls==(enabled?2:1)&&terminalCalls==(enabled?2:1),"wrong provider was invoked");
                    var first=fault[0].GetProperty("context");var last=fault[1].GetProperty("context");
                    Assert(first.GetProperty("state").GetString()=="Ready"&&first.TryGetProperty("hostMode",out _),"entry lost its full context");
                    Assert(last.GetProperty("contextScope").GetString()=="fan-power-boundary-terminal-v1"&&last.GetProperty("state").GetString()=="FaultLocked"&&last.GetProperty("lastError").GetString()=="fixture-terminal-error","terminal context is stale or unmarked");
                    Assert(!last.TryGetProperty("hostMode",out _)&&last.GetProperty("resumePhase").GetString()=="FaultLocked"&&last.TryGetProperty("recoveryClaimEvidence",out _),"terminal projection lost responsibility evidence or gained full context");
                    Assert(JsonElement.DeepEquals(fault[0].GetProperty("details").GetProperty("metadata"),fault[1].GetProperty("details").GetProperty("metadata"))&&fault[1].GetProperty("details").GetProperty("leafEvidence").GetString()=="not-observed","invocation or leaf evidence changed");
                    if(enabled){
                        Assert(JsonElement.DeepEquals(runtime[0],fault[0])&&JsonElement.DeepEquals(runtime[1],fault[1]),"ledger and runtime mirrors differ");
                        var sampleFirst=runtime[2].GetProperty("context");var sampleLast=runtime[3].GetProperty("context");
                        Assert(sampleFirst.TryGetProperty("hostMode",out _)&&sampleFirst.GetProperty("lastError").GetString()=="fixture-terminal-error","sample entry lost full context");
                        Assert(sampleLast.GetProperty("contextScope").GetString()=="fan-power-boundary-terminal-v1"&&
                            sampleLast.GetProperty("lastError").GetString()=="fixture-later-sample-error"&&!sampleLast.TryGetProperty("hostMode",out _)&&
                            sampleLast.GetProperty("recoveryClaimEvidence").ValueKind==JsonValueKind.Object&&
                            runtime[3].GetProperty("details").GetProperty("leafEvidence").GetString()=="not-observed","sample terminal lost fresh compact context or changed leaf evidence");
                    }
                });
            Check("disabled flag keeps actual cancellation and lease decision plus the detected-open event",()=>{
                var log=Log("resume-evidence", "disabled");
                var engineType=assembly.GetType("YeManFanHost.FanHostEngine",true)!;
                var engine=System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(engineType);
                var stateField=engineType.GetField("state",All)!;
                var state=Activator.CreateInstance(stateField.FieldType,nonPublic:true)!;
                stateField.FieldType.GetProperty("State",All)!.SetValue(state,"Resuming");
                stateField.SetValue(engine,state);
                engineType.GetField("diagnostics",All)!.SetValue(engine,log);
                var errorType=assembly.GetType("YeManFanHost.FanApiException",true)!;
                var originProperty=errorType.GetProperty("FailureOrigin",All)!;
                var recover=engineType.GetMethod("RecoverAfterHardwareFailure",All)!;
                var cases=new[]{("FAN_WRITE_SUPERSEDED","Unclassified"),("HC_OPEN_EVENTS_POWER_SUPERSEDED","Unclassified"),("FAN_PUBLIC_POWER_SUPERSEDED","PublicAdmission")};
                foreach(var (code,origin) in cases){
                    var error=Activator.CreateInstance(errorType,[409,code,"fixture exception text"])!;
                    originProperty.SetValue(error,Enum.Parse(originProperty.PropertyType,origin));
                    recover.Invoke(engine,[error]);
                }
                engineType.GetMethod("RevokeAutomaticResumePlaceholderLeaseLocked",All)!.Invoke(engine,[3L,"public-power-superseded"]);
                engineType.GetMethod("RevokeAutomaticResumePlaceholderLeaseLocked",All)!.Invoke(engine,[4L,"user-control-cancelled"]);
                Write.Invoke(log,["hc-abandoned-device-open.detected",new {source="runtime-sample"}]);
                Emit(log,"hc-operation.begin");
                var rows=Rows(Fault(log));
                Assert(rows.Count==6&&Rows(Runtime(log)).Count==0,"failure rows or gate changed");
                for(var i=0;i<cases.Length;i++){
                    var d=rows[i].GetProperty("details");
                    Assert(rows[i].GetProperty("eventName").GetString()=="hc-operation.write-cancelled"
                        && d.GetProperty("cancellationCode").GetString()==cases[i].Item1
                        && d.GetProperty("failureOrigin").GetString()==cases[i].Item2
                        && d.GetProperty("writesMayHaveOccurred").GetBoolean()&&!d.GetProperty("hardwareFailureClaimed").GetBoolean(),"cancellation code, origin or evidence changed");
                }
                foreach(var (i,reason,required) in new[]{(3,"public-power-superseded",true),(4,"user-control-cancelled",false)}){
                    var d=rows[i].GetProperty("details");
                    Assert(rows[i].GetProperty("eventName").GetString()=="power.resume-placeholder-lease-kept"
                        && d.GetProperty("reason").GetString()=="no-lease-present"
                        && d.GetProperty("resumeTransitionReason").GetString()==reason
                        && d.GetProperty("nextAdjustmentStillRequired").GetBoolean()==required,"lease reason and transition reason were conflated");
                }
                Assert(rows[5].GetProperty("eventName").GetString()=="hc-abandoned-device-open.detected","first detected-open event was lost with flag off");
            });
            Check("unreadable flag cannot suppress any synchronous responsibility event",()=>{var log=Log("unreadable-sync","enabled");using(var held=HoldFlag(log)){foreach(var name in free)Emit(log,name);}var rows=Rows(Fault(log));Assert(rows.Count==free.Length,"lost responsibility events: expected="+free.Length+" got="+rows.Count);Assert(rows.Select(x=>x.GetProperty("eventName").GetString()).SequenceEqual(free),"responsibility event order changed");Assert(Rows(Runtime(log)).Count==0,"unreadable flag must not enable runtime");});
            Check("unreadable flag cannot skip lazy responsibility materialization or persistence",()=>{var log=Log("unreadable-lazy","enabled");var calls=0;using(var held=HoldFlag(log)){foreach(var name in free)EmitLazy(log,name,()=>{calls++;return new {phase="fixture",scope="no-hardware"};});}Assert(calls==free.Length,"lazy fault factories lost: expected="+free.Length+" got="+calls);Assert(Rows(Fault(log)).Count==free.Length&&Rows(Runtime(log)).Count==0,"lazy fault evidence lost or runtime enabled");});
            Check("unreadable ordinary flag performs no lazy work and does not escape",()=>{var log=Log("unreadable-ordinary","enabled");var calls=0;using(var held=HoldFlag(log)){Emit(log,"hc-operation.begin");EmitLazy(log,"hc.fan-dispatch.begin",()=>{calls++;return new {phase="fixture"};});}Assert(calls==0&&Rows(Runtime(log)).Count==0&&Rows(Fault(log)).Count==0,"ordinary gate weakened");});
            Check("unlocking flag immediately restores ordinary runtime writes",()=>{var log=Log("recover","enabled");using(var held=HoldFlag(log)){Emit(log,"hc-operation.begin");}Emit(log,"hc-operation.begin");Assert(Rows(Runtime(log)).Count==1,"flag failure poisoned later writes");});
            Check("raw runtime gate still reports unreadable input to its existing caller",()=>{var log=Log("raw-gate","enabled");var failed=false;using(var held=HoldFlag(log)){try{LogType.GetMethod("IsRuntimeGateEnabled",All)!.Invoke(log,null);}catch(TargetInvocationException ex)when(ex.InnerException is IOException){failed=true;}}Assert(failed,"global gate exceptions were hidden; maintenance contract changed");});
            Check("selftest capture and responsibility ledger both survive unreadable flag",()=>{var log=Log("capture","enabled");LogType.GetMethod("ResetSelfTestCapture",All)!.Invoke(null,null);capture.SetValue(null,true);try{using(var held=HoldFlag(log)){Emit(log,"auth.rejected");EmitLazy(log,"parent-exit-handoff.accepted",()=>new {phase="fixture"});Emit(log,"hc-operation.begin");}var values=((IEnumerable)LogType.GetProperty("SelfTestCaptured",All)!.GetValue(null)!).Cast<object>().ToArray();Assert(values.Length==3,"self-test capture gate changed");Assert(Rows(Fault(log)).Count==2&&Rows(Runtime(log)).Count==0,"captured faults lost their required ledger");}finally{capture.SetValue(null,false);LogType.GetMethod("ResetSelfTestCapture",All)!.Invoke(null,null);}});
            Check("unwritable ledger failure stays diagnostic-only and next attempt works",()=>{var log=Log("locked-ledger");File.WriteAllText(Fault(log),"");using(var held=new FileStream(Fault(log),FileMode.Open,FileAccess.ReadWrite,FileShare.None)){Emit(log,"auth.rejected");}Emit(log,"auth.rejected");Assert(Rows(Fault(log)).Count==1,"ledger failure escaped/poisoned later writes");});
            Check("throwing lazy payload remains contained without inventing a row",()=>{var log=Log("payload-failure","enabled");EmitLazy(log,"auth.rejected",()=>throw new InvalidOperationException("fixture payload failure"));Assert(Rows(Runtime(log)).Count==0&&Rows(Fault(log)).Count==0,"payload failure produced invented evidence");});
            var result=new{ok=Failures.Count==0,checks=Results.Count,whitelistEvents=free.Length,failures=Failures,results=Results,powerBoundaryTerminalContextProperties=PowerBoundaryTerminalContextProperties,hardwareCalls=0,cpuBenchmark=false,scope="actual HostDiagnosticLog DLL and selected engine cancellation/no-lease branches; temporary files and real FileShare.None flag/ledger contention; no Host Main, HC, network, native or devices"};var json=JsonSerializer.Serialize(result);File.WriteAllText(args[2],json);Console.WriteLine(json);return Failures.Count==0?0:1;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
