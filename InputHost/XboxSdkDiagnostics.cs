using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

// GP-XBOX-7: observes the stock SDK only. No device writes, new polling thread,
// environment changes outside this Host, or changes to the SDK binary.
internal sealed class XboxSdkDiagnostics : IDisposable
{
    private const int SpanLimit = 128 * 1024;
    private const long LogLimit = 4 * 1024 * 1024;
    private static readonly object LogLock = new();
    private readonly string _directory, _host, _run, _command, _scope;
    private readonly ulong _epoch;
    private readonly long _started = Stopwatch.GetTimestamp();
    private readonly (string Path, long Length, long Created, string? Error)[] _before;
    private string _endpoint = "pnp-not-observed";
    private bool _disposed;

    internal static XboxSdkDiagnostics? TryBegin(string persona, string host, string run,
        ulong epoch, string command, string coreSha, string coreMvid)
    {
        if (persona != "elite") return null;
        try { return new XboxSdkDiagnostics(host, run, epoch, command, coreSha, coreMvid); }
        catch { return null; } // Diagnostic failure must not reject the controller.
    }

    private XboxSdkDiagnostics(string host, string run, ulong epoch, string command,
        string coreSha, string coreMvid)
    {
        _directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
        _host = host; _run = run; _epoch = epoch; _command = command; _scope = Guid.NewGuid().ToString("N");
        // Set before HMContext's first construction (its prewarm can initialize
        // DeviceOrchestrator). Preserve TEMP/TMP and all timeout/driver settings.
        Environment.SetEnvironmentVariable("HIDMAESTRO_DIAG", "1", EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("HIDMAESTRO_TIMING", "1", EnvironmentVariableTarget.Process);
        var sdkDir = Path.Combine(Path.GetTempPath(), "HIDMaestro");
        _before = new[] { Stamp(Path.Combine(sdkDir, "setup_timing.log")), Stamp(Path.Combine(sdkDir, "teardown_diag.log")) };
        string timeoutScaleRaw = Environment.GetEnvironmentVariable("HIDMAESTRO_TIMEOUT_SCALE") ?? "default";
        Write("scope-begin", new {
            coreSha, coreMvid, sdkDirectory = sdkDir,
            flagsRequested = new[] { "HIDMAESTRO_DIAG=1", "HIDMAESTRO_TIMING=1" },
            timeoutScale = timeoutScaleRaw[..Math.Min(32, timeoutScaleRaw.Length)],
            sharedSdkFile = true, producerPidProven = false,
            note = "Scope metadata identifies the reader; raw SDK files lack PID and can contain other writers. Use profile, time and exact root to correlate."
        });
    }

    internal void SetEndpoint(string detail) => _endpoint = detail;

    private static (string Path, long Length, long Created, string? Error) Stamp(string path)
    {
        try { var f = new FileInfo(path); return (path, f.Exists ? f.Length : 0, f.Exists ? f.CreationTimeUtc.Ticks : 0, null); }
        catch (Exception ex) { return (path, 0, 0, ex.GetType().Name); }
    }

    internal static object ReadSpan(string path, long beforeLength, long beforeCreated, int limit = SpanLimit)
    {
        try
        {
            var stamp = Stamp(path);
            if (!File.Exists(path)) return new { source = path, status = "missing", raw = "" };
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            long end = stream.Length;
            bool reset = end < beforeLength || (beforeCreated != 0 && stamp.Created != beforeCreated);
            long start = reset ? 0 : Math.Min(beforeLength, end);
            long omitted = Math.Max(0, end - start - limit); start += omitted;
            int count = checked((int)Math.Min(limit, end - start));
            var bytes = new byte[count]; stream.Position = start;
            int read = 0;
            while (read < count) { int n = stream.Read(bytes, read, count - read); if (n == 0) break; read += n; }
            return new { source = path, status = read == 0 ? "no-new-bytes" : "captured", from = start,
                to = start + read, snapshotEnd = end, reset, omittedBytes = omitted, truncated = omitted > 0,
                firstLineMayBePartial = omitted > 0 || start > 0, sharedSdkFile = true, producerPidProven = false,
                raw = Encoding.UTF8.GetString(bytes, 0, read) };
        }
        catch (Exception ex) { return new { source = path, status = "read-failed", error = ex.GetType().Name, raw = "" }; }
    }

    internal static string[] TargetIds(string detail)
    {
        var result = new List<string>();
        foreach (string key in new[] { ";pnp-primary-root-ids=", ";pnp-hid-ids=" })
        {
            int at = detail.IndexOf(key, StringComparison.Ordinal);
            if (at < 0) continue;
            int start = at + key.Length, end = detail.IndexOf(';', start);
            var field = detail[start..(end < 0 ? detail.Length : end)];
            foreach (Match m in Regex.Matches(field, @"(?:^|,)([^\[]+)\["))
            {
                string id = m.Groups[1].Value;
                if (id.Length <= 512 && (id.StartsWith(@"SWD\HIDMAESTRO_VID_045E_PID_0B00&IG_", StringComparison.OrdinalIgnoreCase) ||
                    id.StartsWith(@"HID\HIDMAESTRO_VID_045E_PID_0B00&IG_", StringComparison.OrdinalIgnoreCase)) &&
                    id.All(c => char.IsAsciiLetterOrDigit(c) || c is '\\' or '_' or '&') && !result.Contains(id, StringComparer.OrdinalIgnoreCase))
                    result.Add(id);
                if (result.Count == 8) return result.ToArray();
            }
        }
        return result.ToArray();
    }

    private static object DeviceSnapshot(string id)
    {
        try
        {
            uint locate = CM_Locate_DevNodeW(out var node, id, 0), state = 0, problem = 0;
            uint statusResult = locate == 0 ? CM_Get_DevNode_Status(out state, out problem, node, 0) : locate;
            using var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\" + id, false);
            string driver = key?.GetValue("Driver") as string ?? "";
            using var driverKey = Regex.IsMatch(driver, @"^\{[0-9A-Fa-f-]{36}\}\\[0-9]{4,10}$")
                ? Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\" + driver, false) : null;
            var guid = new Guid("ec87f1e3-c13b-4100-b5f7-8b84d54260cb");
            uint chars = 0, interfaceResult = CM_Get_Device_Interface_List_SizeW(out chars, ref guid, id, 0);
            string[] interfaces = Array.Empty<string>();
            if (interfaceResult == 0 && chars > 0 && chars <= 8192)
            {
                var buffer = new char[chars];
                interfaceResult = CM_Get_Device_Interface_ListW(ref guid, id, buffer, chars, 0);
                if (interfaceResult == 0) interfaces = new string(buffer).Split('\0', StringSplitOptions.RemoveEmptyEntries);
            }
            return new { instanceId = id, locate, statusResult, state, started = statusResult == 0 && (state & 8) != 0, problem,
                service = key?.GetValue("Service"), upperFilters = key?.GetValue("UpperFilters"), lowerFilters = key?.GetValue("LowerFilters"),
                driverKey = driver, inf = driverKey?.GetValue("InfPath"), section = driverKey?.GetValue("InfSection"),
                driverVersion = driverKey?.GetValue("DriverVersion"), provider = driverKey?.GetValue("ProviderName"),
                matchingId = driverKey?.GetValue("MatchingDeviceId"), interfaceResult, interfaceChars = chars, xusbInterfaces = interfaces,
                filterMeaning = "registry-configured; not proof of the loaded driver stack" };
        }
        catch (Exception ex) { return new { instanceId = id, error = ex.GetType().Name }; }
    }

    private void Write(string phase, object data)
    {
        try
        {
            string line = JsonSerializer.Serialize(new { t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                kind = "xbox-sdk-evidence", phase, scopeId = _scope, scopeHostPid = Environment.ProcessId,
                hostInstanceId = _host, runId = _run, epoch = _epoch, command = _command, persona = "elite", data });
            lock (LogLock)
            {
                Directory.CreateDirectory(_directory);
                var path = Path.Combine(_directory, "input-host-xbox-sdk.log");
                if (File.Exists(path) && new FileInfo(path).Length + Encoding.UTF8.GetByteCount(line) > LogLimit)
                    File.Move(path, path + ".1", true);
                File.AppendAllText(path, line + "\n", new UTF8Encoding(false));
            }
        }
        catch { } // Logging cannot change protocol success/failure.
    }

    public void Dispose()
    {
        if (_disposed) return; _disposed = true;
        try
        {
            foreach (var before in _before) Write("sdk-span", ReadSpan(before.Path, before.Length, before.Created));
            Write("target-snapshot", new { devices = TargetIds(_endpoint).Select(DeviceSnapshot).ToArray(),
                pnpDetail = _endpoint, elapsedMs = Stopwatch.GetElapsedTime(_started).TotalMilliseconds });
        }
        catch (Exception ex) { Write("capture-failed", new { error = ex.GetType().Name }); }
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Locate_DevNodeW(out uint node, string id, uint flags);
    [DllImport("cfgmgr32.dll")] private static extern uint CM_Get_DevNode_Status(out uint status, out uint problem, uint node, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Get_Device_Interface_List_SizeW(out uint chars, ref Guid guid, string id, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Get_Device_Interface_ListW(ref Guid guid, string id, [Out] char[] buffer, uint chars, uint flags);
}
