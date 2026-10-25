using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

return await SleepEngineProbe.MainAsync(args);

internal static partial class SleepEngineProbe
{
    const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
    static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };
    static readonly ConcurrentQueue<string> NativeAttempts = new();
    static readonly List<object> Checks = new();
    static readonly List<object> Evidence = new();
    static readonly List<object> Identities = new();
    static object? Engine;
    static object? Server;
    static Type? EngineType;
    static Type? ServerType;
    static HttpClient? Client;
    static string SessionToken = "";
    static string BaseUrl = "";
    static long Generation = 1000;
    static int Sequence;
    static int HttpRequestCount;
    static string Scope = "probe";
    static string ExecutionPhase = "input";
    static readonly Dictionary<string, string> ArtifactHashes = new(StringComparer.OrdinalIgnoreCase);
    static readonly Dictionary<string, string> LoadedPaths = new(StringComparer.OrdinalIgnoreCase);

    static string Hash(string file) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    static object? Invoke(Type type, string name, object? instance = null, params object?[] parameters) =>
        type.GetMethod(name, All)!.Invoke(instance, parameters);
    static object? Prop(object instance, string name) => instance.GetType().GetProperty(name, All)!.GetValue(instance);
    static void Check(string name, bool passed, object? detail = null)
    {
        Checks.Add(new { name, passed, detail });
        if (!passed) throw new InvalidOperationException("CHECK_FAILED:" + name);
    }
    static JsonElement Element(object value) => JsonSerializer.SerializeToElement(value, JsonOptions);
    static JsonElement Snapshot() => Element(Invoke(EngineType!, "Snapshot", Engine!)!);
    static object SafeState(JsonElement source)
    {
        var allowed = new[] { "state", "powerState", "hostMode", "protocolVersion", "hardwareCapable", "hardwareWrites", "hardwareWritesEnabled", "hardwareWritesObserved", "mockControlEnabled", "mockWritesObserved", "mockControlSequence", "unknownState", "openCalled", "openEventsCalled", "closeCalled", "oemRestoreConfirmed", "oemRestoreEvidence", "oemPhysicalOwnershipConfirmed", "hcVirtualCloseReturned", "hcDeviceManagerStopCompleted", "hcCloseCleanupPending", "powerOperationGeneration", "powerOperationAttempt", "powerOperationStatus", "resumePhase", "resumePhaseGeneration", "leaseGeneration" };
        var result = new Dictionary<string, object?>();
        foreach (var key in allowed) if (source.TryGetProperty(key, out var value)) result[key] = value.Clone();
        result["leaseActive"] = source.TryGetProperty("lease", out var lease) && lease.ValueKind == JsonValueKind.Object;
        return result;
    }
    static bool Bool(JsonElement value, string key) => value.TryGetProperty(key, out var field) && field.ValueKind == JsonValueKind.True;
    static string? Text(JsonElement value, string key) => value.TryGetProperty(key, out var field) && field.ValueKind == JsonValueKind.String ? field.GetString() : null;
    static long Number(JsonElement value, string key) => value.TryGetProperty(key, out var field) && field.TryGetInt64(out var number) ? number : -1;
    static void AssertBoundary()
    {
        Check("realBackend remains null", EngineType!.GetField("realBackend", All)!.GetValue(Engine) is null);
        var state = Snapshot();
        Check("physical hardware writes remain false", !Bool(state, "hardwareWrites") && !Bool(state, "hardwareWritesEnabled") && !Bool(state, "hardwareWritesObserved"));
        Check("HC assembly and target native imports absent", NativeAttempts.IsEmpty && !AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"));
    }
    internal sealed record Receipt(int Status, string? Code, JsonElement Body, JsonElement State);
    static async Task<Receipt> Send(string method, string route, object? body = null, bool authorized = true, string? rawBody = null)
    {
        HttpRequestCount++;
        var id = $"{Scope}:{++Sequence}";
        var before = Snapshot();
        using var request = new HttpRequestMessage(new HttpMethod(method), BaseUrl + route);
        if (authorized) request.Headers.Add("X-YeMan-Fan-Session", SessionToken);
        if (method != "GET") request.Content = new StringContent(rawBody ?? JsonSerializer.Serialize(body ?? new { }, JsonOptions), Encoding.UTF8, "application/json");
        var timer = Stopwatch.StartNew();
        using var response = await Client!.SendAsync(request);
        var text = await response.Content.ReadAsStringAsync();
        var decoded = JsonDocument.Parse(text).RootElement.Clone();
        var state = Snapshot();
        var code = decoded.TryGetProperty("error", out var error) ? Text(error, "code") : null;
        var input = new Dictionary<string, object?> { ["eventId"] = id, ["provenance"] = "source-derived-fixture", ["method"] = method, ["path"] = route, ["credential"] = authorized ? "fresh-synthetic-in-memory" : "missing-negative" };
        if (rawBody is not null) input["rawBodyClass"] = "malformed-json-fixture";
        if (body is not null)
        {
            var fields = Element(body);
            foreach (var key in new[] { "generation", "source", "reason", "nodes", "name" }) if (fields.TryGetProperty(key, out var field)) input[key] = field.Clone();
        }
        var feedback = new Dictionary<string, object?> { ["httpStatus"] = (int)response.StatusCode, ["errorCode"] = code,
            ["durationMs"] = timer.ElapsedMilliseconds, ["hardwareReceipt"] = false,
            ["requestIdPresent"] = response.Headers.Contains("X-YeMan-Fan-Request-Id") };
        foreach (var key in new[] { "ok", "supported", "deduplicated", "resumeNoop", "reason", "generation", "accepted", "completed", "attempt" })
            if (decoded.TryGetProperty(key, out var value) && value.ValueKind != JsonValueKind.Object) feedback[key] = value.Clone();
        Evidence.Add(new { input, consumer = new { type = "YeManFanHost.LocalFanHostServer", entry = "HandleAsync", engine = "YeManFanHost.FanHostEngine", transport = "actual-loopback-http", mainInvoked = false },
            state = new { before = SafeState(before), after = SafeState(state) }, feedback });
        AssertBoundary();
        return new((int)response.StatusCode, code, decoded, state);
    }
    static object Power(long generation, string source = "selftest.native") => new { generation, source };
    static async Task<long> Cycle(int suspendQuietMs = 0)
    {
        var generation = ++Generation;
        var suspended = await Send("POST", "/api/suspend", Power(generation));
        Check("suspend accepted", suspended.Status == 200, new { suspended.Status, suspended.Code });
        if (suspendQuietMs > 0) await Task.Delay(suspendQuietMs);
        var resumed = await Send("POST", "/api/resume", Power(generation));
        Check("resume accepted", resumed.Status == 200, new { resumed.Status, resumed.Code });
        return generation;
    }
    static int ReserveRandomPort()
    {
        var reservation = new TcpListener(IPAddress.Loopback, 0); reservation.Start();
        var port = ((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop(); return port;
    }
    static object MachineGateObservation()
    {
        var rows = new List<object>();
        foreach (var name in new[] { @"Global\YeManFanHost-v2", @"Local\YeManFanHost-v2" })
        {
            try { var exists = Mutex.TryOpenExisting(name, out var handle); handle?.Dispose(); rows.Add(new { name, exists, readable = true, acquired = false }); }
            catch (UnauthorizedAccessException) { rows.Add(new { name, exists = (bool?)null, readable = false, acquired = false }); }
        }
        return rows;
    }
    public static async Task<int> MainAsync(string[] args)
    {
        if (args.Length != 6) { Console.Error.WriteLine("usage: payload-root expected-host-sha256 evidence-root repo-root mode expected-backend-sha256-or-empty"); return 2; }
        var hostRoot = Path.GetFullPath(args[0]); var expectedHash = args[1]; var evidenceRoot = Path.GetFullPath(args[2]);
        var repoRoot = Path.GetFullPath(args[3]); var mode = args[4]; var expectedBackendHash = args[5];
        Directory.CreateDirectory(evidenceRoot);
        string? failure = null; Task? running = null; var serverCompleted = false; var mainCalled = false;
        var initialMachineGates = MachineGateObservation(); var initial = DateTimeOffset.UtcNow;
        try
        {
            Check("supported explicit mode", new[] { "memory", "probe", "forms", "typeseries", "rapidwake" }.Contains(mode));
            var hostFile = Path.Combine(hostRoot, "YeManFanHost.dll");
            Check("explicit expected Host hash required", expectedHash.Length == 64 && expectedHash.All(Uri.IsHexDigit));
            Check("explicit actual Host hash matches", string.Equals(Hash(hostFile), expectedHash, StringComparison.OrdinalIgnoreCase));
            if (!string.IsNullOrWhiteSpace(expectedBackendHash)) Check("explicit Backend hash matches", string.Equals(Hash(Path.Combine(hostRoot, "YeManFanBackend.dll")), expectedBackendHash, StringComparison.OrdinalIgnoreCase));
            ExecutionPhase = "load";
            var assemblyMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var name in new[] { "YeManFanHost.dll", "YeManFanBackend.dll", "Microsoft.Windows.SDK.NET.dll", "WinRT.Runtime.dll" })
            {
                var file = Path.Combine(hostRoot, name); if (!File.Exists(file)) continue;
                var identity = AssemblyName.GetAssemblyName(file); assemblyMap.Add(identity.Name!, file);
                ArtifactHashes.Add(file, Hash(file));
            }
            var tpa = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "").Split(Path.PathSeparator).Where(File.Exists)
                .Select(file => (file, name: AssemblyName.GetAssemblyName(file).Name!)).GroupBy(row => row.name, StringComparer.OrdinalIgnoreCase)
                .ToDictionary(group => group.Key, group => group.First().file, StringComparer.OrdinalIgnoreCase);
            Assembly Load(AssemblyName requested)
            {
                if (assemblyMap.TryGetValue(requested.Name!, out var file))
                {
                    if (AssemblyName.GetAssemblyName(file).Version != requested.Version && requested.Version is not null) throw new FileLoadException("PINNED_DEPENDENCY_VERSION_MISMATCH");
                    var loaded = AssemblyLoadContext.Default.LoadFromAssemblyPath(file);
                    if (LoadedPaths.TryAdd(loaded.GetName().Name!, loaded.Location))
                    {
                        NativeLibrary.SetDllImportResolver(loaded, (name, _, _) => { NativeAttempts.Enqueue(name); throw new DllNotFoundException("TARGET_NATIVE_ACCESS_BLOCKED"); });
                        Identities.Add(new { path = loaded.Location, sha256 = Hash(loaded.Location), identity = loaded.FullName, moduleVersionId = loaded.ManifestModule.ModuleVersionId });
                    }
                    return loaded;
                }
                if (tpa.TryGetValue(requested.Name!, out var platform)) return AssemblyLoadContext.Default.LoadFromAssemblyPath(platform);
                throw new FileNotFoundException("DEPENDENCY_OUTSIDE_PINNED_HOST_AND_PLATFORM_WHITELIST", requested.Name);
            }
            AssemblyLoadContext.Default.Resolving += (_, name) => Load(name);
            var host = Load(new AssemblyName("YeManFanHost"));
            var program = host.GetType("YeManFanHost.Program", true)!;
            program.GetProperty("IsSelfTestProcess", All)!.SetValue(null, true);
            var logType = host.GetType("YeManFanHost.HostDiagnosticLog", true)!;
            Invoke(logType, "EnterSelfTestLogIsolation", null, "sleep-engine");
            var diagnostics = Activator.CreateInstance(logType, All, null, new object?[] { Path.Combine(evidenceRoot, "synthetic-engine-logs"), 0 }, null)!;
            ExecutionPhase = "options";
            var optionsType = host.GetType("YeManFanHost.HostOptions", true)!;
            var port = mode == "memory" ? 27979 : ReserveRandomPort();
            SessionToken = Convert.ToHexString(RandomNumberGenerator.GetBytes(32)).ToLowerInvariant();
            var options = Invoke(optionsType, "Parse", null, new object?[] { new[] { "--port", port.ToString(), "--mock-handshake", "--mock-zero-hardware-evidence", "--session-token", SessionToken } })!;
            Check("strict mock options; no parent or real settings", !(bool)Prop(options, "RealBackend")! && !(bool)Prop(options, "AllowHardwareWrites")!
                && (bool)Prop(options, "MockHandshake")! && (bool)Prop(options, "MockZeroHardwareEvidence")!
                && Prop(options, "ParentPid") is null && Prop(options, "ParentProcessName") is null && Prop(options, "ConfigPath") is null);
            ExecutionPhase = "engine";
            EngineType = host.GetType("YeManFanHost.FanHostEngine", true)!;
            Engine = Activator.CreateInstance(EngineType, All, null, new[] { options, diagnostics }, null)!;
            AssertBoundary();
            if (mode == "memory") { ExecutionPhase = "memory-signals"; await RunMemoryScenarios(repoRoot); }
            else
            {
            ExecutionPhase = "listener";
            ServerType = host.GetType("YeManFanHost.LocalFanHostServer", true)!;
            Server = Activator.CreateInstance(ServerType, All, null, new[] { Engine, options, diagnostics }, null)!;
            running = (Task)Invoke(ServerType, "RunAsync", Server)!;
            BaseUrl = $"http://127.0.0.1:{port}";
            Client = new HttpClient(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(8) };
            var handshake = await Send("POST", "/api/handshake");
            Check("actual authenticated HTTP handshake confirms strict mock", handshake.Status == 200 && Text(handshake.Body, "hostMode") == "mock-handshake" && Bool(handshake.Body, "mockZeroHardwareEvidence"));
            ExecutionPhase = "signals";
            await RunSelectedScenarios(mode, repoRoot);
            ExecutionPhase = "shutdown";
            if (Text(Snapshot(), "state") != "Stopped" || !Bool(Snapshot(), "closeCalled")) await Send("POST", "/api/close");
            var shutdown = await Send("POST", "/api/shutdown"); Check("authenticated shutdown accepted", shutdown.Status == 200);
            await running.WaitAsync(TimeSpan.FromSeconds(8)); serverCompleted = true;
            Check("server RunAsync naturally completed", serverCompleted);

            }
            Check("all supplied artifacts unchanged", ArtifactHashes.All(row => Hash(row.Key) == row.Value));
            ExecutionPhase = "completed";
        }
        catch (Exception error)
        {
            while (error is TargetInvocationException && error.InnerException is not null) error = error.InnerException;
            failure = error is InvalidOperationException && error.Message.StartsWith("CHECK_FAILED:", StringComparison.Ordinal) ? error.Message : error.GetType().Name;
        }
        finally
        {
            Client?.Dispose();
            try { if (Server is IDisposable server) server.Dispose(); } catch (Exception error) { failure ??= "SERVER_DISPOSE_FAILED:" + error.GetType().Name; }
            try { if (Engine is IDisposable engine) engine.Dispose(); } catch (Exception error) { failure ??= "ENGINE_DISPOSE_FAILED:" + error.GetType().Name; }
            if (running is not null) { try { await running.WaitAsync(TimeSpan.FromSeconds(8)); serverCompleted = true; } catch (Exception error) { failure ??= "SERVER_DID_NOT_FINISH:" + error.GetType().Name; } }
        }
        var report = new { schemaVersion = 1, kind = "sleep-engine-consumption", status = failure is not null ? "FAIL" : CompatibilityDifferences.Count > 0 ? "INCOMPLETE" : "PASS", mode,
            summary = new { checks = Checks.Count, scenarios = Scenarios.Count, forms = Scenarios.Count(row => row.Mode == "forms"), typeseries = Scenarios.Count(row => row.Mode == "typeseries"), rapidwake = Scenarios.Count(row => row.Mode == "rapidwake"), negative = Scenarios.Count(row => row.Mode == "negative"), failed = Scenarios.Count(row => row.Status == "FAIL"), compatibilityDifferences = CompatibilityDifferences.Count, requests = HttpRequestCount, memoryOperations = mode == "memory" ? Evidence.Count : 0 },
            process = new { pid = Environment.ProcessId, startedAtUtc = initial, serverCompleted, mainCalled, memoryEngineCompleted = mode == "memory" && failure is null, naturalExitObservedByParent = false },
            actualHost = new { payloadRoot = hostRoot, expectedSha256 = expectedHash, expectedBackendSha256 = expectedBackendHash, backendFileSha256 = ArtifactHashes.GetValueOrDefault(Path.Combine(hostRoot, "YeManFanBackend.dll")),
                pinnedArtifacts = ArtifactHashes.Select(row => new { path = row.Key, sha256 = row.Value, actuallyLoaded = LoadedPaths.Values.Contains(row.Key, StringComparer.OrdinalIgnoreCase) }).ToArray(), loaded = Identities },
            http = new { actualDispatch = HttpRequestCount > 0, baseUrl = BaseUrl, tokenSource = "fresh-random-in-memory", tokenPersisted = false },
            safety = new { operatingSystemSleepPerformed = false, hardwareWritesPerformed = false, hcLoaded = AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"),
                nativeAttempts = NativeAttempts.ToArray(), productionMainInvoked = false, nativeSchedulerExercised = false, networkUsed = HttpRequestCount > 0, localServerConstructed = Server is not null, physicalHardwareValidated = false, machineGateAcquired = false, existingHostStopped = false },
            productionMachineGates = new { before = initialMachineGates, after = MachineGateObservation() },
            checks = Checks, scenarios = Scenarios, fixtureIdentities = FixtureIdentities, compatibilityDifferences = CompatibilityDifferences, inputConsumerStateFeedback = Evidence, failure, failurePhase = failure is null ? null : ExecutionPhase,
            sourceScope = mode == "memory" ? "actual selected Host DLL;direct engine only;no network/server/Main/API dispatch or native scheduler" : "actual selected Host DLL; software engine and real loopback HTTP only;not native scheduler or physical sleep" };
        File.WriteAllText(Path.Combine(evidenceRoot, "sleep-engine-result.json"), JsonSerializer.Serialize(report, JsonOptions));
        Console.WriteLine($"SLEEP_ENGINE {report.status} checks={Checks.Count} events={Evidence.Count}");
        return failure is not null ? 1 : CompatibilityDifferences.Count > 0 ? 10 : 0;
    }
}








