using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
static partial class Harness
{
    static int InstrumentLifecycleHc(string[] args)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetFullPath(args[3]));resolver.AddSearchDirectory(Path.GetFullPath(args[4]));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});var changed=new List<string>();
        MethodReference Port(string name)=>image.MainModule.ImportReference(typeof(LifecycleIoPorts).GetMethod(name)!);
        void Forward(string name,string port)
        {
            var method=image.MainModule.GetType("HandheldCompanion.OpenLibSys").Methods.Single(m=>m.Name==name);method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();for(var i=0;i<=method.Parameters.Count;i++)il.Append(il.Create(i==0?OpCodes.Ldarg_0:i==1?OpCodes.Ldarg_1:OpCodes.Ldarg_2));il.Append(il.Create(OpCodes.Call,Port(port)));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);
        }
        Forward("InitializeOls","Initialize");Forward("Dispose","Dispose");Forward("EnterSuperIoConfig","Enter");Forward("WriteIoPortByte","Write");Forward("ExitSuperIoConfig","Exit");
        foreach(var(type,name,stage)in new[]{("HandheldCompanion.Devices.OneXPlayerX1","Open","general-open"),("HandheldCompanion.Devices.OneXPlayerX1","Close","general-close"),("HandheldCompanion.Devices.OneXPlayerX1Mini","IsReady","mini-led-hid-readiness"),("HandheldCompanion.Devices.IDevice","OpenEvents","general-device-events")})
        {
            var method=image.MainModule.GetType(type).Methods.Single(m=>m.Name==name);method.Body=new Mono.Cecil.Cil.MethodBody(method);var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldstr,stage));il.Append(il.Create(OpCodes.Call,Port("Forbidden")));if(method.ReturnType.FullName=="System.Boolean")il.Append(il.Create(OpCodes.Ldc_I4_0));il.Append(il.Create(OpCodes.Ret));changed.Add(method.FullName);
        }
        var factory=image.MainModule.GetType("HandheldCompanion.Managers.ManagerFactory");var cctor=factory.Methods.Single(m=>m.Name==".cctor");var body=cctor.Body.GetILProcessor();var ret=cctor.Body.Instructions.Last();Check(ret.OpCode==OpCodes.Ret,"Existing constructor-only manager composition must complete once");
        var pp=image.MainModule.GetType("HandheldCompanion.Managers.PowerProfileManager");body.InsertBefore(ret,body.Create(OpCodes.Newobj,pp.Methods.Single(m=>m.IsConstructor&&!m.IsStatic&&m.Parameters.Count==0)));body.InsertBefore(ret,body.Create(OpCodes.Stsfld,factory.Fields.Single(f=>f.Name=="powerProfileManager")));
        var dm=image.MainModule.GetType("HandheldCompanion.Managers.DeviceManager");body.InsertBefore(ret,body.Create(OpCodes.Ldtoken,dm));body.InsertBefore(ret,body.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(Type).GetMethod("GetTypeFromHandle")!)));body.InsertBefore(ret,body.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(System.Runtime.CompilerServices.RuntimeHelpers).GetMethod("GetUninitializedObject")!)));body.InsertBefore(ret,body.Create(OpCodes.Castclass,dm));body.InsertBefore(ret,body.Create(OpCodes.Stsfld,factory.Fields.Single(f=>f.Name=="deviceManager")));changed.Add(cctor.FullName);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var actual=FactoryChangedMethods(args[0],args[1]);Check(actual.Order().SequenceEqual(changed.Order()),"Lifecycle HC changes exactly five transport IO leaves, four mixed-resource traps and one manager composition; original constructors/EC writer remain intact");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=actual,allOriginalDeviceConstructorsAndEcByteWriterUnchanged=true,originalPowerProfileManagerConstructor=true,deviceManagerStatusOnlyUninitializedFixture=true,managerStartCalled=false,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    static int MutateLifecycle(string[] args,string mode)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);var type=image.MainModule.GetType("YeManFanHost.OneXFanDeviceLifecycle");MethodDefinition method;
        if(mode=="family")
        {
            method=type.Methods.Single(m=>m.Name==".cctor");var slot=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldstr&&i.Operand?.ToString()=="HandheldCompanion.Devices.OneXPlayerX1Mini");slot.Operand="private-fault-unadmitted-mini";
        }
        else if(mode is "readiness" or "events")
        {
            var host=image.MainModule.GetType("YeManFanHost.RealHcBackend");method=host.Methods.Single(m=>m.Name==(mode=="readiness"?"WaitForHcDeviceReadyBeforeOpen":"OpenEventsBeginCore"));var il=method.Body.GetILProcessor();
            var slot=mode=="readiness"?method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.Name=="OneXFanDeviceLifecycle"&&m.Name=="IsFamily"):method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldfld&&i.Operand is FieldReference f&&f.Name=="oneXFanLifecycle"&&i.Next.OpCode.FlowControl==FlowControl.Cond_Branch);
            slot.OpCode=OpCodes.Pop;slot.Operand=null;il.InsertAfter(slot,mode=="readiness"?il.Create(OpCodes.Ldc_I4_0):il.Create(OpCodes.Ldnull));
        }
        else
        {
            method=type.Methods.Single(m=>m.Name==(mode=="write-result"?"WriteFanByte":"CloseCore"));var il=method.Body.GetILProcessor();
            if(mode=="repeat-release")
            {
                var slot=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldfld&&i.Operand is FieldReference f&&f.Name=="releaseReturned");slot.OpCode=OpCodes.Pop;slot.Operand=null;il.InsertAfter(slot,il.Create(OpCodes.Ldc_I4_0));
            }
            else
            {
                var slot=mode=="write-result"?method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Throw&&i.Previous.Operand is MethodReference m&&m.DeclaringType.FullName=="YeManFanHost.FanApiException"):method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Throw&&i.Previous.Operand is MethodReference m&&m.Name=="Contract"&&i.Previous.Previous.Operand?.ToString()?.StartsWith("EC fan automatic return was rejected",StringComparison.Ordinal)==true);slot.OpCode=OpCodes.Pop;slot.Operand=null;
            }
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Lifecycle fault changes exactly its named real consumer method");File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",mode,sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,hardwareIo=false}));return 0;
    }

}
