using System.IO;
using System.Text.Json;

internal static partial class SleepEngineProbe
{
    internal sealed record ScenarioResult(string Id, string Mode, string Status, string Fixture, string FixtureSha256,
        string[] RequestEventIds, string? LegacyExpectation, string[] Limitations, string? Failure);
    static readonly List<ScenarioResult> Scenarios = new();
    static readonly List<object> CompatibilityDifferences = new();
    static readonly List<object> FixtureIdentities = new();
    static string RepoRoot = "";
    static string? LeaseId;

    static string FixtureFile(string name) => Path.Combine(RepoRoot, "tools", "ymcc_sim", "domains", "sleep", name);
    static string Stable(JsonElement state) => string.Join("|", new[] { "state", "powerState", "powerOperationGeneration", "powerOperationAttempt", "mockControlSequence", "leaseGeneration", "mockControlEnabled" }.Select(key => state.TryGetProperty(key, out var value) ? value.ToString() : "absent"));
    static void Expect(Receipt response, int status, string? code = null) =>
        Check($"{Scope} HTTP {status} {code}", response.Status == status && (code is null || response.Code == code), new { actualStatus = response.Status, actualCode = response.Code });
    static async Task Case(string id, string mode, string fixture, Func<Task> body, string? legacy = null, params string[] limitations)
    {
        Scope = id; var first = Sequence + 1; string? failure = null;
        try { await body(); }
        catch (Exception error) { failure = error.Message.StartsWith("CHECK_FAILED:", StringComparison.Ordinal) ? error.Message : error.GetType().Name; throw; }
        finally { Scenarios.Add(new(id, mode, failure is null ? "PASS" : "FAIL", fixture, Hash(fixture), Enumerable.Range(first, Math.Max(0, Sequence - first + 1)).Select(number => $"{id}:{number}").ToArray(), legacy, limitations, failure)); }
    }
    static async Task Session(bool curve = false)
    {
        Expect(await Send("POST", "/api/open"), 200);
        Expect(await Send("POST", "/api/open-events"), 200);
        var acquired = await Send("POST", "/api/acquire-control"); Expect(acquired, 200);
        LeaseId = acquired.Body.GetProperty("lease").GetProperty("leaseId").GetString();
        Check("fresh synthetic control lease is present", !string.IsNullOrWhiteSpace(LeaseId));
        if (curve)
        {
            var enabled = await Send("POST", "/api/enable", new { leaseId = LeaseId, nodes = new[] { new { tempC = 0, dutyPercent = 0 }, new { tempC = 55, dutyPercent = 30 }, new { tempC = 75, dutyPercent = 60 }, new { tempC = 90, dutyPercent = 100 } } });
            Expect(enabled, 200);
            Check("real strict-zero mock records software control separately", Bool(enabled.State, "mockControlEnabled") && !Bool(enabled.State, "hardwareWritesObserved"));
        }
    }
    static async Task Release()
    {
        if (LeaseId is null) return;
        var released = await Send("POST", "/api/release-control", new { leaseId = LeaseId }); Expect(released, 200);
        Check("release is not a physical readback", Text(released.State, "oemRestoreEvidence") != "hc-default-table-readback-confirmed" && !Bool(released.State, "oemPhysicalOwnershipConfirmed"));
        LeaseId = null;
    }
    static async Task CurveFaultCycle(int quietMs = 0, bool allowConvergedResume = false)
    {
        await Session(true);
        var generation = ++Generation;
        var suspended = await Send("POST", "/api/suspend", Power(generation)); Expect(suspended, 200);
        Check("curve suspend cannot invent OEM completion with null real backend", Text(suspended.State, "state") == "FaultLocked" && !Bool(suspended.State, "oemRestoreConfirmed"));
        if (quietMs > 0) await Task.Delay(quietMs);
        var resumed = await Send("POST", "/api/resume", Power(generation));
        if (allowConvergedResume) Check("after real quiet interval the engine reports convergence or explicit refusal", resumed.Status is 200 or 409 && Text(resumed.State, "oemRestoreEvidence") != "hc-default-table-readback-confirmed");
        else { Expect(resumed, 409, "HC_RESUME_REBUILD_FAILED"); Check("rejected resume keeps the same fault", Text(resumed.State, "state") == "FaultLocked"); }
        await Release();
    }
    static async Task InvalidUnchanged(string id, string method, string path, object? body, int status, string? code = null, bool authorized = true, string? raw = null)
    {
        var before = Stable(Snapshot()); var response = await Send(method, path, body, authorized, raw); Expect(response, status, code);
        Check(id + " leaves current control and power receipt unchanged", Stable(Snapshot()) == before);
    }
    static async Task RunNegatives()
    {
        var fixture = FixtureFile("sleep_forms.json");
        await Case("negative-malformed-json", "negative", fixture, async () => await InvalidUnchanged("malformed JSON", "POST", "/api/suspend", null, 400, raw: "{broken"));
        await Case("negative-generation-type", "negative", fixture, async () => await InvalidUnchanged("wrong generation type", "POST", "/api/suspend", new { generation = "not-a-number", source = "selftest.native" }, 400));
        await Case("negative-unknown-route", "negative", fixture, async () => await InvalidUnchanged("unknown route", "POST", "/api/not-a-route", new { }, 404, "NOT_FOUND"));
        await Case("negative-invalid-curve", "negative", fixture, async () =>
        {
            await Session();
            await InvalidUnchanged("invalid curve", "POST", "/api/enable", new { leaseId = LeaseId, nodes = new[] { new { tempC = -1, dutyPercent = 200 } } }, 400);
            await Release();
        });
    }
    static async Task RunForms()
    {
        var fixture = FixtureFile("sleep_forms.json");
        var forms = JsonDocument.Parse(File.ReadAllText(fixture)).RootElement.GetProperty("forms").EnumerateArray().Select(row => row.Clone()).ToArray();
        Check("existing fixture contains exactly twenty unique forms", forms.Length == 20 && forms.Select(row => Text(row, "id")).Distinct().Count() == 20);
        foreach (var form in forms)
        {
            var id = Text(form, "id")!; var legacy = Text(form, "expect");
            await Case(id, "forms", fixture, async () =>
            {
                switch (id)
                {
                    case "P1": await InvalidUnchanged(id, "POST", "/api/suspend", Power(++Generation), 401, "API_SESSION_REQUIRED", false); break;
                    case "P2": await InvalidUnchanged(id, "POST", "/api/suspend", new { source = "selftest.native" }, 400, "POWER_GENERATION_REQUIRED"); break;
                    case "P3": await InvalidUnchanged(id, "POST", "/api/suspend", Power(++Generation, "not-native"), 403, "F4_NATIVE_AUTHORITY_REQUIRED"); break;
                    case "P7": await InvalidUnchanged(id, "POST", "/api/suspend", new { generation = ++Generation }, 400, "POWER_GENERATION_REQUIRED"); break;
                    case "P4": { var g = await Cycle(); await InvalidUnchanged(id, "POST", "/api/suspend", Power(g), 409, "POWER_EVENT_STALE"); break; }
                    case "P5": { var g = await Cycle(); await InvalidUnchanged(id, "POST", "/api/suspend", Power(g - 1), 409, "POWER_EVENT_STALE"); break; }
                    case "P6": { var g = await Cycle(); await InvalidUnchanged(id, "POST", "/api/resume", Power(g - 1), 409, "POWER_EVENT_STALE"); break; }
                    case "B1":
                    case "B2":
                    {
                        var g = ++Generation; var suspended = await Send("POST", "/api/suspend", Power(g)); Expect(suspended, 200);
                        Check(id + " reaches Suspended", Text(suspended.State, "state") == "Suspended" && Text(suspended.State, "powerState") == "Suspended");
                        if (id == "B1") await Task.Delay(1500);
                        var resumed = await Send("POST", "/api/resume", Power(g)); Expect(resumed, 200);
                        Check(id + " reaches AwaitingControl/On", Text(resumed.State, "state") == "AwaitingControl" && Text(resumed.State, "powerState") == "On");
                        break;
                    }
                    case "B3":
                    {
                        var g = ++Generation; Expect(await Send("POST", "/api/suspend", Power(g)), 200);
                        var duplicate = await Send("POST", "/api/suspend", Power(g)); Expect(duplicate, 200); Check("duplicate suspend deduplicated", Bool(duplicate.Body, "deduplicated"));
                        var first = await Send("POST", "/api/resume", Power(g)); Expect(first, 200);
                        var second = await Send("POST", "/api/resume", Power(g)); Expect(second, 200);
                        Check("duplicate resume uses the same completed engine attempt", Number(first.State, "powerOperationAttempt") == Number(second.State, "powerOperationAttempt"));
                        break;
                    }
                    case "B4":
                    case "B6": await InvalidUnchanged(id, "POST", "/api/resume", Power(++Generation), 409, "HC_RESUME_RESULT_EXPIRED"); break;
                    case "B5":
                    {
                        var g = await Cycle(); var before = Snapshot(); var duplicate = await Send("POST", "/api/resume", Power(g)); Expect(duplicate, 200);
                        Check("same generation wake receipt is stable", Number(before, "powerOperationAttempt") == Number(duplicate.State, "powerOperationAttempt")); break;
                    }
                    case "C1":
                        await Cycle(); foreach (var ms in new[] { 500, 1000, 2000 }) { await Task.Delay(ms); await Cycle(); } break;
                    case "C2": await Cycle(300); await Task.Delay(300); await Cycle(); break;
                    case "C3":
                    {
                        Expect(await Send("POST", "/api/suspend", Power(++Generation)), 200);
                        var g = ++Generation; var duplicate = await Send("POST", "/api/suspend", Power(g)); Expect(duplicate, 200);
                        Check("new observer generation is absorbed while already suspended", Bool(duplicate.Body, "deduplicated"));
                        Expect(await Send("POST", "/api/resume", Power(g)), 200); break;
                    }
                    case "C4": for (var index = 0; index < 10; index++) { await Cycle(150); if (index != 9) await Task.Delay(150); } break;
                    case "D1": await Session(); await Cycle(500); Check("sleep without curve invalidates control lease", !Snapshot().TryGetProperty("lease", out var lease) || lease.ValueKind == JsonValueKind.Null); LeaseId = null; break;
                    case "D2": await CurveFaultCycle(); break;
                    case "D3":
                    {
                        var g = ++Generation; Expect(await Send("POST", "/api/suspend", Power(g)), 200);
                        Expect(await Send("POST", "/api/close"), 200);
                        Expect(await Send("POST", "/api/resume", Power(g)), 409, "POWER_CLOSE_PENDING");
                        Check("close/later wake keeps Stopped", Text(Snapshot(), "state") == "Stopped"); break;
                    }
                    default: throw new InvalidOperationException("UNMAPPED_EXISTING_FORM");
                }
            }, legacy, id == "B2" ? new[] { "LONG_SLEEP_THRESHOLD_NOT_EXECUTED;compressed edge sequence only" } : id == "D3" ? new[] { "ordered close/later-wake;not-native-scheduler-race" } : Array.Empty<string>());
        }
    }
    static async Task RunTypeSeries()
    {
        var fixture = FixtureFile("lib_sleep_typeseries.ps1");
        await Case("series-S3", "typeseries", fixture, async () =>
        {
            await Session(); var g = ++Generation; Expect(await Send("POST", "/api/suspend", Power(g)), 200);
            await Task.Delay(3000); var mid = await Send("GET", "/api/state"); Check("S3 quiet interval remains suspended", Text(mid.State, "state") == "Suspended");
            Expect(await Send("POST", "/api/resume", Power(g)), 200); LeaseId = null;
        }, "source fixture S3 lease, quiet 3s, resume 200", "software signal semantics;not physical S3");
        await Case("series-S4", "typeseries", fixture, async () =>
        {
            var g = ++Generation; Expect(await Send("POST", "/api/suspend", Power(g)), 200);
            await Task.Delay(25000); var mid = await Send("GET", "/api/state"); Check("S4 25-second quiet interval remains suspended", Text(mid.State, "state") == "Suspended");
            var resumed = await Send("POST", "/api/resume", Power(g));
            Expect(resumed, 200);
            Check("S4 no-curve path is clean software wake without physical restoration", Text(resumed.State, "powerState") == "On" && !Bool(resumed.State, "oemPhysicalOwnershipConfirmed"));
            await CurveFaultCycle(25000, true);
        }, "existing S4a assertion: no-curve quiet 25s resume 200; S4b curve quiet 25s resume 200 or explicit 409", "not physical S4;strict-zero mockWritesObserved replaces legacy physical write assertion");
        await Case("series-S0ix", "typeseries", fixture, async () =>
        {
            var before = Stable(Snapshot()); var first = Sequence;
            await Task.Delay(3000); var after = await Send("GET", "/api/state");
            Check("S0ix fixture emits no classic suspend or resume and keeps state", Stable(after.State) == before && Sequence == first + 1);
        }, "source fixture S0ix emits no classic power edge;quiet 3s;state unchanged", "no platform capability or real S0ix claim");
    }
    static async Task RunRapidWake()
    {
        var fixture = FixtureFile("lib_sleep_rapidwake.ps1");
        foreach (var milliseconds in new[] { 100, 500, 1000, 2000 })
            await Case("rapid-" + milliseconds, "rapidwake", fixture, async () =>
            {
                var g = ++Generation; Expect(await Send("POST", "/api/suspend", Power(g)), 200); await Task.Delay(milliseconds);
                var resumed = await Send("POST", "/api/resume", Power(g)); Expect(resumed, 200);
                Check("rapid no-curve wake leaves no suspended state", Text(resumed.State, "state") != "Suspended");
            }, "source delay " + milliseconds + "ms;no-curve suspend/resume", "software delay only;OS never sleeps");
        await Case("rapid-curve-100", "rapidwake", fixture, () => CurveFaultCycle(100),
            "source 100ms curve case must fail closed without physical readback", "mockControlEnabled replaces legacy fictional hardwareWritesObserved;physical flags stay false");
        await Case("rapid-stress-5", "rapidwake", fixture, async () =>
        {
            var finals = new HashSet<string>();
            for (var index = 0; index < 5; index++)
            {
                var generation = ++Generation;
                Expect(await Send("POST", "/api/suspend", Power(generation)), 200);
                await Task.Delay(100);
                var resumed = await Send("POST", "/api/resume", Power(generation));
                // Preserve the existing rapidwake fixture: after its curve
                // case, an explicit 409 is an honest result, never a rebuild.
                Check("stress resume follows the original 200-or-409 contract", resumed.Status is 200 or 409, new { resumed.Status, resumed.Code });
                finals.Add(Text(resumed.State, "state") ?? "missing");
            }
            Check("five rapid software cycles have one final state", finals.Count == 1 && !finals.Contains("missing"));
        }, "source default 5 rapid repetitions", "no scheduler or physical sleep performance claim");
    }
    static async Task RunSelectedScenarios(string mode, string repoRoot)
    {
        RepoRoot = repoRoot;
        foreach (var name in new[] { "sleep_forms.json", "sleep_types.json", "lib_sleep_typeseries.ps1", "lib_sleep_rapidwake.ps1", "sleep_domain.ps1" })
        {
            var file = FixtureFile(name); FixtureIdentities.Add(new { path = file, sha256 = Hash(file) });
        }
        await RunNegatives();
        if (mode is "all" or "typeseries") await RunTypeSeries();
        if (mode is "all" or "rapidwake") await RunRapidWake();
        if (mode is "all" or "forms") await RunForms();
        if (mode == "probe") { await Cycle(); await CurveFaultCycle(); }
    }
}


