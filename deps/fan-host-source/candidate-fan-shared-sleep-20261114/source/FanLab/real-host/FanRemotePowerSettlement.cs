using YeManCC.SharedSleepPower;

namespace YeManFanHost;

// [LIFECYCLE:FAN:REMOTE-RETURN] One ledger root survives all HTTP/Channel/STA
// borrowers. Only actual completion removes an obligation. Cancellation and
// HasLiveHardwareSession=false prove none of these five return conditions.
internal sealed partial class FanRemotePowerScope
{
    private sealed class ResourceEvidence
    {
        internal readonly HashSet<object> Sdk = [];
        internal readonly HashSet<string> Leases = [];
        internal int Workers, Calls;
        internal Action? Changed;
        internal Func<RemotePowerSettlementV1, RemoteSettlementDelivery>? Send;
        internal RemoteSettlementDelivery Delivery;
        internal bool DeliveryAttempted, SendStarted;
        internal RemotePowerSettlementV1 FinalReceipt;
    }

    internal void InstallSettlementOwner(Action changed,
        Func<RemotePowerSettlementV1, RemoteSettlementDelivery> sender)
    {
        lock (owner.Gate)
        {
            if (owner.Resources.Send is not null) throw new InvalidOperationException("Duplicate settlement owner");
            owner.Resources.Changed = changed;
            owner.Resources.Send = sender;
        }
    }

    private void NotifyResourceChange()
    {
        Action? changed;
        lock (owner.Gate) changed = owner.Resources.Changed;
        try { changed?.Invoke(); }
        catch (Exception error) { lock (owner.Gate) owner.Failure = error; }
    }

    internal IDisposable TrackWorker() => TrackExecution(worker: true);
    internal IDisposable TrackDeviceCall() => TrackExecution(worker: false);
    private IDisposable TrackExecution(bool worker)
    {
        lock (owner.Gate)
        {
            if (owner.ReturnedPrepared) throw FanRemotePowerContext.Superseded();
            if (worker) checked { owner.Resources.Workers++; }
            else checked { owner.Resources.Calls++; }
        }
        return new ResourceReturn(() =>
        {
            lock (owner.Gate)
            {
                if (worker) owner.Resources.Workers--;
                else owner.Resources.Calls--;
            }
            NotifyResourceChange();
        });
    }

    // These methods record resource facts even after this borrower was disposed
    // by its bounded observer. The ledger root and outstanding SDK call remain.
    internal void OweSdk(object epoch) { lock (owner.Gate) owner.Resources.Sdk.Add(epoch); }
    internal void ReturnSdk(object epoch)
    {
        lock (owner.Gate) owner.Resources.Sdk.Remove(epoch);
        NotifyResourceChange();
    }
    internal void OweLease(string key) { lock (owner.Gate) owner.Resources.Leases.Add(key); }
    internal void ReturnLease(string key)
    {
        lock (owner.Gate) owner.Resources.Leases.Remove(key);
        NotifyResourceChange();
    }

    internal bool TryPrepareFinalReturn(out RemotePowerSettlementV1 receipt)
    {
        receipt = default;
        lock (owner.Gate)
        {
            var r = owner.Resources;
            if (r.Send is null || r.DeliveryAttempted || r.Workers != 0 || r.Calls != 0 ||
                r.Sdk.Count != 0 || r.Leases.Count != 0 || owner.References != 1) return false;
            if (!TryCreateSettlement(RemotePowerSettlementOutcome.Returned, RemotePowerSettlementFlags.AllReturned, out receipt)) return false;
            r.FinalReceipt = receipt;
            r.DeliveryAttempted = true; // Reserve once before any IO; Unknown is terminal for this send.
            return true;
        }
    }

    internal RemoteSettlementDelivery SendPreparedFinalReturn(RemotePowerSettlementV1 receipt)
    {
        Func<RemotePowerSettlementV1, RemoteSettlementDelivery>? sender;
        lock (owner.Gate)
        {
            var r = owner.Resources;
            if (!r.DeliveryAttempted || r.SendStarted || !receipt.Equals(r.FinalReceipt))
                return RemoteSettlementDelivery.Rejected;
            r.SendStarted = true;
            sender = r.Send;
        }
        var delivery = RemoteSettlementDelivery.Unknown;
        try { if (sender is not null) delivery = sender(receipt); }
        catch (Exception error) { lock (owner.Gate) owner.Failure = error; }
        lock (owner.Gate) owner.Resources.Delivery = delivery;
        return delivery;
    }

    internal object SettlementSnapshot
    {
        get
        {
            lock (owner.Gate)
            {
                var r = owner.Resources;
                return new { references = owner.References, workers = r.Workers, calls = r.Calls,
                    sdk = r.Sdk.Count, leases = r.Leases.Count, deliveryAttempted = r.DeliveryAttempted,
                    delivery = r.Delivery.ToString(), finalFlags = r.FinalReceipt.Flags.ToString() };
            }
        }
    }

    private sealed class ResourceReturn(Action returned) : IDisposable
    {
        private Action? action = returned;
        public void Dispose() => Interlocked.Exchange(ref action, null)?.Invoke();
    }
}

internal sealed partial class RealHcBackend
{
    private readonly object publicSdkGate = new();
    private object publicSdkEpoch = new();
    private readonly Dictionary<object, FanRemotePowerScope> publicSdkBorrowers = [];
    private long publicSdkCycle = -1;
    private object? publicSdkSource, publicSdkInherited, publicSdkDevice;
    private bool publicSdkAsusOpened;
    internal Action? PublicResourcesChanged { get; set; }
    internal bool PublicSettlementLockHeld => Monitor.IsEntered(hardwareGate) || Monitor.IsEntered(staGate);

    private bool PublicSdkOutstanding => IsOpen || OpenAttempted || HcCloseIsOwed || SourceCloseIsOwed ||
        SourceIoPending || OperationTimedOut || hcOpenInvocationStarted || backendAsusAcpiOpened;

    private void RecordPublicSdkBorrow(FanRemotePowerScope? scope)
    {
        if (scope is null) return;
        if (!Monitor.TryEnter(hardwareGate))
        {
            // Accounting must not introduce a new STA/hardware-lock wait. An
            // unavailable ownership observation retains debt until real Close.
            lock (publicSdkGate)
            {
                scope.OweSdk(publicSdkEpoch);
                publicSdkBorrowers[scope.AuthorityIdentity] = scope;
            }
            return;
        }
        try
        {
            if (!PublicSdkOutstanding) return;
            lock (publicSdkGate)
            {
                // A later read/write borrows the existing SDK session; it did
                // not acquire a second copy of that session's owned resources.
                // Its actual device call and control producers remain tracked.
                if (publicSdkBorrowers.Count != 0 && publicSdkCycle == ControlCycleId &&
                    ReferenceEquals(publicSdkSource, sourceFanSession) &&
                    ReferenceEquals(publicSdkInherited, inheritedEcFanSession) &&
                    ReferenceEquals(publicSdkDevice, device) && publicSdkAsusOpened == backendAsusAcpiOpened) return;
                scope.OweSdk(publicSdkEpoch);
                publicSdkBorrowers[scope.AuthorityIdentity] = scope;
                publicSdkCycle = ControlCycleId; publicSdkSource = sourceFanSession;
                publicSdkInherited = inheritedEcFanSession; publicSdkDevice = device;
                publicSdkAsusOpened = backendAsusAcpiOpened;
            }
        }
        finally { Monitor.Exit(hardwareGate); }
    }

    private T TrackPublicDeviceCall<T>(FanRemotePowerScope? scope, Func<T> action, string operationName)
    {
        scope?.RequireBusinessDeviceAdmission();
        using var call = scope?.TrackDeviceCall();
        RecordPublicSdkBorrow(scope);
        try
        {
            var result = action();
            scope?.RecordKnownDeviceStage(operationName, ControlCycleId);
            return result;
        }
        catch (Exception error)
        {
            if (error is not FanApiException { FailureOrigin: FanPowerFailureOrigin.PublicAdmission or FanPowerFailureOrigin.BusinessDeadline })
                scope?.RecordDeviceActionFailure(error);
            throw;
        }
        finally { RecordPublicSdkBorrow(scope); }
    }

    // Called only after the ORIGINAL Close actually returned, including its
    // family cleanup and owned backend handle return. Never from Snapshot/GET.
    private void RecordPublicSdkCloseReturned()
    {
        FanRemotePowerScope[] scopes;
        object returnedEpoch;
        lock (hardwareGate)
        {
            if (PublicSdkOutstanding || cpuTemperatureMonitor is not null || powerProfileWatchdog is not null ||
                !(SourceControlMode ? SourceControlCloseComplete : HasVerifiedRetiredDeviceWithoutNewOpen)) return;
            lock (publicSdkGate)
            {
                scopes = publicSdkBorrowers.Values.ToArray();
                publicSdkBorrowers.Clear();
                returnedEpoch = publicSdkEpoch;
                publicSdkEpoch = new object();
                publicSdkCycle = -1; publicSdkSource = publicSdkInherited = publicSdkDevice = null;
                publicSdkAsusOpened = false;
            }
            ReleaseRetiredControlPublicPower();
        }
        foreach (var scope in scopes) scope.ReturnSdk(returnedEpoch);
        PublicResourcesChanged?.Invoke();
    }
}
