using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;


static partial class Harness
{
    static int InstrumentConstructorLeaves(string[] args)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);resolver.AddSearchDirectory(Path.GetFullPath(args[3]));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});
        var modified=new List<string>();
        MethodReference Port(string name)=>image.MainModule.ImportReference(typeof(OriginalConstructorPorts).GetMethod(name)!);
        MethodDefinition Leaf(string type,string name)
        {
            var method=image.MainModule.GetType(type).Methods.Single(m=>m.Name==name);
            method.PInvokeInfo=null;method.IsPInvokeImpl=false;method.ImplAttributes=Mono.Cecil.MethodImplAttributes.IL|Mono.Cecil.MethodImplAttributes.Managed;
            method.Body=new Mono.Cecil.Cil.MethodBody(method);modified.Add(method.FullName);return method;
        }
        void Mark(ILProcessor il,string stage){il.Append(il.Create(OpCodes.Ldstr,stage));il.Append(il.Create(OpCodes.Call,Port("Mark")));}
        var board=Leaf("HandheldCompanion.MotherboardInfo","queryCacheValue");var code=board.Body.GetILProcessor();code.Append(code.Create(OpCodes.Ldarg_0));code.Append(code.Create(OpCodes.Ldarg_1));code.Append(code.Create(OpCodes.Call,Port("Board")));code.Append(code.Create(OpCodes.Ret));
        var boardInit=Leaf("HandheldCompanion.MotherboardInfo",".cctor");code=boardInit.Body.GetILProcessor();Mark(code,"motherboard-metadata-composition");
        code.Append(code.Create(OpCodes.Newobj,image.MainModule.ImportReference(typeof(object).GetConstructor(Type.EmptyTypes)!)));code.Append(code.Create(OpCodes.Stsfld,boardInit.DeclaringType.Fields.Single(f=>f.Name=="cacheLock")));
        var cache=boardInit.DeclaringType.Fields.Single(f=>f.Name=="cache");var cacheCtor=new MethodReference(".ctor",image.MainModule.TypeSystem.Void,cache.FieldType){HasThis=true};code.Append(code.Create(OpCodes.Newobj,cacheCtor));code.Append(code.Create(OpCodes.Stsfld,cache));
        code.Append(code.Create(OpCodes.Call,Port("MetadataCacheRoot")));code.Append(code.Create(OpCodes.Stsfld,boardInit.DeclaringType.Fields.Single(f=>f.Name=="cacheDirectory")));code.Append(code.Create(OpCodes.Ret));
        var factory=Leaf("HandheldCompanion.Managers.ManagerFactory",".cctor");code=factory.Body.GetILProcessor();Mark(code,"manager-composition");
        var settings=image.MainModule.GetType("HandheldCompanion.Managers.SettingsManager");code.Append(code.Create(OpCodes.Newobj,settings.Methods.Single(m=>m.IsConstructor&&!m.IsStatic&&m.Parameters.Count==0)));code.Append(code.Create(OpCodes.Stsfld,factory.DeclaringType.Fields.Single(f=>f.Name=="settingsManager")));
        var hotkeys=image.MainModule.GetType("HandheldCompanion.Managers.HotkeysManager");code.Append(code.Create(OpCodes.Newobj,hotkeys.Methods.Single(m=>m.IsConstructor&&!m.IsStatic&&m.Parameters.Count==0)));code.Append(code.Create(OpCodes.Stsfld,factory.DeclaringType.Fields.Single(f=>f.Name=="hotkeysManager")));code.Append(code.Create(OpCodes.Ret));
        var setting=Leaf("HandheldCompanion.Managers.SettingsManager","GetProperty");code=setting.Body.GetILProcessor();code.Append(code.Create(OpCodes.Ldarg_1));code.Append(code.Create(OpCodes.Call,Port("Setting")));code.Append(code.Create(OpCodes.Ret));
        var oneX=Leaf("HandheldCompanion.Devices.OneXPlayerX1","CheckIsBatteryProtectionSupported");code=oneX.Body.GetILProcessor();code.Append(code.Create(OpCodes.Ldarg_0));code.Append(code.Create(OpCodes.Call,Port("Major")));code.Append(code.Create(OpCodes.Call,Port("Minor")));code.Append(code.Create(OpCodes.Callvirt,oneX.DeclaringType.Methods.Single(m=>m.Name=="IsBatteryProtectionSupported")));code.Append(code.Create(OpCodes.Ret));
        var steamDeck=Leaf("HandheldCompanion.Devices.SteamDeck","Open");code=steamDeck.Body.GetILProcessor();Mark(code,"steamdeck-device-open-refused");code.Append(code.Create(OpCodes.Ldc_I4_0));code.Append(code.Create(OpCodes.Ret));
        var memory=Leaf("HandheldCompanion.Devices.IDevice","GetPhysicallyInstalledSystemMemory");code=memory.Body.GetILProcessor();code.Append(code.Create(OpCodes.Ldarg_0));code.Append(code.Create(OpCodes.Call,Port("PhysicalMemory")));code.Append(code.Create(OpCodes.Ret));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);
        using(var written=AssemblyDefinition.ReadAssembly(args[1]))foreach(var name in modified){var method=written.MainModule.Types.SelectMany(t=>t.Methods).Single(m=>m.FullName==name);Check(method.HasBody&&!method.IsPInvokeImpl&&!method.IsInternalCall,"Each declared managed constructor leaf has an actual IL body after rewrite");}
        var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Order().SequenceEqual(modified.Order()),"Exactly seven explicit constructor IO/composition leaves change; every constructor body remains original");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),instrumentedSha256=Hash(args[1]),changedMethods=changed,allDeviceInstanceConstructorBodiesUnchanged=true,gamepadMotionManagedAndNativeAbiUnchanged=true,hardwareIo=false,nativeMathOnly=true},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }
    sealed class ConstructorContext:AssemblyLoadContext
    {
        readonly string runtime,shared,hc,math;
        readonly Dictionary<string,string> pins;
        public readonly List<object> Bindings=new();public int RejectedNative,MathRequests;public IntPtr MathHandle;
        public ConstructorContext(string runtime,string shared,string hc,Dictionary<string,string> pins):base("hc-original-constructors-"+Guid.NewGuid().ToString("N"),isCollectible:false)
        {this.runtime=runtime;this.shared=shared;this.hc=hc;this.pins=pins;math=Path.Combine(runtime,"GamepadMotion.dll");ToolLoadAuthority.Current.RegisterKnownContext("ConstructorContext",this);}
        public Assembly Open()
        {
            Pin(hc);var assembly=ToolLoadAuthority.Current.LoadPath(this,hc);Guard(assembly);return assembly;
        }
        void Pin(string file){Check(pins.TryGetValue(Path.GetFullPath(file),out var expected)&&Hash(file)==expected,"Constructor runtime dependency must match its explicit predeclared pin: "+file);}
        protected override Assembly? Load(AssemblyName name)
        {
            if(name.Name==typeof(OriginalConstructorPorts).Assembly.GetName().Name)return ToolLoadAuthority.Current.RequireExistingExport(this,name,typeof(OriginalConstructorPorts).Assembly);
            foreach(var root in new[]{runtime,shared})
            {
                var file=Path.Combine(root,name.Name+".dll");if(!File.Exists(file))continue;Pin(file);
                var actual=AssemblyName.GetAssemblyName(file);Check(actual.Name==name.Name&&actual.Version==name.Version&&actual.GetPublicKeyToken()!.SequenceEqual(name.GetPublicKeyToken()!),"Exact original constructor managed dependency identity");
                var assembly=ToolLoadAuthority.Current.LoadRequestedPath(this,name,file);Guard(assembly);Bindings.Add(new{assembly=actual.FullName,path=file,sha256=Hash(file)});return assembly;
            }
            return ToolLoadAuthority.Current.ResolveOrReject(this,name);
        }
        void Guard(Assembly assembly)=>NativeLibrary.SetDllImportResolver(assembly,(name,_,_)=>Native(name));
        IntPtr Native(string name)
        {
            if(!string.Equals(name,"GamepadMotion.dll",StringComparison.OrdinalIgnoreCase)){RejectedNative++;throw new InvalidOperationException("CONSTRUCTOR_NATIVE_REFUSED:"+name);}
            Pin(math);MathRequests++;if(MathHandle==IntPtr.Zero)MathHandle=NativeLibrary.Load(math);return MathHandle;
        }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate void DeleteMotion(IntPtr handle);
        public int ForcedCleanup;
        public void ReleaseFixtureMotion(IntPtr handle){Check(MathHandle!=IntPtr.Zero,"Owned native math library remains available for fault cleanup");Marshal.GetDelegateForFunctionPointer<DeleteMotion>(NativeLibrary.GetExport(MathHandle,"DeleteGamepadMotion"))(handle);ForcedCleanup++;}
        protected override IntPtr LoadUnmanagedDll(string name)=>Native(name);
    }
    static void ConstructorDispose(object device)
    {
        if(Get(device,"GamepadMotion")is IDisposable motion)motion.Dispose();
        if(Get(device,"DeviceHotkeys")is IDictionary hotkeys)foreach(DictionaryEntry item in hotkeys)if(item.Value is IDisposable disposable)disposable.Dispose();
    }
    static int RunOriginalConstructors(string[] args)
    {
        // hc-copy, runtime, shared, inventory, pins, output, selection
        var hcFile=Path.GetFullPath(args[0]);var runtime=Path.GetFullPath(args[1]);var shared=Path.GetFullPath(args[2]);var output=Path.GetFullPath(args[5]);
        var root=Path.GetDirectoryName(output)!;Check(!File.Exists(output)&&root.Split(Path.DirectorySeparatorChar).Any(p=>p.Equals("Build",StringComparison.OrdinalIgnoreCase)),"Fresh Build constructor evidence required");Directory.CreateDirectory(root);
        var pins=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(args[4]))!;
        var context=new ConstructorContext(runtime,shared,hcFile,pins);var assembly=context.Open();
        var app=assembly.GetType("HandheldCompanion.App",true)!;var settings=Path.Combine(root,"constructor-owned-settings");Directory.CreateDirectory(settings);OriginalConstructorPorts.SettingsRoot=settings;
        foreach(var (name,value)in new[]{("SettingsPath",settings),("LogsPath",Path.Combine(settings,"logs")),("InstallPath",runtime),("GameControllerDbPath",Path.Combine(runtime,"gamecontrollerdb.txt"))})app.GetField(name,All)!.SetValue(null,value);
        using var inventory=JsonDocument.Parse(File.ReadAllText(args[3]));
        var selected=inventory.RootElement.GetProperty("rows").EnumerateArray().Where(r=>!r.GetProperty("inactive").GetBoolean()).Select(r=>r.GetProperty("type").GetString()!).ToArray();
        if(args[6]!="all")selected=selected.Where(n=>n.EndsWith("."+args[6],StringComparison.Ordinal)).ToArray();Check(selected.Length>0,"Nonempty original constructor selection");
        using var catalog=JsonDocument.Parse(File.ReadAllText(args[7]));
        var catalogRows=catalog.RootElement.GetProperty("devices").EnumerateArray().ToDictionary(row=>row.GetProperty("runtimeType").GetString()!);
        var capabilityType=assembly.GetType("HandheldCompanion.Devices.DeviceCapabilities",true)!;
        ushort Mask(JsonElement row,string bound){ushort result=0;foreach(var name in row.GetProperty("declared").GetProperty("Capabilities").GetProperty(bound).EnumerateArray())result|=Convert.ToUInt16(Enum.Parse(capabilityType,name.GetString()!));return result;}
        var scenarios=selected.Select(name=>(Name:name,Id:name,Major:1,Minor:1,Processor:"AMD Ryzen 9 395",Flags:(ushort?)null,Clock:(uint?)null)).ToList();
        if(args[6]=="all")
        {
            foreach(var (model,major,minor,flags) in new[]{("OneXPlayerX1AMD",1,2,56),("OneXPlayerX1AMD",1,3,504),("OneXPlayerX1AMD",2,0,56),("OneXPlayerX1Intel",0,66,56),("OneXPlayerX1Intel",0,67,504)})
            {var name="HandheldCompanion.Devices."+model;scenarios.Add((name,name+"@firmware-"+major+"."+minor,major,minor,"AMD Ryzen 9 395",(ushort)flags,null));}
            foreach(var (processor,clock)in new[]{("AMD Ryzen AI Max 385",5000u),("AMD Ryzen AI Max 395",5100u)})
            {var name="HandheldCompanion.Devices.GPDWin5";scenarios.Add((name,name+"@cpu-"+clock,1,1,processor,4,clock));}
        }
        var rows=new List<object>();
        foreach(var scenario in scenarios)
        {
            var name=scenario.Name;var before=Checks;object? device=null;OriginalConstructorPorts.Calls.Clear();
            OriginalConstructorPorts.Processor=scenario.Processor;OriginalConstructorPorts.BiosMajor=scenario.Major;OriginalConstructorPorts.BiosMinor=scenario.Minor;
            try
            {
                device=Activator.CreateInstance(assembly.GetType(name,true)!)!;
                Check(device.GetType().FullName==name,"Actual original constructor returns the selected exact model");
                Check(Get(device,"GamepadMotion")is not null,"Original base constructor must retain its motion owner");
                Check(catalogRows.TryGetValue(name,out var declared),"Every actual constructor has a current catalog identity");
                var flags=Convert.ToUInt16(Get(device,"Capabilities"));var minimum=Mask(declared,"minimum");var possible=Mask(declared,"possible");
                Check((flags&minimum)==minimum&&(flags&~possible)==0,"Original constructor capability bits must satisfy the independently declared model bounds: "+name);
                var motion=Get(device,"GamepadMotion")!;var handle=(IntPtr)Get(motion,"handle")!;
                Check(handle!=IntPtr.Zero,"Actual original native math constructor allocates a real software handle");
                Check((string)Get(motion,"deviceInstanceId")=="device_generic","Original base constructor ordering preserves its own initial illustration key");
                Check(Get(device,"DeviceHotkeys")is IDictionary hotkeys&&hotkeys.Count>=4,"Original base/default hotkey constructors create real command/input objects");
                var hotkeyValues=((IDictionary)Get(device,"DeviceHotkeys")!).Values.Cast<object>().ToArray();var buttonType=assembly.GetType("HandheldCompanion.Inputs.ButtonFlags",true)!;
                var expectedHotkeyFlags=Enumerable.Range(0,4).Select(i=>Convert.ToUInt64(Enum.Parse(buttonType,"HOTKEY_RESERVED"+i))).Order().ToArray();
                Check(hotkeyValues.Select(value=>Convert.ToUInt64(Get(value,"ButtonFlags"))).Order().SequenceEqual(expectedHotkeyFlags),"Original constructors preserve the four reserved default hotkey flags");
                Check(Get(device,"DeviceOpen")is false,"Original constructor must not activate device ownership");
                var settingsManager=assembly.GetType("HandheldCompanion.Managers.ManagerFactory",true)!.GetField("settingsManager",All)!.GetValue(null)!;
                Check((Get(settingsManager,"SettingValueChanged")as Delegate)?.GetInvocationList().Length==1,"Exactly the original desktop command owns its settings subscription");
                Check(Get(device,"DevicePowerProfiles")is IList profiles&&profiles.Count>=1,"Original base/model constructors retain their original power-profile metadata");
                Check(Get(device,"GyroMatrix")is not null&&Get(device,"AcceleroMatrix")is not null,"Original constructors initialize actual motion matrices");
                if(scenario.Flags is ushort expectedFlags)Check(flags==expectedFlags,"Original virtual firmware predicate determines exact constructor battery bits");
                if(scenario.Clock is uint expectedClock)Check(Convert.ToUInt32(Get(device,"CpuClock"))==expectedClock,"Original CPU identity branch determines exact model metadata");
                var snapshot=new{type=name,biosMajor=scenario.Major,biosMinor=scenario.Minor,processor=scenario.Processor,capabilities=Convert.ToUInt16(Get(device,"Capabilities")),illustration=Get(device,"ProductIllustration"),model=Get(device,"ProductModel"),cpu=Get(device,"CpuClock"),profileCount=((IList)Get(device,"DevicePowerProfiles")!).Count,hotkeyCount=((IDictionary)Get(device,"DeviceHotkeys")!).Count,ports=OriginalConstructorPorts.Calls.ToArray()};
                ConstructorDispose(device);Check((IntPtr)Get(motion,"handle")! ==IntPtr.Zero,"Actual original managed/native motion Dispose releases its owner handle");Check((Get(settingsManager,"SettingValueChanged")as Delegate)?.GetInvocationList().Length is null or 0,"Original hotkey disposal releases its settings subscription");device=null;
                Check(context.RejectedNative==0,"No constructor may attempt unapproved hardware/native access");
                var manager=assembly.GetType("HandheldCompanion.Managers.ManagerFactory",true)!;
                foreach(var field in manager.GetFields(All).Where(f=>f.IsStatic&&f.FieldType.FullName?.StartsWith("HandheldCompanion.Managers.")==true))
                    if(field.GetValue(null)is object value)Check(Get(value,"Status")?.ToString()=="None","Constructor composition cannot start an HC manager");
                rows.Add(new{id="original-constructor:"+scenario.Id,status="PASS",checks=Checks-before,snapshot});
            }
            catch(Exception error){rows.Add(new{id="original-constructor:"+scenario.Id,status="FAIL",checks=Checks-before,error=error.ToString(),ports=OriginalConstructorPorts.Calls.ToArray()});}
            finally{if(device is not null){var motion=Get(device,"GamepadMotion");ConstructorDispose(device);if(motion is not null&&Get(motion,"handle")is IntPtr handle&&handle!=IntPtr.Zero){context.ReleaseFixtureMotion(handle);Set(motion,"handle",IntPtr.Zero);}}}
        }
        Candidate=Load(Path.GetFullPath(args[8]));
        var backend=System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(Candidate.GetType("YeManFanHost.RealHcBackend",true)!);
        foreach(var kind in new[]{"valid","missing","invalid"})
        {
            var before=Checks;
            try{
                var folder=Path.Combine(root,"native-admission",kind);Directory.CreateDirectory(folder);var file=Path.Combine(folder,"GamepadMotion.dll");
                if(kind=="valid")File.Copy(Path.Combine(runtime,"GamepadMotion.dll"),file);
                if(kind=="invalid")File.WriteAllText(file,"INERT-NOT-A-NATIVE-LIBRARY");
                Set(backend,"hcRuntimeDirectory",folder);
                if(kind=="valid"){Invoke(backend,"ValidateGamepadMotion");Check(Hash(file)==Hash(Path.Combine(runtime,"GamepadMotion.dll")),"Actual Host admits the exact original native math dependency");}
                else Reject(()=>Invoke(backend,"ValidateGamepadMotion"),kind=="missing"?"GamepadMotion.dll not found":"GamepadMotion.dll could not load");
                rows.Add(new{id="native-admission:"+kind,status="PASS",checks=Checks-before});
            }catch(Exception error){rows.Add(new{id="native-admission:"+kind,status="FAIL",checks=Checks-before,error=error.ToString()});}
        }
        var failed=rows.Count(r=>JsonSerializer.Serialize(r).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(output,JsonSerializer.Serialize(new{status=failed==0?"PASS":"FAIL",checks=Checks,cases=rows,hcSha256=Hash(hcFile),originalDeviceConstructorBodiesExecuted=true,managerStartCalled=false,actualDeviceOpenBodyExecuted=false,hostMainStarted=false,hardwareIo=false,nativeMathCodeExecuted=context.MathRequests>0,nativeMathRequests=context.MathRequests,nativeMathFaultCleanup=context.ForcedCleanup,actualHostNativeAdmissionExecuted=true,candidateHostSha256=Hash(args[8]),rejectedNativeRequests=context.RejectedNative,bindings=context.Bindings,settingsRoot=settings},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"Original constructors={rows.Count}, failed={failed}, checks={Checks}, nativeMathRequests={context.MathRequests}");return failed==0?0:2;
    }
    static int MutateOriginalConstructor(string[] args,string mode)
    {
        using var resolver=new DefaultAssemblyResolver();resolver.AddSearchDirectory(Path.GetFullPath(args[3]));resolver.AddSearchDirectory(Path.GetFullPath(args[4]));
        using var image=AssemblyDefinition.ReadAssembly(args[0],new ReaderParameters{AssemblyResolver=resolver});
        var type=image.MainModule.GetType(mode=="capabilities"?"HandheldCompanion.Devices.ROGAlly":mode=="hotkeys"?"HandheldCompanion.Devices.IDevice":mode=="dispose"?"HandheldCompanion.Helpers.GamepadMotion":"YeManFanHost.RealHcBackend");
        var method=type.Methods.Single(m=>m.Name==(mode is "capabilities" or "hotkeys"?".ctor":mode=="dispose"?"Dispose":"ValidateGamepadMotion")&&m.Parameters.Count==0);
        var il=method.Body.GetILProcessor();
        if(mode is "dispose" or "native")
        {
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();il.Append(il.Create(OpCodes.Ret));
        }
        else
        {
            var ret=method.Body.Instructions.Last();Check(ret.OpCode==OpCodes.Ret,"Exact original constructor completion site");
            if(mode=="capabilities")
            {
                var flags=image.MainModule.GetType("HandheldCompanion.Devices.IDevice").Fields.Single(f=>f.Name=="Capabilities");
                foreach(var instruction in new[]{il.Create(OpCodes.Ldarg_0),il.Create(OpCodes.Ldarg_0),il.Create(OpCodes.Ldfld,flags),il.Create(OpCodes.Ldc_I4,~4),il.Create(OpCodes.And),il.Create(OpCodes.Stfld,flags)})il.InsertBefore(ret,instruction);
            }
            else
            {
                var setter=method.Body.Instructions.First(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName=="HandheldCompanion.Hotkey"&&call.Name=="set_ButtonFlags");
                var flag=setter.Previous;Check(flag.OpCode.Name.StartsWith("ldc.i4",StringComparison.Ordinal),"Exact original first default hotkey flag constant");flag.OpCode=OpCodes.Ldc_I4_0;flag.Operand=null;
            }
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);
        var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Original constructor/private native fault changes exactly its single selected method");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,mode,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }

}
