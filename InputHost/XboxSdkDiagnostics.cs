using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
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
    internal static readonly object LogLock = new(); // Shared bounded SDK/USBIP evidence sink.
    private readonly string _directory, _host, _run, _command, _scope, _persona;
    private readonly ulong _epoch;
    private readonly long _started = Stopwatch.GetTimestamp();
    private readonly (string Path, long Length, long Created, string? Error)[] _before;
    private string _endpoint = "pnp-not-observed";
    private bool _disposed;

    internal static XboxSdkDiagnostics? TryBegin(string persona, string host, string run,
        ulong epoch, string command, string coreSha, string coreMvid)
    {
        if (persona is not ("elite" or "dualsense-edge")) return null;
        try { return new XboxSdkDiagnostics(persona, host, run, epoch, command, coreSha, coreMvid); }
        catch { return null; } // Diagnostic failure must not reject the controller.
    }

    private XboxSdkDiagnostics(string persona, string host, string run, ulong epoch, string command,
        string coreSha, string coreMvid)
    {
        _directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
        _persona = persona; _host = host; _run = run; _epoch = epoch; _command = command; _scope = Guid.NewGuid().ToString("N");
        // Set before HMContext's first construction (its prewarm can initialize
        // DeviceOrchestrator). Preserve TEMP/TMP and all timeout/driver settings.
        Environment.SetEnvironmentVariable("HIDMAESTRO_DIAG", "1", EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("HIDMAESTRO_TIMING", "1", EnvironmentVariableTarget.Process);
        var sdkDir = Path.Combine(Path.GetTempPath(), "HIDMaestro");
        _before = new[] { Stamp(Path.Combine(sdkDir, "setup_timing.log")), Stamp(Path.Combine(sdkDir, "teardown_diag.log")) };
        string timeoutScaleRaw = Environment.GetEnvironmentVariable("HIDMAESTRO_TIMEOUT_SCALE") ?? "default";
        Write("scope-begin", new {
            coreSha, coreMvid, sdkDirectory = sdkDir,
            hostBusinessAssembly = typeof(XboxSdkDiagnostics).Assembly.GetName().Name,
            hostBusinessMvid = typeof(XboxSdkDiagnostics).Assembly.ManifestModule.ModuleVersionId,
            hostBusinessLocation = typeof(XboxSdkDiagnostics).Assembly.Location,
            hostIdentityScope = "this-loaded-business-assembly;not-export-manifest-fan-host",
            flagsRequested = new[] { "HIDMAESTRO_DIAG=1", "HIDMAESTRO_TIMING=1" },
            timeoutScale = timeoutScaleRaw[..Math.Min(32, timeoutScaleRaw.Length)],
            sharedSdkFile = true, producerPidProven = false,
            note = "Scope metadata identifies the reader; raw SDK files lack PID and can contain other writers. Use profile, time and exact root to correlate."
        });
    }

    internal void SetEndpoint(string detail)
    {
        _endpoint = detail;
        // Existing prepare calls this before rejected-target cleanup. Preserve
        // the exact observed root's problem/driver facts before it disappears.
        if (_persona == "dualsense-edge" && _command is ("PREPARE_TARGET" or "RECREATE_TARGET"))
        {
            try { Write("target-snapshot-before-cleanup", new {
                devices = TargetIds(_endpoint, _persona).Select(DeviceSnapshot).ToArray(),
                stagingPackage = ReadSdkStagingPackage(),
                pnpDetail = _endpoint, elapsedMs = Stopwatch.GetElapsedTime(_started).TotalMilliseconds,
                scope = "read-only-observed-target-before-prepare-result;not-ownership-or-game-input-proof"
            }); } catch { /* Diagnostic failure cannot change the prepare result. */ }
        }
    }

    // Exact package path selected by the already initialized SDK. BuildDir is
    // intentionally not read: that getter extracts files. No deploy/sign/repair
    // is permitted in diagnostics, and no whole-machine package census runs here.
    private static object ReadSdkStagingPackage()
    {
        try
        {
            var sdk = AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(a => a.GetName().Name == "HIDMaestro.Core");
            if (sdk is null) return new { status = "sdk-not-loaded" };
            var builder = sdk.GetType("HIDMaestro.Internal.DriverBuilder", false);
            var stage = builder?.GetProperty("StagingDir", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static)?.GetValue(null) as string;
            var markers = new List<object>();
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                try { using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
                    using var key = hive.OpenSubKey(@"SOFTWARE\HIDMaestro", false);
                    markers.Add(new { view = view.ToString(), installedManifestSha256 = key?.GetValue("InstalledManifestSha256") as string });
                } catch (Exception ex) { markers.Add(new { view = view.ToString(), error = ex.GetType().Name }); }
            }
            return new { status = "observed", sdkMvid = sdk.ManifestModule.ModuleVersionId,
                stage, installedManifestMarkers = markers, stagingLayout = ReadStagingLayout(stage, sdk.ManifestModule.ModuleVersionId),
                scope = "read-only-SDK-selected-staging;file-existence-not-signature-validity-or-installed-binding-proof" };
        }
        catch (Exception ex) { return new { status = "unavailable", error = ex.GetType().Name }; }
    }

    // v2 keeps embedded raw bytes in "driver" and signs a sibling "package".
    // Derive that one candidate only for the exact audited SDK. Reading a raw
    // payload without CAT is expected and must never be logged as a bad package.
    // This does not establish that a deploy selected, trusted or bound the files.
    internal static object ReadStagingLayout(string? stage, Guid sdkMvid)
    {
        if (string.IsNullOrEmpty(stage)) return new { status = "staging-not-selected", writesIssued = false };
        try
        {
            string raw = Path.TrimEndingDirectorySeparator(Path.GetFullPath(stage));
            if (sdkMvid != new Guid("86aed9a3-c024-4596-b02c-650082ffb852") ||
                !Path.GetFileName(raw).Equals("driver", StringComparison.OrdinalIgnoreCase))
                return new { status = "layout-unproven", stage, rawPayload = ReadStagingPackage(stage),
                    catalogExpectation = "unknown-layout;do-not-infer-deploy-failure", writesIssued = false };
            string package = Path.Combine(Directory.GetParent(raw)!.FullName, "package");
            return new { status = "observed", layout = "v2-separated-driver-package", rawDirectory = raw,
                signedPackageCandidateDirectory = package, rawPayload = ReadStagingPackage(raw),
                signedPackageCandidate = ReadStagingPackage(package),
                rawCatalogExpectation = "not-required-in-raw-driver-directory",
                catalogExpectation = "signed-package-candidate-only;existence-not-trust-or-binding-proof",
                writesIssued = false };
        }
        catch (Exception ex) { return new { status = "unavailable", error = ex.GetType().Name, writesIssued = false }; }
    }

    internal static object ReadStagingPackage(string? stage)
    {
        if (string.IsNullOrEmpty(stage)) return new { status = "staging-not-selected" };
        var files = new List<object>();
        foreach (var name in new[] { "hidmaestro.inf", "hidmaestro_xusb.inf" })
        {
            try
            {
                var path = Path.Combine(stage, name);
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                if (stream.Length > 128 * 1024) { files.Add(new { name, status = "inf-over-budget", bytes = stream.Length }); continue; }
                // Bound actual reads even if another process grows the file.
                var bytes = new byte[(int)stream.Length]; int copied = 0;
                while (copied < bytes.Length) { int count = stream.Read(bytes, copied, bytes.Length - copied); if (count == 0) break; copied += count; }
                using var memory = new MemoryStream(bytes, 0, copied);
                using var reader = new StreamReader(memory, Encoding.UTF8, true);
                var text = reader.ReadToEnd(); var catalogs = new List<object>(); bool version = false;
                foreach (var raw in text.Split('\n'))
                {
                    var line = raw.Split(';', 2)[0].Trim();
                    if (line.StartsWith('[')) { version = line.Equals("[Version]", StringComparison.OrdinalIgnoreCase); continue; }
                    if (!version) continue;
                    var equals = line.IndexOf('='); if (equals < 0) continue;
                    var key = line[..equals].Trim();
                    if (!key.Equals("CatalogFile", StringComparison.OrdinalIgnoreCase) && !key.StartsWith("CatalogFile.", StringComparison.OrdinalIgnoreCase)) continue;
                    if (catalogs.Count >= 4) break;
                    var catalog = line[(equals + 1)..].Trim().Trim('"');
                    if (catalog.Length == 0 || catalog.Length > 128 || catalog.IndexOfAny(new[] { '/', '\\', ':' }) >= 0 || Path.GetFileName(catalog) != catalog || !catalog.EndsWith(".cat", StringComparison.OrdinalIgnoreCase))
                    { catalogs.Add(new { key, status = "catalog-name-rejected" }); continue; }
                    var info = new FileInfo(Path.Combine(stage, catalog));
                    catalogs.Add(new { key, name = catalog, exists = info.Exists, bytes = info.Exists ? info.Length : (long?)null });
                }
                files.Add(new { name, status = "observed", bytes = stream.Length, catalogs,
                    contentScope = "bounded-current-INF-Version-CatalogFile-entries;existence-not-trust-verification" });
            }
            catch (FileNotFoundException) { files.Add(new { name, status = "inf-missing" }); }
            catch (DirectoryNotFoundException) { files.Add(new { name, status = "staging-directory-missing" }); }
            catch (Exception ex) { files.Add(new { name, status = "unavailable", error = ex.GetType().Name }); }
        }
        return new { status = "observed", stage, files, nativePayload = ReadNativeStagingFiles(stage),
            writesIssued = false, maximumInfBytes = 128 * 1024, maximumCatalogsPerInf = 4 };
    }

    private static object[] ReadNativeStagingFiles(string stage)
    {
        var rows = new List<object>();
        foreach (var name in new[] { "HIDMaestro.dll", "HMXInput.dll" })
        {
            try
            {
                using var stream = new FileStream(Path.Combine(stage, name), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                var length = stream.Length;
                if (length > 2 * 1024 * 1024) { rows.Add(new { name, status = "native-over-budget", bytes = length }); continue; }
                var bytes = new byte[(int)length]; int copied = 0;
                while (copied < bytes.Length) { int read = stream.Read(bytes, copied, bytes.Length - copied); if (read == 0) break; copied += read; }
                using var memory = new MemoryStream(bytes, 0, copied);
                using var pe = new PEReader(memory);
                var security = pe.PEHeaders.PEHeader?.CertificateTableDirectory;
                rows.Add(new { name, status = "observed", bytesBefore = length, bytesAfter = stream.Length,
                    copied, sha256OfCopiedBytes = Convert.ToHexString(SHA256.HashData(bytes.AsSpan(0, copied))).ToLowerInvariant(),
                    certificateTableBytes = security?.Size, certificateTableOffset = security?.RelativeVirtualAddress,
                    scope = "bounded-current-staging-bytes;not-atomic-with-create;certificate-table-not-signature-validity-or-loaded-driver-proof" });
            }
            catch (FileNotFoundException) { rows.Add(new { name, status = "native-file-missing" }); }
            catch (DirectoryNotFoundException) { rows.Add(new { name, status = "staging-directory-missing" }); }
            catch (Exception ex) { rows.Add(new { name, status = "unavailable", error = ex.GetType().Name }); }
        }
        return rows.ToArray();
    }

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

    internal static string[] TargetIds(string detail, string persona = "elite")
    {
        var result = new List<string>();
        foreach (string key in new[] { ";pnp-primary-root-ids=", ";pnp-hid-ids=" })
        {
            int at = detail.IndexOf(key, StringComparison.Ordinal);
            if (at < 0) continue;
            int start = at + key.Length, end = detail.IndexOf(';', start);
            var field = detail[start..(end < 0 ? detail.Length : end)];
            foreach (Match m in Regex.Matches(field, @"(?:^|,)([^\[]+)\[([^\]]*)\]"))
            {
                string id = m.Groups[1].Value;
                bool target = persona == "elite" ?
                    id.StartsWith(@"SWD\HIDMAESTRO_VID_045E_PID_0B00&IG_", StringComparison.OrdinalIgnoreCase) ||
                    id.StartsWith(@"HID\HIDMAESTRO_VID_045E_PID_0B00&IG_", StringComparison.OrdinalIgnoreCase) :
                    persona == "dualsense-edge" && Regex.IsMatch(id, @"^ROOT\\HIDCLASS\\HM_[0-9a-fA-F]{4}$", RegexOptions.IgnoreCase) &&
                    m.Groups[2].Value.Split('|').Contains(@"root\HIDMaestro", StringComparer.OrdinalIgnoreCase) &&
                    m.Groups[2].Value.Split('|').Contains(@"root\VID_054C&PID_0DF2", StringComparer.OrdinalIgnoreCase);
                if (id.Length <= 512 && target &&
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
                hostInstanceId = _host, runId = _run, epoch = _epoch, command = _command, persona = _persona, data });
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
            Write("target-snapshot", new { devices = TargetIds(_endpoint, _persona).Select(DeviceSnapshot).ToArray(),
                pnpDetail = _endpoint, elapsedMs = Stopwatch.GetElapsedTime(_started).TotalMilliseconds });
        }
        catch (Exception ex) { Write("capture-failed", new { error = ex.GetType().Name }); }
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Locate_DevNodeW(out uint node, string id, uint flags);
    [DllImport("cfgmgr32.dll")] private static extern uint CM_Get_DevNode_Status(out uint status, out uint problem, uint node, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Get_Device_Interface_List_SizeW(out uint chars, ref Guid guid, string id, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)] private static extern uint CM_Get_Device_Interface_ListW(ref Guid guid, string id, [Out] char[] buffer, uint chars, uint flags);
}
