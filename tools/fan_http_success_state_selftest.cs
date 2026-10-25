using System.Collections;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

internal sealed class StateCopyCounter : JsonConverterFactory
{
    internal static int Reads;
    internal required JsonSerializerOptions Plain;
    public override bool CanConvert(Type type) => type.Name == "HostState" && type.Assembly.GetName().Name == "YeManFanHost";
    public override JsonConverter CreateConverter(Type type, JsonSerializerOptions options) =>
        (JsonConverter)Activator.CreateInstance(typeof(Counter<>).MakeGenericType(type), Plain)!;
    private sealed class Counter<T>(JsonSerializerOptions plain) : JsonConverter<T>
    {
        public override T? Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options)
        {
            Reads++;
            return JsonSerializer.Deserialize<T>(ref reader, plain);
        }
        public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options) => JsonSerializer.Serialize(writer, value, plain);
    }
}

internal static class Checks
{
    private const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    private static Assembly host = null!;
    private static Type engineType = null!, logType = null!, serverType = null!;
    private static string root = "";
    private static bool lazy;
    private static readonly List<object> Results = [], Observations = [];
    private static readonly List<string> Failures = [];
    private static object? Field(object target, string name) => target.GetType().GetField(name, All)!.GetValue(target);
    private static object? Call(object target, string name, params object?[] values)
    {
        try { return target.GetType().GetMethods(All).Single(m => m.Name == name && m.GetParameters().Length == values.Length).Invoke(target, values); }
        catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
    }
    private static void Assert(bool value, string why) { if (!value) throw new InvalidOperationException(why); }
    private static async Task Check(string name, Func<Task> work)
    {
        try { await work(); Results.Add(new { name, ok = true }); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { Results.Add(new { name, ok = false, error = ex.Message }); Failures.Add(name + ": " + ex.Message); Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }
    private sealed class Fixture : IDisposable
    {
        internal readonly object Engine, Log, Server;
        internal readonly string DirectoryPath, Token = Guid.NewGuid().ToString("N");
        internal readonly string LeaseId;
        private readonly HttpListener listener = new();
        private readonly HttpClient client = new(new HttpClientHandler { UseProxy = false });
        private readonly string baseUrl;
        internal Fixture(string name, string? credential = null)
        {
            Token = credential ?? Token;
            DirectoryPath = Path.Combine(root, name); Directory.CreateDirectory(DirectoryPath);
            Log = Activator.CreateInstance(logType, All, null, [DirectoryPath, 0], null)!;
            var reservation = new TcpListener(IPAddress.Loopback, 0); reservation.Start();
            var port = ((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
            baseUrl = $"http://127.0.0.1:{port}/";
            var args = new[] { "--port", port.ToString(), "--mock-handshake", "--session-token", Token };
            var options = host.GetType("YeManFanHost.HostOptions", true)!.GetMethod("Parse", All)!.Invoke(null, [args]);
            Engine = Activator.CreateInstance(engineType, All, null, [options, Log], null)!;
            (Field(Engine, "runtimeDiagnosticsTimer") as Timer)?.Dispose(); engineType.GetField("runtimeDiagnosticsTimer", All)!.SetValue(Engine, null);
            Assert(Field(Engine, "realBackend") is null, "fixture must never instantiate a real backend");
            Call(Engine, "Handshake"); Call(Engine, "Open"); Call(Engine, "OpenEvents");
            var lease = Call(Engine, "AcquireControl")!;
            LeaseId = (string)lease.GetType().GetProperty("LeaseId", All)!.GetValue(lease)!;
            Server = Activator.CreateInstance(serverType, All, null, [Engine, options, Log], null)!;
            listener.Prefixes.Add(baseUrl); listener.Start(); client.Timeout = TimeSpan.FromSeconds(5);
        }
        internal string Flag => Path.Combine(DirectoryPath, "fan-logging-enabled.flag");
        internal void Gate(string value) => File.WriteAllText(Flag, value);
        internal List<JsonElement> Rows => !File.Exists((string)logType.GetProperty("Path", All)!.GetValue(Log)!) ? [] :
            File.ReadAllLines((string)logType.GetProperty("Path", All)!.GetValue(Log)!).Where(x => !string.IsNullOrWhiteSpace(x))
                .Select(x => JsonDocument.Parse(x.TrimStart('﻿')).RootElement.Clone()).ToList();
        internal object State => Field(Engine, "state")!;
        internal object? Lease => State.GetType().GetProperty("Lease", All)!.GetValue(State);
        internal async Task<(int Status, JsonElement Body)> Send(string path = "api/heartbeat", string method = "POST", string? token = null, bool omitToken = false, string? body = null)
        {
            using var message = new HttpRequestMessage(new HttpMethod(method), baseUrl + path);
            if (!omitToken) message.Headers.Add("X-YeMan-Fan-Session", token ?? Token);
            if (method != "GET") message.Content = new StringContent(body ?? JsonSerializer.Serialize(new { leaseId = LeaseId }), Encoding.UTF8, "application/json");
            var accept = listener.GetContextAsync(); var pending = client.SendAsync(message);
            var context = await accept.WaitAsync(TimeSpan.FromSeconds(5));
            await ((Task)serverType.GetMethod("HandleAsync", All)!.Invoke(Server, [context])!).WaitAsync(TimeSpan.FromSeconds(5));
            using var response = await pending;
            return ((int)response.StatusCode, JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.Clone());
        }
        public void Dispose()
        {
            client.Dispose(); listener.Close(); ((IDisposable)Server).Dispose(); ((IDisposable)Engine).Dispose();
        }
    }
    public static async Task<int> Main(string[] args)
    {
        try
        {
            var dll = Path.GetFullPath(args[0]); root = Path.GetFullPath(args[1]); lazy = args[2] == "lazy";
            Directory.CreateDirectory(root);
            AssemblyLoadContext.Default.Resolving += (_, name) => { var f = Path.Combine(Path.GetDirectoryName(dll)!, name.Name + ".dll"); return File.Exists(f) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(f) : null; };
            host = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
            engineType = host.GetType("YeManFanHost.FanHostEngine", true)!; logType = host.GetType("YeManFanHost.HostDiagnosticLog", true)!; serverType = host.GetType("YeManFanHost.LocalFanHostServer", true)!;
            var options = (JsonSerializerOptions)host.GetType("YeManFanHost.JsonDefaults", true)!.GetField("Options", All)!.GetValue(null)!;
            options.Converters.Insert(0, new StateCopyCounter { Plain = new JsonSerializerOptions(options) });
            var capture = logType.GetProperty("SelfTestCaptureEnabled", All)!; capture.SetValue(null, false);
            await Check("ten authenticated heartbeats preserve lease while closed-gate log copies disappear", async () => {
                using var f = new Fixture("closed"); StateCopyCounter.Reads = 0;
                for (var i = 0; i < 10; i++) { var r = await f.Send(); Assert(r.Status == 200 && r.Body.GetProperty("lease").GetProperty("leaseId").GetString() == f.LeaseId, "real heartbeat response changed"); }
                var copies = StateCopyCounter.Reads;
                Assert(copies == (lazy ? 0 : 10), $"extra closed-gate state copies={copies}");
                Assert(f.Lease is not null && f.Rows.All(x => x.GetProperty("eventName").GetString() != "api.request-success"), "lease or log gate changed");
                Observations.Add(new { scenario = "ten-closed-gate-heartbeats", requests = 10, stateCopies = copies, okResponses = 10, runtimeSuccessRows = 0 });
            });
            await Check("completed heartbeat keeps its current lease visible while another thread holds the state gate", async () => {
                using var f = new Fixture("published-lease");
                var response = await f.Send(); Assert(response.Status == 200, "heartbeat response failed");
                var expected = response.Body.GetProperty("lease");
                var held = new ManualResetEventSlim(); var release = new ManualResetEventSlim();
                var stateGate = Field(f.Engine, "gate")!;
                var owner = Task.Run(() => { lock (stateGate) { held.Set(); if (!release.Wait(TimeSpan.FromSeconds(5))) throw new InvalidOperationException("fixture gate release timed out"); } });
                try {
                    Assert(held.Wait(TimeSpan.FromSeconds(5)), "fixture did not hold state gate");
                    var read = Call(f.Engine, "SnapshotForRead")!;
                    var actual = read.GetType().GetProperty("Lease", All)!.GetValue(read);
                    Assert(actual is not null, "completed heartbeat lease missing from busy-gate snapshot");
                    var json = JsonSerializer.SerializeToElement(actual, actual!.GetType(), new JsonSerializerOptions(JsonSerializerDefaults.Web));
                    foreach (var key in new[] { "leaseId", "generation", "expiresAtMonoMs" }) Assert(json.GetProperty(key).GetRawText() == expected.GetProperty(key).GetRawText(), "busy-gate snapshot retained stale lease field " + key);
                } finally { release.Set(); await owner; held.Dispose(); release.Dispose(); }
            });
            await Check("enabled logging retains current state and the original success sampling", async () => {
                using var f = new Fixture("enabled"); f.Gate("enabled"); StateCopyCounter.Reads = 0;
                for (var i = 0; i < 10; i++) Assert((await f.Send()).Status == 200, "enabled heartbeat failed");
                var rows = f.Rows.Where(x => x.GetProperty("eventName").GetString() == "api.request-success").ToList();
                Assert(StateCopyCounter.Reads == 10 && rows.Count == 1 && rows[0].GetProperty("details").GetProperty("state").GetString() == "Ready", "enabled state/sampling changed");
                Observations.Add(new { scenario = "ten-enabled-heartbeats", requests = 10, stateCopies = StateCopyCounter.Reads, runtimeSuccessRows = rows.Count });
            });
            await Check("health and state still create their required response snapshots", async () => {
                using var f = new Fixture("reads"); StateCopyCounter.Reads = 0;
                foreach (var path in new[] { "health", "api/state" }) { var r = await f.Send(path, "GET"); Assert(r.Status == 200 && r.Body.GetProperty("state").GetProperty("state").GetString() == "Ready", "response state omitted"); }
                Assert(StateCopyCounter.Reads == 2, "required response snapshots removed or duplicated");
            });
            await Check("wrong and absent credentials stay 401 with zero handler state copies", async () => {
                using var f = new Fixture("auth"); StateCopyCounter.Reads = 0;
                Assert((await f.Send(token: "fixture-invalid")).Status == 401, "wrong credential admitted");
                Assert((await f.Send(omitToken: true)).Status == 401, "absent credential admitted");
                Assert(StateCopyCounter.Reads == 0 && f.Lease is not null, "authentication mutated control or read diagnostic state");
            });
            await Check("credential matrix preserves exact value and missing-host/request rejection", async () => {
                using var f = new Fixture("auth-matrix", new string('a', 64));
                StateCopyCounter.Reads = 0;
                foreach (var wrong in new[] { new string('b', 64), new string('A', 64), new string('a', 63), new string('a', 65), "fixture-invalid" })
                    Assert((await f.Send(token: wrong)).Status == 401, "a mismatched credential was accepted");
                Assert((await f.Send(omitToken: true)).Status == 401, "missing request credential accepted");
                Assert(StateCopyCounter.Reads == 0 && f.Lease is not null, "credential rejection entered the engine or changed its lease");
                Assert((await f.Send()).Status == 200, "correct current credential no longer reaches heartbeat");
                using var missing = new Fixture("auth-host-missing", " ");
                Assert((await missing.Send(token: "fixture-arbitrary")).Status == 401, "a missing Host credential acquired an authentication bypass");
            });
            await Check("UTF8 buffer boundary and long configured credentials keep the same HTTP acceptance", async () => {
                foreach(var length in new[]{255,256,257,1024}) {
                    using var f = new Fixture("auth-buffer-"+length, new string('a', length));
                    Assert((await f.Send()).Status == 200, "current credential rejected at length "+length);
                    Assert((await f.Send(token: new string('b', length))).Status == 401, "wrong same-length credential accepted at length "+length);
                    Assert((await f.Send(token: new string('a', length-1))).Status == 401, "wrong length accepted at boundary "+length);
                    Assert((await f.Send()).Status == 200, "failed authentication poisoned next valid request at length "+length);
                }
            });
            await Check("credentials remain scoped to each new Host instance", async () => {
                using var a = new Fixture("auth-instance-a", new string('a', 64));
                using var b = new Fixture("auth-instance-b", new string('b', 64));
                Assert((await a.Send()).Status == 200 && (await b.Send()).Status == 200, "a current instance credential was rejected");
                Assert((await a.Send(token: b.Token)).Status == 401 && (await b.Send(token: a.Token)).Status == 401, "another Host instance credential was accepted");
                Assert((await a.Send()).Status == 200 && (await b.Send()).Status == 200, "a wrong credential poisoned subsequent current authentication");
            });
            await Check("bad credentials do not block a later manual enable, OFF, and fresh-lease enable", async () => {
                using var f = new Fixture("auth-manual", new string('c', 64));
                var nodes = new[] { new { tempC = 0, dutyPercent = 100 }, new { tempC = 40, dutyPercent = 100 }, new { tempC = 70, dutyPercent = 100 }, new { tempC = 100, dutyPercent = 100 } };
                var first = JsonSerializer.Serialize(new { nodes, leaseId = f.LeaseId });
                Assert((await f.Send("api/enable", token: new string('d', 64), body: first)).Status == 401, "wrong credential performed a control request");
                Assert((await f.Send("api/enable", body: first)).Status == 200, "current credential manual ON was blocked");
                Assert((await f.Send("api/disable")).Status == 200, "manual OFF was blocked");
                Assert((await f.Send("api/release-control")).Status == 200, "manual OFF lease release was blocked");
                var lease = await f.Send("api/acquire-control");
                Assert(lease.Status == 200, "a new manual attempt could not acquire");
                var leaseId = lease.Body.GetProperty("lease").GetProperty("leaseId").GetString();
                Assert((await f.Send("api/enable", body: JsonSerializer.Serialize(new { nodes, leaseId }))).Status == 200, "fresh-lease manual ON was blocked");
            });
            await Check("invalid lease stays rejected and keeps its failure state evidence", async () => {
                using var f = new Fixture("lease-failure"); StateCopyCounter.Reads = 0;
                var r = await f.Send(body: "{\"leaseId\":\"fixture-invalid\"}");
                Assert(r.Status == 409 && !r.Body.GetProperty("ok").GetBoolean() && f.Lease is not null, "lease rejection changed");
                Assert(StateCopyCounter.Reads > 0, "failure state evidence removed with success-only optimization");
            });
            await Check("unreadable logging flag cannot alter a successful heartbeat", async () => {
                using var f = new Fixture("locked"); f.Gate("enabled"); StateCopyCounter.Reads = 0;
                using (var held = new FileStream(f.Flag, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) Assert((await f.Send()).Status == 200, "diagnostic failure changed response");
                Assert(StateCopyCounter.Reads == (lazy ? 0 : 1) && f.Lease is not null, "locked flag performed unwanted state work or changed lease");
            });
            await Check("observed logging toggles are not cached", async () => {
                using var f = new Fixture("toggle"); StateCopyCounter.Reads = 0;
                Assert((await f.Send()).Status == 200, "closed request failed"); f.Gate("1"); Assert((await f.Send()).Status == 200, "legacy enabled value failed"); f.Gate("disabled"); Assert((await f.Send()).Status == 200, "disabled request failed");
                Assert(StateCopyCounter.Reads == (lazy ? 1 : 3), "gate decision cached or unnecessary snapshot retained");
            });
            await Check("selftest capture remains complete with disk logging closed", async () => {
                using var f = new Fixture("capture"); logType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); capture.SetValue(null, true);
                try {
                    StateCopyCounter.Reads = 0; for (var i = 0; i < 3; i++) Assert((await f.Send()).Status == 200, "capture heartbeat failed");
                    var events = ((IEnumerable)logType.GetProperty("SelfTestCaptured", All)!.GetValue(null)!).Cast<object>().ToList();
                    Assert(StateCopyCounter.Reads == 3 && events.Count == 3, "selftest evidence skipped");
                } finally { capture.SetValue(null, false); logType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); }
            });
            await Check("lazy success state factory failures stay inside diagnostics", () => {
                if (lazy) {
                    var dir = Path.Combine(root, "factory-failure"); Directory.CreateDirectory(dir); File.WriteAllText(Path.Combine(dir, "fan-logging-enabled.flag"), "enabled");
                    var log = Activator.CreateInstance(logType, All, null, [dir, 0], null)!;
                    logType.GetMethod("WriteApiRequestSuccessLazy", All)!.Invoke(log, ["fixture", "POST", "/api/heartbeat", (Func<string>)(() => throw new IOException("fixture-state-unavailable")), 1d]);
                } else {
                    var dir = Path.Combine(root, "factory-failure"); Directory.CreateDirectory(dir); File.WriteAllText(Path.Combine(dir, "fan-logging-enabled.flag"), "enabled");
                    var log = Activator.CreateInstance(logType, All, null, [dir, 0], null)!;
                    logType.GetMethod("WriteLazy", All)!.Invoke(log, ["api.request-success", (Func<object?>)(() => throw new IOException("fixture-state-unavailable"))]);
                }
                return Task.CompletedTask;
            });
            await Check("diagnostic-only fixture never loads HC", () => { Assert(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"), "HC loaded"); return Task.CompletedTask; });
            var result = new { ok = Failures.Count == 0, checks = Results.Count, mode = args[2], observations = Observations, failures = Failures, results = Results, hardwareCalls = 0, cpuBenchmark = false, scope = "Actual built Host HandleAsync over isolated authenticated loopback HTTP; mock-handshake engine without backend; JSON converter counts actual HostState copies; no Host Main, HC, devices, production endpoint or CPU performance timing" };
            var json = JsonSerializer.Serialize(result); File.WriteAllText(args[3], json); Console.WriteLine(json); return Failures.Count == 0 ? 0 : 1;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
