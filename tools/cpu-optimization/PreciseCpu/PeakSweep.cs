using System.Diagnostics;
using System.Text.Json;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;

// Discovery is offline: scheduled residency selects windows, samples describe them.
static class PeakSweep
{
    const long Ms = TimeSpan.TicksPerMillisecond;
    sealed record Slot(long A, long B, TraceProcess Process, int Tid, string ThreadInstance);
    sealed record Peak(long A, long B, long Ticks);
    sealed class Integral
    {
        public readonly long[] Points;
        readonly long[] area, running;
        public Integral(IEnumerable<(long A, long B)> spans)
        {
            var edges = new SortedDictionary<long, int>();
            foreach (var (a, b) in spans)
            {
                edges[a] = edges.GetValueOrDefault(a) + 1;
                edges[b] = edges.GetValueOrDefault(b) - 1;
            }
            Points = edges.Keys.ToArray(); area = new long[Points.Length]; running = new long[Points.Length];
            for (int i = 0; i < Points.Length; ++i)
            {
                if (i > 0) area[i] = area[i-1] + running[i-1] * (Points[i] - Points[i-1]);
                running[i] = (i == 0 ? 0 : running[i-1]) + edges[Points[i]];
            }
        }
        long At(long t)
        {
            int i = Array.BinarySearch(Points, t);
            if (i < 0) i = ~i - 1;
            return i < 0 ? 0 : area[i] + running[i] * (t - Points[i]);
        }
        public long Between(long a, long b) => At(b) - At(a);
    }

    static long Overlap(long a, long b, long lo, long hi) => Math.Max(0, Math.Min(b, hi) - Math.Max(a, lo));
    static Peak[] Pick(Integral signal, Integral problem, long begin, long end, long width, int limit = 3)
    {
        // On each interval between these breakpoints, the sliding integral is linear.
        var edges = signal.Points.Concat(problem.Points).Distinct().ToArray();
        var remaining = new List<(long A, long B)> { (begin, end) };
        var result = new List<Peak>();
        for (int i = 0; i < limit; i++)
        {
            Peak? best = null;
            foreach (var (lo, hi) in remaining.Where(r => r.B - r.A >= width))
            foreach (var a in edges.Concat(edges.Select(t => t - width)).Append(lo).Append(hi - width))
            {
                if (a < lo || a + width > hi || problem.Between(a, a + width) != 0) continue;
                long n = signal.Between(a, a + width);
                if (n > 0 && (best == null || n > best.Ticks || n == best.Ticks && a < best.A)) best = new(a, a + width, n);
            }
            if (best == null) break;
            result.Add(best);
            var block = remaining.Single(r => r.A <= best.A && r.B >= best.B);
            remaining.Remove(block); remaining.Add((block.A, best.A)); remaining.Add((best.B, block.B));
        }
        return result.ToArray();
    }

    public static int Run(string[] args)
    {
        string? output = null;
        bool created = false;
        try
        {
            bool fullFocusStacks = args.Length == 5 && args[4] == "--focus-all-stacks";
            if (args.Length != 2 && !((args.Length == 4 || fullFocusStacks) && args[2] == "--focus"))
                throw new ArgumentException("--peak-sweep <captureRoot> <freshOutputRoot> [--focus <verified-process-receipt.json> [--focus-all-stacks]]");
            string root = Path.GetFullPath(args[0]); output = Path.GetFullPath(args[1]);
            if (File.Exists(output) || Directory.Exists(output)) throw new InvalidDataException("Output must be fresh.");
            Directory.CreateDirectory(output); created = true;
            var identity = Program.Read(Path.Combine(root, "IDENTITY.json"));
            var window = Program.Read(Path.Combine(root, "PEAK-SWEEP-WINDOW.json"));
            var stop = Program.Read(Path.Combine(root, "WPR-STOP.json"));
            var etl = Path.Combine(root, "YMCC-CPU.etl");
            var sha = Program.Hash(etl);
            if (!stop.GetProperty("traceSaved").GetBoolean() || Program.I(stop, "exitCode") != 0 ||
                Program.S(stop, "sha256") != sha || Program.S(stop, "onlyOwnInstanceStopped") != Program.S(identity, "instanceName") ||
                !identity.GetProperty("peakSweep").GetBoolean() || Program.S(identity, "profile") != "YmccCpuScheduling.Verbose.File" ||
                Program.I(window, "formatVersion") != 1 || !window.GetProperty("heldAlive").GetBoolean() ||
                Program.I(window, "pid") != Program.I(identity, "pid") || Program.I(window, "birthFileTime") != Program.I(identity, "birthFileTime") ||
                Program.S(window, "diskImageSha256") != Program.S(identity, "diskImageSha256")) throw new InvalidDataException("Capture identity or saved ETL verification failed.");
            int pid = checked((int)Program.I(identity, "pid"));
            long birth = DateTime.FromFileTimeUtc(Program.I(identity, "birthFileTime")).Ticks;
            long lo = Program.Utc(Program.S(window, "beginUtc")), hi = Program.Utc(Program.S(window, "endUtc"));
            if (pid <= 0 || birth >= lo || hi - lo < 1000 * Ms || hi - lo > 61_000 * Ms) throw new InvalidDataException("Invalid capture time or PID.");
            using var source = new ETWTraceEventSource(etl);
            var model = new Model(lo, hi, source.NumberOfProcessors, (hi-lo)/(decimal)Ms, pid);
            var k = source.Kernel;
            var watch = Stopwatch.StartNew(); long count = 0;
            long T(TraceEvent e)
            {
                if ((++count & 16383) == 0 && (watch.Elapsed.TotalSeconds > 55 || count > 50_000_000))
                    throw new InvalidDataException("Offline ETW analysis limit reached.");
                return e.TimeStamp.ToUniversalTime().Ticks;
            }
            k.ProcessStart += e => model.Process(e.ProcessID, e.ParentID, e.ImageFileName, T(e), true);
            k.ProcessDCStart += e => model.Process(e.ProcessID, e.ParentID, e.ImageFileName, T(e), false);
            k.ProcessStop += e => model.StopProcess(e.ProcessID, T(e), e.ImageFileName);
            k.ThreadStart += e => model.Thread(e.ThreadID, e.ProcessID, T(e), true);
            k.ThreadDCStart += e => model.Thread(e.ThreadID, e.ProcessID, T(e), false);
            k.ThreadStop += e => model.StopThread(e.ThreadID, T(e), e.ProcessID);
            k.ThreadCSwitch += e => model.Switch(e.ProcessorNumber, T(e), e.OldThreadID, e.OldProcessID, e.NewThreadID, e.NewProcessID, e.NewProcessName);
            source.Process();
            if (source.EventsLost != 0 || model.Switches == 0 || model.TargetLifecycleConflict || lo < source.SessionStartTime.ToUniversalTime().Ticks || hi > source.SessionEndTime.ToUniversalTime().Ticks)
                throw new InvalidDataException($"Unusable scheduler trace: lost={source.EventsLost}, switches={model.Switches}, rootConflict={model.TargetLifecycleConflict}, window={Program.Iso(lo)}..{Program.Iso(hi)}.");
            var index = Path.Combine(output, "temporary.etlx");
            try
            {
                TraceLog.CreateFromEventTraceLogFile(etl, index);
                using var log = new TraceLog(index);
                if (log.EventsLost != 0 || log.Truncated) throw new InvalidDataException("Incomplete stack index.");
                var (report, focusStacks) = Analyze(log, model, lo, hi, birth, pid, source.NumberOfProcessors, Program.S(identity, "imagePath"),
                    args.Length >= 4 ? Program.Read(args[3]) : null, fullFocusStacks);
                Program.Save(Path.Combine(output, "PEAKS.json"), new { etl, sha256 = sha, eventsLost = source.EventsLost, report });
                if (focusStacks != null)
                    Program.Save(Path.Combine(output, "FOCUS-STACKS.json"), new { etl, sha256 = sha, focusStacks });
            }
            finally { File.Delete(index); }
            Console.WriteLine(Path.Combine(output, "PEAKS.json"));
            return 0;
        }
        catch (Exception ex)
        {
            if (created) Program.Save(Path.Combine(output!, "ERROR.json"), new { status = "REJECTED", error = ex.ToString() });
            Console.Error.WriteLine(ex.Message); return 2;
        }
    }

    static Peak[] Bursts(Integral signal, Integral problem, long lo, long hi)
    {
        // Fixed 1 ms scan, 15 CPU-ms in 25 wall-ms, coalescing qualifying starts within 250 ms.
        Peak? best = null;
        long last = 0;
        var result = new List<Peak>();
        for (long a = lo; a + 25 * Ms <= hi; a += Ms)
        {
            long b = a + 25 * Ms, value = signal.Between(a, b);
            if (value < 15 * Ms || problem.Between(a, b) != 0) continue;
            if (best != null && a - last > 250 * Ms) { result.Add(best); best = null; }
            if (best == null || value > best.Ticks) best = new(a, b, value);
            last = a;
        }
        if (best != null) result.Add(best);
        return result.ToArray();
    }

    static (object Report, object? FocusStacks) Analyze(TraceLog log, Model model, long lo, long hi, long birth, int pid, int cpus, string image, JsonElement? focusReceipt, bool fullFocusStacks)
    {
        long Start(TraceProcess p) => p.StartTime.ToUniversalTime().Ticks;
        long End(TraceProcess p) => p.EndTime.ToUniversalTime().Ticks;
        var byPid = log.Processes.ToLookup(p => p.ProcessID);
        var target = byPid[pid].Single(p => p.Name.Equals(Path.GetFileNameWithoutExtension(image), StringComparison.OrdinalIgnoreCase) &&
            p.LoadedModules.Any(m => m.FilePath.Equals(image, StringComparison.OrdinalIgnoreCase)) &&
            (Math.Abs(Start(p) - birth) < Ms || (p.StartTimeRelativeMsec == 0 && birth < Start(p))) && Start(p) <= lo && End(p) >= hi);
        var rootModule = target.LoadedModules.Single(m => m.FilePath.Equals(image, StringComparison.OrdinalIgnoreCase)).ModuleFile;
        TraceProcess? focus = null;
        if (focusReceipt is JsonElement receipt)
        {
            int selectedPid = checked((int)Program.I(receipt, "pid"));
            long selectedBirth = DateTime.FromFileTimeUtc(Program.I(receipt, "birthFileTime")).Ticks;
            var selectedImage = Program.S(receipt, "imagePath");
            if (!receipt.GetProperty("sameInstance").GetBoolean() || Program.Utc(Program.S(receipt,"beforeUtc")) > lo ||
                Program.Utc(Program.S(receipt,"afterUtc")) < hi || selectedBirth >= lo || selectedPid <= 0 || selectedPid == pid)
                throw new InvalidDataException("Focus process receipt does not bracket the recording window.");
            focus = byPid[selectedPid].Single(p =>
                p.Name.Equals(Path.GetFileNameWithoutExtension(selectedImage), StringComparison.OrdinalIgnoreCase) &&
                p.LoadedModules.Any(m => m.FilePath.Equals(selectedImage, StringComparison.OrdinalIgnoreCase)) &&
                (Math.Abs(Start(p) - selectedBirth) < Ms || (p.StartTimeRelativeMsec == 0 && selectedBirth < Start(p))) && Start(p) <= lo && End(p) >= hi);
        }
        var family = new HashSet<TraceProcess> { target };
        // Process instances, not PID alone: a short-lived child can exit before capture stops.
        foreach (var p in log.Processes.OrderBy(Start).ThenBy(p => p.ProcessIndex))
        {
            if (p == target || p.ProcessID <= 0 || p.ParentID <= 0 || Start(p) >= hi || End(p) <= lo) continue;
            var parents = byPid[p.ParentID].Where(q => q != p && Start(q) <= Start(p) && Start(p) < End(q)).ToArray();
            if (parents.Length == 1 && family.Contains(parents[0])) family.Add(p);
        }
        // Rundown siblings share the trace start timestamp, so their rows need not be parent-first.
        for (int n = -1; n != family.Count;)
        {
            n = family.Count;
            foreach (var p in log.Processes.Where(p => p.ProcessID > 0 && p.ParentID > 0 && p.StartTimeRelativeMsec == 0 && End(p) > lo))
            {
                var parents = byPid[p.ParentID].Where(q => q != p && q.StartTimeRelativeMsec == 0 && End(q) > Start(p)).ToArray();
                if (parents.Length == 1 && family.Contains(parents[0])) family.Add(p);
            }
            foreach (var p in log.Processes.Where(p => !family.Contains(p) && p.ProcessID > 0 && p.ParentID > 0 && Start(p) < hi && End(p) > lo))
            {
                var parents = byPid[p.ParentID].Where(q => q != p && Start(q) <= Start(p) && Start(p) < End(q)).ToArray();
                if (parents.Length == 1 && family.Contains(parents[0])) family.Add(p);
            }
        }
        string Role(TraceProcess p) => p == target ? "host" : !p.Name.Equals("msedgewebview2", StringComparison.OrdinalIgnoreCase) ? p.Name :
            (p.CommandLine ?? "").Split(' ').FirstOrDefault(a => a.StartsWith("--type=", StringComparison.Ordinal))?[7..].Trim('"')
            ?? (string.IsNullOrWhiteSpace(p.CommandLine) ? "webview-role-unknown" : "webview-browser");
        var roles = family.ToDictionary(p => p, Role);
        if (focus != null) roles.TryAdd(focus, Role(focus));
        var slots = new List<Slot>(); var unknown = new List<(long A, long B)>();
        foreach (var r in model.Runs)
        {
            int owner = r.Thread.Owner?.Pid ?? -1;
            if (owner == 0) continue;
            var matches = byPid[owner].Where(p => Start(p) <= r.A && End(p) >= r.B).Take(2).ToArray();
            if (matches.Length != 1) { unknown.Add((r.A, r.B)); continue; }
            if (roles.ContainsKey(matches[0])) slots.Add(new(r.A, r.B, matches[0], r.Thread.Tid, r.Thread.Key));
        }
        var gaps = new List<(long A, long B)>();
        var byCpu = model.Runs.ToLookup(r => r.Cpu);
        for (int cpu = 0; cpu < cpus; cpu++)
        {
            long cursor = lo;
            foreach (var r in byCpu[cpu]) { if (r.A > cursor) gaps.Add((cursor, r.A)); cursor = Math.Max(cursor, r.B); }
            if (cursor < hi) gaps.Add((cursor, hi));
        }
        var problem = new Integral(gaps.Concat(unknown));
        var groups = new[] { (Name: "tree", Slots: slots.Where(s => family.Contains(s.Process)).ToList()), (Name: "host", Slots: slots.Where(s => s.Process == target).ToList()),
            (Name: "webview", Slots: slots.Where(s => family.Contains(s.Process) && s.Process.Name.Equals("msedgewebview2", StringComparison.OrdinalIgnoreCase)).ToList()) };
        var selected = groups.SelectMany(g => new[] { 25, 100, 250 }.SelectMany(width =>
            Pick(new Integral(g.Slots.Select(s => (s.A, s.B))), problem, lo, hi, width * Ms)
                .Select(p => (g.Name, Width: width, Peak: p, g.Slots)))).ToArray();
        var focusSlots = focus == null ? new List<Slot>() : slots.Where(s => s.Process == focus).ToList();
        var focusSignal = new Integral(focusSlots.Select(s => (s.A, s.B)));
        var focusBursts = focus == null ? Array.Empty<Peak>() : Bursts(focusSignal, problem, lo, hi);
        var relevant = new Dictionary<Peak, List<(TraceProcess Process, int Tid, string ThreadInstance, int Weight, string[] Frames)>>();
        foreach (var s in selected) relevant.TryAdd(s.Peak, new());
        foreach (var p in focusBursts) relevant.TryAdd(p, new());
        var runningByThread = slots.ToLookup(s => (s.Process, s.Tid));
        long unjoinedSamples = 0;
        long focusEvents = 0, focusWeight = 0, focusUnjoinedWeight = 0, focusMissingStackWeight = 0, focusTruncatedWeight = 0;
        var focusStackWeights = new Dictionary<(string Stack, bool Truncated), long>();
        foreach (var e in log.Events.ByEventType<SampledProfileTraceData>())
        {
            long at = e.TimeStamp.ToUniversalTime().Ticks;
            var p = byPid[e.ProcessID].SingleOrDefault(x => roles.ContainsKey(x) && Start(x) <= at && at < End(x));
            if (p == null) continue;
            var windows = relevant.Where(x => x.Key.A <= at && at < x.Key.B).ToArray();
            bool collectFocus = fullFocusStacks && p == focus && lo <= at && at < hi;
            if (windows.Length == 0 && !collectFocus) continue;
            if (collectFocus) { ++focusEvents; focusWeight += e.Count; }
            var matches = runningByThread[(p, e.ThreadID)].Where(s => s.A <= at && at < s.B).Take(2).ToArray();
            if (matches.Length != 1)
            {
                if (windows.Length != 0) ++unjoinedSamples;
                if (collectFocus) focusUnjoinedWeight += e.Count;
                continue;
            }
            var frames = new List<string>();
            var f = e.CallStack();
            for (; f != null && frames.Count < 48; f = f.Caller)
            {
                var addr = f.CodeAddress; var module = addr.ModuleFile;
                var loaded = module == null ? null : p.LoadedModules.FirstOrDefault(m => m.ModuleFile == module && addr.Address >= m.ImageBase && addr.Address - m.ImageBase < (ulong)m.ModuleFile.ImageSize);
                frames.Add(loaded == null ? $"{module?.Name ?? "?"}@0x{addr.Address:x}" : $"{module!.Name}+0x{addr.Address-loaded.ImageBase:x}");
            }
            var stack = frames.ToArray();
            if (collectFocus)
            {
                if (stack.Length == 0) focusMissingStackWeight += e.Count;
                else
                {
                    bool truncated = f != null;
                    if (truncated) focusTruncatedWeight += e.Count;
                    var key = (Stack: string.Join(";", stack), Truncated: truncated);
                    focusStackWeights[key] = focusStackWeights.GetValueOrDefault(key) + e.Count;
                }
            }
            foreach (var w in windows) w.Value.Add((p, e.ThreadID, matches[0].ThreadInstance, e.Count, stack));
        }
        object Window(string scope, int windowMs, Peak p, List<Slot> rows)
        {
            var threads = rows.Where(s => s.A < p.B && p.A < s.B).GroupBy(s => (s.Process, s.Tid, s.ThreadInstance))
                .Select(g => new { G = g, Ticks = g.Sum(s => Overlap(s.A, s.B, p.A, p.B)) }).OrderByDescending(g => g.Ticks).ToArray();
            return new { scope, windowMs, startUtc = Program.Iso(p.A), endUtc = Program.Iso(p.B), scheduledMs = p.Ticks/(decimal)Ms,
                percentOfOneLogicalCpu = p.Ticks/(decimal)(p.B-p.A)*100, percentOfAllLogicalCpus = p.Ticks/(decimal)(p.B-p.A)/cpus*100,
                roles = threads.GroupBy(t => roles[t.G.Key.Process]).OrderByDescending(g => g.Sum(t => t.Ticks)).Select(g => new { role = g.Key, scheduledMs = g.Sum(t => t.Ticks)/(decimal)Ms }),
                threads = threads.Take(12).Select(t => new { pid = t.G.Key.Process.ProcessID, process = t.G.Key.Process.Name, role = roles[t.G.Key.Process],
                    tid = t.G.Key.Tid, threadInstance = t.G.Key.ThreadInstance, scheduledMs = t.Ticks/(decimal)Ms }),
                samples = relevant[p].Where(s => threads.Any(t => t.G.Key == (s.Process, s.Tid, s.ThreadInstance)))
                    .GroupBy(s => (s.Process, s.Tid, s.ThreadInstance, Stack: string.Join(";", s.Frames))).OrderByDescending(g => g.Sum(s => s.Weight))
                    .Select(g => new { pid = g.Key.Process.ProcessID, tid = g.Key.Tid, threadInstance = g.Key.ThreadInstance, weight = g.Sum(s => s.Weight), frames = g.Key.Stack.Length == 0 ? Array.Empty<string>() : g.Key.Stack.Split(';') }) };
        }
        var report = new { status = gaps.Count == 0 && unknown.Count == 0 ? "COMPLETE_SCHEDULER_INTERVAL" : "PARTIAL_INTERVAL_OBSERVED_PEAKS_ONLY",
            beginUtc = Program.Iso(lo), endUtc = Program.Iso(hi), logicalProcessors = cpus, targetPid = pid, model.Switches, model.Counts,
            unobservedCpuMs = gaps.Sum(g => g.B-g.A)/(decimal)Ms, unboundCpuMs = unknown.Sum(g => g.B-g.A)/(decimal)Ms,
            sampleStacksPresent = log.HasCallStacks, unjoinedSamples,
            rootImage = new { path = image, pdbGuid = rootModule.PdbSignature, pdbAge = rootModule.PdbAge },
            note = "Peaks require unbroken CSwitch coverage and bound running owners in the selected window. Partial intervals cannot prove the maximum over the capture. Duration is scheduled residency including possible interrupts; sample weight and RVAs are location evidence only. Parentage of pre-existing descendants is ETW rundown evidence; detached helpers are outside the tree.",
            processes = roles.Where(x => family.Contains(x.Key)).Select(x => new { pid = x.Key.ProcessID, parentPid = x.Key.ParentID, name = x.Key.Name, role = x.Value, startUtc = Program.Iso(Start(x.Key)), endUtc = Program.Iso(End(x.Key)), rundown = x.Key.StartTimeRelativeMsec == 0 }),
            focus = focus == null ? null : new { pid = focus.ProcessID, parentPid = focus.ParentID, name = focus.Name, inTree = family.Contains(focus),
                observedScheduledMs = focusSignal.Between(lo,hi)/(decimal)Ms, windowMs = 25, thresholdScheduledMs = 15, scanStepMs = 1, mergeWithinMs = 250,
                bursts = focusBursts.Select(p => Window("focus", 25, p, focusSlots)) },
            peaks = selected.Select(s => Window(s.Name, s.Width, s.Peak, s.Slots)) };
        // The burst threshold is useful for cadence, but cannot tell 14 ms from 1 ms.
        // Keep this exact covered peak and the RVA identities in the opt-in report.
        var bestFocus = fullFocusStacks ? Pick(focusSignal, problem, lo, hi, 25 * Ms, limit: 1).FirstOrDefault() : null;
        var focusStacks = fullFocusStacks ? new { pid = focus!.ProcessID, beginUtc = Program.Iso(lo), endUtc = Program.Iso(hi),
            bestCovered25ms = bestFocus == null ? null : new { startUtc = Program.Iso(bestFocus.A), endUtc = Program.Iso(bestFocus.B),
                scheduledMs = bestFocus.Ticks/(decimal)Ms },
            images = focus.LoadedModules.Select(m => new { path = m.FilePath, name = m.ModuleFile.Name, size = m.ModuleFile.ImageSize,
                checksum = m.ModuleFile.ImageChecksum, buildTimeUtc = m.ModuleFile.BuildTime.ToUniversalTime() })
                .Distinct().OrderBy(m => m.path, StringComparer.OrdinalIgnoreCase).ToArray(),
            events = focusEvents, weight = focusWeight, unjoinedWeight = focusUnjoinedWeight, missingStackWeight = focusMissingStackWeight,
            truncatedWeight = focusTruncatedWeight, stacks = focusStackWeights.OrderByDescending(k => k.Value)
                .ThenBy(k => k.Key.Stack, StringComparer.Ordinal).ThenBy(k => k.Key.Truncated)
                .Select(k => new { frames = k.Key.Stack.Split(';'), truncated = k.Key.Truncated, weight = k.Value }) } : null;
        return (report, focusStacks);
    }

    public static int SelfTest()
    {
        static void Check(bool ok) { if (!ok) throw new InvalidDataException("Peak sweep control failed."); }
        var empty = new Integral(Array.Empty<(long,long)>());
        var signal = new Integral(new (long,long)[] { (23,43), (30,45), (210,220) });
        var top = Pick(signal, empty, 0, 300, 25);
        Check(top[0] == new Peak(20,45,35)); // Parallel CPU time, even across a fixed bucket edge.
        Check(top[1].Ticks == 10 && top[1].A >= top[0].B);
        var loss = new Integral(new (long,long)[] { (20,48) });
        Check(Pick(signal, loss, 0, 300, 25)[0].Ticks == 10);
        Check(Pick(empty, empty, 0, 300, 25).Length == 0);
        Check(Pick(signal, empty, 0, 24, 25).Length == 0);
        var repeated = new Integral(new (long,long)[] { (50*Ms, 73*Ms), (180*Ms, 198*Ms), (3100*Ms, 3120*Ms), (6100*Ms, 6114*Ms) });
        Check(Bursts(repeated, empty, 0, 6200*Ms).Select(p => p.Ticks / Ms).SequenceEqual(new long[] { 23, 20 }));
        var quiet = new Integral(new (long,long)[] { (30*Ms, 44*Ms) });
        Check(Bursts(quiet, empty, 0, 100*Ms).Length == 0 &&
            Pick(quiet, empty, 0, 100*Ms, 25*Ms, limit: 1).Single() == new Peak(19*Ms, 44*Ms, 14*Ms));
        Console.WriteLine("PASS peak sweep controls: cross-boundary, parallel CPU, gaps, non-overlap, empty and short intervals");
        return 0;
    }
}
