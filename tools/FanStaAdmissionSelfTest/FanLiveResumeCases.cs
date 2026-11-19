using System.IO;
using System.Reflection;
using System.Text.Json;

// Existing Host fixture: full consumer lifecycle, inert HC shape and Native grant.
// No original HC hardware body, importer authentication or physical claim.
internal static class FanLiveResumeCases
{
    const BindingFlags F = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    static object? Field(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o);
    static object? Prop(object o,string n)=>o.GetType().GetProperty(n,F)!.GetValue(o);
    static void Set(object o,string n,object? v)=>o.GetType().GetField(n,F)!.SetValue(o,v);
    static void SetProp(object o,string n,object? v)=>o.GetType().GetProperty(n,F)!.SetValue(o,v);
    static object? Call(object o,string n,params object?[] args)
    {
        var t=o as Type??o.GetType();
        var m=t.GetMethods(F).Single(x=>x.Name==n&&!x.IsGenericMethod&&x.GetParameters().Length>=args.Length&&x.GetParameters().Skip(args.Length).All(p=>p.IsOptional));
        try{return m.Invoke(o is Type?null:o,args.Concat(Enumerable.Repeat<object?>(Type.Missing,m.GetParameters().Length-args.Length)).ToArray());}
        catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}
    }
    static bool Wait(Func<bool> p)=>SpinWait.SpinUntil(p,4000);
    static object Binding(FanPublicPowerScopePort p)=>typeof(FanPublicPowerScopePort).GetField("binding",F)!.GetValue(p)!;
    static void Member(object o,string n,object v)=>o.GetType().GetField(n,F)!.SetValue(o,v);
    static object Value(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o)!;
    internal static void Run(Assembly image,Action<string,bool,string> check,Action<object> scenario,string name)
    {
        var names=new[]{"resume-live-open-revoked","resume-live-off-pending","resume-live-new-sleep-pending","resume-live-open-baseline-revoked","resume-live-open-baseline-close-debt","resume-live-open-baseline-close-still-owed","resume-live-open-baseline-grant-revoked-during-return","resume-live-open-baseline-staged-grant-revoked"};
        if(!names.Contains(name,StringComparer.Ordinal))throw new ArgumentException("Unknown live continuation scenario");
        Type T(string n)=>image.GetType("YeManFanHost."+n,true)!;
        const string token="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        var logs=Environment.GetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT")!;
        Directory.CreateDirectory(logs);var auth=Path.Combine(logs,"inert-only-authorization.md");
        File.WriteAllText(auth,"hardwareWriteAuthorizationGranted: true\nallMappedFanRoutesAuthorized: true\n");
        var options=T("HostOptions").GetMethod("Parse",F)!.Invoke(null,new object[]{new[]{"--real-backend","--parent-pid",Environment.ProcessId.ToString(),"--allow-hardware-writes","--authorization",auth,"--confirm",token,"--session-token",token,"--shared-power-business-profile","native-f5-deadline-v1"}})!;
        var engine=Activator.CreateInstance(T("FanHostEngine"),F,null,new object?[]{options,null},null)!;
        var backend=Field(engine,"realBackend")!;
        var server=Activator.CreateInstance(T("LocalFanHostServer"),F,null,new object?[]{engine,options,null},null)!;
        var state=Field(engine,"state")!;
        var device=new HandheldCompanion.Devices.GPDWin5();
        var route=Call(T("RealHcBackend"),"ResolveRouteForSelfTest",device.GetType().FullName)!;
        var boundary=Activator.CreateInstance(T("RealHcBackend").GetNestedType("SelfTestHcBoundary",F)!,true)!;
        Call(backend,"InstallHcBoundaryForSelfTest",boundary);
        // Explicit entry: selection/deployment already admitted, actual lifecycle
        // starts below. Keep the real authorization file/token checks.
        Call(backend,"InstallSelfTestRouteSessionForSelfTest",device,route,false);
        Set(backend,"assembly",typeof(FanLiveResumeCases).Assembly);
        Set(backend,"hcInitializationAssembly",typeof(FanLiveResumeCases).Assembly);
        Set(backend,"hcInitializationDevice",device);
        var ports=new List<FanPublicPowerScopePort>();
        Task? reader=null;var ok=true;var revoked=false;var captured=false;
        var early=name.StartsWith("resume-live-open-baseline-",StringComparison.Ordinal);
        var stillOwed=name=="resume-live-open-baseline-close-still-owed";
        var revokeDuringReturn=name=="resume-live-open-baseline-grant-revoked-during-return";
        var revokeBeforeDispatch=name=="resume-live-open-baseline-staged-grant-revoked";
        var oweClose=early&&name!="resume-live-open-baseline-revoked";
        var oldOwnerReturnedBeforeOpen=false;
        var originalDiagnostic=(Action<string,object?>?)Prop(backend,"Diagnostic");
        void C(string text,bool b){ok &= b;check(name+": "+text,b,"");}
        IDisposable Bind(FanPublicPowerScopePort p)=>(IDisposable)Call(engine,"BindImportedPowerForRequest",p,p.Purpose)!;
        bool Returned()=>(int)Field(engine,"resumeWorkerActive")! ==0&&(Task?)Field(engine,"resumeTask")is not{IsCompleted:false};
        FanPublicPowerScopePort Scope(uint purpose,ulong id,object? request=null)
        {
            var p=FanPublicPowerScopePort.Create(image,purpose);ports.Add(p);
            var b=Binding(p);var s=Value(b,"Scope");Member(s,"Low",id);Member(b,"Scope",s);
            if(purpose==2)
            {
                var r=request??Value(b,"Request");
                if(request is null){Member(r,"Low",(1UL<<63)|71UL);Member(r,"High",(ulong)Call(T("FanHostEngine"),"GetTickCount64")!+15000UL);}
                Member(b,"Request",r);
                if(request is not null){var facts=Value(b,"Captured");Member(facts,"Phase",0U);Member(facts,"Flags",3U);Member(facts,"SuspendSerial",4UL);Member(facts,"PublicationRevision",2UL);Member(b,"Captured",facts);}
            }
            return p;
        }
        object? key=null;
        JsonElement View()=>JsonSerializer.SerializeToElement(Call(engine,"ReadAcceptedResumeBusiness",key!));
        JsonElement Curve(string lease,double duty)=>JsonSerializer.SerializeToElement(new{leaseId=lease,nodes=new[]{new{tempC=0,dutyPercent=duty},new{tempC=40,dutyPercent=duty},new{tempC=70,dutyPercent=duty},new{tempC=100,dutyPercent=duty}}});
        try
        {
            var manual=Scope(3,20);
            using(Bind(manual))
            {
                Call(engine,"Open");Call(engine,"OpenEvents");
                var lease=Call(engine,"AcquireControl")!;
                Call(engine,"Enable",Curve((string)Prop(lease,"LeaseId")!,100));
            }
            C("original full engine reaches manual control",device.Opens==1&&device.EventOpens==1&&device.Duties.SequenceEqual(new[]{100d})&&Prop(state,"HardwareWritesObserved")is true);
            Call(engine,"SuspendForSystemPower");
            C("original suspend returns prior written device",device.Closes==1&&!device.IsOpen&&Prop(state,"UnknownState")is false&&Field(engine,"suspendedCurve")is not null);
            var first=Scope(2,31);key=Value(Binding(first),"Request");
            if(early)SetProp(backend,"Diagnostic",(Action<string,object?>)((eventName,detail)=>
            {
                originalDiagnostic?.Invoke(eventName,detail);
                if(revoked||eventName!="hc-operation.phase"||detail is null||!Equals(Prop(detail,"phase"),"open.capture-baseline")||
                    device.Opens!=2||!device.IsOpen||Prop(backend,"HasSessionDeviceReference")is not false)return;
                revoked=true;
                if(oweClose)device.CloseFailuresRemaining=stillOwed?2:1;
                first.Revoke();
                Call(backend,"RequireCurrentPublicPower");
            }));
            else first.BeforeCurrent=()=>
            {
                if(!revoked&&Prop(backend,"IsOpen")is true&&Prop(backend,"HasSessionDeviceReference")is true&&Prop(backend,"HcOpenEventsInvoked")is false)
                {revoked=true;first.Revoke();first.BeforeCurrent=null;}
            };
            Call(engine,"WriteSuspendProtection",61L);
            var a=Prop(engine,"SuspendProtectionAuthorizationSnapshot")!;
            using(Bind(first))Call(engine,"ResumeForSystemPowerWithAdmission",61L,Prop(a,"Value"),Prop(a,"Stamp"));
            if(!Wait(Returned))throw new TimeoutException("Original cancelled worker did not return");
            var business=Field(engine,"acceptedResumeBusiness")!;
            if(early)
            {
                C("revoked after exact device Open before session proof and before Events",revoked&&device.Opens==2&&device.EventOpens==1&&device.Duties.Count==1&&Prop(backend,"HasSessionDeviceReference")is false);
                C("old baseline cannot be used as known Open",!Convert.ToBoolean(Value(business,"OpenReturned")));
                C("first real Close result controls outstanding debt",oweClose?device.IsOpen&&device.Closes==1&&device.FailedCloses==1&&first.Disposals==0:!device.IsOpen&&device.Closes==2&&device.FailedCloses==0);
            }
            else
            {
                C("public cancellation follows completed actual backend Open",revoked&&device.Opens==2&&device.EventOpens==1&&device.Duties.Count==1&&Prop(backend,"IsOpen")is true);
                C("old SDK ownership is retained rather than invented returned",first.Disposals==0&&Convert.ToBoolean(Value(business,"OpenReturned")));
            }
            C("accepted business waits after public revocation",View().GetProperty("phase").GetString()=="AwaitingGrant"&&Equals(Prop(state,"PowerOperationStatus"),"superseded"));
            var next=Scope(2,41,key);
            if(oweClose)device.BeforeOpen=()=>oldOwnerReturnedBeforeOpen=first.Disposals==1;
            if(revokeDuringReturn)device.AfterClose=next.Revoke;
            using(Bind(next))
            {
                C("only original business accepts next Native grant",Call(engine,"StageAcceptedResumeParent",key)is true);
                C("duplicate grant does not mint extra attempts",Call(engine,"StageAcceptedResumeParent",key)is true&&View().GetProperty("hostGrantsObserved").GetInt32()==2);
            }
            C("new grant remains on original Channel before execution",device.Duties.Count==1&&View().GetProperty("phase").GetString()=="PendingHandoff");
            if(name=="resume-live-off-pending")Call(engine,"CancelControlIntent");
            if(name=="resume-live-new-sleep-pending")
            {
                Call(engine,"WriteSuspendProtection",62L);Call(engine,"SuspendForSystemPower");
                captured=Prop(state,"UnknownState")is false&&Equals(Prop(state,"State"),"Suspended")&&device.Closes==2;
                C("new sleep closes the unwritten current session without false lock",captured);
            }
            if(revokeBeforeDispatch)next.Revoke();
            reader=(Task)Call(server,"StartPowerWorkerForSelfTest")!;
            if(revokeBeforeDispatch)
            {
                if(!Wait(()=>View().GetProperty("phase").GetString()!="PendingHandoff"&&next.Disposals==1))
                    throw new TimeoutException("The original Channel did not return the revoked staged grant");
                var discarded=View();
                var waiting=discarded.GetProperty("phase").GetString()=="AwaitingGrant"&&
                    discarded.GetProperty("reason").GetString()=="public-admission-revoked"&&
                    discarded.GetProperty("hostGrantsObserved").GetInt32()==2&&
                    !discarded.GetProperty("grantPending").GetBoolean();
                C("unstarted stale grant returns and leaves the original request waiting",waiting);
                C("discarded grant neither repays its slot nor touches outstanding device resources",
                    next.Disposals==1&&first.Disposals==0&&device.Opens==2&&device.Closes==1&&device.FailedCloses==1&&
                    device.EventOpens==1&&device.Duties.Count==1&&(int)Field(engine,"resumeAttemptCounter")! ==1);
                if(!waiting){scenario(new{name,terminal=discarded,device.Opens,device.Closes,device.FailedCloses,device.EventOpens,device.Duties});return;}
                var fresh=Scope(2,42,key);
                using(Bind(fresh))C("same request accepts a third current grant",Call(engine,"StageAcceptedResumeParent",key)is true);
            }
            if(early)
            {
                if(!Wait(()=>Returned()&&View().GetProperty("phase").GetString()=="Terminal"))throw new TimeoutException("Original Channel did not settle early Open continuation");
                if(stillOwed||revokeDuringReturn)
                {
                    C("failed return or revoked new grant cannot cross Open, Events or Enable",device.Opens==2&&device.EventOpens==1&&device.Duties.SequenceEqual(new[]{100d})&&(int)Field(engine,"resumeAttemptCounter")! ==1);
                    C("old SDK owner retires only on a confirmed return",stillOwed?device.IsOpen&&device.FailedCloses==2&&first.Disposals==0:!device.IsOpen&&device.Closes==2&&first.Disposals==1);
                }
                else
                {
                    C("same business restores after exact return and fresh Open",!Equals(View().GetProperty("reason").GetString(),"continuation-admission-or-resource-return-unproven")&&device.Opens==3&&device.Closes==2&&device.EventOpens==2&&device.Duties.SequenceEqual(new[]{100d,100d})&&Equals(Prop(state,"State"),"Ready"));
                    C(revokeBeforeDispatch?"original deadline and all three grants retained":"original deadline and two grants retained",View().GetProperty("hostGrantsObserved").GetInt32()==(revokeBeforeDispatch?3:2)&&(int)Field(engine,"resumeAttemptCounter")! ==2&&Value(Value(business,"FirstGrant"),"Request").Equals(key));
                    if(oweClose)C("the first SDK borrower is returned before fresh Open",oldOwnerReturnedBeforeOpen);
                }
                scenario(new{name,terminal=View(),device.Opens,device.Closes,device.FailedCloses,device.EventOpens,device.Duties,firstDisposalsBeforeFinalClose=first.Disposals,backendOpen=Prop(backend,"IsOpen"),backendCloseOwed=Prop(backend,"HcCloseIsOwed")});
            }
            else if(name=="resume-live-open-revoked")
            {
                if(!Wait(()=>Returned()&&device.Duties.Count==2))throw new TimeoutException("Original Channel did not resume control");
                C("new grant reuses exact Open and completes actual Events and Enable",device.Opens==2&&device.EventOpens==2&&device.Duties.SequenceEqual(new[]{100d,100d})&&Prop(state,"UnknownState")is false&&Equals(Prop(state,"State"),"Ready"));
                C("continuation keeps original deadline and two actual grants",View().GetProperty("phase").GetString()=="Terminal"&&View().GetProperty("hostGrantsObserved").GetInt32()==2&&(int)Field(engine,"resumeAttemptCounter")! ==2&&Value(Value(business,"FirstGrant"),"Request").Equals(key));
            }
            else
            {
                if(!Wait(()=>View().GetProperty("phase").GetString()=="Terminal"))throw new TimeoutException("Cancelled original business did not retire");
                C("OFF or new sleep never resurrects old control",device.Opens==2&&device.EventOpens==1&&device.Duties.Count==1&&(int)Field(engine,"resumeAttemptCounter")! ==1);
            }
        }
        catch(Exception e){ok=false;check(name+": actual consumer completed",false,e.ToString());}
        finally
        {
            device.BeforeOpen=null;
            device.AfterClose=null;
            if(early)SetProp(backend,"Diagnostic",originalDiagnostic);
            foreach(var p in ports)p.BeforeCurrent=null;
            var returned=Wait(Returned);C("actual accepted worker returns",returned);
            Call(server,"CompletePowerQueueForSelfTest");
            C("original Channel reader returns",reader is null||reader.Wait(4000));
            if(returned)
            {
                Call(engine,"ClearLease");Call(engine,"StopCurveTimer");
                if(device.IsOpen)Call(engine,"CloseHcSessionForLifecycle",false,stillOwed,stillOwed);
                if(stillOwed)
                {
                    Call(engine,"SettleReturnedResumePublicPower");
                    C("later original Close returns the inert SDK resources",!device.IsOpen&&device.Closes==2&&Prop(backend,"HcCloseIsOwed")is false&&Prop(backend,"HcVirtualCloseReturned")is true);
                    var retirement=new{unknown=Prop(state,"UnknownState"),sdkCloseReturned=Prop(backend,"HcVirtualCloseReturned"),physicalOwnership=Prop(state,"OemPhysicalOwnershipConfirmed"),roots=ports.Select(p=>p.Disposals).ToArray()};
                    scenario(new{name,retirement});
                    C("SDK cleanup does not invent OEM physical ownership",Equals(retirement.physicalOwnership,false));
                }
                Call(engine,"StopRecoveryRetry");Call(backend,"Dispose");Call(engine,"Dispose");
            }
            C("actual Host returns imported roots before fixture fallback",ports.All(p=>p.Disposals==1&&p.HandlesClosed));
            foreach(var p in ports)p.Dispose();
            C("owned local handles and event registrations returned",ports.All(p=>p.HandlesClosed)&&HandheldCompanion.Managers.ManagerFactory.powerProfileManager.Subscribers==0);
        }
        scenario(new{name,ok,actualRealBackend=true,actualEngineOpenEventsEnableClose=true,actualOriginalChannel=reader is not null,preAdmittedFactoryAndHcAndNativeArePorts=true,physicalWrites=0,newSleepCloseObserved=captured});
    }
}

namespace HandheldCompanion.Devices
{
    public sealed class GPDWin5
    {
        public bool UseOpenLib=>true;
        public bool IsOpen{get;private set;}
        public bool IsSupported=>true;
        public int Opens,Closes,EventOpens,FailedCloses;
        public int CloseFailuresRemaining;
        public Action? BeforeOpen,AfterClose;
        public List<double> Duties{get;}=new();
        public object[] DevicePowerProfiles{get;}=[new FanLiveProfilePort()];
        public bool IsReady()=>true;
        public bool Open(){BeforeOpen?.Invoke();Opens++;IsOpen=true;return true;}
        public void OpenEvents(){EventOpens++;}
        public void Close(){if(CloseFailuresRemaining>0){CloseFailuresRemaining--;FailedCloses++;throw new InvalidOperationException("Inert HC close has not returned ownership");}Closes++;IsOpen=false;AfterClose?.Invoke();}
        public void SetFanDuty(double percent)=>Duties.Add(percent);
    }
}
public sealed class FanLiveProfilePort
{
    public bool DeviceDefault=>true;
    public Guid Guid=>System.Guid.Empty;
    public HostAsusFanPort FanProfile{get;set;}=new();
}
namespace HandheldCompanion.Managers
{
    public static class ManagerFactory
    {
        public static FanLiveProfileManager powerProfileManager=new();
    }
    public sealed class FanLiveProfileManager
    {
        Action<object,object>? applied;
        Action<object,object>? discarded;
        public int Subscribers=>(applied?.GetInvocationList().Length??0)+(discarded?.GetInvocationList().Length??0);
        public event Action<object,object> Applied{add{applied+=value;}remove{applied-=value;}}
        public event Action<object,object> Discarded{add{discarded+=value;}remove{discarded-=value;}}
        public object CloneProfile(object source)=>new FanLiveProfilePort();
    }
}
