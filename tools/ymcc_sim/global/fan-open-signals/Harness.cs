using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using FanFixture;
using HandheldCompanion.Devices;
using Trace = FanFixture.Trace;
static class Program {
 const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static readonly List<object> Cases=new();static readonly List<object> Models=new();static int NativeAttempts;
 static object? Invoke(object obj,string name,params object?[] args){try{return obj.GetType().GetMethods(All).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(obj,args);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void Set(object obj,string name,object? value)=>obj.GetType().GetField(name,All)!.SetValue(obj,value);
 static object? Get(object obj,string name)=>obj.GetType().GetField(name,All)!.GetValue(obj);
 static void Check(bool ok,string message){if(!ok)throw new InvalidOperationException(message);}
 static void Run(string id,Action action){Trace.Reset();try{action();Cases.Add(new{id,status="PASS",trace=Trace.Calls.ToArray()});}catch(Exception e){Cases.Add(new{id,status="FAIL",error=e.ToString(),trace=Trace.Calls.ToArray()});}}
 static void Reject(Action action,string text){Exception? error=null;try{action();}catch(Exception e){error=e;}Check(error is not null && error.Message.Contains(text,StringComparison.OrdinalIgnoreCase),"Expected rejection: "+text);}
 static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
 static object Backend(Assembly assembly,IDevice device){var options=RuntimeHelpers.GetUninitializedObject(assembly.GetType("YeManFanHost.HostOptions",true)!);var backend=Activator.CreateInstance(assembly.GetType("YeManFanHost.RealHcBackend",true)!,All,null,new[]{options},null)!;Set(backend,"device",device);return backend;}
 sealed class HostContext(string path):AssemblyLoadContext(true){protected override Assembly? Load(AssemblyName name){if(name.Name=="HandheldCompanion")return typeof(IDevice).Assembly;var candidate=Path.Combine(Path.GetDirectoryName(path)!,name.Name+".dll");return File.Exists(candidate)?LoadFromAssemblyPath(candidate):null;}}
 static Assembly Load(string path){var ctx=new HostContext(path);var assembly=ctx.LoadFromAssemblyPath(Path.GetFullPath(path));NativeLibrary.SetDllImportResolver(assembly,(name,_,_)=>{NativeAttempts++;throw new InvalidOperationException("native denied "+name);});return assembly;}
 static string[] SemanticOps(MethodDefinition method){
  var source=method.Body.Instructions.ToArray();var result=new List<string>();
  string Slot(Mono.Cecil.Cil.Instruction i)=>i.Operand is Mono.Cecil.Cil.VariableDefinition v?v.Index.ToString():i.OpCode.Name.Split('.').Last();
  for(int n=0;n<source.Length;n++){
   var op=source[n].OpCode.Name;if(op=="nop")continue;
   if(op.StartsWith("stloc")&&n+1<source.Length&&source[n+1].OpCode.Name.StartsWith("ldloc")&&!source[n+1].OpCode.Name.StartsWith("ldloca")&&Slot(source[n])==Slot(source[n+1])){
    var slot=Slot(source[n]);var refs=source.Count(i=>(i.OpCode.Name.StartsWith("stloc")||i.OpCode.Name.StartsWith("ldloc"))&&Slot(i)==slot);
    if(refs==2){n++;continue;}
   }
   if(op.StartsWith("ldloca"))op="ldloca";else if(op.StartsWith("ldloc"))op="ldloc";else if(op.StartsWith("stloc"))op="stloc";
   result.Add(op.EndsWith(".s")?op[..^2]:op);
  }
  return result.ToArray();
 }
 static void ModelAudit(string hcPath){using var actual=AssemblyDefinition.ReadAssembly(hcPath);using var fixture=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
  foreach(var name in new[]{"HandheldCompanion.Devices.IDevice","HandheldCompanion.Devices.ClawA1M","HandheldCompanion.Devices.ClawA2VM","HandheldCompanion.Devices.MSI.ClawCG3EM"}){
   var a=actual.MainModule.GetType(name).Methods.Single(m=>m.Name=="Open");var f=fixture.MainModule.GetType(name).Methods.Single(m=>m.Name=="Open");
   var original=SemanticOps(a);var generated=SemanticOps(f);
   Check(original.SequenceEqual(generated),"Full original Open opcodes differ: "+name+" "+original.Length+"/"+generated.Length);
   var originalCalls=a.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName.Replace("System.Threading.Thread::Sleep","FanFixture.InertThread::Sleep")).ToArray();
   var fixtureCalls=f.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).ToArray();
   Check(originalCalls.SequenceEqual(fixtureCalls),"Original Open call sequence differs: "+name);
   Models.Add(new{type=name,opcodeMatch=true,callMatch=true,instructions=original.Length});
  }
  var msi=fixture.MainModule.GetType("HandheldCompanion.Devices.ClawA1M").Methods.Single(m=>m.Name=="Open");var saved=msi.Body.Instructions.Count;
  Check(saved>100,"BaseOpen-only incomplete parent must be refused");
 }
 static int Main(string[] args){var report=args[3];try{
  Run("model.actual-hc-opcode-and-call-equivalence",()=>ModelAudit(args[2]));
  Check(!Cases.Any(x=>JsonSerializer.Serialize(x).Contains("\"status\":\"FAIL\"")),"Cannot execute an incomplete model");
  Check(Hash(args[0])=="5e94170105c65bb0503751460082db2a0827061b075c55afe92f7ef9b3b91993","legacy pin");
  var old=Load(args[0]);var candidate=Load(args[1]);
  var types=new[]{typeof(ClawA1M),typeof(ClawA2VM),typeof(ClawBZ2EM),typeof(HandheldCompanion.Devices.MSI.ClawCG3EM)};
  foreach(var type in types){
   Run("legacy.full-parent-side-effects."+type.Name,()=>{var d=(IDevice)Activator.CreateInstance(type)!;var b=Backend(old,d);Invoke(b,"OpenHcDevice");Check(d.IsOpen,"legacy flags");Check(Trace.Forbidden>=7,"old Host must hit real parent write intents");Check(Trace.Calls.Any(x=>x.StartsWith("Shift:"))&&Trace.Calls.Any(x=>x.StartsWith("UEFI"))&&Trace.Calls.Any(x=>x=="HID:34"),"old shift/UEFI/ROM");Check(type!=typeof(ClawBZ2EM)||Trace.Power.Contains((82,35)),"BZ block82");Check(type!=typeof(ClawA2VM)&&type.Name!="ClawCG3EM"||Trace.Power.TakeLast(2).SequenceEqual(new[]{(80,30),(81,37)}),"derived overrides");});
   Run("candidate.open-repeat-close-reopen."+type.Name,()=>{var d=(IDevice)Activator.CreateInstance(type)!;var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");Check(d.IsOpen,"both fan flags");var reads=Trace.Calls.Count(x=>x.StartsWith("Structured:"));Invoke(b,"OpenHcDevice");Check(reads==Trace.Calls.Count(x=>x.StartsWith("Structured:")),"duplicate read");var life=Get(b,"msiFanLifecycle")!;Invoke(life,"EnsureEventsReady",(Func<bool>)(()=>true));Invoke(life,"Close");Check(!d.IsOpen&&d.Flags()==(false,false),"close flags");Invoke(life,"Close");Check(Trace.Calls.Count(x=>x.StartsWith("Fan.Release"))==1,"one fan release");Invoke(b,"OpenHcDevice");Check(d.IsOpen,"reopen");Invoke(life,"Close");Check(Trace.Forbidden==0,"non-fan side effects leaked");});
  }
  foreach(var fault in new[]{"base-false","base-throw","wmi-false","wmi-throw","generation-before","generation-during"})Run("candidate.reject."+fault,()=>{
   Trace.UseOpenLib=fault.StartsWith("base-");Trace.BaseFalse=fault=="base-false";Trace.BaseThrow=fault=="base-throw";Trace.WmiBad=fault=="wmi-false";Trace.WmiThrow=fault=="wmi-throw";
   var d=new ClawA1M();var b=Backend(candidate,d);if(fault=="generation-before")Set(b,"suspendGateEpoch",1L);if(fault=="generation-during")Trace.OnRead=()=>Set(b,"suspendGateEpoch",1L);
   Exception? error=null;try{Invoke(b,"OpenHcDevice");}catch(Exception e){error=e;}Check(error is not null,"fault must reject");Check(d.Flags()==(false,false),"failed preparation must clear its flags");Check(Trace.Forbidden==0,"failure must not emit writes");Check(!Trace.Calls.Any(x=>x.StartsWith("Fan.Release")),"no fabricated release on failed preparation");
   if(fault=="generation-during"){Trace.OnRead=null;Set(b,"msiFanOpenAdmissionEpoch",1L);Invoke(b,"OpenHcDevice");Check(d.IsOpen,"fresh resume epoch retries");Invoke(Get(b,"msiFanLifecycle")!,"Close");}
  });
  Run("candidate.no-hid-firmware-stays-unknown",()=>{Trace.NoHid=true;var d=new ClawA1M();var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");Check(d.IsOpen&&d.FirmwareValue()==0,"WMI fan does not invent firmware");Invoke(Get(b,"msiFanLifecycle")!,"Close");Check(Trace.Forbidden==0,"no nonfan calls");});
  Run("candidate.observes-bound-firmware",()=>{var d=new ClawA1M();var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");Check(d.FirmwareValue()==Trace.Firmware,"read bound firmware");Invoke(Get(b,"msiFanLifecycle")!,"Close");});
  Run("candidate.unknown-msi-type-rejects",()=>{var b=Backend(candidate,new UnknownClaw());Reject(()=>Invoke(b,"OpenHcDevice"),"unreviewed");Check(Trace.Calls.Count==0,"unknown must not enter parent");Check(!(bool)b.GetType().GetProperty("HcOpenInvocationStarted",All)!.GetValue(b)!,"unknown must not owe a generic Close");});
  Run("candidate.foreign-open-is-not-adopted",()=>{var d=new ClawA1M();d.ForeignOpen();var b=Backend(candidate,d);Reject(()=>Invoke(b,"OpenHcDevice"),"foreign");Check(d.Flags()==(true,false),"foreign state preserved");Check(Trace.Calls.Count==0,"foreign untouched");});
  Run("candidate.unrelated-virtual-route-retained",()=>{var d=new UnrelatedDevice();var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");Check(d.IsOpen&&Trace.Calls.Contains("Unrelated.Open"),"legitimate nonMSI dispatch");});
  Run("candidate.openlib-owned-cleanup-retry",()=>{Trace.UseOpenLib=true;var d=new ClawA1M();var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");var life=Get(b,"msiFanLifecycle")!;Trace.DisposeThrows=true;Reject(()=>Invoke(life,"Close"),"dispose failed");Check((bool)life.GetType().GetProperty("HasResources",All)!.GetValue(life)!,"cleanup debt retained");Trace.DisposeThrows=false;Invoke(life,"Close");Check(!d.IsOpen,"cleanup retry closes");Check(Trace.Forbidden==0,"transport cleanup only");});
  Run("candidate.release-exception-not-washed",()=>{var d=new ClawA1M();var b=Backend(candidate,d);Invoke(b,"OpenHcDevice");var life=Get(b,"msiFanLifecycle")!;Trace.ReleaseThrows=true;Reject(()=>Invoke(life,"Close"),"release failed");Check(d.Flags()==(false,false),"resources closed, error preserved");});
  Run("legacy.shift-read-failure-still-writes",()=>{Trace.ShiftReadFailure=true;var b=Backend(old,new ClawA1M());Invoke(b,"OpenHcDevice");Check(Trace.Calls.Any(x=>x.StartsWith("Shift:")),"minus1 bit check remains real");});
  Run("candidate.shift-read-failure-never-enters-shift",()=>{Trace.ShiftReadFailure=true;var b=Backend(candidate,new ClawA1M());Invoke(b,"OpenHcDevice");Check(Trace.Forbidden==0&&!Trace.Calls.Contains("Read:Get_AP:0"),"no Shift reader/setter in narrow Open");Invoke(Get(b,"msiFanLifecycle")!,"Close");});
  Run("candidate.actual-curve-consumer-regressions",()=>{var type=candidate.GetType("YeManFanHost.MsiFamilyFanCurveSelfTest",true)!;var list=(IEnumerable<string>)type.GetMethod("Run",All)!.Invoke(null,null)!;var checks=list.ToArray();Check(checks.Length>5,"actual curve and readback cases");Models.Add(new{consumer="MsiFamilyFanCurveSelfTest.Run",checks});});
  }catch(Exception e){Cases.Add(new{id="harness",status="FAIL",error=e.ToString()});}
  var failed=Cases.Any(x=>JsonSerializer.Serialize(x).Contains("\"status\":\"FAIL\""));var result=new{schemaVersion=1,kind="fan-open-signals",status=failed?"FAIL":"PASS",cases=Cases,modelAudit=Models,legacyHostSha256=Hash(args[0]),candidateHostSha256=Hash(args[1]),hcReferenceSha256=Hash(args[2]),hardwareCalls=0,hostMainStarted=false,nativeAttempts=NativeAttempts,fullProductAcceptance=false,remainingGaps=new[]{"physical EC/firmware/TDP/HID effects unverified","full actual HC Manager/Host integration not executed; source-derived complete parent fixture"}};
  File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.status,cases=Cases.Count,NativeAttempts}));return failed?2:0;
 }
}



