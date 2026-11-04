using System.IO;
using System.Text.Json;

// Original owner fixture child: shipping parser, sole generated catalog, exact
// native lease and combined consumer. Only the physical terminals are replaced.
internal static class SuperJoyNativeCompositionChild
{
    internal static int Run(string[] args)
    {
        if(Environment.GetEnvironmentVariable("YMCC_SUPERJOY_COMPOSITION_FIXTURE")!="1")
            throw new InvalidOperationException("EXPLICIT_NATIVE_COMPOSITION_FIXTURE_REQUIRED");
        var mode=Environment.GetEnvironmentVariable("YMCC_SJ_COMPOSITION_MODE")??"ok";
        var issued=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var ready=string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("YMCC_SJ_READY_EVENT"))?null:
            EventWaitHandle.OpenExisting(Environment.GetEnvironmentVariable("YMCC_SJ_READY_EVENT")!);
        SuperJoyWindowsTransportTests.PipePair? pair=null;
        CancellationTokenSource? peerStop=null;Task peer=Task.CompletedTask;
        SuperJoyWindowsHidTransport? transport=null;SuperJoyOwnerLease? owner=null;
        var packets=new List<byte[]>();var ownerRevokedAtDispose=false;var ownerCurrentAtDispose=false;
        var ec=new MemoryEc(mode=="cec-fail"?8:0,()=>{
            ownerRevokedAtDispose=owner!.Token.IsCancellationRequested;ownerCurrentAtDispose=owner.IsCurrent;
        });
        Dictionary<string,object?> data;
        try
        {
            data=Program.RunWithSuperJoyPorts(args,(identity,lease,stop)=>
            {
                owner=lease;pair=SuperJoyWindowsTransportTests.PipePair.Open().GetAwaiter().GetResult();
                peerStop=CancellationTokenSource.CreateLinkedTokenSource(stop);
                peer=Task.Run(async()=>
                {
                    try
                    {
                        for(var n=0;n<3;n++)
                        {
                            var packet=new byte[65];await pair.Peer.ReadExactlyAsync(packet,peerStop.Token);packets.Add(packet);
                            if(mode=="blocked")await Task.Delay(Timeout.Infinite,peerStop.Token);
                            var ack=new byte[65];ack[4]=9;await pair.Peer.WriteAsync(ack,peerStop.Token);
                        }
                    }
                    catch(Exception error)when(error is OperationCanceledException or IOException or ObjectDisposedException){}
                });
                return transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,identity,()=>lease.IsCurrent,lease.Token,
                    ()=>{issued.TrySetResult();ready?.Set();});
            },(_,_)=>ec);
        }
        finally
        {
            peerStop?.Cancel();peer.GetAwaiter().GetResult();pair?.Dispose();peerStop?.Dispose();
        }
        if(mode=="wrong-owner-generation")data["ownerGeneration"]=Convert.ToInt64(data["ownerGeneration"])+1;
        if(mode=="missing-handback")data.Remove("handbackCompleted");
        data["leaseCurrentAfterConsumer"]=ownerCurrentAtDispose;
        data["ecDisposed"]=ec.Disposed;data["ecThreadCount"]=ec.Threads.Distinct().Count();
        data["packets"]=packets.Select(Convert.ToHexString).ToArray();
        data["ecWritesObserved"]=ec.Writes.Select(w=>new[]{(int)w.Register,(int)w.Value}).ToArray();
        data["pendingReadIssued"]=issued.Task.IsCompleted;
        data["pendingReadCancelled"]=transport?.LastReadCancelledWhileIncomplete??false;
        data["leaseRevoked"]=ownerRevokedAtDispose;
        data["nativeCancelIoOutcomes"]=transport?.CancelIoOutcomes.ToArray();
        data["shippingParserAndCatalogUsed"]=true;data["hardwareIo"]=false;data["productMainInvoked"]=false;
        var exit=Program.ExitCodeForNote(data.GetValueOrDefault("note") as string);
        var text=JsonSerializer.Serialize(new{ok=exit==0,result=data});
        var destination=Environment.GetEnvironmentVariable("YMCC_SJ_CHILD_RESULT");
        if(!string.IsNullOrWhiteSpace(destination))
        {
            destination=Path.GetFullPath(destination);
            var evidence=Path.GetFullPath(Environment.GetEnvironmentVariable("YMCC_SJ_COMPOSITION_EVIDENCE_ROOT")??throw new ArgumentException("Explicit evidence root required")).TrimEnd('\\');
            if(!destination.StartsWith(evidence+"\\",StringComparison.OrdinalIgnoreCase))throw new ArgumentException("Evidence outside the declared fixture root");
            File.WriteAllText(destination,text+"\n");
        }
        Console.WriteLine(text);return exit;
    }
    private sealed class MemoryEc(int failAt,Action onDispose) : IAyaneoCeLightingEc
    {
        internal readonly List<(byte Register,byte Value)> Writes=[];
        internal readonly List<int> Threads=[];internal bool Disposed;
        public bool TryWriteByte(byte register,byte value)
        {Threads.Add(Environment.CurrentManagedThreadId);Writes.Add((register,value));return Writes.Count!=failAt;}
        public void Dispose(){Threads.Add(Environment.CurrentManagedThreadId);Disposed=true;onDispose();}
    }
}
