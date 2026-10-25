using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Checks
{
    private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    private static readonly List<object> Results = [];
    private static bool passed = true;
    private static Assembly host = null!;
    private static Type backendType = null!, targetType = null!, stateType = null!;
    private static object backend = null!, engine = null!, options = null!;
    private static FieldInfo? probeField;
    private static FieldInfo? targetReadProbeField;
    private static int reads;
    private static int targetReads;
    private static object? Call(object target, string name, params object?[] args)
    {
        try { return target.GetType().GetMethods(Any).Single(m => m.Name == name && m.GetParameters().Length == args.Length).Invoke(target, args); }
        catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
    }
    private static Type T(string name) => host.GetType("YeManFanHost." + name, true)!;
    private static void Field(object target, string name, object? value) => target.GetType().GetField(name, Any)!.SetValue(target, value);
    private static object? Get(object target, string name) => target.GetType().GetProperty(name, Any)!.GetValue(target);
    private static void Set(object target, string name, object? value) => target.GetType().GetProperty(name, Any)!.SetValue(target, value);
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Check(string name, Action action)
    {
        try { action(); Results.Add(new { name, ok = true }); Console.WriteLine("PASS " + name); }
        catch (Exception error) { passed = false; Results.Add(new { name, ok = false, error = error.Message }); Console.WriteLine("FAIL " + name + ": " + error.Message); }
        finally { probeField?.SetValue(null, null); targetReadProbeField?.SetValue(null, null); }
    }
    private static object State()
    {
        var state = Activator.CreateInstance(stateType)!;
        Set(state, "State", "Ready"); Set(state, "PowerState", "On");
        Set(state, "HardwareWritesEnabled", true); Set(state, "OpenCalled", true); Set(state, "OpenEventsCalled", true);
        Set(state, "Lease", Activator.CreateInstance(T("FanLease"), "fixture-only", 1L, 999999999L, "fixture"));
        var nodes = Array.CreateInstance(T("FanNode"), 4);
        for (var i = 0; i < 4; i++) nodes.SetValue(Activator.CreateInstance(T("FanNode"), i * 25.0, 55.0 + i * 10.0), i);
        Set(state, "ActiveCurve", nodes);
        return state;
    }
    private static void Target(double percent, long sampledAt)
    {
        var owner = Call(backend, "CaptureFanDispatchOwner", new object?[] { null })!;
        var target = Activator.CreateInstance(targetType, Any, null, [owner, percent, sampledAt], null)!;
        Field(backend, "monitorTarget", target); reads = 0;
    }
    private static object Build(object state) => Call(engine, "BuildMonitorFanSnapshot", state)!;
    private static void Empty(object receipt, bool enabled)
    {
        Require(Equals(Get(receipt, "Enabled"), enabled), "control eligibility changed");
        Require(Get(receipt, "Percent") is null && Equals(Get(receipt, "SampledAt"), 0L), "invalid target gained fresh display data");
    }
    public static int Main(string[] args)
    {
        var dll = Path.GetFullPath(args[0]); var output = Path.GetFullPath(args[1]); var requireProbe = args.Length > 2 && args[2] == "probe";
        Directory.CreateDirectory(output);
        var resolver = new AssemblyDependencyResolver(dll);
        AssemblyLoadContext.Default.Resolving += (_, name) => resolver.ResolveAssemblyToPath(name) is { } p ? AssemblyLoadContext.Default.LoadFromAssemblyPath(p) : null;
        host = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
        backendType = T("RealHcBackend"); targetType = backendType.GetNestedType("MonitorTarget", Any)!; stateType = T("HostState");
        options = T("HostOptions").GetMethod("Parse", Any)!.Invoke(null, [Array.Empty<string>()])!;
        backend = Activator.CreateInstance(backendType, Any, null, [options], null)!;
        // Never run Host Main, engine constructor/timers, Open, HC assembly load or a vendor callback.
        engine = RuntimeHelpers.GetUninitializedObject(T("FanHostEngine")); Field(engine, "options", options); Field(engine, "realBackend", backend);
        probeField = targetType.GetField("SampleReadProbeForSelfTest", Any);
        targetReadProbeField = backendType.GetField("MonitorTargetReadProbeForSelfTest", Any);
        if (requireProbe && probeField is null) throw new InvalidOperationException("controlled interleaving requires scratch-only getter instrumentation");
        Check("fixture has no HC assembly, device, STA thread or system timer", () => {
            foreach (var name in new[] { "assembly", "device", "staThread", "temperatureWatchdog", "powerProfileWatchdog" })
                Require(backendType.GetField(name, Any)!.GetValue(backend) is null, "fixture accidentally owns " + name);
            Require(engine.GetType().GetField("runtimeDiagnosticsTimer", Any)!.GetValue(engine) is null, "engine sampler started");
        });
        if (targetReadProbeField is not null)
        {
            Check("inactive state skips backend monitor-target lookup", () => {
                targetReads = 0; targetReadProbeField.SetValue(null, (Action)(() => targetReads++));
                var s = State(); Set(s, "State", "Suspended"); Empty(Build(s), false); Require(targetReads == 0, "inactive target lookup count=" + targetReads);
            });
            Check("active state performs one backend monitor-target lookup", () => {
                targetReads = 0; targetReadProbeField.SetValue(null, (Action)(() => targetReads++));
                Target(85, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000); Build(State()); Require(targetReads == 1, "active target lookup count=" + targetReads);
            });
        }
        Check("fresh target publishes the identical immutable display receipt", () => {
            var stamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, stamp); var r = Build(State());
            Require(Equals(Get(r, "Enabled"), true) && Equals(Get(r, "Percent"), 85.0) && Equals(Get(r, "SampledAt"), stamp), "fresh target missing");
            Require(ReferenceEquals(r, Call(engine, "MonitorFanSnapshotForRead")), "different published receipt");
        });
        foreach (var stampKind in new[] { "zero", "old", "future" }) Check("reject " + stampKind + " target timestamp", () => {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(); Target(60, stampKind == "zero" ? 0 : now + (stampKind == "old" ? -10000 : 120000)); Empty(Build(State()), true);
        });
        foreach (var percent in new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity, -1, 101 }) Check("reject invalid target percent " + percent, () => { Target(percent, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000); Empty(Build(State()), true); });
        foreach (var (name, value) in new (string, object?)[] { ("State", "Suspended"), ("State", "Resuming"), ("State", "FaultLocked"), ("HardwareWritesEnabled", false), ("OpenCalled", false), ("OpenEventsCalled", false), ("Lease", null), ("ActiveCurve", null), ("UnknownState", true), ("CloseCalled", true), ("HcCloseCleanupPending", true), ("PowerState", "Suspending"), ("PowerState", "Suspended"), ("PowerState", "Resuming"), ("OemRestoreConfirmed", true) })
            Check("no fresh display when " + name + "=" + (value ?? "null"), () => {
                Target(80, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000); var s = State(); Set(s, name, value);
                if (probeField is not null) probeField.SetValue(null, (Func<long, long>)(_ => throw new InvalidOperationException("inactive target was read")));
                Empty(Build(s), false);
            });
        Check("unchanged monitor receipt reuses immutable instance", () => {
            var stamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, stamp); var first = Build(State()); var second = Build(State());
            Require(ReferenceEquals(first, second), "unchanged receipt was reallocated");
        });
        Check("changed monitor timestamp publishes a new receipt", () => {
            var stamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, stamp); var first = Build(State()); Target(85, stamp + 1); var second = Build(State());
            Require(!ReferenceEquals(first, second), "changed timestamp reused stale receipt"); Require(Equals(Get(second, "SampledAt"), stamp + 1), "new timestamp missing");
        });
        Check("retired curve owner cannot retain an active target", () => {
            Target(85, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000); Field(backend, "suspendGateEpoch", 1L);
            try { Empty(Build(State()), true); } finally { Field(backend, "suspendGateEpoch", 0L); }
        });
        Check("next response takes a new target without mutating the previous receipt", () => {
            var firstStamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, firstStamp); var first = Build(State());
            Target(65, firstStamp + 200); var second = Build(State());
            Require(Equals(Get(first, "Percent"), 85.0) && Equals(Get(first, "SampledAt"), firstStamp), "earlier receipt mutated");
            Require(Equals(Get(second, "Percent"), 65.0) && Equals(Get(second, "SampledAt"), firstStamp + 200), "later receipt stale");
        });
        if (probeField is not null)
        {
            Check("one response observes a mutable timestamp only once", () => {
                Target(85, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000);
                probeField.SetValue(null, (Func<long, long>)(stamp => { reads++; return stamp; }));
                Build(State()); Require(reads == 1, "timestamp observations=" + reads + ", expected one");
            });
            Check("refresh after the first observation cannot falsely suppress a valid target", () => {
                var firstStamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, firstStamp);
                probeField.SetValue(null, (Func<long, long>)(stamp => ++reads == 1 ? stamp : stamp + 120000));
                var r = Build(State()); Require(Equals(Get(r, "Percent"), 85.0) && Equals(Get(r, "SampledAt"), firstStamp), "mixed observations discarded the first valid target");
            });
            Check("refresh after validation cannot publish an unvalidated timestamp", () => {
                var firstStamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - 1000; Target(85, firstStamp);
                probeField.SetValue(null, (Func<long, long>)(stamp => ++reads <= 3 ? stamp : stamp + 120000));
                var r = Build(State()); Require(Equals(Get(r, "SampledAt"), firstStamp), "receipt published a different timestamp than it validated");
            });
        }
        var report = new { ok = passed, checks = Results.Count, results = Results, hostDll = dll, hostSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(), controlledInterleaving = probeField is not null, actualHostMethod = true, hcAssemblyLoaded = false, hostMainExecuted = false, engineConstructorExecuted = false, hardwareWrites = 0, osSleep = false, cpuMeasurements = false };
        File.WriteAllText(Path.Combine(output, "result.json"), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine(JsonSerializer.Serialize(new { report.ok, report.checks, report.controlledInterleaving })); return passed ? 0 : 1;
    }
}

