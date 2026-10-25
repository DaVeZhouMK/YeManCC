using System.Text.Json;

namespace YeManCC.Lighting;

// The only injected terminal. A true result means the entire output report was
// accepted by the transport, never that the physical LEDs were observed.
public interface ILightingHidTransport : IDisposable
{
    bool IsConnected { get; }
    bool Write(byte[] report);
}

public readonly record struct RgbColor(byte R, byte G, byte B);

[Flags]
internal enum LEDLevel { SolidColor = 1, Rainbow = 4, LEDPreset = 128 }

// No HC types cross this ABI. The consumer checks assembly identity, release
// SHA256 and ApiVersion before calling Supports, then Execute.
public static class LightingDeviceCore
{
    public const int ApiVersion = 1;
    public const string X1Mini = "HandheldCompanion.Devices.OneXPlayerX1Mini";

    public static bool Supports(string deviceClass, string action, string? level) =>
        deviceClass == X1Mini && (action == "brightness" ||
        (action == "color" && (string.IsNullOrWhiteSpace(level) ||
        string.Equals(level, "solid", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(level, "rainbow", StringComparison.OrdinalIgnoreCase))));

    public static string Execute(string deviceClass, string requestJson)
    {
        var request = JsonSerializer.Deserialize<LightingRequest>(requestJson,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new ArgumentException("empty lighting request");
        // Reject before opening any handle. No guessed status/off/preset alias.
        if (!Supports(deviceClass, request.Action, request.Level))
            throw new NotSupportedException("device-core capability unavailable");
        using var transport = WindowsHidTransport.OpenX1Mini();
        if (!transport.IsConnected)
            throw new IOException("device-core HID endpoint disconnected");
        var protocol = new PinnedX1MiniProtocol(transport);
        var writes = 0;
        string note;
        if (request.Action == "brightness")
        {
            writes++;
            note = protocol.SetLedBrightness(Clamp(request.Brightness ?? 50)) ? "ok" : "setLedBrightness-false";
        }
        else
        {
            var main = new RgbColor((byte)Clamp(request.R ?? 255), (byte)Clamp(request.G ?? 0), (byte)Clamp(request.B ?? 0));
            var second = new RgbColor((byte)Clamp(request.R2 ?? request.R ?? 255),
                (byte)Clamp(request.G2 ?? request.G ?? 0), (byte)Clamp(request.B2 ?? request.B ?? 0));
            var level = string.Equals(request.Level, "rainbow", StringComparison.OrdinalIgnoreCase)
                ? LEDLevel.Rainbow : LEDLevel.SolidColor;
            writes++;
            note = protocol.SetLedColor(main, second, level, request.Speed ?? 100) ? "ok" : "setLedColor-false";
            // Preserve the existing setter's order and first failure reporting.
            if (request.Brightness is not null)
            {
                writes++;
                if (!protocol.SetLedBrightness(Clamp(request.Brightness.Value)) && note == "ok")
                    note = "setLedBrightness-false";
            }
        }
        // Dispose before returning a success receipt, so release errors cannot
        // be hidden behind ok. Dispose is idempotent for the using cleanup.
        transport.Dispose();
        return JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["deviceClass"] = deviceClass, ["action"] = request.Action,
            ["ready"] = true, ["opened"] = true, ["released"] = true,
            ["note"] = note, ["writeAttempts"] = writes,
            ["physicalEffectVerified"] = false,
        });
    }

    private static int Clamp(int value) => Math.Clamp(value, 0, 255);
}

internal sealed record LightingRequest(string Action, string? Level = null,
    int? R = null, int? G = null, int? B = null, int? R2 = null, int? G2 = null,
    int? B2 = null, int? Brightness = null, int? Speed = null);
