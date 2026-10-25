using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class FingerprintFaultInjection
{
    [ThreadStatic] internal static bool ThrowNext;
    [ThreadStatic] internal static int Calls;
    internal static int HashData(ReadOnlySpan<byte> source, Span<byte> destination)
    {
        ++Calls;
        if (ThrowNext) { ThrowNext = false; throw new InvalidOperationException("injected-SHA-failure"); }
        return SHA256.HashData(source, destination);
    }
}

internal static class SelfTest
{
    private delegate string Hasher(int buttons, float lx, float ly, float rx, float ry, float lt, float rt);
    private readonly record struct Frame(int Buttons, float Lx, float Ly, float Rx, float Ry, float Lt, float Rt);
    private static int assertions;
    private static long sink;
    private static void Check(bool condition, string message)
    {
        ++assertions;
        if (!condition) throw new InvalidOperationException(message);
    }
    private static string Hash(Hasher hasher, Frame f) => hasher(f.Buttons, f.Lx, f.Ly, f.Rx, f.Ry, f.Lt, f.Rt);
    private static float Bits(uint bits) => BitConverter.Int32BitsToSingle(unchecked((int)bits));
    private static Frame RandomFrame(Random random)
    {
        Span<byte> bytes = stackalloc byte[28]; random.NextBytes(bytes);
        return new(BitConverter.ToInt32(bytes), BitConverter.ToSingle(bytes[4..]), BitConverter.ToSingle(bytes[8..]),
            BitConverter.ToSingle(bytes[12..]), BitConverter.ToSingle(bytes[16..]), BitConverter.ToSingle(bytes[20..]), BitConverter.ToSingle(bytes[24..]));
    }
    private static string Reference(Frame f)
    {
        var values = new[] { f.Lx, f.Ly, f.Rx, f.Ry, f.Lt, f.Rt };
        var text = f.Buttons.ToString(CultureInfo.CurrentCulture);
        foreach (var x in values) text += "|" + unchecked((uint)BitConverter.SingleToInt32Bits(x)).ToString("X8", CultureInfo.InvariantCulture);
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }
    private static void Equal(Frame f, bool reference = false)
    {
        var old = Hash(LegacyCanonical.Compute, f); var next = Hash(MemoizedCanonical.Compute, f);
        Check(old == next, "before/new bit-exact mismatch");
        Check(ReferenceEquals(next, Hash(MemoizedCanonical.Compute, f)), "identical canonical key did not reuse successful result");
        if (reference) Check(old == Reference(f), "before formatter differs from exact UTF8 reference");
    }
    private static object RunCorrectness()
    {
        var original = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
            var random = new Random(0x5eeda);
            for (var i = 0; i < 100_000; ++i) Equal(RandomFrame(random), i < 128);
            var specials = new uint[] { 0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000, 0x80800000,
                0x3f800000, 0xbf800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0x7fa00001,
                0xffc00001, 0x7fffffff, 0xffffffff };
            foreach (var bits in specials)
                foreach (var buttons in new[] { 0, 1, -1, int.MinValue, int.MaxValue })
                    for (var slot = 0; slot < 6; ++slot)
                    {
                        var axes = new float[6]; axes[slot] = Bits(bits);
                        Equal(new(buttons, axes[0], axes[1], axes[2], axes[3], axes[4], axes[5]), true);
                    }
            Check(Hash(MemoizedCanonical.Compute, new(0, Bits(0), 0, 0, 0, 0, 0)) !=
                Hash(MemoizedCanonical.Compute, new(0, Bits(0x80000000), 0, 0, 0, 0, 0)), "signed zero collapsed");
            var cultures = new List<CultureInfo> { CultureInfo.InvariantCulture, new("en-US"), new("fr-FR"), new("ar-SA"), new("fa-IR"), new("zh-CN") };
            foreach (var sign in new[] { "−", "负", "NEGATIVE", "!" })
            { var c = (CultureInfo)CultureInfo.InvariantCulture.Clone(); c.NumberFormat.NegativeSign = sign; cultures.Add(c); }
            foreach (var c in cultures)
            {
                CultureInfo.CurrentCulture = c;
                for (var i = 0; i < 1000; ++i) Equal(RandomFrame(random), i < 20);
                Equal(new(int.MinValue, Bits(0x80000000), Bits(1), Bits(0x7fc00001), Bits(0xff800000), 1, -1), true);
            }
            var negative = new Frame(-7, 0, 1, -1, Bits(1), Bits(0x80000000), Bits(0x7fc00003));
            CultureInfo.CurrentCulture = CultureInfo.InvariantCulture; var invariantHash = Hash(MemoizedCanonical.Compute, negative);
            CultureInfo.CurrentCulture = cultures[^2]; var otherHash = Hash(MemoizedCanonical.Compute, negative);
            Check(invariantHash != otherHash, "CurrentCulture buttons UTF8 bytes omitted from cache key");
            CultureInfo.CurrentCulture = CultureInfo.InvariantCulture; Check(Hash(MemoizedCanonical.Compute, negative) == invariantHash, "culture round-trip changed fingerprint");
            var a = Hash(MemoizedCanonical.Compute, negative); var b = negative with { Rt = Bits(1) };
            Hash(MemoizedCanonical.Compute, b); Check(!ReferenceEquals(a, Hash(MemoizedCanonical.Compute, negative)), "cache kept multiple entries");
            var accepted = Hash(MemoizedCanonical.Compute, negative);
            for (var i = 0; i < accepted.Length; ++i)
            {
                var chars = accepted.ToCharArray(); chars[i] = chars[i] == '0' ? '1' : '0'; var tampered = new string(chars);
                Check(tampered.ToLowerInvariant() != Hash(MemoizedCanonical.Compute, negative), "tampered normalized hash accepted");
                Check((tampered.ToLowerInvariant() != Hash(LegacyCanonical.Compute, negative)) ==
                    (tampered.ToLowerInvariant() != Hash(MemoizedCanonical.Compute, negative)), "old/new hash rejection changed");
            }
            Check(accepted.ToUpperInvariant().ToLowerInvariant() == Hash(MemoizedCanonical.Compute, negative), "existing lowercase normalization changed");
            var raw = Encoding.UTF8.GetBytes("-17|80000000|00000001|7FC00001|7F800000|FF800000|3F800000");
            var expectedRaw = Convert.ToHexStringLower(SHA256.HashData(raw));
            Check(CanonicalFrameFingerprintMemo.GetOrCompute(raw) == expectedRaw, "raw key mismatch");
            var remembered = CanonicalFrameFingerprintMemo.GetOrCompute(raw);
            raw[0] = (byte)'9'; Check(CanonicalFrameFingerprintMemo.GetOrCompute(raw) == Convert.ToHexStringLower(SHA256.HashData(raw)), "input buffer mutation used stale key");
            raw[0] = (byte)'-'; Check(CanonicalFrameFingerprintMemo.GetOrCompute(raw) == expectedRaw, "cached bytes alias caller storage");
            for (var i = 0; i < raw.Length; ++i)
            {
                raw[i] ^= 1; Check(CanonicalFrameFingerprintMemo.GetOrCompute(raw) == Convert.ToHexStringLower(SHA256.HashData(raw)), "single changed raw byte omitted"); raw[i] ^= 1;
            }
            var larger = new byte[257]; random.NextBytes(larger);
            Check(CanonicalFrameFingerprintMemo.GetOrCompute(larger) == Convert.ToHexStringLower(SHA256.HashData(larger)), "oversized key changed SHA");
            var hugeSign = (CultureInfo)CultureInfo.InvariantCulture.Clone(); hugeSign.NumberFormat.NegativeSign = new string('x', 300); CultureInfo.CurrentCulture = hugeSign;
            var failures = new List<string>(); foreach (Hasher hasher in new Hasher[] { LegacyCanonical.Compute, MemoizedCanonical.Compute })
            { try { Hash(hasher, negative); failures.Add("no-exception"); } catch (InvalidOperationException e) { failures.Add(e.Message); } }
            Check(failures[0] == "canonical-frame-buttons-format-failed" && failures[0] == failures[1], "original format rejection changed");
            CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
            byte[] faultA = Encoding.UTF8.GetBytes("a"), faultB = Encoding.UTF8.GetBytes("b");
            var cached = CanonicalFrameFingerprintMemoFaultFixture.GetOrCompute(faultA); var calls = FingerprintFaultInjection.Calls;
            FingerprintFaultInjection.ThrowNext = true; var failed = false;
            try { CanonicalFrameFingerprintMemoFaultFixture.GetOrCompute(faultB); } catch (InvalidOperationException e) when (e.Message == "injected-SHA-failure") { failed = true; }
            Check(failed, "injected SHA did not fail"); Check(ReferenceEquals(cached, CanonicalFrameFingerprintMemoFaultFixture.GetOrCompute(faultA)), "failed SHA replaced previous successful entry");
            Check(FingerprintFaultInjection.Calls == calls + 1, "old successful key recalculated after failure");
            Check(CanonicalFrameFingerprintMemoFaultFixture.GetOrCompute(faultB) == Convert.ToHexStringLower(SHA256.HashData(faultB)), "failed key was cached as success");
            var errors = new ConcurrentQueue<string>(); var threadResults = new string[8]; var threads = new Thread[8];
            for (var i = 0; i < threads.Length; ++i)
            {
                var index = i; threads[i] = new Thread(() =>
                {
                    try
                    {
                        CultureInfo.CurrentCulture = cultures[index % cultures.Count]; var rng = new Random(8123 + index);
                        for (var j = 0; j < 5000; ++j) { var f = RandomFrame(rng); if (Hash(LegacyCanonical.Compute, f) != Hash(MemoizedCanonical.Compute, f)) throw new Exception("thread hash mismatch"); }
                        threadResults[index] = Hash(MemoizedCanonical.Compute, new(99, 0, 0, 0, 0, 0, 0));
                    }
                    catch (Exception e) { errors.Enqueue(e.ToString()); }
                }); threads[i].Start();
            }
            foreach (var t in threads) t.Join(); Check(errors.IsEmpty, string.Join("\n", errors));
            for (var i = 0; i < threads.Length; ++i) for (var j = i + 1; j < threads.Length; ++j)
            { Check(threadResults[i] == threadResults[j], "thread-local formatting result differs"); Check(!ReferenceEquals(threadResults[i], threadResults[j]), "cache shared across OS threads"); }
            return new { passed = true, assertions, randomFrames = 110_000, threadedFrames = 40_000, hardwareAccess = false, productRun = false,
                faultFixture = "same helper source; only class name and SHA call target replaced" };
        }
        finally { CultureInfo.CurrentCulture = original; }
    }
    private static (double Ns, double Bytes) Measure(Hasher hasher, Frame[] frames, int count)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread(); var start = Stopwatch.GetTimestamp(); long sum = 0;
        for (var i = 0; i < count; ++i) { var s = Hash(hasher, frames[i % frames.Length]); sum += s[0]; }
        var elapsed = Stopwatch.GetTimestamp() - start; sink = sum;
        return (elapsed * 1_000_000_000.0 / Stopwatch.Frequency / count, (GC.GetAllocatedBytesForCurrentThread() - allocated) / (double)count);
    }
    private static double Median(IEnumerable<double> values) { var a = values.Order().ToArray(); return a[a.Length / 2]; }
    private static object RunBenchmarks()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var rng = new Random(71231); var moving = Enumerable.Range(0, 1024).Select(_ => RandomFrame(rng)).ToArray();
        var still = new Frame(17, .123f, -.512f, .33f, -.01f, .2f, .8f);
        var tail = Enumerable.Range(1, 1024).Select(i => still with { Rt = Bits((uint)i) }).ToArray();
        var mixed = Enumerable.Range(0, 1024).SelectMany(i => new[] { moving[i], moving[i] }).ToArray();
        var cases = new (string Name, Frame[] Frames, bool Dynamic)[] {
            ("neutral-warm", new[] { new Frame(0, 0, 0, 0, 0, 0, 0) }, false), ("held-warm", new[] { still }, false),
            ("half-identical", mixed, false), ("all-change-random-bits", moving, true), ("all-change-last-word", tail, true),
            ("signed-zero-alternating", new[] { still with { Lx = Bits(0) }, still with { Lx = Bits(0x80000000) } }, true) };
        var output = new List<object>(); bool gate = true;
        foreach (var c in cases)
        {
            Measure(LegacyCanonical.Compute, c.Frames, 30_000); Measure(MemoizedCanonical.Compute, c.Frames, 30_000);
            var old = new List<double>(); var next = new List<double>(); var oldBytes = new List<double>(); var nextBytes = new List<double>();
            for (var sample = 0; sample < 9; ++sample)
            {
                (double Ns, double Bytes) a, b;
                if ((sample & 1) == 0) { a = Measure(LegacyCanonical.Compute, c.Frames, 100_000); b = Measure(MemoizedCanonical.Compute, c.Frames, 100_000); }
                else { b = Measure(MemoizedCanonical.Compute, c.Frames, 100_000); a = Measure(LegacyCanonical.Compute, c.Frames, 100_000); }
                old.Add(a.Ns); next.Add(b.Ns); oldBytes.Add(a.Bytes); nextBytes.Add(b.Bytes);
            }
            var oldMedian = Median(old); var newMedian = Median(next); var ratio = newMedian / oldMedian;
            if (c.Dynamic && ratio > 1.10) gate = false;
            output.Add(new { name = c.Name, dynamic = c.Dynamic, oldMedianNs = oldMedian, newMedianNs = newMedian, ratio,
                oldBytesPerCall = Median(oldBytes), newBytesPerCall = Median(nextBytes), oldSamplesNs = old, newSamplesNs = next,
                cpuMillisecondsSavedPerSecondAt125Hz = (oldMedian - newMedian) * 125 / 1_000_000,
                cpuMillisecondsSavedPerSecondAt250Hz = (oldMedian - newMedian) * 250 / 1_000_000 });
        }
        // First call is intentionally not a steady-state hit: each fresh thread
        // pays its single cache allocation. Thread startup is outside timing.
        var cold = new List<object>();
        for (var i = 0; i < 12; ++i)
        {
            double oldNs = 0, newNs = 0; long oldBytes = 0, newBytes = 0;
            var t = new Thread(() => {
                var f = still with { Buttons = 100_000 + i }; var a = Measure(LegacyCanonical.Compute, new[] { f }, 1); var b = Measure(MemoizedCanonical.Compute, new[] { f }, 1);
                oldNs = a.Ns; newNs = b.Ns; oldBytes = (long)a.Bytes; newBytes = (long)b.Bytes;
            }); t.Start(); t.Join(); cold.Add(new { oldNs, newNs, oldBytes, newBytes });
        }
        return new { dynamicWorstMedianRatioLimit = 1.10, dynamicGatePassed = gate, cases = output, coldThreadFirstCalls = cold,
            scheduling = "unpaced offline calls; 125/250 figures are arithmetic projections, not CPU field measurements", sink };
    }
    internal static int Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("explicit evidence output directory required");
        var correctness = RunCorrectness(); Console.WriteLine($"CANONICAL_FINGERPRINT_SELFTEST passed=true assertions={assertions} hardwareAccess=false productRun=false");
        var bench = RunBenchmarks(); var json = JsonSerializer.Serialize(new { schema = 1, recordedAt = DateTimeOffset.UtcNow, runtime = Environment.Version.ToString(), correctness, benchmarks = bench }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(args[0], "results.json"), json);
        Console.WriteLine(json); return 0;
    }
}
