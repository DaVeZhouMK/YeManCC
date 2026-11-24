using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace YeManCC.SharedSleepPower;

// Candidate contract paired with native/sleep_power_remote_contract.h.
// This file supplies no provider, power source, timer, or permissive fallback.
public enum RemotePowerPurpose : uint { Observation=1, FanResume=2, FanWrite=3 }

[StructLayout(LayoutKind.Sequential, Pack=8)]
public struct RemotePowerKey128 { public ulong Low,High; }

[StructLayout(LayoutKind.Sequential, Pack=8)]
public struct RemotePowerProcessV1
{
    public uint Pid,Session;
    public ulong Birth100ns;
}

[StructLayout(LayoutKind.Sequential, Pack=8)]
public struct SharedPowerSnapshotV1
{
    public uint Size,Version;
    public ulong Generation,PublicationRevision,ObservedNotificationSequence;
    public uint Phase,Flags;
    public ulong SuspendSerial;
}

[StructLayout(LayoutKind.Sequential, Pack=8)]
public struct RemotePowerDescriptorV1
{
    public uint Size,Version;
    public RemotePowerKey128 Scope,Request;
    public RemotePowerProcessV1 Provider,Consumer;
    public RemotePowerPurpose Purpose;
    public uint Reserved;
    public SharedPowerSnapshotV1 Captured;
}

public enum RemotePowerSettlementOutcome : uint
{
    Unknown=0, Returned=1, RetainedResources=2
}

[Flags]
public enum RemotePowerSettlementFlags : uint
{
    None=0, WorkerStopped=1, BorrowersReturned=2, OwnedOccupancyLeaseSettled=4,
    NoOutstandingDeviceCall=8, OwnedSdkResourcesReturned=16, AllReturned=31
}

[StructLayout(LayoutKind.Sequential, Pack=8)]
public struct RemotePowerSettlementV1
{
    public uint Size,Version;
    public RemotePowerKey128 Scope,Request;
    public RemotePowerProcessV1 Provider,Consumer;
    public RemotePowerSettlementOutcome Outcome;
    public RemotePowerSettlementFlags Flags;
}

// Implemented only by an authenticated import from the unique Native provider.
// An absent provider is an absent scope, never an always-current implementation.
// A consumer retains the exact scope through accepted/deferred work and its
// real resource return; it never replaces this instance with a newer Ready.
public interface IRemoteNativePowerScope : IDisposable
{
    RemotePowerDescriptorV1 Binding { get; }

    // Borrowed wait objects. Do not dispose/reset/set them. Implementations own
    // SYNCHRONIZE-only event and QUERY_LIMITED_INFORMATION|SYNCHRONIZE process
    // duplicates; the consumer owns no Native event modification rights.
    WaitHandle Revoked { get; }
    WaitHandle ProviderExited { get; }

    // A coherent fresh source observation; it can report pending/NonReady for
    // diagnostics. Returning true does not grant device or purpose admission.
    bool TryReadCurrent(out SharedPowerSnapshotV1 value);

    // Exact Native-issued purpose, authenticated binding, live provider,
    // non-revoked scope, coherent current tuple and no pending/UNKNOWN.
    // FanResume preserves the original Native F5 purpose gate, not strict Ready.
    // Original Fan/device owner/intent/budget checks remain additional gates.
    bool Current(RemotePowerPurpose requiredPurpose);

    // Sticky local withdrawal only; never OEM Close/restore/FaultLocked.
    void CancelLocal();

    // Creates a bound receipt after caller-proved resource state. Does not send
    // it, refund a budget, close another owner's lease, or prove SDK cleanup.
    // Returned requires AllReturned. Unknown/RetainedResources keep provider debt.
    bool TryCreateSettlement(RemotePowerSettlementOutcome outcome,
        RemotePowerSettlementFlags flags, out RemotePowerSettlementV1 receipt);

    // Dispose releases this import's owned handles and permanently cancels it.
    // Dispose/HTTP completion alone must never synthesize a Returned receipt.
}
