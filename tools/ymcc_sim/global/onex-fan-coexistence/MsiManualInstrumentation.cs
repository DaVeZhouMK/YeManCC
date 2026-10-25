using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
static partial class Harness
{
    // Unknown complete method shapes require directed review before IO replacement.
    static void RequireReviewedMsiPowerIoTerminal(MethodDefinition method, bool allowManualPort = false, bool allowJoinedPort = false)
    {
        var canonical = FactoryMethodCanon(method);
        var sha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(canonical))).ToLowerInvariant();
        const string original = "9c24dfe458d3963f323238c8f073a9467aa74279dea51766c7a7c03d1bd40314";
        const string manualPort = "c1cf2fd33c10528734cc29e32826ae844b51151715059c930374b473ae02fee8";
        const string joinedPort = "641e6d254a73f7d42af64cc8abbab888f7187801e20a4c48b88f3d59db308423";
        if (sha256 != original && !(allowManualPort && sha256 == manualPort) && !(allowJoinedPort && sha256 == joinedPort))
            throw new InvalidOperationException("MSI_POWER_IO_SHAPE_REVIEW_REQUIRED: " + method.FullName + " canonical=" + sha256);
    }

    static int MsiManualConsumerInventory(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> rows)=>rows.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        var methods=Types(image.MainModule.Types).SelectMany(t=>t.Methods).Where(m=>m.HasBody&&(m.Name.Contains("ApplyMsiFamilyCurve",StringComparison.Ordinal)||m.Body.Instructions.Any(i=>i.Operand is MethodReference c&&c.Name=="MsiCurveCommandStillCurrent"))).Select(m=>new {method=m.FullName,il=m.Body.Instructions.Select(i=>new {offset=i.Offset,opcode=i.OpCode.Name,operand=i.Operand?.ToString()}).ToArray()}).ToArray();File.WriteAllText(args[1],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),methods,assembliesExecuted=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int MutateMsiManual(string[] args,string mode)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);foreach(var directory in args.Skip(3))resolver.AddSearchDirectory(Path.GetFullPath(directory));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> rows)=>rows.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        var types=Types(image.MainModule.Types).ToArray();MethodDefinition method;
        if(mode=="owner")
        {
            method=types.SelectMany(t=>t.Methods).Single(m=>m.HasBody&&m.Name.Contains("ApplyMsiFamilyCurve",StringComparison.Ordinal)&&m.Body.Instructions.Any(i=>i.Operand is MethodReference c&&c.Name=="MsiCurveCommandStillCurrent"));var branches=method.Body.Instructions.Where(i=>i.OpCode==OpCodes.Bne_Un_S||i.OpCode==OpCodes.Bne_Un).ToArray();Check(branches.Length==2,"Original compiled owner guard has exactly two reference mismatch branches");var il=method.Body.GetILProcessor();foreach(var i in branches){i.OpCode=OpCodes.Pop;i.Operand=null;il.InsertAfter(i,il.Create(OpCodes.Pop));}
        }
        else if(mode=="boundaries")
        {
            method=types.SelectMany(t=>t.Methods).Single(m=>m.Name.Contains("<Apply>g__CheckWriteBoundary",StringComparison.Ordinal));method.Body=new Mono.Cecil.Cil.MethodBody(method);method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Ret));
        }
        else if(mode=="table-status")
        {
            method=image.MainModule.GetType("YeManFanHost.MsiFamilyFanSubmission").Methods.Single(m=>m.Name=="get_TableStatus");method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,"ok"));il.Append(il.Create(OpCodes.Ret));
        }
        else if(mode=="preservation")
        {
            method=image.MainModule.GetType("HandheldCompanion.Devices.ClawA1M").Methods.Single(m=>m.Name=="WriteMsiFanTableBlock");var copy=method.Body.Instructions.Single(i=>i.Operand is MethodReference c&&c.DeclaringType.FullName=="System.Array"&&c.Name=="Copy");Check(Convert.ToInt32(copy.Previous.Operand)==31,"Original MSI preservation count must be exactly31");copy.Previous.OpCode=OpCodes.Ldc_I4_S;copy.Previous.Operand=(sbyte)8;
        }
        else if(mode=="fullspeed")
        {
            method=types.Where(t=>t.FullName=="YeManFanHost.MsiFamilyFanCurve"||t.FullName.StartsWith("YeManFanHost.MsiFamilyFanCurve/",StringComparison.Ordinal)).SelectMany(t=>t.Methods).Single(m=>m.HasBody&&m.Body.Instructions.Any(i=>i.Operand is MethodReference c&&c.Name=="get_DutyPercent")&&m.Body.Instructions.Any(i=>i.OpCode==OpCodes.Ldc_R8&&i.Operand is double d&&d==100));var constant=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldc_R8&&i.Operand is double d&&d==100);constant.Operand=101d;
        }
        else if(mode=="axis-endpoint")
        {
            method=image.MainModule.GetType("YeManFanHost.ReflectionMsiFanTransport").Methods.Single(m=>m.Name=="ReadTemperatureAxis");var literal=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldstr&&i.Operand?.ToString()=="Get_Temperature");literal.Operand="Get_Fan";
        }
        else throw new InvalidOperationException("Unknown manual MSI fault");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Each manual fault changes exactly one intended original consumer method");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",mode,sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int InstrumentMsiManualHost(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");var method=host.Methods.Single(m=>m.Name=="EnsurePowerProfileWatchdog");var original=FactoryMethodCanon(method);var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.Threading.Timer"&&m.Name=="Change");var op=call.OpCode;var operand=call.Operand;call.OpCode=OpCodes.Call;call.Operand=image.MainModule.ImportReference(typeof(MsiManualIoPorts).GetMethod("Arm")!);
        var request=host.Methods.Single(m=>m.Name=="RequestPowerProfileWatchdogTick");var requestOriginal=FactoryMethodCanon(request);var requestCall=request.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName=="System.Threading.Timer"&&m.Name=="Change");var requestCode=requestCall.OpCode;var requestOperand=requestCall.Operand;requestCall.OpCode=OpCodes.Call;requestCall.Operand=image.MainModule.ImportReference(typeof(MsiManualIoPorts).GetMethod("Arm")!);
        var power=host.Methods.Single(m=>m.Name=="IsPowerLineOnline");RequireReviewedMsiPowerIoTerminal(power, allowJoinedPort: true);power.Body=new Mono.Cecil.Cil.MethodBody(power);var powerIl=power.Body.GetILProcessor();powerIl.Append(powerIl.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(MsiManualIoPorts).GetMethod("Online")!)));powerIl.Append(powerIl.Create(OpCodes.Ret));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Order().SequenceEqual(new[]{method.FullName,request.FullName,power.FullName}.Order()),"Manual fixture changes exactly initial/request timer arms and physical power IO terminal");call.OpCode=op;call.Operand=operand;requestCall.OpCode=requestCode;requestCall.Operand=requestOperand;Check(FactoryMethodCanon(method)==original&&FactoryMethodCanon(request)==requestOriginal,"Restoring Timer.Change operands preserves complete original watchdog creation/request bodies");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=changed,onlyOriginalTimerArmOperandPorted=false,originalTimerArmOperandsAndPowerIoOnly=true,physicalPowerReadIsolated=true,actualPublicApplyCurveAndAllOriginalMsiWritersUnchanged=true,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
}
