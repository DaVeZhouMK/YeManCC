using YeManCC.SharedSleepPower;

namespace YeManFanHost;

// [LIFECYCLE:FAN:PUBLIC-SCOPE] The authenticated importer supplies the only
// authority. Retain this exact owner before queueing/acceptance and through
// actual STA/worker/control return; HTTP completion only releases its borrower.
// Cancellation is sticky and performs no device recovery. A prepared settlement
// is not a sent ACK; Dispose never creates one. Keep this block with the guards.
internal sealed partial class FanRemotePowerScope : IDisposable
{
    private sealed class Owner(IRemoteNativePowerScope scope, RemotePowerPurpose purpose)
    {
        internal readonly object Gate = new();
        internal readonly IRemoteNativePowerScope Scope = scope;
        internal readonly RemotePowerPurpose Purpose = purpose;
        internal RemotePowerDescriptorV1 Binding;
        internal int References = 1;
        internal bool ValidBinding, Cancelled, WithdrawalRequested, ReturnedPrepared;
        internal FanRemotePowerScope? ResumeParent;
        internal Exception? Failure;
        internal Exception? DeviceActionFailure;
        internal bool OpenReturned, EventsReturned, EncodedBusinessDeadline;
        internal bool ResumeModeSelected, SessionOnlyResume;
        internal long DeviceControlCycle;
        internal readonly ResourceEvidence Resources = new();
    }

    private readonly Owner owner;
    private bool released;

    // Ownership transfers even when the imported grant is rejected. Callers
    // always dispose this wrapper, and create borrowers only through Retain.
    internal FanRemotePowerScope(IRemoteNativePowerScope scope, RemotePowerPurpose purpose)
    {
        ArgumentNullException.ThrowIfNull(scope);
        owner = new Owner(scope, purpose);
        try
        {
            owner.Binding = scope.Binding;
            var binding = owner.Binding;
            owner.ValidBinding = binding.Size == 128 && binding.Version == 1 && binding.Reserved == 0 &&
                purpose is RemotePowerPurpose.FanResume or RemotePowerPurpose.FanWrite && binding.Purpose == purpose &&
                HasKey(binding.Scope) && HasKey(binding.Request) &&
                binding.Provider.Pid != 0 && binding.Provider.Birth100ns != 0 &&
                binding.Consumer.Pid != 0 && binding.Consumer.Birth100ns != 0 &&
                binding.Captured.Size == 48 && binding.Captured.Version == 1 && binding.Captured.Generation != 0 &&
                binding.Captured.Phase <= 3 && (binding.Captured.Flags & 1) != 0 && (binding.Captured.Flags & 4) == 0;
        }
        catch (Exception error) { owner.Failure = error; }
    }

    private FanRemotePowerScope(Owner owner) => this.owner = owner;
    internal RemotePowerDescriptorV1 Binding => owner.Binding;
    internal RemotePowerPurpose Purpose => owner.Purpose;
    internal object AuthorityIdentity => owner;
    internal bool IsReleased { get { lock (owner.Gate) return released; } }
    internal bool SharesAuthority(FanRemotePowerScope other) => ReferenceEquals(owner, other.owner);
    internal bool SameAuthority(FanRemotePowerScope other) =>
        ReferenceEquals(owner, other.owner) && IsCurrent && other.IsCurrent;
    internal Exception? LastFailure { get { lock (owner.Gate) return owner.Failure; } }
    internal Exception? FirstDeviceActionFailure { get { lock (owner.Gate) return owner.DeviceActionFailure; } }
    internal bool HasOutstandingDeviceCall { get { lock (owner.Gate) return owner.Resources.Calls != 0; } }
    internal void RecordDeviceActionFailure(Exception error) { lock (owner.Gate) owner.DeviceActionFailure ??= error; }
    internal (bool OpenReturned, bool EventsReturned, long ControlCycle) KnownDeviceStages
    { get { lock (owner.Gate) return (owner.OpenReturned, owner.EventsReturned, owner.DeviceControlCycle); } }
    internal void RecordKnownDeviceStage(string operation, long cycle)
    {
        lock (owner.Gate)
        {
            if (operation == "fan-open") owner.OpenReturned = true;
            if (operation is "open-events-finalize" or "source-open-ready") owner.EventsReturned = true;
            if (owner.OpenReturned || owner.EventsReturned) owner.DeviceControlCycle = cycle;
        }
    }
    internal bool SessionOnlyResume { get { lock (owner.Gate) return owner.SessionOnlyResume; } }
    internal void SelectResumeMode(bool sessionOnly)
    {
        lock (owner.Gate)
        {
            if (Purpose != RemotePowerPurpose.FanResume || !CurrentLocked() ||
                (owner.ResumeModeSelected && owner.SessionOnlyResume != sessionOnly))
                throw FanRemotePowerContext.Superseded();
            owner.ResumeModeSelected = true;
            owner.SessionOnlyResume = sessionOnly;
        }
    }
    internal void SelectEncodedBusinessDeadline()
    { lock (owner.Gate) owner.EncodedBusinessDeadline = true; }
    internal void RequireBusinessDeviceAdmission()
    {
        bool selected; lock (owner.Gate) selected = owner.EncodedBusinessDeadline;
        if (selected && !FanHostEngine.BusinessDeadlineCurrent(Binding.Request))
            throw new FanApiException(409, "FAN_RESUME_BUSINESS_EXPIRED", "Original resume business deadline elapsed")
                { FailureOrigin = FanPowerFailureOrigin.BusinessDeadline };
    }

    internal bool IsCurrent
    {
        get { lock (owner.Gate) return CurrentLocked(); }
    }

    // Cancellation hint only. A false result cannot authorize a device action;
    // final dispatch must still call IsCurrent against the unique Native source.
    internal bool IsRevoked
    {
        get
        {
            lock (owner.Gate)
            {
                if (released || !owner.ValidBinding || owner.Cancelled || owner.ReturnedPrepared) return true;
                try
                {
                    if (!owner.Scope.Revoked.WaitOne(0) && !owner.Scope.ProviderExited.WaitOne(0)) return false;
                }
                catch (Exception error) { owner.Failure = error; }
                owner.Cancelled = true;
                return true;
            }
        }
    }

    internal bool IsWriteChildOf(FanRemotePowerScope parent)
    {
        var child = owner.Binding;
        var original = parent.owner.Binding;
        return Purpose == RemotePowerPurpose.FanWrite && parent.Purpose == RemotePowerPurpose.FanResume &&
            !SameKey(child.Scope, original.Scope) && SameKey(child.Request, original.Request) &&
            SameProcess(child.Provider, original.Provider) && SameProcess(child.Consumer, original.Consumer) &&
            child.Captured.Generation == original.Captured.Generation && child.Captured.SuspendSerial == original.Captured.SuspendSerial &&
            child.Captured.ObservedNotificationSequence == original.Captured.ObservedNotificationSequence &&
            child.Captured.Phase == original.Captured.Phase && child.Captured.Flags == original.Captured.Flags &&
            child.Captured.PublicationRevision >= original.Captured.PublicationRevision && parent.IsCurrent && IsCurrent;
    }

    internal bool TryRetainResumeParent(FanRemotePowerScope parent)
    {
        if (!IsWriteChildOf(parent)) return false;
        lock (owner.Gate)
        {
            if (released || owner.Cancelled || owner.ReturnedPrepared) return false;
            if (owner.ResumeParent is { } existing)
            {
                return existing.SharesAuthority(parent);
            }
            owner.ResumeParent = parent.Retain();
            return owner.ResumeParent is not null;
        }
    }

    private bool CurrentLocked()
    {
        if (released || !owner.ValidBinding || owner.Cancelled || owner.ReturnedPrepared) return false;
        try
        {
            // Current performs the actual Native/provider/purpose check. A
            // readable observation alone never authorizes a fan operation.
            if (SameBinding(owner.Binding, owner.Scope.Binding) && owner.Scope.Current(owner.Purpose)) return true;
        }
        catch (Exception error) { owner.Failure = error; }
        owner.Cancelled = true;
        return false;
    }

    internal FanRemotePowerScope? Retain()
    {
        lock (owner.Gate)
        {
            if (!CurrentLocked() || owner.References == int.MaxValue) return null;
            var borrower = new FanRemotePowerScope(owner);
            owner.References++;
            return borrower;
        }
    }

    internal void CancelLocal()
    {
        lock (owner.Gate)
        {
            if (released) return;
            owner.Cancelled = true;
            if (owner.WithdrawalRequested) return;
            owner.WithdrawalRequested = true;
            try { owner.Scope.CancelLocal(); }
            catch (Exception error) { owner.Failure = error; }
        }
    }

    // Evidence flags come from the actual engine/resource owner. They are
    // never inferred from cancellation or reference-count disposal. Returning
    // a receipt here does not send it or claim that import handles closed.
    internal bool TryCreateSettlement(RemotePowerSettlementOutcome outcome,
        RemotePowerSettlementFlags evidence, out RemotePowerSettlementV1 receipt)
    {
        receipt = default;
        lock (owner.Gate)
        {
            if (released || !owner.ValidBinding || owner.ReturnedPrepared ||
                (evidence & ~RemotePowerSettlementFlags.AllReturned) != 0) return false;
            if (outcome == RemotePowerSettlementOutcome.Returned &&
                (owner.References != 1 || evidence != RemotePowerSettlementFlags.AllReturned)) return false;
            if (outcome is not (RemotePowerSettlementOutcome.Unknown or RemotePowerSettlementOutcome.Returned or
                RemotePowerSettlementOutcome.RetainedResources)) return false;
            try
            {
                if (!SameBinding(owner.Binding, owner.Scope.Binding) ||
                    !owner.Scope.TryCreateSettlement(outcome, evidence, out var candidate) ||
                    candidate.Size != 80 || candidate.Version != 1 || candidate.Outcome != outcome || candidate.Flags != evidence ||
                    !SameKey(candidate.Scope, owner.Binding.Scope) || !SameKey(candidate.Request, owner.Binding.Request) ||
                    !SameProcess(candidate.Provider, owner.Binding.Provider) || !SameProcess(candidate.Consumer, owner.Binding.Consumer)) return false;
                receipt = candidate;
                if (outcome == RemotePowerSettlementOutcome.Returned) owner.ReturnedPrepared = true;
                return true;
            }
            catch (Exception error) { owner.Failure = error; return false; }
        }
    }

    public void Dispose()
    {
        var closeImport = false;
        lock (owner.Gate)
        {
            if (released) return;
            released = true;
            closeImport = --owner.References == 0;
        }
        if (!closeImport) { NotifyResourceChange(); return; }
        // The importer owns only its handles. A failure stays observable and
        // cannot be translated into Returned, OEM restore or another scope.
        try { owner.Scope.Dispose(); }
        catch (Exception error) { lock (owner.Gate) owner.Failure = error; }
        finally { Interlocked.Exchange(ref owner.ResumeParent, null)?.Dispose(); }
    }

    private static bool HasKey(RemotePowerKey128 value) => value.Low != 0 || value.High != 0;
    private static bool SameKey(RemotePowerKey128 a, RemotePowerKey128 b) => a.Low == b.Low && a.High == b.High;
    private static bool SameProcess(RemotePowerProcessV1 a, RemotePowerProcessV1 b) =>
        a.Pid == b.Pid && a.Session == b.Session && a.Birth100ns == b.Birth100ns;
    private static bool SameBinding(RemotePowerDescriptorV1 a, RemotePowerDescriptorV1 b) =>
        a.Size == b.Size && a.Version == b.Version && a.Purpose == b.Purpose && a.Reserved == b.Reserved &&
        SameKey(a.Scope, b.Scope) && SameKey(a.Request, b.Request) &&
        SameProcess(a.Provider, b.Provider) && SameProcess(a.Consumer, b.Consumer) &&
        a.Captured.Size == b.Captured.Size && a.Captured.Version == b.Captured.Version &&
        a.Captured.Generation == b.Captured.Generation && a.Captured.PublicationRevision == b.Captured.PublicationRevision &&
        a.Captured.ObservedNotificationSequence == b.Captured.ObservedNotificationSequence &&
        a.Captured.Phase == b.Captured.Phase && a.Captured.Flags == b.Captured.Flags && a.Captured.SuspendSerial == b.Captured.SuspendSerial;
}
