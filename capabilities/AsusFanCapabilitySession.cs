// AI-NAV[fan]: This is the existing ASUS session consumer, not a second device factory.
// Follow FanCapabilityCatalog.cs and FAN-RETURN-CONSUMPTION.json before changing apply, handoff or disposal.
// The Host retains ACPI/resource lifecycle; session disposal alone must not be read as physical restore proof.
using System;
using System.Text.Json.Serialization;
using YeManFanBackend.Asus;

namespace YeManFanBackend.Capabilities;

[JsonConverter(typeof(JsonStringEnumConverter<FanCapabilityTransportOutcome>))]
public enum FanCapabilityTransportOutcome { CompleteTransport, ShortTransport, Partial, Failed, Unknown }

public sealed record FanCapabilityResult(
    long Sequence, string DeviceId, string Operation, FanCapabilityTransportOutcome Outcome,
    int ReceiptCount, string PhysicalObservation = "not-observed", string? ScopeId = null, string? CapabilityId = null);

public sealed class AsusFanCapabilitySession
{
    private long sequence;
    internal AsusFanCapabilitySession(FanCapabilityDescriptor descriptor, long generation)
    { Descriptor = descriptor; Generation = generation; }
    internal long Generation { get; }
    public FanCapabilityDescriptor Descriptor { get; }
    public FanCapabilityResult? LastResult { get; private set; }
    public bool HandleReleased { get; internal set; }
    public bool HcCallbackReturned { get; internal set; }
    internal bool HandoffClaimed { get; set; }
    public bool CanHandBackToHc => HandleReleased &&
        LastResult is { Operation: "restore-default-curves", Outcome: FanCapabilityTransportOutcome.CompleteTransport };

    public FanCapabilityResult ApplySoftwareCurve(double[]? points) =>
        FanCapabilityCatalog.WithOwner(this, () =>
        {
            BeginOperation("apply-software-curve");
            // ToAsusCurve preserves the pinned HC conversion, including malformed input behavior.
            AsusFanCurveWriter.ApplySoftwareCurveObserved(AsusFanCurveWriter.ToAsusCurve(points), out var receipts);
            return Record("apply-software-curve", receipts);
        });

    // Opt-in software write admission. Keep the existing CLR entry point above.
    // The callback may throw the caller's normal superseded exception unchanged.
    public FanCapabilityResult ApplySoftwareCurve(double[]? points, Func<bool>? currentOperation)
    {
        if (currentOperation is null) return ApplySoftwareCurve(points);
        return FanCapabilityCatalog.WithOwner(this, () =>
        {
            BeginOperation("apply-software-curve");
            var receipts = Array.Empty<AsusAcpiCallReceipt>();
            try
            {
                AsusFanCurveWriter.ApplySoftwareCurveObserved(
                    AsusFanCurveWriter.ToAsusCurve(points), out receipts, currentOperation);
            }
            finally
            {
                // Actual completed/uncertain I/O remains evidence even when the
                // post-call admission check throws. Zero I/O stays Unknown.
                if (receipts.Length > 0) Record("apply-software-curve", receipts);
            }
            return LastResult!;
        });
    }

    public FanCapabilityResult RestoreDefaultCurves() =>
        FanCapabilityCatalog.WithOwner(this, () =>
        {
            BeginOperation("restore-default-curves");
            AsusFanCurveWriter.RestoreDefaultCurvesObserved(out var receipts);
            return Record("restore-default-curves", receipts);
        });

    // Host owns close/suspend ordering. This only returns the catalog lease after its real handle close.
    public void ObserveHostHandleClosed() => FanCapabilityCatalog.ObserveHostHandleClosed(this);
    public void CloseOwnedHandle() => FanCapabilityCatalog.CloseOwnedHandle(this);

    private void BeginOperation(string operation) => LastResult =
        new(++sequence, Descriptor.DeviceId, operation, FanCapabilityTransportOutcome.Unknown, 0, CapabilityId: Descriptor.CapabilityId);

    private FanCapabilityResult Record(string operation, AsusAcpiCallReceipt[] receipts)
    {
        var complete = 0; var shortCount = 0; var unknown = 0;
        foreach (var receipt in receipts)
        {
            if (receipt.Status == AsusAcpiTransportStatus.TransportedComplete) complete++;
            else if (receipt.Status == AsusAcpiTransportStatus.TransportedShort) shortCount++;
            else if (receipt.Status == AsusAcpiTransportStatus.IoException) unknown++;
        }
        var outcome = unknown > 0 ? FanCapabilityTransportOutcome.Unknown
            : complete == 3 && receipts.Length == 3 ? FanCapabilityTransportOutcome.CompleteTransport
            : complete + shortCount == 3 && receipts.Length == 3 ? FanCapabilityTransportOutcome.ShortTransport
            : complete + shortCount > 0 ? FanCapabilityTransportOutcome.Partial
            : FanCapabilityTransportOutcome.Failed;
        LastResult = new(sequence, Descriptor.DeviceId, operation, outcome, receipts.Length,
            ScopeId: receipts.Length == 0 ? null : receipts[0].ScopeId, CapabilityId: Descriptor.CapabilityId);
        return LastResult;
    }
}
