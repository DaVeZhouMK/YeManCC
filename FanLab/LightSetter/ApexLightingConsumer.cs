using System.IO;

// Exact APEX HID-v2 consumer. HueSync7176803c binds APEX to the F1 RGB
// endpoint; fixed HC f2e58468 supplies Windows report-ID framing. No HC
// factory, controller initialization, EC, alternate route or retry is used.
internal sealed record ApexLightingRequest(string Action, string Level, byte R, byte G, byte B, int? Brightness)
{
    internal static ApexLightingRequest Create(string action, string? level, int? r, int? g, int? b, int? brightness)
    {
        if (action is not ("color" or "brightness" or "off"))
            throw new NotSupportedException("APEX_LIGHTING_ACTION_UNAVAILABLE");
        if (action == "off") return new(action, "solid", 0, 0, 0, 0);
        var effect = string.IsNullOrWhiteSpace(level) ? "solid" : level.ToLowerInvariant();
        if (effect is not ("solid" or "rainbow")) throw new NotSupportedException("APEX_LIGHTING_EFFECT_UNAVAILABLE");
        if (brightness is < 0 or > 100) throw new ArgumentOutOfRangeException(nameof(brightness));
        if (action == "brightness" && brightness is null) throw new ArgumentException("APEX_BRIGHTNESS_REQUIRED");
        if (action == "color" && effect == "solid" && (r is null || g is null || b is null))
            throw new ArgumentException("APEX_EXPLICIT_COLOR_REQUIRED");
        if (r is < 0 or > 255 || g is < 0 or > 255 || b is < 0 or > 255)
            throw new ArgumentOutOfRangeException(nameof(r));
        return new(action, effect, (byte)(r ?? 0), (byte)(g ?? 0), (byte)(b ?? 0), action == "off" ? 0 : brightness);
    }
}

internal interface IApexLightingTransport : IDisposable
{
    string DevicePath { get; }
    bool IsConnected { get; }
    ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report, CancellationToken cancellationToken);
    void CancelPendingIo();
}
internal readonly record struct ApexLightingResult(bool Completed, int Writes, bool Released, string Reason);

internal static class ApexLightingConsumer
{
    internal const string Model = "HandheldCompanion.Devices.OneXPlayerApex";
    internal const ushort VendorId = 0x1A2C, ProductId = 0xB001, UsagePage = 0xFF01, Usage = 1;
    internal const int ReportLength = 65, CommandDelayMs = 300;

    internal static bool MatchesOwnerIdentity(SuperJoyIdentity identity) => identity.ModelClass == Model &&
        !string.IsNullOrWhiteSpace(identity.DevicePath) && identity.Generation > 0 &&
        identity.VendorId == VendorId && identity.ProductId == ProductId &&
        identity.UsagePage == UsagePage && identity.Usage == Usage && identity.OutputReportLength == ReportLength;

    internal static SuperJoyIdentity OwnerIdentity(string path, long generation) =>
        new(Model, path, generation, VendorId, ProductId, UsagePage, Usage, 0, ReportLength);

    // Linux hidraw has no report-ID byte. Windows writes reportID0 plus the
    // entire64-byte source command, matching IDevice.WithReportID and V2 helpers.
    internal static byte[] BuildBrightness(int brightness)
    {
        if (brightness is < 0 or > 100) throw new ArgumentOutOfRangeException(nameof(brightness));
        var value = (int)Math.Round(brightness / 20.0, MidpointRounding.ToEven);
        var level = value <= 1 ? 1 : value <= 3 ? 3 : 4;
        var report = Empty();
        report[3] = 0xFD; report[4] = brightness > 0 ? (byte)1 : (byte)0;
        report[5] = 0x05; report[6] = (byte)level;
        return report;
    }
    internal static byte[] BuildSolid(byte r, byte g, byte b)
    {
        var report = Empty(); report[3] = 0xFE;
        for (var i = 0; i < 20; i++) { report[4 + i * 3] = r; report[5 + i * 3] = g; report[6 + i * 3] = b; }
        return report;
    }
    internal static byte[] BuildRainbow() { var report = Empty(); report[3] = 0x03; return report; }
    private static byte[] Empty() { var report = new byte[ReportLength]; report[1] = 0x07; report[2] = 0xFF; return report; }

    internal static ApexLightingResult Apply(SuperJoyIdentity identity, ApexLightingRequest request,
        Func<bool> ownerIsCurrent, CancellationToken stop, Func<IApexLightingTransport> open,
        Action<int, CancellationToken>? wait = null)
    {
        ArgumentNullException.ThrowIfNull(ownerIsCurrent); ArgumentNullException.ThrowIfNull(open);
        if (!MatchesOwnerIdentity(identity)) throw new ArgumentException("APEX_OWNER_IDENTITY_MISMATCH");
        if (request != ApexLightingRequest.Create(request.Action, request.Level, request.R, request.G, request.B, request.Brightness))
            throw new ArgumentException("APEX_REQUEST_NOT_CANONICAL");
        wait ??= static (milliseconds, token) => Task.Delay(milliseconds, token).GetAwaiter().GetResult();
        IApexLightingTransport? transport = null;
        var writes = 0; var released = false; var reason = "not-started"; var completed = false;
        try
        {
            Current();
            transport = open();
            Current();
            if (!string.Equals(transport.DevicePath, identity.DevicePath, StringComparison.OrdinalIgnoreCase))
                throw new IOException("APEX_OWNER_ENDPOINT_CHANGED");
            if (!transport.IsConnected) throw new IOException("APEX_ENDPOINT_DISCONNECTED");
            // Fresh explicit state on every request. No cached success or replay.
            if (request.Action == "off" || request.Action == "brightness")
                Write(BuildBrightness(request.Brightness!.Value));
            else
            {
                if (request.Brightness is not null) Write(BuildBrightness(request.Brightness.Value));
                Write(request.Level == "rainbow" ? BuildRainbow() : BuildSolid(request.R, request.G, request.B));
            }
            Current();
            // A close failure cannot be hidden behind a success receipt.
            transport.Dispose(); released = true; transport = null;
            Current(); completed = true; reason = "reports-completed-effect-unverified";
        }
        catch (OperationCanceledException) { reason = "owner-or-request-cancelled"; }
        catch (IOException error) { reason = error.Message; }
        catch (ObjectDisposedException) { reason = "transport-disposed"; }
        finally
        {
            if (transport is not null)
            {
                try { transport.CancelPendingIo(); }
                catch (Exception error) when (error is IOException or ObjectDisposedException) { reason = "cancel-failed"; completed = false; }
                try { transport.Dispose(); released = true; }
                catch (Exception error) when (error is IOException or ObjectDisposedException) { reason = "release-failed"; completed = false; }
            }
        }
        return new(completed, writes, released, reason);

        void Current()
        {
            stop.ThrowIfCancellationRequested();
            if (!ownerIsCurrent()) throw new OperationCanceledException("APEX_NATIVE_OWNER_RETIRED", stop);
        }
        void Write(byte[] report)
        {
            // Pinned HueSync requires >=200ms after brightness/enable and
            // inserts300ms before color/mode; its default50ms is insufficient.
            if (writes > 0) { wait(CommandDelayMs, stop); Current(); }
            Current();
            if (!transport!.IsConnected) throw new IOException("APEX_ENDPOINT_DISCONNECTED");
            writes++;
            // Completion of the full report is required; an uncertain first
            // write is never replayed or sent to an alternate endpoint.
            if (transport.WriteAsync(report, stop).AsTask().GetAwaiter().GetResult() != report.Length)
                throw new IOException("APEX_SHORT_WRITE");
            Current();
        }
    }
}
