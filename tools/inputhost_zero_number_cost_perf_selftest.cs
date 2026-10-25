using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using P = GeneratedParsers;

internal static class HostZeroCostFixture
{
    private static long Checks, Sink;
    private static readonly string[] Axes = ["lx", "ly", "rx", "ry", "lt", "rt", "ltRaw", "rtRaw"];
    private static void Check(bool condition, string context)
    {
        Checks++;
        if (!condition) throw new InvalidOperationException(context);
    }
    private static string Frame(string[]? axes = null, int identity = 0, bool optional = false, string persona = "dualshock4")
    {
        axes ??= ["0", "0", "0", "0", "0", "0", "0", "0"];
        var v = axes.Select(s => { using var d = JsonDocument.Parse(s); return d.RootElement.GetSingle(); }).ToArray();
        var hash = P.Baseline.Hash(0, v[0], v[1], v[2], v[3], v[4], v[5]);
        var raw = optional ? ",\"ltRaw\":" + axes[6] + ",\"rtRaw\":" + axes[7] + ",\"raw\":{\"buttons\":0,\"sThumbLX\":0,\"sThumbLY\":0,\"sThumbRX\":0,\"sThumbRY\":0,\"bLeftTrigger\":0,\"bRightTrigger\":0}" : "";
        return $$"""{"protocolVersion":1,"command":"SUBMIT_FRAME","requestId":"request-{{identity}}","sequence":{{identity + 10}},"runId":"run-{{identity % 3}}","epoch":{{identity % 4 + 1}},"powerGeneration":{{identity % 5 + 1}},"targetId":"target-{{identity % 7}}","ownerIdentity":"coordinator","persona":"{{persona}}","profileIdentity":"profile-{{persona}}","startupNonce":"nonce-{{identity % 8}}","configRevision":{{identity % 9 + 1}},"configHash":"{{new string('a', 64)}}","buttons":0,"lx":{{axes[0]}},"ly":{{axes[1]}},"rx":{{axes[2]}},"ry":{{axes[3]}},"lt":{{axes[4]}},"rt":{{axes[5]}},"sampleSequence":{{identity + 1}},"monotonicTick":{{identity + 2}},"normalizedInputHash":"{{hash}}","motionPairProofId":"pair-{{identity}}"{{raw}}}""";
    }
    private static string Describe(bool accepted, P.CommandEnvelope? e, string error)
    {
        var values = new SortedDictionary<string, object?>();
        if (e is not null)
            foreach (var prop in typeof(P.CommandEnvelope).GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                var v = prop.GetValue(e);
                values[prop.Name] = v;
                if (v is float f) values[prop.Name + "#bits"] = BitConverter.SingleToInt32Bits(f);
                if (v is double d) values[prop.Name + "#bits"] = BitConverter.DoubleToInt64Bits(d);
            }
        return JsonSerializer.Serialize(new { accepted, error, values });
    }
    private static string Capture(bool candidate, string message)
    {
        try
        {
            var accepted = candidate ? P.Candidate.Parse(message, out var e, out var error) : P.Baseline.Parse(message, out e, out error);
            return Describe(accepted, e, error);
        }
        catch (Exception ex) { return "exception:" + ex.GetType().FullName + ":" + ex.Message; }
    }
    private static void Compare(string message, string name) => Check(Capture(false, message) == Capture(true, message), "Full parser mismatch: " + name);
    private static List<string> Tokens()
    {
        var result = new HashSet<string>(StringComparer.Ordinal) { "0", "-0", "0.0", "-0.0", "0e-9999999999999999999999999", "-0e-9999999999999999999999999", "1", "-1", "1.00000001", "-1.00000001", "1.0000001", "-1.0000001", "0.99999999", "-0.99999999", "1.401298464324817e-45", "-1.401298464324817e-45", "7.006492321624085e-46", "-7.006492321624085e-46", "7.006492321624086e-46", "-7.006492321624086e-46", "3.4028235e38", "-3.4028235e38", "1e10000", "-1e10000", "1e-10000", "-1e-10000" };
        for (var exponent = -400; exponent <= 50; exponent++)
            foreach (var sign in new[] { "", "-" })
                foreach (var mantissa in new[] { "0", "1", "1.0000000000000000001", "7.006492321624085", "9.9999999999999999999" })
                    result.Add(sign + mantissa + "e" + exponent.ToString(CultureInfo.InvariantCulture));
        var random = new Random(20261018);
        for (var k = 0; k < 4096; k++)
        {
            var f = BitConverter.Int32BitsToSingle(random.Next(int.MinValue, int.MaxValue));
            if (float.IsFinite(f)) result.Add(f.ToString("R", CultureInfo.InvariantCulture));
        }
        return result.ToList();
    }
    private static object Semantics()
    {
        var tokens = Tokens();
        var numberComparisons = 0;
        foreach (var token in tokens.Concat(["null", "true", "false", "\"0\"", "{}", "[]"]))
        {
            using var d = JsonDocument.Parse(token);
            var p = new Dictionary<string, JsonElement> { ["v"] = d.RootElement };
            var a = P.Baseline.Axis(p, out var av); var b = P.Candidate.Axis(p, out var bv);
            Check(a == b && BitConverter.SingleToInt32Bits(av) == BitConverter.SingleToInt32Bits(bv), "Axis token " + token);
            a = P.Baseline.Trigger(p, out av); b = P.Candidate.Trigger(p, out bv);
            Check(a == b && BitConverter.SingleToInt32Bits(av) == BitConverter.SingleToInt32Bits(bv), "Trigger token " + token);
            numberComparisons += 2;
        }
        var missing = new Dictionary<string, JsonElement>();
        Check(!P.Candidate.Axis(missing, out var zero) && BitConverter.SingleToInt32Bits(zero) == 0, "Missing number");
        var full = 0;
        // Explicit signed zero, subnormal rounding, range, underflow, exponent and lexical boundary at every reader site.
        foreach (var token in tokens.Where((_, i) => i < 26 || i % 37 == 0))
            for (var field = 0; field < Axes.Length; field++)
            {
                var v = new[] { "0", "0", "0", "0", "0", "0", "0", "0" }; v[field] = token;
                Compare(Frame(v, ++full, true), Axes[field] + ":" + token);
            }
        var frame = Frame(optional: true);
        var baseNode = JsonNode.Parse(frame)!.AsObject();
        foreach (var key in baseNode.Select(p => p.Key).ToArray())
        {
            var node = (JsonObject)baseNode.DeepClone(); node.Remove(key); Compare(node.ToJsonString(), "missing:" + key); full++;
            node = (JsonObject)baseNode.DeepClone(); node[key] = null; Compare(node.ToJsonString(), "null:" + key); full++;
        }
        foreach (var suffix in new[] { ",\"unknown\":0", ",\"lx\":-0", ",\"command\":\"HELLO\"", ",\"msiReportChainSource\":null" })
        { Compare(frame[..^1] + suffix + "}", "duplicate-or-unknown"); full++; }
        foreach (var persona in new[] { "xbox360", "elite", "steamdeck", "dualsense", "dualsense-edge", "dualshock4" })
        {
            var node = JsonNode.Parse(Frame(optional: true, persona: persona))!.AsObject();
            node["imu"] = JsonNode.Parse("{\"admitted\":true,\"gyroDps\":{\"x\":-0.25,\"y\":0,\"z\":3},\"quaternion\":{\"w\":1,\"x\":0,\"y\":0,\"z\":0}}");
            node["physicalBaseContainerInstanceId"] = "机型 😀 \\"; node["screenButtons"] = 1;
            if (persona is "steamdeck" or "dualsense" or "dualsense-edge" or "dualshock4")
                node["touchpads"] = JsonNode.Parse("{\"left\":{\"active\":true,\"click\":false,\"x\":100,\"y\":200,\"pressure\":1},\"right\":{\"active\":false,\"click\":false,\"x\":0,\"y\":0,\"pressure\":0}}");
            Compare(node.ToJsonString(), "persona:" + persona); full++;
        }
        foreach (var text in new[] { "", " ", "null", "[]", "{", "{\"command\":0}", frame + " ", frame + "x", frame[..^1] + ",}", frame.Replace("\"lx\":0", "\"lx\":-0").Replace("\"protocolVersion\":1", "\"protocolVersion\":2"), frame.Replace("\"ltRaw\":0", "\"ltRaw\":-0").Replace(new string('a', 64), "BAD") })
        { Compare(text, "invalid-or-precedence"); full++; }
        // The returned envelope must remain fresh for every message, including its complete identity and input hash.
        P.CommandEnvelope? retained = null;
        for (var i = 0; i < 256; i++)
        {
            var message = Frame(identity: i, optional: true);
            Check(P.Candidate.Parse(message, out var e, out _) && e is not null && e.RequestId == "request-" + i && e.Sequence == (ulong)(i + 10) && e.SampleSequence == (ulong)(i + 1) && e.MonotonicTick == (ulong)(i + 2), "Per-frame identity " + i);
            if (i == 0) retained = e;
            else Check(!ReferenceEquals(retained, e) && retained!.Sequence == 10 && retained.RequestId == "request-0", "Envelope ownership " + i);
            Compare(message, "identity:" + i); full++;
        }
        var culture = CultureInfo.CurrentCulture;
        foreach (var name in new[] { "en-US", "zh-CN", "tr-TR", "ar-SA" })
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(name);
            Compare(Frame(optional: true), "culture:" + name); full++;
        }
        CultureInfo.CurrentCulture = culture;
        Parallel.For(0, 4, thread => { for (var i = 0; i < 128; i++) { var message = Frame(identity: thread * 128 + i, optional: true); if (Capture(false, message) != Capture(true, message)) throw new Exception("Thread-local parser mismatch"); } });
        Checks += 512;
        return new { distinctNumericTokens = tokens.Count, numberReaderComparisons = numberComparisons, fullParserComparisons = full + 512, concurrentThreads = 4, perFrameIdentityCases = 256, boundary = "Exact extracted parser/readers and metadata; no lifecycle, transport, SDK or device execution" };
    }
    private sealed record Scenario(string Name, string[] Messages, bool Stable, bool Accepted);
    private static Scenario[] Scenarios()
    {
        var nonzero = new[] { "0.125", "-0.25", "0.5", "-0.75", "0.25", "0.75", "0.125", "0.5" };
        string[] Make(bool optional, bool moving) => Enumerable.Range(0, 64).Select(i => Frame(moving ? nonzero : null, i, optional)).ToArray();
        var hello = JsonNode.Parse(Frame())!.AsObject(); foreach (var key in new[] { "buttons", "lx", "ly", "rx", "ry", "lt", "rt", "sampleSequence", "monotonicTick", "normalizedInputHash", "motionPairProofId" }) hello.Remove(key); hello["command"] = "HELLO";
        return [new("stable-six-zero", Make(false, false), true, true), new("stable-eight-zero-raw", Make(true, false), true, true), new("nonzero-frame-control", Make(true, true), false, true), new("hello-control", [hello.ToJsonString()], false, true), new("negative-zero-reject-control", [Frame().Replace("\"lx\":0", "\"lx\":-0")], false, false)];
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long Execute(Scenario scenario, bool candidate, int count)
    {
        long value = 0;
        for (var i = 0; i < count; i++)
        {
            var message = scenario.Messages[i % scenario.Messages.Length];
            var ok = candidate ? P.Candidate.Parse(message, out var e, out var error) : P.Baseline.Parse(message, out e, out error);
            if (ok != scenario.Accepted) throw new InvalidOperationException("Benchmark parsed unexpected result " + scenario.Name + ":" + error);
            value += (long)(e?.Sequence ?? 0) + error.Length + (ok ? 1 : 0);
        }
        return value;
    }
    private static double Median(IEnumerable<double> values) { var a = values.Order().ToArray(); return a[a.Length / 2]; }
    private static double Timed(Scenario scenario, bool candidate)
    {
        const int calls = 16384;
        var start = Stopwatch.GetTimestamp(); Sink += Execute(scenario, candidate, calls);
        return Stopwatch.GetElapsedTime(start).TotalNanoseconds / calls;
    }
    private static double Allocation(Scenario scenario, bool candidate)
    {
        const int calls = 4096; var start = GC.GetAllocatedBytesForCurrentThread();
        Sink += Execute(scenario, candidate, calls);
        return (GC.GetAllocatedBytesForCurrentThread() - start) / (double)calls;
    }
    private static (bool Pass, object Rows) Benchmark()
    {
        var rows = new List<object>(); var pass = true;
        var scenarios = Scenarios(); var warm = Stopwatch.StartNew();
        while (warm.ElapsedMilliseconds < 1500) foreach (var scenario in scenarios) { Sink += Execute(scenario, false, 64); Sink += Execute(scenario, true, 64); }
        foreach (var scenario in scenarios)
        {
            warm.Restart(); while (warm.ElapsedMilliseconds < 800) { Sink += Execute(scenario, false, 256); Sink += Execute(scenario, true, 256); }
            var before = new double[11]; var after = new double[11];
            for (var round = 0; round < before.Length; round++)
            {
                if (round % 2 == 0) { before[round] = Timed(scenario, false); after[round] = Timed(scenario, true); }
                else { after[round] = Timed(scenario, true); before[round] = Timed(scenario, false); }
            }
            var b = Allocation(scenario, false); var a = Allocation(scenario, true);
            var ratio = Median(after) / Median(before); var paired = Median(after.Zip(before, (x, y) => x / y)); var faster = after.Zip(before, (x, y) => x < y).Count(x => x);
            var gate = a <= b && (scenario.Stable ? a < b && ratio <= .97 && paired <= .97 && faster >= 9 : ratio <= 1.05 && paired <= 1.05);
            pass &= gate;
            rows.Add(new { name = scenario.Name, baselineNs = Median(before), candidateNs = Median(after), ratio, pairedRatio = paired, fasterPairs = faster, baselineBytes = b, candidateBytes = a, gate, baselineRounds = before, candidateRounds = after });
            Console.WriteLine($"{scenario.Name}: {Median(before):F1} -> {Median(after):F1} ns, ratio={ratio:F4}, paired={paired:F4}, faster={faster}/11, bytes={b}->{a}, gate={gate}");
        }
        return (pass, rows);
    }
    public static int Main(string[] args)
    {
        try
        {
            var semantics = Semantics(); var bench = args[1] == "benchmark" ? Benchmark() : (true, (object)Array.Empty<object>());
            var result = new { pass = true, checks = Checks, semantics, performancePass = bench.Item1, performance = bench.Item2, runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription, architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(), tieredCompilation = Environment.GetEnvironmentVariable("DOTNET_TieredCompilation"), tieredPGO = Environment.GetEnvironmentVariable("DOTNET_TieredPGO"), sink = Sink };
            File.WriteAllText(args[0], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
