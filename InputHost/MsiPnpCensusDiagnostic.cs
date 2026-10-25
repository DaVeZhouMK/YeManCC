namespace YeManInputHost.Diagnostics;

// Receives only facts from PnP calls already made by the Host. No PnP, SDK,
// registry, device, retry or admission operations are performed here.
internal sealed class MsiPnpCensusDiagnostic
{
    internal const int Version = 1;
    private const int EventLimit = 16, CandidateLimit = 32, InstanceIdLimit = 256;
    [ThreadStatic] private static bool t_requested;
    [ThreadStatic] private static MsiPnpCensusDiagnostic? t_current;
    internal static MsiPnpCensusDiagnostic? Current => t_current;
    internal static bool Requested => t_requested;

    internal readonly struct Scope : IDisposable
    {
        private readonly bool _previous;
        internal Scope(bool requested) { _previous = t_requested; t_requested = requested; }
        public void Dispose() => t_requested = _previous;
    }
    internal readonly struct ObservationScope : IDisposable
    {
        private readonly MsiPnpCensusDiagnostic? _previous;
        internal MsiPnpCensusDiagnostic? Capture { get; }
        internal ObservationScope(long beginTick)
        {
            _previous = t_current; Capture = null;
            try { if(t_requested) Capture = new(beginTick); } catch { }
            t_current = Capture;
        }
        public void Dispose() => t_current = _previous;
    }

    internal sealed record Event(string Stage, long Code, long RequiredSize, long ReturnedSize);
    internal sealed record Candidate(string Role, string InstanceId);
    internal sealed record Receipt(int Version, long BeginTick, long EndTick, bool TimeWindowKnown,
        bool EnumerationReachedEnd, bool ExistingProbeSucceeded, int EnumeratedNodeCount,
        int NamedNodeCount, int MissingInstanceIdCount, int HardwareIdUnavailableCount,
        int HardwareIdReadFailureCount, int HardwareIdReadShapeUnknownCount, int AncestorParentNonSuccessCount,
        int AncestorIdNonSuccessCount, int AncestorDepthLimitCount,
        IReadOnlyList<Event> Issues, bool IssuesTruncated,
        IReadOnlyList<Candidate> ObservedCandidates, bool CandidatesTruncated,
        bool DiagnosticComplete, bool ClassificationComplete, bool ExactOwnedIdsPinned,
        bool ExactOwnedAbsenceProven, string Scope);

    private readonly long _beginTick;
    private int _enumerated, _named, _missingId, _hardwareUnavailable, _hardwareReadFailed;
    private int _hardwareShapeUnknown, _parentNonSuccess, _ancestorIdNonSuccess, _ancestorDepthLimit;
    private bool _reachedEnd, _failed, _issuesTruncated, _candidatesTruncated;
    private readonly List<Event> _issues = new(EventLimit);
    private readonly List<Candidate> _candidates = new(CandidateLimit);
    private MsiPnpCensusDiagnostic(long beginTick) => _beginTick = beginTick;

    internal void TryNodeEnumerated() { try { checked { ++_enumerated; } } catch { _failed = true; } }
    internal void TryNamedNode() { try { checked { ++_named; } } catch { _failed = true; } }
    internal void TryEnumerationEnd() => _reachedEnd = true;
    private void Add(string stage, long code, long required = 0, long returned = 0)
    {
        if(_issues.Count < EventLimit) _issues.Add(new(stage, code, required, returned));
        else _issuesTruncated = true;
    }
    internal void TryMissingInstanceId()
    { try { checked { ++_missingId; } Add("instance-id-unavailable", 0); } catch { _failed = true; } }
    internal void TryHardwareIdsUnavailable(long code, uint required)
    { try { checked { ++_hardwareUnavailable; } Add("hardware-id-size-unavailable", code, required); } catch { _failed = true; } }
    internal void TryHardwareIdsReadFailed(long code, uint required)
    { try { checked { ++_hardwareReadFailed; } Add("hardware-id-read-failed", code, required); } catch { _failed = true; } }
    internal void TryHardwareIdsReadShape(uint expected, uint returned, uint propertyType, bool multiStringTerminated)
    {
        try {
            if(propertyType != 7 || returned > expected || (returned & 1) != 0 || !multiStringTerminated)
                { checked { ++_hardwareShapeUnknown; } Add("hardware-id-read-shape-unknown", propertyType, expected, returned); }
        } catch { _failed = true; }
    }
    internal void TryAncestorParentNonSuccess(long code)
    { try { checked { ++_parentNonSuccess; } Add("ancestor-parent-non-success", code); } catch { _failed = true; } }
    internal void TryAncestorIdNonSuccess(long code)
    { try { checked { ++_ancestorIdNonSuccess; } Add("ancestor-id-non-success", code); } catch { _failed = true; } }
    internal void TryAncestorDepthLimit()
    { try { checked { ++_ancestorDepthLimit; } Add("ancestor-depth-limit", 16); } catch { _failed = true; } }
    internal void TryCandidates(string role, IEnumerable<string> instanceIds)
    {
        try {
            foreach(var id in instanceIds) {
                if(string.IsNullOrWhiteSpace(id) || id.Length > InstanceIdLimit || _candidates.Count >= CandidateLimit) {
                    _candidatesTruncated = true; continue;
                }
                _candidates.Add(new(role, id));
            }
        } catch { _failed = true; _candidatesTruncated = true; }
    }
    internal Receipt? TryComplete(bool existingProbeSucceeded, long endTick)
    {
        try { return new(Version, _beginTick, endTick, _beginTick >= 0 && endTick >= _beginTick,
            _reachedEnd, existingProbeSucceeded, _enumerated, _named, _missingId,
            _hardwareUnavailable, _hardwareReadFailed, _hardwareShapeUnknown, _parentNonSuccess, _ancestorIdNonSuccess,
            _ancestorDepthLimit, Array.AsReadOnly(_issues.ToArray()), _issuesTruncated,
            Array.AsReadOnly(_candidates.ToArray()), _candidatesTruncated, !_failed && !_issuesTruncated && !_candidatesTruncated,
            false, false, false,
            "Existing PnP calls only; candidate identity and query gaps are diagnostic, not complete classification, SDK ownership, incarnation or physical/Steam return proof. Parent non-success may include a normal ancestry end; codes are not automatically hardware faults."); }
        catch { return null; }
    }
}
