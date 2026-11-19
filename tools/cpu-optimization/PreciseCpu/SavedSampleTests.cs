using System.Globalization;
using System.Text.Json;

static class SavedSampleTests
{
    static int checks;
    static readonly string RunRoot = Path.Combine(AppContext.BaseDirectory, "saved-sample-inputs", Guid.NewGuid().ToString("N"));
    static readonly DateTime Epoch = new(2026, 11, 8, 23, 0, 0, DateTimeKind.Utc);
    static void Check(bool value, string label) { checks++; if (!value) throw new Exception(label); }
    static void Invalid(Action run, string label) { bool rejected = false; try { run(); } catch (InvalidDataException) { rejected = true; } Check(rejected, label); }
    static string Make(string label, bool trigger = false, bool corruptBirth = false, bool missingBaseline = false)
    {
        var root = Path.Combine(RunRoot, label);
        if (Directory.Exists(root)) throw new IOException("Fresh saved-sample inputs required.");
        Directory.CreateDirectory(root);
        var birth = Epoch.AddSeconds(-1).ToFileTimeUtc().ToString(CultureInfo.InvariantCulture);
        var counts = new long[] { 0, 1, 2, 3, 4, 100, 101, 102, 103 };
        var lines = new List<string>();
        for (int i = 0; i < counts.Length; i++)
        {
            if (missingBaseline && i == 0) continue;
            decimal percent = i < 4 ? 0 : (counts[i] - counts[i - 4]) / 2m;
            lines.Add(JsonSerializer.Serialize(new { sample = i, atUtc = Epoch.AddMilliseconds(i * 250).ToString("O"), pid = 10, birthFileTime = corruptBirth && i == 4 ? "1" : birth, qpc = (1_000_000L + i * 2_500_000L).ToString(), qpcFrequency = 10_000_000, wallMs = 10_000 + i * 250, cpu100ns = (counts[i] * 100_000L).ToString(), windowMs = 1000, cpuPercentMachine1s = percent }));
        }
        File.WriteAllLines(Path.Combine(root, "CPU.jsonl"), lines);
        File.WriteAllText(Path.Combine(root, "IDENTITY.json"), JsonSerializer.Serialize(new { pid = 10, logicalProcessors = 2, birthFileTime = birth, startUtc = Epoch.AddSeconds(-1).ToString("O"), diskImageSha256 = "fixture-only" }));
        File.WriteAllText(Path.Combine(root, "CAPTURE.json"), JsonSerializer.Serialize(new { finalTargetIdentity = new { birthFileTime = birth, exitFileTime = "0", cpu100ns = "10300000", livenessSource = "HELD_SYNCHRONIZABLE_HANDLE_WAIT", waitStatus = 258, alive = true }, finalHeldBirthVerified = true, finalHeldAliveVerified = true, finalDiskImageSha256 = "fixture-only" }));
        if (trigger) File.WriteAllText(Path.Combine(root, "TRIGGER.json"), JsonSerializer.Serialize(new { sample = 5, atUtc = Epoch.AddMilliseconds(1250).ToString("O"), windowMs = 1000, cpuPercentMachine = 49.5m }));
        return root;
    }
    public static int Run()
    {
        try
        {
            var plain = Make("no-trigger");
            var maximum = Capture.Load(plain, 10, 0);
            Check(Program.I(maximum.Peak, "sample") == 5 && maximum.Percent == 49.5m && maximum.Selection == "maximum valid row after warmup", "legacy maximum selection is unchanged");
            var chosen = Capture.Load(plain, 10, 0, 4);
            Check(Program.I(chosen.Peak, "sample") == 4 && Program.I(chosen.Baseline, "sample") == 0 && chosen.Percent == 2m && chosen.Problems.Count == 0, "explicit selected lower window is exact and identity-valid");
            Check(chosen.Selection == "explicit saved sample (not runtime trigger)", "explicit saved sample cannot be called an actual runtime trigger");
            var triggered = Make("actual-trigger", trigger: true);
            Check(Capture.Load(triggered, 10, 0).Selection == "actual trigger row" && Program.I(Capture.Load(triggered, 10, 0).Peak, "sample") == 5, "legacy runtime trigger remains authoritative without explicit selection");
            var other = Capture.Load(triggered, 10, 0, 4);
            Check(Program.I(other.Peak, "sample") == 4 && other.Selection == "explicit saved sample (not runtime trigger)", "explicit saved selection may examine another row without rewriting runtime trigger");
            Invalid(() => Capture.Load(plain, 10, 0, 999), "missing explicit sample cannot fall back to maximum");
            Invalid(() => Capture.Load(plain, 10, 0, 3), "missing exact sample-minus-four cannot fall back");
            Invalid(() => Capture.Load(plain, 10, 11, 4), "explicit whole window before warmup is rejected");
            Invalid(() => Capture.Load(plain, 10, 0, -1), "negative explicit sample is rejected");
            Invalid(() => Capture.Load(Make("changed-birth", corruptBirth: true), 10, 0, 4), "changed row instance is rejected");
            Invalid(() => Capture.Load(Make("missing-baseline", missingBaseline: true), 10, 0, 4), "missing baseline row is not substituted");
            Console.WriteLine(JsonSerializer.Serialize(new { status = "PASS_EXPLICIT_SAVED_SAMPLE", checks, actualTraceAnalyzed = false, osOrProductActions = false, inputRoot = RunRoot }));
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.ToString()); return 1; }
    }
}
