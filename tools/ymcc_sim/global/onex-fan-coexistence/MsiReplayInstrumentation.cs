using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
static partial class Harness
{
    static int MutateMsiReplay(string[] args,string mode)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> rows)=>rows.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));MethodDefinition method;
        if(mode=="sta")
        {
            method=host.Methods.Single(m=>m.Name=="PowerProfileWatchdogTick");var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.Name=="OnStaBounded");call.OpCode=OpCodes.Call;call.Operand=image.MainModule.ImportReference(typeof(MsiReplayIoPorts).GetMethod("Direct")!);
        }
        else if(mode=="timer-owner")
        {
            method=Types(image.MainModule.Types).SelectMany(t=>t.Methods).Single(m=>m.Name.Contains("EnsurePowerProfileWatchdog",StringComparison.Ordinal)&&m.HasBody&&m.Body.Instructions.Any(i=>i.Operand is MethodReference c&&c.Name=="PowerProfileWatchdogTick"));var target=host.Methods.Single(m=>m.Name=="PowerProfileWatchdogTick");var owner=method.DeclaringType.Fields.Single(f=>f.FieldType.FullName==host.FullName);method.Body=new Mono.Cecil.Cil.MethodBody(method){InitLocals=true};var local=new VariableDefinition(target.Parameters[0].ParameterType);method.Body.Variables.Add(local);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldarg_0));il.Append(il.Create(OpCodes.Ldfld,owner));il.Append(il.Create(OpCodes.Ldloca,local));il.Append(il.Create(OpCodes.Initobj,local.VariableType));il.Append(il.Create(OpCodes.Ldloc,local));il.Append(il.Create(OpCodes.Call,target));il.Append(il.Create(OpCodes.Ret));
        }
        else if(mode=="admitted-owner")
        {
            method=host.Methods.Single(m=>m.Name=="PowerProfileWatchdogTickCore");Check(method.Parameters.Count==2,"Original queued replay carries explicit nullable owner");var il=method.Body.GetILProcessor();var first=method.Body.Instructions[0];il.InsertBefore(first,il.Create(OpCodes.Ldarga,method.Parameters[1]));il.InsertBefore(first,il.Create(OpCodes.Initobj,method.Parameters[1].ParameterType));
        }
        else if(mode=="mode-settlement")
        {
            method=host.Methods.Single(m=>m.Name=="PowerProfileWatchdogTickCore");var address=method.Body.Instructions.First(i=>i.OpCode==OpCodes.Ldflda&&i.Operand is FieldReference f&&f.Name=="msiFirmwareModeSettledRevision");var call=address;while(call.Operand is not MethodReference c||c.DeclaringType.FullName!="System.Threading.Interlocked"||c.Name!="Exchange")call=call.Next;var il=method.Body.GetILProcessor();call.OpCode=OpCodes.Pop;call.Operand=null;var pop=il.Create(OpCodes.Pop);il.InsertAfter(call,pop);il.InsertAfter(pop,il.Create(OpCodes.Ldc_I8,0L));
        }
        else if(mode=="profile-complete")
        {
            method=host.NestedTypes.Single(t=>t.Name=="ExternalProfilePendingQueue").Methods.Single(m=>m.Name=="Complete");method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldc_I4_1));il.Append(il.Create(OpCodes.Ret));
        }
        else if(mode=="followup")
        {
            method=host.Methods.Single(m=>m.Name=="ArmBoundedProfileWatchdogFollowUp");method.Body=new Mono.Cecil.Cil.MethodBody(method);method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Ret));
        }
        else throw new InvalidOperationException("Unknown MSI replay fault");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Replay faults change exactly the intended original method");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",mode,sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int InstrumentMsiReplayHost(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");var changed=new List<string>();
        var create=host.Methods.Single(m=>m.Name=="EnsurePowerProfileWatchdog");var originalCreate=FactoryMethodCanon(create);var ctor=create.Body.Instructions.Single(i=>i.OpCode==OpCodes.Newobj&&i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.Threading.Timer");var originalCtor=ctor.Operand;ctor.OpCode=OpCodes.Call;ctor.Operand=image.MainModule.ImportReference(typeof(MsiReplayIoPorts).GetMethod("Create")!);changed.Add(create.FullName);
        var request=host.Methods.Single(m=>m.Name=="RequestPowerProfileWatchdogTick");var originalRequest=FactoryMethodCanon(request);var arm=request.Body.Instructions.Single(i=>i.Operand is MethodReference m&&(m.DeclaringType.FullName=="System.Threading.Timer"&&m.Name=="Change"||m.DeclaringType.FullName=="MsiManualIoPorts"&&m.Name=="Arm"));var armCode=arm.OpCode;var armOperand=arm.Operand;arm.OpCode=OpCodes.Call;arm.Operand=image.MainModule.ImportReference(typeof(MsiReplayIoPorts).GetMethod("Arm")!);changed.Add(request.FullName);
        var power=host.Methods.Single(m=>m.Name=="IsPowerLineOnline");RequireReviewedMsiPowerIoTerminal(power, allowManualPort: true);power.Body=new Mono.Cecil.Cil.MethodBody(power);var il=power.Body.GetILProcessor();il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(MsiReplayIoPorts).GetMethod("Online")!)));il.Append(il.Create(OpCodes.Ret));changed.Add(power.FullName);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var actual=FactoryChangedMethods(args[0],args[1]);Check(actual.Order().SequenceEqual(changed.Order()),"Replay adds only Timer creation/arming and physical power read endpoints");ctor.OpCode=OpCodes.Newobj;ctor.Operand=originalCtor;arm.OpCode=armCode;arm.Operand=armOperand;Check(FactoryMethodCanon(create)==originalCreate&&FactoryMethodCanon(request)==originalRequest,"Restoring two endpoint operands restores complete original timer creation/request algorithms");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=actual,originalTimerCallbacksAndReplayConsumersUnchanged=true,restoredEndpointOperandsEqualOriginalBodies=true,originalBclTimerCreatedUnarmed=true,threadPoolCallbackExecutionRequiredByConsumerProof=true,physicalPowerReadIsolated=true,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
}
