// Exact scratch Host DLL, null backend: no Host process, HC, IPC or hardware.
using System.IO;
using System.Buffers;
using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

var hostPath=Path.GetFullPath(args[0]);var evidence=Path.GetFullPath(args[1]);Directory.CreateDirectory(evidence);
Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",Path.Combine(evidence,"logs"));
AssemblyLoadContext.Default.Resolving+=(_,name)=>{var p=Path.Combine(Path.GetDirectoryName(hostPath)!,name.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
var host=AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);
const BindingFlags flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
Type T(string name)=>host.GetType("YeManFanHost."+name,true)!;
T("Program").GetProperty("IsSelfTestProcess",flags)!.SetValue(null,true);
var options=(JsonSerializerOptions)T("JsonDefaults").GetField("Options",flags)!.GetValue(null)!;
var stateType=T("HostState");var rows=new List<object>();var passed=true;
object? Call(object instance,string name,params object?[] values){try{return instance.GetType().GetMethods(flags).Single(m=>m.Name==name&&m.GetParameters().Length==values.Length).Invoke(instance,values);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
object? Get(object instance,string name)=>instance.GetType().GetProperty(name,flags)!.GetValue(instance);
void Set(object instance,string name,object? value)=>instance.GetType().GetProperty(name,flags)!.SetValue(instance,value);
object Field(object instance,string name)=>instance.GetType().GetField(name,flags)!.GetValue(instance)!;
string Json(object value)=>JsonSerializer.Serialize(value,value.GetType(),options);
object OldCopy(object value)=>JsonSerializer.Deserialize(JsonSerializer.SerializeToUtf8Bytes(value,stateType,options),stateType,options)!;
void Require(bool condition,string text="assertion failed"){if(!condition)throw new InvalidOperationException(text);}
void Equal(object? a,object? b){Require(Equals(a,b),$"expected {b}, got {a}");}
void Check(string name,Action action){try{action();rows.Add(new{name,ok=true});Console.WriteLine("PASS "+name);}catch(Exception error){passed=false;rows.Add(new{name,ok=false,error=error.ToString()});Console.WriteLine("FAIL "+name+": "+error.Message);}}
object? Value(Type type,int seed){
 var underlying=Nullable.GetUnderlyingType(type);if(underlying is not null)return Value(underlying,seed);
 if(type==typeof(string))return $"证据-{seed}-\"-\\-\n";
 if(type==typeof(bool))return seed%2==0;
 if(type==typeof(int))return seed;
 if(type==typeof(long))return (long)seed+100;
 if(type==typeof(double))return seed+0.125;
 if(type==typeof(Dictionary<string,string>))return new Dictionary<string,string>{{"a","first"},{"机型",$"state-{seed}"}};
 if(type.IsGenericType&&type.GetGenericTypeDefinition()==typeof(IReadOnlyList<>)){
  var item=type.GetGenericArguments()[0];var result=Array.CreateInstance(item,4);for(var i=0;i<4;i++)result.SetValue(Activator.CreateInstance(item,(double)i*25,(double)i*30),i);return result;
 }
 var ctor=type.GetConstructors(flags).First(c=>c.IsPublic&&!c.GetParameters().Any(p=>p.ParameterType==type));
 return ctor.Invoke(ctor.GetParameters().Select((p,i)=>Value(p.ParameterType,seed+i)).ToArray());
}
object Sample(int seed=1){var result=Activator.CreateInstance(stateType)!;foreach(var p in stateType.GetProperties(flags).Where(p=>p.SetMethod is not null&&!p.SetMethod.IsStatic))p.SetValue(result,Value(p.PropertyType,seed++));Set(result,"State","Ready");Set(result,"PowerState","On");return result;}
var argv=new[]{"--mock-handshake"};var hostOptions=T("HostOptions").GetMethod("Parse",flags)!.Invoke(null,[argv])!;
var engine=Activator.CreateInstance(T("FanHostEngine"),[hostOptions,null])!;
var state=Field(engine,"state");var engineGate=Field(engine,"gate");var source=Sample();
foreach(var p in stateType.GetProperties(flags).Where(p=>p.SetMethod is not null&&!p.SetMethod.IsStatic))p.SetValue(state,p.GetValue(source));
Require(engine.GetType().GetField("realBackend",flags)!.GetValue(engine) is null,"must have no HC backend");
object Expected(string? phase=null){var copy=OldCopy(state);if(phase is not null)Set(copy,"State",phase);Call(engine,"ApplyDerivedSnapshotFields",copy);return copy;}
Check("all public writable Host fields survive complete Snapshot JSON oracle",()=>Equal(Json(Call(engine,"Snapshot")!),Json(Expected())));
Check("read path publishes the returned whole object and preserves complete fields",()=>{var actual=Call(engine,"SnapshotForRead")!;Equal(Json(actual),Json(Expected()));Require(ReferenceEquals(actual,Field(engine,"lastPublishedSnapshot")));});
Check("phase override occurs before deriving resume and control permission",()=>{lock(engineGate){var actual=Call(engine,"PublishSnapshotLocked","Resuming")!;Equal(Json(actual),Json(Expected("Resuming")));Equal(Get(state,"State"),"Ready");Equal(Get(actual,"ResumePhase"),"Resuming");Equal(Get(actual,"ControlAccepting"),false);}});
Check("write-side SnapshotUnlocked uses the same full derived state",()=>{lock(engineGate){Equal(Json(Call(engine,"SnapshotUnlocked")!),Json(Expected()));}});
foreach(var property in stateType.GetProperties(flags).Where(p=>p.SetMethod is not null&&!p.SetMethod.IsStatic))
 Check("changed field JSON oracle: "+property.Name,()=>{
  var old=property.GetValue(state);
  try{
   var changed=Nullable.GetUnderlyingType(property.PropertyType) is not null?null:property.PropertyType==typeof(bool)?!(bool)old!:Value(property.PropertyType,440);
   property.SetValue(state,changed);var actual=Call(engine,"Snapshot")!;Equal(Json(actual),Json(Expected()));
  }finally{property.SetValue(state,old);}
 });
Check("snapshots detach both dictionaries and curve nodes",()=>{
 var copy=Call(engine,"Snapshot")!;var before=Json(state);
 ((IDictionary)Get(copy,"DeviceIdentity")!)["a"]="consumer-change";
 ((IDictionary)Get(copy,"HcManagerStatuses")!)["a"]="consumer-change";
 var node=((IEnumerable)Get(copy,"ActiveCurve")!).Cast<object>().First();Set(node,"DutyPercent",1.0);
 Set(copy,"State","Suspended");Equal(Json(state),before);
});
Check("null omission and constructor defaults remain JSON-equivalent",()=>{
 var old=Get(state,"PowerState");try{Set(state,"PowerState",null);Equal(Json(Call(engine,"Snapshot")!),Json(Expected()));Equal(Get(Call(engine,"Snapshot")!,"PowerState"),"Unknown");}finally{Set(state,"PowerState",old);}
});
Check("lease expiry and generation remain fresh without mutating the earlier snapshot",()=>{
 var before=Call(engine,"Snapshot")!;var original=Json(before);Set(state,"Lease",Activator.CreateInstance(T("FanLease"),"fixture-lease",52L,123456L,"fixture"));Set(state,"LeaseGeneration",52L);
 var now=Call(engine,"Snapshot")!;Equal(Get(Get(now,"Lease")!,"Generation"),52L);Equal(Get(Get(now,"Lease")!,"ExpiresAtMonoMs"),123456L);Equal(Json(before),original);
});
Check("published read does not wait on a held engine gate",()=>{
 var published=Call(engine,"SnapshotForRead")!;using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
 var holder=Task.Run(()=>{lock(engineGate){entered.Set();Require(release.Wait(TimeSpan.FromSeconds(3)),"fixture release timed out");}});
 Require(entered.Wait(TimeSpan.FromSeconds(2)),"fixture gate entry timed out");try{var reader=Task.Run(()=>Call(engine,"SnapshotForReadBounded",0));Require(reader.Wait(TimeSpan.FromSeconds(1)),"read wrongly waited for held gate");Require(ReferenceEquals(reader.Result,published));}finally{release.Set();holder.GetAwaiter().GetResult();}
});
var copierField=engine.GetType().GetField("snapshotJsonCopy",flags);
Check("engine uses its own reusable JSON scratch buffer",()=>Require(copierField is not null));
if(copierField is not null){
 var copier=copierField.GetValue(engine)!;var buffer=(ArrayBufferWriter<byte>)Field(copier,"buffer");
 byte[]? Storage(){MemoryMarshal.TryGetArray(buffer.WrittenMemory,out ArraySegment<byte> segment);return segment.Array;}
 Check("stable shape reuses buffer and resets written length",()=>{Call(engine,"Snapshot");var storage=Storage();Require(storage is not null);for(var i=0;i<12;i++){Call(engine,"Snapshot");Require(ReferenceEquals(Storage(),storage));Equal(buffer.WrittenCount,0);}});
 Check("buffer growth retains old deep snapshots and later short state",()=>{
  var old=Get(state,"LastError");try{var earlier=Call(engine,"Snapshot")!;var bytes=Json(earlier);Set(state,"LastError",new string('x',20000)+"尾部");Equal(Json(Call(engine,"Snapshot")!),Json(Expected()));Set(state,"LastError","short");Equal(Json(Call(engine,"Snapshot")!),Json(Expected()));Equal(Json(earlier),bytes);}finally{Set(state,"LastError",old);}
 });
 foreach(var bad in new[]{double.NaN,double.PositiveInfinity,double.NegativeInfinity})Check("invalid number still rejects and next manual/state snapshot is usable: "+bad,()=>{
  var old=Get(state,"LastTemperatureC");Set(state,"LastTemperatureC",bad);var rejected=false;try{Call(engine,"Snapshot");}catch(ArgumentException){rejected=true;}finally{Set(state,"LastTemperatureC",old);}Require(rejected);Equal(buffer.WrittenCount,0);Equal(Json(Call(engine,"Snapshot")!),Json(Expected()));
 });
 Check("one shared copier keeps parallel independent source states isolated",()=>{
  var jobs=Enumerable.Range(1,24).Select(i=>Task.Run(()=>{var input=Sample(i*100);Equal(Json(Call(copier,"Copy",input)!),Json(OldCopy(input)));})).ToArray();Require(Task.WaitAll(jobs,TimeSpan.FromSeconds(3)),"copy fixture failed to settle");Equal(buffer.WrittenCount,0);
 });
 Check("consecutive states and explicit null fields cannot borrow cached values",()=>{var first=Sample(200);var second=Activator.CreateInstance(stateType)!;Call(copier,"Copy",first);Equal(Json(Call(copier,"Copy",second)!),Json(OldCopy(second)));});
 foreach(var method in new[]{"Snapshot","SnapshotForRead","SnapshotUnlocked"})Check("production copy site really uses scratch storage: "+method,()=>{
  var old=Get(state,"LastError");try{var capacity=buffer.Capacity;Set(state,"LastError",new string('x',capacity+32));lock(engineGate){Equal(Json(Call(engine,method)!),Json(Expected()));}Require(buffer.Capacity>capacity,"production site bypassed buffer");Equal(buffer.WrittenCount,0);}finally{Set(state,"LastError",old);}
 });
}
// Retire only this null-backend engine; do not leave mock cleanup intent behind.
foreach(var name in new[]{"OpenCalled","OpenEventsCalled","UnknownState","HardwareWritesEnabled","HardwareWritesObserved","HcCloseCleanupPending"})Set(state,name,false);
Call(engine,"Dispose");
var report=new{status=passed?"passed":"failed",checks=rows.Count,results=rows,hostDll=hostPath,actualProductionEngine=true,jsonOracle=true,realHostProcess=false,realHcAssembly=false,physicalHardwareWrites=0,cpuMeasurements=false};
File.WriteAllText(Path.Combine(evidence,"result.json"),JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"Snapshot copy {report.status}: {rows.Count} checks");return passed?0:1;
