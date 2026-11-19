using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;
using YeManCC.SharedSleepPower;

namespace YeManFanHost;

internal sealed partial class FanHostEngine
{
    private sealed class ImportedPowerEntry(RemotePowerOfferV1 offer, FanRemotePowerScope scope)
    {
        internal readonly RemotePowerOfferV1 Offer = offer;
        internal readonly FanRemotePowerScope Scope = scope;
        internal bool Finalized;
        internal RemoteSettlementDelivery Delivery;
    }
    private readonly object remoteIntakeGate = new();
    private readonly Dictionary<(ulong Low, ulong High), ImportedPowerEntry> importedPower = [];
    private int drainingRemoteReturns;
    private long remoteReturnSignals;

    internal IDisposable EnterAuthenticatedPower(RemotePowerOfferV1 offer,
        RemotePowerProcessV1 trustedParent, string trustedImage, RemotePowerPurpose purpose)
    {
        lock (remoteIntakeGate)
        {
            var key = (offer.Binding.Scope.Low, offer.Binding.Scope.High);
            if (!importedPower.TryGetValue(key, out var entry))
            {
                if (!RemoteNativePowerScope.TryImportFromParent(offer, trustedParent, trustedImage, purpose,
                    out var imported, out var reason) || imported is null)
                    throw new FanApiException(409, "FAN_PUBLIC_POWER_SUPERSEDED", reason);
                entry = RegisterImportedPower(offer, imported, imported.SendSettlement);
            }
            if (entry.Finalized || entry.Offer != offer || entry.Scope.Purpose != purpose)
                throw FanRemotePowerContext.Superseded();
            return SharedPowerContext.EnterImportedRoot(entry.Scope);
        }
    }

    // The only new root registration. The caller has already authenticated the
    // concrete importer. Other HTTP requests reuse this exact entry, never Bind.
    private ImportedPowerEntry RegisterImportedPower(RemotePowerOfferV1 offer, IRemoteNativePowerScope imported,
        Func<RemotePowerSettlementV1, RemoteSettlementDelivery> sender)
    {
        var root = new FanRemotePowerScope(imported, offer.Binding.Purpose);
        var key = (offer.Binding.Scope.Low, offer.Binding.Scope.High);
        try
        {
            if (!root.IsCurrent || importedPower.ContainsKey(key)) throw FanRemotePowerContext.Superseded();
            root.InstallSettlementOwner(DrainRemotePowerReturns, sender);
            var entry = new ImportedPowerEntry(offer, root);
            importedPower.Add(key, entry);
            return entry;
        }
        catch { root.Dispose(); throw; }
    }

    private void RecordPublicLeaseAcquired(FanLease lease)
    {
        SharedPowerContext.Borrowed?.OweLease(PublicLeaseKey(lease));
    }
    private void RecordPublicLeaseReturned(FanLease? lease)
    {
        if (lease is null) return;
        ImportedPowerEntry[] entries;
        lock (remoteIntakeGate) entries = importedPower.Values.Where(row => !row.Finalized).ToArray();
        foreach (var entry in entries) entry.Scope.ReturnLease(PublicLeaseKey(lease));
    }
    private static string PublicLeaseKey(FanLease lease) => $"{lease.Generation}:{lease.LeaseId}";

    private void CompletePublicResourceOwner(Action action)
    {
        try { action(); }
        finally { DrainRemotePowerReturns(); }
    }

    // Current action remains the original engine/backend action. A Native-issued
    // child may only adopt the matching still-current resume parent, never a
    // newer global scope or an old Resuming parent silently upgraded to Ready.
    internal void AdoptAuthenticatedWriteChild()
    {
        var child = SharedPowerContext.Borrowed;
        if (child is null || child.Purpose != RemotePowerPurpose.FanWrite || realBackend is null) return;
        var parent = ResolveAuthenticatedResumeParent(child);
        if (parent is not null) realBackend.AdoptCurrentWritePowerForControl(parent);
    }

    private FanRemotePowerScope? ResolveAuthenticatedResumeParent(FanRemotePowerScope child)
    {
        lock (remoteIntakeGate)
        {
            var key = child.Binding.Request;
            var parents = importedPower.Values.Where(row => row.Scope.Purpose == RemotePowerPurpose.FanResume &&
                row.Scope.Binding.Request.Low == key.Low && row.Scope.Binding.Request.High == key.High)
                .ToArray();
            if (parents.Length == 0) return null; // Original independent manual FanWrite, no resume parent.
            return parents.LastOrDefault(row => !row.Finalized && child.IsWriteChildOf(row.Scope))?.Scope
                ?? throw FanRemotePowerContext.Superseded();
        }
    }

    internal void DrainRemotePowerReturns()
    {
        Interlocked.Increment(ref remoteReturnSignals);
        // The existing completion caller owns execution. No new worker/poller,
        // and no IPC send under engine, SDK, STA, intake or scope locks.
        if (Monitor.IsEntered(gate) || Monitor.IsEntered(remoteIntakeGate) ||
            realBackend?.PublicSettlementLockHeld == true ||
            Interlocked.CompareExchange(ref drainingRemoteReturns, 1, 0) != 0) return;
        long observedSignal = 0;
        try
        {
            bool progressed;
            do
            {
                observedSignal = Volatile.Read(ref remoteReturnSignals);
                progressed = false;
                ImportedPowerEntry[] entries;
                lock (remoteIntakeGate) entries = importedPower.Values.Where(row => !row.Finalized)
                    .OrderByDescending(row => row.Scope.Purpose == RemotePowerPurpose.FanWrite).ToArray();
                foreach (var entry in entries)
                {
                    RemotePowerSettlementV1 receipt;
                    if (!entry.Scope.TryPrepareFinalReturn(out receipt)) continue;
                    var delivery = entry.Scope.SendPreparedFinalReturn(receipt);
                    lock (remoteIntakeGate) { entry.Delivery = delivery; entry.Finalized = true; }
                    // Exact import survives through SendSettlement. Disposing a
                    // child only now releases its parent borrower, child first.
                    entry.Scope.Dispose();
                    progressed = true;
                    diagnostics.Write("power.remote-resource-settlement", new {
                        purpose = entry.Scope.Purpose.ToString(), outcome = receipt.Outcome.ToString(),
                        flags = (uint)receipt.Flags, delivery = delivery.ToString(), replayAllowed = false,
                        scopeId = $"{receipt.Scope.High:x16}{receipt.Scope.Low:x16}",
                        nativeRequest = $"{receipt.Request.High:x16}{receipt.Request.Low:x16}",
                        providerPid = receipt.Provider.Pid, consumerPid = receipt.Consumer.Pid,
                        sharedGeneration = entry.Scope.Binding.Captured.Generation,
                        resourceEvidence = entry.Scope.SettlementSnapshot });
                }
            } while (progressed || observedSignal != Volatile.Read(ref remoteReturnSignals));
        }
        finally
        {
            Volatile.Write(ref drainingRemoteReturns, 0);
            if (observedSignal != Volatile.Read(ref remoteReturnSignals)) DrainRemotePowerReturns();
        }
    }
}

internal sealed partial class LocalFanHostServer
{
    private readonly object remoteParentGate = new();
    private Process? remoteNativeParent;
    private RemotePowerProcessV1 remoteNativeIdentity;
    private readonly object? sharedPowerBusinessIdentity;

    private IDisposable? EnterRequestPublicPower(HttpListenerRequest request, string method, string path, RemotePowerKey128? businessDelivery = null)
    {
        if (businessDelivery is null && (method != "POST" || path is not ("/api/open" or "/api/open-events" or "/api/acquire-control" or
            "/api/enable" or "/api/preset" or "/api/resume" or "/api/heartbeat"))) return null;
        if (businessDelivery is not null && (!engine.BusinessProfileSelected || method != "GET" || path != "/api/state"))
            throw FanRemotePowerContext.Superseded();
        var header = request.Headers[RemotePowerOfferHeaderV1.HeaderName];
        if (header is null)
        {
            if (engine.SharedPowerContext.Required) throw FanRemotePowerContext.Superseded();
            return null; // Staged transport remains disabled for the old paired Native.
        }
        if (!RemotePowerOfferHeaderV1.TryParse(header, out var offer))
            throw new FanApiException(400, "FAN_PUBLIC_POWER_OFFER_INVALID", "Invalid shared power offer");
        var purpose = offer.Binding.Purpose;
        if (businessDelivery is { } expected && (purpose != RemotePowerPurpose.FanResume ||
            expected.Low != offer.Binding.Request.Low || expected.High != offer.Binding.Request.High)) throw FanRemotePowerContext.Superseded();
        if (path == "/api/resume" ? purpose != RemotePowerPurpose.FanResume :
            path is "/api/enable" or "/api/preset" or "/api/heartbeat" ? purpose != RemotePowerPurpose.FanWrite :
            purpose is not (RemotePowerPurpose.FanResume or RemotePowerPurpose.FanWrite))
            throw FanRemotePowerContext.Superseded();
        var parent = BindRemoteNativeParent();
        return engine.EnterAuthenticatedPower(offer, parent, parentExecutablePath!, purpose);
    }

    private RemotePowerProcessV1 BindRemoteNativeParent()
    {
        lock (remoteParentGate)
        {
            if (remoteNativeParent is null)
            {
                // Use the original launch identity and its real held process.
                // Offer fields and installation-relative guesses supply neither.
                if (options.ParentPid is null || parentStartTimeUtc is not DateTime expectedStart ||
                    string.IsNullOrWhiteSpace(parentExecutablePath) || !Path.IsPathFullyQualified(parentExecutablePath))
                    throw FanRemotePowerContext.Superseded();
                var (bound, _) = TryBindParentProcess();
                if (bound is null) throw FanRemotePowerContext.Superseded();
                try
                {
                    if (!GetProcessTimes(bound.SafeHandle, out var birth, out _, out _, out _) || birth == 0 ||
                        birth != (ulong)expectedStart.ToFileTimeUtc() ||
                        !ProcessIdToSessionId((uint)bound.Id, out var session)) throw FanRemotePowerContext.Superseded();
                    var text = new StringBuilder(32768); uint length = (uint)text.Capacity;
                    if (!QueryFullProcessImageName(bound.SafeHandle, 0, text, ref length) ||
                        !string.Equals(Path.GetFullPath(text.ToString()), Path.GetFullPath(parentExecutablePath), StringComparison.OrdinalIgnoreCase))
                        throw FanRemotePowerContext.Superseded();
                    remoteNativeIdentity = new() { Pid = (uint)bound.Id, Session = session, Birth100ns = birth };
                    remoteNativeParent = bound;
                    bound = null;
                }
                finally { bound?.Dispose(); }
            }
            if (remoteNativeParent.HasExited) throw FanRemotePowerContext.Superseded();
            return remoteNativeIdentity;
        }
    }

    // Startup-only evidence; no heartbeat hash and no second power authority.
    private object CaptureSharedPowerBusinessIdentity()
    {
        var parent = BindRemoteNativeParent();
        using var self = Process.GetCurrentProcess();
        if (!GetProcessTimes(self.SafeHandle, out var birth, out _, out _, out _) || birth == 0)
            throw FanRemotePowerContext.Superseded();
        var assembly = typeof(LocalFanHostServer).Assembly;
        using var stream = File.OpenRead(assembly.Location);
        var sha = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        stream.Position = 0;
        using var pe = new PEReader(stream);
        var metadata = pe.GetMetadataReader();
        var mvid = metadata.GetGuid(metadata.GetModuleDefinition().Mvid);
        if (mvid != assembly.ManifestModule.ModuleVersionId) throw FanRemotePowerContext.Superseded();
        return new
        {
            profile = options.SharedPowerBusinessProfile,
            hostPid = Environment.ProcessId,
            hostProcessCreated = birth.ToString(System.Globalization.CultureInfo.InvariantCulture),
            hostPath = Environment.ProcessPath,
            hostAssemblySha256 = sha,
            hostAssemblyMvid = mvid.ToString("D"),
            parentPid = parent.Pid,
            parentProcessCreated = parent.Birth100ns.ToString(System.Globalization.CultureInfo.InvariantCulture),
            parentPath = parentExecutablePath,
            parentSession = parent.Session,
        };
    }

    private void DisposeRemoteParent()
    {
        lock (remoteParentGate) { remoteNativeParent?.Dispose(); remoteNativeParent = null; }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessTimes(SafeProcessHandle process, out ulong birth, out ulong exit, out ulong kernel, out ulong user);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ProcessIdToSessionId(uint pid, out uint session);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryFullProcessImageName(SafeProcessHandle process, uint flags, StringBuilder path, ref uint length);
}

// Private successor appended to FanRemotePowerIntake.cs, not a second host.
internal sealed partial class FanHostEngine
{
    // Authenticated dedicated Native POST fixes this mode before Channel capture.
    // A body flag alone cannot create a FanResume grant or clear sleep protection.
    internal void SelectAuthenticatedResumeMode(bool sessionOnly)
    {
        if (!BusinessProfileSelected || SharedPowerContext.Borrowed is not { Purpose: RemotePowerPurpose.FanResume } scope)
        {
            if (sessionOnly) throw FanRemotePowerContext.Superseded();
            return;
        }
        scope.SelectResumeMode(sessionOnly);
    }
    private const string ResumeBusinessEncoding = "native-f5-deadline-v1";
    private sealed class AcceptedResumeBusiness
    {
        internal required RemotePowerDescriptorV1 FirstGrant;
        internal required long FanGeneration, PowerEpoch, GateEpoch, ProtectionCommit, IntentRevision;
        internal required SuspendProtectionAuthorization Authorization;
        internal required IReadOnlyList<FanNode> Curve;
        internal bool SessionOnly;
        internal required ResumeAdmission OriginalAdmission;
        internal readonly List<RemotePowerDescriptorV1> Grants = []; // Maximum three, no eviction.
        internal FanRemotePowerScope? PendingGrant;
        internal ResumeOperationIdentity? Operation;
        internal string Phase = "Running", Reason = "accepted";
        internal string AttemptStatus = "executing";
        internal string? AttemptOutcome;
        internal bool ExecutionReturned, Queued;
        internal bool OpenReturned, EventsReturned;
        internal long OpenCycle;
    }
    private sealed record ResumeBusinessReadView(
        RemotePowerKey128 Request, string Phase, string Reason, long FanGeneration,
        ulong NativeGeneration, int Attempt, string AttemptStatus, string? AttemptOutcome,
        int HostGrantsObserved, bool GrantPending);
    private AcceptedResumeBusiness? acceptedResumeBusiness;
    private ResumeBusinessReadView? publishedResumeBusiness;
    private ulong acceptedResumeRequestSerial;
    private RemotePowerProcessV1 acceptedResumeProvider;
    private long resumeBusinessIntentRevision;
    internal Action<Func<object>>? AcceptedResumeBusinessReady { get; set; }

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern ulong GetTickCount64();

    private static bool SameBusinessProcess(RemotePowerProcessV1 a, RemotePowerProcessV1 b) =>
        a.Pid == b.Pid && a.Session == b.Session && a.Birth100ns == b.Birth100ns;
    private static bool SameBusinessRequest(RemotePowerKey128 a, RemotePowerKey128 b) =>
        a.Low == b.Low && a.High == b.High;
    internal static bool BusinessDeadlineCurrent(RemotePowerKey128 key)
    {
        var now = GetTickCount64();
        return key.High > now && key.High - now <= 15000;
    }
    internal bool BusinessProfileSelected => options.SharedPowerBusinessProfile == ResumeBusinessEncoding;
    private void RequireBusinessProfileGrant(FanRemotePowerScope scope)
    {
        if (!BusinessProfileSelected || scope.Purpose != RemotePowerPurpose.FanResume || !scope.IsCurrent ||
            (scope.Binding.Request.Low & (1UL << 63)) == 0 || (scope.Binding.Request.Low & ~(1UL << 63)) == 0 ||
            !BusinessDeadlineCurrent(scope.Binding.Request)) throw FanRemotePowerContext.Superseded();
    }
    private static bool SameStableBusiness(AcceptedResumeBusiness business, RemotePowerDescriptorV1 grant) =>
        SameBusinessRequest(business.FirstGrant.Request, grant.Request) &&
        SameBusinessProcess(business.FirstGrant.Provider, grant.Provider) &&
        SameBusinessProcess(business.FirstGrant.Consumer, grant.Consumer) &&
        business.FirstGrant.Captured.Generation == grant.Captured.Generation;

    // No serial equality here: serial is a revocation watermark, not business identity.
    // Actual new Host suspend/commit/intent/close is checked independently.
    private bool ResumeBusinessOwnerCurrentLocked(AcceptedResumeBusiness business) =>
        ReferenceEquals(acceptedResumeBusiness, business) && !stopped && !state.CloseCalled &&
        Volatile.Read(ref closeWriteBlocked) == 0 && Volatile.Read(ref parentExitHandoffQueued) == 0 &&
        business.IntentRevision == resumeBusinessIntentRevision && (!controlIntentCancelled || business.SessionOnly) &&
        business.PowerEpoch == Volatile.Read(ref powerEpoch) &&
        business.ProtectionCommit == Volatile.Read(ref suspendProtectionCommitSequence) &&
        business.GateEpoch == (realBackend?.SuspendGateEpochForResume ?? 0) && !SuspendProtectionPending &&
        !state.UnknownState && !state.HcCloseCleanupPending && state.PowerState == "On";

    private void PublishResumeBusinessLocked()
    {
        var b = acceptedResumeBusiness;
        Volatile.Write(ref publishedResumeBusiness, b is null ? null : new(
            b.FirstGrant.Request, b.Phase, b.Reason, b.FanGeneration, b.FirstGrant.Captured.Generation,
            b.Operation?.AttemptId ?? 0, b.AttemptStatus, b.AttemptOutcome, b.Grants.Count, b.PendingGrant is not null));
    }
    private void RefreshResumeBusinessReadFence()
    {
        if (Monitor.IsEntered(gate))
        {
            if (acceptedResumeBusiness is { Phase: not "Terminal" } b && !ResumeBusinessOwnerCurrentLocked(b))
                RetireResumeBusinessLocked("original-business-owner-retired");
        }
    }
    private void RetireResumeBusinessLocked(string reason, bool newIntent = false)
    {
        if (newIntent) checked { resumeBusinessIntentRevision++; }
        if (acceptedResumeBusiness is not { } b || b.Phase == "Terminal") return;
        b.Phase = "Terminal"; b.Reason = reason; b.Queued = false;
        Interlocked.Exchange(ref b.PendingGrant, null)?.Dispose();
        PublishResumeBusinessLocked();
    }
    internal void RetireAcceptedResumeBusinessForServerExit()
    {
        // The original close fence is synchronous; the queued action never writes after it.
        BlockWritesForClose();
    }
    private void RetireResumeBusinessForManualLocked(string reason)
    {
        if (Volatile.Read(ref automaticResumeWorkerThreadId) != Environment.CurrentManagedThreadId)
            RetireResumeBusinessLocked(reason, newIntent: true);
    }

    private bool TryFindAcceptedBusinessLocked(FanRemotePowerScope scope, out ResumeAdmission admission)
    {
        admission = ResumeAdmission.Unknown;
        if (!BusinessProfileSelected || scope.Purpose != RemotePowerPurpose.FanResume) return false;
        RequireBusinessProfileGrant(scope);
        var grant = scope.Binding;
        if (acceptedResumeBusiness is { } b && SameStableBusiness(b, grant))
        {
            // Duplicate accepted POST must never create another attempt or consume protection.
            admission = new(SnapshotUnlocked(), 0, b.Operation?.AttemptId ?? 0, "known-business", 0);
            return true;
        }
        var serial = grant.Request.Low & ~(1UL << 63);
        if (acceptedResumeRequestSerial != 0 && (!SameBusinessProcess(acceptedResumeProvider, grant.Provider) ||
            serial <= acceptedResumeRequestSerial)) throw FanRemotePowerContext.Superseded();
        return false;
    }

    private void EnsureResumeBusinessStepAdmission(ResumeOperationIdentity? operation)
    {
        if (operation?.Business is not { } b) return;
        if (!ReferenceEquals(b.Operation, operation) || !ResumeBusinessOwnerCurrentLocked(b) ||
            !BusinessDeadlineCurrent(b.FirstGrant.Request)) throw ResumeSupersededError();
    }

    private ResumeOperationIdentity AttachAcceptedResumeBusinessLocked(ResumeOperationIdentity operation, IReadOnlyList<FanNode>? curve)
    {
        if (!BusinessProfileSelected || operation.PublicPower is not { Purpose: RemotePowerPurpose.FanResume } power || curve is null)
            return operation;
        RequireBusinessProfileGrant(power);
        power.SelectEncodedBusinessDeadline();
        var grant = power.Binding;
        if (acceptedResumeBusiness is { } old && SameStableBusiness(old, grant))
            throw FanRemotePowerContext.Superseded();
        RetireResumeBusinessLocked("new-accepted-request");
        var business = new AcceptedResumeBusiness { FirstGrant = grant, FanGeneration = operation.Generation,
            PowerEpoch = operation.AcceptedPowerEpoch, GateEpoch = operation.AcceptedGateEpoch,
            ProtectionCommit = Volatile.Read(ref suspendProtectionCommitSequence),
            IntentRevision = resumeBusinessIntentRevision, Authorization = new(operation.AcceptedPendingEpoch, operation.AcceptedPendingStamp),
            Curve = curve.Select(n => new FanNode(n.TempC, n.DutyPercent)).ToArray(), SessionOnly = power.SessionOnlyResume,
            OriginalAdmission = new(null, operation.AttemptId, operation.AttemptId, "attempt", 0) };
        business.Grants.Add(grant);
        operation = operation with { Business = business };
        business.Operation = operation;
        acceptedResumeBusiness = business;
        acceptedResumeRequestSerial = grant.Request.Low & ~(1UL << 63);
        acceptedResumeProvider = grant.Provider;
        PublishResumeBusinessLocked();
        return operation;
    }

    private void RecordResumeBusinessTerminalLocked(long attempt, string status, string? outcome)
    {
        if (acceptedResumeBusiness is not { } b || b.Operation?.AttemptId != attempt) return;
        b.AttemptStatus = status; b.AttemptOutcome = outcome;
        if (status == "completed" || status == "failed") RetireResumeBusinessLocked(status == "completed" ? outcome ?? "completed" : "actual-attempt-failed");
        else if (b.Phase != "AwaitingGrant" && b.Phase != "PendingHandoff") RetireResumeBusinessLocked(outcome ?? "unclassified-superseded");
        PublishResumeBusinessLocked();
    }
    private void AwaitNextResumeBusinessGrantLocked(ResumeOperationIdentity operation, Exception error)
    {
        if (operation.Business is not { } b || !ReferenceEquals(b.Operation, operation)) return;
        var scope = operation.PublicPower;
        if (error is FanApiException { FailureOrigin: FanPowerFailureOrigin.PublicAdmission } && scope is not null &&
            scope.FirstDeviceActionFailure is null && !scope.HasOutstandingDeviceCall &&
            realBackend?.OperationTimedOut != true && BusinessDeadlineCurrent(b.FirstGrant.Request) &&
            ResumeBusinessOwnerCurrentLocked(b) && b.Grants.Count < 3)
        {
            var proof = scope.KnownDeviceStages;
            b.OpenReturned |= proof.OpenReturned; b.EventsReturned |= proof.EventsReturned;
            if (proof.OpenReturned || proof.EventsReturned) b.OpenCycle = proof.ControlCycle;
            b.Phase = "AwaitingGrant"; b.Reason = "public-admission-revoked";
            PublishResumeBusinessLocked();
        }
        else RetireResumeBusinessLocked("public-only-or-original-owner-not-proven");
    }
    private void MarkResumeBusinessStepLocked(ResumeOperationIdentity? operation, bool events)
    {
        if (operation?.Business is not { } b || !ReferenceEquals(b.Operation, operation)) return;
        b.OpenReturned = true; if (events) b.EventsReturned = true;
        b.OpenCycle = realBackend?.ControlCycleId ?? 0;
    }

    // Called after authentication and concrete importer, never creates a business.
    internal bool StageAcceptedResumeParent(RemotePowerKey128 request)
    {
        using var current = SharedPowerContext.Capture();
        if (current is null || !SameBusinessRequest(current.Binding.Request, request)) throw FanRemotePowerContext.Superseded();
        RequireBusinessProfileGrant(current);
        if (!Monitor.TryEnter(gate)) return false;
        try
        {
            if (acceptedResumeBusiness is not { } b || !SameStableBusiness(b, current.Binding) ||
                b.Phase is not ("AwaitingGrant" or "PendingHandoff") || !ResumeBusinessOwnerCurrentLocked(b)) return false;
            foreach (var observed in b.Grants)
                if (SameBusinessRequest(observed.Scope, current.Binding.Scope))
                {
                    // Same Scope may only repeat the SAME complete descriptor.
                    if (!observed.Equals(current.Binding)) throw FanRemotePowerContext.Superseded();
                    return true;
                }
            if (b.PendingGrant is not null || b.Grants.Count >= 3) return false;
            current.SelectEncodedBusinessDeadline();
            var retained = current.Retain() ?? throw FanRemotePowerContext.Superseded();
            b.Grants.Add(current.Binding); b.PendingGrant = retained;
            b.Phase = "PendingHandoff"; b.Reason = "authenticated-parent-staged";
            PublishResumeBusinessLocked();
            SignalAcceptedResumeBusinessLocked(b);
            return true;
        }
        finally { Monitor.Exit(gate); }
    }
    private void SignalAcceptedResumeBusinessLocked(AcceptedResumeBusiness business)
    {
        if (business.Phase != "PendingHandoff" || business.Queued || !business.ExecutionReturned || business.PendingGrant is null) return;
        business.Queued = true;
        try
        {
            if (AcceptedResumeBusinessReady is not { } signal) throw new InvalidOperationException("Original executor unavailable");
            signal(() => DispatchAcceptedResumeBusiness(business));
        }
        catch { business.Queued = false; RetireResumeBusinessLocked("original-executor-unavailable"); }
    }
    private void OnAcceptedResumeTaskReturned(ResumeOperationIdentity operation)
    {
        if (operation.Business is not { } b) return;
        lock (gate)
        {
            if (!ReferenceEquals(acceptedResumeBusiness, b) || !ReferenceEquals(b.Operation, operation)) return;
            b.ExecutionReturned = true;
            SignalAcceptedResumeBusinessLocked(b);
        }
    }
    private object DispatchAcceptedResumeBusiness(AcceptedResumeBusiness expectedBusiness)
    {
        lock (gate)
        {
            if (acceptedResumeBusiness is not { } b || !ReferenceEquals(b, expectedBusiness) || b.Phase != "PendingHandoff") return SnapshotUnlocked();
            b.Queued = false;
            using var parent = Interlocked.Exchange(ref b.PendingGrant, null);
            bool CanContinue() => parent is not null && b.ExecutionReturned && resumeWorkerActive == 0 &&
                BusinessDeadlineCurrent(b.FirstGrant.Request) && ResumeBusinessOwnerCurrentLocked(b) && parent.IsCurrent &&
                state.Lease is null;
            if (!CanContinue())
            { RetireResumeBusinessLocked("continuation-admission-or-resource-return-unproven"); return SnapshotUnlocked(); }
            if (realBackend is { } backend && !backend.ResumeBusinessResourcesCurrent(b.OpenReturned, b.OpenCycle))
            {
                var returned = false;
                if (!b.OpenReturned && !b.EventsReturned && !state.OpenEventsCalled && !state.HardwareWritesEnabled &&
                    b.Operation?.PublicPower is { } previous)
                {
                    try { returned = backend.TryReturnInterruptedResumeOpen(previous, parent!); }
                    catch (Exception error)
                    {
                        diagnostics.Write("power.resume-interrupted-open-return-failed", new { error = ExceptionText(error) });
                    }
                }
                if (returned)
                {
                    // This retires only the aborted Open. The new attempt must still Open/Events.
                    state.OpenCalled = false;
                    SettleReturnedResumePublicPower();
                }
                if (!returned || !CanContinue() || !backend.ResumeBusinessResourcesCurrent(false, 0))
                { RetireResumeBusinessLocked("continuation-admission-or-resource-return-unproven"); return SnapshotUnlocked(); }
            }
            // Enter the NEW immutable parent on the ORIGINAL executor. The original
            // start path checks consumed proof without consuming the old token again.
            using var context = SharedPowerContext.Enter(parent!);
            return StartAutomaticResumeUnlocked(b.Authorization.Value, b.Authorization.Stamp, 0, b).Snapshot!;
        }
    }

    internal object? ReadAcceptedResumeBusiness(RemotePowerKey128 request)
    {
        var view = Volatile.Read(ref publishedResumeBusiness);
        if (view is null || !SameBusinessRequest(view.Request, request)) return null;
        var expiredWaiting = (view.Phase is "AwaitingGrant" or "PendingHandoff") && !BusinessDeadlineCurrent(request);
        return new { schemaVersion = 1, encoding = ResumeBusinessEncoding,
            requestLow = request.Low.ToString("x16"), requestHigh = request.High.ToString("x16"),
            phase = expiredWaiting ? "Terminal" : view.Phase, reason = expiredWaiting ? "original-deadline-expired" : view.Reason,
            fanGeneration = view.FanGeneration.ToString(System.Globalization.CultureInfo.InvariantCulture),
            nativeGeneration = view.NativeGeneration.ToString(System.Globalization.CultureInfo.InvariantCulture), accepted = true,
            currentAttempt = new { id = view.Attempt.ToString(System.Globalization.CultureInfo.InvariantCulture),
                status = view.AttemptStatus, outcome = System.Text.Json.JsonSerializer.SerializeToElement(view.AttemptOutcome) },
            grantBudgetUsed = System.Text.Json.JsonSerializer.SerializeToElement<int?>(null), hostGrantsObserved = view.HostGrantsObserved, grantPending = view.GrantPending };
    }
}

internal sealed partial class RealHcBackend
{
    private sealed record InterruptedResumeOpen(object Authority, object Device, long Cycle, long GateEpoch,
        bool PriorReleaseSettled);
    private InterruptedResumeOpen? interruptedResumeOpen;

    // Win5 HC Open only opens handles and reads controller firmware. Win4 Open
    // can write its fan unlock register, so it cannot consume this proof.
    private bool PriorWin5FanReleaseSettled => Route?.RestoreStrategy == FanRestoreStrategy.GpdWin5HcAutoRelease &&
        HasVerifiedRetiredDeviceWithoutNewOpen &&
        (oemRestoreVerified && FanHostEngine.ParseReleaseAction(oemReleaseActionKind) == OemReleaseAction.CompatRouteWithoutScope ||
         ClosedLegacyCallbackWithoutReturnObligation && FanHostEngine.ParseReleaseAction(oemReleaseActionKind) == OemReleaseAction.NotApplicable);

    // Only a virtual Open that returned before session proof can leave this debt.
    private InterruptedResumeOpen? CaptureInterruptedResumeOpen(Exception error, bool priorReleaseSettled) =>
        error is FanApiException { FailureOrigin: FanPowerFailureOrigin.PublicAdmission } && IsOpen &&
        device is { } opened && sessionDeviceReference is null && !hcOpenEventsInvoked && !SourceControlMode &&
        SharedPowerContext.Borrowed is { Purpose: RemotePowerPurpose.FanResume } scope
            ? new(scope.AuthorityIdentity, opened, ControlCycleId, Volatile.Read(ref suspendGateEpoch), priorReleaseSettled) : null;

    private bool InterruptedResumeOpenOwesNoFanRelease(InterruptedResumeOpen? interrupted) =>
        interrupted is { PriorReleaseSettled: true } &&
        ReferenceEquals(interrupted.Device, device) && interrupted.Cycle == ControlCycleId &&
        Route?.RestoreStrategy == FanRestoreStrategy.GpdWin5HcAutoRelease &&
        HcCloseIsOwed && !HcVirtualCloseReturned && !OperationTimedOut &&
        sessionDeviceReference is null && !hcOpenEventsInvoked &&
        !legacyCallbackFanReturnOwed && activeCurveDuties is null && activeFanProfile is null;

    private bool SettleUnwrittenInterruptedOpen(bool eligible)
    {
        if (!eligible || !HcVirtualCloseReturned || !HcManagerCleanupConfirmed || HcDeviceIsOpenObserved is not false) return false;
        retiredDeviceReference = device;
        retiredControlCycleId = ControlCycleId;
        closed = true;
        oemReleaseActionKind = FanHostEngine.ReleaseActionKindName(OemReleaseAction.NotApplicable);
        oemReleaseActionEvidence = "interrupted Win5 Open closed before events or control; prior session release already settled";
        return true;
    }

    internal bool TryReturnInterruptedResumeOpen(FanRemotePowerScope previous, FanRemotePowerScope current)
    {
        var accepted = Volatile.Read(ref interruptedResumeOpen);
        if (accepted is null || !ReferenceEquals(accepted.Authority, previous.AuthorityIdentity)) return false;
        return OnStaBounded(() =>
        {
            lock (hardwareGate)
            {
                if (!ReferenceEquals(interruptedResumeOpen, accepted) || !ReferenceEquals(device, accepted.Device) ||
                    ControlCycleId != accepted.Cycle || Volatile.Read(ref suspendGateEpoch) != accepted.GateEpoch ||
                    Volatile.Read(ref closeWriteBlocked) != 0 || OperationTimedOut || SourceIoPending ||
                    IsOpen || !HcCloseIsOwed || HcVirtualCloseReturned || sessionDeviceReference is not null ||
                    hcOpenEventsInvoked || backendAsusAcpiOpened || cpuTemperatureMonitor is not null ||
                    powerProfileWatchdog is not null || !current.IsCurrent || !FanHostEngine.BusinessDeadlineCurrent(current.Binding.Request) ||
                    !HcDeviceIsOpen(accepted.Device)) return false;
                // The prior request owns this single Close. Do not replay a returned Close,
                // and do not invalidate a newer sleep epoch by lifting or rewriting the gate.
                var unwritten = InterruptedResumeOpenOwesNoFanRelease(accepted);
                CloseHcDevice();
                if (HcDeviceIsOpenObserved is not false) return false;
                openAttempted = false;
                hcOpenInvocationStarted = false;
                ClearSessionRouteMarker();
                closed = true;
                retiredDeviceReference = accepted.Device;
                retiredControlCycleId = accepted.Cycle;
                legacyCallbackNoReturnAtRetirement = SettleUnwrittenInterruptedOpen(unwritten);
                interruptedResumeOpen = null;
            }
            RecordPublicSdkCloseReturned();
            return true;
        }, "return-interrupted-resume-open");
    }

    internal bool ResumeBusinessResourcesCurrent(bool knownOpen, long cycle)
    {
        if (!Monitor.TryEnter(staGate)) return false;
        try
        {
            if (activeStaWorkItem is not null || OperationTimedOut || staDispatcherStopping) return false;
            if (!Monitor.TryEnter(hardwareGate)) return false;
            try
            {
                if (SourceIoPending || OperationTimedOut) return false;
                if (IsOpen) return knownOpen && ControlCycleId == cycle;
                return !OpenAttempted && !HcCloseIsOwed && !SourceCloseIsOwed;
            }
            finally { Monitor.Exit(hardwareGate); }
        }
        finally { Monitor.Exit(staGate); }
    }
}

internal sealed partial class LocalFanHostServer
{
    private IDisposable? EnterResumeBusinessDelivery(HttpListenerRequest request, RemotePowerKey128 selected) =>
        EnterRequestPublicPower(request, "GET", "/api/state", selected);
    private static RemotePowerKey128? ParseResumeBusinessSelection(HttpListenerRequest request)
    {
        var query = request.QueryString;
        var keys = query.AllKeys.Where(k => k is not null && k.StartsWith("resumeRequest", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (keys.Length == 0) return null;
        if (keys.Length != 2 || !keys.Contains("resumeRequestLow", StringComparer.Ordinal) || !keys.Contains("resumeRequestHigh", StringComparer.Ordinal))
            throw new FanApiException(400, "FAN_RESUME_SELECTOR_INVALID", "Expected exact paired resume request selectors");
        ulong Part(string key)
        {
            var values = query.GetValues(key);
            if (values is not { Length: 1 } || values[0] is not { Length: 16 } value ||
                value.Any(c => !(c is >= '0' and <= '9' or >= 'a' and <= 'f')) ||
                !ulong.TryParse(value, System.Globalization.NumberStyles.AllowHexSpecifier, System.Globalization.CultureInfo.InvariantCulture, out var parsed))
                throw new FanApiException(400, "FAN_RESUME_SELECTOR_INVALID", "Resume request parts must be sixteen lowercase hex characters");
            return parsed;
        }
        return new RemotePowerKey128 { Low = Part("resumeRequestLow"), High = Part("resumeRequestHigh") };
    }
}
