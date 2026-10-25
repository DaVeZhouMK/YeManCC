using System.IO;
using System.Collections;
static partial class JoinedFactoryProbe
{
    static bool SawOriginalMsiBaseOpen;
    static void MsiCase(string id,Action<object> body,string product="MS-1T41",string expected="ClawA1M",int firmware=0x167)
    {
        Run(id,()=>
        {
            LifecycleIoPorts.Reset();MsiLifecycleIoPorts.Reset();Reset("MICRO-STAR INTERNATIONAL CO., LTD.",product,"unused");var backend=Backend();try
            {
                Ready(backend,expected,true);MsiLifecycleIoPorts.Device=LifecycleDevice(backend);MsiLifecycleIoPorts.Firmware=firmware;AuthorizeFixture(backend);body(backend);
                Check(!OriginalConstructorPorts.Calls.Any(c=>c.StartsWith("life.forbidden.",StringComparison.Ordinal)),"MSI narrow fan must not enter general power/ROM/setting lifecycle");
                Check(LifecycleIoPorts.OpenTransportCount==0&&LibraryField.GetValue(null)is null,"Original MSI base transport must be returned");Check(Get(backend,"msiFanLifecycle")is not object currentLife||Get(currentLife,"HasResources")is false,"MSI cannot keep unreported lifecycle resources");ManagersIdle();
            }
            finally
            {
                SawOriginalMsiBaseOpen|=MsiLifecycleIoPorts.Rows.Any(r=>r.Method=="Get_WMI");
                MsiLifecycleIoPorts.OnIo=null;MsiLifecycleIoPorts.ReadFailure=MsiLifecycleIoPorts.WriteFailure="";LifecycleIoPorts.WriteFailures=LifecycleIoPorts.DisposeFailures=0;
                try{if(Get(backend,"msiFanLifecycle")is object life&&Get(life,"HasResources")is true)ClosePublic(backend);}finally{End(backend);}
            }
        });
    }
    static object MsiRelease(object backend)
    {
        var result=MsiLifecycleIoPorts.Get(LifecycleDevice(backend),"LastMsiFanRelease");Check(result is not null,"Original MSI close must enter release and publish a current structured receipt");return result!;
    }
    static void RunMsiLifecycleCases()
    {
        foreach(var row in new(string product,string name,int firmware)[]{("MS-1T41","ClawA1M",0x167),("MS-1T52","ClawA2VM",0x219),("MS-1T8K","ClawBZ2EM",0x308),("MS-1T91","ClawCG3EM",0x411)})
            MsiCase("msi-original-public-cycle:"+row.name,backend=>
            {
                OpenPublic(backend);var device=LifecycleDevice(backend);Check((int)MsiLifecycleIoPorts.Get(device,"Firmware")! == row.firmware,"Original HID readiness must supply the actual narrow fan firmware identity");
                Check(Get(backend,"msiFanLifecycle")is not null&&Get(Get(backend,"msiFanLifecycle")!,"IsOpen")is true,"Already admitted original MSI model selects narrow lifecycle");
                Invoke(backend,"OpenEvents");Check(Subscriptions("Applied")==1&&Subscriptions("Discarded")==1,"MSI public events attach only the actual two profile handlers");ClosePublic(backend);
                var release=MsiRelease(backend);Check(Get(release,"Status")?.ToString()=="ok"&&Get(release,"FirmwareKnown")is true&&Get(release,"OperationToken")?.ToString()?.Length>0,"Actual original MSI release produces a bound result with observed firmware");
                Check(Get(backend,"hcCloseReleaseSettlement")?.ToString()=="proven","Actual Host settles the original current structured release");
                Check(MsiLifecycleIoPorts.Full==5&&MsiLifecycleIoPorts.Software==17,"Original release clears only bit7 in both actual consumer steps");
                var writes=MsiLifecycleIoPorts.Rows.Where(r=>r.Stage=="wmi-write").ToArray();Check(writes.Length==2&&writes.Select(r=>r.Index).SequenceEqual(new[]{152,212})&&writes.All(r=>r.Data.Length==32&&r.Data.Skip(2).All(b=>b==0)),"MSI release keeps actual CTW order and zero-padded original packets");
                Check(Subscriptions("Applied")==0&&Subscriptions("Discarded")==0&&Get(backend,"HcManagerCleanupConfirmed")is true,"MSI public close removes its actual event handlers and confirms original idle managers");
            },row.product,row.name,row.firmware);
        foreach(var row in new(string product,string name,int firmware)[]{("MS-1T41","ClawA1M",0x167),("MS-1T52","ClawA2VM",0x219),("MS-1T8K","ClawBZ2EM",0x308),("MS-1T91","ClawCG3EM",0x411)})
            MsiCase("msi-original-public-reopen:"+row.name,backend=>{for(var i=0;i<2;i++){OpenPublic(backend);Invoke(backend,"OpenEvents");ClosePublic(backend);}Check(MsiLifecycleIoPorts.Rows.Count(r=>r.Stage=="hid-enumerate")==1&&MsiLifecycleIoPorts.Rows.Count(r=>r.Stage=="wmi-read"&&r.Method=="Get_WMI")==2,"MSI reopen reuses only connected original HID metadata but rechecks WMI readiness");Check(Convert.ToInt32(MsiLifecycleIoPorts.Get(LifecycleDevice(backend),"ReleaseMsiFanControlInvocationCount"))==2&&Subscriptions("Applied")==0,"Each reopened original session returns once and unsubscribes");},row.product,row.name,row.firmware);
        MsiCase("msi-original-disconnected-hid-rebind",backend=>{OpenPublic(backend);ClosePublic(backend);foreach(var key in MsiLifecycleIoPorts.Connections.Keys.ToArray())MsiLifecycleIoPorts.Connections[key]=false;MsiLifecycleIoPorts.Firmware=0x166;OpenPublic(backend);Check(MsiLifecycleIoPorts.Rows.Count(r=>r.Stage=="hid-enumerate")==2&&(int)MsiLifecycleIoPorts.Get(LifecycleDevice(backend),"Firmware")! ==0x166,"Disconnected original HID must rebind and refresh actual firmware");ClosePublic(backend);});
        MsiCase("msi-original-unknown-firmware-honest",backend=>{OpenPublic(backend);ClosePublic(backend);var release=MsiRelease(backend);Check(Get(release,"FirmwareKnown")is false&&Get(release,"Firmware")?.ToString()=="unknown","Missing actual firmware cannot become measured zero");},firmware:0);
        foreach(var kind in new[]{"TransportFailed","ShortPayload","FlagNotSuccessful"})MsiCase("msi-original-open-read-failure:"+kind,backend=>{MsiLifecycleIoPorts.ReadFailure="Get_WMI";MsiLifecycleIoPorts.ReadOutcome=kind;Reject(()=>OpenPublic(backend),"WMI readiness read failed or truncated");var device=LifecycleDevice(backend);Check(MsiLifecycleIoPorts.Get(device,"DeviceOpen")is false&&MsiLifecycleIoPorts.Get(device,"ClawOpen")is false&&Get(backend,"HcOpenInvocationStarted")is false,"Unusable original WMI result cannot publish MSI ownership");Check(MsiLifecycleIoPorts.Rows.All(r=>r.Stage!="wmi-write"),"Readiness failure cannot issue a fan or unrelated setting write");});
        MsiCase("msi-original-open-superseded",backend=>{MsiLifecycleIoPorts.OnIo=stage=>{if(stage=="wmi-read"&&MsiLifecycleIoPorts.Rows.Last().Method=="Get_WMI"){MsiLifecycleIoPorts.OnIo=null;Set(backend,"suspendGateEpoch",1L);}};Reject(()=>OpenPublic(backend),"HC_RESUME_SUPERSEDED");Check(MsiLifecycleIoPorts.Get(LifecycleDevice(backend),"DeviceOpen")is false&&MsiLifecycleIoPorts.Rows.All(r=>r.Stage!="wmi-write"),"Superseded actual MSI preparation returns markers before any command");});
        foreach(var method in new[]{"Get_Data","Get_AP"})MsiCase("msi-original-release-read-unavailable:"+method,backend=>{OpenPublic(backend);MsiLifecycleIoPorts.ReadFailure=method;ClosePublic(backend);var release=MsiRelease(backend);Check(Get(release,"Status")?.ToString()=="unknown"&&Get(backend,"hcCloseReleaseSettlement")?.ToString()=="unknown"&&Get(backend,"OemRestoreVerified")is false,"Unavailable original release read must remain unknown through Host settlement");var blocked=method=="Get_Data"?152:212;Check(MsiLifecycleIoPorts.Rows.All(r=>r.Stage!="wmi-write"||r.Index!=blocked),"Failed original bit read cannot write guessed data");});
        MsiCase("msi-original-release-write-transport-failed",backend=>{OpenPublic(backend);MsiLifecycleIoPorts.WriteFailure="Set_Data";ClosePublic(backend);var release=MsiRelease(backend);Check(Get(release,"Status")?.ToString()=="failed"&&Get(backend,"hcCloseReleaseSettlement")?.ToString()=="failed"&&Get(backend,"OemRestoreVerified")is false,"Original failed write cannot become a proven automatic return");});
        MsiCase("msi-original-release-readback-mismatch",backend=>{OpenPublic(backend);MsiLifecycleIoPorts.IgnoreWrite=true;ClosePublic(backend);Check(Get(backend,"hcCloseReleaseSettlement")?.ToString()=="failed"&&MsiLifecycleIoPorts.Full==0x85&&MsiLifecycleIoPorts.Software==0x91&&Get(backend,"OemRestoreVerified")is false,"Accepted WMI transport without changed original readback remains failed");});
        MsiCase("msi-original-release-readback-unavailable",backend=>{OpenPublic(backend);MsiLifecycleIoPorts.ReadFailure="Get_Data";MsiLifecycleIoPorts.ReadFailureAfter=1;ClosePublic(backend);Check(Get(backend,"hcCloseReleaseSettlement")?.ToString()=="unknown"&&Get(backend,"OemRestoreVerified")is false,"Actual unreadable post-write verification cannot become proven");});
        MsiCase("msi-original-already-automatic-no-write",backend=>{MsiLifecycleIoPorts.Full=5;MsiLifecycleIoPorts.Software=17;OpenPublic(backend);ClosePublic(backend);Check(MsiLifecycleIoPorts.Rows.All(r=>r.Stage!="wmi-write")&&Get(backend,"hcCloseReleaseSettlement")?.ToString()=="proven","Original already-target result avoids writes and still returns a current bound receipt");});
        foreach(var field in new[]{"OperationToken","BootIdentity"})MsiCase("msi-original-foreign-release-receipt:"+field,backend=>{OpenPublic(backend);ClosePublic(backend);var release=MsiRelease(backend);Set(release,field,"foreign-fixture");Sta(backend,()=>Invoke(backend,"SettleMsiCloseRelease"),"msi-stale-receipt");Check(Get(backend,"hcCloseReleaseSettlement")?.ToString()=="unknown"&&Get(backend,"OemRestoreVerified")is false,"Original successful release from another operation/boot must be refused by Host");});
        MsiCase("msi-original-foreign-open-marker",backend=>{var device=LifecycleDevice(backend);var field=device.GetType().BaseType!.GetField("DeviceOpen",All)!;if(field is null)field=Hc!.GetType("HandheldCompanion.Devices.IDevice",true)!.GetField("DeviceOpen",All)!;field.SetValue(device,true);try{Reject(()=>OpenPublic(backend),"foreign or partially open");Check((bool)field.GetValue(device)!&&MsiLifecycleIoPorts.Rows.All(r=>r.Stage!="wmi-write"&&r.Method!="Get_WMI"),"Foreign original MSI marker cannot be adopted or cleared");}finally{field.SetValue(device,false);}});
    }
}
