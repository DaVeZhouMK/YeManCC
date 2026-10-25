using System.IO;
using System.Reflection;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Mono.Cecil;
using Mono.Cecil.Cil;
using ValidationPorts=ConsumerFixture.ValidationPorts;
static partial class Harness
{
    sealed record ValidationFixture(string Host,string Runtime,string Record)
    {
        public string HostManifest=>Path.Combine(Host,"YeManFanHost.payload.json");
        public string RuntimeManifest=>Path.Combine(Runtime,"HandheldCompanion.runtime.json");
    }
    static object? ValidationStatic(string name,params object?[] args)
    {
        try{return Candidate.GetType("YeManFanHost.RealHcBackend",true)!.GetMethods(All).Single(m=>m.Name==name&&m.IsStatic&&m.GetParameters().Length==args.Length).Invoke(null,args);}
        catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    static void ValidationJson(string path,JsonNode value){Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.WriteAllText(path,value.ToJsonString(new JsonSerializerOptions{WriteIndented=true}));}
    static JsonObject ValidationRead(string path)=>(JsonObject)JsonNode.Parse(File.ReadAllText(path))!;
    static ValidationFixture MakeValidationFixture(string directory)
    {
        var host=Path.Combine(directory,"host");var runtime=Path.Combine(directory,"runtime");Directory.CreateDirectory(host);Directory.CreateDirectory(runtime);
        foreach(var file in new[]{"YeManFanHost.exe","YeManFanHost.dll","YeManFanBackend.dll","YeManFanHost.deps.json","YeManFanHost.runtimeconfig.json"})File.WriteAllText(Path.Combine(host,file),"INERT-MANIFEST-FIXTURE:"+file);
        foreach(var file in new[]{"HandheldCompanion.dll","HandheldCompanion.deps.json","LibreHardwareMonitorLib.dll","Microsoft.Windows.SDK.NET.dll","WinRT.Runtime.dll","GamepadMotion.dll","hidapi.dll","hidapi.net.dll"})File.WriteAllText(Path.Combine(runtime,file),"NOT-A-LOADABLE-IMAGE:"+file);
        Directory.CreateDirectory(Path.Combine(runtime,"Resources"));File.WriteAllText(Path.Combine(runtime,"Resources","fixture.txt"),"fixture-data");
        JsonArray Entries(string root)=>new(Directory.GetFiles(root,"*",SearchOption.AllDirectories).Order().Select(file=>(JsonNode)new JsonObject{{"path",Path.GetRelativePath(root,file).Replace('\\','/')},{"sha256",Hash(file)}}).ToArray());
        ValidationJson(Path.Combine(runtime,"HandheldCompanion.runtime.json"),new JsonObject{{"schemaVersion",1},{"runtimeId","YMCC-DEVICECORE-20261018-v1"},{"files",Entries(runtime)}});
        ValidationJson(Path.Combine(host,"YeManFanHost.payload.json"),new JsonObject{{"schemaVersion",2},{"runtimeId","YMCC-DEVICECORE-20261018-v1"},{"runtimeManifest",Path.GetRelativePath(host,Path.Combine(runtime,"HandheldCompanion.runtime.json"))},{"files",Entries(host)}});
        var logs=Path.Combine(directory,"isolated-host-logs");Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",logs);Directory.CreateDirectory(logs);
        ValidationPorts.HostRoot=host;return new(host,runtime,Path.Combine(logs,"YeManFanHost.deployment.json"));
    }
    static void RemoveValidationBackend(ValidationFixture fixture)
    {
        const string file="YeManFanBackend.dll";File.Delete(Path.Combine(fixture.Host,file));
        var document=ValidationRead(fixture.HostManifest);var files=document["files"]!.AsArray();
        files.Remove(files.Single(entry=>entry!["path"]!.GetValue<string>()==file));ValidationJson(fixture.HostManifest,document);
    }
    static object ValidationBackend(ValidationFixture fixture)
    {
        var backend=NewFactoryBackend(assemblyPath:Path.Combine(fixture.Runtime,"HandheldCompanion.dll"));
        Set(Get(backend,"options")!,"HcRuntimeRoot",fixture.Runtime);return backend;
    }
    static void SeedValidationRecord(ValidationFixture fixture)=>ValidationJson(fixture.Record,new JsonObject{
        {"schemaVersion",1},{"validationRulesVersion",1},{"payloadRoot",fixture.Host.ToLowerInvariant()},{"runtimeRoot",fixture.Runtime.ToLowerInvariant()},
        {"payloadManifestSha256",Hash(fixture.HostManifest)},{"runtimeManifestSha256",Hash(fixture.RuntimeManifest)},
        {"runtimeId",ValidationRead(fixture.HostManifest)["runtimeId"]!.DeepClone()},{"result","ok"}});
    static void ValidationReject(Action action,string fragment="")
    {
        var failure=CaptureSta(action);Check(failure is not null,"Actual deployment consumer must reject this invalid fixture");
        Check(fragment.Length==0||failure!.ToString().Contains(fragment,StringComparison.OrdinalIgnoreCase),"Actual rejection must come from the binding/validation boundary: "+fragment+"; got "+failure);
    }
    static void RunValidationCases(bool staticOnly)
    {
        var fixtures=Path.Combine(Path.GetDirectoryName(AuthFile)!,"validation-cases");Directory.CreateDirectory(fixtures);var index=0;
        void Case(string id,Action<ValidationFixture> action)=>Run(id,()=>{var directory=Path.Combine(fixtures,(index++).ToString());var fixture=MakeValidationFixture(directory);try{action(fixture);}finally{ValidationPorts.HostRoot="";}});
        void Split(ValidationFixture fixture)=>ValidationStatic("ValidateSplitPayloadManifestAt",fixture.Host,fixture.Runtime);
        Case("validation-static-valid-split-and-nested-resource",fixture=>{Split(fixture);Check(true,"Actual split validator accepts a coherent relative pair and declared nested resource");});
        Case("validation-static-normalized-relative-binding",fixture=>{var m=ValidationRead(fixture.HostManifest);m["runtimeManifest"]="./../runtime//HandheldCompanion.runtime.json";ValidationJson(fixture.HostManifest,m);Split(fixture);Check(true,"Equivalent explicit runtime binding stays usable");});
        var badBindings=new (string Kind,Action<ValidationFixture> Mutate)[]{
            ("runtime-id",f=>{var m=ValidationRead(f.RuntimeManifest);m["runtimeId"]="OTHER-REVIEWED-RUNTIME";ValidationJson(f.RuntimeManifest,m);}),
            ("missing-runtime-id",f=>{var m=ValidationRead(f.RuntimeManifest);m.Remove("runtimeId");ValidationJson(f.RuntimeManifest,m);}),
            ("null-runtime-id",f=>{var m=ValidationRead(f.RuntimeManifest);m["runtimeId"]=null;ValidationJson(f.RuntimeManifest,m);}),
            ("missing-host-id",f=>{var m=ValidationRead(f.HostManifest);m.Remove("runtimeId");ValidationJson(f.HostManifest,m);}),
            ("empty-host-id",f=>{var m=ValidationRead(f.HostManifest);m["runtimeId"]=" ";ValidationJson(f.HostManifest,m);}),
            ("wrong-reference",f=>{var m=ValidationRead(f.HostManifest);m["runtimeManifest"]="../other/HandheldCompanion.runtime.json";ValidationJson(f.HostManifest,m);}),
            ("missing-reference",f=>{var m=ValidationRead(f.HostManifest);m.Remove("runtimeManifest");ValidationJson(f.HostManifest,m);}),
            ("null-reference",f=>{var m=ValidationRead(f.HostManifest);m["runtimeManifest"]=null;ValidationJson(f.HostManifest,m);}),
            ("absolute-reference",f=>{var m=ValidationRead(f.HostManifest);m["runtimeManifest"]=f.RuntimeManifest;ValidationJson(f.HostManifest,m);})};
        foreach(var (kind,mutate)in badBindings)Case("validation-static-reject-binding:"+kind,fixture=>{mutate(fixture);ValidationReject(()=>Split(fixture));});
        Case("validation-static-hash-mismatch-remains-rejected",fixture=>{File.AppendAllText(Path.Combine(fixture.Runtime,"HandheldCompanion.dll"),"changed");ValidationReject(()=>Split(fixture));});
        Case("validation-static-unsafe-member-remains-rejected",fixture=>{var m=ValidationRead(fixture.RuntimeManifest);m["files"]!.AsArray().Add(new JsonObject{{"path","../outside.dll"},{"sha256",new string('0',64)}});ValidationJson(fixture.RuntimeManifest,m);ValidationReject(()=>Split(fixture));});
        Case("validation-static-required-file-remains-rejected",fixture=>{var m=ValidationRead(fixture.RuntimeManifest);var files=m["files"]!.AsArray();files.Remove(files.Single(n=>n!["path"]!.GetValue<string>()=="HandheldCompanion.dll"));ValidationJson(fixture.RuntimeManifest,m);ValidationReject(()=>Split(fixture));});
        Case("validation-static-duplicate-remains-rejected",fixture=>{var m=ValidationRead(fixture.RuntimeManifest);m["files"]!.AsArray().Add(m["files"]![0]!.DeepClone());ValidationJson(fixture.RuntimeManifest,m);ValidationReject(()=>Split(fixture));});
        Case("validation-static-unexpected-file-remains-rejected",fixture=>{File.WriteAllText(Path.Combine(fixture.Runtime,"unlisted.dll"),"extra");ValidationReject(()=>Split(fixture));});
        Case("validation-static-required-backend",fixture=>{RemoveValidationBackend(fixture);ValidationReject(()=>Split(fixture),"YeManFanBackend.dll");});
        var owner=WindowsIdentity.GetCurrent().User!;
        FileSecurity Security(SecurityIdentifier sid,FileSystemRights rights){var security=new FileSecurity();security.AddAccessRule(new FileSystemAccessRule(sid,rights,AccessControlType.Allow));return security;}
        foreach(var (kind,sid)in new[]{("owner",owner),("system",new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null)),("admins",new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null))})
            Run("validation-acl-trusted:"+kind,()=>Check(ValidationStatic("IsPrivateCapabilityAcl",Security(sid,FileSystemRights.FullControl))is true,"Trusted capability principal remains private"));
        foreach(var (label,rights) in new[]{("read",FileSystemRights.ReadData),("write",FileSystemRights.WriteData),("permissions",FileSystemRights.ChangePermissions)})Run("validation-acl-untrusted:"+label,()=>Check(ValidationStatic("IsPrivateCapabilityAcl",Security(new SecurityIdentifier(WellKnownSidType.WorldSid,null),rights))is false,"Untrusted capability access is rejected"));
        foreach(var present in new[]{false,true})Run("validation-acl-null-dacl:"+present,()=>
        {
            var raw=new RawSecurityDescriptor(present?ControlFlags.DiscretionaryAclPresent:ControlFlags.None,owner,owner,null,null);var bytes=new byte[raw.BinaryLength];raw.GetBinaryForm(bytes,0);var security=new FileSecurity();security.SetSecurityDescriptorBinaryForm(bytes);
            Check(ValidationStatic("IsPrivateCapabilityAcl",security)is false,"A NULL/absent DACL cannot be accepted as a private capability");
        });
        Run("validation-acl-empty-dacl-is-distinct",()=>
        {
            var raw=new RawSecurityDescriptor(ControlFlags.DiscretionaryAclPresent,owner,owner,null,new RawAcl(2,0));var bytes=new byte[raw.BinaryLength];raw.GetBinaryForm(bytes,0);var security=new FileSecurity();security.SetSecurityDescriptorBinaryForm(bytes);
            Check(ValidationStatic("IsPrivateCapabilityAcl",security)is true,"Empty denying DACL remains distinct from NULL granting DACL");
        });
        if(staticOnly)return;
        foreach(var cached in new[]{false,true})Case("validation-generation-valid:"+cached,fixture=>
        {
            if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);var events=new List<string>();Set(backend,"Diagnostic",(Action<string,object?>)((name,_)=>events.Add(name)));
            try{Invoke(backend,"ValidateDeploymentGenerationForHcLoad");Check(events.Contains(cached?"deployment.record-hit":"deployment.record-miss"),"Actual generation takes the expected cache/full branch");Check(File.Exists(fixture.Record),"Actual full validation writes a bounded deployment record");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var kind in new[]{"runtime-id","wrong-reference","absolute-reference","missing-reference"})foreach(var cached in new[]{false,true})Case("validation-generation-reject-binding:"+kind+":"+cached,fixture=>
        {
            badBindings.Single(row=>row.Kind==kind).Mutate(fixture);if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);
            try{ValidationReject(()=>Invoke(backend,"ValidateDeploymentGenerationForHcLoad"));}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var manifest in new[]{"host","runtime"})foreach(var cached in new[]{false,true})Case("validation-generation-reject-schema:"+manifest+":"+cached,fixture=>
        {
            var path=manifest=="host"?fixture.HostManifest:fixture.RuntimeManifest;var document=ValidationRead(path);document["schemaVersion"]=0;ValidationJson(path,document);if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);
            try{ValidationReject(()=>Invoke(backend,"ValidateDeploymentGenerationForHcLoad"));}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var cached in new[]{false,true})Case("validation-generation-required-backend:"+cached,fixture=>
        {
            RemoveValidationBackend(fixture);if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);
            try{ValidationReject(()=>Invoke(backend,"ValidateDeploymentGenerationForHcLoad"),"YeManFanBackend.dll");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var cached in new[]{false,true})Case("validation-factory-required-backend:"+cached,fixture=>
        {
            RemoveValidationBackend(fixture);if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);
            try{ValidationReject(()=>Invoke(backend,"LoadAndHandshakeCore"),"YeManFanBackend.dll");Check(Get(backend,"assemblyDirectory")is null&&Get(backend,"assembly")is null&&Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Actual factory refuses an incomplete Host before assembly/native setup");}finally{((IDisposable)backend).Dispose();}
        });
        Case("validation-generation-cache-retains-no-per-dll-rehash",fixture=>
        {
            SeedValidationRecord(fixture);File.AppendAllText(Path.Combine(fixture.Runtime,"HandheldCompanion.dll"),"changed-after-deployment");var backend=ValidationBackend(fixture);var events=new List<string>();Set(backend,"Diagnostic",(Action<string,object?>)((name,_)=>events.Add(name)));
            try{Invoke(backend,"ValidateDeploymentGenerationForHcLoad");Check(events.Contains("deployment.record-hit"),"Matching deployment record keeps the explicitly approved no-per-DLL-rehash contract");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var kind in new[]{"runtime-id","wrong-reference"})foreach(var cached in new[]{false,true})Case("validation-factory-binding-before-assembly:"+kind+":"+cached,fixture=>
        {
            badBindings.Single(row=>row.Kind==kind).Mutate(fixture);if(cached)SeedValidationRecord(fixture);var backend=ValidationBackend(fixture);
            try{ValidationReject(()=>Invoke(backend,"LoadAndHandshakeCore"),"Fan Host runtime binding");Check(Get(backend,"assemblyDirectory")is null&&Get(backend,"assembly")is null&&Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Actual factory refuses the wrong pair before assembly/native setup");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var contents in new[]{"{","{\"schemaVersion\":9223372036854775807}"})Case("validation-generation-damaged-record:"+contents.Length,fixture=>
        {
            File.WriteAllText(fixture.Record,contents);var backend=ValidationBackend(fixture);try{Invoke(backend,"ValidateDeploymentGenerationForHcLoad");Check(ValidationRead(fixture.Record)["schemaVersion"]!.GetValue<int>()==1,"Damaged scheduling data falls back to actual full validation");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var present in new[]{false,true})Case("validation-native-missing-or-invalid:"+present,fixture=>
        {
            if(!present)File.Delete(Path.Combine(fixture.Runtime,"GamepadMotion.dll"));var backend=ValidationBackend(fixture);Set(backend,"hcRuntimeDirectory",fixture.Runtime);
            try{ValidationReject(()=>Invoke(backend,"ValidateGamepadMotion"),present?"could not load":"not found");Check(Get(backend,"assembly")is null,"Missing or non-loadable fixture native data cannot load a device assembly");}finally{((IDisposable)backend).Dispose();}
        });
    }
    static int RunValidationRealMode(string[] args)
    {
        using var config=JsonDocument.Parse(File.ReadAllText(args[4]));var value=config.RootElement;
        var fixture=new ValidationFixture(value.GetProperty("hostRoot").GetString()!,value.GetProperty("runtimeRoot").GetString()!,value.GetProperty("recordPath").GetString()!);
        var allowed=Path.GetDirectoryName(Path.GetFullPath(args[3]))!+Path.DirectorySeparatorChar;
        Check(new[]{fixture.Host,fixture.Runtime,fixture.Record}.All(path=>Path.GetFullPath(path).StartsWith(allowed,StringComparison.OrdinalIgnoreCase)),"Genuine byte fixtures remain inside their owned evidence root");
        Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",Path.GetDirectoryName(fixture.Record));ValidationPorts.HostRoot=fixture.Host;
        Run("validation-real-current-host-runtime-and-shared",()=>{ValidationStatic("ValidateSplitPayloadManifestAt",fixture.Host,fixture.Runtime);Check(true,"Actual full validator consumes genuine current Host/runtime/shared file bytes");});
        Run("validation-real-owned-context-survives-gc",()=>
        {
            GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();var backend=ValidationBackend(fixture);
            try{Check(Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Owned test context stays alive for lazy actual Host dependencies after a full metadata scan");}finally{((IDisposable)backend).Dispose();}
        });
        foreach(var cached in new[]{false,true})Run("validation-real-generation:"+cached,()=>
        {
            var backend=ValidationBackend(fixture);var events=new List<string>();Set(backend,"Diagnostic",(Action<string,object?>)((name,_)=>events.Add(name)));
            try{Invoke(backend,"ValidateDeploymentGenerationForHcLoad");Check(events.Contains(cached?"deployment.record-hit":"deployment.record-miss")&&File.Exists(fixture.Record),"Genuine deployment validates and then reuses the exact current pair");}finally{((IDisposable)backend).Dispose();}
        });
        var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,genuineDeploymentBytesValidated=true,deviceInitializationExecuted=false},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }
    static int RunValidationCasesMode(string[] args,bool staticOnly)
    {
        RunValidationCases(staticOnly);var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,
            actualManifestAndGenerationConsumers=true,manifestFixtureFilesAreInertData=true,aclDescriptorsInMemoryOnly=true,nativeLoaderOnlyRejectsInvalidFixtureBytes=true,staticOnly},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }
    static int MutateValidationGuard(string[] args,string kind)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");
        var method=backend.Methods.Single(m=>m.Name==(kind=="binding"?"ValidateRuntimeManifestBinding":kind=="fast-path"?"ValidateDeploymentGenerationForHcLoad":"IsPrivateCapabilityAcl"));var il=method.Body.GetILProcessor();
        List<Instruction> ReadHostId(Instruction load)
        {
            var options=new VariableDefinition(image.MainModule.ImportReference(typeof(JsonDocumentOptions)));method.Body.Variables.Add(options);method.Body.InitLocals=true;
            return [load,il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(File).GetMethod(nameof(File.ReadAllText),[typeof(string)])!)),
                il.Create(OpCodes.Ldloca,options),il.Create(OpCodes.Initobj,image.MainModule.ImportReference(typeof(JsonDocumentOptions))),il.Create(OpCodes.Ldloc,options),
                il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(JsonDocument).GetMethod(nameof(JsonDocument.Parse),[typeof(string),typeof(JsonDocumentOptions)])!)),
                il.Create(OpCodes.Callvirt,image.MainModule.ImportReference(typeof(JsonDocument).GetProperty(nameof(JsonDocument.RootElement))!.GetMethod!)),
                il.Create(OpCodes.Call,backend.Methods.Single(m=>m.Name=="ReadPayloadRuntimeId"))];
        }
        if(kind=="binding")
        {
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();
            foreach(var instruction in ReadHostId(il.Create(OpCodes.Ldarg_1)))il.Append(instruction);il.Append(il.Create(OpCodes.Ret));
        }
        else if(kind=="fast-path")
        {
            var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.Name=="ValidateRuntimeManifestBinding");
            var file=new VariableDefinition(image.MainModule.ImportReference(typeof(string)));method.Body.Variables.Add(file);
            var instructions=new List<Instruction>{il.Create(OpCodes.Stloc,file),il.Create(OpCodes.Pop)};instructions.AddRange(ReadHostId(il.Create(OpCodes.Ldloc,file)));
            call.OpCode=OpCodes.Pop;call.Operand=null;var cursor=call;foreach(var instruction in instructions){il.InsertAfter(cursor,instruction);cursor=instruction;}
        }
        else if(kind=="acl")
        {
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();il.Append(il.Create(OpCodes.Ldc_I4_1));il.Append(il.Create(OpCodes.Ret));
        }
        else throw new InvalidOperationException("Unknown validation fault");
        WidenStaBranches(method);Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);var changed=FactoryChangedMethods(source,target);
        Check(changed.Length==1&&changed[0]==method.FullName,"Validation fault changes exactly its one named original method");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation=kind,hardwareIo=false}));return 0;
    }
    static int InstrumentValidationPaths(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");
        foreach(var name in new[]{"ValidateDeploymentGenerationForHcLoad","ValidateSplitPayloadManifest"})
        {
            var method=backend.Methods.Single(m=>m.Name==name);var getter=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==typeof(AppContext).FullName&&m.Name=="get_BaseDirectory");
            getter.Operand=image.MainModule.ImportReference(typeof(ValidationPorts).GetMethod(nameof(ValidationPorts.BaseDirectory))!);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);var changed=FactoryChangedMethods(source,target);
        Check(changed.Length==2&&changed.All(name=>name.Contains("::ValidateDeploymentGenerationForHcLoad(")||name.Contains("::ValidateSplitPayloadManifest(")),"Only two base-directory I/O getter operands change; actual manifest, cache, ACL and stage logic retained");
        using(var original=AssemblyDefinition.ReadAssembly(source))using(var modified=AssemblyDefinition.ReadAssembly(target))
        {
            var originalType=original.MainModule.GetType("YeManFanHost.RealHcBackend");var modifiedType=modified.MainModule.GetType("YeManFanHost.RealHcBackend");
            foreach(var name in new[]{"ValidateDeploymentGenerationForHcLoad","ValidateSplitPayloadManifest"})
            {
                var before=originalType.Methods.Single(m=>m.Name==name);var after=modifiedType.Methods.Single(m=>m.Name==name);
                var getter=after.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(ValidationPorts).FullName&&call.Name==nameof(ValidationPorts.BaseDirectory));
                getter.Operand=before.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(AppContext).FullName&&call.Name=="get_BaseDirectory").Operand;
                Check(FactoryMethodCanon(before)==FactoryMethodCanon(after),"Restoring one getter operand restores every original validation instruction and exception boundary");
            }
        }
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),instrumentedSha256=Hash(target),changedMethods=changed,
            originalValidationInstructionsRetained=true,scope="Only AppContext.BaseDirectory reads in two existing consumers return owned Build fixture roots; no validation gate replaced",hardwareIo=false}));return 0;
    }
}
