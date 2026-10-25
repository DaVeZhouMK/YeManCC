using System.IO;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

// Exercises the production engine, not a duplicate curve-selection function.
// All engines use the explicit mock backend. Initial curve and successful Close
// are fixtures; no HC/WMI/ACPI calls or native IPC; loopback HTTP is exercised below.
var evidence = Path.GetFullPath(args[0]);
Directory.CreateDirectory(evidence);
Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", Path.Combine(evidence, "logs"));
var assembly = args.Length > 1
    ? new HostTestLoadContext(Path.GetFullPath(args[1])).LoadFromAssemblyPath(Path.GetFullPath(args[1]))
    : Assembly.Load("YeManFanHost");
if (args.Length > 1 && !string.Equals(assembly.Location, Path.GetFullPath(args[1]), StringComparison.OrdinalIgnoreCase))
    throw new InvalidOperationException("Test loaded a different Host assembly than the requested payload");
Type T(string name) => assembly.GetType("YeManFanHost." + name, true)!;
const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
object? Call(object obj, string name, params object?[] arguments)
{
    try {
        var method = obj.GetType().GetMethods(flags).Single(m => m.Name == name && m.GetParameters().Length >= arguments.Length && m.GetParameters().Skip(arguments.Length).All(p => p.IsOptional));
        var supplied = arguments.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - arguments.Length)).ToArray();
        return method.Invoke(obj, supplied);
    }
    catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
}
object? Field(object obj, string name) => obj.GetType().GetField(name, flags)!.GetValue(obj);
void SetField(object obj, string name, object? value) => obj.GetType().GetField(name, flags)!.SetValue(obj, value);
void SetProperty(object obj, string name, object? value) => obj.GetType().GetProperty(name, flags)!.SetValue(obj, value);
string P(object obj, string name) => obj.GetType().GetProperty(name, flags)!.GetValue(obj)?.ToString() ?? "null";
T("Program").GetProperty("IsSelfTestProcess", flags)!.SetValue(null, true);
var diagnosticType = T("HostDiagnosticLog");
diagnosticType.GetProperty("SelfTestCaptureEnabled", flags)!.SetValue(null, true);
List<string> Events() => ((IEnumerable)diagnosticType.GetProperty("SelfTestCaptured", flags)!.GetValue(null)!).Cast<object>().Select(x => (string)x.GetType().GetField("Item1")!.GetValue(x)!).ToList();
void ResetEvents() => diagnosticType.GetMethod("ResetSelfTestCapture", flags)!.Invoke(null, null);
Array Curve(params double[] duties)
{
    var result = Array.CreateInstance(T("FanNode"), duties.Length);
    var temps = new double[] { 0, 44, 78, 100 };
    for (int i = 0; i < duties.Length; i++) result.SetValue(Activator.CreateInstance(T("FanNode"), temps[i], duties[i]), i);
    return result;
}
string CurveJson(object? curve) => JsonSerializer.Serialize(curve, curve?.GetType() ?? typeof(object), new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase });
var checks = new List<object>();
bool ok = true;
void Check(string name, bool passed)
{
    checks.Add(new { name, passed });
    if (!passed) ok = false;
    Console.WriteLine((passed ? "PASS " : "FAIL ") + name);
}
(object engine, object options) NewEngine(int delay = 0)
{
    var argv = new[] { "--mock-handshake", "--mock-resume-rebuild-delay-ms", delay.ToString() };
    var options = T("HostOptions").GetMethod("Parse", flags)!.Invoke(null, new object[] { argv })!;
    var diagnostics = Activator.CreateInstance(diagnosticType, flags, null, new object[] { Path.Combine(evidence, "logs"), 0 }, null)!;
    var engine = Activator.CreateInstance(T("FanHostEngine"), flags, null, new[] { options, diagnostics }, null)!;
    return (engine, options);
}
void SeedActive(object engine, Array curve)
{
    SetField(engine, "activeCurve", curve);
    var state = Field(engine, "state")!;
    SetProperty(state, "ActiveCurve", curve);
    SetProperty(state, "State", "Ready");
    SetProperty(state, "PowerState", "On");
}
// The mock enable records simulated writes. Before Close, declare the fixture's
// actual zero hardware writes, allowing the existing safe Close boundary.
void SafeCloseFixture(object engine) => SetProperty(Field(engine, "state")!, "HardwareWritesObserved", false);
bool Wait(Func<bool> predicate, int ms = 4000)
{
    var timer = Stopwatch.StartNew();
    while (timer.ElapsedMilliseconds < ms) { if (predicate()) return true; Thread.Sleep(10); }
    return predicate();
}
void DisposeFixture(object engine)
{
    SafeCloseFixture(engine);
    SetProperty(Field(engine, "state")!, "HardwareWritesEnabled", false);
    Call(engine, "Dispose");
}
try
{
    var desired = Curve(34, 52, 71, 93);
    var (engine, options) = NewEngine(400);
    SeedActive(engine, desired);
    Call(engine, "SuspendForSystemPower");
    Check("first-sleep-freezes-exact-user-curve", CurveJson(Field(engine, "suspendedCurve")) == CurveJson(desired));
    Check("sleep-clears-live-curve", Field(engine, "activeCurve") is null);
    ResetEvents();
    Call(engine, "ResumeForSystemPower");
    Check("real-resume-worker-enters-delayed-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    Check("interrupted-resume-has-no-live-curve", Field(engine, "activeCurve") is null);
    Call(engine, "SuspendForSystemPower");
    Check("resleep-keeps-frozen-user-curve", CurveJson(Field(engine, "suspendedCurve")) == CurveJson(desired));
    Check("resleep-keeps-write-gate-suspended", P(Call(engine, "Snapshot")!, "State") == "Suspended");
    var firstWorker = (Task)Field(engine, "resumeTask")!;
    Check("superseded-worker-terminates-within-bound", firstWorker.Wait(4000));
    Check("old-worker-cannot-publish-ready", P(Call(engine, "Snapshot")!, "State") == "Suspended");
    Check("superseded-worker-leaves-no-running-flag", Equals(Field(engine, "automaticResumeInProgress"), false));
    // Remove both mock synthetic-curve creation and delay for the next cycle.
    // A missing real retained curve must now fail, rather than be hidden by the
    // existing mock-delay fallback curve.
    SetProperty(options, "MockResumeRebuildDelayMs", 0);
    ResetEvents();
    Call(engine, "ResumeForSystemPower");
    Check("latest-resume-rebuilds-without-renderer", Wait(() => Events().Contains("power.resume-auto-rebuild.success")));
    var after = Call(engine, "SnapshotForRead")!;
    Check("latest-cycle-restores-ready", P(after, "State") == "Ready");
    Check("latest-cycle-restores-exact-user-curve", CurveJson(Field(engine, "activeCurve")) == CurveJson(desired));
    Check("latest-cycle-not-silent-await-user", !Events().Contains("power.resume-sync-completed"));
    Check("committed-resume-clears-frozen-copy", Field(engine, "suspendedCurve") is null);
    Check("committed-resume-revokes-own-placeholder-lease", P(after, "Lease") == "null");
    DisposeFixture(engine);

    var (latest, _) = NewEngine();
    var older = Curve(30, 45, 70, 100);
    SetField(latest, "suspendedCurve", older);
    SeedActive(latest, desired);
    Call(latest, "SuspendForSystemPower");
    Check("newer-live-curve-wins-over-old-frozen-curve", CurveJson(Field(latest, "suspendedCurve")) == CurveJson(desired));
    Check("frozen-copy-does-not-alias-live-array", !ReferenceEquals(Field(latest, "suspendedCurve"), desired));
    var frozen = Field(latest, "suspendedCurve");
    Call(latest, "SuspendForSystemPower");
    Check("duplicate-suspend-is-read-only", ReferenceEquals(frozen, Field(latest, "suspendedCurve")));
    DisposeFixture(latest);

    foreach (var endpoint in new[] { "Disable", "Restore", "Release", "Close" })
    {
        var (off, _) = NewEngine();
        SetField(off, "suspendedCurve", desired);
        var beforeOffState = Field(off, "state")!;
        SetProperty(beforeOffState, "State", "Ready");
        SetProperty(beforeOffState, "PowerState", "On");
        using var empty = JsonDocument.Parse("{}");
        if (endpoint == "Close") Call(off, endpoint); else Call(off, endpoint, empty.RootElement);
        Check(endpoint + "-clears-retained-intent", Field(off, "suspendedCurve") is null);
        if (endpoint != "Close")
        {
            Call(off, "SuspendForSystemPower");
            Call(off, "ResumeForSystemPower");
            Check(endpoint + "-cannot-auto-reenable", P(Call(off, "Snapshot")!, "PowerOperationOutcome") == "await-user" && Field(off, "activeCurve") is null);
        }
        DisposeFixture(off);
    }
    foreach (var endpoint in new[] { "Disable", "Restore", "Release" })
    {
        var (cancelled, _) = NewEngine(200);
        SeedActive(cancelled, desired);
        Call(cancelled, "SuspendForSystemPower");
        ResetEvents();
        Call(cancelled, "ResumeForSystemPower");
        Check(endpoint + "-fixture-enters-pending-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
        using var empty = JsonDocument.Parse("{}");
        Call(cancelled, endpoint, empty.RootElement);
        Check(endpoint + "-returns-with-no-retained-intent", Field(cancelled, "suspendedCurve") is null);
        Check(endpoint + "-old-worker-terminates", ((Task)Field(cancelled, "resumeTask")!).Wait(4000));
        Check(endpoint + "-old-worker-cannot-reenable", Field(cancelled, "activeCurve") is null && !Events().Contains("api.enable.success"));
        Check(endpoint + "-old-worker-cannot-report-restored", P(Call(cancelled, "Snapshot")!, "PowerOperationOutcome") != "restored");
        DisposeFixture(cancelled);
    }
    var (sleepOff, _) = NewEngine();
    SeedActive(sleepOff, desired);
    Call(sleepOff, "SuspendForSystemPower");
    ResetEvents();
    var sleepOffBefore = Call(sleepOff, "Snapshot")!;
    Call(sleepOff, "CancelControlIntent");
    var sleepOffAfter = Call(sleepOff, "Snapshot")!;
    Check("sleep-off-preserves-sleep-phase", P(sleepOffAfter, "State") == "Suspended" && P(sleepOffAfter, "PowerState") == "Suspended");
    Check("sleep-off-performs-no-restore-or-hc-action", !Events().Any(e => e.StartsWith("restore.") || e.StartsWith("hc-operation.") || e == "api.enable.success"));
    Check("sleep-off-does-not-invent-oem-proof", P(sleepOffBefore, "OemRestoreConfirmed") == P(sleepOffAfter, "OemRestoreConfirmed"));
    Call(sleepOff, "ResumeForSystemPower");
    Check("sleep-off-next-wake-does-not-reenable", P(Call(sleepOff, "Snapshot")!, "PowerOperationOutcome") == "await-user" && Field(sleepOff, "activeCurve") is null);
    DisposeFixture(sleepOff);

    var (manual, _) = NewEngine(200);
    SeedActive(manual, desired);
    Call(manual, "SuspendForSystemPower");
    ResetEvents();
    Call(manual, "ResumeForSystemPower");
    Check("manual-restart-fixture-enters-pending-rebuild", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var oldManualWorker = (Task)Field(manual, "resumeTask")!;
    var cancelledAttempt = long.Parse(P(Call(manual, "Snapshot")!, "PowerOperationAttempt"));
    Call(manual, "CancelControlIntent");
    Call(manual, "Open");
    Call(manual, "OpenEvents");
    var newLease = Call(manual, "AcquireControl")!;
    var newCurve = Curve(20, 35, 60, 85);
    using (var body = JsonDocument.Parse("{\"nodes\":" + CurveJson(newCurve) + ",\"leaseId\":" + JsonSerializer.Serialize(P(newLease, "LeaseId")) + "}"))
        Call(manual, "Enable", body.RootElement);
    Check("manual-restart-is-reachable-before-old-worker-exits", CurveJson(Field(manual, "activeCurve")) == CurveJson(newCurve));
    Check("manual-restart-old-worker-terminates", oldManualWorker.Wait(4000));
    Check("old-worker-does-not-overwrite-new-manual-curve", CurveJson(Field(manual, "activeCurve")) == CurveJson(newCurve));
    Check("old-worker-does-not-fault-new-manual-control", P(Call(manual, "Snapshot")!, "State") == "Ready" && P(Call(manual, "Snapshot")!, "UnknownState") == "False");
    Check("new-manual-control-publishes-admissible-receipt", P(Call(manual, "Snapshot")!, "ControlAccepting") == "True");
    var cancelledBinding = T("ResumeWaitBinding").GetMethod("Pinned", flags)!.Invoke(null, new object[] { cancelledAttempt })!;
    var cancelledWaiterRejected = false;
    try { ((Task)Call(manual, "WaitForAutomaticResumeAsync", 1000, cancelledBinding)!).GetAwaiter().GetResult(); }
    catch (Exception e) { cancelledWaiterRejected = P(e, "Code") == "HC_RESUME_SUPERSEDED"; }
    Check("old-bound-waiter-cannot-borrow-new-manual-success", cancelledWaiterRejected);
    DisposeFixture(manual);
    var (leaseCancel, _) = NewEngine(1);
    SeedActive(leaseCancel, desired);
    Call(leaseCancel, "SuspendForSystemPower");
    using var leaseStage = new ManualResetEventSlim(false);
    using var releaseLeaseStage = new ManualResetEventSlim(false);
    int stageCaptured = 0;
    Func<object?> pauseAtOwnedLease = () =>
    {
        if (Field(Field(leaseCancel, "diagnostics")!, "contextProvider") is not null
            && Field(leaseCancel, "automaticResumePlaceholderLeaseId") is not null
            && Interlocked.CompareExchange(ref stageCaptured, 1, 0) == 0)
        {
            leaseStage.Set();
            if (!releaseLeaseStage.Wait(4000)) throw new InvalidOperationException("fixture owned-lease pause expired");
        }
        return null;
    };
    Call(Field(leaseCancel, "diagnostics")!, "SetContextProvider", pauseAtOwnedLease);
    ResetEvents();
    Call(leaseCancel, "ResumeForSystemPower");
    Check("cancel-after-acquire-reaches-owned-placeholder-stage", leaseStage.Wait(4000));
    var afterAcquireWorker = (Task)Field(leaseCancel, "resumeTask")!;
    var offAfterAcquire = Task.Run(() => Call(leaseCancel, "CancelControlIntent"));
    Check("cancel-after-acquire-revokes-only-worker-placeholder", Wait(() => Field(leaseCancel, "state")!.GetType().GetProperty("Lease")!.GetValue(Field(leaseCancel, "state")) is null, 500));
    releaseLeaseStage.Set();
    Check("cancel-after-acquire-and-old-worker-both-terminate", offAfterAcquire.Wait(4000) && afterAcquireWorker.Wait(4000));
    Check("cancel-after-acquire-never-enables-old-curve", Field(leaseCancel, "activeCurve") is null && !Events().Contains("api.enable.success"));
    var freshLeaseAvailable = false;
    try { Call(leaseCancel, "AcquireControl"); freshLeaseAvailable = true; } catch { }
    Check("manual-owner-can-acquire-without-waiting-old-15s-lease", freshLeaseAvailable);
    DisposeFixture(leaseCancel);

    var (announcedSleep, _) = NewEngine(200);
    SeedActive(announcedSleep, desired);
    Call(announcedSleep, "SuspendForSystemPower");
    ResetEvents();
    Call(announcedSleep, "ResumeForSystemPower");
    Check("announced-sleep-enters-real-delayed-resume", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var sleepInterruptedWorker = (Task)Field(announcedSleep, "resumeTask")!;
    var oldSleepAttempt = long.Parse(P(Call(announcedSleep, "Snapshot")!, "PowerOperationAttempt"));
    var announceTimer = Stopwatch.StartNew();
    Call(announcedSleep, "MarkSystemSuspendPending");
    Check("sleep-announcement-does-not-wait-for-worker", announceTimer.ElapsedMilliseconds < 100);
    Check("announced-sleep-stops-old-worker-within-bound", sleepInterruptedWorker.Wait(4000));
    Check("announced-sleep-old-worker-performs-no-enable", !Events().Contains("api.enable.success"));
    Check("announced-sleep-does-not-leave-false-running-flag", Equals(Field(announcedSleep, "automaticResumeInProgress"), false));
    Check("announced-sleep-retires-old-operation-owner", Field(announcedSleep, "activeResumeOperation") is null);
    var oldSleepBinding = T("ResumeWaitBinding").GetMethod("Pinned", flags)!.Invoke(null, new object[] { oldSleepAttempt })!;
    var oldSleepWaitRejected = false;
    try { ((Task)Call(announcedSleep, "WaitForAutomaticResumeAsync", 150, oldSleepBinding)!).GetAwaiter().GetResult(); }
    catch (Exception error) { oldSleepWaitRejected = P(error, "Code") == "HC_RESUME_SUPERSEDED"; }
    Check("announced-sleep-bound-waiter-gets-own-superseded", oldSleepWaitRejected);
    Call(announcedSleep, "SuspendForSystemPower");
    Check("executed-sleep-still-preserves-user-curve", CurveJson(Field(announcedSleep, "suspendedCurve")) == CurveJson(desired));
    Check("executed-sleep-keeps-suspended-phase", P(Call(announcedSleep, "Snapshot")!, "State") == "Suspended");
    DisposeFixture(announcedSleep);

    // Directly inject a later owner to test only the retired worker's field
    // fencing. Production ingress serializes ownership separately; this does
    // not claim that two simultaneous real resume workers are supported.
    var (laterOwner, _) = NewEngine(200);
    SeedActive(laterOwner, desired);
    Call(laterOwner, "SuspendForSystemPower");
    ResetEvents();
    Call(laterOwner, "ResumeForSystemPower");
    Check("later-owner-fixture-pauses-old-real-worker", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
    var retiredWorker = (Task)Field(laterOwner, "resumeTask")!;
    var replacementOwner = Activator.CreateInstance(T("FanHostEngine+ResumeOperationIdentity"), flags, null, new object[] { 9L, 900, 999L, 999L, 999L }, null)!;
    SetField(laterOwner, "activeResumeOperation", replacementOwner);
    SetProperty(Field(laterOwner, "state")!, "PowerOperationAttempt", 900);
    SetProperty(Field(laterOwner, "state")!, "PowerOperationStatus", "pending");
    Check("retired-old-worker-exits-without-running-new-work", retiredWorker.Wait(4000));
    Check("old-worker-keeps-later-owner-reference", ReferenceEquals(Field(laterOwner, "activeResumeOperation"), replacementOwner));
    Check("old-worker-keeps-later-owner-running-flag", Equals(Field(laterOwner, "automaticResumeInProgress"), true));
    Check("old-worker-keeps-later-operation-status", P(Call(laterOwner, "Snapshot")!, "PowerOperationStatus") == "pending");
    Call(laterOwner, "CancelControlIntent");
    DisposeFixture(laterOwner);

    var (newClientOwner, _) = NewEngine();
    var protectedLease = Activator.CreateInstance(T("FanLease"), "fixture-new-client", 99L, Environment.TickCount64 + 15000L, "fixture-client")!;
    SetProperty(Field(newClientOwner, "state")!, "Lease", protectedLease);
    SetField(newClientOwner, "automaticResumePlaceholderLeaseId", "fixture-old-placeholder");
    SetField(newClientOwner, "automaticResumePlaceholderLeaseGeneration", 2L);
    var oldOperation = Activator.CreateInstance(T("FanHostEngine+ResumeOperationIdentity"), flags, null, new object[] { 2L, 1, 0L, 0L, 0L }, null)!;
    SetField(newClientOwner, "activeResumeOperation", oldOperation);
    Call(newClientOwner, "CancelControlIntent");
    Check("cancel-old-resume-cannot-revoke-new-client-lease", ReferenceEquals(protectedLease, Field(newClientOwner, "state")!.GetType().GetProperty("Lease")!.GetValue(Field(newClientOwner, "state"))));
    DisposeFixture(newClientOwner);

    // Real HttpListener and production authentication/dispatch, explicit mock
    // engine, ephemeral loopback port. No external service or Host process.
    var (httpEngine, httpOptions) = NewEngine();
    SeedActive(httpEngine, desired);
    Call(httpEngine, "SuspendForSystemPower");
    using var portProbe = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
    portProbe.Start();
    var port = ((System.Net.IPEndPoint)portProbe.LocalEndpoint).Port;
    portProbe.Stop();
    SetProperty(httpOptions, "Port", port);
    var token = new string('a', 64);
    SetProperty(httpOptions, "SessionToken", token);
    var httpDiagnostics = Activator.CreateInstance(diagnosticType, flags, null, new object[] { Path.Combine(evidence, "http-logs"), 0 }, null)!;
    var server = Activator.CreateInstance(T("LocalFanHostServer"), flags, null, new[] { httpEngine, httpOptions, httpDiagnostics }, null)!;
    var serving = (Task)Call(server, "RunAsync")!;
    try
    {
        Check("cancel-http-listener-ready", Wait(() => Events().Contains("listener.ready")));
        using var client = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(2) };
        System.Net.Http.HttpResponseMessage Request(string? credential)
        {
            using var message = new System.Net.Http.HttpRequestMessage(System.Net.Http.HttpMethod.Post, $"http://127.0.0.1:{port}/api/cancel-control-intent");
            if (credential is not null) message.Headers.Add("X-YeMan-Fan-Session", credential);
            message.Content = new System.Net.Http.StringContent("{}", System.Text.Encoding.UTF8, "application/json");
            return client.Send(message);
        }
        using var missing = Request(null);
        Check("cancel-http-missing-session-is-401", (int)missing.StatusCode == 401);
        using var wrong = Request(new string('b', 64));
        Check("cancel-http-wrong-session-is-401", (int)wrong.StatusCode == 401);
        Check("unauthorized-cancel-keeps-retained-intent", CurveJson(Field(httpEngine, "suspendedCurve")) == CurveJson(desired));
        ResetEvents();
        using var accepted = Request(token);
        using var receipt = JsonDocument.Parse(accepted.Content.ReadAsStringAsync().GetAwaiter().GetResult());
        Check("authenticated-cancel-http-200-explicit-receipt", (int)accepted.StatusCode == 200 && receipt.RootElement.GetProperty("controlIntentCancelled").GetBoolean());
        Check("authenticated-cancel-clears-retained-intent", Field(httpEngine, "suspendedCurve") is null);
        Check("authenticated-cancel-http-has-no-hardware-action", !Events().Any(e => e.StartsWith("restore.") || e.StartsWith("hc-operation.") || e == "api.enable.success"));
        if (args.Length > 2)
        {
            SetField(httpEngine, "suspendedCurve", desired);
            SetProperty(httpOptions, "MockResumeRebuildDelayMs", 400);
            ResetEvents();
            Call(httpEngine, "ResumeForSystemPower", 2L);
            Check("bridge-http-chain-enters-real-pending-resume", Wait(() => Events().Contains("power.resume-mock-slow-rebuild-window")));
            if (args.Length > 4 && args[4].StartsWith("empty-", StringComparison.Ordinal))
            {
                Call(httpEngine, "CancelControlIntent");
                ((Task)Field(httpEngine, "resumeTask")!).Wait(4000);
                SetProperty(httpOptions, "MockResumeRebuildDelayMs", 0);
                Call(httpEngine, "SuspendForSystemPower");
                Call(httpEngine, "ResumeForSystemPower", 3L);
                Check("bridge-http-empty-resume-has-no-old-control-intent", Field(httpEngine, "suspendedCurve") is null && P(Call(httpEngine, "Snapshot")!, "PowerOperationOutcome") == "await-user");
            }
            var info = new System.Diagnostics.ProcessStartInfo(args[2]) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            info.ArgumentList.Add(args[3]);
            info.ArgumentList.Add($"http://127.0.0.1:{port}");
            if (args.Length > 4) info.ArgumentList.Add(args[4]);
            using var node = System.Diagnostics.Process.Start(info)!;
            var stdout = node.StandardOutput.ReadToEndAsync();
            var stderr = node.StandardError.ReadToEndAsync();
            var finished = node.WaitForExit(10000);
            if (!finished) { node.Kill(entireProcessTree: false); node.WaitForExit(2000); }
            File.WriteAllText(Path.Combine(evidence, "bridge-host-chain.out"), stdout.GetAwaiter().GetResult() + stderr.GetAwaiter().GetResult());
            Check(args.Length > 4 ? "actual-bridge-control-contract-" + args[4] : "actual-bridge-off-then-manual-on-is-reachable", finished && node.ExitCode == 0);
        }
    }
    finally
    {
        Call(server, "Dispose");
        Check("cancel-http-listener-stops-within-bound", serving.Wait(4000));
        DisposeFixture(httpEngine);
    }
    var (cold, _) = NewEngine();
    Call(cold, "SuspendForSystemPower");
    Call(cold, "ResumeForSystemPower");
    Check("never-enabled-session-does-not-invent-curve", P(Call(cold, "Snapshot")!, "State") == "AwaitingControl" && Field(cold, "activeCurve") is null);
    DisposeFixture(cold);
}
catch (Exception e) { Check("runner-no-unexpected-exception", false); Console.Error.WriteLine(e); }
var result = new { ok, count = checks.Count, checks, hostAssemblyPath = assembly.Location, hostAssemblySha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(assembly.Location))).ToLowerInvariant(), scope = "production Host engine with explicit mock backend; safe Close and initial curve are fixtures; real loopback HTTP; no HC/native IPC/device proof" };
File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"ok={ok} checks={checks.Count}");
return ok ? 0 : 1;

sealed class HostTestLoadContext(string hostPath) : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver resolver = new(hostPath);
    protected override Assembly? Load(AssemblyName name)
    {
        var path = resolver.ResolveAssemblyToPath(name);
        return path is null ? null : LoadFromAssemblyPath(path);
    }
}