using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using YeManFanBackend.Capabilities;
using YeManFanHost;

internal static class PinnedFanHostConsumerChecks
{
    private const string Token="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    private sealed class Terminal : IPinnedFanTransport
    {
        public string EndpointIdentity {get;}="fixture:source-host:"+Guid.NewGuid().ToString("N");
        public bool IsReady {get;private set;}=true;
        internal readonly Dictionary<ushort,byte> Values=new();
        internal readonly List<(ushort Register,byte Value)> Writes=new();
        internal Action<ushort,byte>? BeforeWrite;
        internal bool IgnoreAutomatic {get;set;}
        private readonly ushort control;
        internal Terminal(PinnedFanDescriptor source){control=source.ControlRegister;Values[control]=0;Values[source.DutyRegister]=0;}
        public byte ReadByte(ushort register)=>IsReady?Values[register]:throw new IOException("closed terminal");
        public void WriteByte(ushort register,byte value)
        {if(!IsReady)throw new IOException("closed terminal");Writes.Add((register,value));BeforeWrite?.Invoke(register,value);if(!(IgnoreAutomatic&&register==control&&value==0))Values[register]=value;}
        public void Dispose()=>IsReady=false;
    }
    private static void Wire(HostState snapshot,string directory,string name,Action<bool,string> check)
    {
        var json=JsonSerializer.SerializeToElement(new {ok=true,state=snapshot},JsonDefaults.Options);
        var state=json.GetProperty("state");var claim=state.GetProperty("recoveryClaimEvidence");
        check(state.TryGetProperty("sourceFanGeneration",out var generation) &&
            claim.GetProperty("sourceFanGeneration").GetRawText()==generation.GetRawText(),"source-wire:"+name+"-same-snapshot-generation-present-even-null");
        File.WriteAllText(Path.Combine(directory,"source-wire-"+name+".json"),json.GetRawText());
    }
    private static FanNode[] Curve=>new[]{new FanNode(0,0),new FanNode(30,20),new FanNode(70,70),new FanNode(100,100)};
    private static void Reject(Action action,Action<bool,string> check,string name)
    {bool refused=false;try{action();}catch(Exception error)when(error is FanApiException or InvalidOperationException){refused=true;}check(refused,name);}
    private static RealHcBackend Host(string authorization,bool allow=true,string? confirmation=Token)=>
        new(HostOptions.Parse(Array.Empty<string>()) with {AllowHardwareWrites=allow,AuthorizationPath=authorization,Confirmation=confirmation,SessionToken=Token});
    private static void Allow(string path)=>File.WriteAllText(path,"hardwareWriteAuthorizationGranted: true\nallMappedFanRoutesAuthorized: true\n");
    private static JsonElement EnableBody(FanLease lease)=>JsonSerializer.SerializeToElement(new {
        leaseId=lease.LeaseId,nodes=Curve.Select(x=>new{tempC=x.TempC,dutyPercent=x.DutyPercent}).ToArray()});
    private static FanHostEngine Engine(string authorization,string directory,PinnedFanDescriptor source,List<Terminal> terminals)
    {
        var options=HostOptions.Parse(Array.Empty<string>()) with {RealBackend=true,AllowHardwareWrites=true,
            AuthorizationPath=authorization,Confirmation=Token,SessionToken=Token};
        var engine=new FanHostEngine(options,new HostDiagnosticLog(directory));
        engine.RealBackendForSelfTest!.InstallPinnedSourceConsumerForSelfTest(source.Manufacturers[0],source.Products[0],()=>{
            var terminal=new Terminal(source);terminals.Add(terminal);return terminal;});
        return engine;
    }
    private static void RunEngine(Action<bool,string> check,string directory,PinnedFanDescriptor source)
    {
        {
            var oldRoot=Environment.GetEnvironmentVariable(HostDiagnosticLog.SelfTestLogRootEnvVar);
            var ownRoot=Path.Combine(directory,"completed-parent-journal");Directory.CreateDirectory(ownRoot);
            Environment.SetEnvironmentVariable(HostDiagnosticLog.SelfTestLogRootEnvVar,ownRoot);
            try
            {
                var auth=Path.Combine(directory,"completed-parent-auth.txt");Allow(auth);var terminals=new List<Terminal>();
                using var engine=Engine(auth,Path.Combine(directory,"completed-parent-log"),source,terminals);
                engine.Handshake();engine.Open();engine.OpenEvents();var lease=engine.AcquireControl();engine.Enable(EnableBody(lease));
                engine.RealBackendForSelfTest!.DeliverPinnedSourceTemperatureForSelfTest(50);
                var journal=engine.PersistRecoveryCycleForSelfTest(0,DateTime.UtcNow.AddMinutes(-2),2);
                var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;
                var complete=typeof(FanHostEngine).GetMethod("CompleteRecoveryCycleLocked",flags)!;
                check((bool)complete.Invoke(engine,new object[]{"source-fixture-recovered"})!,"source-parent:original-cycle-is-durably-completed-"+source.DeviceId);
                var original=File.ReadAllBytes(journal);var cycle=engine.RecoveryCycleIdForReport;
                var settle=typeof(FanHostEngine).GetMethod("TrySettleCompletedSourceParentExit",flags)!;
                check(!(bool)settle.Invoke(engine,null)!,"source-parent:completed-cycle-does-not-excuse-active-source-owner-"+source.DeviceId);
                var restore=typeof(FanHostEngine).GetMethod("RestoreHardware",flags)!;
                check((bool)restore.Invoke(engine,new object[]{false,true,false,"source-fixture-fault-cleanup"})!,
                    "source-parent:original-restore-retires-current-source-"+source.DeviceId);var retired=engine.Snapshot();
                check(retired.State=="AwaitingControl"&&retired.SourceFanClosed&&retired.SourceFanAutomaticReturned,
                    "source-parent:recovered-source-is-retired-but-listener-not-yet-stopped-"+source.DeviceId);
                var state=(HostState)typeof(FanHostEngine).GetField("state",flags)!.GetValue(engine)!;
                state.UnknownState=true;
                check(!(bool)settle.Invoke(engine,null)!,"source-parent:unknown-current-state-cannot-use-old-completion-"+source.DeviceId);
                state.UnknownState=false;state.HardwareWritesEnabled=true;
                check(!(bool)settle.Invoke(engine,null)!,"source-parent:current-write-permission-cannot-use-old-completion-"+source.DeviceId);
                state.HardwareWritesEnabled=false;
                var foreign=System.Text.Json.Nodes.JsonNode.Parse(original)!;foreign["cycleId"]="different-completed-cycle";
                File.WriteAllText(journal,foreign.ToJsonString());
                check(!(bool)settle.Invoke(engine,null)!,"source-parent:foreign-durable-cycle-cannot-settle-current-owner-"+source.DeviceId);
                File.WriteAllBytes(journal,original);int writes=terminals.Sum(t=>t.Writes.Count);
                var ack=JsonSerializer.SerializeToElement(engine.BeginParentExitHandoff(),JsonDefaults.Options);
                check(ack.GetProperty("recoveryAccepted").GetBoolean()&&!ack.GetProperty("hostWillRemainResident").GetBoolean()
                    &&ack.GetProperty("acceptedRecoveryCycleId").GetString()==cycle,
                    "source-parent:already-retired-owner-finishes-with-same-completed-cycle-"+source.DeviceId);
                var stopped=engine.Snapshot();
                check(stopped.State=="Stopped"&&LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(stopped)
                    &&!stopped.OemPhysicalOwnershipConfirmed&&File.ReadAllBytes(journal).SequenceEqual(original),
                    "source-parent:honest-stop-preserves-original-journal-and-no-physical-claim-"+source.DeviceId);
                engine.BeginParentExitHandoff();
                check(writes==terminals.Sum(t=>t.Writes.Count)&&terminals.Count==1,
                    "source-parent:duplicate-parent-exit-does-not-reopen-or-write-"+source.DeviceId);
            }
            finally{Environment.SetEnvironmentVariable(HostDiagnosticLog.SelfTestLogRootEnvVar,oldRoot);}
        }
        {
            var auth=Path.Combine(directory,"engine-normal-auth.txt");Allow(auth);var terminals=new List<Terminal>();
            using var engine=Engine(auth,Path.Combine(directory,"engine-normal-log"),source,terminals);
            engine.Handshake();Wire(engine.Snapshot(),directory,"unopened",check);engine.Open();engine.OpenEvents();var backend=engine.RealBackendForSelfTest!;
            var lease=engine.AcquireControl();engine.Enable(EnableBody(lease));
            var armed=engine.Snapshot();
            check(armed.HardwareWritesEnabled&&!armed.HardwareWritesObserved&&armed.SourceFanMode,"source-engine:arming-does-not-invent-write-history");
            backend.DeliverPinnedSourceTemperatureForSelfTest(50);var active=engine.Snapshot();
            check(active.HardwareWritesObserved&&active.SourceFanClosePending&&!active.SourceFanClosed,"source-engine:actual-issued-write-appears-in-existing-snapshot");
            check(!LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(active),"source-engine:parent-cannot-exit-with-active-source-owner");
            Wire(active,directory,"active",check);
            var oldGen=active.SourceFanGeneration!.Value;var oldOp=backend.CurrentControlOpenOperationId;
            var oldCallback=backend.CapturePinnedSourceTemperatureForSelfTest();
            engine.Disable(JsonSerializer.SerializeToElement(new{}));var retired=engine.Snapshot();
            Wire(retired,directory,"retired",check);
            check(retired.SourceFanClosed&&retired.SourceFanAutomaticReturned&&!retired.SourceFanClosePending&&terminals[0].IsReady==false,"source-engine:disable-observes-source-auto-return-and-terminal-close");
            check(!retired.HcVirtualCloseReturned&&!retired.HcRestoreCallbackReturned&&!retired.HcDeviceManagerStopCompleted&&!retired.OemPhysicalOwnershipConfirmed,"source-engine:source-release-preserves-honest-hc-and-physical-axes");
            check(retired.OemReleaseActionKind=="proven-by-source-auto-register-and-terminal-close"&&FanHostEngine.IsOemReleaseSafe(true,retired.OemReleaseActionKind),"source-engine:unique-source-release-kind-consumed-by-existing-authority");
            var nextLease=engine.AcquireControl();engine.Enable(EnableBody(nextLease));var successor=engine.Snapshot();
            check(terminals.Count==2&&successor.SourceFanGeneration>oldGen&&!successor.HardwareWritesObserved,"source-engine:explicit-reacquire-opens-new-generation-with-no-invented-history");
            var before=terminals[0].Writes.Count;oldCallback(100);
            check(before==terminals[0].Writes.Count&&terminals[1].Writes.Count==0,"source-engine:retired-sensor-cannot-write-new-owner");
            var binding=new FanHostEngine.ReopenBinding("source-fixture-host","cycle-A",oldGen,oldOp,Environment.TickCount64);
            check(backend.TryObserveReopenReceipt(binding,"source-fixture-host","cycle-A",successor.SourceFanGeneration!.Value,1,out _),"source-engine:actual-reopen-binds-new-source-generation");
            check(!backend.TryObserveReopenReceipt(binding,"foreign-host","cycle-A",successor.SourceFanGeneration.Value,1,out _)&&
                !backend.TryObserveReopenReceipt(binding,"source-fixture-host","cycle-B",successor.SourceFanGeneration.Value,1,out _)&&
                !backend.TryObserveReopenReceipt(binding,"source-fixture-host","cycle-A",oldGen,1,out _),"source-engine:foreign-host-cycle-and-stale-generation-refused");
            backend.DeliverPinnedSourceTemperatureForSelfTest(50);engine.Close();var closed=engine.Snapshot();
            Wire(closed,directory,"closed",check);
            check(closed.State=="Stopped"&&closed.SourceFanClosed&&!closed.SourceFanClosePending&&closed.SourceFanAutomaticReturned,"source-engine:public-close-retires-real-source-owner");
            check(LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(closed),"source-engine:parent-finalizer-accepts-actual-return-plus-exact-source-close");
            closed.SourceFanClosed=false;check(!LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(closed),"source-engine:parent-rejects-missing-source-close-despite-proven-release");
            closed.SourceFanClosed=true;closed.SourceFanAutomaticReturned=false;
            check(!LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(closed),"source-engine:parent-rejects-contradictory-source-return");
        }
        {
            var auth=Path.Combine(directory,"engine-no-write-auth.txt");Allow(auth);var terminals=new List<Terminal>();
            using var engine=Engine(auth,Path.Combine(directory,"engine-no-write-log"),source,terminals);
            engine.Handshake();engine.RealBackendForSelfTest!.Open();
            check(!engine.HcSessionNeverEnteredForSelfTest(),"source-engine:no-session-shortcut-rejects-backend-source-debt");
            engine.Close();var closed=engine.Snapshot();
            check(closed.State=="Stopped"&&!closed.HardwareWritesObserved&&closed.SourceFanClosed&&!closed.SourceFanAutomaticReturned,
                "source-engine:never-written-source-terminal-closes-without-fake-auto-proof");
            check(LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(closed),"source-engine:no-write-parent-completion-still-requires-real-resource-close");
        }
        {
            var auth=Path.Combine(directory,"engine-failed-return-auth.txt");Allow(auth);var terminals=new List<Terminal>();
            using var engine=Engine(auth,Path.Combine(directory,"engine-failed-return-log"),source,terminals);
            engine.Handshake();engine.Open();engine.OpenEvents();var lease=engine.AcquireControl();engine.Enable(EnableBody(lease));
            engine.RealBackendForSelfTest!.DeliverPinnedSourceTemperatureForSelfTest(50);terminals[0].IgnoreAutomatic=true;
            try
            {
                Reject(()=>engine.Close(),check,"source-engine:failed-return-public-close-cannot-complete");var pending=engine.Snapshot();
                check(pending.SourceFanClosePending&&!pending.SourceFanClosed&&pending.UnknownState&&terminals[0].IsReady,
                    "source-engine:unknown-return-retains-exact-source-debt-in-public-state");
                check(!LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(pending),"source-engine:parent-waits-through-failed-source-return");
            }
            finally{terminals[0].IgnoreAutomatic=false;engine.Close();}
            check(engine.Snapshot().State=="Stopped"&&!engine.RecoveryRetryTimerActiveForSelfTest,
                "source-engine:explicit-return-retry-settles-source-owner-and-existing-retry-timer");
        }
    }
    private static void RunPowerAndRelease(Action<bool,string> check,string directory,PinnedFanDescriptor source)
    {
        {
            var auth=Path.Combine(directory,"restore-release-auth.txt");Allow(auth);var terminals=new List<Terminal>();
            using var engine=Engine(auth,Path.Combine(directory,"restore-release-log"),source,terminals);
            engine.Handshake();engine.Open();engine.OpenEvents();var lease=engine.AcquireControl();engine.Enable(EnableBody(lease));
            var backend=engine.RealBackendForSelfTest!;backend.DeliverPinnedSourceTemperatureForSelfTest(50);
            var oldCallback=backend.CapturePinnedSourceTemperatureForSelfTest();var oldGeneration=engine.Snapshot().SourceFanGeneration;
            var body=JsonSerializer.SerializeToElement(new {leaseId=lease.LeaseId});
            engine.Restore(body);var restored=engine.Snapshot();Wire(restored,directory,"restored",check);
            check(restored.SourceFanClosed&&restored.SourceFanAutomaticReturned&&!restored.SourceFanClosePending&&restored.Lease is null,
                "source-outer:actual-restore-retires-exact-owner-"+source.DeviceId);
            var writes=terminals[0].Writes.Count;engine.Release(body);var released=engine.Snapshot();Wire(released,directory,"released",check);
            check(released.State=="AwaitingControl"&&released.Lease is null&&terminals[0].Writes.Count==writes,
                "source-outer:actual-release-after-cleared-lease-does-not-repeat-write-"+source.DeviceId);
            var next=engine.AcquireControl();engine.Enable(EnableBody(next));oldCallback(100);
            var successor=engine.Snapshot();check(successor.SourceFanGeneration>oldGeneration&&terminals.Count==2&&terminals[1].Writes.Count==0,
                "source-outer:actual-reacquire-is-new-owner-and-rejects-old-sensor-"+source.DeviceId);
            backend.DeliverPinnedSourceTemperatureForSelfTest(50);engine.Close();
            check(engine.Snapshot().State=="Stopped"&&!terminals[1].IsReady,
                "source-outer:restore-release-reacquire-close-reaches-terminal-"+source.DeviceId);
        }
        {
            var auth=Path.Combine(directory,"power-auth.txt");Allow(auth);var terminals=new List<Terminal>();
            using var engine=Engine(auth,Path.Combine(directory,"power-log"),source,terminals);
            File.WriteAllText(Path.Combine(directory,"power-handshake.json"),JsonSerializer.Serialize(engine.Handshake(),JsonDefaults.Options));
            Wire(engine.Snapshot(),directory,"power-cold",check);
            engine.Open();Wire(engine.Snapshot(),directory,"power-opened",check);
            engine.OpenEvents();Wire(engine.Snapshot(),directory,"power-events",check);
            var lease=engine.AcquireControl();File.WriteAllText(Path.Combine(directory,"power-lease.json"),JsonSerializer.Serialize(new{ok=true,lease},JsonDefaults.Options));
            Wire(engine.Snapshot(),directory,"power-acquired",check);
            engine.Enable(EnableBody(lease));Wire(engine.Snapshot(),directory,"power-armed",check);
            var backend=engine.RealBackendForSelfTest!;backend.DeliverPinnedSourceTemperatureForSelfTest(50);Wire(engine.Snapshot(),directory,"power-active",check);
            var oldCallback=backend.CapturePinnedSourceTemperatureForSelfTest();var old=engine.Snapshot();
            var serverOptions=HostOptions.Parse(Array.Empty<string>()) with {SessionToken=Token,ParentPid=null,ParentProcessName=null};
            using var server=new LocalFanHostServer(engine,serverOptions,new HostDiagnosticLog(Path.Combine(directory,"power-server-log")));
            var worker=server.StartPowerWorkerForSelfTest();
            try
            {
                server.SuspendForSelfTest(2).WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
                var suspended=engine.Snapshot();Wire(suspended,directory,"suspended",check);
                check(suspended.State=="Suspended"&&suspended.SourceFanClosed&&suspended.SourceFanAutomaticReturned&&!suspended.SourceFanClosePending,
                    "source-power:actual-serialized-suspend-retires-owner-"+source.DeviceId);
                check(!suspended.HcVirtualCloseReturned&&!suspended.HcDeviceManagerStopCompleted&&!suspended.OemPhysicalOwnershipConfirmed,
                    "source-power:suspend-does-not-fake-hc-or-physical-"+source.DeviceId);
                var writes=terminals[0].Writes.Count;server.SuspendForSelfTest(2).WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
                check(terminals[0].Writes.Count==writes&&terminals.Count==1,
                    "source-power:duplicate-suspend-does-not-repeat-source-release-"+source.DeviceId);
                server.ResumeForSelfTest().WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
                var acceptedAttempt=engine.Snapshot().PowerOperationAttempt;
                engine.WaitForAutomaticResumeAsync(5000,ResumeWaitBinding.Pinned(acceptedAttempt)).WaitAsync(TimeSpan.FromSeconds(6)).GetAwaiter().GetResult();
                var resumed=engine.Snapshot();Wire(resumed,directory,"resumed",check);
                check(resumed.State=="Ready"&&resumed.OpenCalled&&resumed.OpenEventsCalled&&resumed.SourceFanGeneration>old.SourceFanGeneration,
                    "source-power:actual-server-resume-rebuilds-new-source-generation-"+source.DeviceId);
                check(terminals.Count==2&&terminals[1].Writes.Count==0&&!resumed.HardwareWritesObserved&&resumed.HardwareWritesEnabled,
                    "source-power:resume-arms-retained-curve-without-invented-sample-"+source.DeviceId);
                oldCallback(100);check(terminals[1].Writes.Count==0,
                    "source-power:pre-sleep-callback-cannot-write-successor-"+source.DeviceId);
                backend.DeliverPinnedSourceTemperatureForSelfTest(50);check(terminals[1].Writes.Count==2,
                    "source-power:current-sensor-drives-resumed-source-owner-"+source.DeviceId);
                var currentGeneration=engine.Snapshot().SourceFanGeneration;bool unpairedRefused=false;
                try{server.ResumeForSelfTest().WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();}
                catch(FanApiException error)when(error.Code=="HC_RESUME_RESULT_EXPIRED"){unpairedRefused=true;}
                check(unpairedRefused,"source-power:unpaired-new-native-generation-refused-"+source.DeviceId);
                check(engine.Snapshot().SourceFanGeneration==currentGeneration&&terminals.Count==2,
                    "source-power:duplicate-resume-does-not-create-third-owner-"+source.DeviceId);
                var bridgeLease=engine.AcquireControl();File.WriteAllText(Path.Combine(directory,"power-lease-after-resume.json"),JsonSerializer.Serialize(new{ok=true,lease=bridgeLease},JsonDefaults.Options));
                Wire(engine.Snapshot(),directory,"power-reacquired",check);
                engine.Enable(EnableBody(bridgeLease));Wire(engine.Snapshot(),directory,"power-rearmed",check);
                engine.Close();Wire(engine.Snapshot(),directory,"power-closed",check);
                check(LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(engine.Snapshot()),
                    "source-power:resumed-source-close-accepted-by-real-parent-finalizer-"+source.DeviceId);
            }
            finally
            {
                server.CompletePowerQueueForSelfTest();worker.WaitAsync(TimeSpan.FromSeconds(3)).GetAwaiter().GetResult();
            }
            check(server.ListenerCreateCountForSelfTest==1&&server.WatchdogStartCountForSelfTest==0,
                "source-power:only-existing-inert-queue-no-listener-run-or-parent-watchdog-"+source.DeviceId);
        }
    }
    private static void RunBridgeReceiptChain(Action<bool,string> check,string directory,PinnedFanDescriptor source)
    {
        var own=Path.Combine(directory,"bridge-chain");Directory.CreateDirectory(own);
        var auth=Path.Combine(own,"auth.txt");Allow(auth);var terminals=new List<Terminal>();
        using var engine=Engine(auth,Path.Combine(own,"log"),source,terminals);
        var handshake=engine.Handshake();File.WriteAllText(Path.Combine(own,"handshake.json"),JsonSerializer.Serialize(handshake,JsonDefaults.Options));
        Wire(engine.Snapshot(),own,"cold",check);
        engine.Open();Wire(engine.Snapshot(),own,"opened",check);
        engine.OpenEvents();Wire(engine.Snapshot(),own,"events",check);
        var lease=engine.AcquireControl();File.WriteAllText(Path.Combine(own,"lease.json"),JsonSerializer.Serialize(new{ok=true,lease},JsonDefaults.Options));
        Wire(engine.Snapshot(),own,"acquired",check);
        engine.Enable(EnableBody(lease));Wire(engine.Snapshot(),own,"armed",check);
        engine.RealBackendForSelfTest!.DeliverPinnedSourceTemperatureForSelfTest(50);Wire(engine.Snapshot(),own,"active",check);
        var body=JsonSerializer.SerializeToElement(new{leaseId=lease.LeaseId});engine.Restore(body);Wire(engine.Snapshot(),own,"restored",check);
        engine.Release(body);Wire(engine.Snapshot(),own,"released",check);
        engine.Close();Wire(engine.Snapshot(),own,"closed",check);
        check(LocalFanHostServer.ParentRecoveryIsSafeToFinalizeForSelfTest(engine.Snapshot())&&terminals.Count==1&&!terminals[0].IsReady,
            "source-wire:actual-public-bridge-chain-terminal-"+source.DeviceId);
    }
    private static void RunMaximum(Action<bool,string> check,string directory,PinnedFanDescriptor source)
    {
        var own=Path.Combine(directory,"maximum-"+source.DeviceId);Directory.CreateDirectory(own);
        var auth=Path.Combine(own,"auth.txt");Allow(auth);var terminals=new List<Terminal>();
        using var engine=Engine(auth,Path.Combine(own,"log"),source,terminals);
        engine.Handshake();engine.Open();engine.OpenEvents();var lease=engine.AcquireControl();
        JsonElement MaximumBody(FanLease held)=>JsonSerializer.SerializeToElement(new {
            leaseId=held.LeaseId,nodes=new[]{new{tempC=0,dutyPercent=100},new{tempC=40,dutyPercent=100},new{tempC=70,dutyPercent=100},new{tempC=100,dutyPercent=100}}});
        engine.Enable(MaximumBody(lease));var backend=engine.RealBackendForSelfTest!;
        check(terminals.Count==1&&terminals[0].Writes.Count==2&&terminals[0].Writes.Last()==(source.DutyRegister,source.Maximum),
            "source-maximum:enable-submits-exact-max-register-without-temperature-"+source.DeviceId);
        check(engine.Snapshot().HardwareWritesEnabled&&!backend.TemperatureMonitorActive,
            "source-maximum:no-false-temperature-dependency-"+source.DeviceId);
        check(engine.Snapshot().MonitorFan is {Percent:100d,SampledAt:>0},
            "source-maximum:monitor-target-receipt-is-100-without-HW-sample-"+source.DeviceId);
        var serverOptions=HostOptions.Parse(Array.Empty<string>()) with {SessionToken=Token,ParentPid=null,ParentProcessName=null};
        using var server=new LocalFanHostServer(engine,serverOptions,new HostDiagnosticLog(Path.Combine(own,"server-log")));
        var worker=server.StartPowerWorkerForSelfTest();
        try {
            server.SuspendForSelfTest(2).WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
            check(engine.Snapshot().State=="Suspended"&&!terminals[0].IsReady,"source-maximum:actual-sleep-retires-max-owner-"+source.DeviceId);
            server.ResumeForSelfTest().WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
            var attempt=engine.Snapshot().PowerOperationAttempt;
            engine.WaitForAutomaticResumeAsync(5000,ResumeWaitBinding.Pinned(attempt)).WaitAsync(TimeSpan.FromSeconds(6)).GetAwaiter().GetResult();
            check(engine.Snapshot().HardwareWritesEnabled&&terminals.Count==2&&terminals[1].Writes.Last()==(source.DutyRegister,source.Maximum)&&!backend.TemperatureMonitorActive,
                "source-maximum:actual-resume-replays-max-without-temperature-"+source.DeviceId);
            var current=engine.AcquireControl();engine.Enable(EnableBody(current));
            check(backend.TemperatureMonitorActive,"source-maximum:lower-curve-restores-original-temperature-owner-"+source.DeviceId);
            var oldTemperature=backend.CapturePinnedSourceTemperatureForSelfTest();
            engine.Enable(MaximumBody(current));var count=terminals[1].Writes.Count;oldTemperature(50);
            check(!backend.TemperatureMonitorActive&&terminals[1].Writes.Count==count,
                "source-maximum:late-lower-curve-sample-cannot-change-new-max-"+source.DeviceId);
            engine.Close();var closed=engine.Snapshot();
            check(closed.State=="Stopped"&&closed.SourceFanClosed&&!terminals[1].IsReady&&!closed.OemPhysicalOwnershipConfirmed,
                "source-maximum:close-and-honest-physical-unknown-"+source.DeviceId);
        }finally{server.CompletePowerQueueForSelfTest();worker.WaitAsync(TimeSpan.FromSeconds(3)).GetAwaiter().GetResult();}
    }
    internal static void Run(Action<bool,string> check,string evidenceRoot)
    {
        var directory=Path.Combine(evidenceRoot,"source-host-consumer-memory-auth");Directory.CreateDirectory(directory);
        foreach(var source in FanCapabilityCatalog.PinnedSourceDeclarations)
        {
            var authorization=Path.Combine(directory,source.DeviceId+".txt");Allow(authorization);
            var terminal=new Terminal(source);int factories=0;
            using var host=Host(authorization);
            host.InstallPinnedSourceConsumerForSelfTest(source.Manufacturers[0],source.Products[0],()=>{factories++;return terminal;});
            var handshake=host.LoadAndHandshake();
            check(handshake.Supported&&handshake.FactoryType is null&&handshake.SourceModelClass==source.ModelClass,"source-host:exact-source-without-runtime-factory-"+source.DeviceId);
            check(factories==0&&terminal.Writes.Count==0,"source-host:handshake-never-opens-terminal-"+source.DeviceId);
            host.Open();host.OpenEvents();
            check(factories==1&&host.IsOpen&&terminal.Writes.Count==0,"source-host:authorized-open-observes-auto-without-control-write-"+source.DeviceId);
            check(host.ApplyCurve(Curve),"source-host:actual-curve-core-armed-"+source.DeviceId);
            check(terminal.Writes.Count==0,"source-host:no-invented-startup-temperature-write-"+source.DeviceId);
            host.DeliverPinnedSourceTemperatureForSelfTest(50);
            var expected=(byte)Math.Round(45d*source.Maximum/100d,MidpointRounding.ToEven);
            check(terminal.Writes.Count==2&&terminal.Writes[0]==(source.ControlRegister,source.ControlOn)&&terminal.Writes[1]==(source.DutyRegister,expected),"source-host:real-hwinfo-callback-source-owner-duty-"+source.DeviceId);
            check(host.SourceWritesObserved&&host.FanDispatchProviderNameForSelfTest=="source-transport","source-host:source-writer-evidence-is-honest-"+source.DeviceId);
            var count=terminal.Writes.Count;host.DeliverPinnedSourceTemperatureForSelfTest(50);
            check(count==terminal.Writes.Count,"source-host:same-temperature-no-duplicate-write-"+source.DeviceId);
            host.DeliverPinnedSourceTemperatureForSelfTest(float.NaN);host.DeliverPinnedSourceTemperatureForSelfTest(0);host.DeliverPinnedSourceTemperatureForSelfTest(111);
            check(count==terminal.Writes.Count,"source-host:invalid-sample-never-reaches-terminal-"+source.DeviceId);
            host.DeliverPinnedSourceTemperatureForSelfTest(65,foreignMonitor:true);host.DeliverPinnedSourceTemperatureForSelfTest(65,sampledTick:Environment.TickCount64-60000);
            check(count==terminal.Writes.Count,"source-host:foreign-and-stale-monitor-sample-refused-"+source.DeviceId);
            host.DeliverPinnedSourceTemperatureForSelfTest(95);
            check(terminal.Writes.Last()==(source.DutyRegister,source.Maximum),"source-host:source-trip-drives-max-register-"+source.DeviceId);
            host.ApplyCurve(Curve);host.DeliverPinnedSourceTemperatureForSelfTest(85);
            check(terminal.Writes.Last()==(source.DutyRegister,source.Maximum),"source-host:curve-edit-preserves-unexpired-thermal-latch-"+source.DeviceId);
            var retiredCallback=host.CapturePinnedSourceTemperatureForSelfTest();
            host.BlockWritesForClose();host.Close();
            check(host.SourceTransportCloseConfirmed&&host.SourceAutomaticReturnObserved&&!host.IsOpen&&!terminal.IsReady,"source-host:exact-return-and-terminal-close-"+source.DeviceId);
            check(!host.HcVirtualCloseReturned&&!host.HcOpenInvocationStarted&&!host.HcOpenEventsInvoked&&!host.HcDeviceManagerStopCompleted,"source-host:source-close-never-fakes-hc-receipts-"+source.DeviceId);
            count=terminal.Writes.Count;retiredCallback(100);host.Close();
            check(count==terminal.Writes.Count,"source-host:late-callback-and-duplicate-close-no-write-"+source.DeviceId);
        }
        var descriptor=FanCapabilityCatalog.PinnedSourceDeclarations[0];
        foreach(var engineSource in FanCapabilityCatalog.PinnedSourceDeclarations)
        {
            var ownDirectory=Path.Combine(directory,"outer-"+engineSource.DeviceId);Directory.CreateDirectory(ownDirectory);
            RunMaximum(check,ownDirectory,engineSource);
            RunEngine(check,ownDirectory,engineSource);
            RunPowerAndRelease(check,ownDirectory,engineSource);
            RunBridgeReceiptChain(check,ownDirectory,engineSource);
        }
        foreach(var scenario in new[]{"missing-file","wrong-token","allow-false"})
        {
            var authorization=Path.Combine(directory,"deny-"+scenario+".txt");if(scenario!="missing-file")Allow(authorization);
            using var host=Host(authorization,scenario!="allow-false",scenario=="wrong-token"?"wrong":Token);int opens=0;
            host.InstallPinnedSourceConsumerForSelfTest(descriptor.Manufacturers[0],descriptor.Products[0],()=>{opens++;return new Terminal(descriptor);});
            Reject(host.Open,check,"source-host:original-authorization-refuses-"+scenario);
            check(opens==0&&!host.IsOpen,"source-host:denied-before-default-or-injected-terminal-"+scenario);
        }
        {
            var authorization=Path.Combine(directory,"null-terminal.txt");Allow(authorization);
            using var host=Host(authorization);host.InstallPinnedSourceConsumerForSelfTest(descriptor.Manufacturers[0],descriptor.Products[0],()=>null!);
            Reject(host.Open,check,"source-host:null-fixture-terminal-never-falls-through-to-live-factory");
            check(!host.SourceCloseIsOwed&&!host.IsOpen,"source-host:null-factory-cleanup-observed");
        }
        {
            var authorization=Path.Combine(directory,"revoked.txt");Allow(authorization);var terminal=new Terminal(descriptor);
            using var host=Host(authorization);host.InstallPinnedSourceConsumerForSelfTest(descriptor.Manufacturers[0],descriptor.Products[0],()=>terminal);
            host.Open();host.OpenEvents();host.ApplyCurve(Curve);host.DeliverPinnedSourceTemperatureForSelfTest(50);int before=terminal.Writes.Count;
            File.Delete(authorization);host.DeliverPinnedSourceTemperatureForSelfTest(60);
            check(before==terminal.Writes.Count&&host.SourceCloseIsOwed,"source-host:revoked-authorization-no-new-duty-keeps-return-debt");
            host.Close();check(host.SourceAutomaticReturnObserved&&host.SourceTransportCloseConfirmed,"source-host:revocation-does-not-block-owned-auto-return");
        }
        {
            var authorization=Path.Combine(directory,"suspend-during-control.txt");Allow(authorization);var terminal=new Terminal(descriptor);
            using var host=Host(authorization);host.InstallPinnedSourceConsumerForSelfTest(descriptor.Manufacturers[0],descriptor.Products[0],()=>terminal);
            host.Open();host.OpenEvents();host.ApplyCurve(Curve);
            terminal.BeforeWrite=(register,value)=>{if(register==descriptor.ControlRegister&&value==descriptor.ControlOn)host.BlockWritesForSuspend();};
            host.DeliverPinnedSourceTemperatureForSelfTest(50);
            check(terminal.Writes.Count==1&&host.SourceCloseIsOwed,"source-host:suspend-after-issued-control-refuses-following-duty");
            terminal.BeforeWrite=null;host.Close();
            check(terminal.Writes.Count==2&&terminal.Writes.Last()==(descriptor.ControlRegister,(byte)0)&&host.SourceTransportCloseConfirmed,"source-host:issued-old-epoch-retired-before-successor");
        }
        {
            var authorization=Path.Combine(directory,"return-failure.txt");Allow(authorization);var terminal=new Terminal(descriptor){IgnoreAutomatic=true};
            using var host=Host(authorization);host.InstallPinnedSourceConsumerForSelfTest(descriptor.Manufacturers[0],descriptor.Products[0],()=>terminal);
            host.Open();host.OpenEvents();host.ApplyCurve(Curve);host.DeliverPinnedSourceTemperatureForSelfTest(50);
            try
            {
                Reject(()=>host.Close(),check,"source-host:failed-auto-readback-is-close-pending");
                check(host.SourceCloseIsOwed&&!host.SourceTransportCloseConfirmed&&terminal.IsReady,"source-host:failed-auto-retains-exact-owner");
            }
            finally{terminal.IgnoreAutomatic=false;host.Close();}
        }
    }
    // The directed UMA group enters the same Host curve/temperature/return
    // consumer as Run above, with production transport and reflected WMI objects.
    internal static void RunUmaCurve(Action<bool,string> check,string evidenceRoot)
    {
        var directory=Path.Combine(evidenceRoot,"uma-host-consumer");Directory.CreateDirectory(directory);
        var authorization=Path.Combine(directory,"curve-auth.txt");Allow(authorization);
        using var world=new PinnedUmaFanChecks.World();
        Action<float?> retiredCallback;long oldGeneration;int factories=0;
        using(var host=Host(authorization))
        {
            host.InstallPinnedSourceConsumerForSelfTest("Vendor ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER SUPER X",
                ()=>{factories++;return world.Transport();});
            var handshake=host.LoadAndHandshake();
            check(handshake.Supported && handshake.FactoryType is null && handshake.SourceModelClass==PinnedUmaFanChecks.ModelClass &&
                handshake.SourceCommit==PinnedUmaFanChecks.Descriptor.SourceCommit && factories==0 && world.Searchers==0,
                "uma-host:actual-source-handshake-does-not-open-searcher-or-hc-factory");
            try
            {
                host.Open();host.OpenEvents();oldGeneration=host.SourceGeneration!.Value;
                check(factories==1 && world.Searchers==1 && host.IsOpen && world.Writes.Length==0,
                    "uma-host:authorized-open-only-observes-real-uma-auto-mode");
                check(host.ApplyCurve(Curve) && world.Writes.Length==0,"uma-host:existing-curve-arms-without-invented-temperature");
                host.DeliverPinnedSourceTemperatureForSelfTest(50);
                var writes=world.Writes;
                check(writes.Length==2 && writes[0].Index is int control && control==0x044A && writes[0].Data is byte on && on==1 &&
                    writes[1].Index is int duty && duty==0x044B && writes[1].Data is byte value && value==115 && world.Values[0x044B]==115,
                    "uma-host:real-hwinfo-callback-curve-interpolation-and-255-scaling-reach-raw-uma");
                check(host.SourceWritesObserved && host.FanDispatchProviderNameForSelfTest=="source-transport" &&
                    world.Calls.All(call=>call.Index is int index && index is 0x044A or 0x044B),
                    "uma-host:source-writer-evidence-and-no-guessed-temperature-register");
                int count=world.Writes.Length;
                host.DeliverPinnedSourceTemperatureForSelfTest(50);host.DeliverPinnedSourceTemperatureForSelfTest(float.NaN);
                host.DeliverPinnedSourceTemperatureForSelfTest(0);host.DeliverPinnedSourceTemperatureForSelfTest(111);
                host.DeliverPinnedSourceTemperatureForSelfTest(65,foreignMonitor:true);
                host.DeliverPinnedSourceTemperatureForSelfTest(65,sampledTick:Environment.TickCount64-60000);
                check(world.Writes.Length==count,"uma-host:existing-duplicate-invalid-foreign-and-stale-sample-guards");
                host.DeliverPinnedSourceTemperatureForSelfTest(95);
                check(world.Writes.Last().Data is byte maximum && maximum==255,"uma-host:existing-host-thermal-policy-drives-superx-max");
                host.ApplyCurve(Curve);host.DeliverPinnedSourceTemperatureForSelfTest(85);
                check(world.Writes.Last().Data is byte latched && latched==255,"uma-host:curve-edit-preserves-current-host-thermal-latch");
                retiredCallback=host.CapturePinnedSourceTemperatureForSelfTest();
                host.BlockWritesForClose();host.Close();
                check(host.SourceTransportCloseConfirmed && host.SourceAutomaticReturnObserved && !host.IsOpen &&
                    world.Writes.Last().Index is int returned && returned==0x044A && world.Writes.Last().Data is byte zero && zero==0 &&
                    world.Values[0x044A]==0,"uma-host:existing-close-observes-auto-and-actual-derived-row-disposal");
                check(!host.HcVirtualCloseReturned && !host.HcOpenInvocationStarted && !host.HcOpenEventsInvoked &&
                    !host.HcDeviceManagerStopCompleted,"uma-host:uma-return-does-not-fabricate-hc-lifecycle-receipts");
                count=world.Calls.Length;host.Close();retiredCallback(100);
                check(world.Calls.Length==count,"uma-host:duplicate-close-and-retired-callback-are-inert");
            }
            finally {world.ClearFaults();host.Close();}
        }
        world.CheckClosed(check,"host-curve-first-generation");
        using(var successor=Host(authorization))
        {
            successor.InstallPinnedSourceConsumerForSelfTest("ONE-NETBOOK","ONEXPLAYER SUPER X",()=>world.Transport());
            try
            {
                successor.Open();successor.OpenEvents();successor.ApplyCurve(Curve);
                int count=world.Calls.Length;retiredCallback(100);
                check(successor.SourceGeneration>oldGeneration && world.Searchers==2 && world.Calls.Length==count,
                    "uma-host:new-host-generation-refuses-old-sensor-callback");
                successor.DeliverPinnedSourceTemperatureForSelfTest(50);
                check(world.Writes.Last().Data is byte duty && duty==115,"uma-host:new-generation-still-consumes-existing-curve");
                successor.Close();
                check(successor.SourceTransportCloseConfirmed && successor.SourceAutomaticReturnObserved,"uma-host:successor-actual-return-and-close");
            }
            finally {world.ClearFaults();successor.Close();}
        }
        world.CheckClosed(check,"host-curve-successor");
    }
    internal static void RunUmaReturnDebt(Action<bool,string> check,string evidenceRoot)
    {
        var directory=Path.Combine(evidenceRoot,"uma-host-consumer");Directory.CreateDirectory(directory);
        foreach(bool revoke in new[]{false,true})
        {
            var authorization=Path.Combine(directory,revoke ? "revoke-auth.txt" : "return-debt-auth.txt");Allow(authorization);
            using var world=new PinnedUmaFanChecks.World();
            using var host=Host(authorization);
            host.InstallPinnedSourceConsumerForSelfTest("ONE-NETBOOK","ONEXPLAYER SUPER X",()=>world.Transport());
            try
            {
                host.Open();host.OpenEvents();host.ApplyCurve(Curve);host.DeliverPinnedSourceTemperatureForSelfTest(50);
                if(revoke)
                {
                    int count=world.Writes.Length;File.Delete(authorization);host.DeliverPinnedSourceTemperatureForSelfTest(60);
                    check(world.Writes.Length==count && host.SourceCloseIsOwed,"uma-host:revoked-write-authorization-retains-owned-uma-return");
                    host.Close();
                    check(host.SourceAutomaticReturnObserved && host.SourceTransportCloseConfirmed && world.Values[0x044A]==0,
                        "uma-host:authorization-revocation-does-not-block-owned-auto-return");
                }
                else
                {
                    world.IgnoreAutomatic=true;
                    Reject(()=>host.Close(),check,"uma-host:readback-failure-keeps-close-pending");
                    check(host.SourceCloseIsOwed && !host.SourceTransportCloseConfirmed && !host.SourceAutomaticReturnObserved &&
                        world.Resources.OfType<PinnedUmaFanChecks.FakeRow>().Single().Closed==false && world.Values[0x044A]==1,
                        "uma-host:actual-failed-auto-return-keeps-exact-selected-row");
                    PinnedUmaFanChecks.Reject(()=> {
                        using var unexpected=FanCapabilityCatalog.AcquirePinnedFanSource(PinnedUmaFanChecks.Descriptor,Token,()=>true,()=>world.Transport());
                        unexpected.Open();
                    },check,"host-return-debt-blocks-new-catalog-owner");
                    world.IgnoreAutomatic=false;host.Close();
                    check(host.SourceAutomaticReturnObserved && host.SourceTransportCloseConfirmed && world.Values[0x044A]==0,
                        "uma-host:explicit-retry-settles-readback-and-exact-row-close");
                }
            }
            finally {world.ClearFaults();host.Close();}
            world.CheckClosed(check,revoke ? "host-revoked-auth" : "host-failed-return");
        }
    }
}
