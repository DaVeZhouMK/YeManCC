// Real FanHost temperature monitor and Windows named RAM mappings only.
// No production HWiNFO mapping, Host listener, HC load, EC/ACPI/WMI or CPU benchmark.
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class Program
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    private static Type monitorType = null!;
    private static readonly List<object> results = [];
    private static int failures;
    private static object? Get(object target, string name) => monitorType.GetField(name, Flags)!.GetValue(target);
    private static void Set(object target, string name, object? value) => monitorType.GetField(name, Flags)!.SetValue(target, value);
    private static object? Call(object target, string name, params object?[] args)
    {
        var method = monitorType.GetMethods(Flags).Single(m => m.Name == name && m.GetParameters().Length == args.Length);
        try { return method.Invoke(target, args); }
        catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
    }
    private static void Check(bool passed, string name)
    {
        results.Add(new { name, passed });
        if (!passed) failures++;
        Console.WriteLine((passed ? "PASS " : "FAIL ") + name);
    }
    private static object NewMonitor() => Activator.CreateInstance(monitorType, true)!;
    private static float? Read(object monitor, string name)
    {
        var result = Call(monitor, "TryReadSnapshot", name);
        return result is null ? null : (float)result.GetType().GetProperty("TemperatureC")!.GetValue(result)!;
    }
    private sealed class Producer : IDisposable
    {
        public readonly string Name = @"Local\YMCC-Fan-MappingFixture-" + Guid.NewGuid().ToString("N");
        public readonly MemoryMappedFile Map;
        public readonly MemoryMappedViewAccessor Writer;
        public Producer()
        {
            Map = MemoryMappedFile.CreateNew(Name, 2048);
            Writer = Map.CreateViewAccessor();
            Writer.Write(0, 0x53695748U); Writer.Write(4, 2U); Writer.Write(8, 1U);
            Writer.Write(12, (ulong)DateTimeOffset.UtcNow.ToUnixTimeSeconds());
            Writer.Write(20, 48U); Writer.Write(24, 392U); Writer.Write(28, 0U);
            Writer.Write(32, 64U); Writer.Write(36, 316U); Writer.Write(40, 2U); Writer.Write(44, 3000U);
            LabelsAndValues(false);
        }
        public void LabelsAndValues(bool reverse)
        {
            string[] labels = reverse ? ["GPU Power", "CPU Package"] : ["CPU Package", "GPU Power"];
            double[] values = reverse ? [11d, 82d] : [61d, 11d];
            for (int i = 0; i < 2; i++)
            {
                byte[] text = new byte[128]; Encoding.ASCII.GetBytes(labels[i]).CopyTo(text, 0);
                Writer.WriteArray(64 + i * 316 + 12, text, 0, text.Length);
                Writer.Write(64 + i * 316 + 284, values[i]);
            }
        }
        public void Dispose() { Writer.Dispose(); Map.Dispose(); }
    }
    public static int Main(string[] args)
    {
        string dll = Path.GetFullPath(args[0]);
        var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
        monitorType = host.GetType("YeManFanHost.HWiNFOTemperatureMonitor", true)!;
        using (var producer = new Producer())
        {
            var monitor = NewMonitor();
            try
            {
                Check(Read(monitor, producer.Name) == 61f, "initial real shared-memory CPU temperature selected");
                var map = (MemoryMappedFile)Get(monitor, "cachedMapping")!;
                var view = (MemoryMappedViewAccessor)Get(monitor, "cachedMappingView")!;
                var freshness = Get(monitor, "producerFreshness");
                Check(Read(monitor, producer.Name) == 61f && ReferenceEquals(map, Get(monitor, "cachedMapping")), "steady read reuses the same section handle");
                Check(ReferenceEquals(view, Get(monitor, "cachedMappingView")), "steady read reuses the same view");
                Call(monitor, "InvalidateCachedMapping", producer.Name);
                Check(map.SafeMemoryMappedFileHandle.IsClosed && view.SafeMemoryMappedViewHandle.IsClosed, "invalidation closes both original handles");
                Check(ReferenceEquals(freshness, Get(monitor, "producerFreshness")), "invalidation retains producer freshness history");
                // Same mapping name/header/poll timestamp; only the producer's labels moved.
                producer.LabelsAndValues(true);
                Check(Read(monitor, producer.Name) == 82f, "reopen rescans moved labels with identical name and header");
                var nextMap = (MemoryMappedFile)Get(monitor, "cachedMapping")!;
                var nextView = (MemoryMappedViewAccessor)Get(monitor, "cachedMappingView")!;
                ((IDisposable)monitor).Dispose();
                Check(nextMap.SafeMemoryMappedFileHandle.IsClosed && nextView.SafeMemoryMappedViewHandle.IsClosed, "Dispose closes the current cached section and view");
                Check(Get(monitor, "cachedMapping") is null && Get(monitor, "cachedMappingView") is null, "Dispose releases cached owners immediately");
                ((IDisposable)monitor).Dispose();
                Check(nextMap.SafeMemoryMappedFileHandle.IsClosed, "repeated Dispose remains safe");
            }
            finally { ((IDisposable)monitor).Dispose(); }
        }
        using (var producer = new Producer())
        {
            var monitor = NewMonitor();
            int healthy = 0, changed = 0;
            Set(monitor, "lastMappingName", producer.Name);
            Set(monitor, "sampleReceived", (Action<float?>)(_ => Interlocked.Increment(ref changed)));
            Set(monitor, "healthySampleReceived", (Action)(() => Interlocked.Increment(ref healthy)));
            object mappingGate = Get(monitor, "mappingGate")!;
            Exception? pollError = null;
            var poll = new Thread(() => { try { Call(monitor, "Poll", new object?[] { null }); } catch (Exception ex) { pollError = ex; } }) { IsBackground = true };
            Monitor.Enter(mappingGate);
            try
            {
                poll.Start();
                Check(SpinWait.SpinUntil(() => (int)Get(monitor, "polling")! == 1 && (poll.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0, 2000), "real Poll captured callbacks and is waiting on mappingGate");
                ((IDisposable)monitor).Dispose();
            }
            finally { Monitor.Exit(mappingGate); }
            Check(poll.Join(2000) && pollError is null, "queued Poll settles after concurrent disposal");
            Check(Get(monitor, "cachedMapping") is null && Get(monitor, "cachedMappingView") is null, "queued Poll cannot reopen a mapping after disposal");
            Check(healthy == 0 && changed == 0, "queued Poll cannot publish health or temperature after disposal");
            ((IDisposable)monitor).Dispose();
        }
        using (var producer = new Producer())
        {
            var monitor = NewMonitor();
            var samples = new List<float?>();
            // Invalid values intentionally enter fallback: isolate all default
            // mapping names in this test process from production HWiNFO.
            var fallbackNames = (string[])monitorType.GetField("SharedMemoryNames", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null)!;
            var originalFallbackNames = fallbackNames.ToArray();
            for (int i = 0; i < fallbackNames.Length; i++)
                fallbackNames[i] = @"Local\YMCC-Fan-NotPresent-" + Guid.NewGuid().ToString("N");
            int healthy = 0;
            Set(monitor, "lastMappingName", producer.Name);
            Set(monitor, "sampleReceived", (Action<float?>)(value => samples.Add(value)));
            Set(monitor, "healthySampleReceived", (Action)(() => healthy++));
            try
            {
                var pollTime = producer.Writer.ReadUInt64(12);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 1 && samples.SequenceEqual(new float?[] { 61f }), "actual Poll publishes the initial private mapping sample");
                var map = Get(monitor, "cachedMapping");
                var view = Get(monitor, "cachedMappingView");
                var cachedLabels = Get(monitor, "labelCacheKinds");
                producer.Writer.Write(64 + 284, 84d);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 2 && samples.SequenceEqual(new float?[] { 61f, 84f }), "same integer-second producer time cannot hide a changed CPU temperature");
                Check(producer.Writer.ReadUInt64(12) == pollTime && ReferenceEquals(map, Get(monitor, "cachedMapping")) && ReferenceEquals(view, Get(monitor, "cachedMappingView")), "same-time updated sample reuses the actual section and view");
                Check(ReferenceEquals(cachedLabels, Get(monitor, "labelCacheKinds")), "same-time updated sample retains label classification instead of rescanning");
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 3 && samples.Count == 2, "unchanged valid temperature refreshes health without another dispatch callback");
                producer.Writer.Write(64 + 284, double.NaN);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 3 && samples.Count == 2, "invalid live temperature with unchanged producer time cannot refresh health using a cached valid value");
                Check(ReferenceEquals(map, Get(monitor, "cachedMapping")) && ReferenceEquals(view, Get(monitor, "cachedMappingView")) &&
                    !((MemoryMappedFile)map!).SafeMemoryMappedFileHandle.IsClosed && !((MemoryMappedViewAccessor)view!).SafeMemoryMappedViewHandle.IsClosed,
                    "missing fallback probes retain the opened preferred section and view after a temporary invalid value");
                Check(Get(monitor, "labelCacheKinds") is null, "failed different-name probes still invalidate label classification");
                producer.Writer.Write(64 + 284, 73d);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 4 && samples.SequenceEqual(new float?[] { 61f, 84f, 73f }), "next valid temperature in the same producer second remains reachable");
                Check(ReferenceEquals(map, Get(monitor, "cachedMapping")) && ReferenceEquals(view, Get(monitor, "cachedMappingView")),
                    "recovered preferred data reuses the section and view instead of reopening them");
                var recoveryMap = (MemoryMappedFile)Get(monitor, "cachedMapping")!;
                var recoveryView = (MemoryMappedViewAccessor)Get(monitor, "cachedMappingView")!;
                producer.Writer.Write(0, 0U);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 4 && samples.Count == 3 && recoveryMap.SafeMemoryMappedFileHandle.IsClosed && recoveryView.SafeMemoryMappedViewHandle.IsClosed && Get(monitor, "cachedMapping") is null,
                    "an invalid preferred header still closes its owners and never refreshes health");
                producer.Writer.Write(0, 0x53695748U); producer.Writer.Write(64 + 284, 74d);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 5 && samples.Last() == 74f, "a repaired header remains reachable through a fresh read");
            }
            finally
            {
                ((IDisposable)monitor).Dispose();
                Array.Copy(originalFallbackNames, fallbackNames, fallbackNames.Length);
            }
        }
        using (var preferred = new Producer())
        using (var alternative = new Producer())
        {
            var monitor = NewMonitor(); var samples = new List<float?>(); int healthy = 0;
            var fallbackNames = (string[])monitorType.GetField("SharedMemoryNames", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null)!;
            var originalFallbackNames = fallbackNames.ToArray();
            for (int i = 0; i < fallbackNames.Length; i++) fallbackNames[i] = @"Local\YMCC-Fan-NotPresent-" + Guid.NewGuid().ToString("N");
            fallbackNames[0] = alternative.Name; alternative.LabelsAndValues(true);
            Set(monitor, "lastMappingName", preferred.Name);
            Set(monitor, "sampleReceived", (Action<float?>)(value => samples.Add(value)));
            Set(monitor, "healthySampleReceived", (Action)(() => healthy++));
            try
            {
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 1 && samples.SequenceEqual(new float?[] { 61f }), "fallback transition starts on the preferred private mapping");
                var oldMap = (MemoryMappedFile)Get(monitor, "cachedMapping")!;
                var oldView = (MemoryMappedViewAccessor)Get(monitor, "cachedMappingView")!;
                var oldKinds = Get(monitor, "labelCacheKinds");
                preferred.Writer.Write(64 + 284, double.NaN);
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 2 && samples.SequenceEqual(new float?[] { 61f, 82f }), "an available fallback is still selected and its moved CPU label is rescanned");
                Check(oldMap.SafeMemoryMappedFileHandle.IsClosed && oldView.SafeMemoryMappedViewHandle.IsClosed && Equals(Get(monitor, "cachedMappingName"), alternative.Name),
                    "a successful replacement retires both old owners and commits the new mapping");
                Check(!ReferenceEquals(oldKinds, Get(monitor, "labelCacheKinds")), "successful replacement cannot reuse old label classification");
                var newMap = Get(monitor, "cachedMapping"); var newView = Get(monitor, "cachedMappingView");
                Call(monitor, "Poll", new object?[] { null });
                Check(healthy == 3 && samples.Count == 2 && ReferenceEquals(newMap, Get(monitor, "cachedMapping")) && ReferenceEquals(newView, Get(monitor, "cachedMappingView")),
                    "the selected fallback stays cached without another unchanged-value dispatch");
            }
            finally { ((IDisposable)monitor).Dispose(); Array.Copy(originalFallbackNames, fallbackNames, fallbackNames.Length); }
        }
        var report = new { ok = failures == 0, checks = results.Count, failures, hostSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(), scope = "actual FanHost DLL, private read/Dispose/Poll paths, real private Windows RAM mappings; no HC/device or CPU performance claim", results };
        string json = JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
        if (args.Length > 1) File.WriteAllText(args[1], json);
        Console.WriteLine(json);
        return failures == 0 ? 0 : 1;
    }
}
