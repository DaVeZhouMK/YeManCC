using System.Buffers.Binary;
using System.ComponentModel;
using System.Text;
using YeManBatteryHost;

// Bounded child of the parent's BatteryHostTests group. Every external endpoint
// is supplied explicitly; neither the default driver path nor WMI is invoked.
internal static class SuperXChargeTransportFixture
{
    private const uint LoadIoctl = unchecked((uint)-1582161788);
    private const uint ExecuteIoctl = unchecked((uint)-1582161660);
    private const string MemoryPath = "memory://hc068/pawnio";
    private static readonly byte[] Module = [0x48, 0x43, 0x30, 0x36, 0x38];

    internal static int Run()
    {
        var checks = new Checks();
        AdmissionAndPackets(checks);
        EveryIoctlFailure(checks);
        InitializationFailures(checks);
        TransportCleanupRetry(checks);
        ProviderAdmission(checks);
        ProviderCleanupRetry(checks);
        ControllerReadback(checks);
        return checks.Count;
    }

    private sealed class Checks
    {
        internal int Count { get; private set; }
        internal void Check(bool condition, string reason)
        {
            if (!condition) throw new Exception("SuperX charge transport: " + reason);
            Count++;
        }
        internal Exception Reject(Action action, string reason)
        {
            Exception? observed = null;
            try { action(); } catch (Exception error) { observed = error; }
            Check(observed is IOException or AggregateException, reason + "; expected an IO failure, got " + observed?.GetType().Name);
            return observed!;
        }
    }

    private sealed record Frame(uint Code, byte[] Input);
    private enum ReplyFailure { None, Nack, ReturnedLength, Exception }
    private enum InitFailure { None, BusFactory, BusEnter, SessionFactory, Image, EmptyImage, Path, KernelFactory, Open }
    private sealed class Firmware
    {
        internal byte Value { get; set; } = 80;
        internal int Effects { get; set; }
    }

    private static byte[] Packet(string command, params long[] arguments)
    {
        var packet = new byte[32 + arguments.Length * 8];
        Encoding.ASCII.GetBytes(command).CopyTo(packet, 0);
        for (int i = 0; i < arguments.Length; i++)
            BinaryPrimitives.WriteInt64LittleEndian(packet.AsSpan(32 + i * 8, 8), arguments[i]);
        return packet;
    }

    private static List<Frame> Expected(byte value)
    {
        // Independent wire expectation: one module load, slot 1, then the exact
        // IT87 at4E unlock and EC16 04A3 value sequence. There is no exit write.
        (ushort Port, byte Value)[] writes =
        [
            (0x4e, 0x87), (0x4e, 0x01), (0x4e, 0x55), (0x4e, 0xaa),
            (0x4e, 0x2e), (0x4f, 0x11), (0x4e, 0x2f), (0x4f, 0x04),
            (0x4e, 0x2e), (0x4f, 0x10), (0x4e, 0x2f), (0x4f, 0xa3),
            (0x4e, 0x2e), (0x4f, 0x12), (0x4e, 0x2f), (0x4f, value)
        ];
        var expected = new List<Frame>
        {
            new(LoadIoctl, Module.ToArray()),
            new(ExecuteIoctl, Packet("ioctl_select_slot", 1))
        };
        foreach (var write in writes)
            expected.Add(new(ExecuteIoctl, Packet("ioctl_pio_outb", write.Port, write.Value)));
        return expected;
    }

    private static void Prefix(Checks checks, Transport transport, int count, byte value, string reason)
    {
        var expected = Expected(value);
        checks.Check(transport.Kernel.Frames.Count == count, reason + ": IOCTL count / immediate stop");
        for (int i = 0; i < count; i++)
        {
            var actual = transport.Kernel.Frames[i];
            checks.Check(actual.Code == expected[i].Code, reason + ": IOCTL code at " + i);
            checks.Check(actual.Input.AsSpan().SequenceEqual(expected[i].Input), reason + ": packet bytes, length, padding and little-endian arguments at " + i);
        }
    }

    private sealed class MemoryKernel(Firmware firmware) : ISuperXKernelChannel
    {
        internal List<Frame> Frames { get; } = [];
        internal List<string> Paths { get; } = [];
        internal bool FailOpen { get; set; }
        internal int FaultAt { get; set; } = -1;
        internal ReplyFailure Failure { get; set; }
        internal bool IgnoreEffect { get; set; }
        internal int DisposeFailures { get; set; }
        internal int DisposeCalls { get; private set; }
        internal int DisposeSuccesses { get; private set; }
        public void Open(string path)
        {
            Paths.Add(path);
            if (FailOpen) throw new IOException("injected memory open failure");
        }
        public SuperXIoResult Ioctl(uint code, byte[] input)
        {
            int position = Frames.Count;
            Frames.Add(new(code, input.ToArray()));
            // A kernel can apply the last byte before returning failure. This
            // deliberately models that uncertainty instead of assuming rollback.
            if (position == 17 && !IgnoreEffect && code == ExecuteIoctl && input.Length == 48 &&
                BinaryPrimitives.ReadInt64LittleEndian(input.AsSpan(32, 8)) == 0x4f)
            {
                firmware.Value = checked((byte)BinaryPrimitives.ReadInt64LittleEndian(input.AsSpan(40, 8)));
                firmware.Effects++;
            }
            if (position == FaultAt)
            {
                if (Failure == ReplyFailure.Nack) return new(false, 0, 1234);
                if (Failure == ReplyFailure.ReturnedLength) return new(true, 1, 0);
                if (Failure == ReplyFailure.Exception) throw new IOException("injected memory IOCTL failure");
            }
            return new(true, 0, 0);
        }
        public void Dispose()
        {
            DisposeCalls++;
            if (DisposeFailures > 0) { DisposeFailures--; throw new IOException("injected memory handle close failure"); }
            DisposeSuccesses++;
        }
    }

    private sealed class MemoryBus : ISuperXBusLease
    {
        internal bool FailEnter { get; set; }
        internal int DisposeFailures { get; set; }
        internal int EnterCalls { get; private set; }
        internal int DisposeCalls { get; private set; }
        internal int DisposeSuccesses { get; private set; }
        public void Enter()
        {
            EnterCalls++;
            if (FailEnter) throw new IOException("injected memory bus entry failure");
        }
        public void Dispose()
        {
            DisposeCalls++;
            if (DisposeFailures > 0) { DisposeFailures--; throw new IOException("injected memory bus release failure"); }
            DisposeSuccesses++;
        }
    }

    private sealed class Transport
    {
        internal Firmware Firmware { get; }
        internal MemoryKernel Kernel { get; }
        internal MemoryBus Bus { get; } = new();
        internal SuperXCheckedLpcSession Session { get; }
        internal SuperXCheckedEcWriter Writer { get; }
        internal int BusCreations { get; private set; }
        internal int SessionCreations { get; private set; }
        internal int ImageReads { get; private set; }
        internal int PathReads { get; private set; }
        internal int KernelCreations { get; private set; }
        internal (int, int, int, int, int, int, int, int) Activity =>
            (BusCreations, Bus.EnterCalls, SessionCreations, ImageReads, PathReads, KernelCreations, Kernel.Paths.Count, Kernel.Frames.Count);

        internal Transport(InitFailure failure = InitFailure.None, Firmware? firmware = null)
        {
            Firmware = firmware ?? new Firmware();
            Kernel = new MemoryKernel(Firmware) { FailOpen = failure == InitFailure.Open };
            Bus.FailEnter = failure == InitFailure.BusEnter;
            Session = new SuperXCheckedLpcSession(
                createKernel: () =>
                {
                    KernelCreations++;
                    if (failure == InitFailure.KernelFactory) throw new IOException("injected memory kernel factory failure");
                    return Kernel;
                },
                moduleImage: () =>
                {
                    ImageReads++;
                    if (failure == InitFailure.Image) throw new IOException("injected memory module failure");
                    return failure == InitFailure.EmptyImage ? Array.Empty<byte>() : Module.ToArray();
                },
                devicePath: () =>
                {
                    PathReads++;
                    if (failure == InitFailure.Path) throw new IOException("injected memory path failure");
                    return MemoryPath;
                });
            Writer = new SuperXCheckedEcWriter(
                createSession: () =>
                {
                    SessionCreations++;
                    if (failure == InitFailure.SessionFactory) throw new IOException("injected memory session factory failure");
                    return Session;
                },
                createBus: () =>
                {
                    BusCreations++;
                    if (failure == InitFailure.BusFactory) throw new IOException("injected memory bus factory failure");
                    return Bus;
                });
        }
    }

    private static void AdmissionAndPackets(Checks checks)
    {
        checks.Check(SuperXCheckedLpcSession.LoadIoctl == LoadIoctl && SuperXCheckedLpcSession.ExecuteIoctl == ExecuteIoctl,
            "fixed IOCTL numbers changed");
        var dormant = new Transport();
        checks.Check(dormant.Activity == default, "constructors acquired an external endpoint");
        dormant.Writer.Dispose(); dormant.Writer.Dispose();
        checks.Reject(() => dormant.Writer.WriteLimit(85), "disposed dormant writer restarted");
        dormant.Session.Dispose();
        checks.Reject(() => dormant.Session.SelectSlot(1), "disposed dormant session restarted");
        checks.Check(dormant.Activity == default && dormant.Kernel.DisposeCalls == 0 && dormant.Bus.DisposeCalls == 0,
            "dormant stop acquired or released an unacquired resource");

        var invalid = new Transport();
        foreach (byte value in new byte[] { 0, 74, 101, 255 })
            checks.Reject(() => invalid.Writer.WriteLimit(value), "out-of-range raw limit admitted");
        checks.Check(invalid.Activity == default, "invalid target acquired bus or session");
        invalid.Writer.WriteLimit(75);
        Prefix(checks, invalid, 18, 75, "valid write after rejected inputs");
        invalid.Writer.Dispose();

        var order = new Transport();
        checks.Reject(() => order.Session.WritePort(0x4e, 0x87), "port write admitted before slot selection");
        checks.Reject(() => order.Session.SelectSlot(0), "slot zero admitted");
        checks.Reject(() => order.Session.SelectSlot(2), "slot two admitted");
        checks.Reject(() => order.Session.WritePort(0x4d, 0x87), "unreviewed port admitted before selection");
        checks.Check(order.Activity == default, "invalid slot/port/order initialized the kernel");
        order.Session.SelectSlot(1);
        var selected = order.Activity;
        checks.Reject(() => order.Session.SelectSlot(1), "slot selected twice");
        checks.Reject(() => order.Session.WritePort(0x4d, 0), "unreviewed port admitted after selection");
        checks.Check(order.Activity == selected, "invalid call after selection reached IOCTL");
        order.Session.Dispose(); order.Session.Dispose();
        checks.Reject(() => order.Session.WritePort(0x4e, 0x87), "stopped selected session reached IOCTL");
        checks.Check(order.Activity == selected && order.Kernel.DisposeSuccesses == 1, "selected session stop replayed or double closed");
        order.Writer.Dispose();

        foreach (byte target in new byte[] { 75, 85, 100 })
        {
            var transport = new Transport();
            transport.Writer.WriteLimit(target);
            Prefix(checks, transport, 18, target, "successful EC04A3 write " + target);
            checks.Check(transport.Kernel.Paths.SequenceEqual(new[] { MemoryPath }) && transport.ImageReads == 1 &&
                transport.PathReads == 1 && transport.KernelCreations == 1, "successful write reopened/reloaded or resolved a default path");
            checks.Check(transport.Bus.EnterCalls == 1 && transport.Bus.DisposeSuccesses == 1 &&
                transport.Kernel.DisposeCalls == 0, "bus/handle ownership ended at the wrong boundary");
            checks.Check(transport.Firmware.Value == target && transport.Firmware.Effects == 1, "wrong final EC value");
            var activity = transport.Activity;
            checks.Reject(() => transport.Writer.WriteLimit(target), "successful one-shot writer replayed");
            checks.Check(transport.Activity == activity, "replay reacquired transport");
            transport.Writer.Dispose(); transport.Writer.Dispose();
            checks.Check(transport.Kernel.DisposeCalls == 1 && transport.Bus.DisposeCalls == 1, "successful transport disposed twice");
        }
    }

    private static void EveryIoctlFailure(Checks checks)
    {
        foreach (var failure in new[] { ReplyFailure.Nack, ReplyFailure.ReturnedLength, ReplyFailure.Exception })
        for (int position = 0; position < 18; position++)
        {
            string reason = failure + " at IOCTL " + position;
            var transport = new Transport();
            transport.Kernel.FaultAt = position; transport.Kernel.Failure = failure;
            var error = checks.Reject(() => transport.Writer.WriteLimit(85), reason);
            if (failure == ReplyFailure.Nack)
                checks.Check(error.InnerException is Win32Exception native && native.NativeErrorCode == 1234, reason + ": lost native error");
            Prefix(checks, transport, position + 1, 85, reason);
            checks.Check(transport.Firmware.Value == (position == 17 ? 85 : 80), reason + ": final effect uncertainty was masked");
            checks.Check(transport.Bus.DisposeCalls == 1 && transport.Kernel.DisposeCalls == 0, reason + ": lost owned resources");
            var activity = transport.Activity;
            checks.Reject(() => transport.Writer.WriteLimit(85), reason + ": writer replay admitted");
            if (position <= 1) checks.Reject(() => transport.Session.SelectSlot(1), reason + ": failed initialization/selection retried");
            else checks.Reject(() => transport.Session.WritePort(0x4e, 0x87), reason + ": faulted session accepted another call");
            checks.Check(transport.Activity == activity, reason + ": retry reopened/reloaded or continued packet stream");
            transport.Writer.Dispose(); transport.Writer.Dispose();
            checks.Check(transport.Kernel.DisposeCalls == 1 && transport.Kernel.DisposeSuccesses == 1 &&
                transport.Bus.DisposeCalls == 1, reason + ": cleanup replay or lost kernel owner");
        }
    }

    private static void InitializationFailures(Checks checks)
    {
        foreach (var failure in new[] { InitFailure.BusFactory, InitFailure.BusEnter, InitFailure.SessionFactory,
            InitFailure.Image, InitFailure.EmptyImage, InitFailure.Path, InitFailure.KernelFactory, InitFailure.Open })
        {
            var transport = new Transport(failure);
            checks.Reject(() => transport.Writer.WriteLimit(85), "initialization failure " + failure);
            checks.Check(transport.Kernel.Frames.Count == 0, failure + ": IOCTL after failed initialization");
            checks.Check(transport.SessionCreations == (failure is InitFailure.BusFactory or InitFailure.BusEnter ? 0 : 1),
                failure + ": session created before bus admission");
            checks.Check(transport.Kernel.Paths.Count == (failure == InitFailure.Open ? 1 : 0), failure + ": open occurred past failed stage");
            checks.Check(transport.Bus.DisposeCalls == (failure == InitFailure.BusFactory ? 0 : 1), failure + ": owned bus not returned");
            var activity = transport.Activity;
            checks.Reject(() => transport.Writer.WriteLimit(85), failure + ": initialization replay admitted");
            if (failure is InitFailure.Image or InitFailure.EmptyImage or InitFailure.Path or InitFailure.KernelFactory or InitFailure.Open)
                checks.Reject(() => transport.Session.SelectSlot(1), failure + ": session reopened after initialization failure");
            checks.Check(transport.Activity == activity, failure + ": initialization repeated");
            transport.Writer.Dispose(); transport.Writer.Dispose();
            checks.Check(transport.Kernel.DisposeCalls == (failure == InitFailure.Open ? 1 : 0), failure + ": failed open ownership lost or unacquired kernel closed");
        }
    }

    private static void TransportCleanupRetry(Checks checks)
    {
        var handle = new Transport(); handle.Writer.WriteLimit(85);
        handle.Kernel.DisposeFailures = 1;
        checks.Reject(handle.Writer.Dispose, "kernel close failure hidden");
        var activity = handle.Activity;
        checks.Reject(() => handle.Writer.WriteLimit(90), "writer with kernel cleanup debt restarted");
        handle.Writer.Dispose(); handle.Writer.Dispose();
        checks.Check(handle.Activity == activity && handle.Kernel.DisposeCalls == 2 && handle.Kernel.DisposeSuccesses == 1 &&
            handle.Bus.DisposeCalls == 1, "kernel cleanup did not retry the exact retained object once");

        var bus = new Transport(); bus.Bus.DisposeFailures = 1;
        checks.Reject(() => bus.Writer.WriteLimit(85), "bus release failure converted to success");
        Prefix(checks, bus, 18, 85, "bus release failure after write");
        activity = bus.Activity;
        checks.Reject(() => bus.Writer.WriteLimit(85), "bus cleanup failure replayed write");
        bus.Writer.Dispose(); bus.Writer.Dispose();
        checks.Check(bus.Activity == activity && bus.Bus.DisposeCalls == 2 && bus.Bus.DisposeSuccesses == 1 &&
            bus.Kernel.DisposeCalls == 1, "bus cleanup did not retain and retry its original owner");
    }

    private sealed class MemoryReader(Firmware firmware) : ISuperXChargeReader
    {
        internal bool FailRead { get; set; }
        internal int Reads { get; private set; }
        internal int DisposeFailures { get; set; }
        internal int DisposeCalls { get; private set; }
        internal int DisposeSuccesses { get; private set; }
        public byte ReadLimit()
        {
            Reads++;
            if (FailRead) throw new IOException("injected memory charge read failure");
            return firmware.Value;
        }
        public void Dispose()
        {
            DisposeCalls++;
            if (DisposeFailures > 0) { DisposeFailures--; throw new IOException("injected memory reader close failure"); }
            DisposeSuccesses++;
        }
    }

    private sealed class MemoryWriter(Firmware firmware) : ISuperXChargeWriter
    {
        internal bool FailAfterEffect { get; set; }
        internal int Writes { get; private set; }
        internal int DisposeFailures { get; set; }
        internal int DisposeCalls { get; private set; }
        internal int DisposeSuccesses { get; private set; }
        public void WriteLimit(byte value)
        {
            Writes++; firmware.Value = value; firmware.Effects++;
            if (FailAfterEffect) throw new IOException("injected memory setter acknowledgement failure");
        }
        public void Dispose()
        {
            DisposeCalls++;
            if (DisposeFailures > 0) { DisposeFailures--; throw new IOException("injected memory writer close failure"); }
            DisposeSuccesses++;
        }
    }

    private static void ProviderAdmission(Checks checks)
    {
        var state = new Firmware(); var reader = new MemoryReader(state); var writer = new MemoryWriter(state);
        using (var provider = new SuperXChargeProvider(reader, writer))
        {
            checks.Check(provider.CanReadLimit && reader.Reads == 0 && writer.Writes == 0, "provider construction performed IO");
            foreach (int target in new[] { -1, 74, 76, 99, 101 })
                checks.Reject(() => provider.Write(target, new(80, 80)), "invalid provider target " + target);
            foreach (int? raw in new int?[] { null, 74, 101 })
                checks.Reject(() => provider.Write(85, new(80, raw)), "invalid before-state admitted");
            checks.Check(reader.Reads == 0 && writer.Writes == 0, "invalid provider input reached external pre-read/setter");
            foreach (byte raw in new byte[] { 0, 74, 101, 255 })
            {
                state.Value = raw;
                checks.Reject(() => provider.Read(), "provider accepted unverified raw limit");
            }
            state.Value = 80; var before = provider.Read(); state.Value = 85;
            checks.Reject(() => provider.Write(90, before), "stale before-state admitted");
            checks.Check(writer.Writes == 0, "stale before-state reached setter");
            before = provider.Read(); reader.FailRead = true;
            checks.Reject(() => provider.Write(90, before), "failed fresh UMA pre-read admitted setter");
            checks.Check(writer.Writes == 0, "failed pre-read reached setter");
            reader.FailRead = false;
            provider.Write(90, before);
            checks.Check(writer.Writes == 1 && provider.Read() == new Reading(90, 90), "valid write/readback after rejected preflight failed");
            int reads = reader.Reads;
            checks.Reject(() => provider.Write(95, new(90, 90)), "provider one-shot write replayed");
            checks.Check(writer.Writes == 1 && reader.Reads == reads, "consumed provider repeated pre-read/setter");
        }

        foreach (int target in new[] { 75, 80, 85, 90, 95, 100 })
        {
            var firmware = new Firmware(); var input = new MemoryReader(firmware); var output = new MemoryWriter(firmware);
            using var provider = new SuperXChargeProvider(input, output);
            var before = provider.Read(); provider.Write(target, before);
            checks.Check(input.Reads == 2 && output.Writes == 1, "target " + target + ": missing independent fresh pre-read");
            checks.Check(provider.Read() == new Reading(target, target), "target " + target + ": checked byte readback mismatch");
        }

        var partial = new Firmware(); var partialReader = new MemoryReader(partial);
        var partialWriter = new MemoryWriter(partial) { FailAfterEffect = true };
        using var uncertain = new SuperXChargeProvider(partialReader, partialWriter);
        var original = uncertain.Read();
        checks.Reject(() => uncertain.Write(85, original), "lost setter acknowledgement converted to success");
        checks.Check(uncertain.Read().Raw == 85 && partialWriter.Writes == 1, "possible setter effect was assumed rolled back");
        int priorReads = partialReader.Reads;
        checks.Reject(() => uncertain.Write(85, new(85, 85)), "uncertain provider write replayed");
        checks.Check(partialWriter.Writes == 1 && partialReader.Reads == priorReads, "uncertain provider reacquired writer");
    }

    private static void ProviderCleanupRetry(Checks checks)
    {
        foreach (var failures in new[] { (Writer: 1, Reader: 0), (Writer: 0, Reader: 1), (Writer: 1, Reader: 1), (Writer: 2, Reader: 1) })
        {
            var state = new Firmware();
            var writer = new MemoryWriter(state) { DisposeFailures = failures.Writer };
            var reader = new MemoryReader(state) { DisposeFailures = failures.Reader };
            var provider = new SuperXChargeProvider(reader, writer);
            var error = checks.Reject(provider.Dispose, "provider cleanup failure hidden");
            checks.Check(writer.DisposeCalls == 1 && reader.DisposeCalls == 1, "one cleanup failure skipped the other resource");
            if (failures.Writer > 0 && failures.Reader > 0)
                checks.Check(error is AggregateException both && both.InnerExceptions.Count == 2, "combined cleanup errors not retained");
            checks.Reject(() => provider.Read(), "provider read restarted after failed stop");
            checks.Reject(() => provider.Write(85, new(80, 80)), "provider write restarted after failed stop");
            if (failures.Writer == 2) checks.Reject(provider.Dispose, "second retained cleanup failure hidden");
            provider.Dispose(); provider.Dispose();
            checks.Check(writer.DisposeCalls == failures.Writer + 1 && reader.DisposeCalls == failures.Reader + 1 &&
                writer.DisposeSuccesses == 1 && reader.DisposeSuccesses == 1, "successful resource cleanup was replayed or failed owner forgotten");
            checks.Check(reader.Reads == 0 && writer.Writes == 0, "cleanup retry performed business IO");
        }
    }

    private sealed class MemoryJournal() : Store("unused-hc068-memory-store")
    {
        private readonly Dictionary<string, object> values = [];
        public override T? Load<T>(string name) where T : default => values.TryGetValue(name, out var value) ? (T)value : default;
        public override void Save<T>(string name, T value) => values[name] = value!;
        public override void Clear(string name) => values.Remove(name);
    }

    private static void ControllerReadback(Checks checks)
    {
        // Only result verification/journaling here. Stop/power/lifecycle tests
        // belong to the parent's fixture. Policy/catalog integration is parent-owned.
        var identity = new Identity("ONE-NETBOOK", "ONEXPLAYER SUPER X", "ONEXPLAYER SUPER X", "AMD Ryzen AI Max+ 395", "2.10", 2, 80);
        checks.Check(Policy.Resolve(identity)?.RouteId == "oxp-superx-dual-ec", "parent's Super X route/catalog integration missing");
        foreach (string scenario in new[] { "success", "mismatch", "Nack", "ReturnedLength", "Exception" })
        {
            var state = new Firmware(); var store = new MemoryJournal(); var transports = new List<Transport>();
            IChargeProvider Open(Route route)
            {
                checks.Check(route.RouteId == "oxp-superx-dual-ec", "Controller dispatched another device route");
                var transport = new Transport(firmware: state);
                transport.Kernel.IgnoreEffect = scenario == "mismatch";
                if (Enum.TryParse<ReplyFailure>(scenario, out var failure))
                {
                    transport.Kernel.FaultAt = 17; transport.Kernel.Failure = failure;
                }
                transports.Add(transport);
                return new SuperXChargeProvider(new MemoryReader(state), transport.Writer);
            }
            Controller NewController() => new(identity, store, Open, () => (true, true, 75));
            int Calls() => transports.Sum(transport => transport.Kernel.Frames.Count);
            var controller = NewController(); var before = controller.Read();
            checks.Check(before.Ok && before.Supported && before.CurrentLimit == 80 && Calls() == 0, scenario + ": preflight not memory-only");
            var result = controller.Apply(before.Fence, 85);
            checks.Check(Calls() == 18, scenario + ": request replayed or did not reach actual checked transport");
            var written = transports.Single(transport => transport.Kernel.Frames.Count != 0);
            Prefix(checks, written, 18, 85, "Controller " + scenario);
            if (scenario == "success")
            {
                checks.Check(result.Ok && result.CurrentLimit == 85 && result.Verification == "readback", "setter completion did not receive independent verified readback");
                checks.Check(store.Load<Recovery>(Controller.RecoveryFile) is null && store.Load<Preference>(Controller.SettingsFile)?.Target == 85,
                    "verified result did not clear journal/save preference");
            }
            else
            {
                int actual = scenario == "mismatch" ? 80 : 85;
                checks.Check(!result.Ok && !result.Supported && result.CurrentLimit == actual && result.Verification == "unconfirmed",
                    scenario + ": uncertain result reported success/rollback or lost actual readback");
                var journal = store.Load<Recovery>(Controller.RecoveryFile);
                checks.Check(journal is { OriginalRaw: 80, OwnedRaw: null, Software: false } && journal.IdentityKey == identity.Key &&
                    journal.RouteId == "oxp-superx-dual-ec" && store.Load<Preference>(Controller.SettingsFile) is null,
                    scenario + ": original owner uncertainty was cleared or persisted as success");
                var refresh = controller.Read();
                checks.Check(refresh.Verification == "unconfirmed" && !refresh.Supported && Calls() == 18, scenario + ": refresh replayed/acknowledged an uncertain write");
                var repeated = controller.Apply(refresh.Fence, 85);
                checks.Check(!repeated.Ok && Calls() == 18, scenario + ": unresolved journal admitted another write");
                var restarted = NewController().Read();
                checks.Check(!restarted.Supported && restarted.Verification == "unconfirmed" && restarted.CurrentLimit == actual && Calls() == 18 &&
                    store.Load<Recovery>(Controller.RecoveryFile) == journal, scenario + ": reconstructed Controller replayed or forgot original journal");
            }
            checks.Check(written.Kernel.DisposeSuccesses == 1 && written.Bus.DisposeSuccesses == 1,
                scenario + ": original checked transport was not returned");
        }
    }
}
