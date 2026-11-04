using System.Text.Json;

static class SelfTests
{
    static int assertions;
    static readonly long Epoch = Program.Utc("2026-11-02T15:00:00.0000000Z");
    static long T(int ms) => Epoch + ms * 10000L;
    static void Check(bool ok, string name) { assertions++; if (!ok) throw new Exception("SELFTEST FAIL: " + name); }
    static JsonElement J(object value) => JsonSerializer.SerializeToElement(value);
    public static int RunLiveness()
    {
        try
        {
            string birth = new DateTime(Epoch - 10000, DateTimeKind.Utc).ToFileTimeUtc().ToString();
            Capture Case(object held)
            {
                var c = new Capture { Baseline = J(new { }), Selection = "liveness-boundary", Begin = Epoch, Identity = J(new { birthFileTime = birth, startUtc = Program.Iso(Epoch - 10000), diskImageSha256 = "abc" }), Peak = J(new { cpu100ns = "100" }), Final = J(new { finalTargetIdentity = held, finalHeldBirthVerified = true, finalDiskImageSha256 = "abc" }) };
                c.ValidateIdentity(); return c;
            }
            Check(Case(new { birthFileTime = birth, exitFileTime = "987", cpu100ns = "100", livenessSource = "HELD_SYNCHRONIZABLE_HANDLE_WAIT", waitStatus = 258, alive = true }).Problems.Count == 0, "live wait admits despite undefined nonzero exit-time diagnostic");
            Check(Case(new { birthFileTime = birth, exitFileTime = "0", cpu100ns = "100", livenessSource = "HELD_SYNCHRONIZABLE_HANDLE_WAIT", waitStatus = 0, alive = false }).Problems.Contains("FINAL_TARGET_NOT_ALIVE"), "signaled held handle rejects even if raw exit diagnostic zero");
            Check(Case(new { birthFileTime = birth, exitFileTime = "0", cpu100ns = "100" }).Problems.Contains("FINAL_TARGET_LIVENESS_UNPROVED"), "legacy exit zero without wait remains unproved");
            Check(Case(new { birthFileTime = birth, exitFileTime = "0", cpu100ns = "100", livenessSource = "HELD_SYNCHRONIZABLE_HANDLE_WAIT", waitStatus = 4294967295L, alive = true }).Problems.Contains("FINAL_TARGET_LIVENESS_UNPROVED"), "failed wait cannot manufacture liveness");
            Console.WriteLine(JsonSerializer.Serialize(new { status = "PASS", assertions, scope = "Only final held-process liveness evidence; prior20 unchanged cases NOT_RUN", productAcceptance = false })); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.ToString()); return 1; }
    }
    public static int Run()
    {
        try
        {
            // Hand-calculated 2-CPU [2,8) ms: owner10=9ms, owner20=3ms;
            // CPU0 IRQ union [3,6) subtracts 2ms from owner10 and 1ms from owner20.
            var m = new Model(T(2), T(8), 2, 6, 10);
            m.Switch(0, T(0), 0, 0, 1, 10); m.Switch(0, T(5), 1, 10, 2, 20); m.Switch(0, T(10), 2, 20, 0, 0);
            m.Switch(1, T(1), 0, 0, 3, 10); m.Switch(1, T(9), 3, 10, 0, 0);
            m.Interrupt(0, T(6), 3, "DPC"); m.Interrupt(0, T(5), 1, "ISR");
            m.Interrupt(1, T(12), 1, "ISR"); // outside window: no subtraction on CPU1
            var r = m.Finish(); var ten = r.WholeMachineProcesses.Single(x => x.Pid == 10); var twenty = r.WholeMachineProcesses.Single(x => x.Pid == 20);
            Check(r.CoverageComplete && m.Runs.Count == 3, "clipped real edges cover both CPUs");
            Check(ten.ScheduledMs == 9 && twenty.ScheduledMs == 3 && ten.PercentAllMachine == 75, "known totals / all-machine denominator");
            Check(ten.AdjustedMs == 7 && twenty.AdjustedMs == 2, "nested DPC ISR union / per-CPU subtraction");
            Check(r.TargetThreads.Single(x => x.Tid == 1).AdjustedMs == 1 && r.TargetThreads.Single(x => x.Tid == 3).AdjustedMs == 6, "target thread known totals");
            Check(r.Precision == "UNKNOWN" && r.CounterReconciliation == "UNKNOWN", "no false exact counter claim");
            var gap = new Model(T(2), T(8), 1, 6, 10);
            gap.Switch(0, T(0), 0, 0, 1, 10); gap.Switch(0, T(4), 999, 10, 2, 20); gap.Switch(0, T(10), 2, 20, 0, 0);
            Check(!gap.Finish().CoverageComplete && gap.Runs.Sum(x => x.B-x.A) == 40000, "mismatched switchout leaves explicit gap; never bridges");
            var partial = new Model(T(2), T(8), 2, 6, 10);
            partial.Switch(0, T(3), 0, 0, 1, 10); partial.Switch(0, T(7), 1, 10, 0, 0);
            Check(!partial.Finish().CoverageComplete && partial.Runs.Sum(x => x.B-x.A) == 40000, "missing CPU / partial start and final edges rejected");
            Check(new Model(T(2), T(8), 1, 6, 10).Finish().Status.StartsWith("REJECTED"), "missing CSwitch is explicit rejection");
            var reuse = new Model(T(0), T(12), 1, 12, 10);
            reuse.Process(10, 4, "first", T(0), true); reuse.Thread(7, 10, T(0), true);
            reuse.Switch(0, T(0), 0, 0, 7, 10); reuse.Switch(0, T(4), 7, 10, 8, 10);
            reuse.StopThread(8, T(5)); reuse.StopProcess(10, T(5));
            reuse.Process(10, 4, "second", T(6), true); reuse.Thread(7, 10, T(6), true);
            reuse.Switch(0, T(6), 8, 10, 7, 10); reuse.Switch(0, T(9), 7, 10, 0, 0);
            var reused = reuse.Finish().WholeMachineProcesses.Where(x => x.Pid == 10).ToArray();
            Check(reused.Length == 2 && reused.Sum(x => x.ScheduledMs) == 7 && reused[0].ProcessKey != reused[1].ProcessKey, "PID reuse keeps distinct instance totals; stop clears open run");
            Check(reuse.Runs.Where(x => x.Thread.Tid == 7).Select(x => x.Thread.Key).Distinct().Count() == 2, "TID reuse keeps distinct lifetime attribution");
            var unknown = new Model(T(0), T(5), 1, 5, 10);
            unknown.Switch(0, T(0), 0, 0, 9, -1); unknown.Switch(0, T(5), 9, -1, 0, 0);
            Check(unknown.Finish().WholeMachineProcesses.Single().ProcessKey == "UNKNOWN", "unknown owner retained explicitly");
            unknown.Thread(9,10,T(6),false);
            Check(unknown.Finish().WholeMachineProcesses.Single().Pid == 10 && unknown.Runs.Single().Thread.Start == null, "late rundown resolves current observed lifetime without fabricating creation");
            var pending = new Model(T(0), T(5), 1, 5, 10);
            pending.Switch(0,T(0),0,0,77,10); pending.StopThread(77,T(2),10); pending.Switch(0,T(5),77,10,0,0);
            Check(pending.Finish().CoverageComplete && pending.Runs.Single().B==T(5) && pending.Finish().TargetThreads.Single().ScheduledMs==5,"ThreadStop before real switch-out preserves actual final boundary");
            var stoppedReuse = new Model(T(0),T(5),1,5,10);
            stoppedReuse.Switch(0,T(0),0,0,77,10); stoppedReuse.StopThread(77,T(2),10); stoppedReuse.Thread(77,20,T(3),true); stoppedReuse.Switch(0,T(5),77,20,0,0);
            Check(stoppedReuse.Runs.Count==0 && !stoppedReuse.Finish().CoverageComplete,"ThreadStop followed by TID birth invalidates pending old life");
            var rundownReuse = new Model(T(0),T(6),1,6,10);
            rundownReuse.Switch(0,T(0),0,0,88,-1); rundownReuse.Switch(0,T(2),88,-1,0,0); rundownReuse.StopThread(88,T(2));
            rundownReuse.Thread(88,20,T(3),true); rundownReuse.Switch(0,T(3),0,0,88,20); rundownReuse.Switch(0,T(5),88,20,0,0); rundownReuse.Thread(88,20,T(6),false);
            Check(rundownReuse.Runs.First().Thread.Owner==null && rundownReuse.Finish().WholeMachineProcesses.Single(x=>x.Pid==20).ScheduledMs==2,"late end rundown never attributes prior stopped reused life");
            var resolve = new Model(T(0),T(4),1,4,10);
            resolve.Switch(0,T(0),0,0,99,-1); resolve.Switch(0,T(2),99,-1,0,0); resolve.Switch(0,T(3),0,0,99,10); resolve.Switch(0,T(4),99,10,0,0);
            Check(resolve.Runs.Where(x=>x.Thread.Tid==99).Select(x=>x.Thread.Key).Distinct().Count()==1 && resolve.Finish().TargetThreads.Single().ScheduledMs==3,"switch-in known owner resolves same uninterrupted unknown life");
            var negative = new Model(T(0), T(5), 1, 5, 10);
            negative.Switch(0, T(0), 0, 0, 1, 10); negative.Switch(0, T(3), 1, 10, 2, 10, negative:true); negative.Switch(0, T(5), 2, 10, 0, 0);
            Check(negative.Runs.Count == 0 && !negative.Finish().CoverageComplete, "negative time clears CPU state");
            var rows = new Dictionary<long, JsonElement>();
            const string birth = "134381052000000000";
            for (int i = 0; i < 5; i++) rows[95+i] = J(new { sample = 95+i, atUtc = Program.Iso(T(i*250)), pid=10, birthFileTime=birth, qpc=(9_007_199_254_740_993L+i*2_500_000L).ToString(), qpcFrequency=10000000, wallMs=1000+i*250, cpu100ns=(90_071_992_547_409_930L+i*100000L).ToString(), windowMs=1000, cpuPercentMachine1s=2 });
            var c = Capture.Window(rows, 99, 10, 2, birth);
            Check(Program.I(c.Baseline,"sample")==95 && c.CounterMs==40 && c.Percent==2 && c.Begin==Epoch, "exact sample-4 / >2^53 counter and QPC / UTC100ns");
            rows.Remove(95); bool missing=false; try { Capture.Window(rows,99,10,2,birth); } catch(InvalidDataException) { missing=true; }
            Check(missing,"missing exact sample-4 cannot substitute another row");
            c.Identity = J(new { birthFileTime=birth, startUtc=DateTime.FromFileTimeUtc(long.Parse(birth)).ToString("O"), diskImageSha256="abc" });
            c.Final = J(new { finalTargetIdentity=new { birthFileTime="1", exitFileTime="2", cpu100ns="1" }, finalHeldBirthVerified=false, finalDiskImageSha256="changed" });
            c.ValidateIdentity();
            Check(c.Problems.Contains("FINAL_BIRTH_MISMATCH") && c.Problems.Contains("FINAL_TARGET_LIVENESS_UNPROVED") && c.Problems.Contains("FINAL_DISK_HASH_MISSING_OR_CHANGED") && c.Problems.Contains("FINAL_COUNTER_MISSING_OR_BACKWARDS"), "explicit final birth/exit/hash/counter negative evidence");
            Console.WriteLine(JsonSerializer.Serialize(new {status="PASS", assertions, meaning="Local analyzer unit self-test; not a product fixture or default gate", actualTrace="NOT_TESTED_BY_SYNTHETIC_CASES"})); return 0;
        }
        catch(Exception e) { Console.Error.WriteLine(e.ToString()); return 1; }
    }
}


