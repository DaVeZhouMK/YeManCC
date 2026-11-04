using System.IO;

// Fixed HC f2e58468 AYANEOSuperJoyRgb protocol dependency. Callers must supply
// an admitted, exclusive vendor endpoint and current owner generation. This
// class neither discovers/opens HID nor admits a model to the runtime catalog.
internal readonly record struct SuperJoyRgb(byte R, byte G, byte B);
internal readonly record struct SuperJoyIdentity(string ModelClass, string DevicePath, long Generation,
    ushort VendorId = 0x1C4F, ushort ProductId = 0x0002, ushort UsagePage = 0xFF00,
    ushort Usage = 1, int InputReportLength = 65, int OutputReportLength = 65)
{
    internal bool MatchesProtocol => Program.IsSuperJoySourceModel(ModelClass) &&
        !string.IsNullOrWhiteSpace(DevicePath) && Generation > 0 && VendorId == 0x1C4F &&
        ProductId == 2 && UsagePage == 0xFF00 && Usage == 1 && InputReportLength == 65 && OutputReportLength == 65;
}
internal interface ISuperJoyLightingTransport : IDisposable
{
    SuperJoyIdentity Identity { get; }
    bool IsConnected { get; }
    ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report, CancellationToken cancellationToken);
    ValueTask<int> ReadAsync(Memory<byte> response, CancellationToken cancellationToken);
    void CancelPendingIo();
}
internal readonly record struct SuperJoyLightingResult(bool Acknowledged, int Writes, int Reads, string Reason);
internal sealed class AyaneoSuperJoyLightingSession : IDisposable
{
    internal const int MaximumAttempts = 3;
    internal const int ResponseTimeoutMilliseconds = 300;
    private readonly ISuperJoyLightingTransport transport;
    private readonly SuperJoyIdentity identity;
    private readonly Func<bool> ownerIsCurrent;
    private readonly SemaphoreSlim transaction = new(1, 1);
    private readonly CancellationTokenSource lifetime = new();
    private int closed;

    internal AyaneoSuperJoyLightingSession(ISuperJoyLightingTransport transport, SuperJoyIdentity identity,
        Func<bool> ownerIsCurrent)
    {
        ArgumentNullException.ThrowIfNull(transport);
        ArgumentNullException.ThrowIfNull(ownerIsCurrent);
        if (!identity.MatchesProtocol || transport.Identity != identity)
            throw new ArgumentException("SUPERJOY_ENDPOINT_IDENTITY_MISMATCH", nameof(identity));
        this.transport = transport;
        this.identity = identity;
        this.ownerIsCurrent = ownerIsCurrent;
    }
    private bool IsCurrent() => Volatile.Read(ref closed) == 0 && transport.IsConnected &&
        transport.Identity == identity && ownerIsCurrent();

    internal async Task<SuperJoyLightingResult> SetSticksAsync(SuperJoyRgb left, SuperJoyRgb right,
        int brightness, bool enabled, CancellationToken cancellationToken = default)
    {
        var writes = 0; var reads = 0; var entered = false;
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token, cancellationToken);
        try
        {
            await transaction.WaitAsync(linked.Token).ConfigureAwait(false);
            entered = true;
            for (var attempt = 0; attempt < MaximumAttempts; attempt++)
            {
                linked.Token.ThrowIfCancellationRequested();
                if (!IsCurrent()) { Dispose(); return new(false, writes, reads, "owner-or-endpoint-retired"); }
                // A fresh request/response for each attempt prevents a short reply
                // from inheriting bytes of an earlier response.
                var request = BuildReport(left, right, brightness, enabled);
                writes++;
                if (await transport.WriteAsync(request, linked.Token).ConfigureAwait(false) != request.Length)
                    return new(false, writes, reads, "short-write");
                linked.Token.ThrowIfCancellationRequested();
                if (!IsCurrent()) { Dispose(); return new(false, writes, reads, "owner-or-endpoint-retired"); }
                var response = new byte[65];
                using var responseDeadline = CancellationTokenSource.CreateLinkedTokenSource(linked.Token);
                responseDeadline.CancelAfter(ResponseTimeoutMilliseconds);
                int length;
                reads++;
                try { length = await transport.ReadAsync(response, responseDeadline.Token).ConfigureAwait(false); }
                catch (OperationCanceledException) when (responseDeadline.IsCancellationRequested && !linked.IsCancellationRequested) { continue; }
                linked.Token.ThrowIfCancellationRequested();
                if (!IsCurrent()) { Dispose(); return new(false, writes, reads, "owner-or-endpoint-retired"); }
                // Pinned hidapi returns 64 payload bytes with subcommand at index 3,
                // not a 65-byte report-ID-prefixed reply and not a color readback.
                if (length == 64 && response[3] == 0x09)
                    return new(true, writes, reads, "protocol-acknowledged-effect-unverified");
            }
            return new(false, writes, reads, "response-not-acknowledged");
        }
        catch (OperationCanceledException)
        { return new(false, writes, reads, Volatile.Read(ref closed) != 0 ? "session-closed" : "operation-cancelled"); }
        catch (Exception error) when (error is IOException or ObjectDisposedException)
        { Dispose(); return new(false, writes, reads, "transport-failed"); }
        finally { if (entered) transaction.Release(); }
    }
    internal static byte[] BuildReport(SuperJoyRgb left, SuperJoyRgb right, int brightness, bool enabled)
    {
        var report = new byte[65]; // report ID 0, then the pinned 64-byte payload
        report[3] = 0x21; report[4] = 0x09; report[24] = 0x20; report[32] = 0x01;
        var level = enabled ? Math.Clamp(brightness, 0, 100) : 0;
        Patch(8, right); Patch(12, left);
        ushort checksum = 0;
        for (var offset = 7; offset < report.Length; offset++) checksum += report[offset];
        report[1] = (byte)checksum; report[2] = (byte)(checksum >> 8);
        return report;
        void Patch(int offset, SuperJoyRgb color)
        {
            report[offset] = enabled ? (byte)0x01 : (byte)0xFF;
            report[offset + 1] = (byte)(color.R * level / 100);
            report[offset + 2] = (byte)(color.G * level / 100);
            report[offset + 3] = (byte)(color.B * level / 100);
        }
    }
    public void Dispose()
    {
        if (Interlocked.Exchange(ref closed, 1) != 0) return;
        // Do not wait for the transaction lock: stop must cancel a blocked read.
        lifetime.Cancel();
        try { transport.CancelPendingIo(); }
        finally { transport.Dispose(); }
    }
}
