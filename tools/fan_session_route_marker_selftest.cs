using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

internal static class Checks
{
    private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
    private static readonly List<object> Results = [];
    private static Type backend = null!, kind = null!, strategy = null!;
    private static MethodInfo match = null!;
    private static void Check(string name, bool expected, object?[] args)
    {
        try
        {
            var actual = (bool)match.Invoke(null, args)!;
            var ok = actual == expected;
            Results.Add(new { name, ok, expected, actual });
            Console.WriteLine((ok ? "PASS " : "FAIL ") + name);
        }
        catch (Exception error) { Results.Add(new { name, ok = false, expected, error = error.ToString() }); Console.WriteLine("FAIL " + name + ": " + error.Message); }
    }
    public static int Main(string[] args)
    {
        if (args.Length != 2) throw new ArgumentException("host dll and output");
        var resolver = new AssemblyDependencyResolver(Path.GetFullPath(args[0]));
        AssemblyLoadContext.Default.Resolving += (_, name) => resolver.ResolveAssemblyToPath(name) is { } p ? AssemblyLoadContext.Default.LoadFromAssemblyPath(p) : null;
        var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.GetFullPath(args[0]));
        backend = asm.GetType("YeManFanHost.RealHcBackend", true)!;
        kind = asm.GetType("YeManFanHost.FanRouteKind", true)!;
        strategy = asm.GetType("YeManFanHost.FanRestoreStrategy", true)!;
        match = backend.GetMethod("SessionRouteBindingMatches", Any)!;
        object K(string n) => Enum.Parse(kind, n);
        object S(string n) => Enum.Parse(strategy, n);
        var g = K("GenericDuty"); var p = K("ProfileCurve"); var none = S("None"); var generic = S("GenericEcControl"); var msi = S("MsiHcDefaultRelease");
        object?[] A(bool captured, string? cf, object? ck, object bs, string? nf, object? nk, object ns) => [captured, cf, ck, bs, nf, nk, ns];
        Check("same captured route matches", true, A(true, "HandheldCompanion.Devices.GPDWin5", g, generic, "HandheldCompanion.Devices.GPDWin5", g, generic));
        Check("missing captured binding rejects", false, A(false, "GPD", g, generic, "GPD", g, generic));
        Check("factory drift rejects", false, A(true, "GPD", g, generic, "MSI", g, generic));
        Check("route kind drift rejects", false, A(true, "GPD", g, generic, "GPD", p, generic));
        Check("restore strategy drift rejects", false, A(true, "GPD", g, generic, "GPD", g, msi));
        Check("null route kind remains comparable", true, A(true, "GPD", null, none, "GPD", null, none));
        Check("null factory drift rejects", false, A(true, null, g, generic, "GPD", g, generic));
        Check("all three changes reject", false, A(true, "A", g, generic, "B", p, msi));
        var passed = Results.All(x => (bool)x.GetType().GetProperty("ok")!.GetValue(x)!);
        var report = new { ok = passed, checks = Results.Count, results = Results, hardwareCalls = 0, cpuBenchmark = false, scope = "pure route-binding predicate; no Host, HC, device, sleep or IPC" };
        File.WriteAllText(Path.GetFullPath(args[1]), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine(JsonSerializer.Serialize(new { report.ok, report.checks }));
        return passed ? 0 : 1;
    }
}

