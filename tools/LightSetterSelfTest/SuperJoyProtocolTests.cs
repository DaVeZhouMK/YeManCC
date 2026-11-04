using YeManFanBackend.Capabilities;
using System.IO;
using System.Reflection;
using System.Windows.Media;
using HCOracle = HandheldCompanion.Devices.AYANEO.AYANEOSuperJoyRgb;

internal static class SuperJoyProtocolTests
{
    private static SuperJoyIdentity Identity(string model = "HandheldCompanion.Devices.AYANEO3", long generation = 7) =>
        new(model, "memory-superjoy-vendor-interface", generation);
    internal static int Run() => RunAsync().GetAwaiter().GetResult();
    private static async Task<int> RunAsync()
    {
        var checks = 0;
        void Check(bool ok, string id) { if (!ok) throw new Exception("SUPERJOY: " + id); checks++; }
        void Reject(Action call, string id)
        { try { call(); } catch (ArgumentException) { checks++; return; } throw new Exception("SUPERJOY accepted " + id); }
        var build = typeof(HCOracle).GetMethod("BuildConfigPacket", BindingFlags.NonPublic | BindingFlags.Static)!;
        var colors = new[] { new SuperJoyRgb(0,0,0), new SuperJoyRgb(255,255,255), new SuperJoyRgb(3,101,254), new SuperJoyRgb(254,7,1) };
        foreach (var left in colors) foreach (var right in colors)
        foreach (var level in new[] { int.MinValue, -1, 0, 1, 33, 99, 100, 101, int.MaxValue })
        foreach (var enabled in new[] { false, true })
        {
            var expected = (byte[])build.Invoke(null, [Color.FromRgb(left.R,left.G,left.B), Color.FromRgb(right.R,right.G,right.B), level, enabled])!;
            var actual = AyaneoSuperJoyLightingSession.BuildReport(left,right,level,enabled);
            Check(actual.Length==65 && actual[0]==0 && actual.AsSpan(1).SequenceEqual(expected), "exact fixed-source packet/color order/clamp/off/checksum");
        }
        var hid = new HidLibrary.HidDevice();
        Check(HCOracle.MatchesInterface(hid), "fixed-source interface oracle positive");
        hid.Capabilities.InputReportByteLength=64;
        Check(!HCOracle.MatchesInterface(hid), "fixed-source short interface rejected");
        Check(HcLightingCapabilityCatalog.Require("ymcc.device.AYANEO3",null,"color","solid").Capabilities.SourceAdapter==LightingSourceAdapter.SuperJoyCec &&
              HcLightingCapabilityCatalog.Require("ymcc.device.AYANEONEXT2",null,"color","solid").Capabilities.SourceAdapter==LightingSourceAdapter.SuperJoyCec,
              "sole catalog supplies explicit typed authority for both source models");
        Check(!HcLightingCapabilityCatalog.IsReviewedExtension("Unreviewed.AYANEO3"),"protocol class name cannot admit an unreviewed identity");
        foreach (var model in new[] { "HandheldCompanion.Devices.AYANEO3", "HandheldCompanion.Devices.AYANEONEXT2" })
        {
            try
            {
                typeof(Program).GetMethod("Run",BindingFlags.Static|BindingFlags.NonPublic)!.Invoke(null,
                    [new[] { "--expected-device-class",model,"--action","color","--level","solid","--r","1","--g","2","--b","3","--hc-assembly","forbidden-superjoy-legacy.dll" }]);
                throw new Exception("incomplete owner request accepted");
            }
            catch(TargetInvocationException error) when(error.InnerException is ArgumentException failure &&
                failure.Message=="SUPERJOY_EXPLICIT_COLORS_AND_BRIGHTNESS_REQUIRED")
            { Check(true,"actual admitted helper refuses incomplete request before legacy HC file/factory or device I/O"); }
        }
        var identity = Identity();
        foreach (var bad in new[] { identity with {ModelClass="Unreviewed.AYANEO3"}, identity with {DevicePath=""},
            identity with {Generation=0}, identity with {VendorId=0x1A86}, identity with {ProductId=0xFE00},
            identity with {UsagePage=1}, identity with {Usage=2}, identity with {InputReportLength=64}, identity with {OutputReportLength=64} })
            Reject(()=>new AyaneoSuperJoyLightingSession(new MemorySuperJoyTransport(bad),bad,()=>true),"wrong identity/endpoint/generation");
        Reject(()=>new AyaneoSuperJoyLightingSession(new MemorySuperJoyTransport(identity),identity with {Generation=8},()=>true),"transport differs from admitted identity");
        foreach (var model in new[] { "HandheldCompanion.Devices.AYANEO3", "HandheldCompanion.Devices.AYANEONEXT2" })
        {
            var t = new MemorySuperJoyTransport(Identity(model));
            using var session = new AyaneoSuperJoyLightingSession(t,t.Identity,()=>true);
            var result = await session.SetSticksAsync(colors[2],colors[3],33,true);
            Check(result.Acknowledged && result.Writes==1 && result.Reads==1 && t.Writes.Count==1, "actual writer reaches memory terminal for exact model");
            Check(result.Reason=="protocol-acknowledged-effect-unverified", "ack is not color readback or physical success");
            Check(t.Writes[0].SequenceEqual(AyaneoSuperJoyLightingSession.BuildReport(colors[2],colors[3],33,true)), "actual submitted packet matches request");
            session.Dispose(); session.Dispose();
            var late = await session.SetSticksAsync(colors[0],colors[0],0,false);
            Check(!late.Acknowledged && late.Writes==0 && t.DisposeCalls==1 && t.CancelCalls==1 && t.Writes.Count==1, "close terminal, idempotent and no late write");
        }
        var retry = new MemorySuperJoyTransport(identity);
        retry.Responses.Enqueue((64, 8)); retry.Responses.Enqueue((65, 9)); retry.Responses.Enqueue((64, 9));
        using (var s = new AyaneoSuperJoyLightingSession(retry,identity,()=>true))
        {
            var r = await s.SetSticksAsync(colors[2],colors[3],50,true);
            Check(r.Acknowledged && r.Writes==3 && r.Reads==3 && retry.Writes.Count==3 && retry.AllResponsesInitiallyZero, "only exact 64-byte subcommand ack accepted; three original attempts");
        }
        var failed = new MemorySuperJoyTransport(identity); failed.Responses.Enqueue((64,8)); failed.Responses.Enqueue((0,0)); failed.Responses.Enqueue((63,9));
        using (var s = new AyaneoSuperJoyLightingSession(failed,identity,()=>true))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true); Check(!r.Acknowledged && r.Writes==3 && r.Reads==3,"wrong/empty/short response does not extend attempt budget"); }
        var shortWrite = new MemorySuperJoyTransport(identity) { WriteLength=64 };
        using (var s = new AyaneoSuperJoyLightingSession(shortWrite,identity,()=>true))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true); Check(!r.Acknowledged && r.Writes==1 && r.Reads==0,"short write cannot be accepted or consume an unrelated response"); }
        var owner=true; var retiredBeforeWrite=new MemorySuperJoyTransport(identity);
        using (var s=new AyaneoSuperJoyLightingSession(retiredBeforeWrite,identity,()=>owner))
        { owner=false;var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(!r.Acknowledged&&r.Writes==0&&retiredBeforeWrite.DisposeCalls==1,"retired owner refuses initial write"); }
        owner=true;var retireAfterWrite=new MemorySuperJoyTransport(identity);retireAfterWrite.AfterWrite=()=>owner=false;
        using (var s=new AyaneoSuperJoyLightingSession(retireAfterWrite,identity,()=>owner))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(!r.Acknowledged&&r.Writes==1&&r.Reads==0&&retireAfterWrite.DisposeCalls==1,"retired owner refuses read/retry after first write"); }
        owner=true;var retireAfterRead=new MemorySuperJoyTransport(identity);retireAfterRead.AfterRead=()=>owner=false;
        using (var s=new AyaneoSuperJoyLightingSession(retireAfterRead,identity,()=>owner))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(!r.Acknowledged&&r.Writes==1&&r.Reads==1,"late ack cannot publish newer owner success"); }
        var rebound=new MemorySuperJoyTransport(identity);rebound.AfterWrite=()=>rebound.Identity=identity with{Generation=8};
        using (var s=new AyaneoSuperJoyLightingSession(rebound,identity,()=>true))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(!r.Acknowledged&&r.Reads==0,"endpoint generation change fails closed"); }
        var canceled=new MemorySuperJoyTransport(identity);using(var cts=new CancellationTokenSource())
        using(var s=new AyaneoSuperJoyLightingSession(canceled,identity,()=>true))
        { cts.Cancel();var r=await s.SetSticksAsync(colors[2],colors[3],100,true,cts.Token);Check(!r.Acknowledged&&r.Writes==0&&canceled.Writes.Count==0,"pre-cancelled operation never reaches terminal"); }
        var broken=new MemorySuperJoyTransport(identity){ThrowRead=true};
        using(var s=new AyaneoSuperJoyLightingSession(broken,identity,()=>true))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(!r.Acknowledged&&r.Reason=="transport-failed"&&broken.DisposeCalls==1,"I/O failure closes terminal without false success"); }
        var blocked=new MemorySuperJoyTransport(identity){BlockRead=true};
        using(var s=new AyaneoSuperJoyLightingSession(blocked,identity,()=>true))
        {
            var first=s.SetSticksAsync(colors[2],colors[3],100,true);
            await blocked.ReadEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));
            var queued=s.SetSticksAsync(colors[3],colors[2],100,true);
            s.Dispose();
            var a=await first.WaitAsync(TimeSpan.FromSeconds(1));var b=await queued.WaitAsync(TimeSpan.FromSeconds(1));
            Check(!a.Acknowledged&&!b.Acknowledged&&blocked.Writes.Count==1&&blocked.DisposeCalls==1,"stop cancels blocked reader and queued operation without extra writes");
        }
        var serial=new MemorySuperJoyTransport(identity){HoldFirstRead=true};
        using(var s=new AyaneoSuperJoyLightingSession(serial,identity,()=>true))
        {
            var first=s.SetSticksAsync(colors[2],colors[3],33,true);
            await serial.ReadEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));
            var second=s.SetSticksAsync(colors[3],colors[2],99,true);
            Check(serial.Writes.Count==1&&!second.IsCompleted,"second operation waits for first transaction");
            serial.ReleaseFirstRead.TrySetResult();
            var r=await Task.WhenAll(first,second).WaitAsync(TimeSpan.FromSeconds(1));
            Check(r.All(x=>x.Acknowledged)&&serial.Writes.Count==2&&serial.MaximumReaders==1&&
                serial.Writes[0].SequenceEqual(AyaneoSuperJoyLightingSession.BuildReport(colors[2],colors[3],33,true))&&
                serial.Writes[1].SequenceEqual(AyaneoSuperJoyLightingSession.BuildReport(colors[3],colors[2],99,true)),"serialized transactions retain own colors/brightness without competing reader");
        }
        var operationCanceled=new MemorySuperJoyTransport(identity){HoldFirstRead=true};
        using(var cts=new CancellationTokenSource())
        using(var s=new AyaneoSuperJoyLightingSession(operationCanceled,identity,()=>true))
        {
            var first=s.SetSticksAsync(colors[2],colors[3],33,true,cts.Token);
            await operationCanceled.ReadEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));
            var second=s.SetSticksAsync(colors[3],colors[2],99,true);
            cts.Cancel();
            var r=await Task.WhenAll(first,second).WaitAsync(TimeSpan.FromSeconds(1));
            Check(!r[0].Acknowledged&&r[1].Acknowledged&&operationCanceled.Writes.Count==2&&operationCanceled.MaximumReaders==1,"operation cancellation retires its own reader before next current operation");
        }
        var foreignCanceled=new MemorySuperJoyTransport(identity){ForeignCancellation=true};
        using(var s=new AyaneoSuperJoyLightingSession(foreignCanceled,identity,()=>true))
        {var r=await s.SetSticksAsync(colors[2],colors[3],33,true);Check(!r.Acknowledged&&r.Writes==1&&r.Reads==1,"foreign cancellation is not misreported as response deadline or retried");}
        // A timed out first response must not leave a competing reader before retry.
        var timed=new MemorySuperJoyTransport(identity){TimeoutFirstRead=true};
        using(var s=new AyaneoSuperJoyLightingSession(timed,identity,()=>true))
        { var r=await s.SetSticksAsync(colors[2],colors[3],100,true);Check(r.Acknowledged&&r.Writes==2&&r.Reads==2&&timed.MaximumReaders==1,"timed read canceled before bounded retry; no competing readers"); }
        return checks;
    }
    private sealed class MemorySuperJoyTransport(SuperJoyIdentity identity) : ISuperJoyLightingTransport
    {
        public SuperJoyIdentity Identity { get; set; } = identity;
        public bool IsConnected { get; set; } = true;
        public List<byte[]> Writes { get; } = [];
        public Queue<(int Length, byte Command)> Responses { get; } = new();
        public int WriteLength { get; set; } = 65;
        public int DisposeCalls { get; private set; }
        public int CancelCalls { get; private set; }
        public bool ThrowRead { get; set; }
        public bool BlockRead { get; set; }
        public bool TimeoutFirstRead { get; set; }
        public bool HoldFirstRead { get; set; }
        public bool ForeignCancellation { get; set; }
        public bool AllResponsesInitiallyZero { get; private set; } = true;
        public TaskCompletionSource ReleaseFirstRead { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public int MaximumReaders { get; private set; }
        private int readers,readCalls;
        public Action? AfterWrite { get; set; }
        public Action? AfterRead { get; set; }
        public TaskCompletionSource ReadEntered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report,CancellationToken token)
        { token.ThrowIfCancellationRequested();Writes.Add(report.ToArray());AfterWrite?.Invoke();return ValueTask.FromResult(WriteLength); }
        public async ValueTask<int> ReadAsync(Memory<byte> response,CancellationToken token)
        {
            token.ThrowIfCancellationRequested();readers++;readCalls++;MaximumReaders=Math.Max(MaximumReaders,readers);
            try
            {
                ReadEntered.TrySetResult();
                AllResponsesInitiallyZero &= response.Span.IndexOfAnyExcept((byte)0)<0;
                if(ForeignCancellation)throw new OperationCanceledException("injected unrelated cancellation");
                if(ThrowRead)throw new IOException("injected read failure");
                if(HoldFirstRead&&readCalls==1)await ReleaseFirstRead.Task.WaitAsync(token);
                if(BlockRead || TimeoutFirstRead&&readCalls==1)await Task.Delay(Timeout.Infinite,token);
                var reply=Responses.Count==0?(64,(byte)9):Responses.Dequeue();
                response.Span[3]=reply.Item2;AfterRead?.Invoke();return reply.Item1;
            }
            finally { readers--; }
        }
        public void CancelPendingIo()=>CancelCalls++;
        public void Dispose(){DisposeCalls++;IsConnected=false;}
    }
}
