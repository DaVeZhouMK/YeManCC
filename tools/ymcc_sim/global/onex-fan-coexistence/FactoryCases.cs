using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using HandheldCompanion.Devices;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Ports=ConsumerFixture.FactoryPorts;

static partial class Harness
{
    static object NewFactoryBackend(bool diagnostic=false,bool authorized=true,string? assemblyPath=null)
    {
        var type=Candidate.GetType("YeManFanHost.HostOptions",true)!;
        var options=RuntimeHelpers.GetUninitializedObject(type);var token=Guid.NewGuid().ToString("N");
        Set(options,"AllowHardwareWrites",authorized);Set(options,"SessionToken",token);Set(options,"Confirmation",token);Set(options,"AuthorizationPath",AuthFile);
        Set(options,"HcAssemblyPath",assemblyPath??typeof(IDevice).Assembly.Location);Set(options,"HcRuntimeRoot",Path.GetDirectoryName(typeof(IDevice).Assembly.Location));
        if(diagnostic)Set(options,"HcDeviceType",typeof(ROGAlly).FullName);
        var backend=Activator.CreateInstance(Candidate.GetType("YeManFanHost.RealHcBackend",true)!,All,null,[options],null)!;
        Ports.BootIdentity=Get(backend,"machineBootIdentity")?.ToString()??"fixture-boot";
        return backend;
    }
    static Exception FactoryFailure(Action action)
    {
        Exception? found=null;try{action();}catch(Exception error){found=error;}
        Check(found is not null,"Expected the actual factory stage to reject its injected failure");
        var origin=found;while(origin is not null&&!ReferenceEquals(origin,Ports.LastFailure))origin=origin.InnerException;
        Check(origin is not null,"Factory stage preserves the original terminal failure object");return found!;
    }
    static void EndFactory(object backend)
    {
        ((IDisposable)backend).Dispose();Ports.Enabled=false;
    }
    static void RunFactoryConsumerCases()
    {
        foreach(var diagnostic in new[]{false,true})Run("factory-healthy-actual-orchestration:"+diagnostic,()=>
        {
            Ports.Reset();var b=NewFactoryBackend(diagnostic);try
            {
                var handshake=Invoke(b,"LoadAndHandshakeCore")!;
                Check(Get(b,"IsLoaded")is true&&Get(b,"hcCoreInitialized")is true&&Get(b,"CanWrite")is true,"Completed factory is ready only after all actual stages return");
                Check(Get(handshake,"Supported")is true&&Get(handshake,"DeviceIdentity")is not null,"Actual handshake publishes the completed identity");
                Check(Ports.Count("assembly.native")==1&&Ports.Count("assembly.configure")==1&&Ports.Count("motherboard.collect")==1&&Ports.Count("device.construct")==1&&Ports.Count("device.pull-sensors")==0&&Ports.Count("device.initialize")==1,"Actual factory orchestration executes each completed stage once");
                var calls=Ports.Calls.Count;var d=Get(b,"device");Invoke(b,"LoadAndHandshakeCore");
                Check(Ports.Calls.Count==calls&&ReferenceEquals(Get(b,"device"),d),"Healthy cached handshake cannot replay factory stages or replace its device");
            }finally{EndFactory(b);}
        });
        foreach(var diagnostic in new[]{false,true})Run("factory-sensor-discovery-not-called:"+diagnostic,()=>
        {
            Ports.Reset("device.pull-sensors",1);var b=NewFactoryBackend(diagnostic);try
            {
                var handshake=Invoke(b,"LoadAndHandshakeCore")!;
                Check(Get(b,"IsLoaded")is true&&Get(b,"hcCoreInitialized")is true&&Get(handshake,"Supported")is true,"Fan-only initialization must succeed when motion discovery is configured to throw");
                Check(Ports.Count("device.pull-sensors")==0&&Ports.Failures==1&&Ports.LastFailure is null,"Fan initialization must never enter or consume the configured failing sensor terminal");
                Check(Ports.Count("device.initialize")==1&&Ports.Count("device.identity")>0&&Get(b,"hcDeviceInitializeReturned")is true,"Skipping motion discovery must preserve real Initialize and identity execution");
                Check(Get(b,"HcCoreWarning")is null&&Get(handshake,"DeviceIdentity")is not null,"Uncalled sensor failure cannot replace real identity or fabricate an unsupported-sensor warning");
                var calls=Ports.Calls.Count;Invoke(b,"LoadAndHandshakeCore");
                Check(Ports.Calls.Count==calls&&Ports.Count("device.pull-sensors")==0,"A cached fan handshake cannot discover sensors later");
            }finally{EndFactory(b);}
        });
        foreach(var stage in new[]{"device.initialize","device.identity"})
        {
            Run("factory-partial-cannot-publish-ready:"+stage,()=>
            {
                Ports.Reset(stage,1);var b=NewFactoryBackend();try
                {
                    FactoryFailure(()=>Invoke(b,"LoadAndHandshakeCore"));
                    Check(Get(b,"IsLoaded")is false&&Get(b,"CanWrite")is false,"Partial factory must not publish IsLoaded or write readiness: "+stage);
                    Check(Get(b,"device")is not null,"Partial identity remains owned for the same factory retry");
                }finally{EndFactory(b);}
            });
            Run("factory-retry-continues-same-device:"+stage,()=>
            {
                Ports.Reset(stage,1);var b=NewFactoryBackend();try
                {
                    FactoryFailure(()=>Invoke(b,"LoadAndHandshakeCore"));var device=Get(b,"device");
                    Invoke(b,"LoadAndHandshakeCore");
                    Check(Get(b,"IsLoaded")is true&&Get(b,"hcCoreInitialized")is true&&Get(b,"DeviceIdentity")is not null,"Failed factory must resume to complete real readiness and identity: "+stage);
                    Check(ReferenceEquals(Get(b,"device"),device)&&Ports.Count("device.construct")==1&&Ports.Count("device.get-current")==1,"Retry cannot replace an already constructed factory device");
                    Check(Ports.Count("assembly.native")==1&&Ports.Count("assembly.configure")==1&&Ports.Count("motherboard.collect")==1,"Retry cannot replay completed bootstrap stages");
                    Check(Ports.Count("device.pull-sensors")==0&&Ports.Count("device.initialize")== (stage=="device.initialize"?2:1),"Retry repeats only its failed device stage and never enters motion discovery");
                }finally{EndFactory(b);}
            });
        }
        foreach(var stage in new[]{"assembly.configure","motherboard.collect","device.construct"})Run("factory-bootstrap-retry-does-not-replay-completed-stages:"+stage,()=>
        {
            Ports.Reset(stage,1);var b=NewFactoryBackend(true);try
            {
                FactoryFailure(()=>Invoke(b,"LoadAndHandshakeCore"));Invoke(b,"LoadAndHandshakeCore");
                Check(Get(b,"IsLoaded")is true&&Ports.Count("assembly.native")==1,"Bootstrap retry must reuse its loaded assembly and resolver");
                Check(Ports.Count("assembly.configure")== (stage=="assembly.configure"?2:1)&&Ports.Count("motherboard.collect")== (stage=="motherboard.collect"?2:1),"Bootstrap retry must preserve completed configuration and motherboard stages");
                Check(Ports.Count("device.construct")== (stage=="device.construct"?2:1),"Only an unreturned constructor may run again");
            }finally{EndFactory(b);}
        });
        Run("factory-write-admission-completes-pending-initialization",()=>
        {
            Ports.Reset("device.initialize",1);var b=NewFactoryBackend();try
            {
                FactoryFailure(()=>Invoke(b,"LoadAndHandshakeCore"));Invoke(b,"EnsureRealWriteAuthorization");
                Check(Get(b,"IsLoaded")is true&&Get(b,"hcCoreInitialized")is true&&Ports.Count("device.initialize")==2&&Ports.Count("assembly.configure")==1&&Ports.Count("device.pull-sensors")==0,"Actual write admission must finish pending initialization before accepting the same device");
            }finally{EndFactory(b);}
        });
        Run("factory-readonly-sampling-never-uses-partial-core",()=>
        {
            Ports.Reset("device.initialize",2);var b=NewFactoryBackend();try
            {
                FactoryFailure(()=>Invoke(b,"LoadAndHandshakeCore"));
                var pending=Invoke(b,"CaptureMsiReadOnlySnapshotCore")!;
                Check((int)pending.GetType().GetField("Item2")!.GetValue(pending)!=0&&Ports.Count("sample.read-only")==0,"Read-only evidence must not sample or report success from a partial factory");
                Ports.Failures=0;var ready=Invoke(b,"CaptureMsiReadOnlySnapshotCore")!;
                Check((int)ready.GetType().GetField("Item2")!.GetValue(ready)==0&&Ports.Count("sample.read-only")==1&&Ports.Count("assembly.configure")==1&&Ports.Count("device.construct")==1,"Read-only evidence recovers only after the same actual factory completes");
            }finally{EndFactory(b);}
        });
        Run("factory-authorization-refusal-precedes-new-work",()=>
        {
            Ports.Reset();var b=NewFactoryBackend(authorized:false);try
            {
                var refused=Reject(()=>Invoke(b,"EnsureRealWriteAuthorization"),"真实风扇写入需要独立授权");
                Check(Get(refused,"Code")?.ToString()=="REAL_WRITE_AUTH_REQUIRED"&&Ports.Calls.Count==0,"Actual authorization refusal cannot start an initialization retry");
            }finally{EndFactory(b);}
        });
        Run("factory-disposed-cannot-republish-or-reload",()=>
        {
            Ports.Reset();var b=NewFactoryBackend();try
            {
                Invoke(b,"LoadAndHandshakeCore");((IDisposable)b).Dispose();
                Check(Get(b,"IsLoaded")is false&&Get(b,"CanWrite")is false,"Disposed factory cannot retain ready or write authority");
                var count=Ports.Calls.Count;Reject(()=>Invoke(b,"LoadAndHandshakeCore"),"ObjectDisposedException");
                Check(Ports.Calls.Count==count,"Disposed factory cannot restart any initialization stage");
            }finally{EndFactory(b);}
        });
        Run("factory-dispose-during-stage-cannot-publish",()=>
        {
            Ports.Reset();var b=NewFactoryBackend();Ports.OnStage=stage=>{if(stage=="device.initialize"){Ports.OnStage=null;((IDisposable)b).Dispose();}};
            try
            {
                Reject(()=>Invoke(b,"LoadAndHandshakeCore"),"ObjectDisposedException");
                Check(Get(b,"IsLoaded")is false&&Get(b,"CanWrite")is false,"Disposed in-flight factory cannot publish a late ready result");
            }finally{EndFactory(b);}
        });
        Run("factory-reentrant-handshake-cannot-publish-partial-core",()=>
        {
            Ports.Reset();var b=NewFactoryBackend();Exception? nested=null;Ports.OnStage=stage=>{if(stage=="device.initialize"){Ports.OnStage=null;try{Invoke(b,"LoadAndHandshakeCore");}catch(Exception error){nested=error;}}};
            try
            {
                Invoke(b,"LoadAndHandshakeCore");
                Check(nested is not null&&Get(nested,"Code")?.ToString()=="HC_NOT_INITIALIZED","Reentrant handshake cannot observe an unfinished initialization as ready");
                Check(Get(b,"IsLoaded")is true&&Ports.Count("device.initialize")==1&&Ports.Count("device.construct")==1,"Original outer factory completes once after rejecting reentry");
            }finally{EndFactory(b);}
        });
        foreach(var kind in new[]{"device","assembly"})Run("factory-owner-change-during-stage-is-refused:"+kind,()=>
        {
            Ports.Reset();var b=NewFactoryBackend();object? original=null;
            Ports.OnStage=stage=>{if(stage=="device.initialize"){Ports.OnStage=null;original=Get(b,kind);Set(b,kind,kind=="device"?new ROGAlly():typeof(Harness).Assembly);}};
            try
            {
                Exception? changed=null;try{Invoke(b,"LoadAndHandshakeCore");}catch(Exception error){changed=error;}
                Check(changed is not null&&Get(changed,"Code")?.ToString()=="HC_NOT_INITIALIZED"&&Get(b,"IsLoaded")is false,"Changed factory identity cannot be adopted by the old stage: "+kind);
                Check(original is not null&&ReferenceEquals(Get(b,kind=="device"?"hcInitializationDevice":"hcInitializationAssembly"),original)&&Ports.Count("device.initialize")==1&&Ports.Count("device.identity")==0&&Get(b,"hcDeviceInitializeReturned")is false,"Original initialization owner remains recorded without accepting replacement Initialize or running identity");
            }finally{EndFactory(b);}
        });
        foreach(var kind in new[]{"device","assembly"})Run("factory-completed-owner-change-invalidates-ready:"+kind,()=>
        {
            Ports.Reset();var b=NewFactoryBackend();try
            {
                Invoke(b,"LoadAndHandshakeCore");Set(b,kind,kind=="device"?new ROGAlly():typeof(Harness).Assembly);
                Check(Get(b,"IsLoaded")is false&&Get(b,"CanWrite")is false,"Replaced factory object cannot inherit a completed initialization");
                var count=Ports.Calls.Count;Exception? changed=null;try{Invoke(b,"LoadAndHandshakeCore");}catch(Exception error){changed=error;}
                Check(changed is not null&&Get(changed,"Code")?.ToString()=="HC_NOT_INITIALIZED"&&Ports.Calls.Count==count,"Owner replacement must be refused before a new bootstrap or constructor");
            }finally{EndFactory(b);}
        });
        Run("factory-readonly-sample-error-keeps-completed-factory",()=>
        {
            Ports.Reset();var b=NewFactoryBackend();try
            {
                Invoke(b,"LoadAndHandshakeCore");Ports.Failure="sample.read-only";Ports.Failures=1;
                var bad=Invoke(b,"CaptureMsiReadOnlySnapshotCore")!;Check((int)bad.GetType().GetField("Item2")!.GetValue(bad)!=0&&Get(b,"IsLoaded")is true,"A read-only sampling error stays distinct from completed factory readiness");
                var good=Invoke(b,"CaptureMsiReadOnlySnapshotCore")!;Check((int)good.GetType().GetField("Item2")!.GetValue(good)==0&&Ports.Count("device.construct")==1&&Ports.Count("device.initialize")==1,"Sampling recovery cannot reconstruct or reinitialize its completed factory");
            }finally{EndFactory(b);}
        });
        Run("factory-disposal-preserves-another-resolver-owner",()=>
        {
            Ports.Reset();var a=NewFactoryBackend();var b=NewFactoryBackend();try
            {
                Invoke(a,"LoadAndHandshakeCore");Invoke(b,"LoadAndHandshakeCore");Ports.ResolverProbe="YMCCFactoryMissing"+Guid.NewGuid().ToString("N");
                try{ToolLoadAuthority.Current.LoadExpectedAbsentProbe(AssemblyLoadContext.Default,new AssemblyName(Ports.ResolverProbe));}catch(FileNotFoundException){}
                Check(Ports.ResolverCalls.GetValueOrDefault(a)==1&&Ports.ResolverCalls.GetValueOrDefault(b)==1,"Each actual factory owns exactly one distinct resolver");
                ((IDisposable)a).Dispose();Ports.ResolverProbe="YMCCFactoryMissing"+Guid.NewGuid().ToString("N");
                try{ToolLoadAuthority.Current.LoadExpectedAbsentProbe(AssemblyLoadContext.Default,new AssemblyName(Ports.ResolverProbe));}catch(FileNotFoundException){}
                Check(Ports.ResolverCalls.GetValueOrDefault(a)==1&&Ports.ResolverCalls.GetValueOrDefault(b)==2,"Disposing one factory cannot remove another owner's resolver or keep its own");
            }finally{EndFactory(a);EndFactory(b);}
        });
        Run("factory-resolver-one-registration-and-owned-disposal",()=>
        {
            Ports.Reset();var file=Path.Combine(Path.GetDirectoryName(AuthFile)!,"invalid-hc-image.dll");File.WriteAllText(file,"not-an-assembly");var b=NewFactoryBackend(assemblyPath:file);
            try
            {
                Reject(()=>Invoke(b,"LoadAndHandshakeCore"),"BadImageFormatException");Reject(()=>Invoke(b,"LoadAndHandshakeCore"),"BadImageFormatException");
                Ports.ResolverProbe="YMCCFactoryMissing"+Guid.NewGuid().ToString("N");
                try{ToolLoadAuthority.Current.LoadExpectedAbsentProbe(AssemblyLoadContext.Default,new AssemblyName(Ports.ResolverProbe));}catch(FileNotFoundException){}
                Check(Ports.ResolverCalls.GetValueOrDefault(b)==1,"Failed assembly retries must register exactly one owned resolver");
                ((IDisposable)b).Dispose();var count=Ports.ResolverCalls.GetValueOrDefault(b);
                Ports.ResolverProbe="YMCCFactoryMissing"+Guid.NewGuid().ToString("N");
                try{ToolLoadAuthority.Current.LoadExpectedAbsentProbe(AssemblyLoadContext.Default,new AssemblyName(Ports.ResolverProbe));}catch(FileNotFoundException){}
                Check(Ports.ResolverCalls.GetValueOrDefault(b)==count,"Disposed factory must remove only its owned resolver");
            }finally{EndFactory(b);}
        });
    }
    static int RunFactoryCases(string[] args)
    {
        RunFactoryConsumerCases();var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,scope="Actual compiled factory orchestration; five explicit deployment/OS/lookup leaves and fixture device/sensor terminals, not full production validation"},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }
    static string FactoryMethodCanon(MethodDefinition method)
    {
        if(!method.HasBody)return method.FullName+"|"+method.Attributes+"|"+method.ImplAttributes+"|no-body";
        var body=method.Body;var instructions=body.Instructions;
        int Index(Instruction? instruction)=>instruction is null?-1:instructions.IndexOf(instruction);
        string Operand(object? operand)=>operand switch
        {
            null=>"",Instruction instruction=>"target:"+Index(instruction),
            Instruction[] targets=>"targets:"+string.Join(",",targets.Select(Index)),
            VariableDefinition variable=>"local:"+variable.Index+":"+variable.VariableType.FullName,
            ParameterDefinition parameter=>"arg:"+parameter.Index+":"+parameter.ParameterType.FullName,
            MemberReference member=>member.FullName,
            _=>Convert.ToString(operand,System.Globalization.CultureInfo.InvariantCulture)??""
        };
        return JsonSerializer.Serialize(new{method.FullName,attributes=(int)method.Attributes,implementation=(int)method.ImplAttributes,
            body.InitLocals,locals=body.Variables.Select(v=>v.VariableType.FullName).ToArray(),
            instructions=instructions.Select(i=>new{opcode=i.OpCode.OperandType==OperandType.ShortInlineBrTarget?i.OpCode.Code.ToString()[..^2]:i.OpCode.Code.ToString(),operand=Operand(i.Operand)}).ToArray(),
            handlers=body.ExceptionHandlers.Select(h=>new{kind=h.HandlerType.ToString(),type=h.CatchType?.FullName,
                start=Index(h.TryStart),end=Index(h.TryEnd),handler=Index(h.HandlerStart),handlerEnd=Index(h.HandlerEnd),filter=Index(h.FilterStart)}).ToArray()});
    }
    static string[] FactoryChangedMethods(string source,string target)
    {
        using var original=AssemblyDefinition.ReadAssembly(source);using var modified=AssemblyDefinition.ReadAssembly(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list)=>list.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        var before=Types(original.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,FactoryMethodCanon);
        var after=Types(modified.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,FactoryMethodCanon);
        Check(before.Keys.Order().SequenceEqual(after.Keys.Order()),"Factory transformations cannot add or remove methods");
        return before.Keys.Where(k=>before[k]!=after[k]).Order().ToArray();
    }
    static int MutateFactoryGuard(string[] args,bool readiness)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);
        var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");
        var method=backend.Methods.Single(m=>m.Name==(readiness?"get_IsLoaded":"LoadAssemblyAndFactory"));var il=method.Body.GetILProcessor();
        if(readiness)
        {
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();
            foreach(var name in new[]{"assembly","device"})
            {
                il.Append(il.Create(OpCodes.Ldarg_0));il.Append(il.Create(OpCodes.Ldfld,backend.Fields.Single(f=>f.Name==name)));
                il.Append(il.Create(OpCodes.Ldnull));il.Append(il.Create(OpCodes.Ceq));il.Append(il.Create(OpCodes.Ldc_I4_0));il.Append(il.Create(OpCodes.Ceq));
            }
            il.Append(il.Create(OpCodes.And));il.Append(il.Create(OpCodes.Ret));
        }
        else foreach(var name in new[]{"hcRuntimeConfigured","hcMotherboardCollected","hcDeviceInitializeReturned"})
        {
            var guards=method.Body.Instructions.Where(i=>i.OpCode==OpCodes.Ldfld&&i.Operand is FieldReference field&&field.Name==name).ToArray();
            Check(guards.Length==1&&guards[0].Next.OpCode.FlowControl==FlowControl.Cond_Branch,"Exact completed-stage guard: "+name);
            var guard=guards[0];guard.OpCode=OpCodes.Pop;guard.Operand=null;il.InsertAfter(guard,il.Create(OpCodes.Ldc_I4_0));
        }
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);var changed=FactoryChangedMethods(source,target);
        Check(changed.Length==1&&changed[0]==method.FullName,"Factory fault changes exactly one named original method; locals and exception handlers are compared");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,
            mutation=readiness?"Restore only the former assembly/device-only readiness predicate":"Ignore only the three remaining completed-stage guards; retain all stage calls, readiness, owner, reentry and disposal gates",
            hardwareIo=false}));return 0;
    }
    static int InstrumentFactoryLeaves(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);
        var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");
        var check=image.MainModule.ImportReference(typeof(Ports).GetMethod(nameof(Ports.Check))!);
        var resolve=image.MainModule.ImportReference(typeof(Ports).GetMethod(nameof(Ports.Resolve))!);
        var leaves=new Dictionary<string,string>{{"ValidateFormalSessionCapabilityAcl","validation.acl"},{"ValidateDeploymentGenerationForHcLoad","validation.payload"},{"ValidateGamepadMotion","assembly.native"},{"ConfigureHcRuntime","assembly.configure"}};
        foreach(var (name,stage)in leaves)
        {
            var method=backend.Methods.Single(m=>m.Name==name);Check(!method.IsStatic&&method.Parameters.Count==0&&method.ReturnType.FullName=="System.Void","Exact no-argument I/O leaf: "+name);
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,stage));il.Append(il.Create(OpCodes.Call,check));il.Append(il.Create(OpCodes.Ret));
        }
        var resolver=backend.Methods.Single(m=>m.Name=="ResolveDependency");Check(!resolver.IsStatic&&resolver.Parameters.Count==2&&resolver.ReturnType.FullName=="System.Reflection.Assembly","Exact dependency-lookup leaf");
        resolver.Body.Instructions.Clear();resolver.Body.Variables.Clear();resolver.Body.ExceptionHandlers.Clear();var body=resolver.Body.GetILProcessor();body.Append(body.Create(OpCodes.Ldarg_0));body.Append(body.Create(OpCodes.Ldarg_2));body.Append(body.Create(OpCodes.Callvirt,image.MainModule.ImportReference(typeof(AssemblyName).GetProperty(nameof(AssemblyName.Name))!.GetMethod!)));body.Append(body.Create(OpCodes.Call,resolve));body.Append(body.Create(OpCodes.Ldnull));body.Append(body.Create(OpCodes.Ret));
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);
        var changed=FactoryChangedMethods(source,target);
        Check(changed.Length==5&&changed.All(name=>leaves.Keys.Any(x=>name.Contains("::"+x+"("))||name.Contains("::ResolveDependency(")),"Exactly five I/O leaves change; readiness/factory/stage/handshake/write/sampling/Dispose control flow is unchanged");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),instrumentedSha256=Hash(target),changedMethods=changed,scope="Only five deployment/OS/logging/lookup terminal bodies; original compiled factory and all consumer gates unchanged",productionValidationExecuted=false,hardwareIo=false}));return 0;
    }
}
