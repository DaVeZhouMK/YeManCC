using System.IO;
using System.Reflection;
using HandheldCompanion;
using HandheldCompanion.Devices;
using HandheldCompanion.Managers;
using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Text.Json;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    static int MutateInheritedEcSession(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);
        using var assembly=AssemblyDefinition.ReadAssembly(source);
        var backend=assembly.MainModule.GetType("YeManFanHost.RealHcBackend");
        var session=backend.NestedTypes.Single(t=>t.Name=="InheritedEcFanSession");
        var detached=backend.Methods.Single(m=>m.Name=="DetachCpuTemperatureLoop");
        var changedExpected=new List<string>();
        foreach(var name in new[]{"RequireCurrent","RequireFreshOpen"})
        {
            var method=session.Methods.Single(m=>m.Name==name);
            Check(method.ReturnType.FullName=="System.Void"&&method.HasBody,"Original identity guard method is required");
            changedExpected.Add(method.FullName);method.Body.Instructions.Clear();method.Body.ExceptionHandlers.Clear();method.Body.Variables.Clear();
            method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Ret));
        }
        // Restore the old ephemeral-cache mistake only at the original detach
        // entry, without changing its temperature subscriptions/timer cleanup.
        var il=detached.Body.GetILProcessor();var first=detached.Body.Instructions[0];
        foreach(var name in new[]{"inheritedEcFanSession","inheritedEcFanWriter","inheritedEcFanWriterDevice"})
        {
            var field=backend.Fields.Single(f=>f.Name==name);Check(!field.IsStatic,"Only existing instance identity fields may be cleared");
            il.InsertBefore(first,il.Create(OpCodes.Ldarg_0));il.InsertBefore(first,il.Create(OpCodes.Ldnull));il.InsertBefore(first,il.Create(OpCodes.Stfld,field));
        }
        changedExpected.Add(detached.FullName);Directory.CreateDirectory(Path.GetDirectoryName(target)!);assembly.Write(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list)=>list.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(h=>h.ToString())):"no-body");
        using var original=AssemblyDefinition.ReadAssembly(source);using var after=AssemblyDefinition.ReadAssembly(target);
        var a=Types(original.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"Session fault adds/removes no method");
        var changed=a.Keys.Where(k=>a[k]!=b[k]).Order().ToArray();Check(changed.SequenceEqual(changedExpected.Order()),"Only two identity guards and original temperature detach may change");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="disconnect two lifecycle identity guards and restore ephemeral identity clearing at original temperature detach; retain actual Open/Close/writes/results",nativeIo=false}));
        return 0;
    }
    static FieldInfo EcLibrary=>typeof(IDevice).GetField("openLibSys",All)!;
    static OpenLibSys Library=>(OpenLibSys)EcLibrary.GetValue(null)!;
    static void PrepareOriginalRestoreContext(object backend)
    {
        Events(backend);Set(backend,"hcProfileTemplate",new HandheldCompanion.Misc.PowerProfile());Invoke(backend,"CaptureBaselineBinding");
    }
    static void RunGenericOwnershipCases(string actualHc)
    {
        Run("generic-owner-complete-original-cec-open",()=>
        {
            using var actual=AssemblyDefinition.ReadAssembly(actualHc);using var fixture=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
            const string name="HandheldCompanion.Devices.AYANEO.AYANEODeviceCEc";
            var a=actual.MainModule.GetType(name).Methods.Single(m=>m.Name=="Open");var f=fixture.MainModule.GetType(name).Methods.Single(m=>m.Name=="Open");
            Check(OldBinaryProbe.Ops(a).SequenceEqual(OldBinaryProbe.Ops(f)),"Complete CEc Open opcodes match actual HC PE");
            Check(a.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).SequenceEqual(f.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName)),"Complete CEc Open calls match actual HC PE");
            Check(a.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName).SequenceEqual(f.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName)),"Complete CEc Open fields match actual HC PE");
            var originalClone=actual.MainModule.GetType("HandheldCompanion.Managers.PowerProfileManager").Methods.Single(m=>m.Name=="CloneProfile");
            var fixtureClone=fixture.MainModule.GetType("HandheldCompanion.Managers.PowerProfileManager").Methods.Single(m=>m.Name=="CloneProfile");
            Check(OldBinaryProbe.Ops(originalClone).SequenceEqual(OldBinaryProbe.Ops(fixtureClone)),"Complete original profile clone opcodes match actual HC PE");
            Check(originalClone.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).SequenceEqual(fixtureClone.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName)),"Original profile clone uses the actual shared Newtonsoft calls");
        });
        foreach(var prime in new[]{false,true})Run("generic-owner-foreign-close-refused:"+(prime?"after-duty":"before-duty"),()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;
            if(prime)Duty(b,25);
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Trace.Calls.Clear();
            try
            {
                Reject(()=>Close(b),"transport ownership changed");
                Check(ReferenceEquals(Library,foreign)&&foreign.DisposeCalls==0&&foreign.PortWrites==0,"Stale close must preserve the foreign transport byte-for-byte");
                Check(!Trace.Calls.Contains("RGB.Release")&&d.IsOpen,"Stale close must stop before any original mixed-lifecycle side effect");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
        Run("generic-owner-temperature-teardown-keeps-close-identity",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);var owned=Library;Invoke(b,"DetachCpuTemperatureLoop");
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Trace.Calls.Clear();
            try
            {
                Reject(()=>Close(b),"transport ownership changed");
                Check(foreign.DisposeCalls==0&&foreign.PortWrites==0&&ReferenceEquals(Library,foreign),"Temperature cache teardown cannot authorize a new close owner");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
        Run("generic-owner-temperature-teardown-cannot-rebind-write",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);var owned=Library;Invoke(b,"DetachCpuTemperatureLoop");
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Trace.Calls.Clear();
            try
            {
                Reject(()=>Duty(b,50),"existing owned EC transport");
                Check(foreign.PortWrites==0&&foreign.DisposeCalls==0,"Rebuilt duty delegate cannot borrow a replacement transport");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
        Run("generic-owner-replaced-device-close-refused",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);var owned=Library;var replacement=new AYANEOFlipDS();replacement.ForeignMarker();Set(b,"device",replacement);Trace.Calls.Clear();
            try
            {
                var failure=Reject(()=>Close(b),"");
                Check(d.IsOpen&&replacement.IsOpen&&owned.DisposeCalls==0&&owned.PortWrites>0&&!Trace.Calls.Contains("RGB.Release"),"Stale backend cannot close the replacement device or original transport");
                Check(failure.ToString().Contains("device ownership changed"),"Original device-ownership cause survives manager cleanup feedback");
            }
            finally{Set(b,"device",d);Close(b);}
        });
        Run("generic-owner-open-cannot-borrow-foreign-library",()=>
        {
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);var d=new AYANEOFlipDS();var b=Backend(d);var initializations=Trace.DriverInitializations;Trace.Calls.Clear();
            try
            {
                Reject(()=>Open(b),"foreign EC session");
                Check(!d.IsOpen&&ReferenceEquals(Library,foreign)&&foreign.DisposeCalls==0&&foreign.PortWrites==0&&Trace.DriverInitializations==initializations,"Rejected Open cannot borrow, mutate or dispose another owner's transport");
                Check(Get(b,"HcCloseIsOwed")is false&&!Trace.Calls.Contains("RGB.Hold"),"Rejected preflight never enters original Open or creates false Close debt");
            }
            finally{EcLibrary.SetValue(null,null);foreign.Dispose();}
        });
        Run("generic-owner-second-backend-cannot-borrow-active-session",()=>
        {
            var d=new AYANEOFlipDS();var a=Backend(d);Open(a);var owned=Library;var b=Backend(new AYANEOFlipDS());Trace.Calls.Clear();
            try
            {
                Reject(()=>Open(b),"foreign EC session");
                Check(ReferenceEquals(Library,owned)&&d.IsOpen&&owned.DisposeCalls==0,"Active first backend remains owner after second Open rejection");
                Check(Get(b,"HcCloseIsOwed")is false,"Rejected second backend has no cleanup claim on first owner");
            }
            finally{Close(a);}
        });
        Run("generic-owner-missing-close-keeps-debt",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;EcLibrary.SetValue(null,null);Trace.Calls.Clear();
            try
            {
                Reject(()=>Close(b),"transport ownership changed");
                Check(d.IsOpen&&owned.DisposeCalls==0&&!Trace.Calls.Contains("RGB.Release"),"Missing transport never becomes a false completed close");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);}
        });
        foreach(var thrown in new[]{false,true})Run("generic-owner-failed-original-open-cleans-before-retry:"+(thrown?"throw":"false"),()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Trace.FailInit=!thrown;Trace.InitThrows=thrown;
            Reject(()=>Open(b),"Open returned false");
            Check(Get(b,"HcCloseIsOwed")is true&&!d.IsOpen,"Entered original Open preserves cleanup responsibility");
            Trace.FailInit=Trace.InitThrows=false;Close(b);Check(EcLibrary.GetValue(null)is null,"Original close settles failed Open without borrowing");
            Invoke(b,"BeginOpenAttemptEvidence");Open(b);Duty(b,50);Check(d.IsOpen&&Trace.DriverInitializations==2,"Retry actually initializes a fresh owned transport");Close(b);
        });
        Run("generic-owner-close-and-reopen-refreshes-only-after-retirement",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);var before=Library;Close(b);
            Invoke(b,"BeginOpenAttemptEvidence");Open(b);Duty(b,50);var after=Library;
            Check(!ReferenceEquals(before,after)&&before.DisposeCalls==1&&Trace.DriverInitializations==2,"Verified retirement permits a new original Open and new session identity");
            Check(after.PortWrites>0&&after.DisposeCalls==0,"Reopened writes use the new original transport");Close(b);
        });
        Run("generic-owner-duplicate-open-cannot-overwrite-live-claim",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;Trace.Calls.Clear();
            try
            {
                Reject(()=>Open(b),"previous EC close is still pending");
                Check(ReferenceEquals(Library,owned)&&d.IsOpen&&owned.DisposeCalls==0&&!Trace.Calls.Contains("RGB.Hold"),"Duplicate Open cannot replace live identity or reenter mixed lifecycle");
            }
            finally{Close(b);}
        });
        Run("generic-owner-concurrent-original-opens-serialize",()=>
        {
            var a=Backend(new AYANEOFlipDS());var b=Backend(new AYANEOFlipDS());
            using var entered=new ManualResetEventSlim();using var finish=new ManualResetEventSlim();
            Trace.OnInit=()=>{entered.Set();if(!finish.Wait(5000))throw new TimeoutException("original Open fixture barrier");};
            var first=Task.Run(()=>Open(a));Check(entered.Wait(5000),"First original Open entered initializer");
            var second=Task.Run(()=>{try{Open(b);return false;}catch(Exception error){return error.ToString().Contains("foreign EC session");}});
            finish.Set();
            try
            {
                Check(first.Wait(5000)&&second.Wait(5000)&&second.Result,"Concurrent original Opens cannot publish two owners");
                Check(Trace.DriverInitializations==1&&Get(b,"HcCloseIsOwed")is false,"Second rejected preflight neither initializes nor acquires Close debt");
            }
            finally{Trace.OnInit=null;Close(a);}
        });
        Run("generic-owner-reentrant-original-open-cannot-borrow",()=>
        {
            var a=Backend(new AYANEOFlipDS());var b=Backend(new AYANEOFlipDS());bool refused=false;
            Trace.OnInit=()=>{Trace.OnInit=null;try{Open(b);}catch(Exception error){refused=error.ToString().Contains("foreign EC session");}};
            try
            {
                Open(a);Check(refused&&Trace.DriverInitializations==1&&Get(b,"HcCloseIsOwed")is false,"Reentrant callback cannot borrow the first initializer's published reference");Duty(a,50);
            }
            finally{Trace.OnInit=null;Close(a);}
        });
        Run("generic-owner-close-cannot-use-drifted-ec-address",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);var owned=Library;var control=d.ECDetails.AddressFanControl;d.ECDetails.AddressFanControl=0x555;Trace.Calls.Clear();
            try
            {
                Reject(()=>Close(b),"EC fan data changed");
                Check(d.IsOpen&&owned.DisposeCalls==0&&!Trace.Calls.Contains("RGB.Release")&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Drifted metadata cannot dispatch a different release register");
            }
            finally{d.ECDetails.AddressFanControl=control;Close(b);}
        });
        Run("generic-owner-restore-rejects-foreign-after-monitor-detach",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);PrepareOriginalRestoreContext(b);Duty(b,25);var owned=Library;
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Trace.Calls.Clear();
            try
            {
                Reject(()=>Invoke(b,"RestoreOemCore"),"existing owned EC transport");
                Check(foreign.PortWrites==0&&foreign.DisposeCalls==0&&ReferenceEquals(Library,foreign),"Actual manual restore cannot rebind after stopping the temperature loop");
                Check(Get(b,"OemRestoreVerified")is not true,"Rejected restore produces no successful OEM handoff");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
        Run("generic-owner-healthy-restore-retains-same-transport",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);PrepareOriginalRestoreContext(b);Duty(b,25);var owned=Library;Trace.Calls.Clear();
            try
            {
                Invoke(b,"RestoreOemCore");
                Check(Hash(LoadLockedNewtonsoft().Location)=="a28c251dfe36d881e9e2462e171441b8b0ec156fe3f452602c9149b1b9efe05b","Actual original restore clone loaded the exact locked Newtonsoft binary");
                Check(ReferenceEquals(Library,owned)&&owned.DisposeCalls==0&&Trace.Calls.Contains("FAN:044a:0"),"Actual healthy restore uses original transport after monitor detach");
                Check(Get(b,"OemPhysicalOwnershipConfirmed")is not true,"Callback transport success never becomes physical ownership");
            }
            finally{Close(b);}
        });
        Run("generic-owner-late-closed-reference-change-is-unsettled",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;var foreign=new OpenLibSys();foreign.InitializeOls();
            IDevice.ClosedEventHandler replace=_=>EcLibrary.SetValue(null,foreign);d.Closed+=replace;
            try
            {
                Reject(()=>Close(b),"");
                Check(owned.DisposeCalls==1&&foreign.DisposeCalls==0&&foreign.PortWrites==0&&ReferenceEquals(Library,foreign),"Post-Close callback cannot convert replacement resources into completed owned cleanup");
            }
            finally{d.Closed-=replace;EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
        Run("generic-owner-new-vendor-open-cannot-abandon-ec-debt",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;var next=new VendorEcOverride();Set(b,"device",next);Trace.Calls.Clear();
            try
            {
                Reject(()=>Open(b),"");
                Check(d.IsOpen&&!next.IsOpen&&ReferenceEquals(Library,owned)&&owned.DisposeCalls==0,"A route change cannot abandon the inherited session's unresolved identity");
            }
            finally{Set(b,"device",d);Close(b);}
        });
        Run("generic-owner-verified-retirement-permits-vendor-open",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var retired=Library;Close(b);var next=new VendorEcOverride();Set(b,"device",next);Invoke(b,"BeginOpenAttemptEvidence");Open(b);var owned=Library;
            try
            {
                Duty(b,50);Profile(b,true);
                Check(next.IsOpen&&owned.DisposeCalls==0&&Trace.Calls.Contains("VendorDuty:50")&&Trace.Calls.Contains("VendorControl:True"),"Verified retirement yields to the existing original vendor dispatch");
                Check(retired.DisposeCalls==1&&!ReferenceEquals(retired,owned),"New vendor lifecycle does not reuse or redispose retired resources");
            }
            finally{Close(b);}
        });
        Run("generic-owner-close-engine-feedback-retains-foreign-owner",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Events(b);Duty(b,25);var owned=Library;var engine=CloseEngine(b);
            var foreign=new OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Set(Get(engine,"state")!,"HardwareWritesObserved",true);Trace.Calls.Clear();
            try
            {
                Check(Invoke(engine,"CloseHcSessionForLifecycle",false,true,true)is false,"Foreign transport cannot make actual engine report completed Close");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(ReferenceEquals(Library,foreign)&&foreign.DisposeCalls==0&&foreign.PortWrites==0,"Actual engine failure cleanup preserves foreign resources");
                Check(Get(snapshot,"UnknownState")is true&&Get(snapshot,"LastError")?.ToString()?.Contains("transport ownership changed")==true,"Actual API snapshot reports the ownership failure");
            }
            finally{EcLibrary.SetValue(null,owned);EndEngine(engine);Close(b);foreign.Dispose();}
        });
    }
}
