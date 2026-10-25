using System.Text;
using System.Text.Json;

namespace YeManInputHost.Diagnostics;

// Data-only diagnostic. It receives existing observations, never enumerates or
// removes devices and never participates in a release/admission decision.
internal sealed class MsiNonSdReleaseDiagnostic
{
    internal const int Version = 1;
    internal const string Field = "msiNonSdReleaseEvidence";
    internal const int DetailLimit = 512;
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    internal sealed record Snapshot(bool ProbeSucceeded, int PrimaryRootCount,
        int XboxCompanionRootCount, int HidChildCount, int SteamDeckRootCount,
        int SteamDeckHidChildCount, bool AllStarted, string Detail)
    {
        public bool CountFieldsValid => PrimaryRootCount >= 0 && XboxCompanionRootCount >= 0 && HidChildCount >= 0 && SteamDeckRootCount >= 0 && SteamDeckHidChildCount >= 0;
        public bool GenericCountOnlyAbsent => ProbeSucceeded && CountFieldsValid && PrimaryRootCount == 0 && XboxCompanionRootCount == 0 && HidChildCount == 0;
        public bool DetailTruncated { get; init; }
        public MsiPnpCensusDiagnostic.Receipt? ExistingPnpCensus { get; init; }
    }
    internal sealed record FreshObservation(string Source, long BeginTick, long EndTick,
        bool TimeWindowKnown, Snapshot Observation);
    internal sealed record PreviousReceipt(long BeginTick, long EndTick,
        bool LocalDisposalReportedOk, int FreshObservationCount);
    internal sealed record Receipt(int Version, string Persona, long BeginTick, long EndTick,
        bool TimeWindowKnown, bool ControllerObjectPresentAtBegin, bool ContextObjectPresentAtBegin,
        bool LocalDisposalReportedOk, string CachedBeforeSource, Snapshot CachedBefore,
        IReadOnlyList<FreshObservation> FreshObservations, PreviousReceipt? Previous,
        bool ExactOwnedIdsPinned, bool ClassificationComplete, bool ExactOwnedAbsenceProven,
        string ObservationState, string Scope)
    {
        public MsiPnpCensusDiagnostic.Receipt? BeforeCreateCensus { get; init; }
        public MsiSdkDeclaredIdentityDiagnostic.Receipt? SdkDeclaredIdentity { get; init; }
    }

    private readonly string _persona;
    private MsiPnpCensusDiagnostic.Receipt? _beforeCreateCensus;
    private MsiSdkDeclaredIdentityDiagnostic.Receipt? _sdkDeclaredIdentity;
    internal void TrySetSdkDeclaredIdentity(MsiSdkDeclaredIdentityDiagnostic.Receipt? identity) => _sdkDeclaredIdentity = identity;
    internal void TrySetBeforeCreateCensus(MsiPnpCensusDiagnostic.Receipt? census) => _beforeCreateCensus = census;
    private Snapshot? _cached;
    private long _beginTick;
    private bool _controllerPresent, _contextPresent, _begun, _failed;
    private readonly List<FreshObservation> _fresh = new(2);
    internal Receipt? Current { get; private set; }

    internal static bool ShouldObserve(bool nativeMsiRequested, string persona) =>
        nativeMsiRequested && persona is "elite" or "dualsense-edge";

    internal MsiNonSdReleaseDiagnostic(string persona) => _persona = persona;

    private static Snapshot Bounded(Snapshot snapshot)
    {
        var detail = snapshot.Detail ?? "";
        if (detail.Length <= DetailLimit) return snapshot with { Detail = detail };
        var length = DetailLimit;
        if (char.IsHighSurrogate(detail[length - 1])) --length;
        return snapshot with { Detail = detail[..length], DetailTruncated = true };
    }

    internal void TryBegin(Snapshot cached, long tick, bool controllerPresent, bool contextPresent)
    {
        try
        {
            _begun = false; _failed = false; _fresh.Clear();
            _cached = Bounded(cached); _beginTick = tick;
            _controllerPresent = controllerPresent; _contextPresent = contextPresent; _begun = true;
        }
        catch { _failed = true; _begun = false; }
    }

    internal void TryObserveExistingPoll(Snapshot snapshot, long beginTick, long endTick)
    {
        if (!_begun || _failed) return;
        try
        {
            if (_fresh.Count >= 2) { _failed = true; return; }
            _fresh.Add(new("existing-release-poll", beginTick, endTick,
                beginTick >= _beginTick && beginTick >= 0 && endTick >= beginTick,
                Bounded(snapshot)));
        }
        catch { _failed = true; }
    }

    internal void TryComplete(bool localDisposalReportedOk, long endTick)
    {
        if (!_begun || _cached is null) { Current = null; return; }
        try
        {
            var prior = Current is { } previous ? new PreviousReceipt(previous.BeginTick,
                previous.EndTick, previous.LocalDisposalReportedOk, previous.FreshObservations.Count) : null;
            Current = new(Version, _persona, _beginTick, endTick,
                _beginTick >= 0 && endTick >= _beginTick, _controllerPresent, _contextPresent,
                localDisposalReportedOk, "existing-response-snapshot-not-fresh-proof", _cached,
                Array.AsReadOnly(_fresh.ToArray()), prior, false, false, false,
                _failed ? "diagnostic-incomplete" : _fresh.Count == 0 ? "no-fresh-poll" : "existing-poll-observed",
                "MSI native opt-in; existing generic release polls only; no complete classification, exact-owned disappearance, physical return or Steam consumption proof") { BeforeCreateCensus = _beforeCreateCensus, SdkDeclaredIdentity = _sdkDeclaredIdentity };
        }
        catch { Current = null; _failed = true; }
        finally { _begun = false; }
    }

    // Never grow an otherwise valid Host response past the original wire limit.
    // Failures are optional diagnostic loss; the original status/fields survive.
    internal string TryAppend(string response, int maxBytes)
    {
        if (Current is null || string.IsNullOrEmpty(response) || response[^1] != '}' || maxBytes <= 0) return response;
        try
        {
            using var document = JsonDocument.Parse(response);
            if (document.RootElement.ValueKind != JsonValueKind.Object ||
                document.RootElement.TryGetProperty(Field, out _)) return response;
            var separator = document.RootElement.EnumerateObject().Any() ? "," : "";
            var prefix = response[..^1] + separator + "\"" + Field + "\":";
            var candidate = prefix + JsonSerializer.Serialize(Current, JsonOptions) + "}";
            if (Encoding.UTF8.GetByteCount(candidate) <= maxBytes) return candidate;
            candidate = prefix + "{\"version\":1,\"observationState\":\"omitted-message-budget\",\"exactOwnedAbsenceProven\":false}}";
            return Encoding.UTF8.GetByteCount(candidate) <= maxBytes ? candidate : response;
        }
        catch { return response; }
    }
}
