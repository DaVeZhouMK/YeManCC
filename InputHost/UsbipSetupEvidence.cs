using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace YeManInputHost.Diagnostics;

// Observations only: never calls the SDK, changes environment/policy, or writes to devices.
internal sealed class UsbipSetupEvidence : IDisposable
{
    private const string Service = @"SYSTEM\CurrentControlSet\Services\usbip2_ude";
    private const string EnumRoot = @"SYSTEM\CurrentControlSet\Enum\";
    private const string ClassRoot = @"SYSTEM\CurrentControlSet\Control\Class\";
    private const int LineLimit = 128, ProgressLimit = 64, StageLimit = 22, TextLimit = 1024, ImageLimit = 2 * 1024 * 1024;
    private const long LogLimit = 4 * 1024 * 1024;
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private static readonly JsonSerializerOptions Json = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };
    private readonly object _gate = new();
    private readonly string _directory, _host, _run, _command, _scope = Guid.NewGuid().ToString("N");
    private readonly ulong _epoch;
    private readonly long _started = Stopwatch.GetTimestamp();
    private int _attempted, _written, _progress, _stages, _dropped, _textTruncated, _logErrors;
    private bool _disposed, _resultRecorded, _exceptionRecorded;

    internal static UsbipSetupEvidence? TryBegin(string persona, string host, string run,
        ulong epoch, string command, string coreSha, string coreMvid)
    {
        if (!string.Equals(persona, "steamdeck", StringComparison.Ordinal)) return null;
        try { return new UsbipSetupEvidence(host, run, epoch, command, coreSha, coreMvid); }
        catch { return null; }
    }

    private UsbipSetupEvidence(string host, string run, ulong epoch, string command, string coreSha, string coreMvid)
    {
        _directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
        _host = Clean(host, 64); _run = Clean(run, 64); _command = Clean(command, 48); _epoch = epoch;
        string installer = coreSha == "8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c" ? "0.9.8.1" : "unknown";
        string keep = Inherited("HIDMAESTRO_KEEP_TRANSPORT", 16, out bool keepCut);
        string timeout = Inherited("HIDMAESTRO_TIMEOUT_SCALE", 32, out bool timeoutCut);
        string diag = Inherited("HIDMAESTRO_DIAG", 16, out bool diagCut);
        string timing = Inherited("HIDMAESTRO_TIMING", 16, out bool timingCut);
        Write("scope-begin", $"HIDMAESTRO_KEEP_TRANSPORT={keep} HIDMAESTRO_TIMEOUT_SCALE={timeout} HIDMAESTRO_DIAG={diag} HIDMAESTRO_TIMING={timing} coreSha={Clean(coreSha, 64)} coreMvid={Clean(coreMvid, 64)} bundledInstallerVersion={installer} migrationBranchObserved=false maxLines={LineLimit} maxProgress={ProgressLimit} maxStages={StageLimit} maxUtf8Bytes={TextLimit} snapshot=begin/end;read-only;registered-binding-and-disk-bytes;not-loaded-memory-image;not-availability-proof", truncatedBeforeWrite: keepCut || timeoutCut || diagCut || timingCut);
        Snapshot("begin");
    }

    internal void Stage(string phase, string detail = "", bool? available = null) => Record(phase, detail, available, false);

    internal void Report(string message) => Record("sdk-progress", message, null, true);

    private void Record(string phase, string detail, bool? available, bool progress)
    {
        try
        {
            lock (_gate)
            {
                if (_disposed) return;
                // 2 * (service + image + enum + 8 * (binding/CM + Class)) + begin/end
                // = 40 facts; 64 reports + 22 stages + two unique terminal stages = 128.
                bool terminal = !progress && (phase is "prepare-result" or "prepare-exception");
                bool rejected;
                if (terminal)
                {
                    rejected = phase == "prepare-result" ? _resultRecorded : _exceptionRecorded;
                    if (phase == "prepare-result") _resultRecorded = true; else _exceptionRecorded = true;
                }
                else if (progress) { rejected = _progress >= ProgressLimit; if (!rejected) ++_progress; }
                else { rejected = _stages >= StageLimit; if (!rejected) ++_stages; }
                if (rejected) { Saturate(ref _dropped); return; }
                Write(phase, detail, available);
            }
        }
        catch { } // A diagnostic failure must not escape into controller setup.
    }

    public void Dispose()
    {
        try
        {
            lock (_gate)
            {
                if (_disposed) return;
                _disposed = true;
                Snapshot("end");
                Write("scope-end", $"attemptedBeforeEnd={_attempted} writtenBeforeEnd={_written} progress={_progress} stages={_stages} dropped={_dropped} textTruncated={_textTruncated} logErrorsBeforeEnd={_logErrors} elapsedMs={Stopwatch.GetElapsedTime(_started).TotalMilliseconds.ToString("F1", CultureInfo.InvariantCulture)};observations-not-availability-or-loaded-memory-image-proof");
            }
        }
        catch { }
    }

    private void Snapshot(string when)
    {
        string prefix = "snapshot=" + when + " ";
        try
        {
            object?[]? service = ReadRegistryValues(Service, "ImagePath", "Type", "Start", "ErrorControl");
            Write("registered-service", prefix + (service is null ? "status=missing" :
                $"status=observed type={Value(service[1])} startConfig={Value(service[2])} errorControl={Value(service[3])}") + ";registered-configuration-only");
            try { Write("disk-image", prefix + DiskImage(service?[0] as string)); }
            catch (Exception ex) { Write("disk-image", prefix + "status=unavailable error=" + ex.GetType().Name); }
        }
        catch (Exception ex) { Write("registered-service", prefix + "status=unavailable error=" + ex.GetType().Name); }
        try
        {
            // Fixed value names only; never enumerate PnP, services, Class, or DriverStore.
            object?[]? entries = ReadRegistryValues(Service + @"\Enum", "Count", "0", "1", "2", "3", "4", "5", "6", "7");
            int? count = entries?[0] is int n && n >= 0 ? n : null;
            Write("service-enum", prefix + $"status={(entries is null ? "missing" : "observed")} registeredCount={(count?.ToString() ?? "unknown")} limit=8 omitted={(count.HasValue ? Math.Max(0, count.Value - 8).ToString() : "unknown")}");
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; entries is not null && i < Math.Min(count ?? 8, 8); ++i)
            {
                string? id = entries[i + 1] as string;
                if (!ValidInstance(id) || !seen.Add(id!))
                { Write("registered-binding", prefix + $"index={i} status=instance-rejected-or-duplicate"); continue; }
                Instance(when, i, id!);
            }
        }
        catch (Exception ex) { Write("service-enum", prefix + "status=unavailable error=" + ex.GetType().Name); }
    }

    private void Instance(string when, int index, string id)
    {
        string prefix = $"snapshot={when} index={index} ";
        try
        {
            object?[]? binding = ReadRegistryValues(EnumRoot + id, "Service", "Driver", "ConfigFlags", "Problem", "StatusFlags");
            bool matches = string.Equals(binding?[0] as string, "usbip2_ude", StringComparison.OrdinalIgnoreCase);
            string cmDetail = "cmStatus=service-binding-unproven started=unknown problem=unknown";
            if (matches)
            {
                try
                {
                    var cm = ReadCmStatus(id);
                    cmDetail = $"locateResult={cm.Locate} statusResult={cm.Result} cmStatus={cm.Status} started={(cm.Result == 0 ? ((cm.Status & 8) != 0).ToString().ToLowerInvariant() : "unknown")} problem={(cm.Result == 0 ? cm.Problem.ToString() : "unknown")}";
                }
                catch (Exception ex) { cmDetail = "cmStatus=unavailable started=unknown problem=unknown error=" + ex.GetType().Name; }
            }
            Write("registered-binding", prefix + $"status={(binding is null ? "missing" : "observed")} {cmDetail} registryService={Value(binding?[0])} driverKey={Value(binding?[1])} configFlags={Value(binding?[2])} registryProblem={Value(binding?[3])} registryStatus={Value(binding?[4])};registered-binding-and-read-only-devnode-status instanceId={Clean(id, 256)}");
            if (!matches)
            { Write("registered-driver", prefix + "status=service-binding-unproven version=versionunknown"); return; }
            string? driver = binding?[1] as string;
            try
            {
                bool valid = driver is not null && Regex.IsMatch(driver, @"^\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}\\[0-9]{4}\z");
                object?[]? klass = valid ? ReadRegistryValues(ClassRoot + driver, "InfPath", "InfSection", "DriverVersion", "DriverDate", "ProviderName") : null;
                Write("registered-driver", prefix + $"status={(klass is not null ? "observed" : valid ? "missing" : "driver-key-rejected")} inf={Value(klass?[0])} section={Value(klass?[1])} version={VersionValue(klass?[2])} date={Value(klass?[3])} provider={Value(klass?[4])};registered-binding;not-loaded-memory-image");
            }
            catch (Exception ex) { Write("registered-driver", prefix + "status=unavailable version=versionunknown error=" + ex.GetType().Name); }
        }
        catch (Exception ex) { Write("registered-binding", prefix + "status=unavailable error=" + ex.GetType().Name); }
    }

    private static bool ValidInstance(string? id) => id is not null && id.Length <= 512 &&
        Regex.IsMatch(id, @"^[a-zA-Z0-9_&{}.-]+\\[a-zA-Z0-9_&{}.-]+\\[a-zA-Z0-9_&{}.-]+\z") &&
        !id.Split('\\').Any(p => p is "." or "..");

    // These two methods are the only registry/CM terminals. The software fixture replaces
    // their bodies explicitly, leaving validation, snapshots, limits and file reads intact.
    private static object?[]? ReadRegistryValues(string path, params string[] names)
    {
        using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
        using var key = hive.OpenSubKey(path, false);
        return key is null ? null : names.Select(n => key.GetValue(n, null, RegistryValueOptions.DoNotExpandEnvironmentNames)).ToArray();
    }

    private static (uint Locate, uint Result, uint Status, uint Problem) ReadCmStatus(string id)
    {
        uint locate = CM_Locate_DevNodeW(out uint node, id, 0), state = 0, problem = 0;
        uint result = locate == 0 ? CM_Get_DevNode_Status(out state, out problem, node, 0) : locate;
        return (locate, result, state, problem);
    }

    private static string? ImagePath(string? raw)
    {
        if (string.IsNullOrEmpty(raw) || raw.Length > 512 || raw.Any(char.IsControl)) return null;
        string path = raw.Trim();
        if (path.Length >= 2 && path[0] == '"' && path[^1] == '"') path = path[1..^1];
        string windows = Path.TrimEndingDirectorySeparator(Path.GetFullPath(Environment.GetFolderPath(Environment.SpecialFolder.Windows)));
        foreach (string alias in new[] { @"\SystemRoot\", @"%SystemRoot%\", @"%windir%\" })
            if (path.StartsWith(alias, StringComparison.OrdinalIgnoreCase)) { path = windows + "\\" + path[alias.Length..]; break; }
        if (path.StartsWith(@"System32\", StringComparison.OrdinalIgnoreCase)) path = windows + "\\" + path;
        // No UNC, NT/device namespace, environment expansion, arguments, ADS or traversal.
        if (!Regex.IsMatch(path, @"^[a-zA-Z]:\\") || path[2..].Contains(':') ||
            !path.EndsWith(".sys", StringComparison.OrdinalIgnoreCase)) return null;
        string[] parts = path[3..].Split('\\');
        if (parts.Length > 24 || parts.Any(p => p.Length == 0 || p is "." or ".." || p.EndsWith('.') || p.EndsWith(' ') ||
            !Regex.IsMatch(p, @"^[a-zA-Z0-9_. -]+$") || Regex.IsMatch(p, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", RegexOptions.IgnoreCase))) return null;
        string full = Path.GetFullPath(path);
        return full.StartsWith(windows + "\\", StringComparison.OrdinalIgnoreCase) ? full : null;
    }

    private static string DiskImage(string? raw)
    {
        string? path = ImagePath(raw);
        if (path is null) return "status=image-path-rejected version=versionunknown;disk-bytes-only;not-loaded-memory-image";
        // Reject reparse points, then check the opened handle's final path before reading.
        for (string? part = path; part is not null; part = Path.GetDirectoryName(part))
            if ((File.GetAttributes(part) & FileAttributes.ReparsePoint) != 0) return "status=reparse-path-rejected version=versionunknown";
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        var final = new StringBuilder(1024);
        uint chars = GetFinalPathNameByHandleW(stream.SafeFileHandle, final, (uint)final.Capacity, 0);
        if (chars == 0 || chars >= final.Capacity || !string.Equals(final.ToString(), @"\\?\" + path, StringComparison.OrdinalIgnoreCase))
            return "status=final-path-rejected version=versionunknown";
        long length = stream.Length;
        if (length > ImageLimit) return $"status=image-over-budget bytes={length} limit={ImageLimit} version=versionunknown;disk-bytes-only";
        var bytes = new byte[(int)length];
        stream.ReadExactly(bytes);
        string sha = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        return $"status=observed bytes={bytes.Length} sha256={sha} version={DiskVersion(bytes)};sha256-and-version-of-copied-disk-bytes;not-loaded-memory-image path={Clean(path, 256)}";
    }

    // Parse only RT_VERSION's first name/language and VS_FIXEDFILEINFO from the already
    // bounded copy. No loading the SYS, second pathname read, temp file, or version policy.
    private static string DiskVersion(byte[] bytes)
    {
        try
        {
            using var pe = new PEReader(new MemoryStream(bytes, false));
            var dir = pe.PEHeaders.PEHeader!.ResourceTableDirectory;
            if (dir.Size <= 0 || dir.Size > ImageLimit) return "versionunknown";
            byte[] resource = pe.GetSectionData(dir.RelativeVirtualAddress).GetContent(0, dir.Size).ToArray();
            uint U32(int at) => BinaryPrimitives.ReadUInt32LittleEndian(resource.AsSpan(at, 4));
            ushort U16(int at) => BinaryPrimitives.ReadUInt16LittleEndian(resource.AsSpan(at, 2));
            int Entry(int at, int? type = null)
            {
                int count = U16(at + 12) + U16(at + 14);
                for (int i = 0; i < Math.Min(count, 32); ++i)
                {
                    int entry = checked(at + 16 + i * 8);
                    if (!type.HasValue || U32(entry) == type.Value) return unchecked((int)U32(entry + 4));
                }
                throw new BadImageFormatException();
            }
            int Branch(int entry) => entry < 0 ? entry & int.MaxValue : throw new BadImageFormatException();
            int nameDir = Branch(Entry(0, 16));
            int languageDir = Branch(Entry(nameDir));
            int dataEntry = Entry(languageDir);
            if (dataEntry < 0) return "versionunknown";
            int rva = checked((int)U32(dataEntry)), size = checked((int)U32(dataEntry + 4));
            if (size < 92 || size > 64 * 1024) return "versionunknown";
            byte[] value = pe.GetSectionData(rva).GetContent(0, size).ToArray();
            byte[] key = Encoding.Unicode.GetBytes("VS_VERSION_INFO\0");
            int fixedAt = (6 + key.Length + 3) & ~3;
            if (!value.AsSpan(6, key.Length).SequenceEqual(key) || BinaryPrimitives.ReadUInt16LittleEndian(value) > size ||
                BinaryPrimitives.ReadUInt16LittleEndian(value.AsSpan(2)) < 52 || fixedAt + 52 > size ||
                BinaryPrimitives.ReadUInt32LittleEndian(value.AsSpan(fixedAt)) != 0xfeef04bd) return "versionunknown";
            uint ms = BinaryPrimitives.ReadUInt32LittleEndian(value.AsSpan(fixedAt + 8));
            uint ls = BinaryPrimitives.ReadUInt32LittleEndian(value.AsSpan(fixedAt + 12));
            return $"{ms >> 16}.{ms & 65535}.{ls >> 16}.{ls & 65535}";
        }
        catch { return "versionunknown"; }
    }

    private static string Inherited(string name, int limit, out bool truncated)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        string value = raw is null ? "<unset>" : Clean(raw, limit);
        truncated = raw is not null && raw.Length != value.Length;
        return value;
    }

    private static void Saturate(ref int value) { if (value < int.MaxValue) ++value; }
    private static string Value(object? value) => value is string s ? Clean(s, 96) :
        value is int or uint or long ? value.ToString()! : "unknown";
    private static string VersionValue(object? value) => value is string s && !string.IsNullOrWhiteSpace(s) ? Clean(s, 96) : "versionunknown";
    private static string Clean(string? text, int limit)
    {
        if (text is null) return "";
        var result = new StringBuilder(Math.Min(text.Length, limit));
        foreach (var rune in text.EnumerateRunes())
        {
            if (result.Length + rune.Utf16SequenceLength > limit) break;
            result.Append(Rune.IsControl(rune) ? " " : rune.ToString());
        }
        return result.ToString();
    }

    private void Write(string phase, string detail, bool? available = null, bool truncatedBeforeWrite = false)
    {
        if (_attempted >= LineLimit) { Saturate(ref _dropped); return; }
        ++_attempted;
        try
        {
            string bounded = Clean(detail, TextLimit), safePhase = Clean(phase, 48);
            bool truncated = truncatedBeforeWrite || bounded.Length != (detail?.Length ?? 0) || safePhase.Length != (phase?.Length ?? 0);
            long timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            string Serialize() => JsonSerializer.Serialize(new { t = timestamp, kind = "usbip-setup-evidence", phase = safePhase,
                scopeId = _scope, scopeHostPid = Environment.ProcessId, hostInstanceId = _host, runId = _run,
                epoch = _epoch, command = _command, persona = "steamdeck", data = new { detail = bounded, available, textTruncated = truncated } }, Json);
            string line = Serialize();
            while (Utf8.GetByteCount(line + "\n") > TextLimit && bounded.Length > 0)
            {
                bounded = Clean(bounded, Math.Max(0, bounded.Length - 16)); truncated = true; line = Serialize();
            }
            if (truncated) Saturate(ref _textTruncated);
            if (Utf8.GetByteCount(line + "\n") > TextLimit) { Saturate(ref _logErrors); return; }
            lock (XboxSdkDiagnostics.LogLock)
            {
                Directory.CreateDirectory(_directory);
                string path = Path.Combine(_directory, "input-host-xbox-sdk.log");
                if (File.Exists(path) && new FileInfo(path).Length + Utf8.GetByteCount(line + "\n") > LogLimit)
                    File.Move(path, path + ".1", true);
                File.AppendAllText(path, line + "\n", Utf8);
            }
            ++_written;
        }
        catch { Saturate(ref _logErrors); }
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern uint CM_Locate_DevNodeW(out uint node, string id, uint flags);
    [DllImport("cfgmgr32.dll", ExactSpelling = true)]
    private static extern uint CM_Get_DevNode_Status(out uint status, out uint problem, uint node, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file, StringBuilder path, uint chars, uint flags);
}
