using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.ExceptionServices;
using System.Runtime.Loader;
using System.Collections;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows.Media;
using YeManFanBackend.Capabilities;

// Actual product decision/action functions with explicit memory-only device terminals.
internal static class LightingCatalogHarness
{
    private static readonly List<object> checks = [];
    private static Type ProductProgram = typeof(Program);
    private static readonly List<string> targetCalls = [];
    private static readonly List<string> dependencyAttempts = [];
    private static readonly List<string> nativeAttempts = [];
    private static int checkedCalls;
    private static object? InvokeTarget(Type type,string method,params object?[] args) {
        targetCalls.Add(type.FullName+"."+method);
        try { return type.GetMethod(method, BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,args); }
        catch(TargetInvocationException e) when(e.InnerException is not null) { ExceptionDispatchInfo.Capture(e.GetBaseException()).Throw();throw; }
    }
    private static Type Catalog => ProductProgram.Assembly.GetType("YeManFanBackend.Capabilities.HcLightingCapabilityCatalog",true)!;
    private static object? Prop(object? value,string name)=>value?.GetType().GetProperty(name)!.GetValue(value);
    private static string? ActualId(string alias)=>(string?)Prop(Prop(InvokeTarget(Catalog,"Resolve",alias),"Device"),"DeviceId");
    private static int ActualCount() { targetCalls.Add(Catalog.FullName+".Bindings");return ((IEnumerable)Catalog.GetProperty("Bindings")!.GetValue(null)!).Cast<object>().Count(); }
    private static bool ActualBrightness(string id)=>(bool)Prop(Prop(InvokeTarget(Catalog,"Resolve",id),"Consumer"),"Brightness")!;
    private static bool ActualSourceBrightness(string id) {
        var t=ProductProgram.Assembly.GetType("YeManFanBackend.Capabilities.HcDeviceCapabilityCatalog",true)!;
        return (Convert.ToUInt64(Prop(InvokeTarget(t,"Find",id),"DeclaredMinimum"))&8UL)!=0;
    }
    private static readonly List<string> failures = [];
    private static object? Call(string method, params object?[] args)
    {
        try { return InvokeTarget(ProductProgram,method,args); }
        catch (TargetInvocationException e) when (e.InnerException is not null) { ExceptionDispatchInfo.Capture(e.GetBaseException()).Throw(); throw; }
    }
    private static void Check(string id, bool ok) { checks.Add(new { id, ok, actualTargetCalls=targetCalls.Count-checkedCalls }); checkedCalls=targetCalls.Count; if (!ok) failures.Add(id); }
    private static bool Reject(Action call) { try { call(); return false; } catch (Exception e) when (e is NotSupportedException or ArgumentException or InvalidOperationException) { return true; } }
    private static string Execute(RecordingLightingState device, string? expected, string action, string? level="solid", int? brightness=null, string? preset=null) =>
        (string)Call("ExecuteCatalogLedAction", device, device.GetType(), expected, action, level, "off", 10,20,30,40,50,60,brightness,75,preset)!;
    private static string PureOff(RecordingLightingState device) => (string)Call("ExecuteLedAction", device, device.GetType(), "off", "solid", "off", null,null,null,null,null,null,null,null)!;
    private static Type MemoryDevice(ModuleBuilder module, HcCapabilityBinding<LightingCapabilityDescriptor> binding, bool skipBrightness=false, bool forceStatus=false)
    {
        var type=module.DefineType(binding.Device.FactoryType, TypeAttributes.Public, typeof(HandheldCompanion.Devices.IDevice));
        type.DefineDefaultConstructor(MethodAttributes.Public);
        if(binding.Device.ConsumerClass=="HandheldCompanion.Devices.OneXPlayerX1") {
            type.DefineField("LEDEnabled",typeof(bool),FieldAttributes.Public);
            type.DefineField("LEDBrightness",typeof(int),FieldAttributes.Public);
            type.DefineField("LEDPreset",typeof(object),FieldAttributes.Public);
        }
        void Override(string api,string recorder,Type[] parameters) {
            var method=type.DefineMethod(api,MethodAttributes.Public|MethodAttributes.Virtual,typeof(bool),parameters);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);
            for(int i=0;i<parameters.Length;i++)il.Emit(OpCodes.Ldarg,i+1);
            il.Emit(OpCodes.Call,typeof(RecordingLightingState).GetMethod(recorder)!);il.Emit(OpCodes.Ret);
            type.DefineMethodOverride(method,typeof(HandheldCompanion.Devices.IDevice).GetMethod(api)!);
        }
        Override("SetLedColor","RecordColor",[typeof(Color),typeof(Color),typeof(MemoryLedLevel),typeof(int)]);
        if(binding.Consumer.Brightness&&!skipBrightness)Override("SetLedBrightness","RecordBrightness",[typeof(int)]);
        if(binding.Consumer.StatusAvailable||binding.Consumer.StatusTransportDisabled||forceStatus)Override("SetLedStatus","RecordStatus",[typeof(bool)]);
        if(binding.Consumer.PresetAvailable)Override("SetLEDPreset","RecordPreset",[typeof(MemoryPreset)]);
        return type.CreateType()!;
    }
    private static void CheckApex(JsonDocument source)
    {
        const string id="hc.device.OneXPlayerApex",cls="HandheldCompanion.Devices.OneXPlayerApex",x1="HandheldCompanion.Devices.OneXPlayerX1";
        var devices=source.RootElement.GetProperty("devices").EnumerateArray().ToArray();
        var frozen=devices.Single(d=>d.GetProperty("deviceId").GetString()==id);
        var lighting=source.RootElement.GetProperty("domains").GetProperty("lighting");
        var row=lighting.GetProperty("rows").EnumerateArray().Single(r=>r.GetProperty("deviceId").GetString()==id);
        var old=row.GetProperty("frozenConsumer");
        var rule=lighting.GetProperty("reviewedConsumerOverrides").EnumerateArray().Single();
        var binding=InvokeTarget(Catalog,"Resolve",id);
        var descriptor=Prop(binding,"Device");var consumer=Prop(binding,"Consumer");
        var sourceFlags=(HcSourceCapability)Convert.ToUInt64(Prop(descriptor,"DeclaredMinimum"));
        var sourceEffects=(HcSourceLedEffect)Convert.ToUInt64(Prop(descriptor,"DeclaredLedMinimum"));
        Check("APEX-frozen-source-and-declared-facts-retained",
            frozen.GetProperty("sourcePath").GetString()=="deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerApex.cs"&&
            frozen.GetProperty("sourceSha256").GetString()=="c64af412f599b43d337be93f9860fac30cc85a578c5bed727d84ddfc02237e60"&&
            (string?)Prop(descriptor,"SourcePath")==frozen.GetProperty("sourcePath").GetString()&&
            (string?)Prop(descriptor,"SourceSha256")==frozen.GetProperty("sourceSha256").GetString()&&
            sourceFlags.HasFlag(HcSourceCapability.DynamicLighting|HcSourceCapability.DynamicLightingBrightness|HcSourceCapability.DynamicLightingSecondLEDColor)&&
            sourceEffects==(HcSourceLedEffect.SolidColor|HcSourceLedEffect.LEDPreset)&&
            frozen.GetProperty("declared").GetProperty("DynamicLightingCapabilities").GetProperty("minimum").EnumerateArray().Select(e=>e.GetString()).SequenceEqual(new[]{"LEDPreset","SolidColor"}));
        Check("APEX-reviewed-override-keeps-registration-count",
            ActualCount()==47&&lighting.GetProperty("rows").GetArrayLength()==47&&
            lighting.GetProperty("extensionRows").GetArrayLength()==4&&devices.Length==92&&
            rule.GetProperty("deviceId").GetString()==id&&rule.GetProperty("sourceAdapter").GetString()=="onex-apex-hid-v2"&&
            !(bool)InvokeTarget(Catalog,"IsReviewedExtension",cls)!);
        bool Flag(string name)=>(bool)Prop(consumer,name)!;
        Check("APEX-exact-effective-descriptor-and-adapter",
            Flag("Brightness")&&!Flag("SecondColor")&&!Flag("PresetDeclared")&&!Flag("PresetAvailable")&&!Flag("StatusAvailable")&&
            Flag("StatusTransportDisabled")&&Flag("FixedSpeed")&&!Flag("IndependentCore")&&
            (HcSourceLedEffect)Convert.ToUInt64(Prop(consumer,"DeclaredEffects"))==(HcSourceLedEffect.SolidColor|HcSourceLedEffect.Rainbow)&&
            Prop(consumer,"SourceAdapter")?.ToString()=="ApexHidV2"&&row.GetProperty("sourceAdapter").GetString()=="onex-apex-hid-v2"&&
            row.GetProperty("sourceDeclaredSecondColor").GetBoolean()&&!row.GetProperty("consumerSecondColor").GetBoolean()&&
            row.GetProperty("declaredEffects").EnumerateArray().Select(e=>e.GetString()).SequenceEqual(new[]{"solid","rainbow"}));
        foreach(var name in new[]{"brightness","secondColor","presetDeclared","presetAvailable","statusAvailable","fixedSpeed","offAvailable"})
            Check("APEX-effective-fact-matches-reviewed-override:"+name,row.GetProperty(name).GetBoolean()==rule.GetProperty(name).GetBoolean());
        Check("APEX-frozen-X1-consumer-provenance-retained",
            old.GetProperty("brightness").GetBoolean()&&old.GetProperty("secondColor").GetBoolean()&&
            old.GetProperty("presetDeclared").GetBoolean()&&old.GetProperty("presetAvailable").GetBoolean()&&old.GetProperty("statusAvailable").GetBoolean()&&
            !old.GetProperty("statusTransportDisabled").GetBoolean()&&
            old.GetProperty("declaredEffects").EnumerateArray().Select(e=>e.GetString()).SequenceEqual(new[]{"solid","preset"}));
        var x1Source=devices.Single(d=>d.GetProperty("name").GetString()=="OneXPlayerX1");
        foreach(var name in new[]{"SetLedColor","SetLedBrightness","SetLedStatus","SetLEDPreset"})
        {
            var method=row.GetProperty("methodSources").GetProperty(name);var original=old.GetProperty("methodSources").GetProperty(name);
            var declaration=x1Source.GetProperty("methods").EnumerateArray().Single(m=>m.GetProperty("name").GetString()==name);
            Check("APEX-frozen-method-owner-and-hash:"+name,method.GetProperty("owner").GetString()=="OneXPlayerX1"&&original.GetProperty("owner").GetString()=="OneXPlayerX1"&&
                method.GetProperty("sourceSha256").GetString()==original.GetProperty("sourceSha256").GetString()&&method.GetProperty("sourceSha256").GetString()==declaration.GetProperty("sourceSha256").GetString());
        }
        foreach(var alias in frozen.GetProperty("aliases").EnumerateArray().Select(a=>a.GetString()!))
        {
            Check("APEX-canonical-same-device-id:"+alias,ActualId(alias)==id&&(bool)InvokeTarget(Catalog,"IdentityMatches",alias,cls)!&&(bool)InvokeTarget(Catalog,"IdentityMatches",cls,alias)!);
            foreach(var expected in new[]{x1,"hc.device.OneXPlayerX1","HandheldCompanion.Devices.OneXPlayer.OneXPlayerX1"})
            {
                Check("APEX-actualAPEX-expectedX1-identity-rejected:"+alias+":"+expected,!(bool)InvokeTarget(Catalog,"IdentityMatches",alias,expected)!);
                Check("APEX-actualAPEX-expectedX1-Require-rejected:"+alias+":"+expected,Reject(()=>{_=InvokeTarget(Catalog,"Require",alias,expected,"color","solid",false,null);}));
            }
        }
        foreach(var child in new[]{"OneXPlayerX1AMD","OneXPlayerX1Intel","OneXPlayerX1Mini","OneXPlayerX1Pro"})
            Check("X1-other-child-identity-retained:"+child,(bool)InvokeTarget(Catalog,"IdentityMatches","HandheldCompanion.Devices."+child,x1)!);
        foreach(var action in new[]{"status","preset","unknown"})
            Check("APEX-unimplemented-action-denied:"+action,Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,action,"solid",false,"LEDPreset_OneXPlayerX1_Preset09");}));
        foreach(var effect in new[]{"breathing","wave","wheel","gradient","ambilight","unreviewed-effect"})
            Check("APEX-unreviewed-effect-denied:"+effect,Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,"color",effect,false,null);}));

        // Parent ApexLightingTests owns exhaustive failures. These four cases connect
        // this catalog's exact Require result to the linked real consumer at memory IO.
        // Also compare its final report with the actual selected helper's wire method.
        var targetConsumer=ProductProgram.Assembly.GetType("ApexLightingConsumer",true)!;
        foreach(var (action,effect,brightness) in new (string,string,int?)[]{("color","solid",60),("color","rainbow",60),("brightness","solid",45),("off","solid",null)})
        {
            var operation=InvokeTarget(Catalog,"Require",cls,cls,action,effect,brightness is not null,null);
            Check("APEX-exact-provider-and-operation:"+action+":"+effect,
                (string?)Prop(operation,"PreferredProvider")=="onex-apex-hid-v2"&&Prop(Prop(operation,"Capabilities"),"SourceAdapter")?.ToString()=="ApexHidV2"&&
                (string?)Prop(Prop(operation,"Device"),"DeviceId")==id&&(string?)Prop(operation,"Action")==action&&(string?)Prop(operation,"Level")==effect);
            var terminal=new ApexMemoryTerminal(@"\\?\hid#vid_1a2c&pid_b001#catalog-memory-only");
            var identity=ApexLightingConsumer.OwnerIdentity(terminal.DevicePath,7);var opens=0;var waits=new List<int>();
            var request=ApexLightingRequest.Create((string)Prop(operation,"Action")!,(string)Prop(operation,"Level")!,10,20,30,brightness);
            var result=ApexLightingConsumer.Apply(identity,request,()=>true,default,()=>{opens++;return terminal;},
                (ms,stop)=>{stop.ThrowIfCancellationRequested();waits.Add(ms);});
            var count=action=="color"?2:1;
            Check("APEX-real-consumer-memory-completion:"+action+":"+effect,result.Completed&&result.Released&&result.Writes==count&&
                result.Reason=="reports-completed-effect-unverified"&&opens==1&&terminal.Disposed&&terminal.Reports.Count==count&&terminal.CancelCalls==0&&
                waits.SequenceEqual(count==2?new[]{50}:Array.Empty<int>()));
            Check("APEX-Windows-report65-ID0:"+action+":"+effect,terminal.Reports.All(r=>r.Length==65&&r[0]==0&&r[1]==7&&r[2]==255));
            var bright=terminal.Reports[0];
            Check("APEX-brightness-and-disabled-off:"+action+":"+effect,bright.Take(7).SequenceEqual(action=="off"?new byte[]{0,7,255,253,0,5,1}:new byte[]{0,7,255,253,1,5,3})&&bright.Skip(7).All(b=>b==0));
            if(action=="color")
            {
                var color=terminal.Reports[1];
                Check("APEX-real-color-wire:"+effect,effect=="solid"?
                    color[3]==254&&color[64]==0&&Enumerable.Range(0,20).All(i=>color[4+i*3]==10&&color[5+i*3]==20&&color[6+i*3]==30):
                    color[3]==3&&color.Skip(4).All(b=>b==0));
            }
            var targetWire=(byte[])(action=="color"?
                effect=="solid"?InvokeTarget(targetConsumer,"BuildSolid",(byte)10,(byte)20,(byte)30):InvokeTarget(targetConsumer,"BuildRainbow"):
                InvokeTarget(targetConsumer,"BuildBrightness",action=="off"?0:brightness!.Value))!;
            Check("APEX-selected-helper-wire-matches-real-memory-consumer:"+action+":"+effect,targetWire.SequenceEqual(terminal.Reports[^1]));
        }
    }
    private sealed class ApexMemoryTerminal(string path) : IApexLightingTransport
    {
        public string DevicePath { get; }=path;
        public bool IsConnected=>!Disposed;
        internal bool Disposed { get; private set; }
        internal int CancelCalls { get; private set; }
        internal List<byte[]> Reports { get; }=[];
        public ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report,CancellationToken stop)
        { stop.ThrowIfCancellationRequested();Reports.Add(report.ToArray());return ValueTask.FromResult(report.Length); }
        public void CancelPendingIo()=>CancelCalls++;
        public void Dispose()=>Disposed=true;
    }
    public static int Main(string[] args)
    {
        if(args.Length is <2 or >5 || args.Length is 3 or 5 && args[^1] is not ("--loki-only" or "--fixed-speed-only"))throw new ArgumentException("catalog.json result.json [helper.dll expected-sha256] [--loki-only|--fixed-speed-only]");
        if(args.Length>=4) {
        var helper=Path.GetFullPath(args[2]);
        if(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(helper))).ToLowerInvariant()!=args[3])throw new InvalidOperationException("Exact helper identity mismatch");
        AssemblyLoadContext.Default.Resolving += (_,name)=> {dependencyAttempts.Add(name.FullName);throw new InvalidOperationException("Unexpected target dependency "+name);};
        AssemblyLoadContext.Default.ResolvingUnmanagedDll += (_,name)=> {nativeAttempts.Add(name);throw new InvalidOperationException("Native target load refused "+name);};
        ProductProgram=AssemblyLoadContext.Default.LoadFromAssemblyPath(helper).GetType("Program",true)!;
        }
        using var source=JsonDocument.Parse(File.ReadAllBytes(args[0]));
        if(args.Length is 3 or 5 && args[^1]=="--fixed-speed-only")return FixedSpeedParity(source.RootElement,args[1]);
        var module=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("MemoryLightingDevices"),AssemblyBuilderAccess.Run).DefineDynamicModule("devices");
        // Only FrozenHc routes use the inherited SetLed* memory devices below.
        // Exact APEX is verified separately through Require and its real HID consumer.
        var frozenBindings=HcLightingCapabilityCatalog.Bindings.Where(b=>b.Consumer.SourceAdapter==LightingSourceAdapter.FrozenHc).ToArray();
        var types=frozenBindings.ToDictionary(b=>b.Device.DeviceId,b=>MemoryDevice(module,b));
        RecordingLightingState Device(string id)=> (RecordingLightingState)Activator.CreateInstance(types[id])!;
        var loki=Device("hc.device.LokiZero");
        try { Check("loki-off-reaches-black-color-writer",PureOff(loki)=="ok"&&loki.Calls.SequenceEqual(["color"])&&loki.Main==Colors.Black); }
        catch(Exception e){Check("loki-off-reaches-black-color-writer",false);failures.Add(e.GetBaseException().Message);}
        if(args.Length is 3 or 5&&args[^1]=="--loki-only")return Save(args[1],1);
        Check("47-registered-lighting-classes",ActualCount()==47);
        Check("46-frozen-HC-generic-consumers-and-one-exact-APEX",frozenBindings.Length==46&&
            HcLightingCapabilityCatalog.Bindings.Count(b=>b.Consumer.SourceAdapter==LightingSourceAdapter.ApexHidV2&&b.Device.DeviceId=="hc.device.OneXPlayerApex")==1);
        var rows=source.RootElement.GetProperty("domains").GetProperty("lighting").GetProperty("rows").EnumerateArray().ToDictionary(r=>r.GetProperty("deviceId").GetString()!);
        foreach(var b in frozenBindings)
        {
            var id=b.Device.DeviceId;var row=rows[id];
            foreach(var effect in row.GetProperty("declaredEffects").EnumerateArray().Select(e=>e.GetString()!).Where(e=>e!="preset")) {
                var d=Device(id);Check(id+":actual-color-consumer:"+effect,Execute(d,id,"color",effect,b.Consumer.Brightness?60:null)=="ok"&&d.Calls[0]=="color"&&!d.Calls.Any(c=>c.StartsWith("noop-")));
            }
            var off=Device(id);Check(id+":actual-off-consumer",Execute(off,id,"off")=="ok"&&off.Main==Colors.Black&&off.Calls.Count==1+(b.Consumer.Brightness?1:0)+(b.Consumer.StatusAvailable?1:0)&&!off.Calls.Any(c=>c.StartsWith("noop-")));
            var bright=Device(id);
            if(b.Consumer.Brightness)Check(id+":brightness-consumed",Execute(bright,id,"brightness",brightness:45)=="ok"&&bright.Brightness==45&&bright.Calls.SequenceEqual(["brightness"]));
            else Check(id+":unsupported-brightness-zero-io",Reject(()=>Execute(bright,id,"brightness",brightness:45))&&bright.Calls.Count==0);
            var status=Device(id);
            if(b.Consumer.StatusAvailable)Check(id+":status-consumed",Execute(status,id,"status")=="ok"&&status.Calls.SequenceEqual(["status"]));
            else Check(id+":base-status-zero-io",Reject(()=>Execute(status,id,"status"))&&status.Calls.Count==0);
            var bad=Device(id);Check(id+":unadvertised-effect-zero-io",Reject(()=>Execute(bad,id,"color","unreviewed-effect"))&&bad.Calls.Count==0);
            Check(id+":spoofed-expected-class-zero-io",Reject(()=>Execute(bad,"Unreviewed.Vendor."+b.Device.FactoryType.Split('.').Last(),"color"))&&bad.Calls.Count==0);
            if(b.Consumer.PresetDeclared) {
                var d=Device(id);d.Presets.AddRange(source.RootElement.GetProperty("domains").GetProperty("lighting").GetProperty("presets").EnumerateArray().Select(p=>new MemoryPreset(p.GetProperty("id").GetString()!,p.GetProperty("value").GetInt32())));
                const string preset="LEDPreset_OneXPlayerX1_Preset09";
                if(b.Consumer.PresetAvailable)Check(id+":named-preset-consumed",Execute(d,id,"preset",brightness:50,preset:preset)=="ok"&&d.Selected?.Value==0x1e3&&d.Calls.SequenceEqual(["brightness","preset"]));
                else Check(id+":declared-preset-without-transport-zero-io",Reject(()=>Execute(d,id,"preset",preset:preset))&&d.Calls.Count==0);
            }
        }
        CheckApex(source);
        foreach(var d in source.RootElement.GetProperty("devices").EnumerateArray())foreach(var alias in d.GetProperty("aliases").EnumerateArray().Select(a=>a.GetString()!))
            Check("exact-lighting-alias:"+alias,rows.ContainsKey(d.GetProperty("deviceId").GetString()!) ? ActualId(alias)==d.GetProperty("deviceId").GetString() : ActualId(alias)is null);
        foreach(var name in new[]{"LokiZero","ROGAlly","ClawA1M"}) {
            var id="hc.device."+name;var d=Device(id);d.ColorOk=false;
            Check(id+":failed-callback-not-success",Execute(d,id,"color")=="setLedColor-false"&&(int)Call("ExitCodeForNote","setLedColor-false")! !=0);
            var off=Device(id);off.ColorOk=false;Check(id+":failed-off-not-success",Execute(off,id,"off")=="off-partial");
            var throwing=Device(id);throwing.ThrowColor=true;Check(id+":throwing-consumer-propagates",Reject(()=>Execute(throwing,id,"color"))&&throwing.Calls.Count==1);
        }
        Check("Mini-serial-status-is-not-advertised",Reject(()=>InvokeTarget(Catalog,"Require","hc.device.OneXPlayerX1Mini",null,"status",null,false,null)));
        Check("reviewed-Loki-native-base-alias",(bool)InvokeTarget(Catalog,"IdentityMatches","hc.device.LokiMax6800U","hc.device.AynLoki")!);
        Check("unreviewed-cross-model-alias-refused",!(bool)InvokeTarget(Catalog,"IdentityMatches","hc.device.ROGAllyX","hc.device.ROGAlly")!);
        Check("MSI-real-brightness-preserved-without-source-flag",ActualBrightness("hc.device.ClawA1M")&&!ActualSourceBrightness("hc.device.ClawA1M"));
        var drift=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("MemoryLightingRuntimeDrift"),AssemblyBuilderAccess.Run).DefineDynamicModule("drift");
        var missing=(RecordingLightingState)Activator.CreateInstance(MemoryDevice(drift,HcLightingCapabilityCatalog.Resolve("hc.device.ROGAlly")!,skipBrightness:true))!;
        Check("off-runtime-missing-writer-zero-io",Reject(()=>Execute(missing,"hc.device.ROGAlly","off"))&&missing.Calls.Count==0);
        var extra=(RecordingLightingState)Activator.CreateInstance(MemoryDevice(drift,HcLightingCapabilityCatalog.Resolve("hc.device.LokiZero")!,forceStatus:true))!;
        Check("off-runtime-extra-writer-zero-io",Reject(()=>Execute(extra,"hc.device.LokiZero","off"))&&extra.Calls.Count==0);
        Check("reviewed-OXP3-extension-resolves",ActualId("HandheldCompanion.Devices.OneXPlayer3")=="ymcc.device.OneXPlayer3");
        Check("frozen-bindings-remain-47",ActualCount()==47);
        Check("OXP3-solid-allowed",!Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"color","solid",false,null);}));
        Check("OXP3-breathing-allowed",!Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"color","breathing",false,null);}));
        Check("OXP3-rainbow-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"color","rainbow",false,null);}));
        Check("OXP3-brightness-allowed",!Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"brightness",null,true,null);}));
        Check("OXP3-status-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"status",null,false,null);}));
        Check("OXP3-preset09-allowed",!Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"preset",null,false,"LEDPreset_OneXPlayerX1_Preset09");}));
        Check("OXP3-preset08-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"preset",null,false,"LEDPreset_OneXPlayerX1_Preset08");}));
        Check("OXP3-identity-exact",(bool)InvokeTarget(Catalog,"IdentityMatches","HandheldCompanion.Devices.OneXPlayer3","HandheldCompanion.Devices.OneXPlayer3")!);
        Check("OXP3-foreign-identity-denied",!(bool)InvokeTarget(Catalog,"IdentityMatches","HandheldCompanion.Devices.OneXPlayer3","HandheldCompanion.Devices.ROGAlly")!);
        Check("OXP3-is-reviewed-extension",(bool)InvokeTarget(Catalog,"IsReviewedExtension","HandheldCompanion.Devices.OneXPlayer3")!);
        Check("unknown-class-not-extension",!(bool)InvokeTarget(Catalog,"IsReviewedExtension","Unreviewed.Vendor.OneX")!);
        var presetArgs=new object?[]{"ymcc.device.OneXPlayer3","LEDPreset_OneXPlayerX1_Preset09",0};
        Check("OXP3-classic-preset-value",(bool)InvokeTarget(Catalog,"TryResolveExtensionPreset",presetArgs)!&&Convert.ToInt32(presetArgs[2])==1000);
        var missingPresetArgs=new object?[]{"ymcc.device.OneXPlayer3","LEDPreset_OneXPlayerX1_Preset08",0};
        Check("OXP3-missing-preset-denied",!(bool)InvokeTarget(Catalog,"TryResolveExtensionPreset",missingPresetArgs)!);
        Check("OXP3-route-not-independent",((string?)Prop(InvokeTarget(Catalog,"Require","HandheldCompanion.Devices.OneXPlayer3",null,"color","solid",false,null),"PreferredProvider"))=="hc-device-core");
        foreach(var model in new[]{"AYANEO3","AYANEONEXT2"})
        {
            var cls="HandheldCompanion.Devices."+model;
            var operation=InvokeTarget(Catalog,"Require",cls,cls,"color","solid",true,null);
            Check(model+"-typed-provider-authority",(string?)Prop(operation,"PreferredProvider")=="superjoy-cec-v1" &&
                Prop(Prop(operation,"Capabilities"),"SourceAdapter")?.ToString()=="SuperJoyCec");
            Check(model+"-reviewed-exact-source-identity",ActualId(cls)=="ymcc.device."+model && (bool)InvokeTarget(Catalog,"IsReviewedExtension",cls)!);
            Check(model+"-brightness-available",!Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,"brightness",null,true,null);}));
            Check(model+"-status-available",!Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,"status",null,false,null);}));
            Check(model+"-rainbow-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,"color","rainbow",true,null);}));
            Check(model+"-preset-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,cls,"preset",null,false,"LEDPreset_OneXPlayerX1_Preset01");}));
            Check(model+"-foreign-expectation-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require",cls,"HandheldCompanion.Devices.ROGAlly","color","solid",true,null);}));
            Check(model+"-nearby-unreviewed-name-denied",Reject(()=>{_=InvokeTarget(Catalog,"Require",cls+"Unknown",cls+"Unknown","color","solid",true,null);}));
        }
        object FakeRows(params (string Id,string Class)[] items)
        {
            var hcType=Catalog.Assembly.GetType("YeManFanBackend.Capabilities.HcDeviceDescriptor",true)!;
            var consumerType=Catalog.Assembly.GetType("YeManFanBackend.Capabilities.LightingCapabilityDescriptor",true)!;
            var capabilityType=Catalog.Assembly.GetType("YeManFanBackend.Capabilities.HcSourceCapability",true)!;
            var effectType=Catalog.Assembly.GetType("YeManFanBackend.Capabilities.HcSourceLedEffect",true)!;
            var tupleType=typeof(ValueTuple<,>).MakeGenericType(hcType,consumerType);
            var array=Array.CreateInstance(tupleType,items.Length);
            foreach(var (index,item) in items.Select((value,index)=>(index,value)))
            {
                var capability=Enum.Parse(capabilityType,"DynamicLighting");
                var effect=Enum.Parse(effectType,"SolidColor");
                var device=Activator.CreateInstance(hcType,new object?[]{item.Id,item.Class,item.Class,"Fake","fake/source",new string('a',64),false,capability,capability,effect,effect});
                var consumer=Activator.CreateInstance(consumerType,new object?[]{item.Id,true,false,effect,false,false,false,false,false});
                array.SetValue(Activator.CreateInstance(tupleType,device,consumer),index);
            }
            return array;
        }
        var singleMap=InvokeTarget(Catalog,"BuildExtensionRoutes",FakeRows(("ymcc.device.FakeA","HandheldCompanion.Devices.FakeA")));
        var containsKey=singleMap?.GetType().GetMethod("ContainsKey")!;
        var count=singleMap?.GetType().GetProperty("Count")!.GetValue(singleMap) as int?;
        Check("C01-same-device-alias-deduplicated",(bool)containsKey.Invoke(singleMap,new object[]{"ymcc.device.FakeA"})!&&(bool)containsKey.Invoke(singleMap,new object[]{"HandheldCompanion.Devices.FakeA"})!&&count==2);
        Check("C01-cross-device-conflict-rejected",Reject(()=>{_=InvokeTarget(Catalog,"BuildExtensionRoutes",FakeRows(("ymcc.device.FakeA","HandheldCompanion.Devices.FakeA"),("ymcc.device.FakeB","HandheldCompanion.Devices.FakeA")));}));
        Check("HC-not-loaded",!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"));
        return Save(args[1],HcLightingCapabilityCatalog.Bindings.Count);
    }
    private static int FixedSpeedParity(JsonElement source, string output)
    {
        var lighting=source.GetProperty("domains").GetProperty("lighting");
        var rows=lighting.GetProperty("rows").EnumerateArray().Concat(lighting.GetProperty("extensionRows").EnumerateArray()).ToArray();
        Check("fixed-speed-source-rows-unique",rows.Length>0&&rows.Select(r=>r.GetProperty("deviceId").GetString()).Distinct().Count()==rows.Length);
        var requiredFixed=new[]{"hc.device.OneXPlayerG1","hc.device.OneXPlayerG1AMD","hc.device.OneXPlayerG1Intel",
            "hc.device.OneXPlayerOneXFly","hc.device.OneXPlayerOneXFlyF1Pro","hc.device.OneXPlayerX1Mini",
            "hc.device.OneXPlayerApex","ymcc.device.OneXPlayerX2MiniPro","ymcc.device.OneXPlayer3"};
        foreach(var id in requiredFixed)
            Check("fixed-speed-source-consumer-evidence:"+id,rows.Count(r=>r.GetProperty("deviceId").GetString()==id&&r.GetProperty("fixedSpeed").GetBoolean())==1);
        foreach(var row in rows)
        {
            var id=row.GetProperty("deviceId").GetString()!;
            var consumer=Prop(InvokeTarget(Catalog,"Resolve",row.GetProperty("deviceClass").GetString()),"Consumer");
            Check("fixed-speed-managed-native-parity:"+id,consumer is not null&&Prop(consumer,"FixedSpeed") is bool actual&&actual==(row.TryGetProperty("fixedSpeed",out var flag)&&flag.GetBoolean()));
        }
        Check("fixed-speed-no-HC-load",!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"));
        return Save(output,rows.Length);
    }
    private static int Save(string file,int models) {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(file))!);
        var result=new{status=failures.Count==0?"PASS":"FAIL",checks=checks.Count,models,failures,hardwareIo=false,HCAssemblyLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),productionEntryPointInvoked=false,assemblySha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(ProductProgram.Assembly.Location))).ToLowerInvariant(),assemblyPath=ProductProgram.Assembly.Location,artifactMode=ProductProgram.Assembly!=typeof(LightingCatalogHarness).Assembly,targetCalls,dependencyAttempts,nativeAttempts,cases=checks};
        using(var stream=new FileStream(file,FileMode.CreateNew,FileAccess.Write,FileShare.None))
        using(var writer=new StreamWriter(stream,new System.Text.UTF8Encoding(false)))writer.Write(JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true})+"\n");
        Console.WriteLine(JsonSerializer.Serialize(new{result.status,result.checks,result.models,failures}));return failures.Count==0?0:1;
    }
}
public enum MemoryLedLevel { SolidColor=1,Breathing=2,Rainbow=4,Wave=8,Wheel=16,Gradient=32,Ambilight=64,LEDPreset=128 }
public sealed record MemoryPreset(string NameKey,int Value);
public class RecordingLightingState
{
    public List<string> Calls{get;}=[];public List<MemoryPreset> Presets{get;}=[];public Color Main;public int Brightness;public MemoryPreset? Selected;public bool ColorOk=true,ThrowColor;
    public bool RecordColor(Color a,Color b,MemoryLedLevel level,int speed){Calls.Add("color");Main=a;if(ThrowColor)throw new InvalidOperationException("memory color failure");return ColorOk;}
    public bool RecordBrightness(int value){Calls.Add("brightness");Brightness=value;return true;}
    public bool RecordStatus(bool value){Calls.Add("status");return true;}
    public bool RecordPreset(MemoryPreset value){Calls.Add("preset");Selected=value;return true;}
}
namespace HandheldCompanion.Devices
{
    // Exact name of the real base no-op contract; these methods have no hardware terminal.
    public class IDevice:RecordingLightingState
    {
        public List<MemoryPreset> LEDPresets=>Presets;
        public virtual bool SetLedColor(Color a,Color b,MemoryLedLevel level,int speed){Calls.Add("noop-color");return true;}
        public virtual bool SetLedBrightness(int value){Calls.Add("noop-brightness");return true;}
        public virtual bool SetLedStatus(bool value){Calls.Add("noop-status");return true;}
        public virtual bool SetLEDPreset(MemoryPreset value){Calls.Add("noop-preset");return true;}
    }
}
