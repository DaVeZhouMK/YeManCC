using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
static partial class Harness
{
    static int MutateMsiLifecycle(string[] args,string mode)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");var type=image.MainModule.GetType("YeManFanHost.MsiFanDeviceLifecycle");MethodDefinition method;
        if(mode=="narrow-open")
        {
            method=host.Methods.Single(m=>m.Name=="OpenHcDevice");var i=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==type.FullName&&m.Name=="IsMsiFamily");var il=method.Body.GetILProcessor();i.OpCode=OpCodes.Pop;i.Operand=null;il.InsertAfter(i,il.Create(OpCodes.Ldc_I4_0));
        }
        else if(mode=="usable")
        {
            method=type.Methods.Single(m=>m.Name=="Open");var i=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldstr&&i.Operand?.ToString()=="IsUsable");i.Operand="TransportOk";
        }
        else if(mode=="firmware")
        {
            method=type.Methods.Single(m=>m.Name=="Open");var i=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==type.FullName&&m.Name=="ObserveBoundFirmware");i.OpCode=OpCodes.Pop;i.Operand=null;
        }
        else if(mode=="release-dispatch")
        {
            method=type.Methods.Single(m=>m.Name=="Close");var i=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==type.FullName&&m.Name=="Invoke");var il=method.Body.GetILProcessor();i.OpCode=OpCodes.Pop;i.Operand=null;var popTarget=il.Create(OpCodes.Pop);var popMethod=il.Create(OpCodes.Pop);il.InsertAfter(i,popTarget);il.InsertAfter(popTarget,popMethod);il.InsertAfter(popMethod,il.Create(OpCodes.Ldnull));
        }
        else if(mode=="settlement")
        {
            method=host.Methods.Single(m=>m.Name=="SettleMsiCloseRelease");var i=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.Name=="ClassifyMsiReleaseStatus");var enumType=image.MainModule.GetType("YeManFanHost.OemReleaseAction");var value=Convert.ToInt32(enumType.Fields.Single(f=>f.Name=="Proven").Constant);var il=method.Body.GetILProcessor();i.OpCode=OpCodes.Pop;i.Operand=null;il.InsertAfter(i,il.Create(OpCodes.Ldc_I4,value));
        }
        else if(mode=="binding")
        {
            method=host.Methods.Single(m=>m.Name=="ReadMsiStructuredRelease");var calls=method.Body.Instructions.Where(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.String"&&m.Name=="Equals").ToArray();Check(calls.Length==2,"Original MSI receipt has exactly boot and operation string binding checks");var il=method.Body.GetILProcessor();foreach(var i in calls){var pop=il.Create(OpCodes.Pop);il.InsertAfter(i,pop);il.InsertAfter(pop,il.Create(OpCodes.Ldc_I4_1));}
        }
        else throw new InvalidOperationException("Unknown MSI lifecycle fault");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"MSI fault must change exactly its intended original consumer");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",mode,sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int InstrumentMsiLifecycleHost(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");var changed=new List<string>();
        var payload=host.Methods.Single(m=>m.Name=="PrepareMsiWmiPayloadBeforeOpen");var originalPayload=FactoryMethodCanon(payload);var operand=payload.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.Environment"&&m.Name=="GetFolderPath");var originalOperand=operand.Operand;operand.Operand=image.MainModule.ImportReference(typeof(MsiLifecycleIoPorts).GetMethod("WindowsFolder")!);changed.Add(payload.FullName);
        var probe=host.Methods.Single(m=>m.Name=="ProbeMsiWmiInstance");var ctor=image.MainModule.GetType("YeManFanHost.MsiWmiMetadataProbe").Methods.Single(m=>m.IsConstructor&&!m.IsStatic&&m.Parameters.Count==3);probe.Body=new Mono.Cecil.Cil.MethodBody(probe);var il=probe.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldarg_1));il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(MsiLifecycleIoPorts).GetMethod("MetadataReady")!)));il.Append(il.Create(OpCodes.Ldc_I4_0));il.Append(il.Create(OpCodes.Ldstr,"isolated-original-wmi-metadata-endpoint"));il.Append(il.Create(OpCodes.Newobj,ctor));il.Append(il.Create(OpCodes.Ret));changed.Add(probe.FullName);
        var enumerate=host.Methods.Single(m=>m.Name=="EnumerateMsiPnpDeviceIds");enumerate.Body=new Mono.Cecil.Cil.MethodBody(enumerate);il=enumerate.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,"general-msi-pnp-enumeration"));il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(LifecycleIoPorts).GetMethod("Forbidden")!)));il.Append(il.Create(OpCodes.Ldnull));il.Append(il.Create(OpCodes.Ret));changed.Add(enumerate.FullName);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var actual=FactoryChangedMethods(args[0],args[1]);Check(actual.Order().SequenceEqual(changed.Order()),"MSI Host ports change only resource target operand, metadata endpoint and forbidden PnP enumeration; actual preparer/orchestrator stay present");operand.Operand=originalOperand;Check(FactoryMethodCanon(payload)==originalPayload,"Restoring only the folder operand preserves complete preparer body");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=actual,preparerBodyChangedOnlyTargetOperand=true,originalRegistrationOrchestratorUnchanged=true,systemPayloadRegistryAndPnpWritesForbidden=true,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int InstrumentMsiLifecycleHc(string[] args)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetFullPath(args[3]));resolver.AddSearchDirectory(Path.GetFullPath(args[4]));using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});var changed=new List<string>();
        var root=image.MainModule.GetType("HandheldCompanion.Devices.IDevice");var claw=image.MainModule.GetType("HandheldCompanion.Devices.ClawA1M");var wmi=image.MainModule.GetType("HandheldCompanion.WMI");
        MethodReference Port(string name)=>image.MainModule.ImportReference(typeof(MsiLifecycleIoPorts).GetMethod(name)!);
        void Forward(MethodDefinition method,string port){method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();for(int i=0;i<method.Parameters.Count;i++)il.Append(il.Create(OpCodes.Ldarg,method.Parameters[i]));il.Append(il.Create(OpCodes.Call,Port(port)));il.Append(il.Create(OpCodes.Castclass,method.ReturnType));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);}
        Forward(root.Methods.Single(m=>m.Name=="GetHidDevices"&&m.Parameters[1].ParameterType is ArrayType),"Enumerate");Forward(wmi.Methods.Single(m=>m.Name=="GetStructured"),"Read");Forward(wmi.Methods.Single(m=>m.Name=="SetStructured"),"Write");Forward(wmi.Methods.Single(m=>m.Name=="Get"),"ReadLegacy");
        var ready=claw.Methods.Single(m=>m.Name=="IsReady");var originalReady=FactoryMethodCanon(ready);var readinessPorts=new List<(Instruction i,OpCode opcode,object operand)>();int replaced=0;foreach(var i in ready.Body.Instructions)if(i.Operand is MethodReference m&&m.FullName=="System.Boolean HidLibrary.HidDevice::get_IsConnected()"){readinessPorts.Add((i,i.OpCode,i.Operand));i.OpCode=OpCodes.Call;i.Operand=Port("Connected");replaced++;}Check(replaced==2,"Original MSI readiness has exactly two connection IO calls");changed.Add(ready.FullName);
        foreach(var (type,name) in new[]{(claw,"Open"),(claw,"Close"),(wmi,"Set"),(claw,"CheckAndFixRegistry"),(image.MainModule.GetType("HandheldCompanion.PnPUtil"),"RestartDevice")}){var method=type.Methods.Single(m=>m.Name==name);method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,"general-msi-"+name));il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(LifecycleIoPorts).GetMethod("Forbidden")!)));if(method.ReturnType.FullName=="System.Boolean")il.Append(il.Create(OpCodes.Ldc_I4_0));else if(method.ReturnType.FullName!="System.Void")il.Append(il.Create(OpCodes.Ldnull));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);}
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var actual=FactoryChangedMethods(args[0],args[1]);Check(actual.Order().SequenceEqual(changed.Order()),"MSI adds exactly ten named IO/trap changes; original lifecycle and release consumers stay unchanged");foreach(var row in readinessPorts){row.i.OpCode=row.opcode;row.i.Operand=row.operand;}Check(FactoryMethodCanon(ready)==originalReady,"Restoring only original two IsConnected IO calls preserves every readiness instruction, local, branch and exception region");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=actual,originalReadinessConnectionCallsPorted=replaced,originalReadinessAlgorithmPreservedExceptConnectionIo=true,originalBaseOpenAndMsiStructuredReleaseUnchanged=true,hidLibraryModified=false,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int MsiLifecycleInventory(string[] args)
    {
        Check(Hash(args[0])=="08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583","MSI inventory requires exact original HC");
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);resolver.AddSearchDirectory(Path.GetFullPath(args[2]));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});
        object Describe(TypeDefinition t)=>new {type=t.FullName,fields=t.Fields.Select(f=>new{name=f.Name,type=f.FieldType.FullName}).ToArray(),properties=t.Properties.Select(p=>new{name=p.Name,type=p.PropertyType.FullName}).ToArray(),methods=t.Methods.Select(m=>new {signature=m.FullName,calls=m.HasBody?m.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>new{op=i.OpCode.Name,method=i.Operand.ToString()}).ToArray():null}).ToArray()};
        var names=new[]{"HandheldCompanion.Devices.ClawA1M","HandheldCompanion.Devices.ClawA2VM","HandheldCompanion.Devices.ClawBZ2EM","HandheldCompanion.Devices.MSI.ClawCG3EM","HandheldCompanion.WMI","HandheldCompanion.Devices.IDevice","HandheldCompanion.OpenLibSys"};
        var types=names.Select(n=>image.MainModule.GetType(n)).ToArray();
        using var hid=resolver.Resolve(image.MainModule.AssemblyReferences.Single(r=>r.Name=="HidLibrary"));
        var hidTypes=new[]{"HidLibrary.HidDevice","HidLibrary.HidDeviceAttributes","HidLibrary.HidDeviceCapabilities"}.Select(n=>hid.MainModule.GetType(n)).Where(t=>t is not null).ToArray();
        var result=new{status="PASS",hcSha256=Hash(args[0]),hidSha256=Hash(hid.MainModule.FileName),types=types.Select(Describe),nested=types.SelectMany(t=>t.NestedTypes).Select(Describe),hid=hidTypes.Select(Describe),hardwareIo=false,assembliesExecuted=false};
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);File.WriteAllText(args[1],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
}
