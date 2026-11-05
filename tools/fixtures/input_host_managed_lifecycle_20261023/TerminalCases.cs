using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using Terminal=YeManInputHost.Diagnostics.HostTerminalCleanup;
internal static partial class Program
{
    private static readonly List<Terminal.Receipt> TerminalReceipts=new();
    private static int ActualLogStart;
    private sealed partial class HostSession
    {
        internal IDisposable? CostDiagnosticLifetime=>TerminalTestPorts.CostLifetime;
    }
    private sealed class InitializationLifetime(Action dispose) : IDisposable
    {
        public void Dispose()=>dispose();
    }
    private static JsonElement[] ReadAllActualLogRows()
    {
        var file=Path.Combine(AppContext.BaseDirectory,"fixture-owned-logs","input-host-fatal.log");
        return File.Exists(file)?File.ReadAllLines(file).Where(line=>line.Length>0).Select(line=>{using var doc=JsonDocument.Parse(line);return doc.RootElement.Clone();}).ToArray():Array.Empty<JsonElement>();
    }
    private static JsonElement[] ActualLogRows()=>ReadAllActualLogRows().Skip(ActualLogStart).ToArray();
    private static Terminal.Receipt SettleSession(HostSession session,int origin=0)
    {
        var owner=session.Controller;var context=session.Context;var threads=new List<int>();
        var receipt=Terminal.Settle(new Terminal.LoopResult(origin,"fixture-direct-terminal"),(out string detail)=>{
            threads.Add(Environment.CurrentManagedThreadId);
            if(threads.Count==2)Assert(ReferenceEquals(owner,session.Controller)&&ReferenceEquals(context,session.Context),"retry changed owner");
            return session.ReleaseLocal(out detail);
        });
        Assert(threads.Distinct().Count()==1,"cleanup moved retry to another thread");TerminalReceipts.Add(receipt);return receipt;
    }
    private static HostSession PersonaReady(string persona)
    {
        if(persona=="steamdeck")return ManagedReady();
        var s=new HostSession("fake-nonce");
        var hello=Message("HELLO",1);hello["persona"]="dualsense-edge";hello["profileIdentity"]="dualsense-edge";Good(Send(s,hello,out _));
        Fixture.OnObserve=()=>Fixture.Creates==0||Fixture.Sweeps>0?Empty():new VirtualEndpointObservation(true,1,0,1,0,0,true,"fake-non-SD-arrival");
        var prepare=Message("PREPARE_TARGET",2);prepare["persona"]="dualsense-edge";prepare["profileIdentity"]="dualsense-edge";Good(Send(s,prepare,out _));
        Fixture.OnSweep=()=>Fixture.Current=Empty();return s;
    }
    private static void CheckReceipt(Terminal.Receipt r,bool success,int attempts,bool retry)
    {
        Assert(r.ManagedReleaseComplete==success&&r.ReleaseAttempts==attempts&&r.Retried==retry,JsonSerializer.Serialize(r));
        Assert(r.ExitCode==(success?0:Terminal.ReleaseFailedExitCode),"terminal result erased cleanup failure");
    }
    private static async Task<Dictionary<string,object?>> TerminalPipeCase(string mode,string worker="healthy",bool transient=false,int? externalParentPid=null,Func<Dictionary<string,object?>,Task>? beforeFixtureCleanup=null)
    {
        if(externalParentPid.HasValue&&(externalParentPid.Value<=0||mode!="shutdown"||worker!="output"||transient||beforeFixtureCleanup is null))throw new ArgumentException("External parent is restricted to the retained shutdown output fixture");
        ResetManaged();OwnedWorkers.BlockOutput=worker=="output";OwnedWorkers.BlockClock=worker=="clock";
        TerminalTestPorts.ReleaseOutputAfterTimeout=transient&&worker=="output";TerminalTestPorts.ReleaseClockAfterTimeout=transient&&worker=="clock";
        var s=mode.StartsWith("shutdown-write-")||mode=="shutdown-parent-loss-neutral"?PersonaReady("dualsense-edge"):ManagedReady();var owner=s.Controller!;var callback=owner.CapturedOutput;
        using var parent=externalParentPid.HasValue?Process.GetProcessById(externalParentPid.Value):StartOwnedParent();var name="ymcc-terminal23-"+Guid.NewGuid().ToString("N");await using var server=ManagedServer(name);
        Task<int>? run=null;bool pass=true;string? error=null;int? exit=null;
        try
        {
            if(mode=="already-dead")await ExitOwnedParent(parent);
            var connection=mode=="connect-loss"?Task.CompletedTask:server.WaitForConnectionAsync();
            run=ExecuteMainTerminalAsync(mode=="connect-loss"?name+"-absent":name,parent.Id,s);
            if(mode!="already-dead"&&mode!="connect-loss")await connection.WaitAsync(TimeSpan.FromSeconds(3));
            if(mode=="shutdown"||mode=="shutdown-neutral-failure"||mode=="shutdown-sdk-failure"||mode=="invalid-shutdown"||mode.StartsWith("shutdown-write-")||mode=="shutdown-parent-loss-neutral")
            {
                Fixture.NeutralFails=mode=="shutdown-neutral-failure"||mode=="shutdown-write-failure-neutral"||mode=="shutdown-parent-loss-neutral";World.ThrowContextDispose=mode=="shutdown-sdk-failure";
                if(mode=="invalid-shutdown") { var invalid=Message("SHUTDOWN",3);invalid["startupNonce"]="wrong-nonce";await server.WriteAsync(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(invalid)));var badData=new byte[16384];int badLength=await server.ReadAsync(badData).AsTask().WaitAsync(TimeSpan.FromSeconds(3));using var badReply=JsonDocument.Parse(badData.AsMemory(0,badLength));Bad(badReply.RootElement,"protocol-error");Assert(!run.IsCompleted&&TerminalReceipts.Count==0&&ReferenceEquals(owner,s.Controller),"invalid SHUTDOWN became terminal"); }
                var shutdownMessage=Message("SHUTDOWN",3);
                bool ackBoundary=mode.StartsWith("shutdown-write-")||mode=="shutdown-parent-loss-neutral";
                if(ackBoundary){shutdownMessage["persona"]="dualsense-edge";shutdownMessage["profileIdentity"]="dualsense-edge";_msiHostStageEvidence=new Stage();TerminalTestPorts.BeforeWrite=()=>{if(mode=="shutdown-parent-loss-neutral"){ExitOwnedParent(parent).GetAwaiter().GetResult();throw new OperationCanceledException("owned-parent-gone");}throw new IOException("injected-write-boundary");};}
                await server.WriteAsync(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(shutdownMessage)));
                if(!ackBoundary){
                var buffer=new byte[16384];var n=await server.ReadAsync(buffer).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
                using var reply=JsonDocument.Parse(buffer.AsMemory(0,n));
                if((mode=="shutdown"&&worker=="healthy")||mode=="invalid-shutdown")Good(reply.RootElement);else Bad(reply.RootElement,"shutdown-local-dispose-failed");
                }
            }
            else if(mode=="exception")
            {
                TerminalTestPorts.ThrowHandle=true;_msiHostStageEvidence=new Stage();
                await server.WriteAsync(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(Message("SUBMIT_NEUTRAL",3))));
            }
            else if(mode=="pipe-break")server.Dispose();
            else await ExitOwnedParent(parent);
            exit=await run.WaitAsync(TimeSpan.FromSeconds(6));
            Assert(TerminalReceipts.Count==1,"terminal settlement did not run exactly once");var receipt=TerminalReceipts.Single();
            Assert(exit==receipt.ExitCode,"Main result lost terminal receipt");
            var actualFinal=ActualLogRows().Where(row=>row.GetProperty("reason").GetString()!.StartsWith("terminal-cleanup-")).ToArray();
            Assert(actualFinal.Length==1&&actualFinal[0].GetProperty("kind").GetString()=="resource-settlement","production logger misclassified or duplicated terminal settlement");
            using(var loggedReceipt=JsonDocument.Parse(actualFinal[0].GetProperty("detail").GetString()!))Assert(loggedReceipt.RootElement.GetProperty("ManagedReleaseComplete").GetBoolean()==receipt.ManagedReleaseComplete&&loggedReceipt.RootElement.GetProperty("ExitCode").GetInt32()==exit,"actual log lost receipt result");
            foreach(var ordered in ActualLogRows().Where(row=>row.GetProperty("reason").GetString()=="shutdown-ordered"))Assert(ordered.GetProperty("kind").GetString()=="orderly"&&!ordered.GetProperty("detail").GetString()!.Contains("ManagedReleaseComplete"),"SHUTDOWN trigger mistaken for resource completion");AssertNoReport(s,owner,callback);
            bool permanent=worker!="healthy"&&!transient;
            if(mode=="shutdown-sdk-failure")
            {
                Assert(exit==7&&!receipt.ManagedReleaseComplete&&receipt.ReleaseAttempts==1&&receipt.DirectReleaseAttempts==0&&!receipt.Retried);
                Assert(s.HasController&&s.HasContext&&World.ContextCalls==1&&receipt.FirstDetail.Contains("controller-dispose:"));AllClosed();
            }
            else if(permanent)
            {
                Assert(exit!=0&&!receipt.ManagedReleaseComplete&&receipt.ResourceDisposition=="owner-retained"&&receipt.Retried&&receipt.ReleaseAttempts==2);
                Assert(ReferenceEquals(owner,s.Controller)&&s.HasContext&&s.Pending,"permanent stall lost retained owner");NoDestruction();
            }
            else
            {
                Assert(!s.HasController&&!s.HasContext);AllClosed();Assert(World.ContextCalls==1,"healthy terminal repeated SDK Dispose");
                Assert(receipt.ManagedReleaseComplete==!(mode=="shutdown-neutral-failure"||mode=="shutdown-write-failure-neutral"||mode=="shutdown-parent-loss-neutral"));
                if(mode.StartsWith("shutdown-write-")||mode=="shutdown-parent-loss-neutral") { Assert(receipt.DirectReleaseAttempts==0&&receipt.ReleaseAttempts==1);if(Fixture.NeutralFails)Assert(receipt.FirstDetail=="neutral:fake-neutral"&&receipt.FinalDetail==receipt.FirstDetail,"ACK boundary lost original release failure"); }
                if(mode=="shutdown"&&worker=="healthy")Assert(receipt.DirectReleaseAttempts==0&&receipt.ReleaseAttempts==1,"SHUTDOWN attempted cleanup twice");
                if(transient)Assert(receipt.Retried&&receipt.ReleaseAttempts==2);
                int expected=mode.StartsWith("shutdown-write-")?6:mode=="exception"?4:mode=="pipe-break"?5:mode=="shutdown-neutral-failure"||mode=="shutdown-parent-loss-neutral"?7:0;Assert(exit==expected,"terminal origin status lost");
            }
            Assert(_msiHostStageEvidence is null,"Main finally did not finish");
        }
        catch(Exception ex){pass=false;error=ex.ToString();}
        var resources=ResourceSnapshot();var receipts=TerminalReceipts.ToArray();var trace=Fixture.Events.ToArray();
        var caseResult=new Dictionary<string,object?>{["name"]=$"terminal-{mode}-{worker}-{(transient?"transient":"fixed")}",["pass"]=pass,["error"]=error,["exit"]=exit,["receipts"]=receipts,["actualProductionLogRows"]=ActualLogRows(),["beforeFixtureCleanup"]=resources,["trace"]=trace};
        try{if(beforeFixtureCleanup is not null)await beforeFixtureCleanup(caseResult);}
        finally
        {
            try{server.Dispose();if(!externalParentPid.HasValue&&!parent.HasExited)await ExitOwnedParent(parent);if(run is not null)await run.WaitAsync(TimeSpan.FromSeconds(6));OwnedWorkers.Cleanup();}
            catch(Exception ex){pass=false;error+=" cleanup:"+ex;}
        }
        caseResult["actualProductionLogRows"]=ActualLogRows();caseResult["pass"]=pass;caseResult["error"]=error;return caseResult;
    }
    private static async Task<Dictionary<string,object?>> TerminalInitializationCase(bool fail)
    {
        ResetManaged();var s=ManagedReady();var owner=s.Controller!;var callback=owner.CapturedOutput;
        bool pass=true;string? error=null;int? exit=null;int costDisposals=0;bool stageAfterSettle=false,costAfterSettle=false;
        TerminalTestPorts.ThrowInitialization=fail;
        TerminalTestPorts.CostLifetime=new InitializationLifetime(()=>{costDisposals++;costAfterSettle=TerminalReceipts.Count==1;});
        TerminalTestPorts.BeforeStageDispose=()=>stageAfterSettle=TerminalReceipts.Count==1;
        try
        {
            exit=await ExecuteMainTerminalAsync("unused-init-"+Guid.NewGuid().ToString("N"),0,s,true).WaitAsync(TimeSpan.FromSeconds(6));
            Assert(TerminalReceipts.Count==1,"owned session initialization bypassed terminal settlement");var receipt=TerminalReceipts.Single();
            Assert(exit==(fail?4:0)&&receipt.ExitCode==exit&&receipt.SourceExitCode==exit,"initialization exception lost source failure");
            Assert(receipt.Reason==(fail?"fatal-exception":"parent-exited")&&receipt.ManagedReleaseComplete&&receipt.ReleaseAttempts==1&&receipt.DirectReleaseAttempts==1&&!receipt.Retried,"initialization did not settle original session once");
            Assert(!s.HasController&&!s.HasContext);AllClosed();Assert(World.ContextCalls==1);AssertNoReport(s,owner,callback);
            Assert(TerminalTestPorts.StageCreates==1&&TerminalTestPorts.StageDisposes==(fail?0:1),"diagnostic stage lifetime mismatch");
            Assert(costDisposals==1&&costAfterSettle&&(fail||stageAfterSettle),"diagnostic lifetime ended before terminal settlement");
            Assert(_msiHostStageEvidence is null,"initialization Main finally did not finish");
        }
        catch(Exception ex){pass=false;error=ex.ToString();}
        var resources=ResourceSnapshot();var receipts=TerminalReceipts.ToArray();var trace=Fixture.Events.ToArray();
        try{OwnedWorkers.Cleanup();}catch(Exception ex){pass=false;error+=" cleanup:"+ex;}
        return new(){["name"]="terminal-initialization-"+(fail?"throws":"healthy"),["pass"]=pass,["error"]=error,["exit"]=exit,["receipts"]=receipts,["costDisposals"]=costDisposals,["costAfterSettle"]=costAfterSettle,["stageCreates"]=TerminalTestPorts.StageCreates,["stageDisposes"]=TerminalTestPorts.StageDisposes,["stageAfterSettle"]=stageAfterSettle,["beforeFixtureCleanup"]=resources,["trace"]=trace};
    }
    private static async Task<int> RetainedOwnerChild(string proofPath,string nonce,int nativeParentPid)
    {
        try
        {
            using var nativeParent=Process.GetProcessById(nativeParentPid);
            using var managed=Process.GetCurrentProcess();
            var nativeParentBirthUtcTicks=nativeParent.StartTime.ToUniversalTime().Ticks.ToString(System.Globalization.CultureInfo.InvariantCulture);
            var managedBirthUtcTicks=managed.StartTime.ToUniversalTime().Ticks.ToString(System.Globalization.CultureInfo.InvariantCulture);
            if(nativeParent.HasExited)throw new IOException("Native retained-owner parent already exited");
            await TerminalPipeCase("shutdown","output",false,externalParentPid:nativeParentPid,beforeFixtureCleanup:async caseResult=>
            {
                var actual=JsonSerializer.SerializeToElement(caseResult);
                Assert(actual.GetProperty("name").GetString()=="terminal-shutdown-output-fixed"&&actual.GetProperty("pass").GetBoolean()&&actual.GetProperty("exit").GetInt32()==7,"Retained proof requires the same successful original case and failed terminal result");
                var receipts=actual.GetProperty("receipts");Assert(receipts.GetArrayLength()==1,"Retained proof requires the original unique terminal receipt");
                var receipt=receipts[0];
                Assert(receipt.GetProperty("ExitCode").GetInt32()==7&&!receipt.GetProperty("ManagedReleaseComplete").GetBoolean()&&receipt.GetProperty("ResourceDisposition").GetString()=="owner-retained"&&receipt.GetProperty("ReleaseAttempts").GetInt32()==2&&receipt.GetProperty("DirectReleaseAttempts").GetInt32()==1&&receipt.GetProperty("Retried").GetBoolean(),"Retained proof cannot claim successful managed release");
                var snapshots=actual.GetProperty("beforeFixtureCleanup").GetProperty("workers");
                Assert(snapshots.GetArrayLength()==1&&snapshots[0].GetProperty("outputAlive").GetBoolean()&&!snapshots[0].GetProperty("outputCtsClosed").GetBoolean()&&snapshots[0].GetProperty("callbacks").GetInt32()==0&&!snapshots[0].GetProperty("outputFinallyRan").GetBoolean()&&!snapshots[0].GetProperty("sdkDisposedFlag").GetBoolean()&&actual.GetProperty("beforeFixtureCleanup").GetProperty("ContextCalls").GetInt32()==0,"Retained proof requires the real pre-cleanup output and CTS debt");
                Assert(OwnedWorkers.All.Count==1&&OwnedWorkers.All[0].Output.IsAlive&&!OwnedWorkers.All[0].CtsHandle.IsClosed&&OwnedWorkers.All[0].Controller.SubscriberCount==0&&!OwnedWorkers.All[0].Controller.OutputClosed&&!OwnedWorkers.All[0].Controller.Disposed&&World.ContextCalls==0,"Retained worker changed before publishing proof");
                Assert(!nativeParent.HasExited&&nativeParent.StartTime.ToUniversalTime().Ticks.ToString(System.Globalization.CultureInfo.InvariantCulture)==nativeParentBirthUtcTicks,"Native parent identity changed before retained proof");
                var proof=new{schemaVersion=1,kind="ymcc-managed-retained-proof-v1",nonce,pid=Environment.ProcessId,nativeParentPid,nativeParentBirthUtcTicks,managedBirthUtcTicks,caseResult};
                var bytes=Encoding.UTF8.GetBytes(JsonSerializer.Serialize(proof));
                using(var stream=new FileStream(proofPath,FileMode.CreateNew,FileAccess.Write,FileShare.Read)){stream.Write(bytes,0,bytes.Length);stream.Flush(true);}
                // Explicit uncooperative child boundary: no gate relief, fixture cleanup,
                // or success return after proof. The native production owner must terminate us.
                using var stopObservation=new CancellationTokenSource(TimeSpan.FromSeconds(30));
                try{await nativeParent.WaitForExitAsync(stopObservation.Token);}
                catch(OperationCanceledException){throw new TimeoutException("Native owner did not terminate the retained fixture within 30 seconds");}
                throw new IOException("Native parent exited before terminating its retained child");
            });
            Console.Error.WriteLine("Retained fixture unexpectedly returned without native termination");return 1;
        }
        catch(Exception ex){Console.Error.WriteLine("RETAINED_OWNER_CHILD_FAILED: "+ex);return 1;}
    }
    private static async Task<int> Main(string[] args)
    {
        if(args.SequenceEqual(new[]{"--owned-parent"})){Console.WriteLine("READY");Console.In.ReadLine();return 0;}
        if(args.Length>0&&args[0]=="--retained-owner-child")
        {
            int nativeParentPid;
            try
            {
                if(args.Length!=4||!Path.IsPathFullyQualified(args[1])||File.Exists(args[1])||Directory.Exists(args[1])||!Directory.Exists(Path.GetDirectoryName(args[1]))||args[2].Length!=32||!args[2].All(c=>c>='0'&&c<='9'||c>='a'&&c<='f'||c>='A'&&c<='F')||args[3].Length==0||!args[3].All(c=>c>='0'&&c<='9')||!int.TryParse(args[3],out nativeParentPid)||nativeParentPid<=0||nativeParentPid==Environment.ProcessId)
                {Console.Error.WriteLine("--retained-owner-child requires a new absolute proof path, 32hex nonce and another positive native parent PID");return 2;}
            }
            catch(Exception ex){Console.Error.WriteLine("Invalid retained-owner child arguments: "+ex.Message);return 2;}
            return await RetainedOwnerChild(Path.GetFullPath(args[1]),args[2],nativeParentPid);
        }
        if(args.Length!=0&&!args.SequenceEqual(new[]{"--eligibility-only"}))
        {
            if(args.Length!=2||args[0]!="--case")
            {
                Console.Error.WriteLine("Expected no arguments, --eligibility-only, --owned-parent, --case <terminal-shutdown-healthy-fixed,terminal-read-loss-healthy-fixed>, or --case terminal-shutdown-output-fixed.");
                return 2;
            }
            var selectedCase=args[1];
            var selectedCases=selectedCase.Split(',');
            if(selectedCases.Length==0||selectedCases.Any(name=>name!="terminal-shutdown-healthy-fixed"&&name!="terminal-read-loss-healthy-fixed"&&name!="terminal-shutdown-output-fixed")||selectedCases.Distinct(StringComparer.Ordinal).Count()!=selectedCases.Length||(selectedCases.Contains("terminal-shutdown-output-fixed")&&selectedCases.Length!=1))
            {
                Console.Error.WriteLine("--case requires a nonempty, unique list of terminal-shutdown-healthy-fixed and/or terminal-read-loss-healthy-fixed, or terminal-shutdown-output-fixed alone.");
                return 2;
            }
            var selectedRows=new List<Dictionary<string,object?>>();
            foreach(var selectedName in selectedCases)
            {
                var row=selectedName=="terminal-shutdown-output-fixed"?await TerminalPipeCase("shutdown","output",false):await TerminalPipeCase(selectedName=="terminal-shutdown-healthy-fixed"?"shutdown":"read-loss");
                if(!row.TryGetValue("name",out var actualName)||actualName is not string actualCaseName||actualCaseName!=selectedName)
                {
                    row["pass"]=false;
                    row["error"]="Selected terminal receipt name mismatch: "+selectedName;
                }
                selectedRows.Add(row);
            }
            var selectedPassed=selectedRows.All(row=>row.TryGetValue("pass",out var pass)&&pass is true);
            Console.WriteLine(JsonSerializer.Serialize(new{passed=selectedPassed,cases=selectedRows,eligibilityOnly=false,productionMainExecuted=false,actualMainTerminalBlockExecuted=true,realSdkAssemblyLoaded=false,realDriverIo=false,selectedCase,selectedCases}));
            return selectedPassed?0:1;
        }
        var rows=new List<Dictionary<string,object?>>();
        rows.Add(Case("actual production LogFatalExit classifies only terminal receipt reasons as resource settlement",()=>{
            foreach(var pair in new[]{("terminal-cleanup-complete","resource-settlement"),("terminal-cleanup-failed","resource-settlement"),("shutdown-ordered","orderly"),("parent-exited","parent-lost"),("fatal-exception","fatal"),("read-failed","fatal"),("write-failed","fatal"),("terminal-cleanup-unknown","fatal")})
            {
                ActualLogFatalExit(pair.Item1,"fixture-classification");var row=ActualLogRows().Last();Assert(row.GetProperty("reason").GetString()==pair.Item1&&row.GetProperty("kind").GetString()==pair.Item2,"production logger kind mismatch: "+pair.Item1);
            }
        }));
        rows.Add(Case("exact retry detail whitelist excludes all non-barrier failures",()=>{
            foreach(var detail in new[]{
                "controller-output-stop:sdk-output-stop-timeout",
                "controller-output-stop:sdk-output-stop-lock-timeout",
                "steamdeck-idle-stop-failed:InvalidOperationException:steamdeck-idle-handoff:sdk-idle-clock-timeout",
                "controller-idle-cts-capture:sdk-idle-clock-timeout",
                "neutral:failed;controller-output-stop:sdk-output-stop-timeout",
                "game-output-detach:IOException;controller-output-stop:sdk-output-stop-timeout"
            })Assert(Terminal.CanRetryStopBarrier(detail),detail);
            foreach(var detail in new[]{
                "",
                "controller-dispose:IOException",
                "context-dispose:IOException",
                "controller-output-stop:sdk-output-stop-cancel-failed",
                "controller-output-stop:sdk-output-stop-join-failed",
                "controller-output-stop:sdk-output-stop-core-identity-mismatch",
                "controller-output-stop:sdk-output-stop-field-value-mismatch",
                "sdk-idle-clock-reader-not-stopped",
                "virtual-endpoint-removal-timeout:present",
                "controller-output-stop:sdk-output-stop-timeout;unknown",
                "controller-dispose:IOException;controller-output-stop:sdk-output-stop-timeout",
                "context-dispose:IOException;controller-idle-cts-capture:sdk-idle-clock-timeout",
                "neutral:failed;controller-dispose:IOException;controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed;controller-output-stop:sdk-output-stop-timeout;controller-dispose:IOException",
                "neutral:failed;controller-idle-cts-capture:sdk-idle-clock-timeout;context-dispose:IOException",
                "controller-idle-cts-release:sdk-idle-clock-reader-not-stopped;controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed;controller-idle-cts-capture:sdk-idle-clock-timeout;virtual-endpoint-removal-timeout:present",
                "neutral:neutral-exception:controller-dispose:IOException;controller-output-stop:sdk-output-stop-timeout",
                "game-output-detach:neutral:IOException;controller-output-stop:sdk-output-stop-timeout",
                "neutral:neutral-exception:IOException:controller-output-stop:sdk-output-stop-timeout",
                "prefix:controller-output-stop:sdk-output-stop-timeout",
                "neutral:IOException;controller-output-stop:sdk-output-stop-timeout:suffix",
                "controller-output-stop:sdk-output-stop-timeout;neutral:failed",
                "neutral:failed;game-output-detach:IOException;controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed;neutral:failed;controller-output-stop:sdk-output-stop-timeout",
                "game-output-detach:IOException;game-output-detach:IOException;controller-output-stop:sdk-output-stop-timeout",
                ";controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed;;controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed;controller-output-stop:sdk-output-stop-timeout;",
                "neutral:failed;steamdeck-idle-stop-failed:InvalidOperationException:steamdeck-idle-handoff:sdk-idle-clock-timeout",
                "neutral:fake-neutral;controller-output-stop:sdk-output-stop-timeout",
                "neutral:failed ;controller-output-stop:sdk-output-stop-timeout",
                "neutral:neutral-exception:Namespace.IOException;controller-output-stop:sdk-output-stop-timeout"
            })Assert(!Terminal.CanRetryStopBarrier(detail),detail);
        }));
        foreach(var worker in new[]{"output","clock","capture"})foreach(var transient in new[]{true,false})rows.Add(Case($"direct-{worker}-{(transient?"transient":"permanent")}",()=>{
            OwnedWorkers.BlockOutput=worker=="output";OwnedWorkers.BlockClock=worker!="output";
            var s=PersonaReady(worker=="capture"?"dualsense-edge":"steamdeck");var c=s.Controller;var ctx=s.Context;
            TerminalTestPorts.ReleaseOutputAfterTimeout=transient&&worker=="output";TerminalTestPorts.ReleaseClockAfterTimeout=transient&&worker!="output";
            var receipt=SettleSession(s);CheckReceipt(receipt,transient,2,true);
            if(transient){Assert(!s.HasController&&!s.HasContext);AllClosed();Assert(World.ContextCalls==1);}
            else{Assert(receipt.ResourceDisposition=="owner-retained"&&ReferenceEquals(c,s.Controller)&&ReferenceEquals(ctx,s.Context));NoDestruction();}
        }));
        rows.Add(Case("cancellation lock timeout retries same owner when lock becomes available",()=>{
            var s=ManagedReady();using var entered=new ManualResetEventSlim();using var leave=new ManualResetEventSlim();
            var hold=Task.Run(()=>{lock(s.Controller!.OutputLock){entered.Set();Assert(leave.Wait(3000));}});Assert(entered.Wait(2000));
            TerminalTestPorts.AfterLockTimeout=()=>{leave.Set();hold.GetAwaiter().GetResult();};
            try{var receipt=SettleSession(s);CheckReceipt(receipt,true,2,true);AllClosed();Assert(TerminalTestPorts.LockTimeouts==1);}finally{leave.Set();hold.GetAwaiter().GetResult();}
        }));
        rows.Add(Case("healthy release invokes SDK Dispose once without terminal retry",()=>{
            var s=ManagedReady();CheckReceipt(SettleSession(s),true,1,false);AllClosed();Assert(World.ContextCalls==1);
        }));
        foreach(var cause in new[]{"controller-dispose","context-dispose","neutral","mixed-neutral-output","capture-identity","release-throws"})rows.Add(Case("non-stop-failure-kept-"+cause,()=>{
            OwnedWorkers.BlockOutput=cause=="mixed-neutral-output";var s=ManagedReady();
            World.ThrowContextDispose=cause=="controller-dispose";Fixture.ContextThrows=cause=="context-dispose";Fixture.NeutralFails=cause=="neutral"||cause=="mixed-neutral-output";
            if(cause=="capture-identity")TerminalTestPorts.CaptureIdentityFailure=true;
            Terminal.Receipt receipt;
            if(cause=="release-throws"){int calls=0;receipt=Terminal.Settle(new(0,"fixture"),(out string detail)=>{calls++;detail="";throw new IOException("injected-release");});Assert(calls==1);TerminalReceipts.Add(receipt);}
            else receipt=SettleSession(s);
            CheckReceipt(receipt,false,1,false);Assert(receipt.FirstDetail==receipt.FinalDetail,"non-stop failure was overwritten");
        }));
        rows.Add(Case("retry that reaches SDK Dispose failure retains both failure details",()=>{
            OwnedWorkers.BlockOutput=true;var s=ManagedReady();TerminalTestPorts.ReleaseOutputAfterTimeout=true;World.ThrowContextDispose=true;
            var r=SettleSession(s);CheckReceipt(r,false,2,true);Assert(r.FirstDetail=="controller-output-stop:sdk-output-stop-timeout"&&r.FinalDetail.Contains("controller-dispose:"));Assert(s.HasController&&s.HasContext);
        }));
        if(!args.Contains("--eligibility-only"))
        {
            rows.Add(await TerminalInitializationCase(false));
            rows.Add(await TerminalInitializationCase(true));
            foreach(var mode in new[]{"shutdown","shutdown-neutral-failure","shutdown-sdk-failure","invalid-shutdown","shutdown-write-failure-neutral","shutdown-write-failure-healthy","shutdown-parent-loss-neutral","exception","read-loss","connect-loss","already-dead","pipe-break"})rows.Add(await TerminalPipeCase(mode));
            foreach(var mode in new[]{"read-loss","shutdown"})foreach(var worker in new[]{"output","clock"})foreach(var transient in new[]{true,false})rows.Add(await TerminalPipeCase(mode,worker,transient));
            foreach(var mode in new[]{"exception","already-dead","pipe-break"})rows.Add(await TerminalPipeCase(mode,"output",false));
        }
        bool passed=rows.All(r=>Equals(r["pass"],true));Console.WriteLine(JsonSerializer.Serialize(new{passed,cases=rows,eligibilityOnly=args.Contains("--eligibility-only"),productionMainExecuted=false,actualMainTerminalBlockExecuted=!args.Contains("--eligibility-only"),realSdkAssemblyLoaded=false,realDriverIo=false}));return passed?0:1;
    }
}
