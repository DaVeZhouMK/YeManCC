using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text.Json;

// Real Host listener/engine; the preloaded device below exposes only inert metadata.
// No Host Main, HC DLL, device Open, WinRT sensors or hardware transport is executed.
internal static class FanColdStartupChecks
{
    const BindingFlags Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    static void Set(object target, string name, object? value) => target.GetType().GetField(name, Flags)!.SetValue(target, value);
    static object? Invoke(object target, string name, params object?[] args)
    {
        try { return target.GetType().GetMethod(name, Flags)!.Invoke(target, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    static HttpRequestMessage Request(HttpMethod method, string path, string? token)
    {
        var request = new HttpRequestMessage(method, path);
        if (token is not null) request.Headers.Add("X-YeMan-Fan-Session", token);
        return request;
    }
    internal static async Task Run(Assembly host, string evidence, Action<string, bool> check)
    {
        using var reservation = new TcpListener(IPAddress.Loopback, 0);
        reservation.Start(); var port = ((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
        const string token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        var optionType = host.GetType("YeManFanHost.HostOptions", true)!;
        var options = optionType.GetMethod("Parse", Flags)!.Invoke(null, [new[]
            { "--port", port.ToString(), "--real-backend", "--session-token", token }])!;
        var logType = host.GetType("YeManFanHost.HostDiagnosticLog", true)!;
        var logRoot = Path.Combine(evidence, "cold-start-logs"); Directory.CreateDirectory(logRoot);
        File.WriteAllText(Path.Combine(logRoot, "fan-logging-enabled.flag"), "enabled");
        var log = Activator.CreateInstance(logType, Flags, null, [logRoot, 0], null)!;
        var engine = Activator.CreateInstance(host.GetType("YeManFanHost.FanHostEngine", true)!, Flags, null, [options, log], null)!;
        var backend = Field(engine, "realBackend")!;
        var device = new InertFanMetadata();
        Set(backend, "assembly", typeof(InertFanMetadata).Assembly);
        Set(backend, "device", device); Set(backend, "deviceType", device.GetType());
        Set(backend, "hcInitializationAssembly", typeof(InertFanMetadata).Assembly);
        Set(backend, "hcInitializationDevice", device); Set(backend, "hcCoreInitialized", true);
        Set(backend, "hcRuntimeConfigured", true); Set(backend, "hcMotherboardCollected", true);
        var route = backend.GetType().GetMethod("ResolveRouteForSelfTest", Flags)!
            .Invoke(null, ["HandheldCompanion.Devices.XboxROGAllyX"]);
        Set(backend, "routeOverrideForSelfTest", route);
        var server = Activator.CreateInstance(host.GetType("YeManFanHost.LocalFanHostServer", true)!, Flags, null, [engine, options, log], null)!;
        using var client = new HttpClient { BaseAddress = new Uri($"http://127.0.0.1:{port}"), Timeout = TimeSpan.FromSeconds(17) };
        Task run = Task.Run(async () => await (Task)Invoke(server, "RunAsync")!);
        var rows = new List<object>();
        try
        {
            // First health must finish even if the old eager handshake entered our blocked getter.
            HttpResponseMessage? initial = null;
            var initialClock = Stopwatch.StartNew();
            for (var i = 0; i < 8 && initial is null; i++)
            {
                try
                {
                    using var budget = new CancellationTokenSource(700);
                    initial = await client.SendAsync(Request(HttpMethod.Get, "/health", token), budget.Token);
                }
                catch (Exception error) when (error is HttpRequestException or TaskCanceledException)
                { if (device.CapabilityCalls > 0) break; await Task.Delay(40); }
            }
            check("cold health responds before any HC initialization", initial?.StatusCode == HttpStatusCode.OK);
            check("listener startup never eagerly loads HC metadata", device.CapabilityCalls == 0);
            if (initial is not null)
            {
                using var health = JsonDocument.Parse(await initial.Content.ReadAsStringAsync());
                check("healthy listener grants no hardware control", !health.RootElement.GetProperty("state").GetProperty("hardwareWritesEnabled").GetBoolean());
            }
            rows.Add(new { name = "initial-health", elapsedMs = initialClock.ElapsedMilliseconds, status = (int?)initial?.StatusCode, capabilityCalls = device.CapabilityCalls });
            // Prevent a RED baseline probe from leaving its deliberately blocked owner running.
            if (device.CapabilityCalls > 0) { device.Release.Set(); return; }
            initial?.Dispose();
            using (var denied = await client.SendAsync(Request(HttpMethod.Post, "/api/handshake", "wrong-token")))
                check("unauthenticated handshake is rejected before initialization", denied.StatusCode == HttpStatusCode.Unauthorized && device.CapabilityCalls == 0);
            var begin = Stopwatch.StartNew();
            Task<HttpResponseMessage> handshake = client.SendAsync(Request(HttpMethod.Post, "/api/handshake", token));
            check("authenticated handshake enters actual backend capability path", await Task.Run(() => device.Entered.Wait(2000)));
            using (var budget = new CancellationTokenSource(700))
            using (var healthResponse = await client.SendAsync(Request(HttpMethod.Get, "/health", token), budget.Token))
            using (var health = JsonDocument.Parse(await healthResponse.Content.ReadAsStringAsync()))
                check("health remains responsive during a blocked HC handshake without write promotion",
                    healthResponse.StatusCode == HttpStatusCode.OK && !health.RootElement.GetProperty("state").GetProperty("hardwareWritesEnabled").GetBoolean());
            using (var budget = new CancellationTokenSource(700))
            using (var denied = await client.SendAsync(Request(HttpMethod.Get, "/health", null), budget.Token))
                check("health authentication remains mandatory during initialization", denied.StatusCode == HttpStatusCode.Unauthorized);
            // Beyond the old five-second health window, but within the original HC budget.
            await Task.Delay(6100);
            check("slow metadata probe remains one in-flight handshake", !handshake.IsCompleted && device.CapabilityCalls == 1);
            device.Release.Set();
            using var response = await handshake.WaitAsync(TimeSpan.FromSeconds(4));
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            check("single slow authenticated handshake completes without another click", response.StatusCode == HttpStatusCode.OK && body.RootElement.GetProperty("supported").GetBoolean());
            check("metadata result still does not grant hardware write permission", !body.RootElement.GetProperty("hardwareWritesEnabled").GetBoolean());
            check("no HC assembly was loaded", !AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"));
            rows.Add(new { name = "delayed-explicit-handshake", elapsedMs = begin.ElapsedMilliseconds, capabilityCalls = device.CapabilityCalls, hardwareCalls = 0 });
            // A truly missing result keeps the one owner until the Host's original 15s timeout.
            device.Release.Reset(); device.Entered.Reset();
            var beforeTimeoutCalls = device.CapabilityCalls;
            var timeoutClock = Stopwatch.StartNew();
            var missingHandshake = client.SendAsync(Request(HttpMethod.Post, "/api/handshake", token));
            check("missing initialization result enters only one capability call", await Task.Run(() => device.Entered.Wait(2000)));
            using (var timeoutResponse = await missingHandshake.WaitAsync(TimeSpan.FromSeconds(16)))
            using (var timeoutBody = JsonDocument.Parse(await timeoutResponse.Content.ReadAsStringAsync()))
                check("unreturned metadata reaches bounded unavailable result", timeoutResponse.StatusCode == HttpStatusCode.OK && !timeoutBody.RootElement.GetProperty("supported").GetBoolean());
            check("unreturned owner consumes the original finite HC timeout", timeoutClock.ElapsedMilliseconds >= 14000 && timeoutClock.ElapsedMilliseconds < 17000);
            using (var healthResponse = await client.SendAsync(Request(HttpMethod.Get, "/health", token)))
                check("health still responds after the HC deadline", healthResponse.StatusCode == HttpStatusCode.OK);
            using (var retryResponse = await client.SendAsync(Request(HttpMethod.Post, "/api/handshake", token)))
            using (var retryBody = JsonDocument.Parse(await retryResponse.Content.ReadAsStringAsync()))
                check("retry cannot create a second in-flight initialization owner", !retryBody.RootElement.GetProperty("supported").GetBoolean() && device.CapabilityCalls == beforeTimeoutCalls + 1);
            device.Release.Set();
            var settleClock = Stopwatch.StartNew();
            while ((bool)Field(backend, "operationTimedOut")! && settleClock.ElapsedMilliseconds < 2000) await Task.Delay(10);
            using (var after = await client.SendAsync(Request(HttpMethod.Get, "/health", token)))
            using (var afterBody = JsonDocument.Parse(await after.Content.ReadAsStringAsync()))
                check("late metadata return cannot enable hardware control", !afterBody.RootElement.GetProperty("state").GetProperty("hardwareWritesEnabled").GetBoolean());
            rows.Add(new { name = "unreturned-metadata", elapsedMs = timeoutClock.ElapsedMilliseconds, capabilityCalls = device.CapabilityCalls, hardwareCalls = 0 });

            var logPath = (string)logType.GetProperty("Path", Flags)!.GetValue(log)!;
            var logged = File.ReadAllLines(logPath).Select(line => JsonDocument.Parse(line)).ToArray();
            try { check("only three explicit results are recorded, preserving unavailable retries", logged.Count(d => d.RootElement.GetProperty("eventName").GetString() == "startup.handshake") == 3); }
            finally { foreach (var document in logged) document.Dispose(); }
        }
        finally
        {
            device.Release.Set();
            ((IDisposable)server).Dispose();
            try { await run.WaitAsync(TimeSpan.FromSeconds(4)); check("listener and its owned workers stop within bound", true); }
            catch (Exception error) { rows.Add(new { name = "server-stop-failure", error = error.GetType().Name }); check("listener and its owned workers stop within bound", false); }
            ((IDisposable)engine).Dispose();
            File.WriteAllText(Path.Combine(evidence, "cold-start-result.json"), JsonSerializer.Serialize(new
            {
                rows, hardwareCalls = 0, hostMainStarted = false,
                scope = "actual Host listener/engine/OnStaBounded, preloaded inert metadata device substitutes only assembly/capability boundary; no real HC/device Open/EC/ACPI/WMI/WinRT"
            }, new JsonSerializerOptions { WriteIndented = true }));
        }
    }
    [Flags] public enum Capability { FanControl = 1 }
    public sealed class InertFanMetadata
    {
        internal readonly ManualResetEventSlim Entered = new(false), Release = new(false);
        internal int CapabilityCalls;
        public Capability Capabilities
        {
            get { Interlocked.Increment(ref CapabilityCalls); Entered.Set(); if (!Release.Wait(25000)) throw new TimeoutException("inert metadata wait"); return Capability.FanControl; }
        }
    }
}
