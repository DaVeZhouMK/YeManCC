using System.IO;
using System.Reflection;
static partial class JoinedFactoryProbe
{
    static object? Sta(object backend,Action action,string operation)
    {
        var method=backend.GetType().GetMethods(All).Single(m=>m.Name=="OnStaBounded"&&!m.IsGenericMethod&&m.GetParameters()[0].ParameterType==typeof(Action));
        try{return method.Invoke(backend,[action,operation]);}catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    static void AuthorizeFixture(object backend)
    {
        var file=Path.Combine(Root,"inert-software-only-authorization.txt");File.WriteAllText(file,"hardwareWritesAuthorized: true\nallMappedFanRoutesAuthorized: true\n");
        var options=Get(backend,"options")!;Set(options,"AllowHardwareWrites",true);Set(options,"AuthorizationPath",file);Check(Get(backend,"CanWrite")is true,"Owned fixture authority must pass the actual original gate while every device IO endpoint remains isolated");
    }
    static readonly (string product,string type)[] LifecycleModels=[("ONEXPLAYER X1 A","OneXPlayerX1AMD"),("ONEXPLAYER X1 i","OneXPlayerX1Intel"),("ONEXPLAYER X1Pro","OneXPlayerX1Pro"),("ONEXPLAYER APEX","OneXPlayerApex"),("ONEXPLAYER X1 mini","OneXPlayerX1Mini")];
    static object LifecycleDevice(object backend)=>Get(backend,"device")!;
    static object Lifecycle(object backend)=>Get(backend,"oneXFanLifecycle")??throw new InvalidOperationException("Original admitted model must select narrow fan lifecycle");
    static System.Reflection.FieldInfo LibraryField=>Hc!.GetType("HandheldCompanion.Devices.IDevice",true)!.GetField("openLibSys",All)!;
    static void OpenPublic(object backend)=>Invoke(backend,"Open");
    static void ClosePublic(object backend)=>Invoke(backend,"Close",true);
    static void Duty(object backend,double value)=>Sta(backend,()=>Invoke(backend,"WriteFanProfileDuty",value),"life-duty");
    static void Control(object backend,bool enabled)
    {
        var profile=Get(backend,"hcProfileTemplate")!;var fan=Get(profile,"FanProfile")!;var field=fan.GetType().GetField("fanMode",All)!;field.SetValue(fan,Enum.ToObject(field.FieldType,enabled?1:0));Sta(backend,()=>Invoke(backend,"ApplyPowerProfile",profile,"Background",null,null),"life-control");
    }
    static int Subscriptions(string name)
    {
        var factory=Hc!.GetType("HandheldCompanion.Managers.ManagerFactory",true)!;var manager=factory.GetField("powerProfileManager",All)!.GetValue(null)!;return(manager.GetType().GetField(name,All)!.GetValue(manager)as Delegate)?.GetInvocationList().Length??0;
    }
    static void ManagersIdle()
    {
        var factory=Hc!.GetType("HandheldCompanion.Managers.ManagerFactory",true)!;foreach(var m in(System.Collections.IEnumerable)factory.GetProperty("Managers",All)!.GetValue(null)!)if(m is not null)Check(Get(m,"Status")?.ToString()=="None","Actual manager observation must not start any manager");
    }
    static void Packet(byte low,byte value,int offset=0)
    {
        var rows=LifecycleIoPorts.Rows.Where(r=>r.Stage=="port-write").Skip(offset).Take(12).ToArray();byte[] values=[0x2e,0x11,0x2f,0x04,0x2e,0x10,0x2f,low,0x2e,0x12,0x2f,value];
        Check(rows.Length==12&&rows.Select(r=>r.Value).SequenceEqual(values)&&rows.Select(r=>r.Register).SequenceEqual(Enumerable.Range(0,12).Select(n=>(ushort)(n%2==0?0x4e:0x4f))),"Original EC writer must retain its complete12-byte command/data packet");
    }
    static void LifecycleCase(string id,Action<object> body,string product="ONEXPLAYER X1 mini",string expected="OneXPlayerX1Mini",bool authority=true)
    {
        Run(id,()=>
        {
            LifecycleIoPorts.Reset();Reset("ONE-NETBOOK",product,"unused");var backend=Backend();try
            {
                Ready(backend,expected,true);if(authority)AuthorizeFixture(backend);body(backend);
                Check(!OriginalConstructorPorts.Calls.Any(c=>c.StartsWith("life.forbidden.",StringComparison.Ordinal)),"Fan lifecycle cannot enter general Open/Close/events or LED HID readiness");
                Check(LibraryField.GetValue(null)is null,"Completed lifecycle case must return its original EC transport");Check(LifecycleIoPorts.OpenTransportCount==0,"Completed original lifecycle must return every initialized transport, including unpublished and foreign fixture owners");ManagersIdle();
            }
            finally
            {
                LifecycleIoPorts.OnIo=null;LifecycleIoPorts.WriteFailures=LifecycleIoPorts.DisposeFailures=LifecycleIoPorts.ExitFailures=0;
                if(Get(backend,"oneXFanLifecycle")is object life&&Get(life,"HasResources")is true)try{Sta(backend,()=>Invoke(life,"Close"),"life-owned-fixture-finally");}catch{}
                End(backend);
            }
        });
    }
    static void RunLifecycleCases()
    {
        LifecycleCase("onex-mini-original-constructed-narrow-open",backend=>{Sta(backend,()=>Invoke(backend,"OpenHcDevice"),"life-open-boundary");Check(Get(Lifecycle(backend),"IsOpen")is true,"Admitted original Mini must use narrow fan lifecycle");ClosePublic(backend);},authority:false);
        LifecycleCase("onex-mini-original-public-open-skips-led-hid",backend=>{OpenPublic(backend);var evidence=Get(backend,"HcReadyWaitEvidenceForSelfTest")!;Check(Get(evidence,"skipped")?.ToString()=="onex-fan-only-ec-readiness"&&Get(evidence,"ready")is false&&Get(evidence,"timedOut")is false,"Skipped LED readiness must remain explicitly unprobed, not physical readiness or a timeout");ClosePublic(backend);});
        foreach(var model in LifecycleModels)
        {
            LifecycleCase("onex-original-public-cycle:"+model.type,backend=>
            {
                OpenPublic(backend);Invoke(backend,"OpenEvents");Check(Subscriptions("Applied")==1&&Subscriptions("Discarded")==1,"Actual Host attaches exactly its two original manager event delegates");
                Control(backend,true);Duty(backend,50);ClosePublic(backend);Packet(0x4a,1);Packet(0x4b,92,12);Packet(0x4a,0,24);
                Check(LifecycleIoPorts.Rows.Count(r=>r.Stage=="port-write")==36&&LifecycleIoPorts.Rows.Count(r=>r.Stage=="initialize")==1&&LifecycleIoPorts.Rows.Count(r=>r.Stage=="dispose")==1,"Exact manual/duty/automatic packets and one owned transport lifecycle");
                Check(Subscriptions("Applied")==0&&Subscriptions("Discarded")==0&&Get(backend,"HcManagerCleanupConfirmed")is true,"Original public close unsubscribes and observes actual idle manager status");Check(Get(backend,"OemRestoreVerified")is false&&Get(backend,"OemOwnershipStatus")?.ToString()=="hc-close-returned-physical-unknown","Software Close cannot claim physical OEM acknowledgement");
            },model.product,model.type);
            LifecycleCase("onex-original-public-reopen:"+model.type,backend=>{for(var n=0;n<2;n++){OpenPublic(backend);Invoke(backend,"OpenEvents");Duty(backend,25);ClosePublic(backend);}Check(LifecycleIoPorts.Rows.Count(r=>r.Stage=="initialize")==2&&LifecycleIoPorts.Rows.Count(r=>r.Stage=="dispose")==2&&Subscriptions("Applied")==0,"Reopen must use a fresh owned transport and return both original event registrations");},model.product,model.type);
        }
        LifecycleCase("onex-original-unauthorized-open",backend=>{Reject(()=>OpenPublic(backend),"REAL_WRITE_AUTH_REQUIRED");Check(LifecycleIoPorts.Rows.Count==0&&Get(LifecycleDevice(backend),"DeviceOpen")is false,"Original authorization rejection precedes any transport or device state");},authority:false);
        LifecycleCase("onex-original-init-false-retry",backend=>{LifecycleIoPorts.InitializeResult=false;Reject(()=>OpenPublic(backend),"Open returned false");Check(Get(Lifecycle(backend),"HasResources")is false&&Get(backend,"HcOpenInvocationStarted")is false,"False init cannot leave a published transport or fabricated close debt");LifecycleIoPorts.InitializeResult=true;OpenPublic(backend);ClosePublic(backend);});
        LifecycleCase("onex-original-init-throw-retry",backend=>{LifecycleIoPorts.InitializeFailures=1;Reject(()=>OpenPublic(backend),"LIFE_INIT_FAILED");Check(Get(Lifecycle(backend),"HasResources")is false,"Thrown init returns only its owned unpublished transport");OpenPublic(backend);ClosePublic(backend);});
        LifecycleCase("onex-original-public-open-cleans-first-dispose-failure",backend=>{LifecycleIoPorts.InitializeFailures=LifecycleIoPorts.DisposeFailures=1;Reject(()=>OpenPublic(backend),"LIFE_INIT_FAILED");Check(Get(Lifecycle(backend),"HasResources")is false&&Get(backend,"HcOpenInvocationStarted")is false&&LifecycleIoPorts.Rows.Count(r=>r.Stage=="dispose")==2,"Public Open failure executes its genuine cleanup retry before returning, without retaining fake debt");});
        LifecycleCase("onex-original-init-cleanup-debt",backend=>{LifecycleIoPorts.InitializeFailures=1;LifecycleIoPorts.DisposeFailures=2;Reject(()=>OpenPublic(backend),"LIFE_INIT_FAILED");Check(Get(Lifecycle(backend),"HasResources")is true,"Throwing init cleanup retains real owned debt");ClosePublic(backend);Check(Get(Lifecycle(backend),"HasResources")is false,"Public close settles the same unpublished transport debt");});
        foreach(var control in new[]{false,true})LifecycleCase("onex-original-false-ec-result:"+(control?"control":"duty"),backend=>{OpenPublic(backend);LifecycleIoPorts.WriteFailures=1;Reject(()=>{if(control)Control(backend,true);else Duty(backend,50);},"FAN_CURVE_WRITE_FAILED");Check(Get(Lifecycle(backend),"HasResources")is true,"Original EC false cannot be silently accepted or discard transport ownership");ClosePublic(backend);});
        LifecycleCase("onex-original-release-rejection-retry",backend=>{OpenPublic(backend);LifecycleIoPorts.WriteFailures=1;Reject(()=>ClosePublic(backend),"automatic return was rejected");Check(Get(Lifecycle(backend),"HasResources")is true&&LibraryField.GetValue(null)is not null&&LifecycleIoPorts.Rows.All(r=>r.Stage!="dispose"),"Rejected original EC return must keep the same owner and avoid disposal");ClosePublic(backend);});
        LifecycleCase("onex-original-dispose-failure-no-repeat-release",backend=>{OpenPublic(backend);LifecycleIoPorts.DisposeFailures=1;Reject(()=>ClosePublic(backend),"LIFE_DISPOSE_FAILED");var bytes=LifecycleIoPorts.Rows.Count(r=>r.Stage=="port-write");Check(bytes==12&&Get(Lifecycle(backend),"HasResources")is true,"A returned EC release with failed disposal stays owned");ClosePublic(backend);Check(LifecycleIoPorts.Rows.Count(r=>r.Stage=="port-write")==bytes,"Disposal retry cannot issue a duplicate EC release");});
        LifecycleCase("onex-original-foreign-transport",backend=>{OpenPublic(backend);var owned=LibraryField.GetValue(null)!;var foreign=Activator.CreateInstance(owned.GetType())!;Sta(backend,()=>Invoke(foreign,"InitializeOls"),"life-foreign-fixture-init");LibraryField.SetValue(null,foreign);try{var count=LifecycleIoPorts.Rows.Count;Reject(()=>Duty(backend,50),"owned transport");Reject(()=>ClosePublic(backend),"owned transport");Check(LifecycleIoPorts.Rows.Count==count&&ReferenceEquals(LibraryField.GetValue(null),foreign),"Retired owner cannot write or dispose another original transport instance");}finally{LibraryField.SetValue(null,owned);ClosePublic(backend);Sta(backend,()=>Invoke(foreign,"Dispose"),"life-foreign-fixture-dispose");}});
        LifecycleCase("onex-original-metadata-drift",backend=>{OpenPublic(backend);var d=LifecycleDevice(backend);var old=Get(d,"ECDetails")!;var changed=Get(d,"ECDetails")!;changed.GetType().GetField("FanValueMax")!.SetValue(changed,(short)255);Set(d,"ECDetails",changed);try{var count=LifecycleIoPorts.Rows.Count;Reject(()=>Duty(backend,50),"EC fan data changed");Check(LifecycleIoPorts.Rows.Count==count,"Changed original metadata is rejected before the EC writer");}finally{Set(d,"ECDetails",old);ClosePublic(backend);}});
        LifecycleCase("onex-original-write-superseded",backend=>{OpenPublic(backend);LifecycleIoPorts.OnIo=(stage,_)=>{if(stage=="exit"){LifecycleIoPorts.OnIo=null;Set(backend,"suspendGateEpoch",1L);}};Reject(()=>Duty(backend,50),"FAN_WRITE_SUPERSEDED");Check(LifecycleIoPorts.Rows.Count(r=>r.Stage=="port-write")==12,"An already-issued original EC packet must remain uncertain after the owner retires");ClosePublic(backend);});
        LifecycleCase("onex-original-init-superseded",backend=>{LifecycleIoPorts.OnIo=(stage,_)=>{if(stage=="initialize"){LifecycleIoPorts.OnIo=null;Set(backend,"suspendGateEpoch",1L);}};Reject(()=>OpenPublic(backend),"HC_RESUME_SUPERSEDED");Check(Get(Lifecycle(backend),"HasResources")is false&&LibraryField.GetValue(null)is null&&LifecycleIoPorts.Rows.All(r=>r.Stage!="port-write"),"Superseded initialization returns its own unpublished transport without a fan command");});
        LifecycleCase("onex-original-invalid-duty",backend=>{OpenPublic(backend);foreach(var value in new[]{double.NaN,double.PositiveInfinity,double.NegativeInfinity,-1,101}){var count=LifecycleIoPorts.Rows.Count;Reject(()=>Duty(backend,value),"ArgumentOutOfRangeException");Check(LifecycleIoPorts.Rows.Count==count,"Invalid duty cannot reach original port IO");}ClosePublic(backend);});
        LifecycleCase("onex-original-foreign-open-marker",backend=>{var device=LifecycleDevice(backend);Set(device,"DeviceOpen",true);try{Reject(()=>OpenPublic(backend),"foreign or partially open");Check(Get(device,"DeviceOpen")is true&&LifecycleIoPorts.Rows.Count==0&&Get(backend,"HcOpenInvocationStarted")is false,"Rejected foreign marker cannot initialize/dispose transport or acquire fake close debt");}finally{Set(device,"DeviceOpen",false);}});
        LifecycleCase("onex-original-manager-busy-close-pending",backend=>{OpenPublic(backend);Invoke(backend,"OpenEvents");var factory=Hc!.GetType("HandheldCompanion.Managers.ManagerFactory",true)!;var manager=factory.GetField("powerProfileManager",All)!.GetValue(null)!;var status=manager.GetType().GetProperty("Status",All)!;status.SetValue(manager,Enum.ToObject(status.PropertyType,16));try{Reject(()=>ClosePublic(backend),"HC_CLOSE_PENDING");Check(LibraryField.GetValue(null)is null&&Get(Lifecycle(backend),"HasResources")is false&&Get(backend,"HcManagerCleanupConfirmed")is false,"Returned transport cannot fabricate manager cleanup while actual status remains Busy");}finally{status.SetValue(manager,Enum.ToObject(status.PropertyType,0));ClosePublic(backend);}});
        LifecycleCase("onex-original-duty-golden-rounding",backend=>{OpenPublic(backend);var n=0;foreach(var row in new(double percent,byte raw)[]{(0,0),(0.5,1),(6.25,12),(12.5,23),(25,46),(50,92),(99,182),(100,184)}){Duty(backend,row.percent);Packet(0x4b,row.raw,n++*12);}ClosePublic(backend);});
    }
}
