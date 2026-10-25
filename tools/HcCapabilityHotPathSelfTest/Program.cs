using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
if(args.Length is < 2 or > 3)throw new ArgumentException("host.dll result.json [--baseline]");
var hostPath=Path.GetFullPath(args[0]);var baseline=args.Length==3&&args[2]=="--baseline";
AssemblyLoadContext.Default.Resolving+=(_,name)=>{var p=Path.Combine(Path.GetDirectoryName(hostPath)!,name.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
var host=AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);var type=host.GetType("YeManFanHost.RealHcBackend",true)!;
var target=RuntimeHelpers.GetUninitializedObject(type);var flags=BindingFlags.Instance|BindingFlags.NonPublic;
var device=type.GetField("device",flags)!;var routeGetter=type.GetProperty("Route")!.GetMethod!.CreateDelegate<Func<object?>>(target);
var cacheField=type.GetField("routeTypeCache",flags);var cases=new List<object>();var failures=new List<string>();
void Check(string id,bool ok){cases.Add(new{id,ok});if(!ok)failures.Add(id);}
object MemoryDevice(string name)=>Activator.CreateInstance(AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("RouteMemory"+Guid.NewGuid().ToString("N")),AssemblyBuilderAccess.Run).DefineDynamicModule("memory").DefineType(name,TypeAttributes.Public).CreateType()!)!;
long Resolutions(){var c=cacheField?.GetValue(target);return c is null?0:(long)c.GetType().GetProperty("ResolutionCount")!.GetValue(c)!;}
var first=MemoryDevice("HandheldCompanion.Devices.ROGAlly");var second=MemoryDevice("HandheldCompanion.Devices.ROGAllyX");var unknown=MemoryDevice("Unreviewed.Vendor.ROGAlly");
device.SetValue(target,first);var expected=routeGetter();Check("known model binds",expected is not null);
for(var i=0;i<100_000;i++)if(!ReferenceEquals(expected,routeGetter()))throw new Exception("route changed on stable device");
if(!baseline){Check("stable device type resolves once",Resolutions()==1);device.SetValue(target,second);var other=routeGetter();Check("new device type rebinds",other is not null&&!ReferenceEquals(expected,other)&&Resolutions()==2);
 device.SetValue(target,unknown);for(var i=0;i<10_000;i++)if(routeGetter() is not null)throw new Exception("unregistered class gained route");Check("unknown type is negatively cached",Resolutions()==3);
 device.SetValue(target,null);Check("missing device refuses route",routeGetter() is null);device.SetValue(target,first);Check("after missing device rebinds",ReferenceEquals(expected,routeGetter())&&Resolutions()==4);
 var replacement=MemoryDevice("HandheldCompanion.Devices.ROGAlly");device.SetValue(target,replacement);Check("same name from changed runtime Type rebinds",ReferenceEquals(expected,routeGetter())&&Resolutions()==5);
 type.GetField("routeOverrideForSelfTest",flags)!.SetValue(target,expected);device.SetValue(target,unknown);Check("existing explicit test override preserved",ReferenceEquals(expected,routeGetter())&&Resolutions()==5);type.GetField("routeOverrideForSelfTest",flags)!.SetValue(target,null);Check("cleared override cannot retain support",routeGetter() is null&&Resolutions()==6);
}
device.SetValue(target,first);routeGetter();for(var i=0;i<1_000_000;i++)routeGetter();
const int queries=1_000_000;var ns=new List<double>();long allocation=0,success=0;
for(var round=0;round<7;round++){var memory=GC.GetAllocatedBytesForCurrentThread();var at=Stopwatch.GetTimestamp();for(var i=0;i<queries;i++)if(routeGetter() is not null)success++;var elapsed=Stopwatch.GetElapsedTime(at);allocation+=GC.GetAllocatedBytesForCurrentThread()-memory;ns.Add(elapsed.TotalNanoseconds/queries);}
ns.Sort();Check("all measured queries reached route",success==queries*7L);if(!baseline)Check("hot route queries allocate no objects",allocation==0);
Check("HC assembly never loaded",!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"));
var backendPath=Path.Combine(Path.GetDirectoryName(hostPath)!,"YeManFanBackend.dll");
var result=new{status=failures.Count==0?(baseline?"BASELINE_OBSERVED":"PASS"):"FAIL",baseline,checks=cases.Count,failures,cases,cachePresent=cacheField is not null,hotLookup=new{queriesPerRound=queries,rounds=7,medianNanoseconds=ns[3],roundNanoseconds=ns,allocatedBytes=allocation},hardwareIo=false,hostSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(hostPath))).ToLowerInvariant(),backendSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(backendPath))).ToLowerInvariant()};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);File.WriteAllText(args[1],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true})+"\n");Console.WriteLine(JsonSerializer.Serialize(result));return failures.Count==0?0:1;
