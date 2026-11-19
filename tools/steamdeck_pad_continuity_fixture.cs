using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using HIDMaestro;
using YeManInputHost.Diagnostics;

internal static class SteamDeckPadContinuityEntry
{
    private static int Main(string[] args) => args.Length switch
    {
        2 => Program.VerifySteamDeckPadContinuity(args[0], args[1], skipSdkWrite: false),
        3 when args[2] == "--negative-skip-sdk-write" => Program.VerifySteamDeckPadContinuity(args[0], args[1], skipSdkWrite: true),
        _ => 2
    };
}

internal static partial class Program
{
    // Only fields used by the pinned SDK's synchronous writer and reader.
    // No SDK constructor, named mapping, event handle, pump or SDK Dispose.
    private sealed class PadRawSlot : IDisposable
    {
        private const int SlotSize = 362, PayloadOffset = 8, ExtendedSizeOffset = 278, GuardSize = 16;
        private const BindingFlags Instance = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        private const int StaleExtendedSize = 64;
        private static readonly FieldInfo IdleThread = typeof(HMController).GetField("_idleThread", Instance)!;
        private delegate bool TryReadFrame(Span<byte> bytes, out int length);
        private readonly byte[] _read = new byte[SlotSize + 2 * GuardSize];
        private readonly byte[] _consumerReport = new byte[80];
        private readonly TryReadFrame _readConsumer;
        private readonly nint _memory;
        private nint View => _memory + GuardSize;
        private uint _lastSequence;
        internal HMController Controller { get; }

        internal PadRawSlot(HMProfile profile, uint seed)
        {
            var type = typeof(HMController);
            if (!HostControllerOutputStop.IsPinnedCore(type)) throw new InvalidDataException("unexpected SDK identity");
            var assembly = type.Assembly;
            var io = assembly.GetType("HIDMaestro.Internal.SharedMemoryIO", true)!;
            int Constant(string name) => (int)io.GetField(name)!.GetRawConstantValue()!;
            if (Constant("SHARED_INPUT_SIZE") != SlotSize || Constant("DATA_OFFSET") != PayloadOffset ||
                Constant("EXTENDED_SIZE_OFFSET") != ExtendedSizeOffset || Constant("DATA_CAPACITY") != 256)
                throw new InvalidDataException("unexpected SDK input slot layout");
            var inner = typeof(HMProfile).GetProperty("Inner", Instance)!.GetValue(profile)!;
            var innerType = inner.GetType();
            var planType = assembly.GetType("HIDMaestro.Internal.PresentationPlan", true)!;
            var plan = planType.GetMethod("Of", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic)!.Invoke(null, new[] { inner })!;
            var ext = profile.ExtendedReport;
            if (!profile.RequiresUsbipBackend || profile.BuildsItsOwnReports ||
                innerType.GetProperty("StateBlock", Instance)!.GetValue(inner) is not null ||
                planType.GetProperty("PacksGip", Instance)!.GetValue(plan) is not false ||
                ext is null || ext.Size != 64 || (ext.AlwaysArmed && ext.ReportIdByte != 0))
                throw new InvalidDataException("unexpected stock SD raw submission lane");
            var builder = innerType.GetMethod("GetOrBuildReportBuilder", Instance)!.Invoke(inner, null)!;
            var usbip = assembly.GetType("HIDMaestro.Internal.Usbip.UsbipEmulatedDevice", true)!;
            var consumer = RuntimeHelpers.GetUninitializedObject(usbip);
            usbip.GetField("_builder", Instance)!.SetValue(consumer, builder);
            _readConsumer = usbip.GetMethod("TryReadInputFrame", Instance)!.CreateDelegate<TryReadFrame>(consumer);
            Controller = (HMController)RuntimeHelpers.GetUninitializedObject(type);
            void Set(string name, object value) => type.GetField(name, Instance)!.SetValue(Controller, value);
            Set("<Profile>k__BackingField", profile);
            Set("_submitLock", new object());
            Set("_reportBuilder", builder);
            Set("_rawReportBuffer", new byte[256]);
            var previous = new byte[64];
            BinaryPrimitives.WriteUInt32LittleEndian(previous.AsSpan(4), seed);
            Set("_extendedReportBuffer", previous);

            _memory = Marshal.AllocHGlobal(_read.Length);
            try
            {
                _read.AsSpan(0, GuardSize).Fill(0x5a);
                _read.AsSpan(GuardSize + SlotSize).Fill(0x5a);
                Marshal.Copy(_read, 0, _memory, _read.Length);
                Marshal.WriteInt32(View, ExtendedSizeOffset, StaleExtendedSize);
                Set("_inputView", View);
                usbip.GetField("_inputView", Instance)!.SetValue(consumer, View);
            }
            catch { Marshal.FreeHGlobal(_memory); throw; }
        }

        internal bool RejectThreadWithoutClock(HMProfile profile, CommandEnvelope e)
        {
            bool rejected = false;
            IdleThread.SetValue(Controller, Thread.CurrentThread);
            try { SubmitSteamDeckWithScreenPads(Controller, profile, e); }
            catch (InvalidOperationException ex) when (ex.Message == "steamdeck-idle-handoff:sdk-idle-clock-thread-without-clock") { rejected = true; }
            finally { IdleThread.SetValue(Controller, null); }
            return rejected && !_screenPadOwnsReport && Marshal.ReadInt32(View, 0) == 0 &&
                Marshal.ReadInt32(View, 4) == 0 && Marshal.ReadInt32(View, ExtendedSizeOffset) == StaleExtendedSize;
        }

        internal bool ReadAfterSubmitMatches(ReadOnlySpan<byte> expected)
        {
            Marshal.Copy(_memory, _read, 0, _read.Length);
            var slot = _read.AsSpan(GuardSize, SlotSize);
            var sequence = BinaryPrimitives.ReadUInt32LittleEndian(slot);
            if (sequence != unchecked(_lastSequence + 2) ||
                BinaryPrimitives.ReadUInt32LittleEndian(slot[4..]) != expected.Length ||
                BinaryPrimitives.ReadUInt32LittleEndian(slot[ExtendedSizeOffset..]) != 0 ||
                _read.AsSpan(0, GuardSize).ContainsAnyExcept((byte)0x5a) ||
                _read.AsSpan(GuardSize + SlotSize).ContainsAnyExcept((byte)0x5a)) return false;
            _lastSequence = sequence;
            return slot.Slice(PayloadOffset, expected.Length).SequenceEqual(expected);
        }

        internal bool ReadConsumerMatches(ReadOnlySpan<byte> expected) =>
            _readConsumer(_consumerReport, out var length) && length == expected.Length &&
            _consumerReport.AsSpan(0, length).SequenceEqual(expected) &&
            !_readConsumer(_consumerReport, out _);

        public void Dispose() => Marshal.FreeHGlobal(_memory);
    }

    internal static int VerifySteamDeckPadContinuity(string vectors, string result, bool skipSdkWrite)
    {
        using var doc = JsonDocument.Parse(File.ReadAllBytes(vectors));
        using var context = new SdkDataOnlyProfiles();
        context.LoadDefaultProfiles();
        var profile = context.GetProfile("steam-deck-composite") ?? throw new InvalidDataException("Missing SD data-only profile");
        var root = doc.RootElement;
        var variants = new[] { "legacy", "typed", "compact" };
        var buffers = variants.Select(_ => new byte[64]).ToArray();
        var counters = variants.Select(_ => uint.MaxValue - 2).ToArray();
        var sdkSlots = new PadRawSlot?[variants.Length];
        var hostState = new (bool OwnsReport, uint Counter)[variants.Length];
        int sdkWrites = 0, sdkReaderMatches = 0, rejectedClockBindings = 0;
        const ulong padMask = 15UL << 17;
        var binding = new string('b', 64);
        int index = 0, active = 0, dual = 0, clicks = 0, neutral = 0;
        ulong previousTick = 0, previousEpoch = 0;
        string stage = "initial";
        CommandEnvelope? boundTuple = null;
        CommandEnvelope? heldExample = null;
        var cases = new HashSet<string>();
        void Check(bool ok, string cause)
        {
            if (!ok) throw new InvalidDataException($"frame {index}, {stage}: {cause}");
        }
        static ScreenPad Expected(JsonElement j) => new(j.GetProperty("active").GetBoolean(), j.GetProperty("click").GetBoolean(),
            j.GetProperty("x").GetInt16(), j.GetProperty("y").GetInt16(), j.GetProperty("pressure").GetUInt16());
        static bool PadBytesMatch(byte[] report, int xy, int pressure, ScreenPad pad) =>
            BinaryPrimitives.ReadInt16LittleEndian(report.AsSpan(xy)) == pad.X &&
            BinaryPrimitives.ReadInt16LittleEndian(report.AsSpan(xy + 2)) == pad.Y &&
            BinaryPrimitives.ReadUInt16LittleEndian(report.AsSpan(pressure)) == pad.Pressure;
        try
        {
            Check(root.GetProperty("ok").GetBoolean() && root.GetProperty("systemInputCalls").GetInt32() == 0, "native fixture unsafe or failed");
            foreach (var row in root.GetProperty("frames").EnumerateArray())
            {
                index++;
                stage = row.GetProperty("stage").GetString()!;
                cases.Add(stage);
                var tick = row.GetProperty("tick").GetUInt64();
                var epoch = row.GetProperty("epoch").GetUInt64();
                Check(tick > previousTick && (epoch == previousEpoch || epoch == (index == 1 ? 29UL : previousEpoch + 1)), "native tick or epoch order");
                if (epoch != previousEpoch)
                {
                    boundTuple = null;
                    if (index != 1)
                    {
                        Check(stage == "replacement-neutral", "epoch replacement must start neutral");
                        Array.Fill(counters, 0U); // Synthetic starting counter for the new target.
                    }
                    for (int v = 0; v < variants.Length; v++)
                    {
                        sdkSlots[v]?.Dispose();
                        sdkSlots[v] = null;
                        sdkSlots[v] = new PadRawSlot(profile, counters[v]);
                        hostState[v] = (false, 0x5a5a5a5a);
                    }
                }
                previousTick = tick;
                previousEpoch = epoch;
                var left = Expected(row.GetProperty("left"));
                var right = Expected(row.GetProperty("right"));
                if (left.Active || right.Active) active++; else neutral++;
                if (left.Active && right.Active) dual++;
                if (left.Click || right.Click) clicks++;
                var expectedBits = (left.Click ? 1UL << 17 : 0) | (right.Click ? 1UL << 18 : 0) |
                    (left.Active ? 1UL << 19 : 0) | (right.Active ? 1UL << 20 : 0);
                for (int v = 0; v < variants.Length; v++)
                {
                    string text = row.GetProperty(variants[v]).GetString()!;
                    bool compact = v == 2;
                    Check(TryParseEnvelope(text, out var e, out var error, compact ? boundTuple : null, compact ? binding : null,
                        utf8Message: Encoding.UTF8.GetBytes(text)) && e is not null, $"{variants[v]} parser rejected: {error}");
                    if (e is null) throw new InvalidDataException("Parser passed without an envelope");
                    boundTuple ??= e;
                    Check(e.Sequence == (ulong)index && e.SampleSequence == (ulong)index && e.MonotonicTick == tick && e.Epoch == epoch, $"{variants[v]} frame identity");
                    Check(e.ScreenPadsPresent == (left.Active || right.Active) && e.ScreenPadLeft == left && e.ScreenPadRight == right, $"{variants[v]} parsed pad mismatch");
                    var counter = unchecked(++counters[v]);
                    var wire = BuildSteamDeckScreenPadReport(profile, e, counter, buffers[v]);
                    Check(ReferenceEquals(wire, buffers[v]) && wire.Length == 64 && wire[0] == 1 && wire[2] == 9 && wire[3] == 64 &&
                        BinaryPrimitives.ReadUInt32LittleEndian(wire.AsSpan(4)) == counter, $"{variants[v]} report header or reused buffer");
                    Check((BinaryPrimitives.ReadUInt64LittleEndian(wire.AsSpan(8)) & padMask) == expectedBits &&
                        PadBytesMatch(wire, 16, 56, left) && PadBytesMatch(wire, 20, 58, right), $"{variants[v]} SD touch bits or data");
                    Check(v == 0 || wire.AsSpan().SequenceEqual(buffers[0]), $"{variants[v]} encoded bytes differ from legacy");

                    // Host ownership is session-global. Isolate the three equivalent
                    // streams while alternating them against separate SDK memory slots.
                    var slot = sdkSlots[v]!;
                    (_screenPadOwnsReport, _screenPadPacketCounter) = hostState[v];
                    if (index == 1 && v == 0)
                    {
                        Check(slot.RejectThreadWithoutClock(profile, e), "malformed clock ownership wrote or claimed the slot");
                        rejectedClockBindings++;
                    }
                    if (!(skipSdkWrite && index == 18 && v == 2))
                        SubmitSteamDeckWithScreenPads(slot.Controller, profile, e);
                    Check(slot.ReadAfterSubmitMatches(wire),
                        $"{variants[v]} SDK slot sequence, report size, lane, guard, or payload mismatch");
                    Check(slot.ReadConsumerMatches(wire), $"{variants[v]} USB/IP reader report mismatch or duplicate");
                    Check(_screenPadOwnsReport && _screenPadPacketCounter == counter, $"{variants[v]} Host report ownership or counter mismatch");
                    hostState[v] = (_screenPadOwnsReport, _screenPadPacketCounter);
                    sdkWrites++;
                    sdkReaderMatches++;
                    if (v == 0 && stage == "left-down") heldExample = e;
                }
                if (stage == "left-down")
                {
                    var text = row.GetProperty("compact").GetString()!;
                    Check(!TryParseEnvelope(text, out _, out var error, boundTuple, new string('c', 64)) && error == "compact-frame-request-binding-mismatch", "wrong compact binding was not rejected");
                    var invalid = JsonNode.Parse(row.GetProperty("typed").GetString()!)!;
                    invalid["touchpads"]!["left"]!["x"] = 32768;
                    Check(!TryParseEnvelope(invalid.ToJsonString(), out _, out _), "out-of-range active coordinate was accepted");
                }
                if (index == 3) Check(counters.All(c => c == 0), "counter wrap not exercised");
            }
            Check(index == 224 && active == 212 && dual == 156 && clicks == 1 && neutral == 12 &&
                sdkWrites == 672 && sdkReaderMatches == 672 && rejectedClockBindings == 1 &&
                cases.Contains("replacement-fresh-move") && stage == "final-neutral", "scenario coverage changed");

            // Delay only the reader: confirm the actual SDK reader recovers the
            // latest report, not the two reports published between its reads.
            var held = heldExample ?? throw new InvalidDataException("missing held report for delayed reader");
            var released = held with { ScreenPadsPresent = false, ScreenPadLeft = default, ScreenPadRight = default };
            using (var slot = new PadRawSlot(profile, 0))
            {
                _screenPadOwnsReport = false;
                _screenPadPacketCounter = uint.MaxValue;
                byte[] Publish(CommandEnvelope e, uint expected, bool isNeutral = false)
                {
                    SubmitSteamDeckWithScreenPads(slot.Controller, profile, e, isNeutralCommand: isNeutral);
                    var wire = BuildSteamDeckScreenPadReport(profile, e, expected);
                    Check(slot.ReadAfterSubmitMatches(wire), "delayed reader setup failed to publish");
                    return wire;
                }
                Check(slot.ReadConsumerMatches(Publish(held, 1)), "delayed reader initial contact mismatch");
                Publish(released, 2, isNeutral: true);
                var next = Publish(held, 3);
                Check(slot.ReadConsumerMatches(next), "delayed reader did not return the newest report only");
            }
            File.WriteAllText(result, JsonSerializer.Serialize(new { ok = true, frames = index, variants, encoded = index * variants.Length,
                active, dual, clicks, neutral, sdkWrites, sdkReaderMatches, rejectedClockBindings, sdkVersion = typeof(HMController).Assembly.GetName().Version?.ToString(),
                sdkMvid = typeof(HMController).Module.ModuleVersionId, sdkScope = "actual-SubmitRawReport-and-USBIP-TryReadInputFrame-on-private-memory-only",
                delayedReaderSawLatestOnly = true, liveIdleClockExercised = false, controllerConstructorCalled = false,
                pipeOpened = false, usbipPumpStarted = false, usbipSocketsOpened = false,
                driverOrSteamConsumptionProven = false }));
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }
        finally
        {
            foreach (var slot in sdkSlots) slot?.Dispose();
        }
    }
}
