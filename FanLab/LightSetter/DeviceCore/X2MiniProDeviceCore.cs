using System.Text.Json;

namespace YeManCC.Lighting;

// Separate additive API keeps the original X1Mini replay/terminal contract
// intact. This type is part of the same pinned YeManLightDeviceCore assembly.
public static class X2MiniProDeviceCore
{
    public const int ApiVersion = 1;
    public const string X2MiniPro = "HandheldCompanion.Devices.OneXPlayerX2MiniPro";
    public const string SourceCommit = "f2e58468d9917189d4be7ccfc242385ce2c3035b";
    public const string SourceSha256 = "7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67";

    public static bool Supports(string deviceClass, string action, string? level) =>
        deviceClass == X2MiniPro && (action is "brightness" or "off" ||
        action == "color" && (string.IsNullOrWhiteSpace(level) ||
        string.Equals(level, "solid", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(level, "breathing", StringComparison.OrdinalIgnoreCase)));

    public static string Execute(string deviceClass, string requestJson) =>
        throw new NotSupportedException("X2 Mini Pro requires ExecuteOwned and a current native OneX owner");

    // An additive API: the existing v1 X1Mini entry remains unchanged. The
    // setter supplies the native owner's exact endpoint, deadline/revocation
    // token and generation check; this component owns only the opened handle.
    public static string ExecuteOwned(string deviceClass, string requestJson, string interfacePath,
        CancellationToken cancellationToken, Action ensureOwnerCurrent)
    {
        if (deviceClass != X2MiniPro) throw new NotSupportedException("unreviewed owned lighting class");
        ArgumentNullException.ThrowIfNull(ensureOwnerCurrent);
        var request = JsonSerializer.Deserialize<LightingRequest>(requestJson,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new ArgumentException("empty lighting request");
        if (!Supports(deviceClass, request.Action, request.Level))
            throw new NotSupportedException("X2 Mini Pro lighting operation unavailable");
        cancellationToken.ThrowIfCancellationRequested();
        ensureOwnerCurrent();
        using var transport = WindowsHidTransport.OpenX2MiniPro(interfacePath, cancellationToken, ensureOwnerCurrent);
        var protocol = new PinnedX2MiniProProtocol(transport, cancellationToken, ensureOwnerCurrent);
        string note;
        string? firstFailure = null;
        if (request.Action == "off")
        {
            // Existing ExecuteLightingOff order, excluding the unsupported
            // status writer. A false brightness result must not skip black.
            var brightnessOk = protocol.SetLedBrightness(0);
            var black = new RgbColor(0, 0, 0);
            var colorOk = protocol.SetLedColor(black, black, LEDLevel.SolidColor);
            note = brightnessOk && colorOk ? "ok" : "off-partial";
            firstFailure = !brightnessOk ? "setLedBrightness-false" : !colorOk ? "setLedColor-false" : null;
        }
        else if (request.Action == "brightness")
        {
            note = protocol.SetLedBrightness(Math.Clamp(request.Brightness ?? 50, 0, 100))
                ? "ok" : "setLedBrightness-false";
        }
        else
        {
            var main = new RgbColor((byte)Clamp(request.R ?? 255), (byte)Clamp(request.G ?? 0), (byte)Clamp(request.B ?? 0));
            var second = new RgbColor((byte)Clamp(request.R2 ?? request.R ?? 255),
                (byte)Clamp(request.G2 ?? request.G ?? 0), (byte)Clamp(request.B2 ?? request.B ?? 0));
            var level = string.Equals(request.Level, "breathing", StringComparison.OrdinalIgnoreCase)
                ? LEDLevel.Breathing : LEDLevel.SolidColor;
            note = protocol.SetLedColor(main, second, level) ? "ok" : "setLedColor-false";
            if (request.Brightness is not null &&
                !protocol.SetLedBrightness(Math.Clamp(request.Brightness.Value, 0, 100)) && note == "ok")
                note = "setLedBrightness-false";
        }
        cancellationToken.ThrowIfCancellationRequested();
        ensureOwnerCurrent();
        transport.Dispose();
        return JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["deviceClass"] = deviceClass, ["action"] = request.Action,
            ["ready"] = true, ["opened"] = true, ["released"] = true,
            ["note"] = note, ["writeAttempts"] = protocol.WriteAttempts,
            ["firstFailure"] = note == "ok" ? null : firstFailure ?? note,
            ["physicalEffectVerified"] = false,
            ["sourceCommit"] = SourceCommit, ["sourceSha256"] = SourceSha256,
            ["sourceAdapter"] = "onex-x2mini-hid-v1", ["speedApplied"] = false,
            ["speedIgnored"] = true, ["fixedSpeed"] = true,
        });
    }

    private static int Clamp(int value) => Math.Clamp(value, 0, 255);
}
