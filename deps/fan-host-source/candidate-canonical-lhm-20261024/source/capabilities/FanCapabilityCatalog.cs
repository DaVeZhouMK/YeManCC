// AI-NAV[fan]: See AI-INDEX.json domains[fan] and FAN-RETURN-CONSUMPTION.json for the full handoff contract.
// This selects the existing ASUS session; FanLab/real-host/Program.cs and AsusFanCapabilitySession.cs own execution/release.
// A protocol receipt is not firmware acceptance; retain live lease, restore and failed-release settlement.
using System;
using System.Runtime.ExceptionServices;
using YeManFanBackend.Asus;

namespace YeManFanBackend.Capabilities;

// Source compiled into YeManFanBackend: the catalog adds no runtime DLL.
public sealed record FanCapabilityDescriptor(string DeviceId, string LegacyFactoryType, string Protocol)
{
    public string CapabilityId => HcDeviceCapabilityCatalog.Find(LegacyFactoryType)?.DeviceId ??
        throw new InvalidOperationException("HC_CAPABILITY_UNREGISTERED_DEVICE: " + LegacyFactoryType);
}

public static class FanCapabilityCatalog
{
    private static readonly object Gate = new();
    private static AsusFanCapabilitySession? owner;
    private static bool legacyDispatchInProgress;
    private static long ownershipGeneration;

    public static FanCapabilityDescriptor ResolveAsusCurve(string factoryType)
    {
        var device = HcDeviceCapabilityCatalog.Find(factoryType) ??
            throw new NotSupportedException($"No independent ASUS fan capability for {factoryType}");
        return device.ConsumerClass switch
        {
            "HandheldCompanion.Devices.ROGAlly" => new("asus.rog-ally", device.FactoryType, "asus.acpi.fan-curves"),
            "HandheldCompanion.Devices.ROGAllyX" => new("asus.rog-ally-x", device.FactoryType, "asus.acpi.fan-curves"),
            "HandheldCompanion.Devices.XboxROGAlly" => new("asus.xbox-rog-ally", device.FactoryType, "asus.acpi.fan-curves"),
            "HandheldCompanion.Devices.XboxROGAllyX" => new("asus.xbox-rog-ally-x", device.FactoryType, "asus.acpi.fan-curves"),
            _ => throw new NotSupportedException($"No independent ASUS fan capability for {factoryType}"),
        };
    }

    public static AsusFanCapabilitySession AcquireAsusCurve(string factoryType)
    {
        var descriptor = ResolveAsusCurve(factoryType);
        lock (Gate)
        {
            if (owner is not null || legacyDispatchInProgress)
                throw new InvalidOperationException("FAN_CAPABILITY_BUSY: fan writer already owned in this process");
            owner = new AsusFanCapabilitySession(descriptor, ++ownershipGeneration);
            return owner;
        }
    }

    internal static T WithOwner<T>(AsusFanCapabilitySession session, Func<T> operation)
    {
        lock (Gate)
        {
            if (!ReferenceEquals(owner, session))
                throw new InvalidOperationException("FAN_CAPABILITY_STALE: session no longer owns the writer");
            return operation();
        }
    }

    internal static void ObserveHostHandleClosed(AsusFanCapabilitySession session)
    {
        lock (Gate)
        {
            if (session.HandleReleased) return;
            if (!ReferenceEquals(owner, session) || AsusACPI.IsOpen)
                throw new InvalidOperationException("FAN_CAPABILITY_RELEASE_PENDING: ASUS handle is still owned");
            session.HandleReleased = true;
            owner = null;
        }
    }

    internal static void CloseOwnedHandle(AsusFanCapabilitySession session)
    {
        lock (Gate)
        {
            if (session.HandleReleased) return;
            if (!ReferenceEquals(owner, session))
                throw new InvalidOperationException("FAN_CAPABILITY_STALE: cannot close another session's handle");
            AsusACPI.Close();
            ObserveHostHandleClosed(session);
        }
    }

    // Explicit caller-owned handoff. Never invoked automatically after a failed write.
    // The callback may be a production HC adapter or a source reference in an isolated test;
    // its return is callback evidence only, never physical OEM confirmation.
    public static void InvokeHcAfterRelease(AsusFanCapabilitySession previous, Action hcCallback, Func<bool> hcHandleReleased)
    {
        ArgumentNullException.ThrowIfNull(hcCallback);
        ArgumentNullException.ThrowIfNull(hcHandleReleased);
        lock (Gate)
        {
            if (owner is not null || legacyDispatchInProgress || AsusACPI.IsOpen ||
                !previous.CanHandBackToHc || previous.HandoffClaimed || previous.Generation != ownershipGeneration)
                throw new InvalidOperationException("FAN_CAPABILITY_HANDOFF_PENDING: default curves and handle release required");
            previous.HandoffClaimed = true;
            ++ownershipGeneration;
            legacyDispatchInProgress = true;
            Exception? callbackFailure = null;
            Exception? closeFailure = null;
            try { hcCallback(); previous.HcCallbackReturned = true; }
            catch (Exception ex) { callbackFailure = ex; }
            // Unknown/failed close keeps the writer blocked until this Host exits.
            try { if (hcHandleReleased()) legacyDispatchInProgress = false; }
            catch (Exception ex) { closeFailure = ex; }
            if (callbackFailure is not null && closeFailure is not null)
                throw new AggregateException("HC callback and handle-close observation both failed", callbackFailure, closeFailure);
            if (callbackFailure is not null) ExceptionDispatchInfo.Capture(callbackFailure).Throw();
            if (closeFailure is not null) ExceptionDispatchInfo.Capture(closeFailure).Throw();
            if (legacyDispatchInProgress)
                throw new InvalidOperationException("FAN_CAPABILITY_HC_RELEASE_PENDING: HC handle closure unconfirmed");
        }
    }
}
