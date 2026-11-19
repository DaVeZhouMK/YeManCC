using System.IO;
using YeManFanBackend.Capabilities;

// Narrow ownership operation, independent of colors and fan/charging policy.
internal interface IAyaneoOemRestoreIo : IDisposable
{
    LightingOemOwnershipProtocol Protocol { get; }
    bool ReleaseLegacy();
    void EnterRam();
    void WritePort(ushort port, byte value);
    void ExitRam();
}
internal readonly record struct AyaneoOemRestoreResult(bool RequestCompleted, int Writes, string Reason);
internal static class AyaneoOemLightingRestore
{
    internal static AyaneoOemRestoreResult Apply(LightingOemOwnershipCapability capability,
        IAyaneoOemRestoreIo io, CancellationToken stop)
    {
        var writes = 0;
        if (io.Protocol != capability.Protocol) return new(false, 0, "ownership-transport-mismatch");
        try
        {
            stop.ThrowIfCancellationRequested();
            if (capability.Protocol == LightingOemOwnershipProtocol.LegacyCec)
            {
                writes++;
                return io.ReleaseLegacy() ? new(true, writes, "ok") : new(false, writes, "oem-restore-write-failed");
            }
            if (capability.Protocol != LightingOemOwnershipProtocol.CeiiRam)
                return new(false, 0, "ownership-protocol-unavailable");
            // Exact CEii ECRamDirectWrite(0x87,0,0xD1) port sequence. No device
            // Open/Close, fan setter, mode, battery limit or bypass-charge call.
            try
            {
                io.EnterRam();
                foreach (var pair in new (ushort Port, byte Value)[] {
                    (0x4E,0x2E),(0x4F,0x11),(0x4E,0x2F),(0x4F,0xD1),
                    (0x4E,0x2E),(0x4F,0x10),(0x4E,0x2F),(0x4F,0x87),
                    (0x4E,0x2E),(0x4F,0x12),(0x4E,0x2F),(0x4F,0x00) })
                {
                    stop.ThrowIfCancellationRequested();
                    writes++;
                    io.WritePort(pair.Port, pair.Value);
                }
            }
            finally { io.ExitRam(); }
            return new(true, writes, "ok");
        }
        catch (OperationCanceledException) { return new(false, writes, "oem-restore-cancelled"); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidOperationException)
        { return new(false, writes, "oem-restore-write-failed"); }
    }
}
