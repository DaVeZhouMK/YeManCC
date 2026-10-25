using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using YeManInputHost.Diagnostics;

// Standalone diagnostic writer. Compile ONLY this file and the actual helper.
// All checkpoints below are fixture observations; no input backend is invoked.
internal static class MsiHostStageChannelFixture
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private sealed class Request
    {
        public int ParentPid { get; set; }
        public string HostInstance { get; set; } = "";
        public string RequestId { get; set; } = "";
        public string RunId { get; set; } = "";
        public string Command { get; set; } = "";
        public ulong Sequence { get; set; }
        public ulong Epoch { get; set; }
        public ulong PowerGeneration { get; set; }
        public int IncomingChars { get; set; }
    }

    private static void Emit(object value)
    {
        Console.WriteLine(JsonSerializer.Serialize(value, JsonOptions));
        Console.Out.Flush();
    }

    private static JsonElement Snapshot(MsiHostStageEvidence evidence) =>
        JsonSerializer.SerializeToElement(evidence.FatalSnapshot(), JsonOptions);

    private static async Task<int> Main(string[] args)
    {
        MsiHostStageEvidence? evidence = null;
        var lifetime = Stopwatch.StartNew();
        try
        {
            if (args.Length != 1) throw new ArgumentException("Exactly one private fixture request path is required.");
            var request = JsonSerializer.Deserialize<Request>(File.ReadAllText(args[0]), JsonOptions)
                ?? throw new InvalidDataException("Missing fixture request.");
            if (!request.RequestId.StartsWith("stage-fixture-", StringComparison.Ordinal) ||
                !request.RunId.StartsWith("stage-run-", StringComparison.Ordinal) ||
                request.Command != "fixture_stage_only")
                throw new InvalidDataException("Fixture-only identity and command required.");

            // This is the actual Windows CreateNew factory, never TryCreateForTest.
            evidence = MsiHostStageEvidence.TryCreate(request.ParentPid, request.HostInstance);
            if (evidence is null)
            {
                Emit(new { ok = false, apiIssue = "Actual MsiHostStageEvidence.TryCreate returned null; no cause inferred.",
                    actualTryCreateCalled = true, realMappingCreated = false });
                return 2;
            }
            Emit(new { ok = true, op = "ready", processId = Environment.ProcessId,
                actualTryCreateCalled = true, realMappingCreated = true,
                snapshot = Snapshot(evidence), lifetimeLimitMs = 24000,
                loadedAssemblies = AppDomain.CurrentDomain.GetAssemblies()
                    .Select(a => a.GetName().Name).OrderBy(n => n).ToArray() });

            while (true)
            {
                var remaining = TimeSpan.FromMilliseconds(24000 - lifetime.ElapsedMilliseconds);
                if (remaining <= TimeSpan.Zero) throw new TimeoutException("Fixture lifetime exceeded.");
                string? line = await Console.In.ReadLineAsync().WaitAsync(remaining);
                if (line is null) return 0;
                using var command = JsonDocument.Parse(line);
                var op = command.RootElement.GetProperty("op").GetString();
                switch (op)
                {
                    case "collision":
                        var before = Snapshot(evidence);
                        var duplicate = MsiHostStageEvidence.TryCreate(request.ParentPid, request.HostInstance);
                        bool returnedNull = duplicate is null;
                        duplicate?.Dispose();
                        var after = Snapshot(evidence);
                        bool unchanged = before.GetRawText() == after.GetRawText();
                        Emit(new { ok = returnedNull && unchanged, op, collisionReturnedNull = returnedNull,
                            existingSnapshotUnchanged = unchanged, before, after });
                        break;
                    case "publish":
                        // Diagnostic stamps and parsed test metadata ONLY.
                        evidence.BeginRead();
                        evidence.EndRead(request.IncomingChars);
                        evidence.BeginHandle();
                        evidence.Decode(request.RequestId, request.Sequence, request.RunId,
                            request.Epoch, request.PowerGeneration, request.Command);
                        evidence.BeginSubmit();
                        evidence.EndSubmit();
                        evidence.EndHandle();
                        evidence.BeginWrite();
                        evidence.EndWrite();
                        Emit(new { ok = true, op, snapshot = Snapshot(evidence), inputSubmissions = 0 });
                        break;
                    case "beginRead":
                        evidence.BeginRead();
                        Emit(new { ok = true, op, snapshot = Snapshot(evidence) });
                        break;
                    case "dispose":
                        evidence.Dispose();
                        Emit(new { ok = true, op, snapshot = Snapshot(evidence), disposed = true });
                        break;
                    case "quit":
                        Emit(new { ok = true, op, lifetimeMs = lifetime.ElapsedMilliseconds });
                        return 0;
                    default:
                        throw new InvalidDataException("Unsupported fixture operation.");
                }
            }
        }
        catch (Exception ex)
        {
            Emit(new { ok = false, error = ex.GetType().Name, detail = ex.Message,
                lifetimeMs = lifetime.ElapsedMilliseconds });
            return 3;
        }
        finally { evidence?.Dispose(); }
    }
}
