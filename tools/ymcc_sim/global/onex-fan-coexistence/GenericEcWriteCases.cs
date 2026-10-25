using System.IO;
using HandheldCompanion.Devices;
using HandheldCompanion.Misc;
using Mono.Cecil;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{

    static int MutateInheritedEcBool(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);
        using var assembly=AssemblyDefinition.ReadAssembly(source);
        var backend=assembly.MainModule.GetType("YeManFanHost.RealHcBackend");
        var type=backend.NestedTypes.Single(t=>t.Name=="InheritedEcFanWriter");
        var method=type.Methods.Single(m=>m.Name=="Write");
        var invoke=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.Name=="Invoke"&&m.Parameters.Count==2&&m.DeclaringType is GenericInstanceType g&&g.GenericArguments.Select(x=>x.FullName).SequenceEqual(new[]{"System.UInt16","System.Byte","System.Boolean"}));
        var branch=invoke.Next;
        Check(branch.OpCode==Mono.Cecil.Cil.OpCodes.Brtrue||branch.OpCode==Mono.Cecil.Cil.OpCodes.Brtrue_S,"Exact bool-result branch required");
        var success=(Mono.Cecil.Cil.Instruction)branch.Operand;
        var scan=branch.Next;bool failureCode=false;
        while(scan!=success){Check(scan is not null,"Expected success branch target");if(scan!.OpCode==Mono.Cecil.Cil.OpCodes.Ldstr&&Equals(scan.Operand,"FAN_CURVE_WRITE_FAILED"))failureCode=true;scan=scan.Next;}
        Check(failureCode,"Only the EC false-result refusal may be bypassed");
        var il=method.Body.GetILProcessor();il.InsertBefore(branch,il.Create(Mono.Cecil.Cil.OpCodes.Pop));
        branch.OpCode=Mono.Cecil.Cil.OpCodes.Br;branch.Operand=success;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);assembly.Write(target);
        using var after=AssemblyDefinition.ReadAssembly(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> types)=>types.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(e=>e.ToString())):"no-body");
        // Compare the output to a fresh original read, because the in-memory
        // assembly above contains the intentional mutation already.
        using var original=AssemblyDefinition.ReadAssembly(source);
        var a=Types(original.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"No method added/removed by fault");
        var changed=a.Keys.Where(k=>a[k]!=b[k]).ToArray();Check(changed.Length==1&&changed[0]==method.FullName,"Only the bool-consumption method may change");
        File.WriteAllText(args[2],System.Text.Json.JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="execute original EC call, discard bool, branch to original post-write owner check",nativeIo=false}));
        return 0;
    }

    static void RunGenericEcWriteCases(string actualHc)
    {
        Run("generic-ec-frozen-write-contract", () =>
        {
            using var assembly=AssemblyDefinition.ReadAssembly(actualHc);
            var root=assembly.MainModule.GetType("HandheldCompanion.Devices.IDevice");
            using var fixture=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
            var fixtureRoot=fixture.MainModule.GetType(root.FullName);
            foreach(var name in new[]{"SetFanControl","SetFanDuty"})
            {
                var m=root.Methods.Single(x=>x.Name==name);
                var call=m.Body.Instructions.Single(x=>x.Operand is MethodReference r&&r.Name=="ECRamDirectWriteByte");
                Check(m.ReturnType.FullName=="System.Void"&&call.Next.OpCode.Name=="pop", "Exact HC " + name + " discards the low-level bool");
                var compiledSource=fixtureRoot.Methods.Single(x=>x.Name==name);
                Check(OldBinaryProbe.Ops(m).SequenceEqual(OldBinaryProbe.Ops(compiledSource)),"Complete inherited fan opcodes match the actual HC PE: "+name);
                Check(m.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName).SequenceEqual(compiledSource.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName)),"All inherited fan calls match the actual HC PE: "+name);
                Check(m.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName).SequenceEqual(compiledSource.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName)),"All inherited EC field references match the actual HC PE: "+name);
            }
            var write=root.Methods.Single(x=>x.Name=="ECRamDirectWriteByte");
            Check(write.ReturnType.FullName=="System.Boolean"&&write.Parameters.Count==3,"Actual EC bool boundary is present");
        });
        foreach(var mode in new[]{"duty","primed-duty","software-profile","hardware-profile"})
            Run("generic-ec-rejected-"+mode,()=>
            {
                var d=new AYANEOFlipDS();var b=Backend(d);Open(b);
                var logs=new List<string>();b.GetType().GetProperty("Diagnostic")!.SetValue(b,new Action<string,object?>((name,_)=>logs.Add(name)));
                if(mode=="primed-duty")Duty(b,25);
                Trace.Calls.Clear();logs.Clear();Trace.ReleaseThrows=true;
                Reject(()=>{if(mode.EndsWith("duty"))Duty(b,50);else Profile(b,mode=="software-profile");},"EC fan write was rejected");
                Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW"))&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Real low-level false is not a successful fan packet");
                Check(!logs.Contains("hc.fan-dispatch.callback-returned"),"Failed control has no success receipt");
                Check(d.IsOpen&&!Trace.Calls.Contains("OpenLib.Dispose"),"Failed write retains existing transport for ordered return");
                Trace.ReleaseThrows=false;
                if(mode.EndsWith("duty")){Duty(b,50);Check(Trace.Calls.Contains("FAN:044b:50"),"Explicit retry preserves model EC scale");}
                else {Profile(b,mode=="software-profile");Check(logs.Contains("hc.fan-dispatch.callback-returned"),"Successful retry reaches the original result boundary");}
                Close(b);
            });
        Run("generic-ec-rejected-transport-exit",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Trace.ExitThrows=true;
            Reject(()=>Duty(b,50),"IO_EXIT_THROW");Trace.ExitThrows=false;Close(b);
        });
        Run("generic-ec-original-rounding-and-curve-scale",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Trace.Calls.Clear();
            foreach(var value in new[]{0d,1d,25d,50d,50.5d,51.5d,99d,100d})Duty(b,value);
            var actual=Trace.Calls.Where(x=>x.StartsWith("FAN:")).ToArray();
            Check(actual.SequenceEqual(new[]{"FAN:044b:0","FAN:044b:1","FAN:044b:25","FAN:044b:50","FAN:044b:50","FAN:044b:52","FAN:044b:99","FAN:044b:100"}),"Inherited HC Convert.ToByte rounding is exact");Close(b);
        });

        Run("generic-ec-original-nonzero-minimum-and-255-scale",()=>
        {
            var d=new AYANEOFlipDS();d.ECDetails.FanValueMin=10;d.ECDetails.FanValueMax=250;
            var b=Backend(d);Open(b);Trace.Calls.Clear();Duty(b,0);Duty(b,50);Duty(b,100);
            Check(Trace.Calls.Where(x=>x.StartsWith("FAN:")).SequenceEqual(new[]{"FAN:044b:10","FAN:044b:130","FAN:044b:250"}),"Source EC min/max data governs scaling, not OneX's fixed 184");Close(b);
        });
        foreach(var point in new[]{"before","during"})Run("generic-ec-superseded-"+point,()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Trace.Calls.Clear();
            if(point=="before")Set(b,"suspendGateEpoch",1L);
            else Trace.OnPortWrite=(_,_)=>Set(b,"suspendGateEpoch",1L);
            Reject(()=>Duty(b,50),"superseded");
            Check(Trace.Calls.Any(x=>x.StartsWith("FAN:"))==(point=="during"),"Supersession distinguishes unsent request from write already attempted");
            Trace.OnPortWrite=null;Close(b);
        });
        Run("generic-ec-metadata-and-owned-readiness-cannot-drift",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);Trace.Calls.Clear();
            d.ECDetails.AddressFanDuty=0x555;Reject(()=>Duty(b,50),"EC fan data changed");d.ECDetails.AddressFanDuty=0x44b;
            d.DisableOpenLib();Reject(()=>Duty(b,50),"existing owned EC transport");d.EnableOpenLib();
            d.LoseMarker();Reject(()=>Duty(b,50),"existing owned EC transport");d.ForeignMarker();
            foreach(var bad in new[]{double.NaN,double.PositiveInfinity,-1,101})Reject(()=>Duty(b,bad),"between 0 and 100");
            Check(!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Drift and invalid duty never reach the EC terminal");Close(b);
        });
        Run("generic-ec-cache-and-owner-reference-do-not-authorize-retired-write",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);
            Set(b,"fanDutyWriterDevice",d);Set(b,"fanDutyWriter",new Action<double>(_=>throw new InvalidOperationException("LEGACY_CACHE_USED")));
            Duty(b,50);Check(Trace.Calls.Contains("FAN:044b:50"),"Strict result handling precedes the legacy delegate cache");
            Trace.OnPortWrite=(_,_)=>Set(b,"device",new AYANEOFlipDS());
            Reject(()=>Duty(b,60),"superseded");Trace.OnPortWrite=null;Set(b,"device",d);Close(b);
        });
        Run("generic-ec-vendor-overrides-retain-their-consumers",()=>
        {
            var d=new VendorEcOverride();var b=Backend(d);Open(b);Trace.Calls.Clear();Duty(b,50);Profile(b,true);
            Check(Trace.Calls.Contains("VendorDuty:50")&&Trace.Calls.Contains("VendorControl:True")&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Inherited-only binding does not replace a vendor's protocol");Close(b);
        });
        Run("generic-ec-lost-or-replaced-transport-never-reopens-driver",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);Duty(b,25);
            var field=typeof(IDevice).GetField("openLibSys",All)!;var owned=field.GetValue(null)!;var initializations=Trace.DriverInitializations;
            IDevice.ForeignTransport();var foreign=field.GetValue(null)!;Trace.Calls.Clear();
            Reject(()=>Duty(b,50),"existing owned EC transport");
            Check(!Trace.Calls.Any(x=>x.StartsWith("FAN:"))&&Trace.DriverInitializations==initializations,"Changed global transport is not borrowed or reinitialized by a stale device marker");
            field.SetValue(null,owned);((IDisposable)foreign).Dispose();
            field.SetValue(null,null);Trace.Calls.Clear();
            Reject(()=>Duty(b,50),"existing owned EC transport");
            Check(Trace.DriverInitializations==initializations&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Lost transport cannot become a nullable no-op success or a fresh driver initialization");
            field.SetValue(null,owned);Close(b);
        });
        Run("generic-ec-temperature-failure-reaches-existing-engine-recovery",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var monitor=AttachTemperature(b);Exception? failure=null;
            using var delivered=new ManualResetEventSlim();
            b.GetType().GetProperty("FanDispatchFailure")!.SetValue(b,new Action<Exception>(e=>{failure=e;delivered.Set();}));
            Trace.ReleaseThrows=true;object? engine=null;
            try
            {
                Temperature(b,monitor,65);Check(delivered.Wait(5000),"Current temperature producer must report rejected inherited EC write");
                Check(Get(b,"cpuTemperatureMonitor")is null,"Failed temperature source is detached before recovery");
                engine=Engine(b);Invoke(engine,"OnBackendFanDispatchFailure",failure!);
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(snapshot,"State")is string state&&state=="FaultLocked"&&Get(snapshot,"UnknownState")is true,"Actual /api/state snapshot reports failure instead of successful control");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("EC fan write was rejected")==true,"Public state feedback retains the actual rejected-write cause");
                Check(Get(engine,"RecoveryRetryTimerActiveForSelfTest")is true,"Existing bounded recovery is used");
            }
            finally{if(engine is not null)EndEngine(engine);Trace.ReleaseThrows=false;Close(b);}
        });
    }
}
