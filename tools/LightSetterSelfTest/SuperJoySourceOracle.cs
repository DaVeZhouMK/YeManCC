// The exact fixed upstream class is compiled into this existing fixture.
// All of its external I/O terminals throw, so this oracle can only build bytes
// or evaluate the descriptor predicate; it cannot open or write any endpoint.
namespace HidLibrary
{
    public sealed class SuperJoyFixtureCapabilities
    {
        public short UsagePage { get; set; } = unchecked((short)0xFF00);
        public short Usage { get; set; } = 1;
        public int InputReportByteLength { get; set; } = 65;
        public int OutputReportByteLength { get; set; } = 65;
    }
    public sealed partial class HidDevice
    {
        public SuperJoyFixtureCapabilities Capabilities { get; } = new();
        public string DevicePath => "source-oracle-forbidden-endpoint";
    }
}
namespace hidapi
{
    public sealed class HidDevice : IDisposable
    {
        public HidDevice(ushort vendor, ushort product, int length, int index) =>
            throw new InvalidOperationException("SOURCE_ORACLE_NATIVE_CONSTRUCTION_FORBIDDEN");
        public bool IsDeviceValid => throw new InvalidOperationException("SOURCE_ORACLE_IO_FORBIDDEN");
        public bool OpenDevice(string path) => throw new InvalidOperationException("SOURCE_ORACLE_IO_FORBIDDEN");
        public void Write(byte[] packet) => throw new InvalidOperationException("SOURCE_ORACLE_IO_FORBIDDEN");
        public int Read(byte[] response, int timeout) => throw new InvalidOperationException("SOURCE_ORACLE_IO_FORBIDDEN");
        public void Dispose() { }
    }
}
namespace HandheldCompanion.Shared
{
    public static class LogManager
    {
        public static void LogWarning(string message, params object[] args) { }
        public static void LogInformation(string message, params object[] args) { }
        public static void LogError(string message, params object[] args) { }
    }
}
