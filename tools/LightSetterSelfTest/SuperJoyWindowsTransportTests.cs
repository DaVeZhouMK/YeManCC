using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// Actual asynchronous FileStream + Windows pipe handles, never a HID handle.
// The product Open/descriptor path is not called; only its owned I/O terminal
// is exercised at a controlled OS endpoint by the original LightSetter fixture.
internal static class SuperJoyWindowsTransportTests
{
    internal static int Run()=>RunAsync().GetAwaiter().GetResult();
    private static async Task<int> RunAsync()
    {
        var checks=0;
        void Check(bool ok,string name){if(!ok)throw new Exception("SUPERJOY_WINDOWS: "+name);Interlocked.Increment(ref checks);}
        var payload=Enumerable.Range(0,64).Select(i=>(byte)i).ToArray();var raw=new byte[65];payload.CopyTo(raw,1);var decoded=new byte[65];
        Check(SuperJoyWindowsHidTransport.DecodeResponse(raw,decoded)==64&&decoded.AsSpan(0,64).SequenceEqual(payload),"exact Windows ID0 to fixed hidapi payload conversion");
        foreach(var bad in new[]{new byte[64],new byte[66],new byte[65]})
        {
            if(bad.Length==65)bad[0]=1;
            try{SuperJoyWindowsHidTransport.DecodeResponse(bad,decoded);throw new Exception("bad frame accepted");}
            catch(InvalidDataException){Check(true,"short/long/numbered input rejected without guessed framing");}
        }
        try{SuperJoyWindowsHidTransport.DecodeResponse(raw,new byte[63]);throw new Exception("small output accepted");}
        catch(ArgumentException){Check(true,"caller cannot truncate the source payload");}
        var identity=new SuperJoyIdentity("HandheldCompanion.Devices.AYANEO3","memory-superjoy-os-pipe",11);
        await Roundtrip(identity,"current generation actual OS IO");
        using(var pair=await PipePair.Open())
        using(var ownerStop=new CancellationTokenSource())
        {
            var issued=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            using var transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,identity,()=>true,ownerStop.Token,()=>issued.TrySetResult());
            var pending=transport.ReadAsync(new byte[65],CancellationToken.None).AsTask();
            await issued.Task.WaitAsync(TimeSpan.FromSeconds(1));
            Check(!pending.IsCompleted&&transport.LastReadIssuedIncomplete&&!transport.LastReadTokenCancelledAtIssue,"owner stop target is an actual pending uncancelled OS read");
            ownerStop.Cancel();var cancelled=false;
            try{await pending.WaitAsync(TimeSpan.FromSeconds(1));}catch(OperationCanceledException){cancelled=true;}
            Check(cancelled&&transport.LastReadCancelledWhileIncomplete&&!transport.IsConnected,"owner revocation cancels the pending read and closes the old endpoint");
            Check(transport.CancelIoOutcomes.Any(code=>code is 0 or 1168),"native CancelIoEx outcome recorded, including already-cancelled ERROR_NOT_FOUND");
            try{await transport.WriteAsync(new byte[65],CancellationToken.None);throw new Exception("old endpoint reused");}
            catch(ObjectDisposedException){Check(true,"retired transport cannot write after owner stop");}
        }
        await Roundtrip(identity with{Generation=12},"independent successor endpoint after old owner stop");
        using(var pair=await PipePair.Open())
        {
            var issued=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            using var transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,identity,()=>true,readIssued:()=>issued.TrySetResult());
            var pending=transport.ReadAsync(new byte[65],CancellationToken.None).AsTask();
            await issued.Task.WaitAsync(TimeSpan.FromSeconds(1));transport.Dispose();
            var stopped=false;try{await pending.WaitAsync(TimeSpan.FromSeconds(1));}catch(Exception error)when(error is OperationCanceledException or ObjectDisposedException){stopped=true;}
            Check(stopped&&!transport.IsConnected,"explicit close cancels actual pending read without waiting for transaction lock");
        }
        using(var pair=await PipePair.Open())
        using(var transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,identity,()=>true))
        using(var session=new AyaneoSuperJoyLightingSession(transport,identity,()=>true))
        {
            var drain=Task.Run(async()=>{var request=new byte[65];for(var i=0;i<3;i++)await pair.Peer.ReadExactlyAsync(request);});
            var result=await session.SetSticksAsync(new(1,2,3),new(4,5,6),50,true).WaitAsync(TimeSpan.FromSeconds(2));
            await drain.WaitAsync(TimeSpan.FromSeconds(1));
            Check(!result.Acknowledged&&result.Writes==3&&result.Reads==3&&transport.IsConnected,"three actual 300ms read deadlines retain attempt budget and no success");
            Check(transport.LastReadCancelledWhileIncomplete&&transport.CancelIoOutcomes.Count>=3,"each timed-out OS read settles through cancellation before retry");
        }
        return checks;

        async Task Roundtrip(SuperJoyIdentity id,string label)
        {
            using var pair=await PipePair.Open();
            using var transport=SuperJoyWindowsHidTransport.ForTest(pair.Handle,id,()=>true);
            using var session=new AyaneoSuperJoyLightingSession(transport,id,()=>true);
            var expected=AyaneoSuperJoyLightingSession.BuildReport(new(3,7,255),new(251,1,88),33,true);
            var peer=Task.Run(async()=>
            {
                var bytes=new byte[65];await pair.Peer.ReadExactlyAsync(bytes);
                Check(bytes.SequenceEqual(expected),label+": actual write bytes");
                var response=new byte[65];response[4]=9;await pair.Peer.WriteAsync(response);
            });
            var result=await session.SetSticksAsync(new(3,7,255),new(251,1,88),33,true).WaitAsync(TimeSpan.FromSeconds(1));
            await peer.WaitAsync(TimeSpan.FromSeconds(1));
            Check(result.Acknowledged&&result.Writes==1&&result.Reads==1,label+": normalized real OS response reaches fixed source check");
        }
    }
    internal sealed class PipePair(SafeFileHandle handle,NamedPipeClientStream peer):IDisposable
    {
        internal SafeFileHandle Handle{get;}=handle;
        internal NamedPipeClientStream Peer{get;}=peer;
        internal static async Task<PipePair> Open()
        {
            var name="YMCC-SuperJoy-Software-"+Guid.NewGuid().ToString("N");
            var handle=PipeNative.CreateNamedPipe(@"\\.\pipe\"+name,0x40000003u,0,1,4096,4096,0,IntPtr.Zero);
            if(handle.IsInvalid)throw new IOException("create memory pipe "+Marshal.GetLastWin32Error());
            var peer=new NamedPipeClientStream(".",name,PipeDirection.InOut,PipeOptions.Asynchronous);
            try
            {
                await peer.ConnectAsync(1000);
                if(!PipeNative.ConnectNamedPipe(handle,IntPtr.Zero)&&Marshal.GetLastWin32Error()!=535)
                    throw new IOException("connect memory pipe "+Marshal.GetLastWin32Error());
                return new(handle,peer);
            }
            catch{peer.Dispose();handle.Dispose();throw;}
        }
        public void Dispose(){Peer.Dispose();Handle.Dispose();}
    }
}
