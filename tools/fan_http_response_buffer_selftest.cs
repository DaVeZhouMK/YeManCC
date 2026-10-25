using System.Collections.Concurrent;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Checks
{
    private const BindingFlags All = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static Assembly host = null!;
    private static Type serverType = null!, engineType = null!, logType = null!;
    private static MethodInfo write = null!;
    private static JsonSerializerOptions options = null!;
    private static readonly List<object> results = [], observations = [];
    private static readonly List<string> failures = [];
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static async Task Check(string name, Func<Task> test)
    {
        try { await test(); results.Add(new { name, ok = true }); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { failures.Add(name + ": " + ex.Message); results.Add(new { name, ok = false, error = ex.Message }); Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }
    private sealed class HeldPayload
    {
        internal readonly ManualResetEventSlim Entered = new(false), Release = new(false);
        public string First => "held-first";
        public string Last { get { Entered.Set(); if (!Release.Wait(5000)) throw new InvalidOperationException("fixture-hold-expired"); return "held-last"; } }
    }
    private sealed class ThrowingPayload
    {
        internal bool Fail = true;
        internal int Reads;
        public string Current { get { Reads++; if (Fail) throw new InvalidOperationException("fixture-getter-fault"); return "live-recovery"; } }
    }
    private sealed record Outcome(bool Delivered, string? ErrorType);
    private sealed class Job(object value, int status, bool fallback, bool abort)
    {
        internal readonly object Value = value;
        internal readonly int Status = status;
        internal readonly bool Fallback = fallback, Abort = abort;
        internal readonly TaskCompletionSource<Outcome> Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal readonly TaskCompletionSource<Task<bool>> WriteStarted = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    private sealed record Delivery(byte[] Bytes, long? ContentLength, int Status, Outcome Outcome);
    private sealed class Fixture : IDisposable
    {
        private readonly object engine, log;
        internal readonly object Server;
        private readonly HttpListener listener = new();
        private readonly HttpClient client = new(new HttpClientHandler { UseProxy = false });
        private readonly ConcurrentDictionary<string, Job> jobs = new();
        private readonly string baseUrl;
        private long next;
        internal Fixture(string root)
        {
            Directory.CreateDirectory(root);
            log = Activator.CreateInstance(logType, All, null, [root, 0], null)!;
            var reservation = new TcpListener(IPAddress.Loopback, 0); reservation.Start();
            var port = ((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
            baseUrl = $"http://127.0.0.1:{port}/";
            var parsed = host.GetType("YeManFanHost.HostOptions", true)!.GetMethod("Parse", All)!.Invoke(null,
                [new[] { "--port", port.ToString(), "--mock-handshake", "--session-token", Guid.NewGuid().ToString("N") }]);
            engine = Activator.CreateInstance(engineType, All, null, [parsed, log], null)!;
            (engineType.GetField("runtimeDiagnosticsTimer", All)!.GetValue(engine) as Timer)?.Dispose();
            engineType.GetField("runtimeDiagnosticsTimer", All)!.SetValue(engine, null);
            Require(engineType.GetField("realBackend", All)!.GetValue(engine) is null, "real backend must stay absent");
            Server = Activator.CreateInstance(serverType, All, null, [engine, parsed, log], null)!;
            listener.Prefixes.Add(baseUrl); listener.Start(); client.Timeout = TimeSpan.FromSeconds(8);
        }
        internal object? Idle => serverType.GetField("idleJsonResponseBuffer", All)?.GetValue(Server);
        private async Task<bool> Write(HttpListenerContext context, int status, object value, Job? job = null)
        {
            try
            {
                var pending = (Task<bool>)write.Invoke(write.IsStatic ? null : Server, [context, status, value])!;
                job?.WriteStarted.TrySetResult(pending);
                return await pending;
            }
            catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
        }
        private async Task Process(HttpListenerContext context)
        {
            var id = context.Request.Url!.AbsolutePath.Trim('/');
            var job = jobs[id]; bool delivered = false; string? error = null;
            try
            {
                if (job.Abort) context.Response.Abort();
                delivered = await Write(context, job.Status, job.Value, job);
            }
            catch (Exception ex)
            {
                error = ex.GetType().Name;
                if (job.Fallback) delivered = await Write(context, 500, new { ok = false, error = "fixture-serialization-fault" });
                else { try { context.Response.Abort(); } catch { } }
            }
            finally
            {
                try { context.Response.Close(); } catch { }
                job.Completion.TrySetResult(new Outcome(delivered, error));
                jobs.TryRemove(id, out _);
            }
        }
        internal async Task<Delivery> Send(object value, int status = 200, bool fallback = false, bool abort = false)
        {
            var id = Interlocked.Increment(ref next).ToString(); var job = new Job(value, status, fallback, abort); jobs[id] = job;
            var accept = listener.GetContextAsync(); var pending = client.GetAsync(baseUrl + id);
            var context = await accept.WaitAsync(TimeSpan.FromSeconds(8));
            _ = Task.Run(() => Process(context));
            byte[] bytes = []; long? length = null; int code = 0;
            try { using var response = await pending; bytes = await response.Content.ReadAsByteArrayAsync(); length = response.Content.Headers.ContentLength; code = (int)response.StatusCode; }
            catch (HttpRequestException) when (abort) { }
            var outcome = await job.Completion.Task.WaitAsync(TimeSpan.FromSeconds(8));
            return new Delivery(bytes, length, code, outcome);
        }
        internal async Task VerifyPendingWriteOwnership()
        {
            var warm = new { value = "warm-before-slow-send" }; SameBody(await Send(warm), warm); var borrowed = Idle;
            var id = Interlocked.Increment(ref next).ToString(); var job = new Job(new { large = new string('q', 4 * 1024 * 1024) }, 200, false, false); jobs[id] = job;
            using var raw = new TcpClient(); raw.ReceiveBufferSize = 1024; raw.LingerState = new LingerOption(true, 0);
            var accept = listener.GetContextAsync(); var endpoint = new Uri(baseUrl);
            await raw.ConnectAsync(IPAddress.Loopback, endpoint.Port);
            await raw.GetStream().WriteAsync(System.Text.Encoding.ASCII.GetBytes($"GET /{id} HTTP/1.1\r\nHost: 127.0.0.1:{endpoint.Port}\r\nConnection: close\r\n\r\n"));
            var context = await accept.WaitAsync(TimeSpan.FromSeconds(8)); _ = Task.Run(() => Process(context));
            Outcome outcome = new(false, null);
            try
            {
                var actualWrite = await job.WriteStarted.Task.WaitAsync(TimeSpan.FromSeconds(8));
                Require(!actualWrite.IsCompleted, "slow socket did not hold the actual asynchronous write");
                Require(Idle is null, "active async send returned its buffer before completion");
                var other = new { value = "current-during-pending-send" }; SameBody(await Send(other), other);
                if (borrowed is not null) Require(Idle is not null && !ReferenceEquals(borrowed, Idle), "pending async send shared its borrowed owner");
                Require(!actualWrite.IsCompleted, "the original write was no longer held during the independent response");
                observations.Add(new { scenario = "actual-pending-output-write", inFlightConfirmed = true, independentResponseCompleted = true });
            }
            finally
            {
                raw.Dispose(); outcome = await job.Completion.Task.WaitAsync(TimeSpan.FromSeconds(8));
            }
            Require(!outcome.Delivered && outcome.ErrorType is null, "aborted held write was reported delivered or escaped transport handling");
            var recovered = new { value = "usable-after-held-send-abort" }; SameBody(await Send(recovered), recovered);
        }
        public void Dispose() { client.Dispose(); listener.Close(); ((IDisposable)Server).Dispose(); ((IDisposable)engine).Dispose(); }
    }
    private static void SameBody(Delivery response, object value, int status = 200)
    {
        var oracle = JsonSerializer.SerializeToUtf8Bytes(value, options);
        Require(response.Status == status && response.Outcome.Delivered && response.Outcome.ErrorType is null, "HTTP status or delivery result changed");
        Require(response.ContentLength == oracle.Length && response.Bytes.AsSpan().SequenceEqual(oracle), "body/Content-Length differs from the original serializer expression");
    }
    public static async Task<int> Main(string[] args)
    {
        var dll = Path.GetFullPath(args[0]); var root = Path.GetFullPath(args[1]); var resultFile = args[2]; Directory.CreateDirectory(root);
        var resolver = new AssemblyDependencyResolver(dll);
        AssemblyLoadContext.Default.Resolving += (_, name) => resolver.ResolveAssemblyToPath(name) is { } path ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
        host = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
        serverType = host.GetType("YeManFanHost.LocalFanHostServer", true)!; engineType = host.GetType("YeManFanHost.FanHostEngine", true)!; logType = host.GetType("YeManFanHost.HostDiagnosticLog", true)!;
        options = (JsonSerializerOptions)host.GetType("YeManFanHost.JsonDefaults", true)!.GetField("Options", All)!.GetValue(null)!;
        write = serverType.GetMethod("WriteJsonAsync", All)!;
        await Check("actual response bytes and Content-Length match the existing serializer across shapes", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "parity"));
            object[] values = [new { }, new { ok = true, state = "Ready", missing = (string?)null },
                new { text = "风扇🙂<>&\"\n\t", fraction = -0.25, count = long.MaxValue, value = 1e-30 },
                new { nodes = new[] { new { tempC = 30, dutyPercent = 25 }, new { tempC = 80, dutyPercent = 100 } }, lease = new { leaseId = "private-fixture-lease", generation = 7 } },
                new Dictionary<string, object?> { ["nested"] = new { value = "new" }, ["null"] = null, ["array"] = new[] { 1, 2, 3 } },
                JsonDocument.Parse("{\"ok\":true,\"unicode\":\"风扇\",\"array\":[1,null,false]}").RootElement.Clone(), null!];
            foreach (var value in values) SameBody(await f.Send(value), value);
            observations.Add(new { scenario = "wire-byte-oracle", responses = values.Length, exactBodyAndLength = true });
        });
        await Check("sequential small responses reuse one server-owned buffer without a stale suffix", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "reuse")); var first = new { text = new string('x', 10000), current = 1 }; SameBody(await f.Send(first), first); var owner = f.Idle;
            var second = new { current = 2 }; SameBody(await f.Send(second), second);
            Require(owner is not null && ReferenceEquals(owner, f.Idle), "sequential response buffer was not reused");
        });
        await Check("an oversized response remains complete and is not retained in the idle slot", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "oversized")); var warm = new { marker = "warm" }; SameBody(await f.Send(warm), warm); var prior = f.Idle;
            var large = new { marker = new string('z', 200000) }; SameBody(await f.Send(large), large); Require(f.Idle is null, "oversized storage remained resident");
            var fresh = new { marker = "after-large" }; SameBody(await f.Send(fresh), fresh); Require(f.Idle is not null && !ReferenceEquals(prior, f.Idle), "small response failed to obtain fresh bounded storage");
        });
        await Check("strict-number serialization failure still precedes transport and the next response recovers", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "number-fault")); var failed = await f.Send(new { value = double.NaN }, fallback: true);
            Require(failed.Status == 500 && failed.Outcome.ErrorType == "ArgumentException", "strict number error disappeared or changed classification");
            var fallback = JsonSerializer.SerializeToUtf8Bytes(new { ok = false, error = "fixture-serialization-fault" }, options);
            Require(failed.Bytes.AsSpan().SequenceEqual(fallback) && failed.ContentLength == fallback.Length, "partial invalid JSON leaked before fallback");
            var good = new { value = 17 }; SameBody(await f.Send(good), good);
        });
        await Check("getter faults are not cached and later current getter data is serialized", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "getter-fault")); var value = new ThrowingPayload(); var failed = await f.Send(value, fallback: true);
            Require(failed.Status == 500 && failed.Outcome.ErrorType == "InvalidOperationException" && value.Reads == 1, "getter exception contract changed");
            value.Fail = false; var recovered = await f.Send(value); Require(recovered.Status == 200 && recovered.Outcome.ErrorType is null && value.Reads == 2, "later getter recovery did not execute");
            Require(JsonDocument.Parse(recovered.Bytes).RootElement.GetProperty("current").GetString() == "live-recovery", "stale or partial getter value returned");
        });
        await Check("a held serializer cannot block another response or share its active storage", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "concurrent-owner")); var warm = new { value = "warm" }; SameBody(await f.Send(warm), warm); var borrowed = f.Idle; var held = new HeldPayload();
            var pending = f.Send(held);
            try
            {
                Require(await Task.Run(() => held.Entered.Wait(5000)), "held serialization did not enter"); Require(!pending.IsCompleted, "held writer was not live");
                var other = new { value = "parallel-current", items = Enumerable.Range(0, 100).ToArray() }; SameBody(await f.Send(other), other);
                if (borrowed is not null) Require(f.Idle is not null && !ReferenceEquals(borrowed, f.Idle), "a borrowed buffer entered the idle slot before ownership settled");
            }
            finally { held.Release.Set(); }
            SameBody(await pending, held);
        });
        await Check("actual pending output write retains ownership while an independent request completes", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "pending-output-write")); await f.VerifyPendingWriteOwnership();
        });
        await Check("parallel responses keep their own current bytes and exact lengths", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "parallel-wave")); var values = Enumerable.Range(0, 24).Select(i => (object)new { sequence = i, text = new string((char)('a' + i), 256 + i * 541), label = "风扇" }).ToArray();
            var returned = await Task.WhenAll(values.Select(value => f.Send(value))); for (var i = 0; i < values.Length; i++) SameBody(returned[i], values[i]);
            observations.Add(new { scenario = "parallel-wire-parity", responses = values.Length, exactBodyAndLength = true });
        });
        await Check("closed response transport returns undelivered and leaves a fresh response usable", async () =>
        {
            using var f = new Fixture(Path.Combine(root, "aborted")); var failed = await f.Send(new { text = "not-delivered" }, abort: true);
            Require(!failed.Outcome.Delivered && failed.Outcome.ErrorType is null, "known transport failure escaped or became success");
            var good = new { text = "next-current-response" }; SameBody(await f.Send(good), good);
        });
        await Check("separate Host instances never share response storage", async () =>
        {
            using var a = new Fixture(Path.Combine(root, "instance-a")); using var b = new Fixture(Path.Combine(root, "instance-b"));
            var av = new { instance = "a" }; var bv = new { instance = "b" }; SameBody(await a.Send(av), av); SameBody(await b.Send(bv), bv);
            Require(a.Idle is not null && b.Idle is not null && !ReferenceEquals(a.Idle, b.Idle), "buffer ownership crossed Host instances");
        });
        await Check("response-only fixtures never initialize HC or a production backend", () =>
        {
            Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"), "HC assembly was loaded"); return Task.CompletedTask;
        });
        var report = new { ok = failures.Count == 0, checks = results.Count, results, failures, observations,
            hostDllSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(), hardwareCalls = 0, cpuBenchmark = false,
            scope = "Actual private WriteJsonAsync and real private loopback HTTP; original SerializeToUtf8Bytes oracle; inert engine/no HC/backend; synthetic getter hold and closed response; no Host Main, device, OS sleep or CPU/allocation timing" };
        var json = JsonSerializer.Serialize(report); File.WriteAllText(resultFile, json); Console.WriteLine(json); return failures.Count == 0 ? 0 : 1;
    }
}
