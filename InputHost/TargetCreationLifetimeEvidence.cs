using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace YeManInputHost.Diagnostics;

// Diagnostic state only. The caller holds its existing _lifecycleGate. No SDK
// methods, object inspection, I/O, clocks, OS identity or protocol counters are
// used. One helper per host Session; never call it from SUBMIT_FRAME.
internal sealed class TargetCreationLifetimeEvidence
{
    internal const int Schema = 1;
    internal const int Version = 1;
    internal const string EvidenceField = "targetCreationLifetimeEvidence";
    internal const int MaximumResponseBytes = 64 * 1024;
    internal const int MaximumEvidenceBytes = 2048;
    private const int MaximumRootProperties = 128;
    private const string Source = "caller-confirmed-sdk-target-object-lifecycle";
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);

    private enum State { Unknown, Attempting, Live, Closed }
    private readonly string _namespace;
    private ulong _attemptOrdinal;
    private ulong _creationOrdinal;
    private State _state = State.Unknown;
    private bool _disabled;
    private string? _lifetimeToken;
    private string? _lifetimeCreationOrdinal;
    private string? _closedLifetimeToken;
    private string? _closedCreationOrdinal;
    private string _reason = "not-observed";

    // Owner reference + monotonic local ordinal prevents foreign/stale tickets.
    // Tickets never contain or infer runId/epoch/power/request/OS identity.
    internal readonly struct Attempt
    {
        internal Attempt(TargetCreationLifetimeEvidence owner, ulong ordinal)
        { Owner = owner; Ordinal = ordinal; }
        internal TargetCreationLifetimeEvidence? Owner { get; }
        internal ulong Ordinal { get; }
        internal bool IsValid => Owner is not null && Ordinal != 0;
    }

    private TargetCreationLifetimeEvidence(string localNamespace, ulong attemptOrdinal, ulong creationOrdinal)
    { _namespace = localNamespace; _attemptOrdinal = attemptOrdinal; _creationOrdinal = creationOrdinal; }

    internal static TargetCreationLifetimeEvidence? TryCreate()
    {
        try { return new(Guid.NewGuid().ToString("N"), 0, 0); }
        catch { return null; } // Missing diagnostic helper means unknown.
    }

    // Pure fixtures link this exact file. No runtime uses a seeded counter or
    // an injected namespace source; neither is a device/SDK factory.
    internal static TargetCreationLifetimeEvidence? TryCreateForTest(ulong attemptOrdinal = 0,
        ulong creationOrdinal = 0, Func<string?>? namespaceSource = null)
    {
        try
        {
            var localNamespace = namespaceSource is null ? Guid.NewGuid().ToString("N") : namespaceSource();
            if (localNamespace is null || localNamespace.Length != 32) return null;
            foreach (var c in localNamespace)
                if (c is not (>= '0' and <= '9') and not (>= 'a' and <= 'f')) return null;
            return new(localNamespace, attemptOrdinal, creationOrdinal);
        }
        catch { return null; }
    }

    internal Attempt BeginAttempt()
    {
        try
        {
            CloseCurrent(); // Even failed/new attempts cannot borrow an old token.
            if (_disabled) { _state = State.Unknown; return default; }
            if (_attemptOrdinal == ulong.MaxValue) { Disable("attempt-counter-overflow"); return default; }
            ++_attemptOrdinal;
            _state = State.Attempting;
            _reason = "creation-attempt-started";
            return new(this, _attemptOrdinal);
        }
        catch { Disable("diagnostic-exception"); return default; }
    }

    internal bool Created(Attempt attempt, object? sdkTargetObject)
    {
        try
        {
            if (_disabled) { CloseCurrent(); _state = State.Unknown; return false; }
            if (!Matches(attempt) || _state != State.Attempting)
            { Unknown("missing-foreign-stale-or-completed-attempt"); return false; }
            if (sdkTargetObject is null) { Unknown("sdk-create-returned-null"); return false; }
            if (_creationOrdinal == ulong.MaxValue) { Disable("creation-counter-overflow"); return false; }
            // Build bounded diagnostics before committing a successful ordinal.
            var next = _creationOrdinal + 1;
            var ordinal = Exact(next);
            var token = "target-creation:v1:" + _namespace + ":" + ordinal;
            _creationOrdinal = next;
            _lifetimeToken = token;
            _lifetimeCreationOrdinal = ordinal;
            _state = State.Live;
            _reason = "sdk-object-created";
            return true;
        }
        catch { Disable("diagnostic-exception"); return false; }
    }

    internal void ReleaseAttempt()
    {
        try
        {
            CloseCurrent(); // Before neutral/Dispose, even when Dispose later fails.
            _state = _disabled ? State.Unknown : State.Closed;
            _reason = _disabled ? _reason : "release-attempt-started";
        }
        catch { Disable("diagnostic-exception"); }
    }

    // Also valid for post-create preparation failure or failure after cleanup.
    // A stale failure closes current evidence instead of affecting SDK policy.
    internal void Failure(Attempt attempt)
    {
        try
        {
            if (_disabled) { CloseCurrent(); _state = State.Unknown; return; }
            Unknown(Matches(attempt) ? "creation-attempt-failed" : "missing-foreign-or-stale-failure");
        }
        catch { Disable("diagnostic-exception"); }
    }

    private bool Matches(Attempt attempt) => attempt.Owner == this && attempt.Ordinal != 0 &&
        attempt.Ordinal == _attemptOrdinal;
    private static string Exact(ulong value) => value.ToString(CultureInfo.InvariantCulture);
    private void CloseCurrent()
    {
        if (_lifetimeToken is not null)
        {
            _closedLifetimeToken = _lifetimeToken;
            _closedCreationOrdinal = _lifetimeCreationOrdinal;
        }
        _lifetimeToken = null;
        _lifetimeCreationOrdinal = null;
    }
    private void Unknown(string reason) { CloseCurrent(); _state = State.Unknown; _reason = reason; }
    private void Disable(string reason) { _disabled = true; Unknown(reason); }

    internal sealed class Snapshot
    {
        [JsonPropertyName("schema")] public int Schema { get; init; } = TargetCreationLifetimeEvidence.Schema;
        [JsonPropertyName("version")] public int Version { get; init; } = TargetCreationLifetimeEvidence.Version;
        [JsonPropertyName("source")] public string Source { get; init; } = TargetCreationLifetimeEvidence.Source;
        [JsonPropertyName("state")] public string State { get; init; } = "unknown";
        [JsonPropertyName("reason")] public string Reason { get; init; } = "not-observed";
        [JsonPropertyName("namespace")] public string Namespace { get; init; } = "";
        [JsonPropertyName("attemptOrdinal")] public string AttemptOrdinal { get; init; } = "0";
        [JsonPropertyName("creationOrdinal")] public string CreationOrdinal { get; init; } = "0";
        [JsonPropertyName("lifetimeToken")] public string? LifetimeToken { get; init; }
        [JsonPropertyName("closed")] public bool Closed { get; init; } = true;
        [JsonPropertyName("closedLifetimeToken")] public string? ClosedLifetimeToken { get; init; }
        [JsonPropertyName("closedCreationOrdinal")] public string? ClosedCreationOrdinal { get; init; }
        [JsonPropertyName("disabled")] public bool Disabled { get; init; }
        [JsonPropertyName("scope")] public string Scope { get; init; } = "host-local-SDK-object-creation-lifetime-diagnostic-only";
        [JsonPropertyName("protocolTupleIncluded")] public bool ProtocolTupleIncluded { get; init; }
        [JsonPropertyName("callerLockScope")] public string CallerLockScope { get; init; } = "existing-host-lifecycle-lock-required;not-independently-observed";
        [JsonPropertyName("osIncarnationProven")] public bool OSIncarnationProven { get; init; }
        [JsonPropertyName("sdkOwnershipProven")] public bool SdkOwnershipProven { get; init; }
        [JsonPropertyName("steamConsumptionProven")] public bool SteamConsumptionProven { get; init; }
        [JsonPropertyName("gameInputUnverified")] public bool GameInputUnverified { get; init; } = true;
    }

    internal Snapshot? Capture()
    {
        try
        {
            return new()
            {
                State = _state switch { State.Attempting => "attempting", State.Live => "live",
                    State.Closed => "closed", _ => "unknown" },
                Reason = _reason, Namespace = "target-creation-local:v1:" + _namespace,
                AttemptOrdinal = Exact(_attemptOrdinal), CreationOrdinal = Exact(_creationOrdinal),
                LifetimeToken = _lifetimeToken, Closed = _lifetimeToken is null,
                ClosedLifetimeToken = _closedLifetimeToken, ClosedCreationOrdinal = _closedCreationOrdinal,
                Disabled = _disabled
            };
        }
        catch { Disable("diagnostic-exception"); return null; }
    }

    private static bool LifecycleCommand(string? command) =>
        command is "PREPARE_TARGET" or "RECREATE_TARGET" or "RELEASE_TARGET" or "SHUTDOWN";

    // This suffix preserves all original characters/bytes; it never reserializes
    // the enclosing protocol tuple. Every failure returns the SAME original.
    // No caller should replace its normal response/error with helper success.
    internal string? TryAppend(string? originalResponse, string? command, int maxMessageBytes)
    {
        if (!LifecycleCommand(command) || originalResponse is null) return originalResponse;
        try
        {
            if (maxMessageBytes <= 0 || maxMessageBytes > MaximumResponseBytes ||
                originalResponse.Length > maxMessageBytes) return originalResponse;
            if (StrictUtf8.GetByteCount(originalResponse) > maxMessageBytes) return originalResponse;
            using var document = JsonDocument.Parse(originalResponse, new JsonDocumentOptions { MaxDepth = 16 });
            if (document.RootElement.ValueKind != JsonValueKind.Object) return originalResponse;
            var fields = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in document.RootElement.EnumerateObject())
                if (!fields.Add(property.Name) || fields.Count > MaximumRootProperties ||
                    property.NameEquals(EvidenceField)) return originalResponse;
            var snapshot = Capture();
            if (snapshot is null) return originalResponse;
            var evidence = JsonSerializer.Serialize(snapshot);
            if (StrictUtf8.GetByteCount(evidence) > MaximumEvidenceBytes) return originalResponse;
            var end = originalResponse.Length - 1;
            while (end >= 0 && originalResponse[end] is ' ' or '\t' or '\r' or '\n') --end;
            if (end < 0 || originalResponse[end] != '}') return originalResponse;
            var suffix = (fields.Count == 0 ? "" : ",") + "\"" + EvidenceField + "\":" + evidence;
            if (StrictUtf8.GetByteCount(originalResponse) + StrictUtf8.GetByteCount(suffix) > maxMessageBytes)
                return originalResponse;
            return originalResponse[..end] + suffix + originalResponse[end..];
        }
        catch { return originalResponse; }
    }

    // Optional byte seam for a caller that already has UTF8. No invalid UTF8 is
    // repaired and failed additions keep both original byte values and identity.
    internal byte[]? TryAppend(byte[]? originalResponse, string? command, int maxMessageBytes)
    {
        if (!LifecycleCommand(command) || originalResponse is null) return originalResponse;
        try
        {
            if (maxMessageBytes <= 0 || maxMessageBytes > MaximumResponseBytes ||
                originalResponse.Length > maxMessageBytes) return originalResponse;
            var original = StrictUtf8.GetString(originalResponse);
            var appended = TryAppend(original, command, maxMessageBytes);
            return ReferenceEquals(original, appended) ? originalResponse : StrictUtf8.GetBytes(appended!);
        }
        catch { return originalResponse; }
    }
}
