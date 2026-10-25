using System.IO;
using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Text.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Managers;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    static int MutateEventAdmission(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);
        using var assembly=AssemblyDefinition.ReadAssembly(source);
        var method=assembly.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="RequireOpenEventsAdmissionCurrent");
        Check(method.ReturnType.FullName=="System.Void"&&method.Parameters.Count==2&&method.HasBody,"Exact original OpenEvents validation method is required");
        method.Body.Instructions.Clear();method.Body.ExceptionHandlers.Clear();method.Body.Variables.Clear();method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Ret));
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);assembly.Write(target);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list)=>list.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        static string Canon(MethodDefinition m)=>m.FullName+"\n"+m.Attributes+"\n"+(m.HasBody?string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand))+"\n"+string.Join("\n",m.Body.ExceptionHandlers.Select(h=>h.ToString())):"no-body");
        using var original=AssemblyDefinition.ReadAssembly(source);using var after=AssemblyDefinition.ReadAssembly(target);
        var a=Types(original.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);var b=Types(after.MainModule.Types).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Canon);
        Check(a.Keys.Order().SequenceEqual(b.Keys.Order()),"Event validation fault adds/removes no method");
        var changed=a.Keys.Where(k=>a[k]!=b[k]).ToArray();Check(changed.Length==1&&changed[0]==method.FullName,"Only the event admission validation method changes");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation="disconnect only OpenEvents admission validation; preserve actual prepare/finalize, subscription, rollback, original lifecycle and feedback",nativeIo=false}));
        return 0;
    }
    static Exception RejectEventAdmission(Action action)
    {
        Exception? failure=null;try{action();}catch(Exception error){failure=error;}
        Check(failure is not null,"Obsolete OpenEvents must be refused before publishing ready or retaining handlers");return failure!;
    }
    static void RunEventAdmissionCases()
    {
        foreach(var type in new[]{typeof(OneXPlayerX1AMD),typeof(AYANEOFlipDS)})
        {
            foreach(var kind in new[]{"sleep","close","timeout","marker","generation","device"})
            Run("events-admission-between-"+kind+":"+type.Name,()=>
            {
                var d=Model(type);var b=Backend(d);Open(b);var preparation=PrepareEvents(b);
                var epoch=(long)Get(b,"suspendGateEpoch")!;var profileQueue=Get(b,"externalProfileQueue")!;
                try
                {
                    if(kind=="sleep")Invoke(b,"BlockWritesForSuspend");
                    else if(kind=="close")Invoke(b,"BlockWritesForClose");
                    else if(kind=="timeout")Set(b,"operationTimedOut",true);
                    else if(kind=="marker")d.LoseMarker();
                    else if(kind=="generation")Invoke(profileQueue,"BeginSession");
                    else Set(b,"device",Model(type));
                    RejectEventAdmission(()=>FinalizeEvents(b,preparation));
                    Check(ManagerFactory.powerProfileManager.Subscribers==0&&Get(b,"HcOpenEventsInvoked")is not true,"Stale event finalization must not subscribe or publish ready");
                }
                finally{Set(b,"device",d);d.ForeignMarker();Set(b,"operationTimedOut",false);Set(b,"suspendGateEpoch",epoch);Set(b,"closeWriteBlocked",0);Close(b);}
            });
            foreach(var trigger in new[]{"Applied","Discarded"})
            Run("events-admission-during-subscribe-"+trigger+":"+type.Name,()=>
            {
                var b=Backend(Model(type));Open(b);var preparation=PrepareEvents(b);
                ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage==trigger){ManagerFactory.powerProfileManager.PublishApplied(new HandheldCompanion.Misc.PowerProfile());Invoke(b,"BlockWritesForSuspend");}};
                try
                {
                    RejectEventAdmission(()=>FinalizeEvents(b,preparation));
                    Check(ManagerFactory.powerProfileManager.Subscribers==0&&Get(b,"HcOpenEventsInvoked")is not true,"Superseded subscription must roll back its own handlers and ready flag");
                    Check(Get(Get(b,"externalProfileQueue")!,"HasPendingForSelfTest")is not true,"Rejected event admission cannot retain actionable profile work");
                }
                finally{ManagerFactory.powerProfileManager.OnAdd=null;Close(b);}
            });
            Run("events-admission-rollback-preserves-new-generation:"+type.Name,()=>
            {
                var b=Backend(Model(type));Open(b);var preparation=PrepareEvents(b);var queue=Get(b,"externalProfileQueue")!;long replacement=0;
                ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage=="Applied")replacement=(long)Invoke(queue,"BeginSession")!;};
                try
                {
                    RejectEventAdmission(()=>FinalizeEvents(b,preparation));
                    Check(ManagerFactory.powerProfileManager.Subscribers==0&&(long)Get(queue,"CurrentGeneration")! ==replacement,"Rollback removes only its own handlers and cannot invalidate a newer profile generation");
                    ManagerFactory.powerProfileManager.OnAdd=null;FinalizeEvents(b,PrepareEvents(b));
                    Check(Get(b,"HcOpenEventsInvoked")is true&&ManagerFactory.powerProfileManager.Subscribers==2,"The newer generation remains usable after old-admission rollback");
                }
                finally{ManagerFactory.powerProfileManager.OnAdd=null;Close(b);}
            });
            Run("events-admission-healthy-public-entry:"+type.Name,()=>
            {
                var b=Backend(Model(type));Open(b);SeedEventsContext(b);Set(b,"staThread",Thread.CurrentThread);
                try
                {
                    Invoke(b,"OpenEvents");
                    Check(Get(b,"HcOpenEventsInvoked")is true&&ManagerFactory.powerProfileManager.Subscribers==2,"Actual public OpenEvents must complete a current preparation");
                }
                finally{Close(b);}
            });
            Run("events-admission-old-preparation-cannot-publish-new-open:"+type.Name,()=>
            {
                var b=Backend(Model(type));Open(b);var old=PrepareEvents(b);Set(b,"staThread",Thread.CurrentThread);Invoke(b,"Close",false);
                Set(b,"oneXFanOpenAdmissionEpoch",Get(b,"suspendGateEpoch"));Invoke(b,"BeginOpenAttemptEvidence");Open(b);var current=PrepareEvents(b);
                try
                {
                    RejectEventAdmission(()=>FinalizeEvents(b,old));
                    Check(ManagerFactory.powerProfileManager.Subscribers==0&&Get(b,"HcOpenEventsInvoked")is not true,"An old preparation cannot publish the new Open as ready");
                    FinalizeEvents(b,current);Check(Get(b,"HcOpenEventsInvoked")is true&&ManagerFactory.powerProfileManager.Subscribers==2,"The current preparation still completes after refusing the old one");
                }
                finally{Close(b);}
            });
        }
        foreach(var replace in new[]{false,true})
        Run("events-admission-public-insertion-wait:"+(replace?"generation-change":"healthy"),()=>
        {
            var d=new ROGAlly();var b=Backend(d);Open(b);SeedEventsContext(b);Set(b,"staThread",Thread.CurrentThread);var queue=Get(b,"externalProfileQueue")!;var admitted=(long)Get(queue,"CurrentGeneration")!;
            d.OnInsert=()=>{if(replace)Invoke(queue,"BeginSession");};
            try
            {
                if(replace)RejectEventAdmission(()=>Invoke(b,"OpenEvents"));else Invoke(b,"OpenEvents");
                var operation=Get(b,"LastDeviceOpenOperation")!;
                Check(operation is not null&&(long)Get(operation,"SessionGeneration")! ==admitted,"Actual insertion/wait record must retain the admitted generation across raw device insertion");
                Check(Trace.Calls.Contains("Insertion.Terminal:True")&&d.Inserted,"Actual public begin/wait/finalize path entered its explicit inert insertion terminal");
                Check((Get(b,"HcOpenEventsInvoked")is true)==!replace&&ManagerFactory.powerProfileManager.Subscribers==(replace?0:2),"Only the same admitted insertion may publish event readiness");
            }
            finally{d.OnInsert=null;Close(b);}
        });
        Run("events-admission-rollback-remove-failure-retains-close-debt",()=>
        {
            var b=Backend(new AYANEOFlipDS());Open(b);var preparation=PrepareEvents(b);
            ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage=="Applied")Invoke(b,"BlockWritesForSuspend");};
            ManagerFactory.powerProfileManager.OnRemove=stage=>{if(stage=="Applied")throw new IOException("PROFILE_REMOVE_THROW");};
            try
            {
                var failure=RejectEventAdmission(()=>FinalizeEvents(b,preparation));
                Check(failure.ToString().Contains("PROFILE_REMOVE_THROW")&&failure.ToString().Contains("superseded"),"Rollback must retain both the original supersession and its cleanup failure");
                Check(ManagerFactory.powerProfileManager.Subscribers==1&&ReferenceEquals(Get(b,"subscribedPowerProfileManager"),ManagerFactory.powerProfileManager)&&Get(b,"HcOpenEventsInvoked")is not true,"Incomplete rollback preserves exact handler cleanup debt without publishing ready");
                ManagerFactory.powerProfileManager.OnRemove=null;ManagerFactory.powerProfileManager.OnAdd=null;Close(b);
                Check(ManagerFactory.powerProfileManager.Subscribers==0&&Get(b,"subscribedPowerProfileManager")is null,"Existing Close retries and settles the retained registration");
            }
            finally{ManagerFactory.powerProfileManager.OnAdd=null;ManagerFactory.powerProfileManager.OnRemove=null;Close(b);}
        });
        Run("events-admission-nonpower-rejection-uses-existing-recovery",()=>
        {
            var b=Backend(new AYANEOFlipDS());Open(b);SeedEventsContext(b);Set(b,"staThread",Thread.CurrentThread);var engine=Engine(b);Set(Get(engine,"state")!,"OpenCalled",true);Set(Get(engine,"state")!,"State","Open");
            ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage=="Applied")Invoke(Get(b,"externalProfileQueue")!,"BeginSession");};
            try
            {
                var failure=RejectEventAdmission(()=>Invoke(b,"OpenEvents"));ManagerFactory.powerProfileManager.OnAdd=null;
                Check(Get(failure,"Code")?.ToString()=="HC_OPEN_EVENTS_SUPERSEDED","A profile-only replacement cannot impersonate an announced power owner");
                Invoke(engine,"RecoverAfterHardwareFailure",failure);
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(b,"IsOpen")is false&&Get(snapshot,"State")?.ToString()!="Ready"&&ManagerFactory.powerProfileManager.Subscribers==0,"A non-power event failure still uses the existing recovery and retires its actual device");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("superseded")==true,"Real recovery feedback retains the non-power admission cause");
            }
            finally{ManagerFactory.powerProfileManager.OnAdd=null;EndEngine(engine);Close(b);}
        });
        Run("events-admission-public-failure-reaches-existing-engine-feedback",()=>
        {
            var b=Backend(new AYANEOFlipDS());Open(b);SeedEventsContext(b);Set(b,"staThread",Thread.CurrentThread);var engine=Engine(b);Set(Get(engine,"state")!,"OpenCalled",true);Set(Get(engine,"state")!,"State","Open");
            ManagerFactory.powerProfileManager.OnAdd=stage=>{if(stage=="Applied")Invoke(b,"BlockWritesForSuspend");};
            try
            {
                var failure=RejectEventAdmission(()=>Invoke(b,"OpenEvents"));ManagerFactory.powerProfileManager.OnAdd=null;
                var before=Trace.Calls.Count;
                Invoke(engine,"RecoverAfterHardwareFailure",failure);
                Check(Get(b,"IsOpen")is true&&!Trace.Calls.Skip(before).Any(call=>call.StartsWith("FAN:")||call=="OpenLib.Dispose"||call=="RGB.Release"),"A power-superseded OpenEvents error cannot become a second close/restore owner");
                var snapshot=Invoke(engine,"SnapshotForRead")!;
                Check(Get(snapshot,"State")?.ToString()!="Ready"&&Get(snapshot,"OpenEventsCalled")is not true,"Actual recovery/public snapshot cannot publish the superseded OpenEvents as Ready");
                Check(Get(snapshot,"LastError")?.ToString()?.Contains("superseded")==true,"Existing public state retains the actual event-admission failure cause");
                Check(ManagerFactory.powerProfileManager.Subscribers==0,"Existing engine recovery leaves no obsolete profile handlers");
            }
            finally{ManagerFactory.powerProfileManager.OnAdd=null;EndEngine(engine);Close(b);}
        });
        Run("events-admission-inherited-foreign-before-raw-events",()=>
        {
            var d=new AYANEOFlipDS();var b=Backend(d);Open(b);var owned=Library;var foreign=new HandheldCompanion.OpenLibSys();foreign.InitializeOls();EcLibrary.SetValue(null,foreign);Trace.Calls.Clear();
            try
            {
                RejectEventAdmission(()=>PrepareEvents(b));
                Check(!Trace.Calls.Contains("Base.Events")&&ManagerFactory.powerProfileManager.Subscribers==0,"Foreign EC identity must be rejected before the original mixed OpenEvents boundary");
            }
            finally{EcLibrary.SetValue(null,owned);Close(b);foreign.Dispose();}
        });
    }
}
