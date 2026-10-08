using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

internal static class Program
{
const uint DigcfPresent = 0x00000002;
const uint DigcfDeviceInterface = 0x00000010;
const uint GenericRead = 0x80000000;
const uint FileShareRead = 0x00000001;
const uint FileShareWrite = 0x00000002;
const uint OpenExisting = 3;
const uint FileFlagOverlapped = 0x40000000;
const ushort VIRTUAL_SONY_VID = 0x054C;
const ushort VIRTUAL_DS4_PID = 0x09CC;
const ushort VIRTUAL_DUALSENSE_PID = 0x0CE6;
public static int Main(string[] args)
{
// --watch <seconds> [--pid <hex>]：消费者侧线缆追踪（只读）。逐帧读回虚拟手柄的
// 原始 input report，按真实线缆布局解码每个字段，只打印"变化"的字段；退出时给出
// 字段级统计。每帧按设计必变的字段（DS4 wire7 计数器 / DualSense seq+timestamp）
// 单独归类，其余任何变化都意味着线上真的发出了信号 —— 这是"实际输出但无感知"
// 信号的唯一可信来源（UI 只显示 canonical 值，看不到 raw 噪声）。
// 不带参数时保持原 JSON 语义（virtual_toggle_state.ps1 依赖）。
if (args.Length >= 2 && args[0] == "--watch")
{
    if (!int.TryParse(args[1], out var seconds) || seconds < 1 || seconds > 600) throw new ArgumentException("--watch seconds must be 1..600");
    ushort? pidFilter = null;
    var waitSeconds = 15;
    ushort vidFilter = VIRTUAL_SONY_VID;
    string? saveProfile = null;
    string? diffProfile = null;
    for (var i = 2; i + 1 < args.Length; i += 2)
    {
        if (args[i] == "--pid") pidFilter = Convert.ToUInt16(args[i + 1], 16);
        else if (args[i] == "--vid") vidFilter = Convert.ToUInt16(args[i + 1], 16);
        else if (args[i] == "--wait") waitSeconds = Math.Clamp(Convert.ToInt32(args[i + 1]), 0, 300);
        else if (args[i] == "--save-profile") saveProfile = args[i + 1];
        else if (args[i] == "--diff-profile") diffProfile = args[i + 1];
    }
    return Watch(seconds, pidFilter, waitSeconds, vidFilter, saveProfile, diffProfile);
}
// --usage-watch <seconds> [--pid <hex>] [--wait <sec>]：按描述符 usage 读同一份
// 报文（Windows HID 解析器口径），用于实测"平台按 Windows 轴约定误读 Sony 手柄"。
if (args.Length >= 2 && args[0] == "--usage-watch")
{
    if (!int.TryParse(args[1], out var usageSeconds) || usageSeconds < 1 || usageSeconds > 600) throw new ArgumentException("--usage-watch seconds must be 1..600");
    ushort? usagePid = null;
    var usageWait = 15;
    ushort usageVid = VIRTUAL_SONY_VID;
    for (var i = 2; i + 1 < args.Length; i += 2)
    {
        if (args[i] == "--pid") usagePid = Convert.ToUInt16(args[i + 1], 16);
        else if (args[i] == "--vid") usageVid = Convert.ToUInt16(args[i + 1], 16);
        else if (args[i] == "--wait") usageWait = Math.Clamp(Convert.ToInt32(args[i + 1]), 0, 300);
    }
    return UsageWatch(usageSeconds, usagePid, usageWait, usageVid);
}
var hidGuid = new Guid("4D1E55B2-F16F-11CF-88CB-001111000030");
var rows = new List<object>();
var info = SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
if (info == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
try
{
    for (uint index = 0; ; index++)
    {
        var interfaceData = new SpDeviceInterfaceData { CbSize = Marshal.SizeOf<SpDeviceInterfaceData>() };
        if (!SetupDiEnumDeviceInterfaces(info, IntPtr.Zero, ref hidGuid, index, ref interfaceData)) break;
        SetupDiGetDeviceInterfaceDetail(info, ref interfaceData, IntPtr.Zero, 0, out var required, IntPtr.Zero);
        if (required == 0) continue;
        var detail = Marshal.AllocHGlobal((int)required);
        try
        {
            Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
            if (!SetupDiGetDeviceInterfaceDetail(info, ref interfaceData, detail, required, IntPtr.Zero, IntPtr.Zero)) continue;
            var path = Marshal.PtrToStringUni(IntPtr.Add(detail, IntPtr.Size == 8 ? 8 : 4));
            if (path?.StartsWith("?\\", StringComparison.Ordinal) == true) path = "\\\\" + path;
            if (string.IsNullOrWhiteSpace(path)) continue;
            var handle = CreateFile(path, GenericRead, FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, FileFlagOverlapped, IntPtr.Zero);
            if (handle == new IntPtr(-1))
            {
                rows.Add(new { path, open = false, error = Marshal.GetLastWin32Error() });
                continue;
            }
            try
            {
                var attributes = new HidpAttributes { Size = Marshal.SizeOf<HidpAttributes>() };
                var attributesOk = HidDGetAttributes(handle, ref attributes);
                var candidate = attributesOk && attributes.VendorId == 0x054C
                    && (attributes.ProductId == VIRTUAL_DS4_PID || attributes.ProductId == VIRTUAL_DUALSENSE_PID);
                var caps = new HidpCaps { Reserved2 = new ushort[17] };
                var preparsedOk = HidDGetPreparsedData(handle, out var preparsed);
                var capsStatus = -1;
                if (preparsedOk)
                {
                    capsStatus = HidPGetCaps(preparsed, ref caps);
                    HidDFreePreparsedData(preparsed);
                }
                var input = candidate ? ReadInputStream(handle, caps.InputReportByteLength) : new { attempted = false, ok = false, length = (int)caps.InputReportByteLength, hex = (string?)null, lastError = 0 };
                var feature = candidate ? ReadReport(handle, caps.FeatureReportByteLength, HidDGetFeature) : new { attempted = false, length = caps.FeatureReportByteLength };
                rows.Add(new
                {
                    path,
                    open = true,
                    attributesOk,
                    candidate,
                    vid = attributes.VendorId,
                    pid = attributes.ProductId,
                    version = attributes.VersionNumber,
                    preparsedOk,
                    capsStatus,
                    usagePage = caps.UsagePage,
                    usage = caps.Usage,
                    inputLength = caps.InputReportByteLength,
                    featureLength = caps.FeatureReportByteLength,
                    input,
                    feature
                });
            }
            finally { CloseHandle(handle); }
        }
        finally { Marshal.FreeHGlobal(detail); }
    }
}
finally { SetupDiDestroyDeviceInfoList(info); }

Console.WriteLine(JsonSerializer.Serialize(new { utc = DateTime.UtcNow, count = rows.Count, devices = rows }, new JsonSerializerOptions { WriteIndented = true }));
return 0;
}

static object ReadReport(IntPtr handle, ushort length, Func<IntPtr, byte[], int, bool> getter)
{
    if (length == 0 || length > 1024) return new { attempted = false, length };
    var bytes = new byte[length];
    var ok = getter(handle, bytes, bytes.Length);
    return new { attempted = true, ok, length = bytes.Length, hex = ok ? Convert.ToHexString(bytes) : null, lastError = ok ? 0 : Marshal.GetLastWin32Error() };
}

// ─ 线缆追踪（--watch）──────────────────────────────────────────────────────
// 字段名表按真实线缆布局（DS4: SDL3 PS4StatePacket_t / ds4drv；DualSense:
// Linux hid-playstation struct dualsense_input_report）。索引 = payload 字节
// （报告 ID 由驱动前置，已剥离），与 InputHost 的 data-only 数组同坐标系。
static string[] Ds4Names() => new[]
{
    "lx", "ly", "rx", "ry", "hat", "face", "btn1(L1R1L2R2/SH/OP/L3R3)", "btn2(PS/TPAD)",
    "wire7.counter", "l2analog", "r2analog", "timestamp", "pad0", "gyro", "accel", "pad1",
    "battery", "pad2", "touch0", "touch1", "deviceSpecific"
};
static string[] Ds4PerFrame() => new[] { "wire7.counter", "timestamp" };
static string[] DualSenseNames() => new[]
{
    "lx", "ly", "rx", "ry", "l2analog", "r2analog", "seq", "btn0(hat/face)", "btn1(L1R1L2R2/Create/Options/L3R3)",
    "btn2(PS/TPAD/Mute)", "reserved10-14", "gyro", "accel", "sensorTimestamp", "reserved31", "touch0", "touch1",
    "reserved40-51", "status52-54"
};
static string[] DualSensePerFrame() => new[] { "seq", "sensorTimestamp" };
static string[] Names(ushort pid) => pid == VIRTUAL_DUALSENSE_PID ? DualSenseNames() : Ds4Names();
static string[] PerFrame(ushort pid) => pid == VIRTUAL_DUALSENSE_PID ? DualSensePerFrame() : Ds4PerFrame();
static string S16(byte[] p, int i) => ((short)(p[i] | (p[i + 1] << 8))).ToString();
static string Hex(byte[] p, int i, int count) => Convert.ToHexString(p, i, count);

static string[] Decode(ushort pid, byte[] p)
{
    if (pid == VIRTUAL_DUALSENSE_PID)
    {
        var gyroStart = p.Length > 20 ? 15 : -1;
        return new[]
        {
            p[0].ToString(), p[1].ToString(), p[2].ToString(), p[3].ToString(),
            p[4].ToString(), p[5].ToString(), p[6].ToString(),
            $"0x{p[7]:X2}", $"0x{p[8]:X2}", $"0x{p[9]:X2}",
            p.Length > 14 ? Hex(p, 10, 5) : "n/a",
            gyroStart >= 0 ? $"{S16(p, 15)},{S16(p, 17)},{S16(p, 19)}" : "n/a",
            gyroStart >= 0 ? $"{S16(p, 21)},{S16(p, 23)},{S16(p, 25)}" : "n/a",
            gyroStart >= 0 ? ((uint)(p[27] | (p[28] << 8) | (p[29] << 16) | (p[30] << 24))).ToString() : "n/a",
            p.Length > 31 ? $"0x{p[31]:X2}" : "n/a",
            p.Length > 35 ? Hex(p, 32, 4) : "n/a",
            p.Length > 39 ? Hex(p, 36, 4) : "n/a",
            p.Length > 51 ? Hex(p, 40, 12) : "n/a",
            p.Length > 54 ? Hex(p, 52, 3) : "n/a"
        };
    }
    return new[]
    {
        p[0].ToString(), p[1].ToString(), p[2].ToString(), p[3].ToString(),
        $"{(p[4] & 0x0F)}", $"0x{(p[4] >> 4):X2}", $"0x{p[5]:X2}", $"0x{(p[6] & 0x03):X2}",
        $"{(p[6] >> 2)}", p[7].ToString(), p[8].ToString(),
        p.Length > 10 ? ((ushort)(p[9] | (p[10] << 8))).ToString() : "n/a",
        p.Length > 11 ? $"0x{p[11]:X2}" : "n/a",
        p.Length > 17 ? $"{S16(p, 12)},{S16(p, 14)},{S16(p, 16)}" : "n/a",
        p.Length > 23 ? $"{S16(p, 18)},{S16(p, 20)},{S16(p, 22)}" : "n/a",
        p.Length > 28 ? Hex(p, 24, 5) : "n/a",
        p.Length > 29 ? $"0x{p[29]:X2}" : "n/a",
        p.Length > 33 ? Hex(p, 30, 4) : "n/a",
        p.Length > 37 ? Hex(p, 34, 4) : "n/a",
        p.Length > 41 ? Hex(p, 38, 4) : "n/a",
        p.Length > 53 ? Hex(p, 42, 12) : "n/a"
    };
}

static int Watch(int seconds, ushort? pidFilter, int waitSeconds, ushort vidFilter, string? saveProfile, string? diffProfile)
{
    var deadline = DateTime.UtcNow.AddSeconds(waitSeconds);
    var targets = new List<(string Path, ushort Pid, ushort InputLength)>();
    var diagnostics = string.Empty;
    var attempt = 0;
    while (true)
    {
        attempt++;
        targets = EnumerateWatchTargets(pidFilter, vidFilter, out diagnostics);
        if (targets.Count > 0) break;
        if (DateTime.UtcNow >= deadline) break;
        if (attempt == 1 || attempt % 8 == 0) Console.WriteLine($"waiting for target... ({diagnostics})");
        Thread.Sleep(250);
    }
    if (targets.Count == 0)
    {
        Console.WriteLine($"no matching HID interface present (vid=0x{vidFilter:X4}" + (pidFilter.HasValue ? $", pid=0x{pidFilter.Value:X4}" : string.Empty) + ")" + (waitSeconds > 0 ? $" after {waitSeconds}s" : string.Empty) + ".");
        Console.WriteLine("   probe sighting: " + diagnostics);
        return 2;
    }
    var stats = new Dictionary<string, FieldStat>();
    foreach (var target in targets)
    {
        var perTarget = WatchDevice(target.Path, target.Pid, target.InputLength, seconds);
        foreach (var pair in perTarget) stats[pair.Key] = pair.Value;
    }
    if (saveProfile is not null)
    {
        var payload = new
        {
            schema = "ymcc-wire-profile/1",
            capturedUtc = DateTime.UtcNow.ToString("o"),
            pid = pidFilter.HasValue ? pidFilter.Value.ToString("X4") : (targets.Count > 0 ? targets[0].Pid.ToString("X4") : string.Empty),
            layout = pidFilter == VIRTUAL_DUALSENSE_PID ? "DualSense" : "DS4",
            seconds,
            fields = stats.ToDictionary(p => p.Key, p => new { changes = p.Value.Changes, first = p.Value.First, last = p.Value.Last })
        };
        File.WriteAllText(Path.GetFullPath(saveProfile), JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"profile saved: {Path.GetFullPath(saveProfile)} ({stats.Count} fields with observed activity)");
    }
    if (diffProfile is not null)
    {
        // A/B 对照：与参考档案逐字段比对"活跃度"。用于把真实手柄与虚拟手柄（或两个
        // persona）放在同一解码器下比较，机械地回答"是否还有别人会变而我们不会变的
        // 字段"（= 用户担心的"抓不到的信号"）。
        var reference = JsonSerializer.Deserialize<JsonElement>(File.ReadAllText(Path.GetFullPath(diffProfile)));
        var referenceFields = new Dictionary<string, long>();
        if (reference.TryGetProperty("fields", out var fields))
        {
            foreach (var field in fields.EnumerateObject())
                referenceFields[field.Name] = field.Value.TryGetProperty("changes", out var c) ? c.GetInt64() : 0;
        }
        Console.WriteLine("-- A/B diff vs reference profile --");
        Console.WriteLine($"   reference: {Path.GetFullPath(diffProfile)} ({referenceFields.Count} active fields), current: {stats.Count} active fields");
        var missing = referenceFields.Keys.Where(n => !stats.ContainsKey(n)).ToList();
        var extra = stats.Keys.Where(n => !referenceFields.ContainsKey(n)).ToList();
        var both = referenceFields.Keys.Where(n => stats.ContainsKey(n)).ToList();
        Console.WriteLine("   changed THERE but never here (fields this side cannot produce): " + (missing.Count > 0 ? string.Join(", ", missing) : "(none)"));
        Console.WriteLine("   changed HERE but not there (extra activity): " + (extra.Count > 0 ? string.Join(", ", extra) : "(none)"));
        Console.WriteLine("   active on both sides: " + (both.Count > 0 ? string.Join(", ", both) : "(none)"));
    }
    return 0;
}

// Enumerate HID interfaces and keep the virtual Sony targets. Diagnostics count
// every step so an empty result can be told apart from a blocked/failed open
// (HidHide cloaking, device not yet enumerated, attributes refused, ...).
static List<(string Path, ushort Pid, ushort InputLength)> EnumerateWatchTargets(ushort? pidFilter, ushort vidFilter, out string diagnostics)
{
    var hidGuid = new Guid("4D1E55B2-F16F-11CF-88CB-001111000030");
    var info = SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
    if (info == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    var targets = new List<(string Path, ushort Pid, ushort InputLength)>();
    var interfaces = 0;
    var opened = 0;
    var openFailed = 0;
    var sonySighted = 0;
    var pids = new List<string>();
    try
    {
        for (uint index = 0; ; index++)
        {
            var interfaceData = new SpDeviceInterfaceData { CbSize = Marshal.SizeOf<SpDeviceInterfaceData>() };
            if (!SetupDiEnumDeviceInterfaces(info, IntPtr.Zero, ref hidGuid, index, ref interfaceData)) break;
            interfaces++;
            SetupDiGetDeviceInterfaceDetail(info, ref interfaceData, IntPtr.Zero, 0, out var required, IntPtr.Zero);
            if (required == 0) continue;
            var detail = Marshal.AllocHGlobal((int)required);
            try
            {
                Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                if (!SetupDiGetDeviceInterfaceDetail(info, ref interfaceData, detail, required, IntPtr.Zero, IntPtr.Zero)) continue;
                var path = Marshal.PtrToStringUni(IntPtr.Add(detail, IntPtr.Size == 8 ? 8 : 4));
                if (path?.StartsWith("?\\", StringComparison.Ordinal) == true) path = "\\\\" + path;
                if (string.IsNullOrWhiteSpace(path)) continue;
                var handle = CreateFile(path, GenericRead, FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, FileFlagOverlapped, IntPtr.Zero);
                if (handle == new IntPtr(-1)) { openFailed++; continue; }
                opened++;
                try
                {
                    var attributes = new HidpAttributes { Size = Marshal.SizeOf<HidpAttributes>() };
                    if (!HidDGetAttributes(handle, ref attributes)) continue;
                    if (attributes.VendorId != vidFilter) continue;
                    sonySighted++;
                    pids.Add($"{attributes.ProductId:X4}");
                    if (!pidFilter.HasValue && attributes.ProductId != VIRTUAL_DS4_PID && attributes.ProductId != VIRTUAL_DUALSENSE_PID) continue;
                    if (pidFilter.HasValue && attributes.ProductId != pidFilter.Value) continue;
                    var caps = new HidpCaps { Reserved2 = new ushort[17] };
                    if (!HidDGetPreparsedData(handle, out var preparsed)) continue;
                    try { HidPGetCaps(preparsed, ref caps); } finally { HidDFreePreparsedData(preparsed); }
                    targets.Add((path, attributes.ProductId, caps.InputReportByteLength));
                }
                finally { CloseHandle(handle); }
            }
            finally { Marshal.FreeHGlobal(detail); }
        }
    }
    finally { SetupDiDestroyDeviceInfoList(info); }
    diagnostics = $"hidInterfaces={interfaces} opened={opened} openFailed={openFailed} vid=0x{vidFilter:X4} seen={sonySighted}"
        + (pids.Count > 0 ? $" pids=[{string.Join(",", pids)}]" : string.Empty)
        + (pidFilter.HasValue ? $" pidFilter={pidFilter.Value:X4}" : string.Empty);
    return targets;
}

sealed record FieldStat(long Changes, string First, string Last);

static Dictionary<string, FieldStat> WatchDevice(string path, ushort pid, ushort inputLength, int seconds)
{
    Console.WriteLine($"== watch {seconds}s | {pid:X4} | {path}");
    Console.WriteLine($"   layout: {(pid == VIRTUAL_DUALSENSE_PID ? "DualSense (hid-playstation)" : "DS4 (SDL3 PS4StatePacket_t)")}"
        + $" | report {inputLength}B (incl. report id) | per-frame fields by design: {string.Join(", ", PerFrame(pid))}");
    var handle = CreateFile(path, GenericRead, FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, FileFlagOverlapped, IntPtr.Zero);
    if (handle == new IntPtr(-1))
    {
        Console.WriteLine($"open failed: error={Marshal.GetLastWin32Error()}");
        return new Dictionary<string, FieldStat>();
    }
    try
    {
        using var borrowed = new SafeFileHandle(handle, ownsHandle: false);
        using var stream = new FileStream(borrowed, FileAccess.Read, Math.Max(1, (int)inputLength), isAsync: true);
        var buffer = new byte[Math.Max((int)inputLength, 64)];
        var names = Names(pid);
        var perFrame = new HashSet<string>(PerFrame(pid));
        var start = DateTime.UtcNow;
        var deadline = start.AddSeconds(seconds);
        string[]? previous = null;
        string? lastHex = null;
        long frames = 0;
        var perFrameCounts = new SortedDictionary<string, long>();
        var changeCounts = new SortedDictionary<string, long>();
        var firstValue = new SortedDictionary<string, string>();
        var lastValue = new SortedDictionary<string, string>();
        while (DateTime.UtcNow < deadline)
        {
            int read;
            try
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(250));
                read = stream.ReadAsync(buffer.AsMemory(0, (int)inputLength), timeout.Token).AsTask().GetAwaiter().GetResult();
            }
            catch (OperationCanceledException) { continue; }
            catch (Exception ex) { Console.WriteLine("read ended: " + ex.GetType().Name + ":" + ex.Message); break; }
            if (read <= 1) continue;
            frames++;
            var payload = new byte[read - 1];
            Array.Copy(buffer, 1, payload, 0, payload.Length);
            var values = Decode(pid, payload);
            lastHex = Convert.ToHexString(buffer, 0, read);
            if (previous is null)
            {
                Console.WriteLine($"first frame: {lastHex}");
                Console.WriteLine("   " + string.Join(" | ", names.Zip(values, (n, v) => $"{n}={v}")));
            }
            else
            {
                var unexpected = new List<string>();
                for (var i = 0; i < Math.Min(previous.Length, values.Length); i++)
                {
                    if (previous[i] == values[i]) continue;
                    var name = names[i];
                    if (perFrame.Contains(name))
                    {
                        perFrameCounts[name] = perFrameCounts.TryGetValue(name, out var c) ? c + 1 : 1;
                        continue;
                    }
                    unexpected.Add($"{name} {previous[i]}->{values[i]}");
                    if (!changeCounts.ContainsKey(name))
                    {
                        changeCounts[name] = 0;
                        firstValue[name] = previous[i];
                    }
                    changeCounts[name]++;
                    lastValue[name] = values[i];
                }
                if (unexpected.Count > 0)
                    Console.WriteLine($"CHG #{frames:D6} t=+{(DateTime.UtcNow - start).TotalSeconds:F3}s | {string.Join(" | ", unexpected)}");
            }
            previous = values;
        }
        var elapsed = (DateTime.UtcNow - start).TotalSeconds;
        Console.WriteLine("-- summary --");
        Console.WriteLine($"frames={frames} elapsed={elapsed:F2}s rate={(elapsed > 0 ? frames / elapsed : 0):F1}/s");
        foreach (var pair in perFrameCounts) Console.WriteLine($"by design changed: {pair.Key} x{pair.Value}");
        if (changeCounts.Count == 0)
            Console.WriteLine("NO unexpected field changed: every wire field stayed identical while watching.");
        foreach (var pair in changeCounts)
            Console.WriteLine($"UNEXPECTED: {pair.Key} x{pair.Value} ({firstValue[pair.Key]} -> {lastValue[pair.Key]})");
        var untouched = new List<string>();
        for (var i = 0; i < names.Length; i++)
            if (!changeCounts.ContainsKey(names[i]) && !perFrameCounts.ContainsKey(names[i])) untouched.Add(names[i]);
        Console.WriteLine("fields never changed: " + (untouched.Count > 0 ? string.Join(", ", untouched) : "(none)"));
        Console.WriteLine($"last frame: {lastHex}");
        // 档案必须同时包含"每帧必变"字段（DS4 wire7.counter / DualSense seq+timestamp）：
        // 真实手柄与虚拟手柄的差异最可能出现在这里（例如真实设备是否也在推进
        // payload 9-10），A/B 对照要能看到它。
        var combined = changeCounts.ToDictionary(
            pair => pair.Key,
            pair => new FieldStat(pair.Value, firstValue[pair.Key], lastValue[pair.Key]));
        foreach (var pair in perFrameCounts) combined[pair.Key] = new FieldStat(pair.Value, "(per-frame)", "(per-frame)");
        return combined;
    }
    finally { CloseHandle(handle); }
}

static object ReadInputStream(IntPtr handle, ushort length)
{
    if (length == 0 || length > 1024) return new { attempted = false, ok = false, length, hex = (string?)null, lastError = 0 };
    try
    {
        using var borrowed = new SafeFileHandle(handle, ownsHandle: false);
        using var stream = new FileStream(borrowed, FileAccess.Read, 1, isAsync: true);
        var bytes = new byte[length];
        using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(750));
        var read = stream.ReadAsync(bytes.AsMemory(0, bytes.Length), timeout.Token).AsTask().GetAwaiter().GetResult();
        return new { attempted = true, ok = read > 0, length = read, hex = read > 0 ? Convert.ToHexString(bytes, 0, read) : null, lastError = 0 };
    }
    catch (Exception ex) { return new { attempted = true, ok = false, length = (int)length, hex = (string?)null, lastError = Marshal.GetLastWin32Error(), error = ex.GetType().Name + ":" + ex.Message }; }
}

// ─ Windows HID 解析器视角（--usage-watch）──────────────────────────────────
// 关键：有些消费者（DirectInput / Windows.Gaming.Input / 系统组件）**不认 Sony
// 私有约定**，而是按描述符声明的 usage 取轴值：X/Y=左摇杆、Rx/Ry=右摇杆、
// Z/Rz=扳机（Windows 约定）。Sony 手柄把右摇杆放在 Z/Rz、把扳机放在 Rx/Ry，
// 于是同一份报文会被读成"扳机半按 + 右摇杆死推左上"。本模式用系统自己的 HID
// 解析器（HidP_GetUsageValue / HidP_GetUsages）把报文按 usage 读出来，把这一
// 误读从"推断"变成"实测"，并与 Sony 约定视角并列打印。
static string UsageView(IntPtr preparsed, byte[] report, int reportLength)
{
    var axes = new List<string>();
    foreach (var item in new[] { ("X", (ushort)0x30), ("Y", (ushort)0x31), ("Z", (ushort)0x32), ("Rx", (ushort)0x33), ("Ry", (ushort)0x34), ("Rz", (ushort)0x35), ("Hat", (ushort)0x39) })
    {
        for (ushort linkCollection = 0; linkCollection <= 8; linkCollection++)
        {
            var status = HidPGetUsageValue(0, 0x01, linkCollection, item.Item2, out var value, preparsed, report, (uint)reportLength);
            if (status == 0x00110000 || status == 0x10110000) { axes.Add($"{item.Item1}=0x{value:X2}"); break; }
        }
    }
    var pressed = new List<string>();
    for (ushort linkCollection = 0; linkCollection <= 8; linkCollection++)
    {
        var usageList = new ushort[64];
        ushort count = (ushort)usageList.Length;
        var status = HidPGetUsages(0, 0x09, linkCollection, usageList, ref count, preparsed, report, (uint)reportLength);
        if (status == 0x00110000 || status == 0x10110000)
        {
            for (var i = 0; i < count; i++) pressed.Add(usageList[i].ToString());
            break;
        }
    }
    return string.Join(" ", axes) + " | buttons=[" + string.Join(",", pressed) + "]";
}

static int UsageWatch(int seconds, ushort? pidFilter, int waitSeconds, ushort vidFilter)
{
    var deadline = DateTime.UtcNow.AddSeconds(waitSeconds);
    var targets = new List<(string Path, ushort Pid, ushort InputLength)>();
    var diagnostics = string.Empty;
    while (true)
    {
        targets = EnumerateWatchTargets(pidFilter, vidFilter, out diagnostics);
        if (targets.Count > 0 || DateTime.UtcNow >= deadline) break;
        Thread.Sleep(250);
    }
    if (targets.Count == 0)
    {
        Console.WriteLine("no matching HID target present; probe sighting: " + diagnostics);
        return 2;
    }
    foreach (var target in targets) UsageWatchDevice(target.Path, target.Pid, target.InputLength, seconds);
    return 0;
}

static void UsageWatchDevice(string path, ushort pid, ushort inputLength, int seconds)
{
    Console.WriteLine($"== usage-watch {seconds}s | {pid:X4} | {path}");
    Console.WriteLine("   Windows HID parser view (DirectInput/WGI style): X,Y = left stick; Rx,Ry = right stick; Z,Rz = triggers");
    var handle = CreateFile(path, GenericRead, FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, FileFlagOverlapped, IntPtr.Zero);
    if (handle == new IntPtr(-1)) { Console.WriteLine($"open failed: error={Marshal.GetLastWin32Error()}"); return; }
    try
    {
        if (!HidDGetPreparsedData(handle, out var preparsed)) { Console.WriteLine("preparsed data unavailable"); return; }
        try
        {
            using var borrowed = new SafeFileHandle(handle, ownsHandle: false);
            using var stream = new FileStream(borrowed, FileAccess.Read, Math.Max(1, (int)inputLength), isAsync: true);
            var buffer = new byte[Math.Max((int)inputLength, 64)];
            var start = DateTime.UtcNow;
            var deadline = start.AddSeconds(seconds);
            string? previous = null;
            var frames = 0;
            var changes = 0;
            var distinct = new List<string>();
            while (DateTime.UtcNow < deadline)
            {
                int read;
                try
                {
                    using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(250));
                    read = stream.ReadAsync(buffer.AsMemory(0, (int)inputLength), timeout.Token).AsTask().GetAwaiter().GetResult();
                }
                catch (OperationCanceledException) { continue; }
                catch (Exception ex) { Console.WriteLine("read ended: " + ex.GetType().Name); break; }
                if (read <= 1) continue;
                frames++;
                var view = UsageView(preparsed, buffer, read);
                if (view.StartsWith(" | buttons=")) view = "(no Generic-Desktop axes declared on this interface; " + view.TrimStart(' ', '|') + ")";
                if (view != previous)
                {
                    if (previous is not null) changes++;
                    Console.WriteLine($"   t=+{(DateTime.UtcNow - start).TotalSeconds,6:F2}s  {view}");
                    if (distinct.Count < 6) distinct.Add(view);
                    previous = view;
                }
            }
            var elapsed = (DateTime.UtcNow - start).TotalSeconds;
            Console.WriteLine($"-- usage-watch summary: frames={frames} elapsed={elapsed:F2}s distinctViews={changes + 1}");
            Console.WriteLine($"   final view: {previous ?? "(no frame)"}");
            Console.WriteLine("   interpretation: 'Z/Rz = 0x80' means a Windows-convention consumer reports BOTH TRIGGERS ~half pressed;");
            Console.WriteLine("                   'Rx/Ry = 0x00' means it reports the RIGHT STICK pushed hard up-left (Sony sends those bytes as triggers).");
        }
        finally { HidDFreePreparsedData(preparsed); }
    }
    finally { CloseHandle(handle); }
}

[StructLayout(LayoutKind.Sequential)] struct SpDeviceInterfaceData { public int CbSize; public Guid InterfaceClassGuid; public int Flags; public IntPtr Reserved; }
[StructLayout(LayoutKind.Sequential)] struct HidpAttributes { public int Size; public ushort VendorId; public ushort ProductId; public ushort VersionNumber; }
[StructLayout(LayoutKind.Sequential)] struct HidpCaps
{
    public ushort Usage; public ushort UsagePage; public ushort InputReportByteLength; public ushort OutputReportByteLength; public ushort FeatureReportByteLength; public ushort Reserved;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved2;
}

[DllImport("setupapi.dll", SetLastError = true)] static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, uint flags);
[DllImport("setupapi.dll", SetLastError = true)] static extern bool SetupDiEnumDeviceInterfaces(IntPtr infoSet, IntPtr devInfo, ref Guid interfaceClassGuid, uint memberIndex, ref SpDeviceInterfaceData data);
[DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr infoSet, ref SpDeviceInterfaceData data, IntPtr detail, uint detailSize, out uint required, IntPtr devInfoData);
[DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr infoSet, ref SpDeviceInterfaceData data, IntPtr detail, uint detailSize, IntPtr required, IntPtr devInfoData);
[DllImport("setupapi.dll", SetLastError = true)] static extern bool SetupDiDestroyDeviceInfoList(IntPtr infoSet);
[DllImport("hid.dll", EntryPoint = "HidD_GetAttributes", SetLastError = true)] static extern bool HidDGetAttributes(IntPtr handle, ref HidpAttributes attributes);
[DllImport("hid.dll", EntryPoint = "HidD_GetPreparsedData", SetLastError = true)] static extern bool HidDGetPreparsedData(IntPtr handle, out IntPtr preparsed);
[DllImport("hid.dll", EntryPoint = "HidD_FreePreparsedData", SetLastError = true)] static extern bool HidDFreePreparsedData(IntPtr preparsed);
[DllImport("hid.dll", EntryPoint = "HidP_GetCaps", SetLastError = true)] static extern int HidPGetCaps(IntPtr preparsed, ref HidpCaps caps);
[DllImport("hid.dll", EntryPoint = "HidP_GetUsageValue", SetLastError = true)] static extern int HidPGetUsageValue(int reportType, ushort usagePage, ushort linkCollection, ushort usage, out uint usageValue, IntPtr preparsed, [In] byte[] report, uint reportLength);
[DllImport("hid.dll", EntryPoint = "HidP_GetUsages", SetLastError = true)] static extern int HidPGetUsages(int reportType, ushort usagePage, ushort linkCollection, [In, Out] ushort[] usageList, ref ushort usageLength, IntPtr preparsed, [In] byte[] report, uint reportLength);
[DllImport("hid.dll", EntryPoint = "HidD_GetInputReport", SetLastError = true)] static extern bool HidDGetInputReport(IntPtr handle, [In, Out] byte[] report, int length);
[DllImport("hid.dll", EntryPoint = "HidD_GetFeature", SetLastError = true)] static extern bool HidDGetFeature(IntPtr handle, [In, Out] byte[] report, int length);
[DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
[DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr handle);
}
