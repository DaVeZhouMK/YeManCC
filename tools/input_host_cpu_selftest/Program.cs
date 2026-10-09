using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

// Loads exact before/after product assemblies. No Host Main, controller factory,
// hardware API, driver install or normal YMCC startup is invoked by this runner.
internal static class CpuSelftest
{
    private delegate string FrameHash(int buttons, float lx, float ly, float rx, float ry, float lt, float rt);
    private static int _checks;
    private static int _sink;
    private static readonly BindingFlags Methods = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
    private sealed class ProductContext(string path) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver _resolver = new(path);
        protected override Assembly? Load(AssemblyName name)
        {
            var resolved = _resolver.ResolveAssemblyToPath(name);
            return resolved is null ? null : LoadFromAssemblyPath(resolved);
        }
    }
    private readonly record struct Frame(int Buttons, float Lx, float Ly, float Rx, float Ry, float Lt, float Rt);
    private static void Check(bool ok, string name)
    {
        if (!ok) throw new InvalidOperationException("FAIL: " + name);
        _checks++;
    }
    private static string Hash(FrameHash fn, Frame f) => fn(f.Buttons, f.Lx, f.Ly, f.Rx, f.Ry, f.Lt, f.Rt);
    private static string FileHash(string path) => Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));
    private static double Median(IEnumerable<double> data) { var v = data.Order().ToArray(); return v[v.Length / 2]; }
    private static Process Child(int milliseconds)
    {
        var path = Environment.ProcessPath ?? throw new InvalidOperationException("missing runner process path");
        var start = new ProcessStartInfo(path) { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden };
        if (Path.GetFileNameWithoutExtension(path).Equals("dotnet", StringComparison.OrdinalIgnoreCase)) start.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
        start.ArgumentList.Add("--child-wait"); start.ArgumentList.Add(milliseconds.ToString(CultureInfo.InvariantCulture));
        return Process.Start(start) ?? throw new InvalidOperationException("own probe child could not start");
    }
    private static object? Watch(MethodInfo open, int pid) => open.Invoke(null, [pid]);
    private static Func<bool> Alive(Type type, object watch) => (Func<bool>)type.GetProperty("IsAlive", Methods)!.GetMethod!.CreateDelegate(typeof(Func<bool>), watch);
    private static object Measure(string kind, string label, int repeat, int count, Action loop)
    {
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        using var process = Process.GetCurrentProcess();
        var cpuBefore = process.TotalProcessorTime.TotalMilliseconds;
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var sw = Stopwatch.StartNew(); loop(); sw.Stop();
        var bytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
        var cpuMs = process.TotalProcessorTime.TotalMilliseconds - cpuBefore;
        return new { kind, label, repeat, count, elapsedMs = sw.Elapsed.TotalMilliseconds, cpuMs, bytes, bytesPerCall = bytes / (double)count, microsecondsPerCall = sw.Elapsed.TotalMicroseconds / count };
    }
    private static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--child-wait") { Thread.Sleep(int.Parse(args[1], CultureInfo.InvariantCulture)); return 0; }
        if (args.Length != 3) throw new ArgumentException("usage: runner before.dll after.dll output.json");
        var beforePath = Path.GetFullPath(args[0]); var afterPath = Path.GetFullPath(args[1]); var output = Path.GetFullPath(args[2]);
        var beforeContext = new ProductContext(beforePath); var afterContext = new ProductContext(afterPath);
        var beforeAssembly = beforeContext.LoadFromAssemblyPath(beforePath); var afterAssembly = afterContext.LoadFromAssemblyPath(afterPath);
        var beforeProgram = beforeAssembly.GetType("Program")!; var afterProgram = afterAssembly.GetType("Program")!;
        var before = beforeProgram.GetMethod("CanonicalFrameHash", Methods)!.CreateDelegate<FrameHash>();
        var after = afterProgram.GetMethod("CanonicalFrameHash", Methods)!.CreateDelegate<FrameHash>();
        var legacyParent = beforeProgram.GetMethod("IsParentAlive", Methods)!.CreateDelegate<Func<int, bool>>();
        var parentType = afterAssembly.GetType("ParentProcessLifetime")!; var open = parentType.GetMethod("TryOpen", Methods)!;
        var random = new Random(20261003);
        float RandomFloat() => BitConverter.Int32BitsToSingle((int)random.NextInt64(int.MinValue, (long)int.MaxValue + 1));
        var frames = Enumerable.Range(0, 10000).Select(_ => new Frame(random.Next(65536), RandomFloat(), RandomFloat(), RandomFloat(), RandomFloat(), RandomFloat(), RandomFloat())).ToArray();
        var unusual = (CultureInfo)CultureInfo.InvariantCulture.Clone(); unusual.NumberFormat.NegativeSign = "\u2212";
        var cultures = new[] { CultureInfo.InvariantCulture, CultureInfo.GetCultureInfo("zh-CN"), CultureInfo.GetCultureInfo("en-US"), CultureInfo.GetCultureInfo("fr-FR"), CultureInfo.GetCultureInfo("ar-SA"), CultureInfo.GetCultureInfo("tr-TR"), unusual };
        var savedCulture = CultureInfo.CurrentCulture;
        var corners = new[] { 0f, BitConverter.Int32BitsToSingle(unchecked((int)0x80000000)), float.Epsilon, -float.Epsilon, float.MaxValue, float.MinValue, 1f, -1f, float.PositiveInfinity, float.NegativeInfinity, float.NaN, BitConverter.Int32BitsToSingle(0x7fc01234) };
        foreach (var culture in cultures)
        {
            CultureInfo.CurrentCulture = culture;
            foreach (var f in frames) Check(Hash(before, f) == Hash(after, f), "bitwise hash parity");
            foreach (var button in new[] { 0, 1, 65535, -1, int.MinValue, int.MaxValue })
                foreach (var value in corners) { var f = new Frame(button, value, value, value, value, value, value); Check(Hash(before, f) == Hash(after, f), "hash edge/culture parity"); }
        }
        CultureInfo.CurrentCulture = savedCulture;
        var positiveZero = after(0, 0, 0, 0, 0, 0, 0); var negativeZero = after(0, BitConverter.Int32BitsToSingle(unchecked((int)0x80000000)), 0, 0, 0, 0, 0);
        Check(positiveZero != negativeZero, "signed zero is not normalized");
        Check(positiveZero.Length == 64 && positiveZero == positiveZero.ToLowerInvariant(), "wire hash remains lowercase SHA-256");
        Check(Watch(open, 0) is null && Watch(open, -1) is null && Watch(open, int.MaxValue) is null, "invalid/missing parent is fail-closed");
        using (var watch = (IDisposable)Watch(open, Environment.ProcessId)!)
        {
            var alive = Alive(parentType, watch); Check(alive(), "current parent alive");
            watch.Dispose(); Check(!alive(), "disposed handle is terminal"); watch.Dispose(); Check(!alive(), "double disposal is safe");
        }
        using (var child = Child(300))
        {
            using var watch = (IDisposable)(Watch(open, child.Id) ?? throw new InvalidOperationException("own-child handle missing"));
            var alive = Alive(parentType, watch); Check(alive(), "original child parent alive");
            Check(child.WaitForExit(10000), "own probe child exited within budget");
            Check(!alive(), "original parent exit is terminal");
            using var unrelated = Child(200);
            Check(!alive(), "new process cannot revive the retained original handle");
            Check(unrelated.WaitForExit(10000), "second own probe child exited");
        }
        // Warm both real methods before alternating measured windows.
        using var ownWatch = (IDisposable)(Watch(open, Environment.ProcessId) ?? throw new InvalidOperationException("own watch missing"));
        var cachedAlive = Alive(parentType, ownWatch);
        foreach (var f in frames) { _sink ^= Hash(before, f).Length; _sink ^= Hash(after, f).Length; }
        for (var i = 0; i < 1000; i++) { _sink ^= legacyParent(Environment.ProcessId) ? 1 : 0; _sink ^= cachedAlive() ? 1 : 0; }
        var measurements = new List<object>(); const int hashCount = 200000, parentCount = 20000;
        for (var repeat = 0; repeat < 5; repeat++)
        {
            foreach (var label in repeat % 2 == 0 ? new[] { "before", "after" } : new[] { "after", "before" })
            {
                var fn = label == "before" ? before : after;
                measurements.Add(Measure("frame-hash", label, repeat + 1, hashCount, () => { for (var i = 0; i < hashCount; i++) _sink ^= Hash(fn, frames[i % frames.Length]).Length; }));
                measurements.Add(Measure("parent-lifetime", label, repeat + 1, parentCount, () => { for (var i = 0; i < parentCount; i++) _sink ^= (label == "before" ? legacyParent(Environment.ProcessId) : cachedAlive()) ? 1 : 0; }));
            }
        }
        var raw = JsonSerializer.SerializeToElement(measurements);
        var summaries = new List<object>();
        foreach (var kind in new[] { "frame-hash", "parent-lifetime" })
        {
            double Read(string label, string property) => Median(raw.EnumerateArray().Where(r => r.GetProperty("kind").GetString() == kind && r.GetProperty("label").GetString() == label).Select(r => r.GetProperty(property).GetDouble()));
            var oldUs = Read("before", "microsecondsPerCall"); var newUs = Read("after", "microsecondsPerCall");
            summaries.Add(new { kind, beforeMedianUs = oldUs, afterMedianUs = newUs, elapsedReductionPercent = (oldUs - newUs) / oldUs * 100, beforeBytesPerCall = Read("before", "bytesPerCall"), afterBytesPerCall = Read("after", "bytesPerCall") });
        }
        var evidence = new { status = "PASS", capturedAtUtc = DateTime.UtcNow, scope = "real InputHost private hash + real cached-parent implementation only; microbenchmark, not complete InputHost/YMCC or HID submit CPU", checks = _checks, randomizedHashParityCases = frames.Length * cultures.Length, cultureAndEdgeCases = cultures.Length * 6 * corners.Length, beforeAssembly = beforePath, beforeSha256 = FileHash(beforePath), afterAssembly = afterPath, afterSha256 = FileHash(afterPath), runtime = Environment.Version.ToString(), measurements, summaries };
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        File.WriteAllText(output, JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine(JsonSerializer.Serialize(new { evidence.status, evidence.checks, evidence.randomizedHashParityCases, evidence.cultureAndEdgeCases, summaries }, new JsonSerializerOptions { WriteIndented = true }));
        beforeContext.Unload(); afterContext.Unload(); return 0;
    }
}