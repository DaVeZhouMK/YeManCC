using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

// Explicit owner-only variant of the existing LightSetter fixture. The native
// publisher is the reviewed test executable, and its managed child never calls
// product Main or native HID Open. Normal LightSetter assertions remain intact.
internal static class SuperJoyOwnerLeaseTests
{
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Contains("--expected-device-class",StringComparer.Ordinal))
                return SuperJoyNativeCompositionChild.Run(args);
            if(args.Length==7&&args[0]=="--lease-child")return await Child(args);
            var apexOwner=args.Length==4&&args[3]=="--apex-owner";
            if(args.Length!=3&&!apexOwner)throw new ArgumentException("native-publisher.exe expected-sha256 evidence-directory [--apex-owner]");
            var publisher=Path.GetFullPath(args[0]);
            var expected=args[1];
            if(expected.Length!=64||Hash(publisher)!=expected)throw new InvalidOperationException("OWNER_PUBLISHER_IDENTITY_MISMATCH");
            var output=Path.GetFullPath(args[2]);var repo=Path.GetFullPath(Environment.CurrentDirectory).TrimEnd('\\');
            if(output.Equals(repo,StringComparison.OrdinalIgnoreCase)||output.StartsWith(repo+"\\",StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Owner evidence must be external");
            Directory.CreateDirectory(output);
            if(File.Exists(Path.Combine(output,"result.json")))throw new IOException("Owner evidence already exists");
            var dotnet=Environment.ProcessPath??throw new InvalidOperationException("dotnet path");
            var assembly=Assembly.GetExecutingAssembly().Location;var cases=new List<object>();
            var scenarios=new[]{"event","power","topology","model","heartbeat","future-heartbeat","tamper-path","parent-exit","reject-model","reject-path","reject-token","reject-odd","foreign-helper","debt"};
            var selectedScenarioNames=scenarios.Select(scenario=>apexOwner?"apex-"+scenario:scenario).ToArray();
            var actualScenarioNames=new List<string>();
            foreach(var scenario in scenarios)
            {
                var selectedScenario=apexOwner?"apex-"+scenario:scenario;
                var dir=Path.Combine(output,selectedScenario);
                if(Directory.Exists(dir))throw new IOException("Owner scenario evidence already exists: "+selectedScenario);
                Directory.CreateDirectory(dir);
                var childResult=Path.Combine(dir,"child.json");var ready=@"Local\YMCC-SuperJoy-OwnerTest-Ready-"+Environment.ProcessId+"-"+Guid.NewGuid().ToString("N");
                var arguments=scenario=="debt"?new[]{apexOwner?selectedScenario:"--debt"}:new[]{selectedScenario,dotnet,assembly,childResult,ready};
                var start=new ProcessStartInfo(publisher){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
                foreach(var arg in arguments)start.ArgumentList.Add(arg);
                using var process=Process.Start(start)??throw new InvalidOperationException("native publisher start");
                var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
                using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(12));
                try{await process.WaitForExitAsync(deadline.Token);}
                catch{if(!process.HasExited)process.Kill(entireProcessTree:true);throw;}
                var text=await stdout;var error=await stderr;
                await WriteNew(Path.Combine(dir,"native.stdout.log"),text);
                await WriteNew(Path.Combine(dir,"native.stderr.log"),error);
                if(process.ExitCode!=0)throw new Exception(scenario+" native failed: "+error);
                using var nativeDoc=JsonDocument.Parse(text.Trim());var native=nativeDoc.RootElement.Clone();
                JsonElement? child=null;
                if(scenario!="debt")
                {
                    var limit=Stopwatch.StartNew();while(!File.Exists(childResult)&&limit.ElapsedMilliseconds<4000)await Task.Delay(20);
                    if(!File.Exists(childResult))throw new Exception(scenario+" missing child result after native exit");
                    using var childDoc=JsonDocument.Parse(await File.ReadAllTextAsync(childResult));child=childDoc.RootElement.Clone();
                    if(child.Value.GetProperty("status").GetString()!="PASS")throw new Exception(scenario+" child failed: "+child.Value);
                    if(scenario.StartsWith("reject-",StringComparison.Ordinal)||scenario=="foreign-helper")
                    {
                        if(!child.Value.GetProperty("rejectedBeforeIo").GetBoolean())throw new Exception("Expected admission refusal");
                    }
                    else if(!child.Value.GetProperty("pendingReadCancelled").GetBoolean()||!child.Value.GetProperty("lateWriteRefused").GetBoolean()||
                        !child.Value.GetProperty("leaseTokenCancelled").GetBoolean()||!child.Value.GetProperty("actualPendingRead").GetBoolean())
                        throw new Exception(scenario+" owner/OS cancellation not complete");
                    if(scenario=="parent-exit")
                    {
                        if(native.GetProperty("status").GetString()!="PARENT_EXIT_ARMED"||!child.Value.GetProperty("parentExited").GetBoolean())
                            throw new Exception("Parent death not observed");
                        await ObserveExactChildExit(native.GetProperty("childPid").GetInt32(),native.GetProperty("childBirth").GetUInt64());
                    }
                }
                if(scenario!="parent-exit"&&native.GetProperty("status").GetString()!="PASS")throw new Exception(scenario+" native status");
                if(apexOwner&&native.GetProperty("scenario").GetString()!=selectedScenario)throw new Exception("Native selected scenario mismatch");
                actualScenarioNames.Add(selectedScenario);
                cases.Add(new{scenario=selectedScenario,status="PASS",nativePid=process.Id,nativeExitCode=process.ExitCode,native,child,command=new{file=publisher,arguments}});
            }
            if(Hash(publisher)!=expected)throw new Exception("OWNER_PUBLISHER_CHANGED");
            if(cases.Count!=selectedScenarioNames.Length||!actualScenarioNames.SequenceEqual(selectedScenarioNames,StringComparer.Ordinal))
                throw new Exception("Owner scenario names/count mismatch");
            var result=new{status="PASS",scope=apexOwner?"APEX lease OS-pipe; not ApexWindowsHidTransport":"Original LightSetter owner-only variant; real native publication, exact owned processes and managed software pipe terminals",
                apexOwner,selectedScenarioNames,actualScenarioNames,actualCount=cases.Count,cases,
                parentExecutableSha256=expected,managedFixtureSha256=Hash(assembly),normalProductMainExecuted=false,nativeHidOpenExecuted=false,hardwareIo=false,
                runtimeRouteAdmitted=false,wholeGoalComplete=false};
            await WriteNew(Path.Combine(output,"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true})+"\n");
            Console.WriteLine(JsonSerializer.Serialize(new{result.status,apexOwner,cases=cases.Count,actualCount=cases.Count,scenarioNames=actualScenarioNames,result.hardwareIo,result.runtimeRouteAdmitted}));return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
    private static async Task ObserveExactChildExit(int pid,ulong birth)
    {
        Process child;
        try{child=Process.GetProcessById(pid);}
        catch(ArgumentException){return;}
        using(child)
        {
            _=child.SafeHandle;
            if(child.HasExited||(ulong)child.StartTime.ToUniversalTime().ToFileTimeUtc()!=birth)return;
            using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try{await child.WaitForExitAsync(deadline.Token);}
            catch{if(!child.HasExited)child.Kill();throw;}
        }
    }
    private static async Task<int> Child(string[] args)
    {
        if(string.Equals(args[1],ApexLightingConsumer.Model,StringComparison.Ordinal))return await ApexChild(args);
        var model=args[1];var path=args[2];var admission=args[3];var resultFile=Path.GetFullPath(args[4]);var readyName=args[5];var expectedRejection=args[6]=="reject";
        var imports=new List<string>();
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),(name,_,_)=>
        {
            lock(imports)imports.Add(name);
            if(name.Equals("hid.dll",StringComparison.OrdinalIgnoreCase)||name.Equals("setupapi.dll",StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("PHYSICAL_HID_IMPORT_FORBIDDEN");
            return IntPtr.Zero;
        });
        using var ready=EventWaitHandle.OpenExisting(readyName);
        object result;
        try
        {
            var fields=SuperJoyOwnerLease.ParseToken(admission);
            var identity=new SuperJoyIdentity(model,path,checked((long)fields[2]));
            SuperJoyOwnerLease lease;
            try{lease=SuperJoyOwnerLease.Acquire(identity,admission);}
            catch(IOException error)when(expectedRejection&&error.Message=="SUPERJOY_OWNER_NOT_ACTIVE")
            {
                result=new{status="PASS",rejectedBeforeIo=true,reason=error.Message,HCAssemblyLoaded=false,nativeHidOpenExecuted=false,imports};
                await AtomicWrite(resultFile,result);ready.Set();return 0;
            }
            using(lease)
            {
                if(expectedRejection)throw new Exception("Native contract mismatch was admitted");
                lease.EnsureCurrent();
                using var pair=await SuperJoyWindowsTransportTests.PipePair.Open();
                var issued=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                using var transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,identity,()=>lease.IsCurrent,lease.Token,()=>issued.TrySetResult());
                var request=AyaneoSuperJoyLightingSession.BuildReport(new(1,20,255),new(3,80,7),33,true);
                var written=await transport.WriteAsync(request,lease.Token);
                var received=new byte[65];await pair.Peer.ReadExactlyAsync(received);
                if(written!=65||!received.SequenceEqual(request))throw new Exception("Owned OS write not complete");
                var pending=transport.ReadAsync(new byte[65],CancellationToken.None).AsTask();
                await issued.Task.WaitAsync(TimeSpan.FromSeconds(1));
                var actualPending=!pending.IsCompleted&&transport.LastReadIssuedIncomplete&&!transport.LastReadTokenCancelledAtIssue;
                if(!actualPending)throw new Exception("No actual pending read before native action");
                var start=Stopwatch.StartNew();ready.Set();var cancelled=false;
                try{await pending.WaitAsync(TimeSpan.FromSeconds(5));}catch(OperationCanceledException){cancelled=true;}
                var elapsed=start.ElapsedMilliseconds;var refused=false;
                try{await transport.WriteAsync(request,CancellationToken.None);}catch(Exception error)when(error is OperationCanceledException or ObjectDisposedException){refused=true;}
                var parentExited=false;
                try{using var parent=Process.GetProcessById((int)fields[0]);parentExited=parent.HasExited||(ulong)parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=fields[1];}
                catch(ArgumentException){parentExited=true;}
                if(!cancelled||!lease.Token.IsCancellationRequested||!refused||transport.IsConnected||!transport.LastReadCancelledWhileIncomplete)
                    throw new Exception("Native lease did not retire the exact OS terminal");
                result=new{status="PASS",actualPendingRead=actualPending,pendingReadCancelled=cancelled,lateWriteRefused=refused,
                    leaseTokenCancelled=lease.Token.IsCancellationRequested,leaseCurrentAfterStop=lease.IsCurrent,transportClosed=!transport.IsConnected,
                    nativeCancelIoOutcomes=transport.CancelIoOutcomes.ToArray(),revocationReason=lease.RevocationReason,observedWaitMilliseconds=elapsed,
                    parentPid=fields[0],parentBirth=fields[1],parentExited,initialOwnedWriteBytes=written,recordSize=SuperJoyOwnerLease.RecordSize,
                    nativeHidOpenExecuted=false,HCAssemblyLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),imports};
            }
            await AtomicWrite(resultFile,result);return 0;
        }
        catch(Exception error)
        {
            await AtomicWrite(resultFile,new{status="FAIL",error=error.ToString(),imports});ready.Set();return 1;
        }
    }
    private static async Task<int> ApexChild(string[] args)
    {
        var model=args[1];var path=args[2];var admission=args[3];var resultFile=Path.GetFullPath(args[4]);var readyName=args[5];var expectedRejection=args[6]=="reject";
        if(!string.Equals(model,ApexLightingConsumer.Model,StringComparison.Ordinal))throw new ArgumentException("Exact Apex child model required");
        var imports=new List<string>();
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),(name,_,_)=>
        {
            lock(imports)imports.Add(name);
            if(name.Equals("hid.dll",StringComparison.OrdinalIgnoreCase)||name.Equals("setupapi.dll",StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("PHYSICAL_HID_IMPORT_FORBIDDEN");
            return IntPtr.Zero;
        });
        using var ready=EventWaitHandle.OpenExisting(readyName);
        object result;
        try
        {
            var fields=SuperJoyOwnerLease.ParseToken(admission);
            var identity=ApexLightingConsumer.OwnerIdentity(path,checked((long)fields[2]));
            SuperJoyOwnerLease lease;
            try{lease=SuperJoyOwnerLease.Acquire(identity,admission);}
            catch(IOException error)when(expectedRejection&&error.Message=="SUPERJOY_OWNER_NOT_ACTIVE")
            {
                result=new{status="PASS",scope="APEX lease OS-pipe",model,rejectedBeforeIo=true,reason=error.Message,HCAssemblyLoaded=false,nativeHidOpenExecuted=false,imports};
                await AtomicWrite(resultFile,result);ready.Set();return 0;
            }
            using(lease)
            {
                if(expectedRejection)throw new Exception("Native contract mismatch was admitted");
                lease.EnsureCurrent();
                using var pair=await SuperJoyWindowsTransportTests.PipePair.Open();
                using var pipe=new FileStream(pair.Handle,FileAccess.ReadWrite,65,true);
                var issuedWrites=0;var completedWrites=0;
                async Task WriteCurrent(ReadOnlyMemory<byte> report,CancellationToken token)
                {
                    lease.EnsureCurrent();
                    using var stop=CancellationTokenSource.CreateLinkedTokenSource(lease.Token,token);
                    stop.Token.ThrowIfCancellationRequested();++issuedWrites;
                    await pipe.WriteAsync(report,stop.Token);lease.EnsureCurrent();++completedWrites;
                }
                var request=ApexLightingConsumer.BuildSolid(1,20,255);
                await WriteCurrent(request,CancellationToken.None);
                var received=new byte[65];await pair.Peer.ReadExactlyAsync(received);
                if(completedWrites!=1||!received.SequenceEqual(request))throw new Exception("Apex lease pipe source V2 write not complete");
                var pending=pipe.ReadAsync(new byte[65],lease.Token).AsTask();
                var actualPending=!pending.IsCompleted&&!lease.Token.IsCancellationRequested;
                if(!actualPending)throw new Exception("No actual Apex lease pipe pending read before native action");
                var start=Stopwatch.StartNew();ready.Set();var cancelled=false;
                try{await pending.WaitAsync(TimeSpan.FromSeconds(5));}catch(OperationCanceledException){cancelled=true;}
                var elapsed=start.ElapsedMilliseconds;var refused=false;
                try{await WriteCurrent(request,CancellationToken.None);}catch(Exception error)when(error is OperationCanceledException or ObjectDisposedException){refused=true;}
                pipe.Dispose();
                var parentExited=false;
                try{using var parent=Process.GetProcessById((int)fields[0]);parentExited=parent.HasExited||(ulong)parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=fields[1];}
                catch(ArgumentException){parentExited=true;}
                if(!cancelled||!pending.IsCanceled||!lease.Token.IsCancellationRequested||!refused||issuedWrites!=1||completedWrites!=1||!pair.Handle.IsClosed||lease.IsCurrent)
                    throw new Exception("Apex lease did not cancel and close the exact OS pipe terminal");
                result=new{status="PASS",scope="APEX lease OS-pipe; not ApexWindowsHidTransport",model,actualPendingRead=actualPending,pendingReadCancelled=cancelled,
                    lateWriteRefused=refused,leaseTokenCancelled=lease.Token.IsCancellationRequested,leaseCurrentAfterStop=lease.IsCurrent,pipeHandleClosed=pair.Handle.IsClosed,
                    sourceV2ReportWrites=completedWrites,issuedWrites,observedWaitMilliseconds=elapsed,revocationReason=lease.RevocationReason,
                    parentPid=fields[0],parentBirth=fields[1],parentExited,initialOwnedWriteBytes=request.Length,recordSize=SuperJoyOwnerLease.RecordSize,
                    apexWindowsHidTransportExecuted=false,nativeHidOpenExecuted=false,physicalEffectVerified=false,
                    HCAssemblyLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),imports};
            }
            await AtomicWrite(resultFile,result);return 0;
        }
        catch(Exception error)
        {
            await AtomicWrite(resultFile,new{status="FAIL",scope="APEX lease OS-pipe",error=error.ToString(),imports});ready.Set();return 1;
        }
    }
    private static async Task WriteNew(string path,string text)
    {
        await using var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None);
        await file.WriteAsync(System.Text.Encoding.UTF8.GetBytes(text));await file.FlushAsync();file.Flush(true);
    }
    private static async Task AtomicWrite(string path,object value)
    {
        var temporary=path+"."+Environment.ProcessId+".tmp";
        await WriteNew(temporary,JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true})+"\n");
        File.Move(temporary,path);
    }
    private static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
}
