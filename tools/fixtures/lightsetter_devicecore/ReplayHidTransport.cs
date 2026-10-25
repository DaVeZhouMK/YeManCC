using System.Text.Json;

namespace YeManCC.Lighting;

// Compile-time terminal injection for this offline fixture ONLY. This file is
// never included by the production core project and cannot enumerate real HID.
internal sealed class WindowsHidTransport : ILightingHidTransport
{
    private readonly string mode = Environment.GetEnvironmentVariable("YMCC_LIGHT_REPLAY_MODE") ?? "normal";
    private int writes;
    private bool disposed;
    internal static void Log(string eventName, string? hex = null) => File.AppendAllText(
        Environment.GetEnvironmentVariable("YMCC_LIGHT_REPLAY_LOG") ?? throw new Exception("fixture log required"),
        JsonSerializer.Serialize(new { eventName, hex, loaded = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetName().Name).ToArray() }) + "\n");
    internal static ILightingHidTransport OpenX1Mini()
    {
        Log("open");
        if (Environment.GetEnvironmentVariable("YMCC_LIGHT_REPLAY_MODE") == "no-device")
            throw new IOException("fixture HID endpoint missing");
        return new WindowsHidTransport();
    }
    public bool IsConnected => !disposed && mode != "disconnected";
    public bool Write(byte[] report)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        writes++;
        Log("write", Convert.ToHexString(report));
        if (mode == "throw:" + writes) throw new IOException("fixture write failure");
        return mode != "false:" + writes;
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        Log("release");
        if (mode == "release-throw") throw new IOException("fixture release failure");
    }
}
