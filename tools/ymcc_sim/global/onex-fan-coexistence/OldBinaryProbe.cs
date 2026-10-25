using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using HandheldCompanion.Devices;
using Trace = ConsumerFixture.Trace;
static partial class OldBinaryProbe
{
 const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static int NativeAttempts;
 static readonly List<object> Checks=new();
 static readonly List<object> Cases=new();
 static readonly List<object> Audits=new();
 static void Check(bool ok,string name){Checks.Add(new{name,passed=ok});if(!ok)throw new InvalidOperationException(name);}
 static string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
 static object? Invoke(object target,string name,params object?[] args)
 {try{return target.GetType().GetMethods(All).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(target,args);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void Set(object target,string field,object? value)=>target.GetType().GetField(field,All)!.SetValue(target,value);
 static object Backend(Assembly assembly,IDevice device){var options=RuntimeHelpers.GetUninitializedObject(assembly.GetType("YeManFanHost.HostOptions",true)!);var obj=Activator.CreateInstance(assembly.GetType("YeManFanHost.RealHcBackend",true)!,All,null,[options],null)!;Set(obj,"device",device);return obj;}
 static Assembly ExistingFixtureAssembly()=>typeof(IDevice).Assembly;
 static bool BaseOpen(IDevice device)
 {var call=new System.Reflection.Emit.DynamicMethod("BaseOpenFeasibilityOnly",typeof(bool),[typeof(object)],typeof(OldBinaryProbe).Module,true);var il=call.GetILGenerator();il.Emit(System.Reflection.Emit.OpCodes.Ldarg_0);il.Emit(System.Reflection.Emit.OpCodes.Castclass,typeof(IDevice));il.Emit(System.Reflection.Emit.OpCodes.Call,typeof(IDevice).GetMethod("Open")!);il.Emit(System.Reflection.Emit.OpCodes.Ret);return call.CreateDelegate<Func<object,bool>>()(device);}
 static IDisposable Prepare(Assembly helper,object device)
 {try{return (IDisposable)helper.GetType("OneXLightingSession",true)!.GetMethod("Prepare",All)!.Invoke(null,[device])!;}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 internal static string[] Ops(MethodDefinition m)
 {
  var src=m.Body.Instructions.ToArray();var list=new List<string>();
  string Slot(Mono.Cecil.Cil.Instruction i)=>i.Operand is Mono.Cecil.Cil.VariableDefinition v?v.Index.ToString():i.OpCode.Name.Split('.').Last();
  for(int n=0;n<src.Length;n++)
  {
   var op=src[n].OpCode.Name;if(op=="nop")continue;
   if(op.StartsWith("stloc")&&n+1<src.Length&&src[n+1].OpCode.Name.StartsWith("ldloc")&&!src[n+1].OpCode.Name.StartsWith("ldloca")&&Slot(src[n])==Slot(src[n+1]))
   {var slot=Slot(src[n]);if(src.Count(i=>(i.OpCode.Name.StartsWith("stloc")||i.OpCode.Name.StartsWith("ldloc"))&&Slot(i)==slot)==2){n++;continue;}}
   if(op.StartsWith("ldloca"))op="ldloca";else if(op.StartsWith("ldloc"))op="ldloc";else if(op.StartsWith("stloc"))op="stloc";
   list.Add(op.EndsWith(".s")?op[..^2]:op);
  }
  return list.ToArray();
 }
 static void ModelAudit(string hc)
 {
  using var original=AssemblyDefinition.ReadAssembly(hc);using var model=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
  foreach(var type in new[]{"IDevice","IDevice::OpenLibSys","IDevice::ECRamDirectWriteByte","OneXAOKZOE","OneXPlayerX1"})
  {
   var parts=type.Split("::");var name="HandheldCompanion.Devices."+parts[0];var methodName=parts.Length==2?parts[1]:"Open";var a=original.MainModule.GetType(name).Methods.Single(m=>m.Name==methodName);var f=model.MainModule.GetType(name).Methods.Single(m=>m.Name==methodName);
   var ao=Ops(a);var fo=Ops(f);Check(ao.SequenceEqual(fo),"Complete Open opcodes match actual HC: "+type+" "+ao.Length+"/"+fo.Length);
   string Canon(string s)=>System.Text.RegularExpressions.Regex.Replace(s,@"<Open>b__\d+_\d+","<Open>lambda");
   var ac=a.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>Canon(((MethodReference)i.Operand).FullName)).ToArray();
   var fc=f.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>Canon(((MethodReference)i.Operand).FullName)).ToArray();
   if(!ac.SequenceEqual(fc))Audits.Add(new{type=name,actualCalls=ac,fixtureCalls=fc});Check(ac.SequenceEqual(fc),"Complete Open calls match actual HC: "+type);
   Audits.Add(new{type=name,opcodes=ao.Length,opcodeMatch=true,callMatch=true,calls=ac});
  }
  foreach(var spec in new[]{("IDevice","OpenLibSys"),("IDevice","Close"),("IDevice","OpenEvents"),("OneXAOKZOE","Close"),("OneXPlayerX1","Close"),("OneXPlayerX1","QuerySettings"),("OneXPlayerX1","Device_Inserted"),("OneXPlayerX1","StartVendorHidListener")})
  {
   var type=original.MainModule.GetType("HandheldCompanion.Devices."+spec.Item1);var method=type.Methods.Single(m=>m.Name==spec.Item2);
   Audits.Add(new{type=type.FullName,method=method.Name,evidenceKind="actual-PE-lifecycle-prerequisite-review",executed=false,instructions=method.Body.Instructions.Select(i=>i.Offset.ToString("x4")+": "+i.OpCode+" "+i.Operand).ToArray()});
  }
  var parent=original.MainModule.GetType("HandheldCompanion.Devices.OneXAOKZOE");
  foreach(var name in new[]{"SetFanControl","SetFanDuty"})
  {
   var method=parent.Methods.Single(m=>m.Name==name);var modelMethod=model.MainModule.GetType(parent.FullName).Methods.Single(m=>m.Name==name);
   Check(Ops(method).SequenceEqual(Ops(modelMethod)),"Complete fan opcodes match actual HC: "+name);
   var realCalls=method.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).ToArray();
   var fakeCalls=modelMethod.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).ToArray();
   Check(realCalls.SequenceEqual(fakeCalls),"Complete fan calls match actual HC: "+name);
   var operands=method.Body.Instructions.Select(i=>i.Operand?.ToString()??"").ToArray();var all=string.Join("\n",operands);
   Check(all.Contains("UseOpenLib")&&all.Contains("get_IsOpen")&&all.Contains("ECRamDirectWriteByte"),"Actual fan method retains OLS/open/EC prerequisites: "+name);
   Check(!all.Contains("SerialPort")&&!all.Contains("0xF1"),"Actual fan method has no LED serial transport dependency: "+name);
   Audits.Add(new{type=parent.FullName,method=name,operands,evidenceKind="actual-PE-static-fan-prerequisites",executed=false});
  }
 }
 static void Reset(){Check(Trace.Ports.Count==0,"No fixture serial leak before case");IDevice.ResetLibrary();Trace.Calls.Clear();Trace.SerialOpenAttempts=0;Trace.DriverInitializations=0;Trace.FailInit=false;}
 internal static int Run(string[] args)
 {
  var result=args[3];Exception? error=null;
  try
  {
   Check(Hash(args[0])=="7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f","Exact installed Host identity");
   Check(Hash(args[1])=="e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf","Exact installed lighting helper identity");
   Check(Hash(args[2])=="08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583","Exact HC PE identity");
   ModelAudit(args[2]);var host=Load(args[0]);var helper=Load(args[1]);
   Reset();using(var alone=Prepare(helper,new OneXPlayerX1()))Check(Trace.Ports.Contains("COM3"),"Helper alone owns its LED serial port");
   Check(Trace.Ports.Count==0,"Helper alone returns owned port");Cases.Add(new{id="lighting-alone",status="PASS",calls=Trace.Calls.ToArray()});
   foreach(var type in new[]{typeof(OneXPlayerX1AMD),typeof(OneXPlayerX1Intel),typeof(OneXPlayerX1Pro),typeof(HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex)})
   {
    Reset();var fan=(IDevice)Activator.CreateInstance(type)!;var backend=Backend(host,fan);var admission=Invoke(backend,"ObserveFanCapability")!;
    Check(admission.GetType().GetProperty("Supported")!.GetValue(admission) is true,"Actual Host capability policy admits exact inherited EC contract: "+type.Name);
    Check(admission.GetType().GetProperty("Declared")!.GetValue(admission) is false,"Fixture preserves actual X1 overwritten FanControl capability bit: "+type.Name);
    Invoke(backend,"OpenHcDevice");
    Check(fan.IsOpen&&Trace.Ports.Contains("COM3"),"Actual Host Open captures LED serial: "+type.Name);
    Exception? busy=null;try{using var session=Prepare(helper,Activator.CreateInstance(type)!);}catch(Exception e){busy=e;}
    while(busy is TargetInvocationException t && t.InnerException is not null)busy=t.InnerException;
    Check(busy is IOException&&busy.Message.Contains("SERIAL_PORT_BUSY"),"Actual helper rejects fan-owned port: "+type.Name);
    Check(Trace.Ports.Contains("COM3")&&((OneXPlayerX1)fan).HasPort,"Failed helper does not release fan-owned port: "+type.Name);
    Cases.Add(new{id="fan-then-lighting:"+type.Name,status="DEFECT_REPRODUCED_IN_MEMORY",error=busy!.Message,driverInitializations=Trace.DriverInitializations,serialOpenAttempts=Trace.SerialOpenAttempts,calls=Trace.Calls.ToArray()});
    // Fixture-owned resource release only; no claim about Host Close is made here.
    Trace.Ports.Clear();
   }
   Reset();using(var held=Prepare(helper,new OneXPlayerX1AMD()))
   {
    var fan=new OneXPlayerX1AMD();Invoke(Backend(host,fan),"OpenHcDevice");
    Check(fan.IsOpen&&!fan.EnableSerialPort&&!fan.HasPort,"Opposite order silently disables Host LED serial after busy");
    Check(Trace.Ports.Contains("COM3"),"Host busy fallback leaves lighting owner's port held");
    Cases.Add(new{id="lighting-then-fan",status="CONTENTION_REPRODUCED_IN_MEMORY",calls=Trace.Calls.ToArray()});
   }
   Reset();Trace.FailInit=true;var failedDevice=new OneXPlayerX1AMD();var failedBackend=Backend(host,failedDevice);Exception? firstFailure=null;
   try{Invoke(failedBackend,"OpenHcDevice");}catch(Exception e){firstFailure=e;}
   Check(firstFailure is not null&&!failedDevice.IsOpen&&!IDevice.LibraryInitialized&&Trace.Ports.Count==0,"First OLS initialization failure does not report an open device");
   Invoke(failedBackend,"OpenHcDevice");
   Check(failedDevice.IsOpen&&!IDevice.LibraryInitialized&&Trace.DriverInitializations==1,"Actual second Open accepts cached failed OLS object without retry");
   Cases.Add(new{id="failed-ols-cached-retry",status="DEFECT_REPRODUCED_IN_MEMORY",firstError=firstFailure!.Message,deviceOpen=failedDevice.IsOpen,libraryInitialized=IDevice.LibraryInitialized,driverInitializations=Trace.DriverInitializations,calls=Trace.Calls.ToArray()});
   Trace.Ports.Clear();
   Reset();var narrow=new OneXPlayerX1AMD();narrow.SetFanControl(true);narrow.SetFanDuty(50);
   Check(!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Actual inherited fan methods reject unopened transport");
   Check(BaseOpen(narrow)&&narrow.IsOpen,"Base transport feasibility retains OLS and open marker");
   narrow.SetFanControl(true);narrow.SetFanDuty(50);
   Check(Trace.Calls.Contains("FAN:044a:1")&&Trace.Calls.Contains("FAN:044b:92"),"Actual inherited fan methods still write configured control and scaled duty");
   using(var coexist=Prepare(helper,new OneXPlayerX1AMD()))Check(Trace.Ports.Contains("COM3"),"Feasibility-only base transport leaves LED port available");
   narrow.SetFanControl(false);Check(Trace.Calls.Contains("FAN:044a:0"),"Actual inherited automatic fan return remains reachable");
   Check(Trace.DriverInitializations==1&&!Trace.Calls.Any(x=>x.StartsWith("EC:")),"Feasibility-only narrow Open retains OLS without OEM writes");
   Cases.Add(new{id="base-transport-feasibility-only",status="FEASIBILITY_CONFIRMED_IN_MEMORY",productFixImplemented=false,hostLifecycleValidated=false,calls=Trace.Calls.ToArray()});
   IDevice.ResetLibrary(); narrow.LoseMarker();
   Check(Trace.Ports.Count==0,"All owned fixture serial resources released");Check(NativeAttempts==0,"No native IO attempted");
  }
  catch(Exception e){error=e;}
  var report=new{schemaVersion=1,status=error is null?"DEFECT_REPRODUCED_IN_MEMORY":"PROBE_FAILED",error=error?.ToString(),checks=Checks,cases=Cases,modelAudit=Audits,
   host=new{path=args[0],sha256=Hash(args[0]),mainInvoked=false},helper=new{path=args[1],sha256=Hash(args[1]),mainInvoked=false},hc=new{path=args[2],sha256=Hash(args[2]),codeExecuted=false},
   nativeAttempts=NativeAttempts,hardwareIo=false,physicalConflictObserved=false,productSourcesChanged=false,scope="Actual product Host capability admission and OpenHcDevice plus actual helper Prepare; source-derived complete Open hierarchy checked against HC PE; shared in-memory exclusive serial terminal. No real factory or physical enumeration; no Host Close acceptance."};
  File.WriteAllText(result,JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(report.status+" cases="+Cases.Count+" native="+NativeAttempts+" "+error?.Message);return error is null?0:2;
 }
}
