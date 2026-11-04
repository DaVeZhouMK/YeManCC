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
            if(args.Length!=3)throw new ArgumentException("native-publisher.exe expected-sha256 evidence-directory");
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
            foreach(var scenario in new[]{"event","power","topology","model","heartbeat","future-heartbeat","tamper-path","parent-exit","reject-model","reject-path","reject-token","reject-odd","foreign-helper","debt"})
            {
                var dir=Path.Combine(output,scenario);Directory.CreateDirectory(dir);
                var childResult=Path.Combine(dir,"child.json");var ready=@"Local\YMCC-SuperJoy-OwnerTest-Ready-"+Environment.ProcessId+"-"+Guid.NewGuid().ToString("N");
                var arguments=scenario=="debt"?new[]{"--debt"}:new[]{scenario,dotnet,assembly,childResult,ready};
                var start=new ProcessStartInfo(publisher){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
                foreach(var arg in arguments)start.ArgumentList.Add(arg);
                using var process=Process.Start(start)??throw new InvalidOperationException("native publisher start");
                var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
                using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(12));
                try{await process.WaitForExitAsync(deadline.Token);}
                catch{if(!process.HasExited)process.Kill(entireProcessTree:true);throw;}
                var text=await stdout;var error=await stderr;
                await File.WriteAllTextAsync(Path.Combine(dir,"native.stdout.log"),text);
                await File.WriteAllTextAsync(Path.Combine(dir,"native.stderr.log"),error);
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
                cases.Add(new{scenario,status="PASS",nativePid=process.Id,nativeExitCode=process.ExitCode,native,child,command=new{file=publisher,arguments}});
            }
            if(Hash(publisher)!=expected)throw new Exception("OWNER_PUBLISHER_CHANGED");
            var result=new{status="PASS",scope="Original LightSetter owner-only variant; real native publication, exact owned processes and managed software pipe terminals",cases,
                parentExecutableSha256=expected,managedFixtureSha256=Hash(assembly),normalProductMainExecuted=false,nativeHidOpenExecuted=false,hardwareIo=false,
                runtimeRouteAdmitted=false,wholeGoalComplete=false};
            await File.WriteAllTextAsync(Path.Combine(output,"result.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true})+"\n");
            Console.WriteLine(JsonSerializer.Serialize(new{result.status,cases=cases.Count,result.hardwareIo,result.runtimeRouteAdmitted}));return 0;
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
    private static async Task AtomicWrite(string path,object value)
    {
        var temporary=path+"."+Environment.ProcessId+".tmp";
        await File.WriteAllTextAsync(temporary,JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true})+"\n");
        File.Move(temporary,path,true);
    }
    private static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
}
