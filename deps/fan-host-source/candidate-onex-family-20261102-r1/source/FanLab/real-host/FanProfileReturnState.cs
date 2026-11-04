namespace YeManFanHost;

// This state consumes only the existing catalog's legacy duty/RPM callback
// contracts. It adds no model table, transport, polling or physical evidence.
internal sealed partial class RealHcBackend
{
    private object? legacyCallbackFanReturnOwner;
    private bool legacyCallbackFanReturnOwed;
    private bool UsesLegacyCallbackAppliedFanGuard => Route is
    {
        Evidence: FanEvidenceMode.CompatCallbackNoScope,
        Kind: FanRouteKind.GenericDuty or FanRouteKind.GenericRpmTarget,
        Precondition: FanRoutePrecondition.OpenLibHandleOpen or FanRoutePrecondition.DeviceOpenAndSupported,
    };
    internal bool AppliedProfileReturnOwedForSelfTest => legacyCallbackFanReturnOwed;
    // Existing original OneX cases retain their exact accessor and assertions.
    internal bool LegacyOneXFanReturnOwedForSelfTest => legacyCallbackFanReturnOwed;
    private void RequireLegacyCallbackFanReturnOwner()
    {
        if (legacyCallbackFanReturnOwed && !ReferenceEquals(legacyCallbackFanReturnOwner, device))
            throw new FanApiException(409, "FAN_RETURN_OWNER_CHANGED",
                "Fan return still belongs to the previous exact device session");
    }
    private bool SkipLegacyCallbackDefaultWithoutSoftware()
    {
        RequireLegacyCallbackFanReturnOwner();
        return UsesLegacyCallbackAppliedFanGuard && !legacyCallbackFanReturnOwed &&
            activeCurveDuties is null && activeFanProfile is null;
    }
    private void BeginLegacyCallbackFanReturnRisk(bool rawOpenEvents = false)
    {
        RequireLegacyCallbackFanReturnOwner();
        if (!UsesLegacyCallbackAppliedFanGuard) return;
        // Consume the existing live per-route predicate: Steam requires open +
        // supported, while EC routes require open + OpenLib. Do not interchange.
        if (!rawOpenEvents && !EvaluateCompatCallbackPreconditions().Ok) return;
        legacyCallbackFanReturnOwner = device ?? throw new InvalidOperationException("Fan session missing");
        // Raw HC OpenEvents can call its own profile internally. Record that
        // possibility before entry, including a partial throw. It is not proof
        // that software control, a write, or physical ownership was established.
        legacyCallbackFanReturnOwed = true;
    }
    private void CompleteLegacyCallbackFanReturn()
    {
        RequireLegacyCallbackFanReturnOwner();
        if (!UsesLegacyCallbackAppliedFanGuard) return;
        legacyCallbackFanReturnOwed = false;
        legacyCallbackFanReturnOwner = null;
    }
}
