using YeManCC.SharedSleepPower;

namespace YeManFanHost;

internal enum FanPowerFailureOrigin { Unclassified, PublicAdmission, BusinessDeadline }

// Request context carries an imported Native scope, never a new power source.
// Once importer-backed admission is selected it cannot fall back to legacy
// admission. Queue owners must capture before enqueue and enter on execution.
internal sealed class FanRemotePowerContext
{
    private readonly AsyncLocal<FanRemotePowerScope?> request = new();
    private int required;
    internal bool Required => Volatile.Read(ref required) != 0;
    internal void RequireImportedPower() => Interlocked.Exchange(ref required, 1);
    internal FanRemotePowerScope? Borrowed => request.Value;
    internal Func<FanRemotePowerScope, FanRemotePowerScope?>? ResumeParentForWrite { get; set; }

    internal IDisposable Import(IRemoteNativePowerScope imported, RemotePowerPurpose purpose)
    {
        Interlocked.Exchange(ref required, 1);
        var scope = new FanRemotePowerScope(imported, purpose);
        if (!scope.IsCurrent) { scope.Dispose(); throw Superseded(); }
        return new Frame(this, scope);
    }

    internal FanRemotePowerScope? Capture()
    {
        if (!Required) return null; // Existing transport is staged separately.
        return request.Value?.Retain() ?? throw Superseded();
    }

    internal IDisposable Enter(FanRemotePowerScope scope) =>
        new Frame(this, scope.Retain() ?? throw Superseded());

    internal IDisposable EnterImportedRoot(FanRemotePowerScope scope)
    {
        Interlocked.Exchange(ref required, 1);
        return Enter(scope);
    }

    internal static FanApiException Superseded() =>
        new(409, "FAN_PUBLIC_POWER_SUPERSEDED", "公共睡眠许可已失效，保留控制意图等待当前许可")
        { FailureOrigin = FanPowerFailureOrigin.PublicAdmission };

    private sealed class Frame : IDisposable
    {
        private readonly FanRemotePowerContext context;
        private readonly FanRemotePowerScope? previous;
        private FanRemotePowerScope? scope;
        internal Frame(FanRemotePowerContext context, FanRemotePowerScope scope)
        {
            this.context = context;
            this.scope = scope;
            previous = context.request.Value;
            context.request.Value = scope;
        }
        public void Dispose()
        {
            var captured = Interlocked.Exchange(ref scope, null);
            if (captured is null) return;
            if (ReferenceEquals(context.request.Value, captured)) context.request.Value = previous;
            captured.Dispose();
        }
    }
}

internal sealed partial class RealHcBackend
{
    // Production intake will supply the authenticated import here. Until that
    // transport is wired, no remote scope or production completion is claimed.
    internal FanRemotePowerContext SharedPowerContext { get; set; } = new();
    internal IDisposable BindImportedPowerForRequest(IRemoteNativePowerScope scope, RemotePowerPurpose purpose) =>
        SharedPowerContext.Import(scope, purpose);

    private T OnStaPowerBounded<T>(Func<T> action, string operationName = "hc-operation")
    {
        using var scope = SharedPowerContext.Capture();
        return OnStaBoundedCore(() => TrackPublicDeviceCall(scope, action, operationName), operationName, scope);
    }

    private void OnStaPowerBounded(Action action, string operationName = "hc-operation") =>
        OnStaPowerBounded(() => { action(); return true; }, operationName);
}

internal sealed partial class FanHostEngine
{
    internal FanRemotePowerContext SharedPowerContext { get; } = new();
    private FanRemotePowerScope? resumeContinuationPublicPower;
    // Resume parent remains owned after the worker transfers its responsibility
    // to control. The Native-issued FanWrite child and its final ACK are wired
    // with the authenticated importer; a later global Ready cannot supply it.
    private readonly List<FanRemotePowerScope> residentResumePublicPower = [];

    internal IDisposable BindImportedPowerForRequest(IRemoteNativePowerScope scope, RemotePowerPurpose purpose) =>
        SharedPowerContext.Import(scope, purpose);

    private void SettleReturnedResumePublicPower()
    {
        // Do not dispose a cancelled scope while a partial Open/unknown SDK
        // call still owes return. These retained owners grant no new actions
        // and never synthesize Returned; original resource evidence retires them.
        if (HasLiveHardwareSession()) return;
        realBackend?.ReleaseRetiredControlPublicPower();
        foreach (var scope in residentResumePublicPower) scope.Dispose();
        residentResumePublicPower.Clear();
    }

    private static void RequireResumePublicPower(ResumeOperationIdentity? operation)
    {
        if (operation?.PublicPower is { } scope && !scope.IsCurrent) throw FanRemotePowerContext.Superseded();
    }
}
