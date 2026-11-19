using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    private static Stage? _msiHostStageEvidence;
    private static HostTerminalCleanup.ProcessExit? _terminalProcessExit;
    private sealed class Stage { public void BeginRead(){} public void EndRead(int n){} public void BeginHandle(){} public void EndHandle(){} public void BeginWrite(){} public void EndWrite(){} }
    private static readonly List<string> Fatal = new();
    private static Action<string>? BeforeFatal;
    private static void LogFatalExit(string kind,string detail) { BeforeFatal?.Invoke(kind); lock(Fatal) Fatal.Add(kind); }
    private sealed class HostSession
    {
        public int Releases, Handled;
        public bool ReleaseSucceeds=true;
        public bool CostEnabledForRead=>false;
        public void RecordCostPipeRead(long n){}
        // Session parsing remains the fixture's explicit recorder; the actual
        // UTF8 pipe loop and codec consume their current production signature.
        public string HandleUtf8(ReadOnlyMemory<byte> message,out bool shutdown)=>Handle(Encoding.UTF8.GetString(message.Span),out shutdown);
        public bool ReleaseLocal(out string detail){Interlocked.Increment(ref Releases);detail=ReleaseSucceeds?"fake-release":"fixture-release-unconfirmed";return ReleaseSucceeds;}
        public string Handle(string message,out bool shutdown){
            Interlocked.Increment(ref Handled);shutdown=message=="SHUTDOWN";
            if(message=="THROW_HANDLE")throw new InvalidOperationException("fixture-handle-exception");
            if(message=="BLOCK_HANDLE"){Console.WriteLine("BLOCKED-HANDLE");Console.Out.Flush();Thread.Sleep(Timeout.Infinite);}
            if(shutdown){_terminalProcessExit?.Begin();var released=ReleaseLocal(out var detail);return JsonSerializer.Serialize(new { ok=released, status="shutdown", command="SHUTDOWN", detail });}
            return message=="BLOCK_WRITE"?new string('x',16000):"ACK";
        }
    }
    // This is a test continuation around the actual helper, not product Main.
    // ReleaseLocal is still a recorder; real managed worker cleanup is proved by
    // the separate actual-source managed-terminal consumer.
    private static Task<int> RunTerminalForFixtureAsync(string pipeName,int parentPid,HostSession session)=>
        HostTerminalCleanup.RunAsync(()=>RunPipeClientAsync(pipeName,parentPid,session),session.ReleaseLocal,LogFatalExit);
    private static Process StartParent()
    {
        var start=new ProcessStartInfo(Environment.ProcessPath!){RedirectStandardInput=true,RedirectStandardOutput=true,UseShellExecute=false,CreateNoWindow=true};
        start.ArgumentList.Add(typeof(Program).Assembly.Location);start.ArgumentList.Add("--parent");
        var p=Process.Start(start)!;if(p.StandardOutput.ReadLine()!="READY")throw new Exception("child handshake");return p;
    }
    private static async Task ExitParent(Process p){p.StandardInput.Close();await p.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));}
    private static NamedPipeServerStream Server(string name)=>new(name,PipeDirection.InOut,1,PipeTransmissionMode.Message,PipeOptions.Asynchronous,1024,1024);
    private static async Task Write(NamedPipeServerStream server,string value)=>await server.WriteAsync(Encoding.UTF8.GetBytes(value));
    private static async Task<bool> Ends(Task<int> task,int ms=1500)=>await Task.WhenAny(task,Task.Delay(ms))==task;
    private static async Task<Dictionary<string,object>> Connected(string mode)
    {
        Fatal.Clear();using var parent=StartParent();var name="ymcc-offline-host-lifetime-"+Guid.NewGuid().ToString("N");
        await using var server=Server(name);var waiting=server.WaitForConnectionAsync();var session=new HostSession();var run=RunTerminalForFixtureAsync(name,parent.Id,session);
        await waiting.WaitAsync(TimeSpan.FromSeconds(3));
        bool passed;bool pendingBefore=true;int releasesBefore=0;
        try {
            if(mode=="healthy-idle") {
                await Task.Delay(100);pendingBefore=!run.IsCompleted;releasesBefore=session.Releases;
                await Write(server,"SHUTDOWN");var b=new byte[32];await server.ReadAsync(b).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
                passed=pendingBefore&&releasesBefore==0&&await Ends(run)&&await run==0&&session.Handled==1;
            } else if(mode=="pipe-break") {
                server.Dispose();passed=await Ends(run)&&session.Releases>0&&parent.HasExited==false;
            } else {
                if(mode=="parent-loss-during-write") {
                    await Write(server,"BLOCK_WRITE");var watch=Stopwatch.StartNew();while(session.Handled==0&&watch.ElapsedMilliseconds<1500)await Task.Delay(5);
                    await Task.Delay(100);pendingBefore=!run.IsCompleted;
                }
                await ExitParent(parent);passed=await Ends(run)&&session.Releases>0;
                if(mode=="parent-loss-during-write")passed&=pendingBefore&&session.Handled==1;
            }
            return new(){["name"]=mode,["pass"]=passed,["releasedBeforeForcedPipeClose"]=session.Releases,["pendingBefore"]=pendingBefore,["fatal"]=Fatal.ToArray(),["handled"]=session.Handled};
        } finally {server.Dispose();await run.WaitAsync(TimeSpan.FromSeconds(5));if(!parent.HasExited)await ExitParent(parent);}
    }
    private static async Task<Dictionary<string,object>> ConnectLoss()
    {
        using var parent=StartParent();var session=new HostSession();var run=RunTerminalForFixtureAsync("ymcc-offline-absent-"+Guid.NewGuid().ToString("N"),parent.Id,session);
        await ExitParent(parent);var timely=await Ends(run);await run.WaitAsync(TimeSpan.FromSeconds(5));
        return new(){["name"]="parent-loss-during-connect",["pass"]=timely&&session.Releases>0,["completedWithin1500ms"]=timely,["handled"]=session.Handled};
    }
    // Exercise the actual pipe loop AND terminal helper. The log is held open
    // while the original release recorder is inspected; ending the fixture or
    // forcibly closing the peer cannot masquerade as release-before-log.
    private static async Task<Dictionary<string,object>> BlockedTerminalLog(string mode)
    {
        Fatal.Clear();using var parent=StartParent();var name="ymcc-offline-terminal-log-"+Guid.NewGuid().ToString("N");
        await using var server=Server(name);var waiting=server.WaitForConnectionAsync();
        var session=new HostSession{ReleaseSucceeds=mode!="release-failure-log-blocked"};
        var blockedReason=mode=="handle-exception-log-blocked"?"fatal-exception":mode=="parent-loss-log-blocked"?"parent-exited":mode=="shutdown-log-blocked"?"shutdown-ordered":"read-failed";
        using var allowLog=new ManualResetEventSlim();
        var entered=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        BeforeFatal=kind=>{if(kind==blockedReason){entered.TrySetResult();allowLog.Wait();}};
        var run=RunTerminalForFixtureAsync(name,parent.Id,session);
        bool pass=false,logEntered=false,pendingAtLog=false,parentAliveAtLog=false;int releasedAtLog=-1;int? exit=null;string? error=null;
        try
        {
            await waiting.WaitAsync(TimeSpan.FromSeconds(3));
            if(mode=="handle-exception-log-blocked")await Write(server,"THROW_HANDLE");
            else if(mode=="parent-loss-log-blocked")await ExitParent(parent);
            else if(mode=="shutdown-log-blocked")
            {
                await Write(server,"SHUTDOWN");var buffer=new byte[1024];
                await server.ReadAsync(buffer).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
            }
            else server.Dispose();
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(3));logEntered=true;
            releasedAtLog=Volatile.Read(ref session.Releases);pendingAtLog=!run.IsCompleted;parentAliveAtLog=!parent.HasExited;
            allowLog.Set();exit=await run.WaitAsync(TimeSpan.FromSeconds(3));
            var expected=mode=="handle-exception-log-blocked"?4:mode is "parent-loss-log-blocked" or "shutdown-log-blocked"?0:5;
            var expectedReceipt=session.ReleaseSucceeds?"terminal-cleanup-complete":"terminal-cleanup-failed";
            pass=releasedAtLog==1&&pendingAtLog&&session.Releases==1&&exit==expected&&
                Fatal.Count(f=>f==blockedReason)==1&&Fatal.Count(f=>f==expectedReceipt)==1&&
                (mode=="parent-loss-log-blocked"||parentAliveAtLog);
        }
        catch(Exception ex){error=ex.ToString();}
        finally
        {
            allowLog.Set();server.Dispose();
            try{await run.WaitAsync(TimeSpan.FromSeconds(5));}finally{BeforeFatal=null;if(!parent.HasExited)await ExitParent(parent);}
        }
        return new(){["name"]=mode,["pass"]=pass,["error"]=error??"",["logEntered"]=logEntered,["releasedWhileLogBlocked"]=releasedAtLog,["terminalStillWaitingForLog"]=pendingAtLog,["parentAliveAtLog"]=parentAliveAtLog,["releasesAfterLog"]=session.Releases,["exit"]=exit??-1,["fatal"]=Fatal.ToArray()};
    }
    // Only these inert children create a process deadline. The ordinary pipe
    // cases above still exercise their original unmodified release recorders.
    private static async Task<int> TerminalExitChild(string mode,string file,int parentPid,string pipeName)
    {
        _terminalProcessExit=new HostTerminalCleanup.ProcessExit();
        using var held=new FileStream(file,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None);
        held.WriteByte(0x41);held.Flush(true);
        Console.WriteLine("LOCKED");Console.Out.Flush();
        if(Console.In.ReadLine()!="GO")return 2;
        if(mode=="terminal-foreground-tail")
            new Thread(()=>Thread.Sleep(Timeout.Infinite)){IsBackground=false}.Start();
        int result;
        if(mode is "terminal-parent-loss-in-handle" or "terminal-healthy-unarmed")
        {
            var session=new HostSession();
            result=await HostTerminalCleanup.RunAsync(()=>RunPipeClientAsync(pipeName,parentPid,session),
                session.ReleaseLocal,(reason,detail)=>Console.WriteLine("LOG:"+reason),_terminalProcessExit.Begin);
        }
        else
        {
            bool Release(out string detail)
            {
                Console.WriteLine("RELEASE");Console.Out.Flush();detail="fixture-release";
                if(mode=="terminal-release-blocked")
                {
                    // Repeated requests must not move the first deadline.
                    new Thread(()=>{for(;;){_terminalProcessExit.Begin();Thread.Sleep(100);}}){IsBackground=true}.Start();
                    Thread.Sleep(Timeout.Infinite);
                }
                return true;
            }
            result=await HostTerminalCleanup.RunAsync(()=>Task.FromResult(new HostTerminalCleanup.LoopResult(0,"fixture-terminal",DiagnosticDetail:"fixture")),
                Release,(reason,detail)=>{Console.WriteLine("LOG:"+reason);Console.Out.Flush();if(mode=="terminal-log-blocked")Thread.Sleep(Timeout.Infinite);},
                _terminalProcessExit.Begin);
        }
        _terminalProcessExit.Complete(result);
        return result;
    }

    private static async Task<Dictionary<string,object>> TerminalExitCase(string mode)
    {
        var file=Path.Combine(Environment.CurrentDirectory,"update-target-"+Guid.NewGuid().ToString("N")+".bin");
        var pipeName="ymcc-offline-terminal-"+Guid.NewGuid().ToString("N");
        using var parent=StartParent();
        await using var server=Server(pipeName);
        var connected=server.WaitForConnectionAsync();
        var start=new ProcessStartInfo(Environment.ProcessPath!){RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false,CreateNoWindow=true};
        foreach(var arg in new[]{typeof(Program).Assembly.Location,"--terminal-child",mode,file,parent.Id.ToString(),pipeName})start.ArgumentList.Add(arg);
        using var child=Process.Start(start)!;
        bool locked=false,selfExited=false,rewritable=false,salvaged=false,healthyPastDeadline=false;
        int? exit=null;string error="",tail="";var timer=new Stopwatch();
        try
        {
            var first=await child.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(5));
            if(first!="LOCKED")throw new Exception("terminal child failed file-lock handshake: "+first);
            try{using var check=new FileStream(file,FileMode.Open,FileAccess.Write,FileShare.None);}
            catch(IOException){locked=true;}
            timer.Start();await child.StandardInput.WriteLineAsync("GO");await child.StandardInput.FlushAsync();
            if(mode is "terminal-parent-loss-in-handle" or "terminal-healthy-unarmed")
            {
                await connected.WaitAsync(TimeSpan.FromSeconds(5));
                if(mode=="terminal-parent-loss-in-handle")
                {
                    await Write(server,"BLOCK_HANDLE");
                    if(await child.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(3))!="BLOCKED-HANDLE")throw new Exception("missing blocked handler");
                    await ExitParent(parent);timer.Restart();
                }
                else
                {
                    await Write(server,"PING");var buffer=new byte[1024];await server.ReadAsync(buffer).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
                    await Task.Delay(HostTerminalCleanup.ProcessExit.DeadlineMilliseconds+500);
                    healthyPastDeadline=!child.HasExited;
                    await Write(server,"SHUTDOWN");await server.ReadAsync(buffer).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
                }
            }
            await child.WaitForExitAsync().WaitAsync(TimeSpan.FromMilliseconds(HostTerminalCleanup.ProcessExit.DeadlineMilliseconds+3000));
            selfExited=true;exit=child.ExitCode;
        }
        catch(Exception ex){error=ex.GetType().Name+":"+ex.Message;}
        finally
        {
            if(!child.HasExited){salvaged=true;child.Kill();await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));}
            tail=await child.StandardOutput.ReadToEndAsync();
            error+=await child.StandardError.ReadToEndAsync();
            if(!parent.HasExited)await ExitParent(parent);
            if(File.Exists(file))
            {
                try{using var update=new FileStream(file,FileMode.Open,FileAccess.Write,FileShare.None);update.WriteByte(0x42);update.Flush(true);rewritable=true;}
                catch(IOException){}
            }
        }
        var expected=mode is "terminal-foreground-tail" or "terminal-healthy-unarmed"?0:HostTerminalCleanup.ReleaseFailedExitCode;
        var passed=locked&&selfExited&&!salvaged&&rewritable&&exit==expected&&error.Length==0&&
            (mode!="terminal-healthy-unarmed"||healthyPastDeadline)&&
            (mode!="terminal-log-blocked"||!tail.Contains("LOG:terminal-cleanup-complete"));
        return new(){["name"]=mode,["pass"]=passed,["childPid"]=child.Id,["initialFileLocked"]=locked,["selfExited"]=selfExited,
            ["expectedExit"]=expected,["actualExit"]=exit??-1,["fixtureForceCleanup"]=salvaged,["fileRewritableAfterExit"]=rewritable,
            ["healthyBeyondTerminalBudget"]=healthyPastDeadline,["elapsedMs"]=timer.Elapsed.TotalMilliseconds,["tail"]=tail,["error"]=error};
    }

    private static async Task<int> Main(string[] args)
    {
        if(args.Length==5&&args[0]=="--terminal-child")return await TerminalExitChild(args[1],args[2],int.Parse(args[3]),args[4]);
        if(args.SequenceEqual(new[]{"--parent"})){Console.WriteLine("READY");Console.In.ReadLine();return 0;}
        var selectedCase=args.Length==2&&args[0]=="--case"?args[1]:null;
        var modes=new[]{"healthy-idle","pipe-break","parent-loss-during-read","parent-loss-during-write"};
        var blockedModes=new[]{"pipe-break-log-blocked","handle-exception-log-blocked","parent-loss-log-blocked","shutdown-log-blocked","release-failure-log-blocked"};
        var terminalModes=new[]{"terminal-foreground-tail","terminal-release-blocked","terminal-log-blocked","terminal-parent-loss-in-handle","terminal-healthy-unarmed"};
        var selectedCases=selectedCase?.Split(',');var known=modes.Concat(new[]{"parent-loss-during-connect"}).Concat(blockedModes).Concat(terminalModes).ToArray();
        if((args.Length!=0&&selectedCase is null)||(selectedCases is not null&&(selectedCases.Length==0||selectedCases.Distinct().Count()!=selectedCases.Length||selectedCases.Any(c=>!known.Contains(c)))))return 2;
        var rows=new List<Dictionary<string,object>>();
        foreach(var mode in selectedCases??known.Take(10))
            rows.Add(mode=="parent-loss-during-connect"?await ConnectLoss():blockedModes.Contains(mode)?await BlockedTerminalLog(mode):terminalModes.Contains(mode)?await TerminalExitCase(mode):await Connected(mode));
        var passed=rows.All(r=>(bool)r["pass"]);Console.WriteLine(JsonSerializer.Serialize(new{passed,selectedCase,selectedCases,cases=rows,realBclPipes=true,ownedChildProcesses=true,hardwareIo=false,productMainExecuted=false}));return passed?0:1;
    }
}
