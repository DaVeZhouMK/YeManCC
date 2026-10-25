using System.IO;
using System.Text.Json;
using HandheldCompanion;
using HandheldCompanion.Devices;
using HandheldCompanion.Devices.AYANEO;
using HandheldCompanion.Managers;
using HandheldCompanion.Controllers;
using HandheldCompanion.Inputs;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    static object MixedSubscriberSnapshot()=>new {settingsInit=ManagerFactory.settingsManager.InitializedSubscribers,settings=ManagerFactory.settingsManager.SettingSubscribers,deviceInit=ManagerFactory.deviceManager.InitializedSubscribers,device=ManagerFactory.deviceManager.DeviceSubscribers,profileInit=ManagerFactory.powerProfileManager.InitializedSubscribers,system=SystemManager.Subscribers,battery=ConsumerFixture.CloseTerminals.PowerSubscribers,controller=ControllerManager.Subscribers};
    static void MixedEmptySubscriptions()
    {
        Check(ManagerFactory.settingsManager.InitializedSubscribers==0&&ManagerFactory.settingsManager.SettingSubscribers==0,
            "Original mixed settings subscriptions must be removed by Close");
        Check(ManagerFactory.deviceManager.InitializedSubscribers==0&&ManagerFactory.deviceManager.DeviceSubscribers==0&&
            ManagerFactory.powerProfileManager.InitializedSubscribers==0&&ManagerFactory.powerProfileManager.Subscribers==0,
            "Original device/profile subscriptions must be removed by Close");
        Check(SystemManager.Subscribers==0&&ControllerManager.Subscribers==0&&ConsumerFixture.CloseTerminals.PowerSubscribers==0,
            "Original system/controller/battery subscriptions must be removed by Close");
    }
    static void MixedReset()
    {
        MixedEmptySubscriptions();
        ManagerFactory.settingsManager.Status=ManagerFactory.deviceManager.Status=ManagerFactory.powerProfileManager.Status=ManagerStatus.None;
        SystemManager.IsInitialized=false;
        ManagerFactory.settingsManager.Values.Clear();
        ManagerFactory.settingsManager.Values["BatteryChargeLimit"]=false;
        ManagerFactory.settingsManager.Values["BatteryChargeLimitPercent"]=80;
        ManagerFactory.settingsManager.Values["AYANEOFlipScreenEnabled"]=false;
        ManagerFactory.settingsManager.Values["AYANEOFlipScreenBrightness"]=37;
        ManagerFactory.deviceManager.PnPDevices.Clear();
        ManagerFactory.powerProfileManager.Current=new();
    }
    static object MixedBackend(int initializedMask=0,bool systemReady=false)
    {
        MixedReset();
        ManagerFactory.settingsManager.Status=(initializedMask&1)!=0?ManagerStatus.Initialized:ManagerStatus.None;
        ManagerFactory.deviceManager.Status=(initializedMask&2)!=0?ManagerStatus.Initialized:ManagerStatus.None;
        ManagerFactory.powerProfileManager.Status=(initializedMask&4)!=0?ManagerStatus.Initialized:ManagerStatus.None;
        SystemManager.IsInitialized=systemReady;
        var backend=Backend(new AYANEOFlipDS());Open(backend);Trace.Calls.Clear();return backend;
    }
    static void MixedEvents(object backend)
    {
        Events(backend);
        Check(Trace.Calls.Contains("Base.Events")&&ControllerManager.Subscribers==1&&ConsumerFixture.CloseTerminals.PowerSubscribers==1,
            "Complete original mixed OpenEvents entry must execute before Host ready publication");
    }
    static void MixedClose(object backend)
    {
        var active=ManagerFactory.settingsManager.Status!=ManagerStatus.None||ManagerFactory.deviceManager.Status!=ManagerStatus.None||ManagerFactory.powerProfileManager.Status!=ManagerStatus.None;
        if(active)
        {
            Reject(()=>Close(backend),"HC_CLOSE_PENDING");
            Check(Get(backend,"HcVirtualCloseReturned")is true&&Get(backend,"HcDeviceManagerStopCompleted")is not true,
                "An initialized shared manager cannot be declared cleaned by a fan-only Close");
        }
        else Close(backend);
        MixedEmptySubscriptions();
        Check(!IDevice.LibraryInitialized,"Original Close retires its owned transport before the manager verdict");
        // Explicit fixture manager-owner settlement; no production Start/Stop is
        // invoked, and the pending manager verdict above is never called success.
        ManagerFactory.settingsManager.Status=ManagerFactory.deviceManager.Status=ManagerFactory.powerProfileManager.Status=ManagerStatus.None;
        if(active)Close(backend);
        SystemManager.IsInitialized=false;
    }
    static void RunMixedEventsCases(string actualHc)
    {
        Run("mixed-events-full-original-methods-match-pe",()=>
        {
            using var a=AssemblyDefinition.ReadAssembly(actualHc);
            using var f=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location);
            var groups=new Dictionary<string,string[]> {
                [typeof(IDevice).FullName!]=["OpenEvents","QueryDevices","DeviceManager_HidDeviceArrived","DeviceManager_HidDeviceRemoved","DeviceManager_Initialized","QuerySettings","SettingsManager_SettingValueChanged","SettingsManager_Initialized","QueryPowerProfile","PowerProfileManager_Initialized"],
                [typeof(AYANEODevice).FullName!]=["OpenEvents","SystemManager_Initialized","SystemManager_PowerLineStatusChanged_Handler","SystemManager_SessionLockChanged"],
                [typeof(AYANEODeviceCEc).FullName!]=["OpenEvents","QuerySettings","SettingsManager_SettingValueChanged","PowerManager_RemainingChargePercentChanged"],
                [typeof(AYANEOFlipDS).FullName!]=["OpenEvents","QuerySettings","SettingsManager_SettingValueChanged","ControllerManager_InputsUpdated"]
            };
            foreach(var (type,names)in groups)foreach(var name in names)
            {
                var x=a.MainModule.GetType(type).Methods.Single(m=>m.Name==name);
                var y=f.MainModule.GetType(type).Methods.Single(m=>m.Name==name);
                Check(OldBinaryProbe.Ops(x).SequenceEqual(OldBinaryProbe.Ops(y)),"Complete mixed method opcodes match pinned HC PE: "+type+"::"+name);
                Check(x.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName)
                    .SequenceEqual(y.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName)),"Mixed method call signatures match pinned HC PE: "+type+"::"+name);
                Check(x.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName)
                    .SequenceEqual(y.Body.Instructions.Where(i=>i.Operand is FieldReference).Select(i=>((FieldReference)i.Operand).FullName)),"Mixed method field references match pinned HC PE: "+type+"::"+name);
                Check(x.Body.ExceptionHandlers.Select(h=>h.HandlerType+":"+h.CatchType?.FullName).SequenceEqual(y.Body.ExceptionHandlers.Select(h=>h.HandlerType+":"+h.CatchType?.FullName)),"Mixed exception regions match pinned HC PE: "+type+"::"+name);
            }
            var actualEnum=a.MainModule.GetType(typeof(ManagerStatus).FullName!);var fixtureEnum=f.MainModule.GetType(typeof(ManagerStatus).FullName!);
            Check(actualEnum.CustomAttributes.Any(x=>x.AttributeType.FullName=="System.FlagsAttribute")&&fixtureEnum.CustomAttributes.Any(x=>x.AttributeType.FullName=="System.FlagsAttribute"),"Original manager Flags formatting is retained in the compiled fixture");
            Check(actualEnum.Fields.Where(x=>x.HasConstant).Select(x=>x.Name+":"+x.Constant).SequenceEqual(fixtureEnum.Fields.Where(x=>x.HasConstant).Select(x=>x.Name+":"+x.Constant)),"Manager branch selection uses original enum values, not invented string statuses");
        });
        for(var mask=0;mask<8;mask++)foreach(var ready in new[]{false,true})
        {
            var selected=mask;
            Run("mixed-events-initial-state:"+mask+":"+ready,()=>
            {
                var b=MixedBackend(selected,ready);
                try
                {
                    MixedEvents(b);
                    Check(ManagerFactory.settingsManager.InitializedSubscribers==((selected&1)==0?1:0)&&ManagerFactory.settingsManager.SettingSubscribers==((selected&1)!=0?1:0),"Settings ready/deferred branch is consumed by the complete original method");
                    Check(ManagerFactory.deviceManager.InitializedSubscribers==((selected&2)==0?1:0)&&ManagerFactory.deviceManager.DeviceSubscribers==((selected&2)!=0?4:0),"Device ready/deferred branch retains original enumeration subscription");
                    Check(ManagerFactory.powerProfileManager.InitializedSubscribers==((selected&4)==0?1:0),"Profile ready/deferred branch retains original subscription");
                    Check(Trace.Calls.Count(c=>c=="FAN:044a:0")==((selected&4)!=0?1:0),"Only original ready profile query performs its current HC hardware callback");
                    Check(SystemManager.Subscribers==(ready?3:1)&&Trace.Calls.Count(c=>c=="System.UpdateLED")== (ready?1:0),"System ready/deferred branch retains original power and lock ownership");
                    Check(Get(b,"HcOpenEventsInvoked")is true,"Actual Host publishes completion only after original mixed entry returns");
                }
                finally{MixedClose(b);}
            });
        }
        foreach(var manager in new[]{"settings","device","profile","system"})Run("mixed-events-late-initialization:"+manager,()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);Trace.Calls.Clear();
                switch(manager)
                {
                    case "settings":ManagerFactory.settingsManager.RaiseInitialized();Check(ManagerFactory.settingsManager.SettingSubscribers==1&&Trace.Calls.Contains("Screen.Brightness:0")&&Trace.Calls.Contains("Battery.Bypass:close"),"Late settings initialization reaches both original Flip and CEc queries");break;
                    case "device":ManagerFactory.deviceManager.RaiseInitialized();Check(ManagerFactory.deviceManager.DeviceSubscribers==4&&Trace.Calls.Contains("Devices.GenericUpdated"),"Late device initialization executes original enumeration and binding");break;
                    case "profile":ManagerFactory.powerProfileManager.RaiseInitialized();Check(Trace.Calls.Contains("FAN:044a:0"),"Late profile initialization reaches the original HC callback");break;
                    default:SystemManager.RaiseInitialized();Check(SystemManager.Subscribers==3&&Trace.Calls.Contains("System.UpdateLED"),"Late system initialization owns both power callbacks");break;
                }
            }finally{MixedClose(b);}
        });
        Run("mixed-events-original-device-filter-and-unsubscribe",()=>
        {
            var b=MixedBackend();var match=new PnPDetails{VendorID=100,ProductID=200};var wrong=new PnPDetails{VendorID=101,ProductID=200};
            ManagerFactory.deviceManager.PnPDevices["matching"]=match;ManagerFactory.deviceManager.PnPDevices["other"]=wrong;
            try
            {
                MixedEvents(b);Trace.Calls.Clear();ManagerFactory.deviceManager.RaiseInitialized();
                Check(Trace.Calls.Count(c=>c=="Device.Inserted:True")==1,"Original QueryDevices enumerates and filters matching hardware IDs");
                Trace.Calls.Clear();ManagerFactory.deviceManager.PublishArrived(wrong);ManagerFactory.deviceManager.PublishRemoved(wrong);Check(Trace.Calls.Count==0,"Unrelated PnP events do not reach original insertion/removal");
                ManagerFactory.deviceManager.PublishArrived(match);ManagerFactory.deviceManager.PublishRemoved(match);Check(Trace.Calls.SequenceEqual(new[]{"Device.Inserted:True","Device.Removed"}),"Original matching arrival/removal callbacks remain reachable");
            }finally{MixedClose(b);}
            Trace.Calls.Clear();ManagerFactory.deviceManager.PublishArrived(match);ManagerFactory.deviceManager.PublishRemoved(match);Check(Trace.Calls.Count==0,"Closed original device cannot receive late PnP callbacks");
        });
        Run("mixed-events-original-controller-settings-chain",()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);ManagerFactory.settingsManager.RaiseInitialized();Trace.Calls.Clear();
                var press=new ControllerState();press.ButtonState[ButtonFlags.OEM5]=true;
                ControllerManager.Publish(press,true);Check(Trace.Calls.Count==0,"Mapped input cannot activate the original OEM screen action");
                ControllerManager.Publish(press,false);Check(Trace.Calls.Count(c=>c.StartsWith("Settings.Set:AYANEOFlipScreenEnabled"))==1&&Trace.Calls.Contains("Screen.Brightness:37")&&Trace.Calls.Contains("Screen.Visibility:Visible"),"Original unmapped OEM5 callback reaches settings and screen leaves once");
                Trace.Calls.Clear();ControllerManager.Publish(press,false);Check(Trace.Calls.Count==0,"Held unchanged button state cannot replay the original toggle");
                ControllerManager.Publish(new ControllerState(),false);Check(Trace.Calls.Count==0,"Button release cannot toggle the original screen setting");
                ControllerManager.Publish(press,false);Check(Trace.Calls.Contains("Screen.Brightness:0")&&Trace.Calls.Contains("Screen.Visibility:Collapsed"),"A later press reaches the original opposite setting");
            }finally{MixedClose(b);}
        });
        Run("mixed-events-original-battery-threshold-chain",()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);ManagerFactory.settingsManager.RaiseInitialized();ManagerFactory.settingsManager.Publish("BatteryChargeLimit",true);
                foreach(var percent in new[]{79,80,81}){Trace.Calls.Clear();ConsumerFixture.CloseTerminals.PublishBattery(percent);Check(Trace.Calls.SequenceEqual(new[]{percent<80?"Battery.Bypass:close":"Battery.Bypass:open"}),"Original battery threshold branch preserved at "+percent);}
                ManagerFactory.settingsManager.Publish("BatteryChargeLimit",false);Trace.Calls.Clear();ConsumerFixture.CloseTerminals.PublishBattery(100);Check(Trace.Calls.Count==0,"Disabled battery policy cannot issue a power-event action");
            }finally{MixedClose(b);}
            Trace.Calls.Clear();ConsumerFixture.CloseTerminals.PublishBattery(100);Check(Trace.Calls.Count==0,"Closed original battery subscription cannot issue a late action");
        });
        Run("mixed-events-original-system-power-and-lock-chain",()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);SystemManager.RaiseInitialized();Trace.Calls.Clear();
                SystemManager.PublishLine(System.Windows.Forms.PowerLineStatus.Online);SystemManager.PublishLock(true);Check(Trace.Calls.Count==0,"Online and locked original system paths retain their early returns");
                SystemManager.PublishLine(System.Windows.Forms.PowerLineStatus.Offline);SystemManager.PublishLock(false);Check(Trace.Calls.Count(c=>c=="System.UpdateLED")==2,"Original offline/unlock paths reach their explicit LED leaf");
            }finally{MixedClose(b);}
            Trace.Calls.Clear();SystemManager.PublishLine(System.Windows.Forms.PowerLineStatus.Offline);SystemManager.PublishLock(false);Check(Trace.Calls.Count==0,"Closed original system callbacks cannot reach LED work");
        });
        Run("mixed-events-close-blocks-all-late-initialization",()=>
        {
            var b=MixedBackend();MixedEvents(b);MixedClose(b);Trace.Calls.Clear();
            ManagerFactory.settingsManager.RaiseInitialized();ManagerFactory.deviceManager.RaiseInitialized();ManagerFactory.powerProfileManager.RaiseInitialized();SystemManager.RaiseInitialized();
            ManagerFactory.settingsManager.Publish("BatteryChargeLimit",true);ConsumerFixture.CloseTerminals.PublishBattery(100);var press=new ControllerState();press.ButtonState[ButtonFlags.OEM5]=true;ControllerManager.Publish(press,false);
            Check(Trace.Calls.Count==0,"Original Close must remove all callbacks before late manager/input/power events");MixedEmptySubscriptions();MixedReset();
        });
        Run("mixed-events-repeated-host-entry-is-idempotent",()=>
        {
            var b=MixedBackend();try{MixedEvents(b);var before=Trace.Calls.Count(c=>c=="Base.Events");Events(b);Check(Trace.Calls.Count(c=>c=="Base.Events")==before&&ControllerManager.Subscribers==1&&ConsumerFixture.CloseTerminals.PowerSubscribers==1&&SystemManager.Subscribers==1,"Actual Host cannot re-enter original mixed subscriptions after publication");}finally{MixedClose(b);}
        });
        Run("mixed-events-settings-exception-retains-close-owner",()=>
        {
            var b=MixedBackend(1);ManagerFactory.settingsManager.Values["AYANEOFlipScreenEnabled"]="not-a-bool";
            try{Reject(()=>Events(b),"FormatException");Check(Get(b,"HcOpenEventsInvoked")is not true&&IDevice.LibraryInitialized,"Original mixed callback failure cannot publish Host ready or abandon owned transport");}
            finally{MixedClose(b);}
        });
    }

    static void RunMixedDebtCases()
    {
        Run("mixed-events-complete-close-reopen-starts-new-entry",()=>
        {
            var b=MixedBackend();MixedEvents(b);Set(b,"staThread",Thread.CurrentThread);Invoke(b,"Close",false);MixedEmptySubscriptions();
            Check(Get(b,"hcRawOpenEventsDevice")is null&&Get(b,"hcRawOpenEventsReturned")is false,"Completed original Close retires only its raw entry record");
            Set(b,"oneXFanOpenAdmissionEpoch",Get(b,"suspendGateEpoch"));Invoke(b,"BeginOpenAttemptEvidence");Open(b);Trace.Calls.Clear();
            try{MixedEvents(b);Check(Trace.Calls.Count(c=>c=="Base.Events")==1,"A freshly opened session must execute its own original raw entry");}finally{MixedClose(b);}
        });
        Run("mixed-events-throwing-close-retains-raw-entry-owner",()=>
        {
            var b=MixedBackend();MixedEvents(b);var d=Get(b,"device");Trace.RgbReleaseThrows=true;
            try
            {
                Reject(()=>Close(b),"RGB_RELEASE_THROW");Check(ReferenceEquals(Get(b,"hcRawOpenEventsDevice"),d)&&Get(b,"hcRawOpenEventsReturned")is true,"A throwing original Close cannot erase its raw subscription debt");
                Check(Get(b,"HcVirtualCloseReturned")is not true,"Raw debt retention cannot fabricate virtual Close success");
            }
            finally{Trace.RgbReleaseThrows=false;MixedClose(b);}
            Check(Get(b,"hcRawOpenEventsDevice")is null,"Successful existing close retry retires raw entry debt");
        });
        Run("mixed-events-late-close-exception-retains-debt-until-return",()=>
        {
            var b=MixedBackend();MixedEvents(b);var d=(IDevice)Get(b,"device")!;IDevice.ClosedEventHandler fail=_=>throw new IOException("MIXED_LATE_CLOSED_THROW");d.Closed+=fail;
            try
            {
                Reject(()=>Close(b),"MIXED_LATE_CLOSED_THROW");
                Check(ManagerFactory.settingsManager.InitializedSubscribers==0&&ManagerFactory.settingsManager.SettingSubscribers==0&&ManagerFactory.deviceManager.InitializedSubscribers==0&&ManagerFactory.deviceManager.DeviceSubscribers==0&&ManagerFactory.powerProfileManager.InitializedSubscribers==0&&SystemManager.Subscribers==0&&ControllerManager.Subscribers==0&&ConsumerFixture.CloseTerminals.PowerSubscribers==0,
                    "Late Closed-event failure follows original raw subscription removal");
                Check(ManagerFactory.powerProfileManager.Subscribers==2,"Host profile handlers remain a recorded debt until virtual Close returns normally");
                Check(Get(b,"hcRawOpenEventsDevice")is not null&&Get(b,"HcVirtualCloseReturned")is not true&&!IDevice.LibraryInitialized,"Actual transport retirement and removed subscriptions do not fake a normal virtual return");
            }
            finally{d.Closed-=fail;MixedClose(b);}
            Check(Get(b,"hcRawOpenEventsDevice")is null,"A normally returned original close retry retires its own raw record");
        });
    }

    static void RunMixedManagerFlagCases()
    {
        foreach(var manager in new[]{"settings","device","profile"})Run("mixed-events-combined-manager-flags-retain-cleanup:"+manager,()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);
                IManager selected=manager=="settings"?ManagerFactory.settingsManager:manager=="device"?ManagerFactory.deviceManager:ManagerFactory.powerProfileManager;
                selected.Status=ManagerStatus.Initialized|ManagerStatus.Busy;
                Invoke(b,"ObserveManagerFactoryLifecycle");
                Check(Get(b,"hcManagerFactoryLifecycle")?.ToString()=="Active","Combined original manager flags must remain active until their own owner settles");
            }
            finally{MixedClose(b);}
        });
        foreach(var status in new[]{ManagerStatus.Halting|ManagerStatus.Busy,ManagerStatus.Halted|ManagerStatus.Busy})Run("mixed-events-faulted-manager-flags-retain-cleanup:"+status,()=>
        {
            var b=MixedBackend();try
            {
                MixedEvents(b);ManagerFactory.deviceManager.Status=status;
                Reject(()=>Invoke(b,"ObserveManagerFactoryLifecycle"),"HC_MANAGER_FACTORY_LIFECYCLE_UNKNOWN");
                Check(Get(b,"hcManagerFactoryLifecycle")?.ToString()=="Faulted"&&Get(b,"HcDeviceManagerStopCompleted")is not true,"Combined halting/halted manager flags cannot fabricate clean ownership");
            }
            finally{MixedClose(b);}
        });
        foreach(var manager in new[]{"settings","device","profile"})Run("mixed-events-combined-flags-reach-public-cleanup:"+manager,()=>
        {
            var b=MixedBackend();var engine=CloseEngine(b);try
            {
                MixedEvents(b);var state=Get(engine,"state")!;Set(state,"OpenCalled",true);Set(state,"OpenEventsCalled",true);
                // The fixture starts after Open. Match the actual Open() API's
                // false pending-session marker instead of HostState's idle default.
                Set(state,"HcDeviceManagerStopCompleted",Get(b,"HcDeviceManagerStopCompleted"));
                IManager selected=manager=="settings"?ManagerFactory.settingsManager:manager=="device"?ManagerFactory.deviceManager:ManagerFactory.powerProfileManager;
                selected.Status=ManagerStatus.Initialized|ManagerStatus.Busy;
                Check(Invoke(engine,"CloseHcSessionForLifecycle",false,true,true)is false,"Combined manager flags must keep the actual engine cleanup pending");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(snapshot,"HcCloseCleanupPending")is true&&Get(snapshot,"HcDeviceManagerStopCompleted")is not true,"Actual public snapshot cannot report a combined active manager as cleaned");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("HC_CLOSE_PENDING")==true,"Actual public snapshot retains the original pending-cleanup cause");
            }
            finally{EndEngine(engine);MixedClose(b);}
        });
    }
    static int RunMixedFlagReproduction(string[] args)
    {
        RunMixedManagerFlagCases();var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,scope="Same eight original enum flag backend/engine/public snapshot consumers"},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }

    static void RunMixedReentryCases()
    {
        Run("mixed-events-partial-entry-requires-close-before-retry",()=>
        {
            var b=MixedBackend(1);ManagerFactory.settingsManager.Values["AYANEOFlipScreenEnabled"]="not-a-bool";
            try
            {
                Reject(()=>Events(b),"FormatException");
                ManagerFactory.settingsManager.Values["AYANEOFlipScreenEnabled"]=false;
                var count=Trace.Calls.Count(c=>c=="Base.Events");
                Exception? pending=null;try{PrepareEvents(b);}catch(Exception error){pending=error;}
                Check(pending is not null&&Get(pending,"Code")?.ToString()=="HC_CLOSE_PENDING","A partial original mixed entry must require same-owner Close before retry");
                Check(Trace.Calls.Count(c=>c=="Base.Events")==count,"A partial original mixed entry cannot be retried before its same-owner Close");
            }
            finally{MixedClose(b);}
        });
        Run("mixed-events-superseded-finalize-does-not-repeat-original-entry",()=>
        {
            var b=MixedBackend();var preparation=PrepareEvents(b);var queue=Get(b,"externalProfileQueue")!;
            ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage=="Applied")Invoke(queue,"BeginSession");};
            try
            {
                RejectEventAdmission(()=>FinalizeEvents(b,preparation));
                ManagerFactory.powerProfileManager.OnAdd=null;
                FinalizeEvents(b,PrepareEvents(b));
                Check(Trace.Calls.Count(c=>c=="Base.Events")==1&&ManagerFactory.settingsManager.InitializedSubscribers==1&&ManagerFactory.deviceManager.InitializedSubscribers==1&&ManagerFactory.powerProfileManager.InitializedSubscribers==1&&ControllerManager.Subscribers==1&&ConsumerFixture.CloseTerminals.PowerSubscribers==1&&SystemManager.Subscribers==1,
                    "Original mixed subscriptions cannot be repeated after superseded Host finalization");
            }
            finally{ManagerFactory.powerProfileManager.OnAdd=null;Close(b);}
            MixedEmptySubscriptions();
        });
    }
    static int RunMixedReproduction(string[] args)
    {
        RunMixedReentryCases();
        var failures=Cases.Count(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failures==0?"PASS":"FAIL",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,scope="Same two actual mixed-entry consumers, isolated reproduction process"},new JsonSerializerOptions{WriteIndented=true}));
        return failures==0?0:2;
    }

    static int MutateMixedManagerFlags(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);
        var method=image.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="HasManagerStatusFlag");
        Check(method.IsStatic&&method.Parameters.Count==2&&method.ReturnType.FullName=="System.Boolean","Exact manager flag consumer is required");
        method.Body.Instructions.Clear();method.Body.ExceptionHandlers.Clear();method.Body.Variables.Clear();
        var equals=image.MainModule.ImportReference(typeof(string).GetMethod(nameof(string.Equals),[typeof(string),typeof(StringComparison)])!);var il=method.Body.GetILProcessor();
        il.Append(il.Create(OpCodes.Ldarg_0));il.Append(il.Create(OpCodes.Ldarg_1));il.Append(il.Create(OpCodes.Ldc_I4,(int)StringComparison.OrdinalIgnoreCase));il.Append(il.Create(OpCodes.Callvirt,equals));il.Append(il.Create(OpCodes.Ret));
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);
        using var before=AssemblyDefinition.ReadAssembly(source);using var after=AssemblyDefinition.ReadAssembly(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> all)=>all.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(h=>h.ToString())):"no-body");
        var a=Types(before.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"Manager flag fault cannot add or remove methods");var changed=a.Keys.Where(k=>a[k]!=b[k]).ToArray();Check(changed.Length==1&&changed[0]==method.FullName,"Only manager flag token consumption changes");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="Replace only combined manager flag consumption with previous whole-string comparison; actual original events and Close still execute",nativeIo=false}));return 0;
    }

    static int MutateMixedReentryGuard(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);
        var method=image.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="OpenEventsBeginCore");
        var field=method.Body.Instructions.First(i=>i.OpCode==OpCodes.Ldfld&&i.Operand is FieldReference f&&f.Name=="hcRawOpenEventsDevice");
        var branch=field.Next;
        Check(branch.OpCode==OpCodes.Brfalse||branch.OpCode==OpCodes.Brfalse_S,"Exact original raw-entry once branch is required");
        var targetInstruction=(Instruction)branch.Operand;
        var il=method.Body.GetILProcessor();il.InsertBefore(branch,il.Create(OpCodes.Pop));branch.OpCode=OpCodes.Br;branch.Operand=targetInstruction;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);
        using var before=AssemblyDefinition.ReadAssembly(source);using var after=AssemblyDefinition.ReadAssembly(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> all)=>all.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(h=>h.ToString())):"no-body");
        var a=Types(before.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"Mixed entry fault cannot add or remove methods");var changed=a.Keys.Where(k=>a[k]!=b[k]).ToArray();Check(changed.Length==1&&changed[0]==method.FullName,"Only the existing raw-entry once guard changes");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="Bypass only raw-entry once guard; preserve actual original mixed OpenEvents, Host admission/finalization and complete original Close",nativeIo=false}));return 0;
    }
}
