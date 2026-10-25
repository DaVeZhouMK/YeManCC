using System.Collections.Concurrent;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

internal static class Program {
    private static string HostPath = null!;
    private static int checks; private static readonly List<string> failures = [];
    private static void Check(string name, Action test) { ++checks; try { test(); } catch(Exception ex) { failures.Add(name + ": " + ex.Message); } }
    private static void Equal(object? actual, object? expected) { if (!Equals(actual, expected)) throw new Exception($"expected {expected ?? "null"}, got {actual ?? "null"}"); }
    public static int Main(string[] args) {
        if (args.Length != 1) throw new ArgumentException("Provide the exact scratch-built YeManFanHost.dll path");
        HostPath = Path.GetFullPath(args[0]);
        AssemblyLoadContext.Default.Resolving += (_, name) => {
            var candidate = Path.Combine(Path.GetDirectoryName(HostPath)!, name.Name + ".dll");
            return File.Exists(candidate) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(candidate) : null;
        };
        var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(HostPath);
        var backend = host.GetType("YeManFanHost.RealHcBackend", true)!;
        const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
        var invoke = backend.GetMethod("Invoke", flags)!; var tryInvokeCached = backend.GetMethod("TryInvokeCached", flags)!; var getMember = backend.GetMethod("GetMember", flags)!;
        var findNoArgMethod = backend.GetMethod("FindNoArgMethod", flags)!;
        var resolveRoute = backend.GetMethod("ResolveRoute", flags)!;
        var routeTable = (System.Collections.IDictionary)backend.GetField("FanRoutes", flags)!.GetValue(null)!;
        var getStaticMember = backend.GetMethod("GetStaticMember", flags)!; var findDeclaredStaticField = backend.GetMethod("FindDeclaredStaticFieldMetadata", flags)!;
        object? Call(MethodInfo method, object?[] args) {
            try { return method.Invoke(null, args); } catch(TargetInvocationException ex) when(ex.InnerException is not null) { throw ex.InnerException; }
        }
        object? Invoke(object target, string name, params object?[] args) => Call(invoke, [target, name, args]);
        bool TryInvokeCached(object target, string name, object?[] args, out object? result) { var callArgs = new object?[] { target, name, args, null }; var found = (bool)Call(tryInvokeCached, callArgs)!; result = callArgs[3]; return found; }
        MethodInfo? FindNoArg(Type type, string name) => Call(findNoArgMethod, [type, name]) as MethodInfo;
        object? Member(object target, string name) => Call(getMember, [target, name]);
        object? StaticMember(Type type, string name) => Call(getStaticMember, [type, name]);
        FieldInfo? StaticField(Type type, string name) => Call(findDeclaredStaticField, [type, name]) as FieldInfo;
        Check("method metadata never caches returned values", () => { var x=new Probe{Value=4};Equal(Invoke(x,"Next"),5);Equal(Invoke(x,"Next"),6);Equal(x.Value,6); });
        Check("same runtime type still invokes current target", () => { Equal(Invoke(new Probe{Value=10},"Next"),11);Equal(Invoke(new Probe{Value=20},"Next"),21); });
        Check("method name and argument count keep separate entries", () => { var x=new Probe{Value=4};Equal(Invoke(x,"Read"),4);Equal(Invoke(x,"Read",3),7);Equal(Invoke(x,"Read"),4); });
        Check("optional cached method keeps current target values", () => { var x=new Probe{Value=4}; object? result; if(!TryInvokeCached(x,"Next",[],out result)) throw new Exception("method not found"); Equal(result,5); if(!TryInvokeCached(x,"Next",[],out result)) throw new Exception("method not found"); Equal(result,6); });
        Check("optional cached method preserves missing-member result", () => { object? result; if(TryInvokeCached(new Probe(),"Absent",[],out result)) throw new Exception("unexpected method"); Equal(result,null); });
        Check("no-arg metadata lookup preserves current target values", () => { var x=new Probe{Value=4}; var method=FindNoArg(typeof(Probe),"Next") ?? throw new Exception("method not found"); Equal(method.Invoke(x,null),5); Equal(method.Invoke(x,null),6); });
        Check("no-arg metadata lookup preserves absent-member result", () => { Equal(FindNoArg(typeof(Probe),"Absent"),null); Equal(FindNoArg(typeof(Probe),"Absent"),null); });
        Check("public no-arg probe cannot expose private or required-argument methods", () => { Equal(FindNoArg(typeof(Probe),"PrivateMethod"),null); Equal(FindNoArg(typeof(Probe),"Overload"),null); });
        Check("public no-arg probe keeps virtual dispatch and exceptions live", () => { Equal(FindNoArg(typeof(DerivedProbe),"Kind")!.Invoke(new DerivedProbe(),null),"derived"); for(var i=0;i<2;i++){try{FindNoArg(typeof(Probe),"Fail")!.Invoke(new Probe(),null);throw new Exception("unexpected success");}catch(TargetInvocationException ex){if(ex.InnerException is not InvalidOperationException)throw;}} });
        Check("cached method still uses actual virtual dispatch", () => { Equal(Invoke(new DerivedProbe(),"Kind"),"derived");Equal(Invoke(new Probe(),"Kind"),"base"); });
        Check("private instance method remains reachable", () => Equal(Invoke(new Probe(),"PrivateMethod"),"private"));
        Check("missing method is still an exception", () => { for(int i=0;i<2;i++){try{Invoke(new Probe(),"Absent");throw new Exception("unexpected success");}catch(MissingMethodException){}} });
        Check("target exception is still reported by MethodInfo.Invoke", () => {try{Invoke(new Probe(),"Fail");throw new Exception("unexpected success");}catch(TargetInvocationException ex){if(ex.InnerException is not InvalidOperationException)throw;} });
        Check("same-arity overload preserves first original resolver and binder", () => {
            var target=new Probe();var original=target.GetType().GetMethods(BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic).First(m=>m.Name=="Overload"&&m.GetParameters().Length==1);
            object? arg=original.GetParameters()[0].ParameterType==typeof(string)?"value":7;
            Equal(Invoke(target,"Overload",arg),original.Invoke(target,[arg]));
        });
        Check("property metadata does not cache mutable values", () => {var x=new Probe{Value=1};Equal(Member(x,"Property"),1);x.Value=8;Equal(Member(x,"Property"),8);});
        Check("field metadata reads current target", () => {Equal(Member(new Probe{Value=1},"Value"),1);Equal(Member(new Probe{Value=9},"Value"),9);});
        Check("getter executes on every read", () => {var x=new Probe();Equal(Member(x,"Counted"),1);Equal(Member(x,"Counted"),2);});
        Check("most-derived field beats a base property", () => Equal(Member(new FieldHidesProperty(),"Item"),"derived-field"));
        Check("most-derived property beats a base field", () => Equal(Member(new PropertyHidesField(),"Item"),"derived-property"));
        Check("private base member remains readable", () => Equal(Member(new DerivedProbe(),"PrivateField"),"base-private"));
        Check("missing members remain null and are not inserted in cache", () => {
            var dictionary=backend.GetField("instanceMemberMetadata",flags)!.GetValue(null)!; var count=dictionary.GetType().GetProperty("Count")!; var before=count.GetValue(dictionary);
            Equal(Member(new Probe(),"AbsentProperty"),null);Equal(Member(new Probe(),"AbsentProperty"),null);Equal(count.GetValue(dictionary),before);
        });
        Check("static metadata reads current property and field values", () => {
            StaticProbe.Value = 3; Equal(StaticMember(typeof(StaticProbe), "Property"), 3);
            StaticProbe.Value = 8; Equal(StaticMember(typeof(StaticProbe), "Property"), 8);
            StaticProbe.Field = 11; Equal(StaticMember(typeof(StaticProbe), "Field"), 11);
        });
        Check("static metadata preserves base lookup", () => { StaticProbe.Value = 17; Equal(StaticMember(typeof(DerivedStaticProbe), "Property"), 17); });
        Check("declared static field metadata reuses immutable FieldInfo", () => { var first = StaticField(typeof(StaticProbe), "Field"); var second = StaticField(typeof(StaticProbe), "Field"); if(first is null || !ReferenceEquals(first, second)) throw new Exception("FieldInfo was not cached"); Equal(StaticField(typeof(StaticProbe), "AbsentField"), null); });
        Check("missing static members remain null and are not inserted in cache", () => {
            var dictionary=backend.GetField("staticMemberMetadata",flags)!.GetValue(null)!; var count=dictionary.GetType().GetProperty("Count")!; var before=count.GetValue(dictionary);
            Equal(StaticMember(typeof(StaticProbe),"AbsentProperty"),null);Equal(StaticMember(typeof(StaticProbe),"AbsentProperty"),null);Equal(count.GetValue(dictionary),before);
        });
        Check("getter exceptions are not swallowed or cached", () => {for(int i=0;i<2;i++){try{Member(new Probe(),"Throwing");throw new Exception("unexpected success");}catch(TargetInvocationException ex){if(ex.InnerException is not InvalidOperationException)throw;}}});
        Check("same full name in different runtime Types does not cross-bind", () => {
            object Make(string assemblyName,int value){var a=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName(assemblyName),AssemblyBuilderAccess.Run);var m=a.DefineDynamicModule("Main");var t=m.DefineType("Fixtures.SameName",TypeAttributes.Public);t.DefineField("Value",typeof(int),FieldAttributes.Public);var type=t.CreateType()!;var instance=Activator.CreateInstance(type)!;type.GetField("Value")!.SetValue(instance,value);return instance;}
            Equal(Member(Make("FixtureA",31),"Value"),31);Equal(Member(Make("FixtureB",62),"Value"),62);
        });
        Check("concurrent metadata reuse retains per-object values", () => {
            var bad=new ConcurrentQueue<string>();Parallel.For(0,16,i=>{var x=new Probe{Value=i};if(!Equals(Invoke(x,"Next"),i+1)||!Equals(Member(x,"Value"),i+1))bad.Enqueue(i.ToString());});if(!bad.IsEmpty)throw new Exception(string.Join(',',bad));
        });
        object? Route(string? fullName) => Call(resolveRoute, [fullName]);
        object? OriginalRoute(string? fullName) {
            if (string.IsNullOrWhiteSpace(fullName)) return null;
            if (routeTable.Contains(fullName)) return routeTable[fullName];
            var shortName=fullName[(fullName.LastIndexOf('.')+1)..];
            foreach (System.Collections.DictionaryEntry entry in routeTable)
                if (((string)entry.Key).EndsWith("."+shortName,StringComparison.Ordinal)) return entry.Value;
            return null;
        }
        Check("all registered routes and namespace aliases preserve exact original object mapping", () => {
            if(routeTable.Count!=70)throw new Exception("route table unexpectedly changed");
            foreach(System.Collections.DictionaryEntry entry in routeTable) {
                var fullName=(string)entry.Key;var shortName=fullName[(fullName.LastIndexOf('.')+1)..];
                foreach(var name in new[]{fullName,"HandheldCompanion.Devices.MSI."+shortName,"HandheldCompanion.Devices.OtherVendor."+shortName})
                    for(var repeat=0;repeat<3;repeat++)if(!ReferenceEquals(Route(name),OriginalRoute(name)))throw new Exception("route mapping changed: "+name);
            }
        });
        Check("route lookup preserves absent, case-sensitive and cross-family results", () => {
            foreach(var name in new string?[]{null,""," ","UnknownDevice","HandheldCompanion.Devices.MSI.clawCG3EM","HandheldCompanion.Devices.GPDWin5Extra"})
                for(var repeat=0;repeat<2;repeat++)Equal(Route(name),null);
            var names=new[]{"HandheldCompanion.Devices.MSI.ClawCG3EM","HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.GPD.GPDWin5","HandheldCompanion.Devices.MSI.ClawCG3EM"};
            foreach(var name in names)if(!ReferenceEquals(Route(name),OriginalRoute(name)))throw new Exception("cross-family binding: "+name);
        });
        var temperature=host.GetType("YeManFanHost.HWiNFOTemperatureMonitor",true)!;
        var monitor=Activator.CreateInstance(temperature)!;
        const BindingFlags instanceFlags = BindingFlags.Instance | BindingFlags.NonPublic;
        var read=temperature.GetMethod("ReadSnapshot",instanceFlags,null,[typeof(MemoryMappedViewAccessor)],null)!;
        object? Snapshot((string Label,double Value)[] samples,uint signature=0x53695748,uint? countOverride=null,uint size=316,uint offset=44) {
            // Each anonymous fixture map is a new producer identity. Production
            // TryReadSnapshot invalidates label metadata at that same boundary;
            // the private reader alone cannot infer a new unnamed mapping.
            temperature.GetMethod("InvalidateCachedMapping",instanceFlags)!.Invoke(monitor,[null]);
            using var map=MemoryMappedFile.CreateNew(null,4096);using var view=map.CreateViewAccessor();
            view.Write(0,signature);view.Write(32,offset);view.Write(36,size);view.Write(40,countOverride??(uint)samples.Length);
            for(var i=0;i<samples.Length;i++){var at=44+i*316;var bytes=Encoding.ASCII.GetBytes(samples[i].Label);view.WriteArray(at+12,bytes,0,bytes.Length);view.Write(at+284,samples[i].Value);}
            object? value;
            try { value=read.Invoke(monitor,[view]); }
            catch(TargetInvocationException ex) when(ex.InnerException is not null) { throw ex.InnerException; }
            return value?.GetType().GetProperty("TemperatureC")!.GetValue(value);
        }
        Check("mapping scan selects max Tctl/Tdie over CPU Package",()=>Equal(Snapshot([("CPU Package",95),("CPU Tctl/Tdie",55),("CPU Tdie",63)]),63f));
        Check("mapping scan selects maximum package when no Tctl/Tdie",()=>Equal(Snapshot([("CPU Package",50),("CPU Package",71)]),71f));
        Check("scan ignores GPU and power/RPM/invalid temperature labels",()=>Equal(Snapshot([("GPU Temperature",80),("CPU Tdie Power",70),("CPU Package",0),("CPU Tdie",double.NaN),("CPU Package",101)]),null));
        Check("a new map and scan do not reuse an old selected temperature",()=>{Equal(Snapshot([("CPU Package",60)]),60f);Equal(Snapshot([("CPU Package",40)]),40f);Equal(Snapshot([("GPU Temperature",70)]),null);});
        Check("wrong mapping signature is rejected",()=>Equal(Snapshot([("CPU Package",60)],signature:0),null));
        Check("zero or excessive count is rejected",()=>{Equal(Snapshot([],countOverride:0),null);Equal(Snapshot([],countOverride:100001),null);});
        Check("undersized structure and out-of-range layout are rejected",()=>{Equal(Snapshot([],size:315,countOverride:1),null);Equal(Snapshot([],offset:43,countOverride:1),null);Equal(Snapshot([],offset:4000,countOverride:1),null);});
        Check("no HandheldCompanion assembly was loaded by these pure checks",()=>{if(AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"))throw new Exception("HC unexpectedly loaded");});
        Console.WriteLine(JsonSerializer.Serialize(new {ok=failures.Count==0,checks,failed=failures.Count,failures,scope="actual-scratch-host-dll-pure-static-metadata-methods-and-anonymous-mapping-no-host-start-no-hc-device-no-cpu-benchmark"},new JsonSerializerOptions{WriteIndented=true}));
        return failures.Count==0?0:1;
    }
    public class Probe { public int Value; private string PrivateField="base-private";public int Property=>Value;public int Counted=>++Value;public string Throwing=>throw new InvalidOperationException("fixture");public int Next()=>++Value;public int Read()=>Value;public int Read(int delta)=>Value+delta;public virtual string Kind()=>"base";private string PrivateMethod()=>"private";public void Fail()=>throw new InvalidOperationException("fixture");public string Overload(string x)=>"string:"+x;public string Overload(int x)=>"int:"+x; }
    public class DerivedProbe:Probe {public override string Kind()=>"derived";}
    public class StaticBaseProbe { public static int Value; public static int Property => Value; }
    public class StaticProbe:StaticBaseProbe { public static int Field; }
    public class DerivedStaticProbe:StaticProbe { }
    public class BaseProperty {public string Item=>"base-property";} public class FieldHidesProperty:BaseProperty {public new string Item="derived-field";}
    public class BaseField {public string Item="base-field";} public class PropertyHidesField:BaseField {public new string Item=>"derived-property";}
}
