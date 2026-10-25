using System.IO;
using System.Windows.Threading;
using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

static partial class JoinedFactoryProbe
{
    const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
    static string Root="",Runtime="",Shared="",HcPath="",HostPath="",AppBase="",BoundaryRoot="";
    static readonly List<object> BoundaryFiles=new();
    static readonly List<object> Cases=new();static readonly HashSet<string> Constructed=new();static readonly List<object> Bindings=new();
    static Dictionary<string,string> Pins=new(StringComparer.OrdinalIgnoreCase);
    static readonly List<object> NativeDenials=new();
    static Assembly Host=null!;static int Checks,DeniedNative,MathBindings;static IntPtr MathHandle;
    static bool PublicEntry,SawDispatcherPump,LifecycleMode,MsiLifecycleMode,MsiManualMode,MsiReplayMode;static int CallerThreadId,ForcedThreadCleanup;
    static readonly HashSet<object> PublicRequests=new();static readonly List<object> ThreadEvidence=new();
    static string Hash(string file)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    static void Check(bool value,string why){Interlocked.Increment(ref Checks);if(!value)throw new InvalidOperationException(why);}
    static object? Get(object value,string name)=>value.GetType().GetField(name,All)?.GetValue(value)??value.GetType().GetProperty(name,All)?.GetValue(value);
    static void Set(object value,string name,object? data){var field=value.GetType().GetField(name,All);if(field is not null)field.SetValue(value,data);else value.GetType().GetProperty(name,All)!.SetValue(value,data);}
    static object? Invoke(object value,string name,params object?[] args)
    {
        try{return value.GetType().GetMethods(All).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(value,args);}
        catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    static Exception Reject(Action action,string reason)
    {
        Exception? failure=null;try{action();}catch(Exception error){failure=error;}
        Check(failure is not null&&(Get(failure,"Code")?.ToString()==reason||failure.ToString().Contains(reason,StringComparison.OrdinalIgnoreCase)),"Joined consumer must refuse "+reason+"; got "+failure);return failure!;
    }
    static Assembly? Hc=>AssemblyLoadContext.Default.Assemblies.SingleOrDefault(a=>a.GetName().Name=="HandheldCompanion");
    static void Pin(string file){file=Path.GetFullPath(file);Check(Pins.TryGetValue(file,out var expected)&&Hash(file)==expected,"Joined dependency must match its explicit original pin: "+file);}
    static void Guard(Assembly assembly)
    {
        if(assembly.IsDynamic||string.IsNullOrEmpty(assembly.Location)||!Pins.ContainsKey(Path.GetFullPath(assembly.Location)))return;
        Pin(assembly.Location);NativeLibrary.SetDllImportResolver(assembly,(name,_,_)=>
        {
            if(!string.Equals(name,"GamepadMotion.dll",StringComparison.OrdinalIgnoreCase)){DeniedNative++;NativeDenials.Add(new{name,assembly=assembly.GetName().Name,stack=Environment.StackTrace});throw new InvalidOperationException("JOINED_NATIVE_REFUSED:"+name);}
            var file=Path.Combine(Runtime,"GamepadMotion.dll");Pin(file);MathBindings++;
            if(MathHandle==IntPtr.Zero)MathHandle=NativeLibrary.Load(file);return MathHandle;
        });
        Bindings.Add(new{assembly=assembly.GetName().FullName,path=assembly.Location,sha256=Hash(assembly.Location)});
    }
    static Assembly? Resolve(AssemblyLoadContext context,AssemblyName name)
    {
        if(name.Name==typeof(OriginalConstructorPorts).Assembly.GetName().Name)return ToolLoadAuthority.Current.RequireExistingExport(context,name,typeof(OriginalConstructorPorts).Assembly);
        foreach(var folder in new[]{Path.GetDirectoryName(HostPath)!,Runtime,Shared})
        {
            var file=Path.Combine(folder,name.Name+".dll");if(!File.Exists(file))continue;Pin(file);
            var identity=AssemblyName.GetAssemblyName(file);Check(identity.Name==name.Name&&(name.Version is null||identity.Version==name.Version)&&(name.GetPublicKeyToken()is not {Length:>0} token||identity.GetPublicKeyToken()!.SequenceEqual(token)),"Joined resolver preserves all requested identity fields and the exact predeclared pinned bytes");return ToolLoadAuthority.Current.LoadRequestedPath(context,name,file);
        }
        return ToolLoadAuthority.Current.ResolveOrReject(context,name);
    }
    static object Backend(string? diagnostic=null)
    {
        var options=System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(Host.GetType("YeManFanHost.HostOptions",true)!);
        Set(options,"AllowHardwareWrites",false);Set(options,"HcAssemblyPath",HcPath);Set(options,"HcRuntimeRoot",Runtime);Set(options,"SessionToken","INERT-JOINED-TOKEN");Set(options,"Confirmation","INERT-JOINED-TOKEN");
        if(diagnostic is not null)Set(options,"HcDeviceType",diagnostic);
        return Activator.CreateInstance(Host.GetType("YeManFanHost.RealHcBackend",true)!,All,null,[options],null)!;
    }
    static void DisposeDevice(object? device)
    {
        if(device is null)return;
        if(Get(device,"GamepadMotion")is IDisposable motion)motion.Dispose();
        if(Get(device,"DeviceHotkeys")is IDictionary hotkeys)foreach(DictionaryEntry pair in hotkeys)if(pair.Value is IDisposable value)value.Dispose();
    }
    static void Reset(string manufacturer="ASUSTeK COMPUTER INC.",string product="RC71L",string model="ROG Ally")
    {
        if(Hc is Assembly hc)hc.GetType("HandheldCompanion.Devices.IDevice",true)!.GetField("device",All)!.SetValue(null,null);
        OriginalConstructorPorts.Calls.Clear();OriginalConstructorPorts.Frames.Clear();OriginalConstructorPorts.OnMark=null;OriginalConstructorPorts.Failure="";OriginalConstructorPorts.Failures=0;
        OriginalConstructorPorts.Manufacturer=manufacturer;OriginalConstructorPorts.Product=product;OriginalConstructorPorts.Model=model;OriginalConstructorPorts.SystemName="joined-system";OriginalConstructorPorts.Version="joined-version";OriginalConstructorPorts.Processor="AMD Ryzen 9 395";
        OriginalConstructorPorts.SettingsRoot=Path.Combine(JoinedFactoryPorts.Root,"YeManFanHost-HC");OriginalConstructorPorts.Cores=8;OriginalConstructorPorts.BiosMajor=1;OriginalConstructorPorts.BiosMinor=1;JoinedFactoryPorts.UnsupportedSensors=false;
    }
    static OriginalConstructorPorts.StageFrame[] ObserveFrames()
    {
        var frames=OriginalConstructorPorts.Frames.ToArray();if(frames.Any(f=>f.Context=="System.Windows.Threading.DispatcherSynchronizationContext"))SawDispatcherPump=true;return frames;
    }
    static void Run(string id,Action action)
    {
        var before=Checks;
        try{Reset();action();Check(DeniedNative==0,"No joined consumer may attempt an unapproved native/device call");Cases.Add(new{id,status="PASS",checks=Checks-before,calls=OriginalConstructorPorts.Calls.ToArray(),stageFrames=ObserveFrames(),lifecycleIo=LifecycleMode||MsiLifecycleMode||MsiManualMode||MsiReplayMode?LifecycleIoPorts.Rows.ToArray():null,msiLifecycleIo=MsiLifecycleMode?MsiLifecycleIoPorts.Rows.ToArray():null,msiManualIo=MsiManualMode||MsiReplayMode?MsiManualIoPorts.Rows.ToArray():null,replayArms=MsiReplayMode?MsiReplayIoPorts.Arms.ToArray():null,replayFires=MsiReplayMode?MsiReplayIoPorts.Fires.ToArray():null});}
        catch(Exception error){Cases.Add(new{id,status="FAIL",checks=Checks-before,error=error.ToString(),calls=OriginalConstructorPorts.Calls.ToArray(),stageFrames=ObserveFrames(),lifecycleIo=LifecycleMode||MsiLifecycleMode||MsiManualMode||MsiReplayMode?LifecycleIoPorts.Rows.ToArray():null,msiLifecycleIo=MsiLifecycleMode?MsiLifecycleIoPorts.Rows.ToArray():null,msiManualIo=MsiManualMode||MsiReplayMode?MsiManualIoPorts.Rows.ToArray():null,replayArms=MsiReplayMode?MsiReplayIoPorts.Arms.ToArray():null,replayFires=MsiReplayMode?MsiReplayIoPorts.Fires.ToArray():null});}
        finally{OriginalConstructorPorts.OnMark=null;OriginalConstructorPorts.Failure="";OriginalConstructorPorts.Failures=0;}
    }
    static object Handshake(object backend){if(PublicEntry)PublicRequests.Add(backend);return Invoke(backend,PublicEntry?"LoadAndHandshake":"LoadAndHandshakeCore")!;}
    static void Ready(object backend,string expected,bool identity)
    {
        var handshake=Handshake(backend);var device=Get(backend,"device")!;Constructed.Add(device.GetType().FullName!);
        if(PublicEntry)
        {
            var thread=Get(backend,"staThread")as Thread;
            Check(thread is not null&&thread.IsAlive&&thread.ManagedThreadId!=CallerThreadId,"Public original factory must dispatch via its owned STA, not the caller");
            Check(!thread!.IsBackground&&thread.GetApartmentState()==ApartmentState.STA,"Public original factory preserves the foreground STA lifecycle owner");
            var staIdentity=Get(backend,"StaThreadIdentityForDiagnostics");Check(staIdentity is not null&&Convert.ToInt32(Get(staIdentity,"ManagedThreadId"))==thread.ManagedThreadId&&Convert.ToUInt32(Get(staIdentity,"OsThreadId"))>0,"Public factory publishes the actual same read-only OS thread identity");
            var frames=OriginalConstructorPorts.Frames.Where(f=>f.Stage!="sta.os-thread-id").ToArray();Check(frames.Length>0&&frames.All(f=>f.ManagedThreadId==thread.ManagedThreadId&&f.Apartment=="STA"&&f.Context=="System.Windows.Threading.DispatcherSynchronizationContext"),"Every original factory stage and constructor executes on the same actual message-pumped STA");
        }
        Check(Get(backend,"IsLoaded")is true&&Get(backend,"hcCoreInitialized")is true,"Joined real factory publishes readiness after all original stages");
        Check(Hc is not null&&ReferenceEquals(Hc,device.GetType().Assembly)&&Path.GetFullPath(Hc.Location)==HcPath&&Hash(Hc.Location)==Pins[HcPath],"Joined factory must load the actual pinned original-constructor HC copy, never the legacy fixture");
        Check(device.GetType().Name==expected,"Joined original factory constructs the independently expected model: "+expected);
        Check(Get(backend,"CanWrite")is false,"Joined read-only factory cannot grant write authorization");
        var flags=Convert.ToUInt16(Get(device,"Capabilities"));var observation=Invoke(backend,"ObserveFanCapability")!;
        Check(Get(observation,"Declared")is bool declared&&declared==((flags&4)!=0),"Host capability observation must preserve the actual original constructor's FanControl bit");
        Check(Convert.ToUInt16(Get(device,"Capabilities"))==flags,"Host capability evaluation cannot forge constructor capability bits");
        if(expected is "OneXPlayerX1AMD" or "OneXPlayerX1Intel" or "OneXPlayerX1Pro" or "OneXPlayerX1Mini" or "OneXPlayerApex")
            Check(Get(observation,"Supported")is true&&Get(observation,"Declared")is false&&Get(observation,"Evidence")?.ToString()=="hc-x1-inherited-ec-contract","Joined OneX original constructor must reach its exact inherited-EC capability decision without fabricating FanControl");
        if(expected is "DefaultDevice" or "ASUS" or "SteamDeck" or "OneXPlayerG1AMD")Check(Get(observation,"Supported")is false,"Joined unsupported original model must stay refused: "+expected);
        if(expected is "ROGAlly" or "ROGAllyX" or "XboxROGAlly" or "XboxROGAllyX")Check(Get(observation,"Supported")is true,"Joined declared original ASUS fan capability must remain supported");
        var actual=(Dictionary<string,string>)Get(backend,"DeviceIdentity")!;
        if(identity)Check(actual.GetValueOrDefault("ManufacturerName")==OriginalConstructorPorts.Manufacturer.ToUpper()&&actual.GetValueOrDefault("ProductName")==OriginalConstructorPorts.Product&&actual.GetValueOrDefault("SystemModel")==OriginalConstructorPorts.Model,"Joined identity must come from the actual original GetCurrent-populated fields");
        else Check(actual.Count==0,"Diagnostic constructor cannot invent motherboard identity fields");
        var app=Hc!.GetType("HandheldCompanion.App",true)!;Check((string)app.GetField("SettingsPath",All)!.GetValue(null)! ==OriginalConstructorPorts.SettingsRoot,"Actual ConfigureHcRuntime must set the owned Build settings path");
        Check(!OriginalConstructorPorts.Calls.Any(c=>c.StartsWith("setting-write:",StringComparison.Ordinal)),"Joined factory must preserve Initialize(false,false) without inactive settings writes");
        Check(!OriginalConstructorPorts.Calls.Contains("device.sensors"),"Fan-only factory must not enter HC motion-sensor discovery");
        var calls=OriginalConstructorPorts.Calls.Count;var current=Get(backend,"device");Handshake(backend);Check(OriginalConstructorPorts.Calls.Count==calls&&ReferenceEquals(current,Get(backend,"device")),"Ready joined factory preserves the same original instance and completed stage cache");
        Reject(()=>Invoke(backend,"EnsureRealWriteAuthorization"),"REAL_WRITE_AUTH_REQUIRED");Check(OriginalConstructorPorts.Calls.Count==calls,"Unauthorized write admission cannot enter device IO or restart bootstrap");
        Check(Get(device,"DeviceOpen")is false,"Actual original model stays unopened after handshake and refused write admission");
    }
    static void End(object backend)
    {
        var device=Get(backend,"device");if(device is not null)Constructed.Add(device.GetType().FullName!);
        var requested=PublicRequests.Remove(backend);var thread=Get(backend,"staThread")as Thread;
        try
        {
            ((IDisposable)backend).Dispose();
            if(PublicEntry&&requested)
            {
                Check(thread is not null,"Public original factory must dispatch via its owned STA before returning any admitted stage");
                var stopped=thread!.Join(TimeSpan.FromSeconds(2));
                if(!stopped){ForcedThreadCleanup++;var dispatcher=Dispatcher.FromThread(thread);if(dispatcher is not null)dispatcher.BeginInvokeShutdown(DispatcherPriority.Send);thread.Join(TimeSpan.FromSeconds(2));}
                ThreadEvidence.Add(new{managedThreadId=thread.ManagedThreadId,originalDisposeStoppedThread=stopped,allOwnedThreadsExited=!thread.IsAlive});
                Check(stopped,"Original public factory disposal must stop its own STA without fixture shutdown");Check(Get(backend,"StaThreadIdentityForDiagnostics")is null,"Retired public STA cannot publish its old OS identity");
            }
        }
        finally{DisposeDevice(device);}
    }
    static readonly (string vendor,string product,string model,string expected)[] Models=[
        ("ASUSTeK COMPUTER INC.","RC71L","unused","ROGAlly"),("ASUSTEK COMPUTER INC.","RC72LA","unused","ROGAllyX"),
        ("ASUSTEK COMPUTER INC.","RC73YA","unused","XboxROGAlly"),("ASUSTEK COMPUTER INC.","RC73XA","unused","XboxROGAllyX"),
        ("ASUSTEK COMPUTER INC.","unknown-asus","unused","ASUS"),
        ("AYANEO","FLIP DS","unused","AYANEOFlipDS"),("AYANEO","FLIP KB","unused","AYANEOFlipKB"),
        ("AYANEO","FLIP 1S DS","unused","AYANEOFlip1SDS"),("AYANEO","FLIP 1S KB","unused","AYANEOFlip1SKB"),
        ("ONE-NETBOOK","ONEXPLAYER X1 i","unused","OneXPlayerX1Intel"),("ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER X1 A","unused","OneXPlayerX1AMD"),
        ("ONE-NETBOOK","ONEXPLAYER X1z","unused","OneXPlayerX1AMD"),("ONE-NETBOOK","ONEXPLAYER X1Pro","unused","OneXPlayerX1Pro"),
        ("ONE-NETBOOK","ONEXPLAYER X1 mini","unused","OneXPlayerX1Mini"),("ONE-NETBOOK","ONEXPLAYER APEX","unused","OneXPlayerApex"),
        ("ONE-NETBOOK","ONEXPLAYER G1 i","unused","OneXPlayerG1Intel"),("ONE-NETBOOK","ONEXPLAYER G1 A","unused","OneXPlayerG1AMD"),
        ("Valve","Jupiter","unused","SteamDeck"),("Valve","Galileo","unused","SteamDeck"),
        ("LENOVO","different-product","83E1","LegionGoTablet"),("LENOVO","different-product","83N1","LegionGoTablet2"),
        ("LENOVO","different-product","83L3","LegionGoSZ2"),("LENOVO","different-product","83N6","LegionGoSZ1"),
        ("MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T42","unused","ClawA2VM"),("MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T91","unused","ClawCG3EM"),
        ("ZOTAC","G0A1W","unused","GamingZone"),("unknown-vendor","unknown-product","unknown-model","DefaultDevice")];
    static void RunMatrix(string inventory)
    {
        foreach(var row in Models)Run("joined-auto:"+row.expected+":"+row.product,()=>
        { Reset(row.vendor,row.product,row.model);var backend=Backend();try{Ready(backend,row.expected,true);}finally{End(backend);} });
        var rows=JsonNode.Parse(File.ReadAllText(inventory))!["rows"]!.AsArray();
        foreach(var row in rows.Where(r=>r!["inactive"]!.GetValue<bool>()==false))
        {
            var type=row!["type"]!.GetValue<string>();var nested=type[..type.LastIndexOf('.') ]!="HandheldCompanion.Devices";
            Run((nested?"joined-diagnostic-refused:":"joined-diagnostic:")+type,()=>
            {
                var backend=Backend(type);try
                {
                    if(!nested){Ready(backend,type[(type.LastIndexOf('.')+1)..],false);return;}
                    Reject(()=>Handshake(backend),"outside the approved device profile");
                    Check(Get(backend,"device")is null&&Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Nested diagnostic type cannot bypass the original profile admission");
                    Check(!OriginalConstructorPorts.Calls.Contains("device.sensors"),"Refused nested diagnostics cannot enter sensor discovery");
                }finally{End(backend);}
            });
        }
        Run("joined-all-admitted-models-loaded",()=>Check(rows.Where(r=>r!["inactive"]!.GetValue<bool>()==false).All(r=>Constructed.Contains(r!["type"]!.GetValue<string>())),"Every admitted original constructor must reach the joined Host through diagnostic or automatic production route"));
    }
    static void RunFailures()
    {
        foreach(var kind in new[]{"missing","invalid","long-valid","long-missing","long-invalid"})Run("joined-native:"+kind,()=>
        {
            var runtime=Path.Combine(Root,"native-admission",kind);
            if(kind.StartsWith("long-",StringComparison.Ordinal))while(runtime.Length<300)runtime=Path.Combine(runtime,"owned-native-path-segment");
            Directory.CreateDirectory(runtime);
            if(kind=="long-valid")File.Copy(Path.Combine(Runtime,"GamepadMotion.dll"),Path.Combine(runtime,"GamepadMotion.dll"));
            if(kind is "invalid" or "long-invalid")File.WriteAllText(Path.Combine(runtime,"GamepadMotion.dll"),"INERT-NOT-A-NATIVE-LIBRARY");
            var backend=Backend();Set(Get(backend,"options")!,"HcRuntimeRoot",runtime);try
            {
                if(kind=="long-valid")
                {
                    Check(Hash(Path.Combine(runtime,"GamepadMotion.dll"))==Pins[Path.Combine(Runtime,"GamepadMotion.dll")],"Long-path validation must load the exact pinned software math bytes");
                    // Some CLR loader paths normalize >=260 automatically. Reproduce the
                    // observed259-character boundary explicitly instead of assuming monotonic failure.
                    var segment=new string('x',259-BoundaryRoot.Length-2-"GamepadMotion.dll".Length);
                    var boundary=Path.Combine(BoundaryRoot,segment);Directory.CreateDirectory(boundary);var file=Path.Combine(boundary,"GamepadMotion.dll");File.Copy(Path.Combine(Runtime,"GamepadMotion.dll"),file);
                    Check(file.Length==259,"Native loader regression must exercise the exact observed259-character file path");BoundaryFiles.Add(new{path=file,sha256=Hash(file)});
                    foreach(var folder in new[]{boundary,@"\\?\"+boundary,runtime,@"\\?\"+runtime})
                    {
                        Set(Get(backend,"options")!,"HcRuntimeRoot",folder);Ready(backend,"ROGAlly",true);End(backend);Reset();backend=Backend();
                    }
                    return;
                }
                Reject(()=>Handshake(backend),kind.EndsWith("missing",StringComparison.Ordinal)?"GamepadMotion.dll not found":"GamepadMotion.dll could not load");
                Check(Get(backend,"assembly")is null&&Get(backend,"device")is null&&Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Missing or invalid native math must stop the actual joined factory before HC assembly loading and device construction");
                Check(!OriginalConstructorPorts.Calls.Contains("runtime.temp-root"),"Native rejection must happen before original runtime configuration");
            }finally{End(backend);}
        });
        Run("joined-platform-unsupported-sensors",()=>
        {
            JoinedFactoryPorts.UnsupportedSensors=true;var backend=Backend();try
            {
                Ready(backend,"ROGAlly",true);
                Check(JoinedFactoryPorts.UnsupportedSensors&&Get(backend,"HcCoreWarning")is null,"Fan-only initialization must leave a configured unsupported sensor endpoint uncalled, without a fabricated skip warning");
            }finally{End(backend);}
        });
        Run("joined-sensor-failure-not-called",()=>
        {
            OriginalConstructorPorts.Failure="device.sensors";OriginalConstructorPorts.Failures=1;var backend=Backend();try
            {
                Ready(backend,"ROGAlly",true);
                Check(OriginalConstructorPorts.Failures==1&&!OriginalConstructorPorts.Calls.Contains("device.sensors"),"Fan initialization must succeed while the configured throwing sensor endpoint remains unused");
                Check(Get(backend,"hcDeviceInitializeReturned")is true&&Get(backend,"HcCoreWarning")is null,"Skipping motion discovery must preserve real Initialize completion without a sensor warning");
            }finally{End(backend);}
        });
        foreach(var stage in new[]{"validation.acl","validation.payload","runtime.temp-root","board:baseboard:Manufacturer"})Run("joined-retry:"+stage,()=>
        {
            OriginalConstructorPorts.Failure=stage;OriginalConstructorPorts.Failures=1;var backend=Backend();try
            {
                Reject(()=>Handshake(backend),"JOINED_PORT_FAILURE:"+stage);
                Check(Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Failed original factory stage cannot publish readiness or write authorization");
                var device=Get(backend,"device");var configured=Get(backend,"hcRuntimeConfigured")is true;var calls=OriginalConstructorPorts.Calls.Count(c=>c=="runtime.temp-root");
                Ready(backend,"ROGAlly",true);
                if(device is not null)Check(ReferenceEquals(device,Get(backend,"device")),"Joined retry preserves the original constructed instance");
                if(configured)Check(OriginalConstructorPorts.Calls.Count(c=>c=="runtime.temp-root")==calls,"Joined retry cannot replay completed runtime configuration");
            }finally{End(backend);}
        });
        Run("joined-reentrant-handshake",()=>
        {
            var backend=Backend();Exception? nested=null;try
            {
                Ready(backend,"ROGAlly",true);
                Set(backend,"hcCoreInitialized",false);Set(backend,"hcRuntimeConfigured",false);
                OriginalConstructorPorts.OnMark=stage=>{if(stage=="runtime.temp-root"){OriginalConstructorPorts.OnMark=null;nested=Reject(()=>Handshake(backend),"HC_NOT_INITIALIZED");}};
                Ready(backend,"ROGAlly",true);Check(nested is not null,"Actual factory rejects reentrant readiness during its remaining runtime configuration stage");
            }finally{End(backend);}
        });
        Run("joined-dispose-during-original-stage",()=>
        {
            var backend=Backend();try
            {
                Ready(backend,"ROGAlly",true);
                Set(backend,"hcCoreInitialized",false);Set(backend,"hcRuntimeConfigured",false);
                OriginalConstructorPorts.OnMark=stage=>{if(stage=="runtime.temp-root"){OriginalConstructorPorts.OnMark=null;((IDisposable)backend).Dispose();}};
                Reject(()=>Handshake(backend),"ObjectDisposedException");Check(Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Disposed joined factory cannot publish late readiness");
            }finally{End(backend);}
        });
        foreach(var kind in new[]{"device","assembly"})Run("joined-owner-replacement:"+kind,()=>
        {
            var backend=Backend();object? original=null;object? substitute=null;var replaced=false;
            try
            {
                Ready(backend,"ROGAlly",true);original=Get(backend,kind);
                var ownerField=kind=="device"?"hcInitializationDevice":"hcInitializationAssembly";
                Check(original is not null&&ReferenceEquals(Get(backend,ownerField),original),"Replacement negative must start from a real captured initialization owner");
                // Test-only retry state: keep the real selected objects and completed stages,
                // then enter the surviving validation boundary with readiness unpublished.
                Set(backend,"hcCoreInitialized",false);
                Check(Get(backend,"hcCoreInitialized")is false,"Owner replacement retry must keep the real core readiness marker unpublished");
                OriginalConstructorPorts.OnMark=stage=>{if(stage=="validation.acl"){OriginalConstructorPorts.OnMark=null;substitute=kind=="device"?Activator.CreateInstance(Hc!.GetType("HandheldCompanion.Devices.ROGAlly",true)!):typeof(JoinedFactoryProbe).Assembly;Set(backend,kind,substitute);replaced=true;}};
                Reject(()=>Handshake(backend),"HC_NOT_INITIALIZED");
                Check(replaced&&ReferenceEquals(Get(backend,kind),substitute),"Owner replacement must actually occur inside the remaining validation stage");
                Check(Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Replacement cannot inherit the original joined owner's readiness");
                Check(ReferenceEquals(Get(backend,ownerField),original),"Original initialization owner must remain recorded");
            }finally{if(kind=="device"&&original is not null)DisposeDevice(original);End(backend);}
        });
        Run("joined-disposed-factory-stays-refused",()=>
        {
            var backend=Backend();try{Ready(backend,"ROGAlly",true);((IDisposable)backend).Dispose();var count=OriginalConstructorPorts.Calls.Count;Reject(()=>Handshake(backend),"ObjectDisposedException");Check(OriginalConstructorPorts.Calls.Count==count&&Get(backend,"IsLoaded")is false,"Disposed owner cannot restart bootstrap");}finally{End(backend);}
        });
    }
    [STAThread]
    internal static int Main(string[] args)
    {
        try
        {
            // host, HC copy, runtime, shared, pins, original inventory, log config, output
            Check(args.Length==8||(args.Length==9&&(args[8]=="public"||args[8]=="lifecycle"||args[8]=="msi-lifecycle"||args[8]=="msi-manual"||args[8]=="msi-replay")),"Pass exact joined factory fixture arguments");PublicEntry=args.Length==9;LifecycleMode=PublicEntry&&args[8]=="lifecycle";MsiLifecycleMode=PublicEntry&&args[8]=="msi-lifecycle";MsiManualMode=PublicEntry&&args[8]=="msi-manual";MsiReplayMode=PublicEntry&&args[8]=="msi-replay";CallerThreadId=Environment.CurrentManagedThreadId;HostPath=Path.GetFullPath(args[0]);HcPath=Path.GetFullPath(args[1]);Runtime=Path.GetFullPath(args[2]);Shared=Path.GetFullPath(args[3]);var output=Path.GetFullPath(args[7]);Root=Path.GetDirectoryName(output)!;
            Check(!File.Exists(output)&&Root.Split(Path.DirectorySeparatorChar).Any(p=>p.Equals("Build",StringComparison.OrdinalIgnoreCase)),"Fresh owned Build joined evidence required");Directory.CreateDirectory(Root);
            Pins=new(JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(args[4]))!,StringComparer.OrdinalIgnoreCase);Pin(HostPath);Pin(HcPath);var probeReferences=typeof(JoinedFactoryProbe).Assembly.GetReferencedAssemblies().Select(a=>a.Name!).Order().ToArray();Check(!probeReferences.Contains("HandheldCompanion")&&!probeReferences.Contains("OneXFanCoexistence"),"Standalone probe cannot reference the legacy HC fixture or Harness");Check(Hc is null,"Standalone joined probe must not preload any HandheldCompanion fixture");
            AppBase=Path.Combine(Root,"owned-app-base");Directory.CreateDirectory(AppBase);var config=JsonNode.Parse(File.ReadAllText(args[6]))!;config["Serilog"]!["WriteTo"]![0]!["Args"]!["path"]=Path.Combine(Root,"real-shared.log");File.WriteAllText(Path.Combine(AppBase,"YeManFanHost.json"),config.ToJsonString());AppContext.SetData("APP_CONTEXT_BASE_DIRECTORY",AppBase);
            JoinedFactoryPorts.Root=Path.Combine(Root,"owned-temp");Directory.CreateDirectory(JoinedFactoryPorts.Root);OriginalConstructorPorts.SettingsRoot=Path.Combine(JoinedFactoryPorts.Root,"YeManFanHost-HC");
            BoundaryRoot=Path.Combine(Root,"n");Check(!Directory.Exists(BoundaryRoot)&&BoundaryRoot.Length<239,"Fresh boundary fixture must remain inside the owned case; select an evidence directory short enough for the exact259-character path");Directory.CreateDirectory(BoundaryRoot);
            AssemblyLoadContext.Default.Resolving+=Resolve;AppDomain.CurrentDomain.AssemblyLoad+=(_,e)=>Guard(e.LoadedAssembly);
            ToolLoadAuthority.Current.LoadPath(AssemblyLoadContext.Default,Path.Combine(Runtime,"Shared.dll"));Host=ToolLoadAuthority.Current.LoadPath(AssemblyLoadContext.Default,HostPath);
            if(PublicEntry)
            {
                var imports=typeof(JoinedFactoryPorts).Assembly.GetTypes().SelectMany(t=>t.GetMethods(All|BindingFlags.DeclaredOnly)).Select(m=>new{method=m,import=m.GetCustomAttribute<DllImportAttribute>()}).Where(r=>r.import is not null).ToArray();
                Check(imports.Length==1&&imports[0].import!.Value=="kernel32.dll"&&imports[0].import!.EntryPoint=="GetCurrentThreadId"&&imports[0].import!.ExactSpelling&&imports[0].method.GetParameters().Length==0&&imports[0].method.ReturnType==typeof(uint),"Public fixture declares only the original exact read-only OS identity endpoint, never a broad native grant");
            }
            if(MsiReplayMode)RunMsiReplayCases();else if(MsiManualMode)RunMsiManualCases();else if(MsiLifecycleMode)RunMsiLifecycleCases();else if(LifecycleMode)RunLifecycleCases();else{RunMatrix(args[5]);RunFailures();}
            var failed=Cases.Count(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
            File.WriteAllText(output,JsonSerializer.Serialize(new{status=failed==0?"PASS":"FAIL",checks=Checks,cases=Cases,hostSha256=Hash(HostPath),hcSha256=Hash(HcPath),lifecycleMode=LifecycleMode,msiLifecycleMode=MsiLifecycleMode,msiManualMode=MsiManualMode,msiReplayMode=MsiReplayMode,msiReplayCaseCount=MsiReplayCaseCount,msiManualPublicAttempts=MsiManualPublicAttempts,msiManualPublicSuccess=MsiManualPublicSuccess,msiManualOpenCycles=MsiManualOpenCycles,lifecycleIo=LifecycleIoPorts.Rows.ToArray(),publicEntry=PublicEntry,originalPublicLoadAndHandshakeExecuted=PublicEntry,actualStaMessagePumpExecuted=PublicEntry&&SawDispatcherPump,readOnlyOsThreadIdentityCalls=JoinedFactoryPorts.ThreadIdentityReads,inertPowerLineReads=JoinedFactoryPorts.PowerReads,forcedThreadCleanup=ForcedThreadCleanup,threadEvidence=ThreadEvidence,actualDefaultAssemblyLoadExecuted=true,actualOriginalGetCurrentAndConstructorsExecuted=Hc is not null,actualConfigureAndNativeAdmissionExecuted=true,actualIdentityCapabilityAndAuthorizationExecuted=true,legacyFixtureReferenced=probeReferences.Contains("HandheldCompanion"),probeReferences,nativeBoundaryRoot=BoundaryRoot,nativeBoundaryFiles=BoundaryFiles,managerStartCalled=false,actualDeviceOpenBodyExecuted=MsiLifecycleMode&&SawOriginalMsiBaseOpen||(MsiManualMode||MsiReplayMode)&&MsiManualOpenCycles>0,actualOriginalMsiBaseOpenExecuted=MsiLifecycleMode&&SawOriginalMsiBaseOpen||(MsiManualMode||MsiReplayMode)&&MsiManualOpenCycles>0,uniqueConstructedTypes=Constructed.Order().ToArray(),nativeMathBindings=MathBindings,deniedNative=DeniedNative,nativeDenials=NativeDenials,bindings=Bindings,hostMainStarted=false,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"Joined cases={Cases.Count} failed={failed} checks={Checks}");return failed==0?0:2;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
