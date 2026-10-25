// An intentionally untrusted protocol peer, compiled only into simulator scratch.
// It has no HID, HC, WMI, driver or hardware terminal.
namespace YeManCC.Lighting;
public static class LightingDeviceCore
{
    public const int ApiVersion = 1;
    public static bool Supports(string deviceClass, string action, string? level) =>
        Environment.GetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_UNSUPPORTED") != "true";
    public static string Execute(string deviceClass, string requestJson)
    {
        Environment.SetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_EXECUTED", "true");
        return Environment.GetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_RECEIPT") ?? "null";
    }
}
