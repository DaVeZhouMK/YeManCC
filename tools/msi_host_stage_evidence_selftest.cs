using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;

// Standalone fixture: the only compiled product source is MsiHostStageEvidence.cs.
// All producer instances use Factory/FakeSink. Never call the real TryCreate path.
internal static class MsiHostStageEvidenceSelftest
{
    private const string Host = "0123456789abcdef0123456789abcdef";
    private static int _checks;
    private static long _checkpointAllocationBytes;
    private static int _cases;
    private static readonly List<string> Failures = new();
    private static readonly List<string> Passed = new();

    private static int Main(string[] args)
    {
        if (args.Length < 2 || args.Length > 3) throw new ArgumentException("Expected contract path, result path, and optional fixture directory.");
        Run("fixed-contract-and-api", () => Contract(args[0]));
        Run("initial-layout-endian-identity-publication", Initial);
        Run("all-stages-and-reset-retention", Stages);
        Run("strict-ascii-capacities-invalid-and-no-truncation", TextFields);
        Run("strict-host-identity-and-positive-pids", Identity);
        Run("creation-collision-and-initial-write-failure", CreationFailures);
        Run("every-checkpoint-failure-disables-and-closes", CheckpointFailures);
        Run("odd-partial-record-rejected-and-sequence-not-reused", PartialFailure);
        Run("dispose-best-effort-idempotent", Disposal);
        Run("fatal-snapshot-current-diagnostic-only", Snapshot);
        Run("repeated-checkpoints-no-managed-allocation", Allocation);
        if (args.Length == 3) Run("interop-wire-fixture-beginsubmit", () => WriteWireFixture(args[2]));
        var result = new
        {
            passed = Failures.Count == 0, caseCount = _cases, checkCount = _checks,
            passedCases = Passed, failed = Failures,
            checkpointAllocationBytes = _checkpointAllocationBytes,
            allocationCycles = 20000, checkpointsPerCycle = 14,
            scope = "Standalone net10.0 producer plus synthetic fake sinks only. No InputHost assembly, real mappings, product/Host/probes, services, devices, or historical selftests executed.",
            realMappingCreationCalls = 0, inputHostProductionAssemblyLoaded = false,
            hardwareOrInputApiCalls = 0,
            allocationScope = "Warmed checkpoints through preallocated fake sinks; real MemoryMappedViewAccessor allocation behavior is not measured. FatalSnapshot and initialization intentionally allocate."
        };
        string json = JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(args[1], json, new UTF8Encoding(false));
        Console.WriteLine(json);
        return Failures.Count == 0 ? 0 : 1;
    }

    private static void Run(string name, Action action)
    {
        ++_cases;
        try { action(); Passed.Add(name); }
        catch (Exception ex) { Failures.Add(name + ": " + ex); }
    }

    private static void Check(bool condition, string message)
    {
        ++_checks;
        if (!condition) throw new InvalidOperationException(message);
    }

    private static ulong U(byte[] bytes, int offset) =>
        BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(offset, 8));
    private static long I(byte[] bytes, int offset) => unchecked((long)U(bytes, offset));
    private static void Put(byte[] bytes, int offset, ulong value) =>
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(offset, 8), value);
    private static string Text(byte[] bytes, int offset, int count) => Encoding.ASCII.GetString(bytes, offset, count);
    private static void Zero(byte[] bytes, int offset, int count, string label)
    {
        for (int i = offset; i < offset + count; ++i) Check(bytes[i] == 0, label + " at " + i);
    }

    private static MsiHostStageEvidence New(out FakeSink sink, out Factory factory)
    {
        sink = new FakeSink();
        factory = new Factory(sink);
        return MsiHostStageEvidence.TryCreateForTest(1234, 5678, Host, 10_000_000, factory)
            ?? throw new InvalidOperationException("Synthetic creation failed.");
    }

    private static void Contract(string path)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        var root = doc.RootElement;
        Check(root.GetProperty("version").GetInt32() == 1, "contract version");
        Check(root.GetProperty("capacity").GetInt32() == 1024, "capacity");
        Check(root.GetProperty("magicHex").GetString() == "315647455453484D", "magic");
        Check(root.GetProperty("mapPrefix").GetString() == MsiHostStageEvidence.MapPrefix, "map prefix");
        var offsets = new Dictionary<string, int>
        {
            ["magic"] = 0, ["version"] = 8, ["capacity"] = 16, ["publicationSequence"] = 24,
            ["hostPid"] = 32, ["parentPid"] = 40, ["qpcFrequency"] = 48, ["phase"] = 56,
            ["phaseSinceQpc"] = 64, ["readBegin"] = 72, ["readEnd"] = 80, ["handleBegin"] = 88,
            ["decoded"] = 96, ["submitBegin"] = 104, ["submitEnd"] = 112, ["handleEnd"] = 120,
            ["writeBegin"] = 128, ["writeEnd"] = 136, ["requestSequence"] = 144, ["epoch"] = 152,
            ["powerGeneration"] = 160, ["incomingChars"] = 168, ["textValidityBits"] = 176,
            ["lastCompletedSequence"] = 184, ["hostInstanceLength"] = 192, ["hostInstanceBytes"] = 200,
            ["requestIdLength"] = 264, ["requestIdBytes"] = 272, ["runIdLength"] = 336,
            ["runIdBytes"] = 344, ["commandLength"] = 408, ["commandBytes"] = 416,
            ["extensionVersion"] = 448, ["backendBegin"] = 456, ["backendEnd"] = 464,
            ["costBegin"] = 472, ["costEnd"] = 480
        };
        Check(root.GetProperty("offsets").EnumerateObject().Count() == offsets.Count, "offset count");
        foreach (var item in offsets) Check(root.GetProperty("offsets").GetProperty(item.Key).GetInt32() == item.Value, item.Key);
        string[] names = { "hostInstance", "requestId", "runId", "command" };
        int[] capacities = { 64, 64, 64, 32 };
        for (int i = 0; i < names.Length; ++i)
        {
            Check(root.GetProperty("textCapacities").GetProperty(names[i]).GetInt32() == capacities[i], names[i] + " capacity");
            Check(root.GetProperty("validityBits").GetProperty(names[i]).GetInt32() == (1 << i), names[i] + " bit");
        }
        string[] phases = { "created", "readPending", "readReceived", "handlePending", "commandDecoded",
            "submitFrameHandler", "submitFrameHandlerReturned", "handleReturned", "writePending", "writeReturned", "closed",
            "backendSwitch", "backendSwitchReturned", "costWindow", "costWindowReturned" };
        Check(root.GetProperty("phases").EnumerateObject().Count() == phases.Length, "phase count");
        for (int i = 0; i < phases.Length; ++i)
            Check(root.GetProperty("phases").GetProperty(phases[i]).GetInt32() == i + 1, "phase " + phases[i]);
        Check(root.GetProperty("singleWriter").GetBoolean(), "single writer");
        Check(root.GetProperty("extension").GetProperty("version").GetInt32() == 1, "extension marker");
        Check(typeof(MsiHostStageEvidence).Namespace == "YeManInputHost.Diagnostics", "namespace");
        Check(typeof(MsiHostStageEvidence).IsSealed && !typeof(MsiHostStageEvidence).IsPublic, "internal sealed");
        Check(typeof(IDisposable).IsAssignableFrom(typeof(MsiHostStageEvidence)), "IDisposable");
        var create = typeof(MsiHostStageEvidence).GetMethod("TryCreate", BindingFlags.Static | BindingFlags.NonPublic);
        Check(create is not null && create.ReturnType == typeof(MsiHostStageEvidence), "TryCreate return type");
        Check(create!.GetParameters().Select(p => p.ParameterType).SequenceEqual(new[] { typeof(int), typeof(string) }), "TryCreate parameters");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "YeManInputHost"), "no production assembly");
    }

    private static void Initial()
    {
        using var evidence = New(out var sink, out var factory);
        Check(factory.Calls == 1 && factory.Capacity == 1024, "one exact-capacity create");
        Check(factory.Name == "Local\\YeManCC-MSI-HostStage-v1-1234-5678-" + Host, "exact map name");
        Check(sink.OperationCount == 4 && sink.OddReadRejected == 3 && sink.EvenReads == 1, "odd/data/data/even publication");
        Check(U(sink.Bytes, 0) == 0x315647455453484DUL, "magic endian");
        Check(Text(sink.Bytes, 0, 8) == "MHSTEGV1", "magic exact bytes");
        Check(U(sink.Bytes, 8) == 1 && U(sink.Bytes, 16) == 1024, "header");
        Check(U(sink.Bytes, 24) == 2, "initial even sequence");
        Check(U(sink.Bytes, 32) == 5678 && U(sink.Bytes, 40) == 1234, "pid offsets");
        Check(U(sink.Bytes, 48) == 10_000_000, "frequency");
        Check(U(sink.Bytes, 56) == 1 && U(sink.Bytes, 64) > 0, "created phase/stamp");
        Check(U(sink.Bytes, 176) == 1 && U(sink.Bytes, 192) == 32, "host length/validity");
        Check(Text(sink.Bytes, 200, 32) == Host, "host bytes");
        var expected = new byte[1024];
        Put(expected, 0, 0x315647455453484DUL); Put(expected, 8, 1); Put(expected, 16, 1024);
        Put(expected, 24, 2); Put(expected, 32, 5678); Put(expected, 40, 1234);
        Put(expected, 48, 10_000_000); Put(expected, 56, 1); Put(expected, 64, U(sink.Bytes, 64));
        Put(expected, 176, 1); Put(expected, 192, 32); Put(expected, 448, 1);
        Encoding.ASCII.GetBytes(Host).CopyTo(expected, 200);
        Check(expected.AsSpan().SequenceEqual(sink.Bytes), "entire initial 1024 bytes including zero reserved tail");
        Check(sink.TryRead(out var copy) && copy!.AsSpan().SequenceEqual(expected), "synthetic consumer stable record");
    }

    private static void AssertStage(FakeSink sink, ulong phase, int stamp)
    {
        Check(U(sink.Bytes, 56) == phase, "phase " + phase);
        Check(U(sink.Bytes, 64) > 0 && U(sink.Bytes, 64) == U(sink.Bytes, stamp), "stage stamp " + stamp);
        Check(U(sink.Bytes, 24) != 0 && (U(sink.Bytes, 24) & 1) == 0, "committed sequence");
        Check(U(sink.Bytes, 448) == 1, "explicit extension marker");
        Zero(sink.Bytes, 488, 536, "reserved tail");
    }

    private static void Stages()
    {
        using var evidence = New(out var sink, out _);
        evidence.BeginRead(); AssertStage(sink, 2, 72);
        evidence.EndRead(0x01020304); AssertStage(sink, 3, 80);
        Check(I(sink.Bytes, 168) == 0x01020304 && sink.Bytes[168] == 4 && sink.Bytes[171] == 1, "incoming chars endian");
        evidence.BeginHandle(); AssertStage(sink, 4, 88);
        const ulong sequence = 0xFEDCBA9876543210UL, epoch = 0x0102030405060708UL, power = ulong.MaxValue;
        evidence.Decode("request-1", sequence, "run-1", epoch, power, "SUBMIT_FRAME"); AssertStage(sink, 5, 96);
        Check(U(sink.Bytes, 144) == sequence && sink.Bytes[144] == 0x10 && sink.Bytes[151] == 0xFE, "unsigned sequence endian");
        Check(U(sink.Bytes, 152) == epoch && U(sink.Bytes, 160) == power, "epoch/power offsets and unsigned range");
        Check(U(sink.Bytes, 184) == 0, "decode not completed/accepted receipt");
        Check(U(sink.Bytes, 176) == 15, "text validity bits");
        evidence.BeginSubmit(); AssertStage(sink, 6, 104);
        evidence.BeginBackend(); AssertStage(sink, 12, 456);
        evidence.EndBackend(); AssertStage(sink, 13, 464);
        evidence.BeginCost(); AssertStage(sink, 14, 472);
        evidence.EndCost(); AssertStage(sink, 15, 480);
        Check(U(sink.Bytes, 184) == 0, "backend/cost returns are not response completion");
        evidence.EndSubmit(); AssertStage(sink, 7, 112);
        Check(U(sink.Bytes, 184) == 0, "submit handler return not completed response");
        evidence.EndHandle(); AssertStage(sink, 8, 120);
        evidence.BeginWrite(); AssertStage(sink, 9, 128);
        evidence.EndWrite(); AssertStage(sink, 10, 136);
        Check(U(sink.Bytes, 184) == sequence, "EndWrite current decoded sequence");
        ulong previous = 0;
        for (int offset = 72; offset <= 136; offset += 8)
        { Check(U(sink.Bytes, offset) >= previous && U(sink.Bytes, offset) > 0, "ordered stamps"); previous = U(sink.Bytes, offset); }
        Check(U(sink.Bytes, 24) == 28, "thirteen stage publications plus creation");
        evidence.BeginRead();
        Check(U(sink.Bytes, 184) == sequence && Text(sink.Bytes, 200, 32) == Host, "reset retention");
        Check(U(sink.Bytes, 176) == 1 && U(sink.Bytes, 56) == 2, "reset host-only validity");
        Zero(sink.Bytes, 80, 96, "reset stamps/metadata/chars");
        Zero(sink.Bytes, 264, 184, "reset command fields");
        Zero(sink.Bytes, 456, 32, "reset extension stamps");
        Check(U(sink.Bytes, 448) == 1, "BeginRead retains extension marker");
        evidence.EndWrite();
        Check(U(sink.Bytes, 184) == sequence, "no decoded metadata retains previous completion");
        evidence.BeginRead(); evidence.EndRead(-1);
        Check(I(sink.Bytes, 168) == -1, "signed incoming sentinel");
        evidence.Decode("", 0, "", 0, 0, ""); evidence.EndWrite();
        Check(U(sink.Bytes, 144) == 0 && U(sink.Bytes, 184) == 0 && U(sink.Bytes, 176) == 15, "zero/empty parsed metadata has no admission gate");
    }

    private static void Field(FakeSink sink, int length, int offset, int capacity, ulong bit, string? expected)
    {
        Check(I(sink.Bytes, length) == (expected is null ? -1 : expected.Length), "field length " + length);
        Check((U(sink.Bytes, 176) & bit) == (expected is null ? 0UL : bit), "field bit " + bit);
        if (expected is null) Zero(sink.Bytes, offset, capacity, "invalid cleared");
        else
        {
            Check(Text(sink.Bytes, offset, expected.Length) == expected, "exact field bytes");
            Zero(sink.Bytes, offset + expected.Length, capacity - expected.Length, "field padding");
        }
    }

    private static void TextFields()
    {
        using var evidence = New(out var sink, out _);
        string full64 = new string('~', 64), full32 = new string(' ', 32);
        evidence.Decode(full64, 1, full64, 2, 3, full32);
        Field(sink, 264, 272, 64, 2, full64); Field(sink, 336, 344, 64, 4, full64); Field(sink, 408, 416, 32, 8, full32);
        evidence.Decode("short", 2, "r", 2, 3, "c");
        Field(sink, 264, 272, 64, 2, "short"); Field(sink, 336, 344, 64, 4, "r"); Field(sink, 408, 416, 32, 8, "c");
        string?[] invalid = { null, new string('a', 65), "prefix\0tail", "line\nbreak", "tab\t", "\u007f", "caf\u00e9", "\u0080", "\u4e2d", "\ud83d\ude00", "\ud800" };
        foreach (string? value in invalid)
        {
            evidence.Decode(value!, 5, value!, 6, 7, value!);
            Field(sink, 264, 272, 64, 2, null); Field(sink, 336, 344, 64, 4, null); Field(sink, 408, 416, 32, 8, null);
            Check(U(sink.Bytes, 176) == 1 && U(sink.Bytes, 56) == 5, "invalid text independent of parsed phase");
        }
        for (int c = 0; c < 32; ++c)
        {
            string value = "valid" + (char)c + "suffix";
            evidence.Decode(value, 8, "run", 9, 10, "cmd");
            Field(sink, 264, 272, 64, 2, null); Field(sink, 336, 344, 64, 4, "run"); Field(sink, 408, 416, 32, 8, "cmd");
        }
        for (int c = 32; c <= 126; ++c)
        {
            string value = ((char)c).ToString();
            evidence.Decode(value, 0, value, 0, 0, value);
            Field(sink, 264, 272, 64, 2, value); Field(sink, 336, 344, 64, 4, value); Field(sink, 408, 416, 32, 8, value);
        }
        evidence.Decode(full64 + "a", 99, "run", 0, 0, full32 + "a");
        Field(sink, 264, 272, 64, 2, null); Field(sink, 336, 344, 64, 4, "run"); Field(sink, 408, 416, 32, 8, null);
        evidence.EndWrite(); Check(U(sink.Bytes, 184) == 99, "invalid text does not reject metadata tuple");
        evidence.Decode("", 0, "", 0, 0, "");
        Field(sink, 264, 272, 64, 2, ""); Field(sink, 336, 344, 64, 4, ""); Field(sink, 408, 416, 32, 8, "");
    }

    private static void Identity()
    {
        string?[] bad = { null, "", Host.ToUpperInvariant(), "{" + Host + "}", "01234567-89ab-cdef-0123-456789abcdef", Host[..31], Host + "0", "g" + Host[1..], " " + Host[1..], "\0" + Host[1..] };
        foreach (string? value in bad)
        {
            var factory = new Factory(new FakeSink());
            Check(MsiHostStageEvidence.TryCreateForTest(1, 2, value!, 1, factory) is null, "invalid host");
            Check(factory.Calls == 0, "invalid host never opens sink");
        }
        foreach (int pid in new[] { 0, -1, int.MinValue })
        {
            var factory = new Factory(new FakeSink());
            Check(MsiHostStageEvidence.TryCreateForTest(pid, 2, Host, 1, factory) is null, "invalid parent");
            Check(MsiHostStageEvidence.TryCreateForTest(1, pid, Host, 1, factory) is null, "invalid host pid");
            Check(factory.Calls == 0, "invalid pid never opens sink");
        }
        foreach (long frequency in new[] { 0L, -1L })
        {
            var factory = new Factory(new FakeSink());
            Check(MsiHostStageEvidence.TryCreateForTest(1, 2, Host, frequency, factory) is null && factory.Calls == 0, "invalid frequency");
        }
        Check(MsiHostStageEvidence.TryCreateForTest(1, 2, Host, 1, null!) is null, "null factory");
        var maxFactory = new Factory(new FakeSink());
        using var max = MsiHostStageEvidence.TryCreateForTest(int.MaxValue, int.MaxValue, new string('0', 32), long.MaxValue, maxFactory);
        Check(max is not null && maxFactory.Name == MsiHostStageEvidence.MapPrefix + "2147483647-2147483647-" + new string('0', 32), "positive max pid and GUID-N zero syntax");
        CultureInfo original = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
            using var localized = New(out _, out var factory);
            Check(factory.Name == MsiHostStageEvidence.MapPrefix + "1234-5678-" + Host, "invariant name");
        }
        finally { CultureInfo.CurrentCulture = original; }
    }

    private static void CreationFailures()
    {
        foreach (Exception failure in new Exception[] { new IOException("synthetic collision"), new UnauthorizedAccessException("synthetic denied"), new InvalidOperationException("synthetic create failure") })
        {
            var factory = new Factory(new FakeSink()) { CreationFailure = failure };
            Check(MsiHostStageEvidence.TryCreateForTest(1, 2, Host, 1, factory) is null, "creation fails closed");
            Check(factory.Calls == 1, "no adoption or retry on create failure");
        }
        Check(MsiHostStageEvidence.TryCreateForTest(1, 2, Host, 1, new Factory(null)) is null, "null sink");
        for (int operation = 1; operation <= 4; ++operation)
        {
            var sink = new FakeSink { FailAt = operation, DisposeThrows = true };
            Check(MsiHostStageEvidence.TryCreateForTest(1, 2, Host, 1, new Factory(sink)) is null, "initial publish failure " + operation);
            Check(sink.DisposeCount == 1, "failed initialization cleanup");
        }
    }

    private static readonly Action<MsiHostStageEvidence>[] Checkpoints =
    {
        e => e.BeginRead(), e => e.EndRead(42), e => e.BeginHandle(),
        e => e.Decode("request", 42, "run", 7, 8, "SUBMIT_FRAME"),
        e => e.BeginSubmit(), e => e.BeginBackend(), e => e.EndBackend(),
        e => e.BeginCost(), e => e.EndCost(), e => e.EndSubmit(), e => e.EndHandle(),
        e => e.BeginWrite(), e => e.EndWrite()
    };

    private static void CheckpointFailures()
    {
        foreach (var checkpoint in Checkpoints)
        for (int operation = 1; operation <= 4; ++operation)
        {
            using var evidence = New(out var sink, out _);
            sink.FailAt = sink.OperationCount + operation;
            checkpoint(evidence); // Must not throw regardless of failed publication step.
            Check(sink.DisposeCount == 1 && U(sink.Bytes, 56) == 11, "fault closes and releases");
            using var snapshot = JsonDocument.Parse(JsonSerializer.Serialize(evidence.FatalSnapshot()));
            Check(snapshot.RootElement.GetProperty("disabled").GetBoolean(), "fault disables");
            int before = sink.OperationCount;
            foreach (var later in Checkpoints) later(evidence);
            Check(sink.OperationCount == before, "disabled checkpoints do not touch sink");
        }
    }

    private static void PartialFailure()
    {
        using var evidence = New(out var sink, out _);
        sink.FailAt = sink.OperationCount + 3; sink.PersistentFailure = true;
        evidence.Decode("request", 1, "run", 2, 3, "SUBMIT_FRAME");
        Check((U(sink.Bytes, 24) & 1) == 1 && !sink.TryRead(out _), "partially written record never accepted");
        Check(sink.DisposeCount == 1, "persistent failure still releases");
        using var second = New(out var secondSink, out _);
        secondSink.FailAt = secondSink.OperationCount + 4;
        secondSink.ThrowAfterCommit = true;
        second.BeginRead();
        Check(U(secondSink.Bytes, 24) == 6 && U(secondSink.Bytes, 56) == 11, "throw after even commit reserves new sequence for Closed");
        Check(secondSink.DisposeCount == 1, "throw after commit cleanup");
        using var exhausted = New(out var exhaustedSink, out _);
        typeof(MsiHostStageEvidence).GetField("_publicationSequence", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(exhausted, ulong.MaxValue - 1);
        int before = exhaustedSink.OperationCount;
        exhausted.BeginRead();
        Check(exhaustedSink.OperationCount == before && exhaustedSink.DisposeCount == 1, "sequence exhaustion disables without forbidden zero");
    }

    private static void Disposal()
    {
        var evidence = New(out var sink, out _);
        evidence.Decode("request", 21, "run", 1, 2, "SUBMIT_FRAME"); evidence.EndWrite();
        evidence.Dispose();
        Check(U(sink.Bytes, 56) == 11 && U(sink.Bytes, 184) == 21 && sink.DisposeCount == 1, "Closed preserves completion");
        int before = sink.OperationCount;
        evidence.Dispose(); foreach (var checkpoint in Checkpoints) checkpoint(evidence);
        Check(before == sink.OperationCount && sink.DisposeCount == 1, "idempotent dispose/no subsequent writes");
        var failing = New(out var badSink, out _);
        badSink.FailAt = badSink.OperationCount + 1; badSink.PersistentFailure = true; badSink.DisposeThrows = true;
        failing.Dispose(); failing.Dispose();
        Check(badSink.DisposeCount == 1, "closed publication/disposal errors are contained");
    }

    private static void Snapshot()
    {
        using var evidence = New(out var sink, out _);
        evidence.BeginRead(); evidence.EndRead(123); evidence.BeginHandle();
        evidence.Decode("request", 17, "run", 18, 19, "SUBMIT_FRAME"); evidence.BeginSubmit();
        int before = sink.OperationCount;
        using var snapshot = JsonDocument.Parse(JsonSerializer.Serialize(evidence.FatalSnapshot()));
        var s = snapshot.RootElement;
        Check(s.GetProperty("phase").GetUInt64() == 6 && s.GetProperty("requestSequence").GetUInt64() == 17, "current diagnostic state");
        Check(s.GetProperty("incomingChars").GetInt64() == 123 && s.GetProperty("lastCompletedSequence").GetUInt64() == 0, "snapshot observations");
        Check(s.GetProperty("scope").GetString()!.Contains("no tuple, consumer, or Steam acceptance guarantee"), "fatal scope");
        Check(s.GetProperty("hostInstanceId").GetString() == Host && s.GetProperty("command").GetString() == "SUBMIT_FRAME", "snapshot text");
        Check(sink.OperationCount == before, "snapshot does not publish or change phase");
        evidence.BeginRead();
        using var reset = JsonDocument.Parse(JsonSerializer.Serialize(evidence.FatalSnapshot()));
        Check(reset.RootElement.GetProperty("requestSequence").GetUInt64() == 0 && reset.RootElement.GetProperty("submitBegin").GetUInt64() == 0 && reset.RootElement.GetProperty("requestId").GetString() == "", "snapshot reset current command");
        evidence.Decode("bad\n", 1, null!, 0, 0, "cmd");
        using var invalid = JsonDocument.Parse(JsonSerializer.Serialize(evidence.FatalSnapshot()));
        Check(invalid.RootElement.GetProperty("requestId").ValueKind == JsonValueKind.Null && invalid.RootElement.GetProperty("requestIdLength").GetInt64() == -1, "invalid snapshot text");
    }

    private static void Cycle(MsiHostStageEvidence evidence, ulong sequence)
    {
        evidence.BeginRead(); evidence.EndRead(200); evidence.BeginHandle();
        evidence.Decode("request", sequence, "run", 1, 2, "SUBMIT_FRAME");
        evidence.BeginSubmit(); evidence.BeginBackend(); evidence.EndBackend();
        evidence.BeginCost(); evidence.EndCost(); evidence.EndSubmit(); evidence.EndHandle(); evidence.BeginWrite(); evidence.EndWrite();
        // Exercise a second Decode to include invalid-field validation in the bound.
        evidence.Decode("invalid\n", sequence, "run", 1, 2, "SUBMIT_FRAME");
    }

    private static void WriteWireFixture(string directory)
    {
        Directory.CreateDirectory(directory);
        var sink = new FakeSink();
        var clock = new DeterministicClock(1000, 1000);
        var factory = new Factory(sink);
        using var evidence = MsiHostStageEvidence.TryCreateForTest(111, 222, Host, clock, factory)
            ?? throw new InvalidOperationException("Synthetic fixture creation failed.");
        evidence.BeginRead();
        evidence.EndRead(0);
        evidence.BeginHandle();
        evidence.Decode("req-42", 42, "run-7", 7, 3, "SUBMIT_FRAME");
        evidence.BeginSubmit();
        Check(U(sink.Bytes, 24) == 12 && U(sink.Bytes, 56) == 6, "BeginSubmit fixture phase/sequence");
        Check(U(sink.Bytes, 48) == 1000 && U(sink.Bytes, 64) == 1005, "BeginSubmit fixture frequency/tick");
        Check(Text(sink.Bytes, 200, 32) == Host, "BeginSubmit fixture host identity: " + Text(sink.Bytes, 200, 32));
        Check(Text(sink.Bytes, 272, 6) == "req-42", "BeginSubmit fixture request id: " + Text(sink.Bytes, 272, 6));
        Check(Text(sink.Bytes, 344, 5) == "run-7", "BeginSubmit fixture run id: " + Text(sink.Bytes, 344, 5));
        Check(Text(sink.Bytes, 416, 12) == "SUBMIT_FRAME", "BeginSubmit fixture command: " + Text(sink.Bytes, 416, 11));
        string wirePath = Path.Combine(directory, "msi-host-stage-beginsubmit-1024.bin");
        File.WriteAllBytes(wirePath, sink.Bytes);
        var expected = new
        {
            stage = "BeginSubmit",
            mapName = MsiHostStageEvidence.MapPrefix + "111-222-" + Host,
            capacity = 1024,
            qpcFrequency = U(sink.Bytes, 48),
            ticks = new
            {
                phaseSinceQpc = U(sink.Bytes, 64), readBegin = U(sink.Bytes, 72),
                readEnd = U(sink.Bytes, 80), handleBegin = U(sink.Bytes, 88),
                decoded = U(sink.Bytes, 96), submitBegin = U(sink.Bytes, 104)
            },
            publicationSequence = U(sink.Bytes, 24),
            phase = U(sink.Bytes, 56),
            hostPid = U(sink.Bytes, 32), parentPid = U(sink.Bytes, 40),
            hostInstanceId = Text(sink.Bytes, 200, checked((int)U(sink.Bytes, 192))),
            requestId = Text(sink.Bytes, 272, checked((int)U(sink.Bytes, 264))),
            requestSequence = U(sink.Bytes, 144),
            runId = Text(sink.Bytes, 344, checked((int)U(sink.Bytes, 336))),
            epoch = U(sink.Bytes, 152), powerGeneration = U(sink.Bytes, 160),
            command = Text(sink.Bytes, 416, checked((int)U(sink.Bytes, 408))),
            incomingChars = I(sink.Bytes, 168), textValidityBits = U(sink.Bytes, 176),
            lastCompletedSequence = U(sink.Bytes, 184),
            note = "Actual C# producer bytes captured from the injected fake sink at BeginSubmit; no named mapping was created."
        };
        string expectedPath = Path.Combine(directory, "msi-host-stage-beginsubmit-expected.json");
        File.WriteAllText(expectedPath, JsonSerializer.Serialize(expected, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
        Check(new FileInfo(wirePath).Length == 1024, "fixture exact wire length");
        Check(clock.LastTimestamp == 1005, "fixture last tick");
        evidence.BeginBackend(); evidence.EndBackend(); evidence.BeginCost(); evidence.EndCost();
        File.WriteAllBytes(Path.Combine(directory, "msi-host-stage-costreturn-1024.bin"), sink.Bytes);
        Check(U(sink.Bytes, 56) == 15 && U(sink.Bytes, 448) == 1, "extension interop fixture");
    }

    private static void Allocation()
    {
        using var evidence = New(out var sink, out _);
        for (ulong i = 0; i < 10000; ++i) Cycle(evidence, i);
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (ulong i = 0; i < 20000; ++i) Cycle(evidence, i);
        _checkpointAllocationBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        Check(_checkpointAllocationBytes == 0, "warmed checkpoints allocated " + _checkpointAllocationBytes + " bytes");
        Check(sink.DisposeCount == 0 && U(sink.Bytes, 184) == 19999, "allocation loop remains active and completes responses");
    }

    private sealed class DeterministicClock : MsiHostStageEvidence.IClock
    {
        private long _next;
        internal long LastTimestamp { get; private set; }
        public long Frequency { get; }
        internal DeterministicClock(long firstTimestamp, long frequency)
        { _next = firstTimestamp; Frequency = frequency; }
        public long GetTimestamp()
        {
            LastTimestamp = _next;
            return _next++;
        }
    }

    private sealed class Factory : MsiHostStageEvidence.ISinkFactory
    {
        private readonly FakeSink? _sink;
        internal int Calls, Capacity;
        internal string? Name;
        internal Exception? CreationFailure;
        internal Factory(FakeSink? sink) { _sink = sink; }
        public MsiHostStageEvidence.ISink CreateNew(string name, int capacity)
        {
            ++Calls; Name = name; Capacity = capacity;
            if (CreationFailure is not null) throw CreationFailure;
            return _sink!;
        }
    }

    private sealed class FakeSink : MsiHostStageEvidence.ISink
    {
        internal readonly byte[] Bytes = new byte[1024];
        internal int OperationCount, DisposeCount, FailAt, OddReadRejected, EvenReads;
        internal bool PersistentFailure, DisposeThrows, ThrowAfterCommit;
        private int _step;
        private ulong _odd;
        private bool ShouldFail()
        {
            ++OperationCount;
            return FailAt > 0 && (OperationCount == FailAt || (PersistentFailure && OperationCount >= FailAt));
        }
        public void WriteSequence(ulong value)
        {
            bool fail = ShouldFail();
            if (fail && !(ThrowAfterCommit && (value & 1) == 0)) throw new IOException("Synthetic sequence failure.");
            if ((value & 1) != 0)
            {
                _odd = value; _step = 1;
            }
            else
            {
                if (value == 0 || _step != 3 || value != _odd + 1) throw new InvalidOperationException("Bad publication order.");
                _step = 0;
            }
            Put(Bytes, 24, value);
            Observe();
            if (fail) throw new IOException("Synthetic failure after commit.");
        }
        public void WriteData(byte[] buffer, int offset, int count)
        {
            if (ShouldFail()) throw new IOException("Synthetic data failure.");
            // Accept full records and equivalent mutable-region publications.
            bool full = offset == 0 || offset == 32;
            bool first = full ? offset == 0 && count == 24 : offset == 56 &&
                count == (U(buffer, 56) is 2 or 5 ? 392 : 136);
            bool second = full ? offset == 32 && count == 992 : offset == 456 && count == 32;
            if (!((_step == 1 && first) || (_step == 2 && second)))
                throw new InvalidOperationException("Data writes must exclude the sequence slot and preserve all mutable fields.");
            buffer.AsSpan(offset, count).CopyTo(Bytes.AsSpan(offset, count));
            ++_step;
            Observe();
        }
        private void Observe()
        {
            ulong value = U(Bytes, 24);
            if (value == 0 || (value & 1) != 0) ++OddReadRejected;
            else ++EvenReads;
        }
        internal bool TryRead(out byte[]? copy)
        {
            // Synthetic seqlock consumer, bounded to the contract's three attempts.
            copy = null;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                ulong before = U(Bytes, 24);
                if (before == 0 || (before & 1) != 0) continue;
                System.Threading.Thread.MemoryBarrier();
                var candidate = (byte[])Bytes.Clone();
                System.Threading.Thread.MemoryBarrier();
                ulong after = U(Bytes, 24);
                if (before == after && (after & 1) == 0) { copy = candidate; return true; }
            }
            return false;
        }
        public void Dispose()
        {
            ++DisposeCount;
            if (DisposeThrows) throw new IOException("Synthetic disposal failure.");
        }
    }
}
