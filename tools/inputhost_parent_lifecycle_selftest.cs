using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    private static Stage? _msiHostStageEvidence;
    private sealed class Stage { public void BeginRead(){} public void EndRead(int n){} public void BeginHandle(){} public void EndHandle(){} public void BeginWrite(){} public void EndWrite(){} }
    private static readonly List<string> Fatal = new();
    private static void LogFatalExit(string kind,string detail) { lock(Fatal) Fatal.Add(kind); }
    private sealed class HostSession
    {
        public int Releases, Handled;
        public bool CostEnabledForRead=>false;
        public void RecordCostPipeRead(long n){}
        public bool ReleaseLocal(out string detail){Interlocked.Increment(ref Releases);detail="fake-release";return true;}
        public string Handle(string message,out bool shutdown){
            Interlocked.Increment(ref Handled);shutdown=message=="SHUTDOWN";
            if(shutdown){var released=ReleaseLocal(out var detail);return JsonSerializer.Serialize(new { ok=released, status="shutdown", command="SHUTDOWN", detail });}
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
    private static async Task<int> Main(string[] args)
    {
        if(args.SequenceEqual(new[]{"--parent"})){Console.WriteLine("READY");Console.In.ReadLine();return 0;}
        var rows=new List<Dictionary<string,object>>();
        foreach(var mode in new[]{"healthy-idle","pipe-break","parent-loss-during-read","parent-loss-during-write"})rows.Add(await Connected(mode));
        rows.Add(await ConnectLoss());
        var passed=rows.All(r=>(bool)r["pass"]);Console.WriteLine(JsonSerializer.Serialize(new{passed,cases=rows,realBclPipes=true,ownedChildProcesses=true,hardwareIo=false,productMainExecuted=false}));return passed?0:1;
    }
}
