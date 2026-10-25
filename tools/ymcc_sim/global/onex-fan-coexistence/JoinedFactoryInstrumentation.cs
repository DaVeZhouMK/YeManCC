using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;

static partial class Harness
{
    static int MutateNativePath(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var method=image.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="ValidateGamepadMotion");
        var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.Runtime.InteropServices.NativeLibrary"&&m.Name=="TryLoad");
        var operand=call.Previous.Previous;Check(operand.OpCode==OpCodes.Ldloc_1&&method.Body.Variables[0].VariableType.FullName=="System.String"&&method.Body.Variables[1].VariableType.FullName=="System.String","Native path fault restores only the exact original pre-normalization path argument");
        operand.OpCode=OpCodes.Ldloc_0;operand.Operand=null;Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Native path fault changes exactly one method and one native-loader path operand");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,mutation="Use the original unprefixed path only at native loader admission",hardwareIo=false}));return 0;
    }
    static int MutatePublicFactoryDispatch(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var type=image.MainModule.GetType("YeManFanHost.RealHcBackend");var method=type.Methods.Single(m=>m.Name=="LoadAndHandshake");method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldarg_0));il.Append(il.Create(OpCodes.Call,type.Methods.Single(m=>m.Name=="LoadAndHandshakeCore")));il.Append(il.Create(OpCodes.Ret));Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Public factory fault changes only its original public dispatcher call");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,mutation="Bypass only public STA dispatch and invoke the same original factory core on caller",hardwareIo=false}));return 0;
    }
    static int InstrumentJoinedHost(string[] args)
    {
        Check(args.Length==3||(args.Length==4&&args[3]=="public"),"Exact joined Host leaf mode required");var publicEntry=args.Length==4;
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var type=image.MainModule.GetType("YeManFanHost.RealHcBackend");var changed=new List<string>();
        if(publicEntry)
        {
            var power=type.Methods.Single(m=>m.Name=="IsPowerLineOnline");RequireReviewedMsiPowerIoTerminal(power);
            power.Body=new Mono.Cecil.Cil.MethodBody(power);var powerIl=power.Body.GetILProcessor();powerIl.Append(powerIl.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("PowerOnline")!)));powerIl.Append(powerIl.Create(OpCodes.Ret));changed.Add(power.FullName);
            var method=type.Methods.Single(m=>m.Name=="ReadCurrentOsThreadId");Check(method.IsPInvokeImpl&&method.PInvokeInfo.Module.Name=="kernel32.dll"&&method.PInvokeInfo.EntryPoint=="GetCurrentThreadId"&&method.Parameters.Count==0&&method.ReturnType.FullName=="System.UInt32","Public joined probe redirects only the exact original read-only OS identity endpoint");
            method.PInvokeInfo=null;method.IsPInvokeImpl=false;method.ImplAttributes=Mono.Cecil.MethodImplAttributes.IL|Mono.Cecil.MethodImplAttributes.Managed;method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("ReadThreadIdentity")!)));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);
        }
        var check=image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("Validation")!);
        foreach(var (name,stage)in new[]{("ValidateFormalSessionCapabilityAcl","validation.acl"),("ValidateDeploymentGenerationForHcLoad","validation.payload")})
        {
            var method=type.Methods.Single(m=>m.Name==name);method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,stage));il.Append(il.Create(OpCodes.Call,check));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);
        }
        var configure=type.Methods.Single(m=>m.Name=="ConfigureHcRuntime");var originalConfigure=FactoryMethodCanon(configure);var temp=configure.Body.Instructions.Where(i=>i.Operand is MethodReference method&&method.DeclaringType.FullName=="System.IO.Path"&&method.Name=="GetTempPath").ToArray();Check(temp.Length==1,"Only the original ConfigureHcRuntime temporary-root getter may be redirected");
        var originalOperand=temp[0].Operand;temp[0].Operand=image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("TempRoot")!);changed.Add(configure.FullName);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var actual=FactoryChangedMethods(args[0],args[1]);Check(actual.Order().SequenceEqual(changed.Order()),"Joined Host changes exactly its declared validation/configuration and optional exact OS-identity leaves");
        temp[0].Operand=originalOperand;Check(FactoryMethodCanon(configure)==originalConfigure,"Every Configure instruction, local, branch and exception region remains original after restoring the single IO getter");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=actual,originalMvid=image.MainModule.Mvid,publicStaEntryPreserved=publicEntry,publicPowerIoIsolated=publicEntry,originalPowerTerminalShapeReviewed=publicEntry,osIdentityEndpoint=publicEntry?"actual-kernel32-GetCurrentThreadId-through-exact-port":null,configureBodyExactExceptSingleTempGetter=true,actualNativeAdmissionPreserved=true,actualAssemblyLoadPreserved=true,actualConfigureAndResolverPreserved=true,actualFactoryIdentityReadinessAndCapabilityPreserved=true,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int InstrumentJoinedHc(string[] args)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetFullPath(args[3]));resolver.AddSearchDirectory(Path.GetFullPath(args[4]));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});var methods=new List<string>();
        var sensor=image.MainModule.GetType("HandheldCompanion.Devices.IDevice").Methods.Single(m=>m.Name=="PullSensors");sensor.Body=new Mono.Cecil.Cil.MethodBody(sensor);var il=sensor.Body.GetILProcessor();il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("Sensors")!)));il.Append(il.Create(OpCodes.Ret));methods.Add(sensor.FullName);
        var setting=image.MainModule.GetType("HandheldCompanion.Managers.SettingsManager").Methods.Single(m=>m.Name=="SetProperty");setting.Body=new Mono.Cecil.Cil.MethodBody(setting);il=setting.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldarg_1));il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(JoinedFactoryPorts).GetMethod("SettingWrite")!)));il.Append(il.Create(OpCodes.Ret));methods.Add(setting.FullName);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Order().SequenceEqual(methods.Order()),"Joined HC adds only sensor discovery and refused settings IO leaves to original constructor fixture");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=changed,originalMvid=image.MainModule.Mvid,originalAllInstanceConstructorsGetCurrentAndInitializeUnchanged=true,originalNativeMathUnchanged=true,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
}
