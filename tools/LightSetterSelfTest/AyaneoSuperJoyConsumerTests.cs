using System.IO;
using System.Reflection;
using System.Windows.Media;

internal static class AyaneoSuperJoyConsumerTests
{
    public static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--ec-wrapper-only") { Console.WriteLine(EcWrapperChecks()); return; }
        if (args.Length != 1 || args[0] != "--handback-revocation-only") throw new ArgumentException("Explicit software probe required");
        var id = new SuperJoyIdentity("HandheldCompanion.Devices.AYANEO3", "memory-handback-revocation", 7);
        var request = AyaneoSuperJoyRequest.Create(id.ModelClass, "color", "solid", null, 255, 7, 101, 3, 2, 1, 33);
        var owner = true; var ec = new MemoryEc(); var hid = new MemoryHid(id);
        ec.AfterWrite = () => { if (ec.Writes.Last() == ((byte)0xBF, (byte)0)) owner = false; };
        var result = AyaneoSuperJoyConsumer.Apply(id, request, () => owner, default, () => hid, () => ec);
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { result, owner, result.Completed, result.HandbackCompleted }));
        if (result.Completed || !result.HandbackCompleted || !ec.Disposed || !hid.Disposed)
            throw new Exception("RETIRED_DURING_HANDBACK_MUST_NOT_REPORT_SUCCESS");
    }

    internal static int Run()
    {
        var checks = 0;
        void Check(bool ok, string label) { if (!ok) throw new Exception("SUPERJOY_COMBINED: " + label); checks++; }
        void Reject(Action action, string label)
        {
            try { action(); } catch (Exception error) when (error is ArgumentException or NotSupportedException) { checks++; return; }
            throw new Exception("SUPERJOY_COMBINED accepted " + label);
        }
        foreach (var model in new[] { "HandheldCompanion.Devices.AYANEO3", "HandheldCompanion.Devices.AYANEONEXT2" })
        foreach (var action in new[] { "color", "brightness", "status", "off" })
        foreach (var brightness in new[] { 0, 1, 33, 99, 100 })
        {
            var identity = new SuperJoyIdentity(model, "memory-combined-superjoy", 7);
            var request = AyaneoSuperJoyRequest.Create(model, action, "solid", "on", 7, 101, 254, 99, 1, 200, brightness);
            var ec = new MemoryEc(); var hid = new MemoryHid(identity);
            var thread = Environment.CurrentManagedThreadId;
            var result = AyaneoSuperJoyConsumer.Apply(identity, request, () => true, CancellationToken.None, () => hid, () => ec);
            Check(result.Completed && result.HandbackAttempted && result.HandbackCompleted, "both terminals and handback complete");
            Check(result.HidWrites == 3 && result.HidReads == 3 && hid.Writes.Count == 3, "fresh CEc override request count");
            Check(ec.Disposed && hid.Disposed && ec.Threads.All(t => t == thread), "EC acquisition/use/disposal stay on request thread");
            var expected = ExpectedEc(request);
            Check(ec.Writes.SequenceEqual(expected) && result.EcWrites == expected.Count, "AYA B,R,G floating scaling, model confirmation, status and release");
            Check(ec.Writes.All(w => w.Register is 0xBF or 0x6D or 0xB1 or 0xB2), "no fan, bypass charging, bank selection or unrelated registers");
            var expectedPackets = ExpectedHid(request);
            Check(hid.Writes.Zip(expectedPackets).All(p => p.First.SequenceEqual(p.Second)), "combined request exact independent pinned SuperJoy packets");
#if HC_F2_CEC_ORACLE
            var oracle = new FixedCeOracle(model);
            oracle.Apply(request);
            Check(ec.Writes.SequenceEqual(oracle.Writes.Select(w => ((byte)w.Address, w.Value))), "exact complete fixed CEc source method execution agrees");
            Check(hid.Writes.Count == oracle.Packets.Count && hid.Writes.Zip(oracle.Packets).All(p => p.First.SequenceEqual(p.Second)), "fixed CEc virtual overrides agree on complete HID sequence");
            Check(oracle.BaseOpenCalls == 0 && oracle.FanWrites == 0, "oracle did not enter unrelated Open/Close or fan lifecycle");
#endif
        }
        var id = new SuperJoyIdentity("HandheldCompanion.Devices.AYANEO3", "memory-combined-superjoy", 7);
        var normal = AyaneoSuperJoyRequest.Create(id.ModelClass, "color", "solid", null, 255, 7, 101, 3, 2, 1, 33);
        Reject(() => AyaneoSuperJoyRequest.Create("unknown", "off", null, null, null, null, null, null, null, null, null), "model identity");
        Reject(() => AyaneoSuperJoyRequest.Create(id.ModelClass, "color", "rainbow", null, 1, 2, 3, 4, 5, 6, 50), "unsupported effect");
        Reject(() => AyaneoSuperJoyRequest.Create(id.ModelClass, "preset", "solid", null, 1, 2, 3, 4, 5, 6, 50), "unsupported preset");
        Reject(() => AyaneoSuperJoyRequest.Create(id.ModelClass, "brightness", "solid", null, 1, 2, 3, null, null, null, 50), "missing second color context");
        Reject(() => AyaneoSuperJoyRequest.Create(id.ModelClass, "status", "solid", "on", 1, 2, 3, 4, 5, 6, null), "missing brightness context");
        var opens = 0;
        Reject(() => AyaneoSuperJoyConsumer.Apply(id with { Generation = 0 }, normal, () => true, default,
            () => { opens++; return new MemoryHid(id); }, () => { opens++; return new MemoryEc(); }), "bad owner identity");
        Reject(() => AyaneoSuperJoyConsumer.Apply(id, normal with { Brightness = 101 }, () => true, default,
            () => { opens++; return new MemoryHid(id); }, () => { opens++; return new MemoryEc(); }), "internal request tamper");
        Check(opens == 0, "request/identity refusal before either terminal factory");
        var pre = AyaneoSuperJoyConsumer.Apply(id, normal, () => false, default,
            () => { opens++; return new MemoryHid(id); }, () => { opens++; return new MemoryEc(); });
        Check(!pre.Completed && !pre.HandbackAttempted && opens == 0, "retired owner opens neither terminal");
        foreach (var model in new[] { id.ModelClass, "HandheldCompanion.Devices.AYANEONEXT2" })
        {
            var identity = id with { ModelClass = model }; var request = normal with { ModelClass = model };
            var length = ExpectedEc(request).Count;
            for (var failAt = 1; failAt <= length; failAt++)
            {
                var ec = new MemoryEc { FailAt = failAt }; var hid = new MemoryHid(identity);
                var result = AyaneoSuperJoyConsumer.Apply(identity, request, () => true, default, () => hid, () => ec);
                Check(!result.Completed && result.HandbackAttempted && ec.Disposed && hid.Disposed, "EC failure never reports success or leaks terminals");
                Check(ec.Writes.Last() == ((byte)0xBF, (byte)0) && ec.Writes.Count == (failAt == length ? length : failAt + 1), "failed/partial write followed only by one release");
                Check(result.HandbackCompleted == (failAt != length), "handback failure reported separately");
            }
            for (var retireAt = 1; retireAt <= length; retireAt++)
            {
                var owner = true; var ec = new MemoryEc(); var hid = new MemoryHid(identity);
                ec.AfterWrite = () => { if (ec.Writes.Count == retireAt) owner = false; };
                var result = AyaneoSuperJoyConsumer.Apply(identity, request, () => owner, default, () => hid, () => ec);
                Check(!result.Completed && result.HandbackCompleted && ec.Writes.Count == Math.Min(retireAt + 1, length) && ec.Writes.Last() == ((byte)0xBF, (byte)0), "mid-command revocation permits release only, never next normal write");
            }
        }
        using (var stop = new CancellationTokenSource())
        {
            var ec = new MemoryEc(); var hid = new MemoryHid(id);
            ec.AfterWrite = () => { if (ec.Writes.Last() == ((byte)0xBF, (byte)0x10)) stop.Cancel(); };
            var result = AyaneoSuperJoyConsumer.Apply(id, normal, () => true, stop.Token, () => hid, () => ec);
            Check(!result.Completed && result.HandbackCompleted && ec.Writes.Count == 6 && ec.Writes.Last() == ((byte)0xBF, (byte)0), "cancellation at 5ms wait cannot write post-command FE or later color");
        }
        foreach (var failedWrite in new[] { 1, 2, 3 })
        {
            var ec = new MemoryEc(); var hid = new MemoryHid(id) { FailAt = failedWrite };
            var result = AyaneoSuperJoyConsumer.Apply(id, normal, () => true, default, () => hid, () => ec);
            Check(!result.Completed && result.HandbackCompleted && hid.Writes.Count == failedWrite && ec.Writes.Last() == ((byte)0xBF, (byte)0), "HID failure cannot continue into later phase or report combined success");
        }
        var foreign = new MemoryHid(id with { Generation = 8 });
        Reject(() => AyaneoSuperJoyConsumer.Apply(id, normal, () => true, default, () => foreign,
            () => { opens++; return new MemoryEc(); }), "terminal identity changed");
        Check(foreign.Disposed && opens == 0, "foreign endpoint disposed before EC open");
        checks += EcWrapperChecks();
        return checks;
    }

    private static int EcWrapperChecks()
    {
        var checks = 0;
        void Check(bool ok, string label) { if (!ok) throw new Exception("CEC_WRAPPER: " + label); checks++; }
        AyaneoCeWindowsEcTransport Create(Mutex mutex, IDisposable controller, Func<byte,byte,bool> write) =>
            (AyaneoCeWindowsEcTransport)typeof(AyaneoCeWindowsEcTransport).GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single().Invoke([mutex,controller,write]);
        var name = @"Local\YeManCC-Cec-SoftwareFixture-" + Guid.NewGuid().ToString("N");
        using var mutex = new Mutex(true, name);
        using var observer = Mutex.OpenExisting(name);
        var disposed = 0; var calls = 0;
        var endpoint = Create(mutex, new Disposal(() => disposed++), (_,_) => { calls++; return false; });
        Check(!endpoint.TryWriteByte(0x6D,4) && calls == 1, "actual bool write failure preserved");
        foreach (var register in new byte[] { 0x1E, 0x44, 0xFF })
        {
            try { endpoint.TryWriteByte(register,1); throw new Exception("non lighting register accepted"); }
            catch (ArgumentException) { Check(calls == 1, "fan/bypass/bank write refused before delegate"); }
        }
        var threadRefused = Task.Run(() =>
        {
            try { endpoint.TryWriteByte(0xBF,0); return false; }
            catch (InvalidOperationException error) { return error.Message == "CEC_THREAD_AFFINITY"; }
        }).GetAwaiter().GetResult();
        Check(threadRefused && calls == 1, "wrong thread cannot write while mutex belongs to caller");
        endpoint.Dispose(); endpoint.Dispose();
        Check(disposed == 1, "controller disposal exactly once");
        try { endpoint.TryWriteByte(0xBF,0); throw new Exception("disposed writer accepted"); }
        catch (ObjectDisposedException) { Check(calls == 1, "disposed endpoint cannot write"); }
        Check(Task.Run(() => { using var next = Mutex.OpenExisting(name); var acquired=next.WaitOne(0); if(acquired)next.ReleaseMutex(); return acquired; }).GetAwaiter().GetResult(), "actual isolated OS mutex released for successor");
        var errorName = @"Local\YeManCC-Cec-DisposalFixture-" + Guid.NewGuid().ToString("N");
        using var errorMutex = new Mutex(true,errorName);
        using var errorObserver = Mutex.OpenExisting(errorName);
        var failing = Create(errorMutex,new Disposal(() => throw new IOException("expected-disposal")),(_,_) => true);
        try { failing.Dispose(); throw new Exception("disposal error hidden"); }
        catch (IOException) { checks++; }
        Check(Task.Run(() => { using var next=Mutex.OpenExisting(errorName); var acquired=next.WaitOne(0); if(acquired)next.ReleaseMutex();return acquired; }).GetAwaiter().GetResult(), "actual OS mutex released despite controller disposal exception");
        return checks;
    }
    private sealed class Disposal(Action action) : IDisposable { public void Dispose() => action(); }

    private static List<(byte Register, byte Value)> ExpectedEc(AyaneoSuperJoyRequest r)
    {
        List<(byte, byte)> writes = [(0xBF, 0xFE)];
        if (r.Action == "off") Command(2, 0xC0);
        Command(12, Scale(r.Left.B)); Command(13, Scale(r.Left.R)); Command(14, Scale(r.Left.G));
        if (r.Action != "off") Command(2, r.Enabled ? (byte)0x80 : (byte)0xC0);
        writes.Add((0xBF, 0)); return writes;
        byte Scale(byte v) => (byte)(int)(((((float)v / 255) * 192) / 100) * r.Brightness);
        void Command(byte command, byte value)
        {
            writes.AddRange(new (byte,byte)[] { (0x6D,4),(0xB1,command),(0xB2,value),(0xBF,0x10),(0xBF,0xFE) });
            if (r.ModelClass.EndsWith("AYANEONEXT2", StringComparison.Ordinal))
                writes.AddRange(new (byte,byte)[] { (0x6D,3),(0xB1,0),(0xB2,0),(0xBF,0x10),(0xBF,0xFE) });
        }
    }
    private static byte[] PinnedPacket(SuperJoyRgb left, SuperJoyRgb right, int brightness, bool enabled)
    {
        var build = typeof(HandheldCompanion.Devices.AYANEO.AYANEOSuperJoyRgb).GetMethod("BuildConfigPacket", BindingFlags.NonPublic | BindingFlags.Static)!;
        var payload = (byte[])build.Invoke(null, [Color.FromRgb(left.R,left.G,left.B), Color.FromRgb(right.R,right.G,right.B), brightness, enabled])!;
        return new byte[] { 0 }.Concat(payload).ToArray();
    }
    private static List<byte[]> ExpectedHid(AyaneoSuperJoyRequest r) => r.Action == "off"
        ? [PinnedPacket(default,default,0,false), PinnedPacket(default,default,0,false), PinnedPacket(default,default,0,false)]
        : [PinnedPacket(r.Left,default,r.Brightness,false), PinnedPacket(r.Left,r.Right,r.Brightness,false), PinnedPacket(r.Left,r.Right,r.Brightness,r.Enabled)];

    private sealed class MemoryEc : IAyaneoCeLightingEc
    {
        internal readonly List<(byte Register, byte Value)> Writes = [];
        internal readonly List<int> Threads = [];
        internal int FailAt; internal Action? AfterWrite; internal bool Disposed;
        public bool TryWriteByte(byte register, byte value)
        { Threads.Add(Environment.CurrentManagedThreadId); Writes.Add((register,value)); AfterWrite?.Invoke(); return Writes.Count != FailAt; }
        public void Dispose() { Threads.Add(Environment.CurrentManagedThreadId); Disposed = true; }
    }
    private sealed class MemoryHid(SuperJoyIdentity identity) : ISuperJoyLightingTransport
    {
        public SuperJoyIdentity Identity => identity;
        public bool IsConnected => !Disposed;
        internal bool Disposed; internal int FailAt; internal readonly List<byte[]> Writes = [];
        public ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report, CancellationToken stop)
        { stop.ThrowIfCancellationRequested(); Writes.Add(report.ToArray()); return ValueTask.FromResult(Writes.Count == FailAt ? 64 : 65); }
        public ValueTask<int> ReadAsync(Memory<byte> response, CancellationToken stop)
        { stop.ThrowIfCancellationRequested(); response.Span[3] = 9; return ValueTask.FromResult(64); }
        public void CancelPendingIo() { }
        public void Dispose() { Disposed = true; }
    }
#if HC_F2_CEC_ORACLE
    // Complete fixed upstream CEc body, with ONLY its namespace relocated at
    // explicit build input. Base/EC and virtual SuperJoy terminal are memory-only.
    private sealed class FixedCeOracle : HandheldCompanion.Devices.AYANEO.PinnedF2.AYANEODeviceCEc
    {
        internal readonly List<byte[]> Packets = [];
        internal FixedCeOracle(string model) { rgbConfirmation = model.EndsWith("AYANEONEXT2", StringComparison.Ordinal); }
        internal void Apply(AyaneoSuperJoyRequest r)
        {
            CEcControl_RgbHoldControl();
            if (r.Action == "off") SetLedStatus(false);
            SetLedBrightness(r.Brightness);
            SetLedColor(Color.FromRgb(r.Left.R,r.Left.G,r.Left.B), Color.FromRgb(r.Right.R,r.Right.G,r.Right.B), HandheldCompanion.Utils.DeviceUtils.LEDLevel.SolidColor, 100);
            if (r.Action != "off") SetLedStatus(r.Enabled);
            CEcControl_RgbReleaseControl();
        }
        private void Capture(bool? enabled = null)
        {
            var left = ledColorSticksLeft ?? Colors.Black; var right = ledColorStickRight ?? Colors.Black;
            Packets.Add(PinnedPacket(new(left.R,left.G,left.B), new(right.R,right.G,right.B), ledBrightness ?? 100, enabled ?? ledStatus ?? false));
        }
        protected override void CEcRgb_SetColorAll(LEDGroup group, Color color)
        { if (group is LEDGroup.StickLeft or LEDGroup.StickRight) Capture(); else base.CEcRgb_SetColorAll(group,color); }
        protected override void CEcRgb_GlobalOn(LEDGroup group, byte speed = 0)
        { if (group == LEDGroup.StickBoth) Capture(true); else base.CEcRgb_GlobalOn(group,speed); }
        protected override void CEcRgb_GlobalOff(LEDGroup group)
        { if (group == LEDGroup.StickBoth) Capture(false); else base.CEcRgb_GlobalOff(group); }
    }
#endif
}
