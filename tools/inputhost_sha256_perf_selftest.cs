// Compiled only with the two verbatim extracted helpers. No Host/SDK references.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;

internal static class Sha256Fixture
{
    private static readonly Func<string, bool> Before = BeforeSha256.Bind();
    private static readonly Func<string, bool> Current = CurrentSha256.Bind();
    private static long checks;
    private static int sink;
    private sealed record Scenario(string Name, string[] Values, string[] Configs = null, bool Throws = false);
    private sealed record Sample(string Variant, int Round, int Order, int Iterations, double NsPerOperation,
        long AllocatedBytes, double BytesPerOperation, int Gen0, int Gen1, int Gen2, int Checksum);

    private static void Verify(string value, bool expected, string group)
    {
        var before = Before(value);
        var current = Current(value);
        if (before != current || before != expected)
            throw new Exception($"Parity failure: {group}, length={value.Length}, before={before}, current={current}, expected={expected}");
        checks++;
    }
    private static string ExceptionFingerprint(Func<string, bool> method)
    {
        try { method(null); throw new Exception("Null was accepted"); }
        catch (NullReferenceException ex) { return $"{ex.GetType().FullName}|{ex.HResult}|{ex.Message}|{ex.Data.Count}"; }
    }
    private static object Semantics()
    {
        var chars = Enumerable.Repeat('a', 64).ToArray();
        var accepted = new List<int>();
        for (int c = 0; c <= char.MaxValue; c++) if (Uri.IsHexDigit((char)c)) accepted.Add(c);
        if (accepted.Count != 22) throw new Exception("Re-audit changed runtime Uri.IsHexDigit alphabet");
        // Every UTF-16 code unit in every position, including isolated surrogates.
        for (int pos = 0; pos < 64; pos++)
        {
            for (int c = 0; c <= char.MaxValue; c++)
            {
                chars[pos] = (char)c;
                Verify(new string(chars), Uri.IsHexDigit((char)c), "exhaustive UTF-16");
            }
            chars[pos] = 'a';
        }
        long bmpChecks = checks;
        // Every supplementary Unicode scalar, represented by its actual two code units.
        foreach (int pos in new[] { 0, 31, 62 })
        {
            for (int scalar = 0x10000; scalar <= 0x10ffff; scalar++)
            {
                int offset = scalar - 0x10000;
                chars[pos] = (char)(0xd800 + (offset >> 10));
                chars[pos + 1] = (char)(0xdc00 + (offset & 1023));
                Verify(new string(chars), false, "exhaustive supplementary scalars");
            }
            chars[pos] = chars[pos + 1] = 'a';
        }
        long scalarChecks = checks - bmpChecks;
        for (int length = 0; length <= 257; length++)
        {
            Verify(new string('F', length), length == 64, "length");
            Verify(new string('\uff26', length), false, "non-ASCII length");
        }
        Verify(new string('a', 16384), false, "long length");
        Verify(new string('\ud800', 16384), false, "long surrogate length");
        // Multiple invalid units exercise all first-reject positions and invalid suffixes.
        for (int first = 0; first < 64; first++)
            for (int second = first; second < 64; second++)
            {
                Array.Fill(chars, 'a');
                chars[first] = '\uff10'; chars[second] = '?';
                Verify(new string(chars), false, "multiple invalid positions");
            }
        var cultures = new[] { "", "en-US", "tr-TR", "ar-SA", "zh-CN" };
        var originalCulture = CultureInfo.CurrentCulture;
        var originalUiCulture = CultureInfo.CurrentUICulture;
        string nullFingerprint = null;
        var random = new Random(0x1410);
        try
        {
            foreach (var name in cultures)
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(name);
                CultureInfo.CurrentUICulture = CultureInfo.GetCultureInfo(name);
                var beforeNull = ExceptionFingerprint(Before);
                var currentNull = ExceptionFingerprint(Current);
                if (beforeNull != currentNull) throw new Exception("Null exception fingerprint changed");
                nullFingerprint = beforeNull;
                checks++;
                for (int n = 0; n < 20000; n++)
                {
                    int length = n % 3 == 0 ? random.Next(130) : 64;
                    var data = new char[length];
                    const string alphabet = "0123456789abcdefABCDEF";
                    for (int i = 0; i < length; i++) data[i] = alphabet[random.Next(alphabet.Length)];
                    if ((n & 1) != 0 && length != 0) data[random.Next(length)] = (char)random.Next(65536);
                    bool expected = length == 64;
                    foreach (char c in data) expected &= Uri.IsHexDigit(c);
                    Verify(new string(data), expected, "seeded cultures");
                }
            }
        }
        finally { CultureInfo.CurrentCulture = originalCulture; CultureInfo.CurrentUICulture = originalUiCulture; }
        // The fixture models just the two validation calls, not the production parser.
        foreach (var method in new[] { Before, Current })
        {
            if (method(new string('?', 64)) && method(null)) throw new Exception("Rejected config continued");
            if (method(new string('a', 63)) && method(null)) throw new Exception("Length reject continued");
            if (!(method(NativeHash.Config) && method(new string('A', 64)))) throw new Exception("Two hashes rejected");
            if (method(NativeHash.Config) && method(new string('a', 63))) throw new Exception("Invalid frame hash accepted");
            checks += 4;
        }
        return new { Checks = checks, Utf16CodeUnitPositions = bmpChecks, SupplementaryScalarPositions = scalarChecks,
            RandomCultureCases = 100000, Cultures = cultures, AcceptedCodeUnits = accepted, NullException = nullFingerprint,
            RejectionOrderEvidence = "Length read precedes scan; current loop scans forward and returns on the first false Uri.IsHexDigit. Parser call order is preserved outside the function; paired fixture gates exercise base-before-frame short circuit." };
    }
    private static List<Scenario> Corpus()
    {
        const int count = 4096;
        var random = new Random(0x2561410);
        var lower = new string[count]; var upper = new string[count]; var mixed = new string[count];
        var first = new string[count]; var middle = new string[count]; var last = new string[count];
        var varying = new string[count]; var shortValues = new string[count]; var longValues = new string[count];
        var unicode = new string[count]; var fixedConfig = new string[count]; var rejectedConfig = new string[count];
        var nullValues = new string[count]; var empty = new string[count];
        const string hex = "0123456789abcdef";
        for (int n = 0; n < count; n++)
        {
            var chars = new char[64];
            for (int i = 0; i < chars.Length; i++) chars[i] = hex[random.Next(16)];
            lower[n] = new string(chars); upper[n] = lower[n].ToUpperInvariant();
            for (int i = 0; i < chars.Length; i++) if (random.Next(2) != 0) chars[i] = char.ToUpperInvariant(chars[i]);
            mixed[n] = new string(chars);
            string Invalid(int position, char bad) { var copy = (char[])chars.Clone(); copy[position] = bad; return new string(copy); }
            first[n] = Invalid(0, '?'); middle[n] = Invalid(32, '?'); last[n] = Invalid(63, '?');
            varying[n] = Invalid(n & 63, (n & 1) == 0 ? '?' : '\uff10');
            unicode[n] = Invalid(n & 63, (char)(0xd800 + (n & 2047)));
            shortValues[n] = mixed[n].Substring(0, 63); longValues[n] = mixed[n] + "A";
            fixedConfig[n] = NativeHash.Config; rejectedConfig[n] = varying[n]; empty[n] = "";
        }
        return new List<Scenario> {
            new("dynamic-valid-lower", lower), new("dynamic-valid-upper", upper), new("dynamic-valid-mixed", mixed),
            new("dynamic-invalid-first", first), new("dynamic-invalid-middle", middle), new("dynamic-invalid-last", last),
            new("dynamic-invalid-varying", varying), new("dynamic-invalid-surrogates", unicode),
            new("length-63", shortValues), new("length-65", longValues), new("length-empty", empty),
            new("frame-two-valid-hashes", upper, fixedConfig), new("frame-base-reject-skip-null", nullValues, rejectedConfig),
            new("frame-normalized-hash-reject", varying, fixedConfig), new("null-exception", nullValues, Throws: true)
        };
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Execute(Func<string, bool> method, Scenario scenario, int iterations)
    {
        int sum = 0, mask = scenario.Values.Length - 1;
        if (scenario.Throws)
        {
            for (int i = 0; i < iterations; i++)
                try { method(scenario.Values[i & mask]); throw new Exception("Null did not throw"); }
                catch (NullReferenceException) { sum++; }
        }
        else if (scenario.Configs != null)
        {
            for (int i = 0; i < iterations; i++)
                if (method(scenario.Configs[i & mask]) && method(scenario.Values[i & mask])) sum++;
        }
        else
        {
            for (int i = 0; i < iterations; i++) if (method(scenario.Values[i & mask])) sum++;
        }
        Volatile.Write(ref sink, sum);
        return sum;
    }
    private static Sample Measure(Func<string, bool> method, Scenario scenario, string variant, int round, int order, int iterations)
    {
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        int gen0 = GC.CollectionCount(0), gen1 = GC.CollectionCount(1), gen2 = GC.CollectionCount(2);
        long bytes = GC.GetAllocatedBytesForCurrentThread();
        long started = Stopwatch.GetTimestamp();
        int checksum = Execute(method, scenario, iterations);
        long elapsed = Stopwatch.GetTimestamp() - started;
        long allocated = GC.GetAllocatedBytesForCurrentThread() - bytes;
        return new Sample(variant, round, order, iterations, elapsed * 1e9 / Stopwatch.Frequency / iterations,
            allocated, (double)allocated / iterations, GC.CollectionCount(0) - gen0, GC.CollectionCount(1) - gen1,
            GC.CollectionCount(2) - gen2, checksum);
    }
    private static double Median(IEnumerable<double> values)
    {
        var sorted = values.OrderBy(v => v).ToArray();
        return sorted.Length % 2 == 1 ? sorted[sorted.Length / 2] : (sorted[sorted.Length / 2 - 1] + sorted[sorted.Length / 2]) / 2;
    }
    public static int Main(string[] args)
    {
        try
        {
            int iterations = int.Parse(args[1], CultureInfo.InvariantCulture), rounds = int.Parse(args[2], CultureInfo.InvariantCulture);
            if (iterations < 10000 || rounds < 3) throw new Exception("Use at least 10000 iterations and 3 rounds");
            var parity = Semantics();
            var scenarios = Corpus();
            // All allocations for corpus/delegates/first-use method-group caches precede measurement.
            foreach (var scenario in scenarios)
                foreach (var method in new[] { Before, Current }) Execute(method, scenario, scenario.Throws ? 128 : 50000);
            var results = new List<object>();
            foreach (var scenario in scenarios)
            {
                int count = scenario.Throws ? Math.Min(iterations, 4096) : iterations;
                var samples = new List<Sample>();
                for (int round = 0; round < rounds; round++)
                {
                    bool currentFirst = (round & 1) != 0;
                    samples.Add(Measure(currentFirst ? Current : Before, scenario, currentFirst ? "current" : "before", round, 0, count));
                    samples.Add(Measure(currentFirst ? Before : Current, scenario, currentFirst ? "before" : "current", round, 1, count));
                }
                var before = samples.Where(s => s.Variant == "before").ToArray();
                var current = samples.Where(s => s.Variant == "current").ToArray();
                if (before.Zip(current).Any(pair => pair.First.Checksum != pair.Second.Checksum)) throw new Exception("Benchmark checksum mismatch");
                double beforeNs = Median(before.Select(s => s.NsPerOperation)), currentNs = Median(current.Select(s => s.NsPerOperation));
                double beforeBytes = Median(before.Select(s => s.BytesPerOperation)), currentBytes = Median(current.Select(s => s.BytesPerOperation));
                results.Add(new { scenario.Name, OperationsPerWindow = count, CorpusSize = scenario.Values.Length,
                    MedianBeforeNs = beforeNs, MedianCurrentNs = currentNs, MedianBeforeBytes = beforeBytes,
                    MedianCurrentBytes = currentBytes, MedianSpeedup = beforeNs / currentNs,
                    PairedWins = before.Zip(current).Count(pair => pair.First.NsPerOperation > pair.Second.NsPerOperation), Samples = samples });
                Console.WriteLine(FormattableString.Invariant($"{scenario.Name}: before={beforeNs:F2} ns/{beforeBytes:F2} B current={currentNs:F2} ns/{currentBytes:F2} B speedup={beforeNs / currentNs:F2}x"));
            }
            var evidence = new { Passed = true, Parity = parity, Benchmark = new {
                Runtime = RuntimeInformation.FrameworkDescription, OS = RuntimeInformation.OSDescription,
                Architecture = RuntimeInformation.ProcessArchitecture.ToString(), Environment.ProcessorCount,
                Stopwatch.Frequency, Iterations = iterations, Rounds = rounds, Seed = "0x2561410",
                TieredCompilation = Environment.GetEnvironmentVariable("DOTNET_TieredCompilation"),
                TieredPGO = Environment.GetEnvironmentVariable("DOTNET_TieredPGO"),
                ReadyToRun = Environment.GetEnvironmentVariable("DOTNET_ReadyToRun"),
                Invocation = "Cached strongly typed delegates bound directly to the extracted private methods; no per-call reflection.",
                Allocation = "Current-thread managed bytes measured around the same checksum-protected timed loop; corpus, delegates, first-use caches and GC preparation excluded.",
                Scope = "Pure helper microbenchmark. Frame cases model only the two hash validations; no parser, SDK, Host Main, devices or lifecycle execute. These are not end-to-end CPU numbers.",
                Scenarios = results } };
            System.IO.File.WriteAllText(args[0], JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine($"PASS parity={checks}; raw alternating windows saved");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
