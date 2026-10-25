using System.IO;
using System.Text.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Managers;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    static int MutateCloseReleaseGuard(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);
        using var assembly=AssemblyDefinition.ReadAssembly(source);
        var method=assembly.MainModule.GetType("YeManFanHost.FanHostEngine").Methods.Single(m=>m.Name=="CloseHcSessionForLifecycle");
        var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.Name=="RouteLostCloseIsRelease");
        var reference=(MethodReference)call.Operand;
        Check(call.OpCode==OpCodes.Call&&reference.Parameters.Count==2&&reference.ReturnType.FullName=="System.Boolean","Exact existing release predicate is required");
        // Consume the original two arguments, force only this close verdict true.
        // Original virtual Close, cleanup, and public feedback still execute.
        var il=method.Body.GetILProcessor();call.OpCode=OpCodes.Pop;call.Operand=null;
        var pop=il.Create(OpCodes.Pop);il.InsertAfter(call,pop);il.InsertAfter(pop,il.Create(OpCodes.Ldc_I4_1));
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);assembly.Write(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list)=>list.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(h=>h.ToString())):"no-body");
        using var original=AssemblyDefinition.ReadAssembly(source);using var after=AssemblyDefinition.ReadAssembly(target);
        var a=Types(original.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"No method added/removed by close fault");
        var changed=a.Keys.Where(k=>a[k]!=b[k]).ToArray();Check(changed.Length==1&&changed[0]==method.FullName,"Only existing lifecycle close verdict changed");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="force only the existing close release-safety verdict true; preserve real Close and cleanup",nativeIo=false}));
        return 0;
    }
    static void CheckCloseSubscriptions(bool seeded)
    {
        Check(ManagerFactory.settingsManager.InitializedSubscribers==(seeded?1:0)&&ManagerFactory.settingsManager.SettingSubscribers==(seeded?1:0),"Settings subscriptions reflect original root Close");
        Check(ManagerFactory.powerProfileManager.InitializedSubscribers==(seeded?1:0),"Profile initialized subscription reflects original root Close");
        Check(ManagerFactory.deviceManager.InitializedSubscribers==(seeded?1:0)&&ManagerFactory.deviceManager.DeviceSubscribers==(seeded?4:0),"Device subscriptions reflect original root Close");
        Check(ConsumerFixture.CloseTerminals.PowerSubscribers==(seeded?1:0),"Power subscription reflects original CEc Close");
        Check(ControllerManager.Subscribers==(seeded?1:0),"Input subscription reflects original FlipDS Close");
        Check(SystemManager.Subscribers==(seeded?3:0),"System power/lock subscriptions reflect original AYANEO Close");
    }
    // Existing lifecycle-thread entry is an explicit fixture precondition.
    // This scenario tests complete Close consumers, not dispatcher bootstrap.
    static object CloseEngine(object backend){Set(backend,"staThread",Thread.CurrentThread);return Engine(backend);}
    static void RunGenericCloseCases(string actualHc)
    {
        Run("generic-close-full-frozen-contract",()=>
        {
            using var actual=AssemblyDefinition.ReadAssembly(actualHc);
            using var fixture=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
            foreach(var name in new[]{"HandheldCompanion.Devices.IDevice","HandheldCompanion.Devices.AYANEO.AYANEODevice","HandheldCompanion.Devices.AYANEO.AYANEODeviceCEc","HandheldCompanion.Devices.AYANEOFlipDS"})
            {
                var a=actual.MainModule.GetType(name).Methods.Single(m=>m.Name=="Close");
                var f=fixture.MainModule.GetType(name).Methods.Single(m=>m.Name=="Close");
                Check(OldBinaryProbe.Ops(a).SequenceEqual(OldBinaryProbe.Ops(f)),"Complete original Close opcodes match actual HC PE: "+name);
                var expectedCalls=a.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).ToArray();
                var fixtureCalls=f.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).ToArray();
                Check(expectedCalls.SequenceEqual(fixtureCalls),"All original Close calls match actual HC PE: "+name+" "+JsonSerializer.Serialize(new{expectedCalls,fixtureCalls}));
                Check(a.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName).SequenceEqual(f.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName)),"All original Close fields match actual HC PE: "+name);
                Check(a.Body.ExceptionHandlers.Select(h=>h.HandlerType+":"+h.CatchType?.FullName).SequenceEqual(f.Body.ExceptionHandlers.Select(h=>h.HandlerType+":"+h.CatchType?.FullName)),"Original Close exception regions match: "+name);
            }
            foreach(var name in new[]{"HandheldCompanion.Devices.AYANEOFlipDS","HandheldCompanion.Devices.AYANEOFlipKB","HandheldCompanion.Devices.AYANEO.AYANEODeviceCEc","HandheldCompanion.Devices.AYANEO.AYANEODevice"})
                Check(actual.MainModule.GetType(name).BaseType.FullName==fixture.MainModule.GetType(name).BaseType.FullName,"Actual Close inheritance chain retained: "+name);
        });
        Run("generic-close-source-order-unsubscribes-all",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);d.SeedFixtureCloseSubscriptions();CheckCloseSubscriptions(true);
            Profile(b,true);Duty(b,50);Trace.Calls.Clear();Invoke(b,"BlockWritesForSuspend");Close(b);
            var calls=Trace.Calls;var controller=calls.IndexOf("Controller.Unsubscribe");var rgb=calls.IndexOf("RGB.Release");var power=calls.IndexOf("Power.Unsubscribe");var fan=calls.IndexOf("FAN:044a:0");var dispose=calls.IndexOf("OpenLib.Dispose");var closed=calls.IndexOf("Base.Close");
            Check(controller>=0&&controller<rgb&&rgb<power&&power<fan&&fan<dispose&&dispose<closed,"Complete original Close order is controller, RGB, power, fan, OLS, Closed");
            CheckCloseSubscriptions(false);Check(!d.IsOpen&&!IDevice.LibraryInitialized,"Actual root Close retires existing EC transport");
            Check(Get(b,"HcVirtualCloseReturned")is true,"Actual Host consumes original virtual Close return");
            Check(Get(b,"OemRestoreVerified")is not true,"Close cleanup alone never creates an explicit OEM callback receipt");
        });
        foreach(var process in new[]{false,true})Run("generic-close-rejected-release-keeps-debt:"+(process?"process":"suspend"),()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);d.SeedFixtureCloseSubscriptions();Profile(b,true);Duty(b,50);var engine=CloseEngine(b);
            try
            {
                var state=Get(engine,"state")!;Set(state,"HardwareWritesObserved",true);Set(state,"OpenCalled",true);Set(state,"OpenEventsCalled",true);
                Invoke(b,process?"BlockWritesForClose":"BlockWritesForSuspend");Trace.Calls.Clear();Trace.ReleaseThrows=true;
                var result=Invoke(engine,"CloseHcSessionForLifecycle",process,true,true);
                Check(result is false,"A returned Close must not settle unproven EC release after real write history");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(snapshot,"UnknownState")is true&&Get(snapshot,"RecoveryReadbackRequired")is true,"Actual public state retains unresolved release responsibility");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("释放动作未证")==true,"Public state retains current unproven-release cause");
                Check(Get(snapshot,"HcVirtualCloseReturned")is true&&!d.IsOpen&&!IDevice.LibraryInitialized,"Completed close cleanup remains distinct from unproven release");
                Check(Get(snapshot,"OemPhysicalOwnershipConfirmed")is not true&&Get(snapshot,"OemRestoreConfirmed")is not true,"Neither physical ownership nor callback return is fabricated");
                Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW"))&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Complete original root setter swallowed a real failed write");
                CheckCloseSubscriptions(false);
            }
            finally{Trace.ReleaseThrows=false;EndEngine(engine);Close(b);}
        });
        Run("generic-close-no-write-history-keeps-honest-close",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);d.SeedFixtureCloseSubscriptions();var engine=CloseEngine(b);
            try
            {
                Trace.ReleaseThrows=true;Check(Invoke(engine,"CloseHcSessionForLifecycle",false,true,true)is true,"No prior control write needs no invented release receipt");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(snapshot,"UnknownState")is false&&Get(snapshot,"OemPhysicalOwnershipConfirmed")is not true,"Honest no-history close does not claim physical release");CheckCloseSubscriptions(false);
            }
            finally{Trace.ReleaseThrows=false;EndEngine(engine);Close(b);}
        });
        foreach(var kind in new[]{"rgb","dispose"})Run("generic-close-"+kind+"-failure-retains-owner",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);d.SeedFixtureCloseSubscriptions();Duty(b,50);var engine=CloseEngine(b);
            try
            {
                Set(Get(engine,"state")!,"HardwareWritesObserved",true);Trace.RgbReleaseThrows=kind=="rgb";Trace.DisposeThrows=kind=="dispose";var starts=Trace.DriverInitializations;
                Check(Invoke(engine,"CloseHcSessionForLifecycle",false,true,true)is false,"Incomplete original Close must remain pending");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(d.IsOpen&&IDevice.LibraryInitialized&&Get(snapshot,"UnknownState")is true,"Failed Close preserves the existing owner and unknown public state");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains(kind=="rgb"?"RGB_RELEASE_THROW":"OLS_DISPOSE_THROW")==true,"Original cleanup failure reaches public feedback");
                Trace.RgbReleaseThrows=Trace.DisposeThrows=false;Close(b);
                Check(!d.IsOpen&&!IDevice.LibraryInitialized&&Trace.DriverInitializations==starts,"Same existing owner settles cleanup without a new driver initialization");CheckCloseSubscriptions(false);
            }
            finally{Trace.RgbReleaseThrows=Trace.DisposeThrows=false;EndEngine(engine);Close(b);}
        });
        Run("generic-close-closed-event-failure-never-fabricates-release",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);d.SeedFixtureCloseSubscriptions();Duty(b,50);var engine=CloseEngine(b);
            IDevice.ClosedEventHandler fail=_=>throw new IOException("CLOSED_EVENT_THROW");d.Closed+=fail;
            try
            {
                Set(Get(engine,"state")!,"HardwareWritesObserved",true);
                Check(Invoke(engine,"CloseHcSessionForLifecycle",false,true,true)is false,"Throwing original Closed event is not a successful Host boundary");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(!d.IsOpen&&!IDevice.LibraryInitialized&&Get(snapshot,"UnknownState")is true,"Post-dispose event failure retains unknown release without reopening hardware");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("CLOSED_EVENT_THROW")==true,"Late original Closed error reaches public feedback");CheckCloseSubscriptions(false);
            }
            finally{d.Closed-=fail;EndEngine(engine);Close(b);}
        });
    }
}
