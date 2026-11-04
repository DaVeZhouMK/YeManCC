using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Diagnostics.Tracing;

// Development-only offline sidecar. No TraceLog conversion, live session, symbol download or product hooks.
static class Program
{
    internal static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    internal static string S(JsonElement e, string k) => e.TryGetProperty(k, out var v) && v.ValueKind != JsonValueKind.Null ? (v.ValueKind == JsonValueKind.String ? v.GetString()! : v.GetRawText()) : "";
    internal static long I(JsonElement e, string k) => long.Parse(S(e, k), CultureInfo.InvariantCulture);
    internal static decimal D(JsonElement e, string k) => decimal.Parse(S(e, k), CultureInfo.InvariantCulture);
    internal static long Utc(string s) => DateTimeOffset.Parse(s, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind).UtcTicks;
    internal static string Iso(long t) => new DateTime(t, DateTimeKind.Utc).ToString("O");
    internal static JsonElement Read(string path) => JsonDocument.Parse(File.ReadAllText(path)).RootElement.Clone();
    internal static string Hash(string path) { using var f = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(f)).ToLowerInvariant(); }
    internal static void Save(string path, object data) => File.WriteAllText(path, JsonSerializer.Serialize(data, Json));
    public static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--self-test") return SelfTests.Run();
        if (args.Length == 1 && args[0] == "--self-test-liveness") return SelfTests.RunLiveness();
        if (args.Length < 4 || args[2] != "--target-pid") { Console.Error.WriteLine("PreciseCpu <captureRoot> <freshOutputRoot> --target-pid PID [--warmup-seconds 15] | --self-test"); return 2; }
        string? output = null; bool outputCreated = false;
        try
        {
            var root = Path.GetFullPath(args[0]); output = Path.GetFullPath(args[1]);
            if (Directory.Exists(output) || File.Exists(output)) throw new InvalidDataException("Output must be fresh.");
            if (output.Equals(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Output equals capture root.");
            int pid = int.Parse(args[3], CultureInfo.InvariantCulture);
            decimal warmup = 15;
            if (args.Length > 4) { if (args.Length != 6 || args[4] != "--warmup-seconds") throw new InvalidDataException("Unknown arguments."); warmup = decimal.Parse(args[5], CultureInfo.InvariantCulture); }
            if (pid <= 0 || warmup < 0) throw new InvalidDataException("Invalid PID/warmup.");
            Directory.CreateDirectory(output); outputCreated = true;
            var watch = Stopwatch.StartNew();
            var input = Capture.Load(root, pid, warmup);
            Save(Path.Combine(output, "WINDOW.json"), input.Evidence());
            var etls = Directory.GetFiles(root, "*.etl", SearchOption.TopDirectoryOnly);
            var etl = Path.Combine(root, "YMCC-CPU.etl");
            if (!File.Exists(etl)) { if (etls.Length != 1) throw new InvalidDataException("Need one unambiguous ETL."); etl = etls[0]; }
            using var source = new ETWTraceEventSource(etl);
            var model = new Model(input.Begin, input.End, source.NumberOfProcessors, input.WindowMs, pid);
            var kernel = source.Kernel;
            long count = 0;
            void Guard() { if ((++count & 16383) == 0 && (watch.Elapsed.TotalSeconds > 55 || count > 50_000_000)) throw new InvalidDataException("Offline parse bounded limit (55s/50M relevant events) reached; incomplete."); }
            long T(TraceEvent e) => e.TimeStamp.ToUniversalTime().Ticks;
            kernel.ProcessStart += e => { Guard(); model.Process(e.ProcessID, e.ParentID, e.ImageFileName, T(e), true); };
            kernel.ProcessDCStart += e => { Guard(); model.Process(e.ProcessID, e.ParentID, e.ImageFileName, T(e), false); };
            kernel.ProcessDCStop += e => { Guard(); model.Process(e.ProcessID, e.ParentID, e.ImageFileName, T(e), false); };
            kernel.ProcessStop += e => { Guard(); model.StopProcess(e.ProcessID, T(e), e.ImageFileName); };
            kernel.ThreadStart += e => { Guard(); model.Thread(e.ThreadID, e.ProcessID, T(e), true); };
            kernel.ThreadDCStart += e => { Guard(); model.Thread(e.ThreadID, e.ProcessID, T(e), false); };
            kernel.ThreadDCStop += e => { Guard(); model.Thread(e.ThreadID, e.ProcessID, T(e), false); };
            kernel.ThreadStop += e => { Guard(); model.StopThread(e.ThreadID, T(e), e.ProcessID); };
            kernel.ThreadCSwitch += e => { Guard(); model.Switch(e.ProcessorNumber, T(e), e.OldThreadID, e.OldProcessID, e.NewThreadID, e.NewProcessID, e.NewProcessName, e.TimeStampRelativeMSec < 0); };
            kernel.PerfInfoDPC += e => { Guard(); model.Interrupt(e.ProcessorNumber, T(e), e.ElapsedTimeMSec, "DPC"); };
            kernel.PerfInfoISR += e => { Guard(); model.Interrupt(e.ProcessorNumber, T(e), e.ElapsedTimeMSec, "ISR"); };
            kernel.PerfInfoSample += e => { Guard(); model.Sample(e.ProcessorNumber, T(e), e.ProcessID, e.ThreadID, e.InstructionPointer, e.Count, e.ExecutingDPC, e.ExecutingISR); };
            source.Process();
            var report = model.Finish(input, source.EventsLost);
            using (var f = new StreamWriter(Path.Combine(output, "PEAK-INTERVALS.jsonl")))
                foreach (var run in model.Runs) f.WriteLine(JsonSerializer.Serialize(model.RunEvidence(run)));
            using (var f = new StreamWriter(Path.Combine(output, "INTERRUPT-INTERVALS.jsonl")))
                foreach (var pair in model.Interrupts) foreach (var irq in pair.Value) f.WriteLine(JsonSerializer.Serialize(new { cpu = pair.Key, startUtc = Iso(irq.A), endUtc = Iso(irq.B), startUtc100ns = irq.A.ToString(), endUtc100ns = irq.B.ToString(), kind = irq.Kind }));
            Save(Path.Combine(output, "SAMPLED-LOCATIONS.json"), new { meaning = "Occurrence/location evidence only; no implicit 1ms/sample; no elapsed CPU inference or symbol resolution", locations = model.Samples.OrderByDescending(x => x.Value).Select(x => new { key = x.Key, occurrences = x.Value }), model.SamplesTruncated });
            Save(Path.Combine(output, "REPORT.json"), new { status = report.Status, report, input = input.Evidence(), header = new { source.EventsLost, source.NumberOfProcessors, SessionStartTime = source.SessionStartTime.ToUniversalTime().ToString("O"), SessionEndTime = source.SessionEndTime.ToUniversalTime().ToString("O"), sessionStartUtc100ns = source.SessionStartTime.ToUniversalTime().Ticks.ToString(), sessionEndUtc100ns = source.SessionEndTime.ToUniversalTime().Ticks.ToString(), note = "Zero EventsLost alone does not prove completeness." }, etl = new { path = etl, bytes = new FileInfo(etl).Length }, parser = new { processId = Environment.ProcessId, relevantEvents = count, elapsedMs = watch.Elapsed.TotalMilliseconds, source = "ETWTraceEventSource raw Kernel callbacks", developmentOnly = true, wholeTraceRanking = "NOT_IMPLEMENTED", readyThread = "NOT_IMPLEMENTED" } });
            Console.WriteLine(JsonSerializer.Serialize(new { report.Status, outputRoot = output, targetPid = pid, windowStart = Iso(input.Begin), windowEnd = Iso(input.End), report.CoverageComplete, report.Precision, report.CounterReconciliation, elapsedMs = watch.Elapsed.TotalMilliseconds }));
            return report.Status.StartsWith("REJECTED") ? 3 : 0;
        }
        catch (Exception ex)
        {
            if (outputCreated && output != null && Directory.Exists(output)) Save(Path.Combine(output, "ERROR.json"), new { status = "REJECTED", error = ex.ToString(), atUtc = DateTime.UtcNow.ToString("O") });
            Console.Error.WriteLine(ex.ToString()); return 2;
        }
    }
}

sealed class Capture
{
    public required JsonElement Identity, Final, Peak, Baseline;
    public required string Selection;
    public int Pid, CpuCount;
    public long Begin, End;
    public decimal WindowMs, CounterMs, Percent, QpcWindowMs, UtcWindowMs;
    public readonly List<string> Problems = new();
    public readonly List<object> Hashes = new();
    public static Capture Load(string root, int pid, decimal warmup)
    {
        var id = Program.Read(Path.Combine(root, "IDENTITY.json"));
        var final = Program.Read(Path.Combine(root, "CAPTURE.json"));
        if (Program.I(id, "pid") != pid) throw new InvalidDataException("IDENTITY PID mismatch.");
        int cpus = checked((int)Program.I(id, "logicalProcessors"));
        if (cpus <= 0) throw new InvalidDataException("Invalid logicalProcessors.");
        var rows = new Dictionary<long, JsonElement>();
        foreach (string line in File.ReadLines(Path.Combine(root, "CPU.jsonl")))
        {
            if (string.IsNullOrWhiteSpace(line)) continue;
            var row = JsonDocument.Parse(line).RootElement.Clone();
            if (!rows.TryAdd(Program.I(row, "sample"), row)) throw new InvalidDataException("Duplicate sample ID.");
            if (rows.Count > 1_000_000) throw new InvalidDataException("CPU row bound exceeded.");
        }
        JsonElement? trigger = null;
        if (File.Exists(Path.Combine(root, "TRIGGER.json"))) trigger = Program.Read(Path.Combine(root, "TRIGGER.json"));
        else if (final.TryGetProperty("trigger", out var tr) && tr.ValueKind == JsonValueKind.Object) trigger = tr;
        Capture? selected = null;
        var failures = new List<string>();
        foreach (var pair in rows.OrderBy(x => x.Key))
        {
            if (trigger.HasValue && Program.I(trigger.Value, "sample") != pair.Key) continue;
            if (!trigger.HasValue && Program.D(pair.Value, "wallMs") < warmup * 1000) continue;
            try
            {
                var candidate = Window(rows, pair.Key, pid, cpus, Program.S(id, "birthFileTime"));
                candidate.Identity = id; candidate.Final = final;
                if (trigger.HasValue)
                {
                    var t = trigger.Value;
                    if (Program.Utc(Program.S(t, "atUtc")) != candidate.End || Math.Abs(Program.D(t, "windowMs") - candidate.WindowMs) > 0.000001m || Math.Abs(Program.D(t, "cpuPercentMachine") - candidate.Percent) > 0.000000001m) throw new InvalidDataException("Trigger does not match selected CPU row.");
                }
                if (selected == null || candidate.Percent > selected.Percent) selected = candidate;
            }
            catch (Exception e) { if (failures.Count < 10) failures.Add($"sample {pair.Key}: {e.Message}"); }
        }
        if (selected == null) throw new InvalidDataException("No valid exact sample-4 window (actual trigger cannot fall back): " + string.Join("; ", failures));
        selected.Selection = trigger.HasValue ? "actual trigger row" : "maximum valid row after warmup";
        selected.ValidateIdentity();
        foreach (var file in new[] { "CPU.jsonl", "IDENTITY.json", "CAPTURE.json", "TRIGGER.json" })
        { var p = Path.Combine(root, file); if (File.Exists(p)) selected.Hashes.Add(new { path = p, sha256 = Program.Hash(p) }); }
        return selected;
    }
    public static Capture Window(Dictionary<long, JsonElement> rows, long sample, int pid, int cpus, string birth)
    {
        if (!rows.TryGetValue(sample - 4, out var baseline)) throw new InvalidDataException("Missing exact sample-4 baseline.");
        var peak = rows[sample];
        for (long i = sample - 4; i <= sample; i++)
        {
            if (!rows.TryGetValue(i, out var r) || Program.I(r, "pid") != pid || Program.S(r, "birthFileTime") != birth) throw new InvalidDataException("Missing/changed held-handle sample identity.");
            if (i > sample - 4)
            {
                var old = rows[i - 1];
                if (Program.D(r, "wallMs") <= Program.D(old, "wallMs") || BigInteger.Parse(Program.S(r, "qpc")) <= BigInteger.Parse(Program.S(old, "qpc")) || BigInteger.Parse(Program.S(r, "cpu100ns")) < BigInteger.Parse(Program.S(old, "cpu100ns")) || Program.Utc(Program.S(r, "atUtc")) <= Program.Utc(Program.S(old, "atUtc"))) throw new InvalidDataException("Nonmonotonic five-row window.");
            }
            if (Program.S(r, "qpcFrequency") != Program.S(peak, "qpcFrequency")) throw new InvalidDataException("QPC frequency changed.");
        }
        decimal wall = Program.D(peak, "wallMs") - Program.D(baseline, "wallMs");
        var delta = BigInteger.Parse(Program.S(peak, "cpu100ns")) - BigInteger.Parse(Program.S(baseline, "cpu100ns"));
        decimal cpuMs = checked((decimal)delta) / 10000m;
        decimal pct = cpuMs / wall / cpus * 100;
        if (wall <= 0 || Math.Abs(Program.D(peak, "windowMs") - wall) > 0.000001m || Math.Abs(Program.D(peak, "cpuPercentMachine1s") - pct) > 0.000000001m) throw new InvalidDataException("Stored denominator/CPU delta/percent mismatch.");
        decimal freq = Program.D(peak, "qpcFrequency");
        if (freq <= 0) throw new InvalidDataException("Invalid QPC frequency.");
        decimal qpcMs = checked((decimal)(BigInteger.Parse(Program.S(peak, "qpc")) - BigInteger.Parse(Program.S(baseline, "qpc")))) * 1000m / freq;
        var result = new Capture { Identity = default, Final = default, Peak = peak, Baseline = baseline, Selection = "test", Pid = pid, CpuCount = cpus, Begin = Program.Utc(Program.S(baseline, "atUtc")), End = Program.Utc(Program.S(peak, "atUtc")), WindowMs = wall, CounterMs = cpuMs, Percent = pct, QpcWindowMs = qpcMs };
        result.UtcWindowMs = (result.End - result.Begin) / 10000m;
        // Reads are sequential in the collector: QPC/UTC and wall are not simultaneous.
        if (Math.Abs(qpcMs - wall) > 1m) result.Problems.Add("QPC_WALL_WINDOW_DIFFERENCE_OVER_1MS");
        if (Math.Abs(result.UtcWindowMs - wall) > 1m) result.Problems.Add("UTC_WALL_WINDOW_DIFFERENCE_OVER_1MS");
        return result;
    }
    public void ValidateIdentity()
    {
        if (Program.Utc(Program.S(Identity, "startUtc")) != DateTime.FromFileTimeUtc(long.Parse(Program.S(Identity, "birthFileTime"))).Ticks) Problems.Add("IDENTITY_OS_BIRTH_DISAGREES");
        if (Begin < Program.Utc(Program.S(Identity, "startUtc"))) Problems.Add("WINDOW_BEFORE_TARGET_BIRTH");
        if (!Final.TryGetProperty("finalTargetIdentity", out var held)) { Problems.Add("FINAL_HELD_IDENTITY_MISSING"); return; }
        if (Program.S(held, "birthFileTime") != Program.S(Identity, "birthFileTime")) Problems.Add("FINAL_BIRTH_MISMATCH");
        if (Program.S(Final, "finalHeldBirthVerified") != "true") Problems.Add("FINAL_HELD_BIRTH_NOT_VERIFIED");
        if (Program.S(held, "livenessSource") != "HELD_SYNCHRONIZABLE_HANDLE_WAIT" || Program.S(held, "waitStatus") != "258" || Program.S(held, "alive") != "true") Problems.Add("FINAL_TARGET_LIVENESS_UNPROVED");
        if (Program.S(held, "waitStatus") == "0") Problems.Add("FINAL_TARGET_NOT_ALIVE");
        if (string.IsNullOrEmpty(Program.S(Final, "finalDiskImageSha256")) || !Program.S(Final, "finalDiskImageSha256").Equals(Program.S(Identity, "diskImageSha256"), StringComparison.OrdinalIgnoreCase)) Problems.Add("FINAL_DISK_HASH_MISSING_OR_CHANGED");
        if (!BigInteger.TryParse(Program.S(held, "cpu100ns"), out var cpu) || cpu < BigInteger.Parse(Program.S(Peak, "cpu100ns"))) Problems.Add("FINAL_COUNTER_MISSING_OR_BACKWARDS");
    }
    public object Evidence() => new { Selection, targetPid = Pid, CpuCount, baselineSample = Program.I(Baseline, "sample"), peakSample = Program.I(Peak, "sample"), startUtc = Program.Iso(Begin), endUtc = Program.Iso(End), startUtc100ns = Begin.ToString(), endUtc100ns = End.ToString(), baselineCpu100ns = Program.S(Baseline, "cpu100ns"), peakCpu100ns = Program.S(Peak, "cpu100ns"), deltaCpu100ns = (BigInteger.Parse(Program.S(Peak, "cpu100ns")) - BigInteger.Parse(Program.S(Baseline, "cpu100ns"))).ToString(), baselineQpc = Program.S(Baseline, "qpc"), peakQpc = Program.S(Peak, "qpc"), WindowMs, CounterMs, Percent, QpcWindowMs, UtcWindowMs, denominatorAllMachineMs = WindowMs * CpuCount, arithmetic = "BigInteger CPU/QPC strings; decimal denominator; exact sample ID minus four; 1e-6 ms and 1e-9 percentage comparison tolerance", identityProblems = Problems, sourceHashes = Hashes };
}

sealed class Proc
{
    public int Pid, Parent;
    public required string Key, Name, Origin;
    public string? ParentKey;
    public long? Start, Stop;
}
sealed class Thr
{
    public int Tid;
    public required string Key, Origin;
    public bool PendingFinalOut;
    public Proc? Owner;
    public long? Start, Stop;
}
sealed record Open(long A, Thr Thread);
sealed record Run(int Cpu, long A, long B, long RawA, long RawB, Thr Thread);
sealed record Irq(long A, long B, string Kind);
sealed record Range(long A, long B);
sealed record Ranking(int Pid, string ProcessKey, string Name, int ParentPid, string? ParentKey, int? Tid, string? ThreadKey, string OwnerEvidence, decimal ScheduledMs, decimal InterruptOverlapMs, decimal AdjustedMs, decimal PercentAllMachine, decimal AdjustedPercentAllMachine);
sealed record Result(string Status, bool CoverageComplete, string Precision, string CounterReconciliation, object[] Coverage, Ranking[] WholeMachineProcesses, Ranking[] TargetThreads, Ranking[] TargetAndDescendants, object Reconciliation, object Diagnostics);

sealed class Model
{
    readonly long begin, end;
    readonly int cpus, target;
    readonly decimal wall;
    readonly Dictionary<int, Open> open = new();
    readonly Dictionary<int, long> lastCpu = new();
    readonly Dictionary<int, Proc> processes = new();
    readonly Dictionary<int, Thr> threads = new();
    readonly Dictionary<int, int> pgen = new(), tgen = new();
    readonly List<Proc> allProcesses = new();
    public readonly List<Run> Runs = new();
    public readonly Dictionary<int, List<Irq>> Interrupts = new();
    public readonly Dictionary<string, long> Samples = new();
    public readonly Dictionary<string, long> Counts = new();
    public bool SamplesTruncated;
    public long Switches, Dpcs, Isrs;
    public bool TargetLifecycleConflict;
    public Model(long a, long b, int n, decimal wallMs, int pid) { begin = a; end = b; cpus = n; wall = wallMs; target = pid; if (n <= 0 || n > 4096 || b <= a) throw new InvalidDataException("Invalid window/CPU count."); }
    void Count(string k) { Counts[k] = Counts.GetValueOrDefault(k) + 1; }
    static int Next(Dictionary<int, int> gens, int id) { int v = gens.GetValueOrDefault(id) + 1; gens[id] = v; return v; }
    Proc Observe(int pid, string name = "")
    {
        if (processes.TryGetValue(pid, out var p)) { if (name.Length > 0 && p.Name.Length == 0) p.Name = name; return p; }
        p = new Proc { Pid = pid, Parent = -1, Key = $"{pid}#{Next(pgen, pid)}", Name = pid == 0 ? "Idle" : name, Origin = "CSwitch/thread owner observation; OS birth unknown" };
        processes[pid] = p; allProcesses.Add(p); return p;
    }
    void Invalidate(Func<Thr, bool> predicate)
    {
        foreach (var pair in open.Where(x => predicate(x.Value.Thread)).ToArray()) { open.Remove(pair.Key); Count("openRunInvalidatedWithoutInventedEnd"); }
    }
    public void Process(int pid, int parent, string name, long time, bool start)
    {
        if (pid < 0) { Count("invalidPid"); return; }
        if (start)
        {
            Count("processStart");
            Invalidate(t => t.Owner?.Pid == pid);
            if (pid == target && time >= begin && time <= end) TargetLifecycleConflict = true;
            if (processes.TryGetValue(pid, out var old))
            {
                Count("pidReuseOrStartAfterObservation"); old.Stop = time;
                Invalidate(t => t.Owner == old);
                foreach (var t in threads.Values.Where(t => t.Owner == old).ToArray()) { t.Stop = time; threads.Remove(t.Tid); }
                processes.Remove(pid);
            }
        }
        else Count("processRundownObservationNotCreation");
        var p = Observe(pid, name);
        p.Parent = parent; p.Name = name;
        if (start) { p.Start = time; p.Origin = "ProcessStart event (not held-handle OS birth)"; }
        else if (!p.Start.HasValue) p.Origin = "Process rundown (DCStart/DCStop) observation; creation unknown";
        if (processes.TryGetValue(parent, out var pp) && pp != p) p.ParentKey = pp.Key;
    }
    public void StopProcess(int pid, long time, string name = "")
    {
        Count("processStop");
        if (pid == target && time >= begin && time <= end) TargetLifecycleConflict = true;
        if (!processes.Remove(pid, out var p)) { Count("unmatchedProcessStop"); return; }
        p.Stop = time; if (name.Length > 0) p.Name = name;
        foreach (var t in threads.Values.Where(t => t.Owner == p).ToArray()) { t.Stop = time; t.PendingFinalOut = true; threads.Remove(t.Tid); }
    }
    public void Thread(int tid, int pid, long time, bool start)
    {
        if (tid <= 0) return;
        if (threads.TryGetValue(tid, out var old))
        {
            if (!start && old.Owner == null && pid >= 0) { old.Owner = Observe(pid); old.Origin = "Late rundown owner for uninterrupted observed thread lifetime; creation unknown"; Count("lateRundownOwnerResolved"); return; }
            if (!start && (pid < 0 || old.Owner?.Pid == pid)) { Count("threadDCStartObservationNotCreation"); return; }
            Invalidate(t => t == old); old.Stop = time; threads.Remove(tid); Count("tidReuseOrOwnerChange");
        }
        Invalidate(x => x.Tid == tid);
        var t = new Thr { Tid = tid, Key = $"{tid}#{Next(tgen, tid)}", Origin = start ? "ThreadStart" : "ThreadDCStart observation; creation unknown", Owner = pid >= 0 ? Observe(pid) : null, Start = start ? time : null };
        threads[tid] = t;
    }
    public void StopThread(int tid, long time, int pid = -1)
    {
        Count("threadStop");
        if (threads.Remove(tid, out var t))
        {
            if (t.Owner != null && pid >= 0 && t.Owner.Pid != pid) { t.Stop = time; Invalidate(x => x == t); Count("threadStopOwnerConflict"); return; }
            if (t.Owner == null && pid >= 0) { t.Owner = Observe(pid); Count("ownerResolvedAtThreadStop"); }
            t.Stop = time; t.PendingFinalOut = true; Count("stoppedLifePendingActualSwitchOut");
        }
        else Count("unmatchedThreadStop");
    }
    Thr Resolve(int tid, int pid, string name, long time)
    {
        if (tid == 0) return new Thr { Tid = 0, Key = "idle", Origin = "per-CPU idle", Owner = Observe(0, "Idle") };
        if (threads.TryGetValue(tid, out var t))
        {
            if (t.Owner == null && pid >= 0) { t.Owner = Observe(pid, name); Count("ownerResolvedAtSwitchInSameLife"); return t; }
            if (pid < 0 || t.Owner?.Pid == pid) return t;
            t.Stop = time; Invalidate(x => x == t); threads.Remove(tid); Count("tidOwnerConflictNewGeneration");
        }
        Invalidate(x => x.Tid == tid);
        t = new Thr { Tid = tid, Key = $"{tid}#{Next(tgen, tid)}", Origin = "CSwitch owner observation; creation unknown", Owner = pid >= 0 ? Observe(pid, name) : null };
        threads[tid] = t; return t;
    }
    public void Switch(int cpu, long time, int oldTid, int oldPid, int newTid, int newPid, string name = "", bool negative = false)
    {
        Switches++;
        if (negative || cpu < 0 || cpu >= cpus || oldTid < 0 || newTid < 0) { Count(negative ? "negativeEventTime" : "invalidCSwitchFields"); open.Remove(cpu); return; }
        if (lastCpu.TryGetValue(cpu, out var last) && time < last) { Count("negativePerCpuTimeDelta"); open.Remove(cpu); lastCpu[cpu] = time; return; }
        lastCpu[cpu] = time;
        if (open.Remove(cpu, out var o))
        {
            if (o.Thread.Owner == null && oldPid >= 0 && o.Thread.Tid == oldTid) { o.Thread.Owner = Observe(oldPid); Count("ownerResolvedAtSwitchOutSameLife"); }
            bool lifetime = o.Thread.Tid == 0 || (threads.TryGetValue(o.Thread.Tid, out var active) && active == o.Thread) || (o.Thread.PendingFinalOut && !threads.ContainsKey(o.Thread.Tid) && o.Thread.Stop <= time);
            bool owner = oldPid < 0 || o.Thread.Owner == null || o.Thread.Owner.Pid == oldPid;
            if (o.Thread.Tid == oldTid && lifetime && owner && time >= o.A)
            {
                long a = Math.Max(begin, o.A), b = Math.Min(end, time);
                if (a < b)
                {
                    if (Runs.Count >= 2_000_000) throw new InvalidDataException("Peak interval memory bound exceeded.");
                    Runs.Add(new Run(cpu, a, b, o.A, time, o.Thread));
                }
            }
            else Count("mismatchedSwitchOutGap");
            if (o.Thread.PendingFinalOut) { o.Thread.PendingFinalOut = false; Count("pendingStoppedLifeReachedRealSwitchOut"); }
        }
        else Count("unpairedSwitchOutNoInventedFirstEdge");
        if (newTid != 0)
        {
            foreach (var duplicate in open.Where(x => x.Value.Thread.Tid == newTid).ToArray()) { open.Remove(duplicate.Key); Count("concurrentTidAmbiguity"); }
        }
        var thread = Resolve(newTid, newPid, name, time);
        open[cpu] = new Open(time, thread);
    }
    public void Interrupt(int cpu, long endTime, double elapsedMs, string kind)
    {
        if (kind == "DPC") Dpcs++; else Isrs++;
        if (cpu < 0 || cpu >= cpus || !double.IsFinite(elapsedMs) || elapsedMs < 0 || elapsedMs > 86_400_000) { Count("invalidInterruptDurationOrCpu"); return; }
        long ticks = checked((long)Math.Round(elapsedMs * 10000, MidpointRounding.AwayFromZero));
        long a = Math.Max(begin, endTime - ticks), b = Math.Min(end, endTime);
        if (a >= b) return;
        if (!Interrupts.TryGetValue(cpu, out var list)) Interrupts[cpu] = list = new();
        if (list.Count >= 1_000_000) throw new InvalidDataException("Interrupt memory bound exceeded.");
        list.Add(new Irq(a, b, kind));
    }
    public void Sample(int cpu, long time, int pid, int tid, ulong ip, int count, bool dpc, bool isr)
    {
        if (time < begin || time >= end) return;
        string key = $"cpu={cpu};pid={pid};tid={tid};ip=0x{ip:x};payloadCount={count};DPC={dpc};ISR={isr}";
        if (!Samples.ContainsKey(key) && Samples.Count >= 100_000) { SamplesTruncated = true; return; }
        Samples[key] = Samples.GetValueOrDefault(key) + 1;
    }
    public static List<Range> Union(IEnumerable<Range> ranges)
    {
        var merged = new List<Range>();
        foreach (var r in ranges.OrderBy(x => x.A).ThenBy(x => x.B))
        {
            if (merged.Count == 0 || r.A > merged[^1].B) merged.Add(r);
            else merged[^1] = new Range(merged[^1].A, Math.Max(merged[^1].B, r.B));
        }
        return merged;
    }
    Dictionary<int, List<Range>>? irqCache;
    Dictionary<int, List<Range>> IrqUnion() => irqCache ??= Interrupts.ToDictionary(x => x.Key, x => Union(x.Value.Select(i => new Range(i.A, i.B))));
    static long Overlap(Run r, List<Range>? irq)
    {
        if (irq == null) return 0;
        long ticks = 0;
        foreach (var i in irq) { if (i.B <= r.A) continue; if (i.A >= r.B) break; ticks += Math.Max(0, Math.Min(r.B, i.B) - Math.Max(r.A, i.A)); }
        return ticks;
    }
    public object RunEvidence(Run r)
    {
        var p = r.Thread.Owner;
        long interrupt = Overlap(r, IrqUnion().GetValueOrDefault(r.Cpu));
        return new { cpu = r.Cpu, pid = p?.Pid, processKey = p?.Key ?? "UNKNOWN", processName = p?.Name ?? "UNKNOWN", tid = r.Thread.Tid, threadKey = r.Thread.Key, ownerEvidence = p?.Origin ?? "UNKNOWN_OWNER", parentPid = p?.Parent, parentKey = p?.ParentKey, startUtc = Program.Iso(r.A), endUtc = Program.Iso(r.B), startUtc100ns = r.A.ToString(), endUtc100ns = r.B.ToString(), actualSwitchInUtc = Program.Iso(r.RawA), actualSwitchOutUtc = Program.Iso(r.RawB), clippedAtWindowStart = r.A != r.RawA, clippedAtWindowEnd = r.B != r.RawB, scheduled100ns = (r.B - r.A).ToString(), scheduledMs = (r.B - r.A) / 10000m, interruptUnion100ns = interrupt.ToString(), interruptUnionMs = interrupt / 10000m, adjustedEstimateMs = (r.B - r.A - interrupt) / 10000m };
    }
    public Result Finish(Capture? input = null, int lost = 0)
    {
        var coverage = new List<object>(); bool complete = Switches > 0;
        for (int cpu = 0; cpu < cpus; cpu++)
        {
            var intervals = Union(Runs.Where(r => r.Cpu == cpu).Select(r => new Range(r.A, r.B)));
            var gaps = new List<object>(); long cursor = begin, covered = 0;
            foreach (var r in intervals) { if (r.A > cursor) gaps.Add(new { startUtc = Program.Iso(cursor), endUtc = Program.Iso(r.A), ms = (r.A - cursor) / 10000m }); covered += r.B - r.A; cursor = r.B; }
            if (cursor < end) gaps.Add(new { startUtc = Program.Iso(cursor), endUtc = Program.Iso(end), ms = (end - cursor) / 10000m });
            if (covered != end - begin) complete = false;
            coverage.Add(new { cpu, coveredMs = covered / 10000m, requestedMs = (end - begin) / 10000m, gaps, firstAcceptedSwitchInUtc = intervals.Count == 0 ? null : Program.Iso(intervals[0].A), lastAcceptedSwitchOutUtc = intervals.Count == 0 ? null : Program.Iso(intervals[^1].B) });
        }
        var unions = IrqUnion();
        Ranking[] Rank(bool byThread) => Runs.GroupBy(r => (p: r.Thread.Owner?.Key ?? "UNKNOWN", t: byThread ? r.Thread.Key : ""))
            .Select(g => { var first = g.First(); var p = first.Thread.Owner; long raw = g.Sum(r => r.B - r.A); long irq = g.Sum(r => Overlap(r, unions.GetValueOrDefault(r.Cpu))); return new Ranking(p?.Pid ?? -1, p?.Key ?? "UNKNOWN", p?.Name ?? "UNKNOWN", p?.Parent ?? -1, p?.ParentKey, byThread ? first.Thread.Tid : null, byThread ? first.Thread.Key : null, p?.Origin ?? "UNKNOWN_OWNER", raw / 10000m, irq / 10000m, (raw - irq) / 10000m, raw / 10000m / (wall * cpus) * 100, (raw - irq) / 10000m / (wall * cpus) * 100); }).OrderByDescending(r => r.ScheduledMs).ToArray();
        var procs = Rank(false); var targetThreads = Rank(true).Where(r => r.Pid == target).ToArray();
        var family = new HashSet<string>(procs.Where(r => r.Pid == target).Select(r => r.ProcessKey));
        for (int i = 0; i < allProcesses.Count; i++) { int n = family.Count; foreach (var p in allProcesses) if (p.ParentKey != null && family.Contains(p.ParentKey)) family.Add(p.Key); if (family.Count == n) break; }
        var targetKeys = procs.Where(r => r.Pid == target).Select(r => r.ProcessKey).Distinct().Count();
        bool unknown = Runs.Any(r => r.Thread.Owner == null);
        bool binding = input != null && input.Problems.Count == 0 && targetKeys == 1 && !TargetLifecycleConflict;
        string status = !complete ? "REJECTED_INCOMPLETE_CSWITCH_COVERAGE" : "ANALYZED";
        decimal rawTarget = procs.Where(r => r.Pid == target).Sum(r => r.ScheduledMs), adjustedTarget = procs.Where(r => r.Pid == target).Sum(r => r.AdjustedMs);
        return new Result(status, complete, "UNKNOWN", "UNKNOWN", coverage.ToArray(), procs.Where(r => r.Pid != 0).ToArray(), targetThreads, procs.Where(r => family.Contains(r.ProcessKey)).ToArray(), new { status = "UNKNOWN", targetBinding = binding ? "HELD_IDENTITY_CONSISTENT_WITH_OBSERVED_WINDOW" : "UNKNOWN", targetKeys, TargetLifecycleConflict, inputProblems = input?.Problems, counterMs = input?.CounterMs, scheduledTargetMs = rawTarget, interruptAdjustedTargetEstimateMs = adjustedTarget, counterMinusRawMs = input == null ? (decimal?)null : input.CounterMs - rawTarget, counterMinusAdjustedMs = input == null ? (decimal?)null : input.CounterMs - adjustedTarget, note = "Residency includes interrupts; union-adjusted duration is an estimate. Counter reads/UTC/QPC are sequential and counter quantization and unobserved kernel work prevent exact reconciliation. DCStart is never OS birth." }, new { Switches, Dpcs, Isrs, EventsLost = lost, unknownOwners = unknown, Counts, openRunsWithoutInventedFinalEdge = open.Count, idleScheduledMs = procs.Where(r => r.Pid == 0).Sum(r => r.ScheduledMs), interruptUnionMs = unions.Values.Sum(v => v.Sum(r => r.B - r.A)) / 10000m, adjustmentEvidence = Dpcs > 0 && Isrs > 0 ? "DPC_AND_ISR_EVENTS_OBSERVED" : "UNKNOWN_MISSING_INTERRUPT_EVENT_CLASS", familyMapping = "Observed parent-instance mapping only; missing/rundown parent generation can leave descendants unknown", limits = "No exact claim: inspect loss, each CPU's coverage, unknown ownership, identity and interrupt evidence separately; zero loss alone is insufficient" });
    }
}




