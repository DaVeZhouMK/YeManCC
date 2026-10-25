// Child entry for the offline native/managed pipe integration fixture only.
// Program.RunPipeClientAsync, MessageCodec and ParentProcessLifetime are copied
// verbatim from the current product. HostSession members come from the existing
// exact-consumer fixture. Only the admitted SDK/PnP/diagnostic ends are fakes.
using System.Diagnostics;
using System.IO.Pipes;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

internal static partial class Program
{
    private static HostSession? _managedSession;
    private static readonly List<object> _managedObservations = new();
    private static readonly List<object> _managedOutputStops = new();
    private static readonly List<object> _managedExitReasons = new();
    private static string _managedFault = "none";
    private static bool _managedFaultConsumed;
    private static string? _managedLastCommand;
    private static ulong _managedLastSequence;

    // Extends the pre-existing no-op diagnostic declaration, never Handle.
    private sealed partial class Stage
    {
        public void BeginRead() => ManagedObserve("pipe-read-begin");
        public void EndRead(int length) => ManagedObserve("pipe-read-end", new { length });
        public void BeginHandle() => ManagedObserve("handle-begin");
        public void EndHandle() => ManagedObserve("handle-end");
        public void BeginWrite() => ManagedObserve("pipe-write-begin");
        public void EndWrite() => ManagedObserve("pipe-write-end");
    }

    private sealed partial class HostSession
    {
        // Read-only fixture observation. These are the existing held objects.
        public string ManagedPersona => _tuple?.Persona ?? "";
        public object ManagedSnapshot() => new
        {
            hasController = HasController,
            hasContext = HasContext,
            phase = PhaseName,
            localReleasePending = LocalReleasePending,
            outputReleasePending = _outputReleasePending,
            idleCtsReleasePending = _idleCtsReleasePending,
            steamDeckReleasePending = _steamDeckReleasePending,
            lastSequence = _lastSequence,
            targetPnpAdmitted = _targetPnpAdmitted,
            tuple = _tuple is null ? null : new
            {
                _tuple.RunId, _tuple.Epoch, _tuple.PowerGeneration, _tuple.TargetId,
                _tuple.OwnerIdentity, _tuple.Persona, _tuple.ProfileIdentity,
                _tuple.StartupNonce, _tuple.ConfigRevision, _tuple.ConfigHash
            },
            ownedReleaseEvidence = Receipt
        };

        // Timing is disabled in the reused fixture. No CPU collector is added.
        public bool CostEnabledForRead => false;
        public void RecordCostPipeRead(long microseconds) { }
    }

    private static void ManagedObserve(string kind, object? data = null)
    {
        _managedObservations.Add(new
        {
            index = _managedObservations.Count,
            kind,
            command = _managedLastCommand,
            sequence = _managedLastSequence,
            eventCount = Fixture.Events.Count,
            state = _managedSession?.ManagedSnapshot(),
            data
        });
    }

    // Used only by the existing inert Stage.Decode endpoint.
    private static void ManagedDecode(string requestId, ulong sequence, string runId,
        ulong epoch, ulong powerGeneration, string command)
    {
        if (_managedFault is "controller-dispose-once" or "context-dispose-once")
        {
            Fixture.ControllerThrows=Fixture.ContextThrows=false;
            if (!_managedFaultConsumed && command=="RELEASE_TARGET")
            {
                Fixture.ControllerThrows=_managedFault=="controller-dispose-once";
                Fixture.ContextThrows=_managedFault=="context-dispose-once";
                _managedFaultConsumed=true;
            }
        }
        _managedLastCommand = command;
        _managedLastSequence = sequence;
        GameMotorOnDecoded(command, sequence);
        ManagedObserve("command-decoded", new { requestId, runId, epoch, powerGeneration });
    }

    // Used only at the existing fake output-stop return expression. It returns
    // the original false once, then rearms the fake for the next real call.
    private static bool ManagedOutputStopResult(bool result)
    {
        var consumedNow = _managedFault == "output-stop-once" && !_managedFaultConsumed && !result;
        if (consumedNow)
        {
            _managedFaultConsumed = true;
            Fixture.OutputStops = true;
        }
        _managedOutputStops.Add(new
        {
            attempt = _managedOutputStops.Count + 1,
            eventIndex = Fixture.Events.Count - 1,
            command = _managedLastCommand,
            sequence = _managedLastSequence,
            result,
            faultConsumedNow = consumedNow,
            outputStopsForNextCall = Fixture.OutputStops,
            stateBeforeCallerUpdatesPendingFlags = _managedSession?.ManagedSnapshot()
        });
        ManagedObserve("output-stop-return", new { result, consumedNow });
        return result;
    }

    // Replaces only fatal diagnostic IO; records the production loop's cause.
    private static void LogFatalExit(string reason, string detail)
    {
        _managedExitReasons.Add(new
        {
            index = _managedExitReasons.Count,
            reason,
            detail,
            eventCount = Fixture.Events.Count
        });
        ManagedObserve("exit-reason", new { reason, detail });
    }

    private static VirtualEndpointObservation ManagedPnpObservation()
    {
        // Existing fake CreateController sets Fixture.Current to Arrival;
        // existing RemoveAll's OnSweep returns it to Empty. Translate only this
        // admitted observation end for the two non-SteamDeck current personas.
        var current = Fixture.Current;
        if (current.IsFullyAbsent) return current;
        return _managedSession?.ManagedPersona switch
        {
            "elite" => new(true, 1, 0, 1, 0, 0, true, "fixture-elite-arrival"),
            "dualsense-edge" => new(true, 1, 0, 1, 0, 0, true, "fixture-edge-arrival"),
            _ => current
        };
    }

    private sealed record ManagedOptions(string Pipe, int ParentPid, string Receipt, string Fault, string Nonce);
    private static bool TryManagedOptions(string[] args, out ManagedOptions? options)
    {
        options = null;
        if (args.Length is not (6 or 8 or 10)) return false;
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            if (args[i] is not ("--pipe" or "--parent-pid" or "--receipt" or "--fault" or "--nonce") ||
                !values.TryAdd(args[i], args[i + 1])) return false;
        }
        if (!values.TryGetValue("--pipe", out var pipe) || string.IsNullOrWhiteSpace(pipe) ||
            pipe.Length > 220 || pipe.IndexOfAny(new[] { '\\', '/', ':', '\0' }) >= 0 ||
            !values.TryGetValue("--parent-pid", out var parent) ||
            !int.TryParse(parent, out var parentPid) || parentPid <= 0 ||
            !values.TryGetValue("--receipt", out var receipt) || !Path.IsPathFullyQualified(receipt) ||
            !receipt.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ||
            File.Exists(receipt) || Directory.Exists(receipt)) return false;
        var fault = values.GetValueOrDefault("--fault", "none");
        if (fault is not ("none" or "output-stop-once" or "controller-dispose-once" or "context-dispose-once" or "game-fast" or "game-dom" or "game-stale-ticket" or "game-stale-tuple" or "game-replay" or "game-unsupported" or "game-zero-unbind" or "game-unbind-old")) return false;
        var nonce=values.GetValueOrDefault("--nonce","fake-nonce");
        if(string.IsNullOrWhiteSpace(nonce)||nonce.Length>160)return false;
        options = new(pipe, parentPid, Path.GetFullPath(receipt), fault, nonce);
        return true;
    }

    private static string ManagedHash(string file)
    {
        using var stream = File.OpenRead(file);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static object[] CheckManagedBindings(JsonElement manifest, string property, bool frozen)
    {
        var checks = new List<object>();
        foreach (var row in manifest.GetProperty(property).EnumerateArray())
        {
            var file = row.GetProperty(frozen ? "snapshot" : "path").GetString()!;
            var expected = row.GetProperty("sha256").GetString()!;
            var actual = ManagedHash(file);
            if (actual != expected) throw new InvalidDataException("binding-changed:" + file);
            checks.Add(new { path = file, sha256 = actual, matched = true });
        }
        return checks.ToArray();
    }

    private static object[] LoadedManagedBindings(JsonElement inputs)
    {
        var expected = inputs.GetProperty("toolchainAllowlist").EnumerateArray()
            .ToDictionary(r => Path.GetFullPath(r.GetProperty("path").GetString()!),
                r => r.GetProperty("sha256").GetString()!, StringComparer.OrdinalIgnoreCase);
        var own = typeof(Program).Assembly.Location;
        return AppDomain.CurrentDomain.GetAssemblies().Where(a => !a.IsDynamic)
            .Select(a => new { assembly = a.GetName().Name, path = a.Location })
            .Where(a => a.path.Length > 0).Select(a =>
            {
                var hash = ManagedHash(a.path);
                var ownAssembly = string.Equals(own, a.path, StringComparison.OrdinalIgnoreCase);
                if (!ownAssembly && (!expected.TryGetValue(a.path, out var pin) || pin != hash))
                    throw new InvalidDataException("loaded-assembly-not-bound:" + a.path);
                return (object)new { a.assembly, a.path, sha256 = hash, ownAssembly };
            }).ToArray();
    }

    private static async Task<int> Main(string[] args)
    {
        if (!TryManagedOptions(args, out var parsed))
        {
            Console.Error.WriteLine("fixture arguments invalid; use --pipe NAME --parent-pid PID --receipt ABS_NEW_JSON [--fault output-stop-once]");
            return 2;
        }
        var options = parsed!;
        _managedFault = options.Fault;
        _managedSession = new HostSession(options.Nonce, options.ParentPid);
        var session = _managedSession;
        object? stateAfterLoop = null;
        object? stateAfterFinalCleanup = null;
        object? startupBindings = null;
        object? finalBindings = null;
        JsonElement? inputManifest = null;
        string? buildReceiptHash = null;
        string? buildReceiptPath = null;
        string? failure = null;
        var pipeLoopReturned = false;
        var finalCleanupAttempted = false;
        bool? finalCleanupOk = null;
        string? finalCleanupDetail = null;
        int? pipeLoopExitCode = null;
        var requestedExitCode = 3;
        var inputPath = Path.Combine(AppContext.BaseDirectory, "fixture-manifest.json");
        try
        {
            buildReceiptPath = inputPath;
            buildReceiptHash = ManagedHash(inputPath);
            using var build = JsonDocument.Parse(File.ReadAllBytes(inputPath));
            var bindingText = build.RootElement.GetProperty("inputBindings").GetRawText();
            var bindingHash = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(bindingText))).ToLowerInvariant();
            if (bindingHash != ManagedFixtureIdentity.InputManifestSha256 ||
                build.RootElement.GetProperty("status").GetString() != "BUILT" ||
                build.RootElement.GetProperty("inputManifestSha256").GetString() != ManagedFixtureIdentity.InputManifestSha256)
                throw new InvalidDataException("build-manifest-not-bound");
            inputManifest = build.RootElement.GetProperty("inputBindings").Clone();
            var manifest = inputManifest.Value;
            var outputChecks = new List<object>();
            foreach (var row in build.RootElement.GetProperty("runtimeArtifacts").EnumerateArray())
            {
                var file = row.GetProperty("path").GetString()!;
                var actual = ManagedHash(file);
                if (actual != row.GetProperty("sha256").GetString())
                    throw new InvalidDataException("runtime-artifact-changed:" + file);
                outputChecks.Add(new { path = file, sha256 = actual, matched = true });
            }
            startupBindings = new
            {
                sourceOriginals = CheckManagedBindings(manifest, "sources", false),
                sourceSnapshots = CheckManagedBindings(manifest, "sources", true),
                generated = CheckManagedBindings(manifest, "generatedInputs", false),
                runtimeArtifacts = outputChecks,
                loadedAssemblies = LoadedManagedBindings(manifest)
            };

            Fixture.Reset();
            Fixture.OutputStops = options.Fault != "output-stop-once";
            Fixture.IdleStops = true;
            Fixture.FrameFails = false;
            Fixture.OnSweep = () => Fixture.Current = Empty("fixture-removal-observed");
            Fixture.OnObserve = ManagedPnpObservation;
            _msiHostStageEvidence = new Stage();
            ManagedObserve("child-ready", new { options.Pipe, options.ParentPid, options.Fault });
            pipeLoopExitCode = await RunPipeClientAsync(options.Pipe, options.ParentPid, session).ConfigureAwait(false);
            pipeLoopReturned = true;
            requestedExitCode = pipeLoopExitCode.Value;
            stateAfterLoop = session.ManagedSnapshot();
        }
        catch (Exception ex)
        {
            failure = ex.GetType().Name + ":" + ex.Message;
            requestedExitCode = 3;
            LogFatalExit("fixture-exception", failure);
        }
        finally
        {
            stateAfterLoop ??= session.ManagedSnapshot();
            // Match production Main's final cleanup using the real ReleaseLocal.
            // The first failed stop on pipe break can be retried here, just as
            // in the product; stateAfterLoop preserves the retained objects.
            try
            {
                finalCleanupAttempted = true;
                finalCleanupOk = session.ReleaseLocal(out var detail);
                finalCleanupDetail = detail;
            }
            catch (Exception ex)
            {
                failure ??= "final-cleanup:" + ex.GetType().Name + ":" + ex.Message;
                requestedExitCode = 3;
            }
            stateAfterFinalCleanup = session.ManagedSnapshot();
            _msiHostStageEvidence = null;
            try
            {
                if (inputManifest is { } manifest)
                {
                    if (ManagedHash(inputPath) != buildReceiptHash)
                        throw new InvalidDataException("manifest-changed-during-child");
                    finalBindings = new
                    {
                        sourceOriginals = CheckManagedBindings(manifest, "sources", false),
                        sourceSnapshots = CheckManagedBindings(manifest, "sources", true),
                        generated = CheckManagedBindings(manifest, "generatedInputs", false),
                        loadedAssemblies = LoadedManagedBindings(manifest)
                    };
                }
            }
            catch (Exception ex)
            {
                failure ??= "final-binding:" + ex.GetType().Name + ":" + ex.Message;
                requestedExitCode = 3;
            }
            // A child can record that it is returning, but cannot observe its
            // own OS exit. The native parent must confirm wait + exit code.
            var receipt = new
            {
                schemaVersion = 1,
                status = failure is null && pipeLoopReturned && finalBindings is not null ? "OBSERVED" : "INCOMPLETE",
                behaviorPass = (bool?)null,
                scope = "Actual managed pipe loop and HostSession; SDK/PnP/diagnostic terminals are admitted fakes. Native parent evaluates behavior and observes natural exit.",
                processId = Environment.ProcessId,
                parentPid = options.ParentPid,
                pipeName = options.Pipe,
                startupNonce = options.Nonce,
                fault = options.Fault,
                faultConsumed = _managedFaultConsumed,
                faultTriggered = _managedFaultConsumed,
                pipeLoopReturned,
                pipeLoopExitCode,
                pipeExitCode = pipeLoopExitCode,
                requestedExitCode,
                naturalExitObserved = (bool?)null,
                exitObservationOwner = "native-parent-wait",
                exitReasons = _managedExitReasons,
                failure,
                stateAfterLoop,
                stateAfterFinalCleanup,
                hasController = session.HasController,
                hasContext = session.HasContext,
                phase = session.PhaseName,
                finalCleanupAttempted,
                finalCleanupOk,
                finalCleanupDetail,
                events = Fixture.Events.ToArray(),
                eventTrace = Fixture.Events.Select((value, index) => new { index, value }).ToArray(),
                outputStopResults = _managedOutputStops,
                gameMotorEvents = _gameMotorEvents,
                observations = _managedObservations,
                creates = Fixture.Creates,
                sweeps = Fixture.Sweeps,
                probes = Fixture.Probes,
                lockedCoreSha256 = LockedCoreSha256,
                lockedCoreMvid = LockedCoreMvid,
                sdkIdentityScope = "Current product constants only; the asset verifier is an explicit fake, not real SDK identity acceptance.",
                inputManifestPath = inputPath,
                inputManifestSha256 = ManagedFixtureIdentity.InputManifestSha256,
                buildReceiptPath,
                buildReceiptSha256 = buildReceiptHash,
                inputBindings = inputManifest,
                startupBindings,
                finalBindings,
                actualProductionPipeLoop = true,
                actualProductionMessageCodec = true,
                actualProductionParentProcessLifetime = true,
                actualProductionHostSessionConsumer = true,
                productMainExecuted = false,
                hardwareIo = false,
                realSdkLoaded = false
            };
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(options.Receipt)!);
                using var stream = new FileStream(options.Receipt, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                await JsonSerializer.SerializeAsync(stream, receipt, new JsonSerializerOptions { WriteIndented = true }).ConfigureAwait(false);
                await stream.FlushAsync().ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("fixture receipt write failed: " + ex.GetType().Name);
                requestedExitCode = 3;
            }
        }
        Console.WriteLine(JsonSerializer.Serialize(new { receipt = options.Receipt, requestedExitCode, pipeLoopReturned }));
        return requestedExitCode;
    }
}
