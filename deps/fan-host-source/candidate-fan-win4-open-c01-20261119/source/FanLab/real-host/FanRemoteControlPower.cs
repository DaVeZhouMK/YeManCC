using YeManCC.SharedSleepPower;

namespace YeManFanHost;

internal sealed partial class RealHcBackend
{
    // [LIFECYCLE:FAN:CONTROL-POWER] A control grant is Native-issued FanWrite,
    // never an upgraded FanResume. Each original timer keeps its producer root;
    // new child adoption retires old producers. Early hints only read sticky
    // waits; actual device actions use Current. Return keeps its existing path.
    private FanRemotePowerScope? controlPublicPower;

    private FanRemotePowerScope? PublicPowerForAction =>
        SharedPowerContext.Borrowed is { IsReleased: false } request ? request : Volatile.Read(ref controlPublicPower);

    private bool PublicControlWritesBlocked => SharedPowerContext.Required &&
        (Volatile.Read(ref controlPublicPower) is not { } scope || scope.Purpose != RemotePowerPurpose.FanWrite || scope.IsRevoked);

    private bool RequireCurrentPublicPower()
    {
        if (!SharedPowerContext.Required) return true;
        if (PublicPowerForAction is not { } scope || !scope.IsCurrent) throw FanRemotePowerContext.Superseded();
        return true;
    }

    private IDisposable? EnterControlPublicPower()
    {
        if (!SharedPowerContext.Required) return null;
        var scope = Volatile.Read(ref controlPublicPower);
        if (scope is null || scope.Purpose != RemotePowerPurpose.FanWrite || scope.IsRevoked) throw FanRemotePowerContext.Superseded();
        return SharedPowerContext.Enter(scope);
    }

    // Called by the acknowledged explicit curve operation or the authenticated
    // Native child handoff. No request flags can mint or upgrade this scope.
    internal bool AdoptCurrentWritePowerForControl(FanRemotePowerScope? resumeParent = null, bool rearmExisting = true)
    {
        using var request = SharedPowerContext.Capture();
        if (request is null) return false; // Existing staged transport remains separate.
        if (request.Purpose != RemotePowerPurpose.FanWrite)
        {
            if (resumeParent is not null) throw FanRemotePowerContext.Superseded();
            return false; // F5 rebuild may finish, but has no continuous write grant.
        }
        resumeParent ??= SharedPowerContext.ResumeParentForWrite?.Invoke(request);
        if (resumeParent is not null && !request.TryRetainResumeParent(resumeParent)) throw FanRemotePowerContext.Superseded();
        lock (hardwareGate)
        {
            if (!request.IsCurrent) throw FanRemotePowerContext.Superseded();
            if (controlPublicPower is { } current && request.SharesAuthority(current)) return true;
            var retained = request.Retain() ?? throw FanRemotePowerContext.Superseded();
            var previous = controlPublicPower;
            controlPublicPower = retained;
            previous?.Dispose();
            // Rebind only this existing producer when a real child arrives.
            // This is no polling loop and does not reset profile retry budgets.
            if (rearmExisting && activeCurveDuties is not null && powerProfileWatchdog is not null) EnsurePowerProfileWatchdog();
            cpuTemperatureMonitor?.RequestSampleDelivery();
            return true;
        }
    }

    internal void ReleaseRetiredControlPublicPower()
    {
        lock (hardwareGate)
        {
            if (IsOpen || OpenAttempted || OperationTimedOut || HcCloseIsOwed) return;
            Interlocked.Exchange(ref controlPublicPower, null)?.Dispose();
        }
    }
}
