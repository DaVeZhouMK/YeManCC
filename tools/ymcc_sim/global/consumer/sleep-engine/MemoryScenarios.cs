using System.IO;
using System.Reflection;
using System.Text.Json;

internal static partial class SleepEngineProbe
{
    static JsonElement ActualSnapshot() => Element(Invoke(EngineType!, "Snapshot", Engine!)!);
    static object? MemoryInvoke(string method, object?[]? arguments = null, object? publicInput = null, string? expectedCode = null)
    {
        var id = $"{Scope}:{++Sequence}";
        var before = ActualSnapshot(); object? result = null; string? code = null; int? status = null;
        try { result = Invoke(EngineType!, method, Engine, arguments ?? Array.Empty<object?>()); }
        catch (Exception error)
        {
            while (error is TargetInvocationException && error.InnerException is not null) error = error.InnerException;
            if (error.GetType().Name != "FanApiException") throw;
            code = (string?)Prop(error, "Code"); status = (int?)Prop(error, "StatusCode");
        }
        var after = ActualSnapshot();
        Evidence.Add(new { input = new { eventId = id, provenance = "source-derived-fixture", method, fields = publicInput },
            consumer = new { type = "YeManFanHost.FanHostEngine", entry = method, transport = "none-direct-owned-memory", mainInvoked = false,
                httpDispatchExercised = false, apiAuthenticationExercised = false, generationAdmissionExercised = false },
            state = new { before = SafeState(before), after = SafeState(after), observation = "authoritative-Snapshot-under-owned-engine-gate" },
            feedback = new { status = code is null ? "returned" : "rejected", engineErrorCode = code, engineErrorStatus = status, hardwareReceipt = false } });
        AssertBoundary();
        Check(method + " direct engine outcome", expectedCode is null ? code is null : code == expectedCode, new { actualCode = code, expectedCode });
        return result;
    }
    static async Task RunMemoryScenarios(string repoRoot)
    {
        RepoRoot = repoRoot;
        var fixture = Path.Combine(repoRoot, "tools", "FanFinalizeContractSelfTest", "Program.cs");
        FixtureIdentities.Add(new { path = fixture, sha256 = Hash(fixture) });
        Check("memory mode never constructs server, client or socket", Server is null && Client is null && BaseUrl == "");
        await Case("memory-open-and-lease", "memory", fixture, () =>
        {
            var handshake = Element(MemoryInvoke("Handshake")!);
            Check("memory handshake is strict zero-write mock", Bool(handshake, "mockZeroHardwareEvidence"));
            MemoryInvoke("Open"); MemoryInvoke("OpenEvents");
            var granted = Element(MemoryInvoke("AcquireControl")!);
            LeaseId = Text(granted, "leaseId"); Check("owned memory lease exists", !string.IsNullOrWhiteSpace(LeaseId));
            return Task.CompletedTask;
        }, null, "actual engine only;no HTTP/auth/generation admission proof");
        await Case("memory-published-snapshot-fallback", "memory", fixture, async () =>
        {
            var actual = ActualSnapshot();
            var gate = EngineType!.GetField("gate", All)!.GetValue(Engine)!;
            using var held = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
            var holder = Task.Run(() => { lock (gate) { held.Set(); if (!release.Wait(3000)) throw new TimeoutException("OWNED_MEMORY_GATE_HOLDER_TIMEOUT"); } });
            JsonElement published;
            Check("self-test holds only its own engine gate", held.Wait(1000));
            try { published = Element(Invoke(EngineType!, "SnapshotForRead", Engine!)!); }
            finally { release.Set(); await holder; }
            Check("already-granted lease is visible in authoritative state", Number(actual, "leaseGeneration") > 0);
            Evidence.Add(new { input = new { eventId = $"{Scope}:{++Sequence}", provenance = "source-derived-fixture", gateFixture = "FanFinalizeContractSelfTest-owned-gate-pattern" },
                consumer = new { entry = "SnapshotForRead", transport = "none-direct-owned-memory", cachedFallbackAllowed = true },
                state = new { before = SafeState(actual), after = SafeState(ActualSnapshot()), publishedFallback = SafeState(published) },
                feedback = new { status = "observed", physicalReceipt = false, cachedViewIsNotAuthoritativeMutationProof = true } });
            Check("published fallback does not invent software writes", !Bool(published, "mockControlEnabled"));
        }, null, "bounded cached snapshot can predate an already-granted lease;not a mutation by the later bad curve");
        await Case("memory-invalid-curve-keeps-actual-state", "memory", fixture, () =>
        {
            var before = Stable(ActualSnapshot());
            var bad = Element(new { leaseId = LeaseId, nodes = new[] { new { tempC = -1, dutyPercent = 200 } } });
            MemoryInvoke("Enable", new object?[] { bad }, new { nodes = new[] { new { tempC = -1, dutyPercent = 200 } } }, "INVALID_CURVE");
            Check("invalid curve does not mutate authoritative lease/control state", Stable(ActualSnapshot()) == before);
            return Task.CompletedTask;
        });
        await Case("memory-control-release-close", "memory", fixture, () =>
        {
            var body = Element(new { leaseId = LeaseId, nodes = new[] { new { tempC = 0, dutyPercent = 0 }, new { tempC = 55, dutyPercent = 30 }, new { tempC = 75, dutyPercent = 60 }, new { tempC = 90, dutyPercent = 100 } } });
            MemoryInvoke("Enable", new object?[] { body }, new { fourNodeCurve = true });
            Check("memory control is software only", Bool(ActualSnapshot(), "mockControlEnabled") && !Bool(ActualSnapshot(), "hardwareWritesObserved"));
            MemoryInvoke("Release", new object?[] { Element(new { leaseId = LeaseId }) });
            LeaseId = null; MemoryInvoke("Close");
            Check("owned engine reaches Stopped without physical receipt", Text(ActualSnapshot(), "state") == "Stopped" && !Bool(ActualSnapshot(), "oemPhysicalOwnershipConfirmed"));
            return Task.CompletedTask;
        });
        Check("pure memory run opened no HTTP or server", HttpRequestCount == 0 && Server is null && Client is null && BaseUrl == "");
    }
}

