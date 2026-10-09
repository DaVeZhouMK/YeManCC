// virtual_axis_decl_ab —— S0「描述符声明交换」本机 A/B 可退回验证件（一次性）
//
// 目的：在**不改产品代码**的前提下，验证「只改 HID 描述符的轴 usage 标签、数据字节不动」
//       是否能让 Windows 按正确语义读取虚拟 Sony 手柄（V24），并观察开始菜单行为（V25）。
// 可退回：进程退出即 Dispose 控制器；不留任何系统改动（除本目录的构建产物）。
//
// 用法：
//   virtual_axis_decl_ab --mode original|swapped --persona dualsense|dualshock4 --hold-sec 20 [--start-probe-sec 6] [--out <file>]
//
// 输出：描述符 sha256/length、axisMap、每帧计数、（可选）开始菜单判定与 VK_GAMEPAD 命中。

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using HIDMaestro;

internal static class Program
{
    private const ushort UsageZ = 0x32, UsageRx = 0x33, UsageRy = 0x34, UsageRz = 0x35;

    private static int Main(string[] args)
    {
        string mode = "original", persona = "dualsense", outFile = "", setSpec = "";
        double holdSec = 12; double startProbeSec = 0; bool isStatic = false;
        for (var i = 0; i < args.Length - 1; i++)
        {
            switch (args[i])
            {
                case "--mode": mode = args[++i]; break;
                case "--persona": persona = args[++i]; break;
                case "--hold-sec": holdSec = double.Parse(args[++i]); break;
                case "--start-probe-sec": startProbeSec = double.Parse(args[++i]); break;
                case "--out": outFile = args[++i]; break;
                case "--set": setSpec = args[++i]; break;
                case "--static": isStatic = true; break;
            }
        }
        if (mode is not ("original" or "swapped" or "perm1" or "vendor" or "vendorpad")) { Log("mode must be original|swapped|perm1|vendor|vendorpad"); return 2; }
        if (persona is not ("dualsense" or "dualshock4")) { Log("persona must be dualsense|dualshock4"); return 2; }
        if (outFile.Length > 0) { try { File.AppendAllText(outFile, ""); } catch { } }

        // 运行前的 VK_GAMEPAD 按住状态（差分基线：行为探针必须前后对比，避免陈旧按住污染）
        var preKeys = SnapshotDown();
        Log($"  keys down BEFORE create: [{(preKeys.Count == 0 ? "-" : string.Join(",", preKeys))}]");

        var profileId = persona == "dualsense" ? "dualsense" : "dualshock-4-v2";
        Log($"[{DateTime.Now:HH:mm:ss.fff}] === virtual_axis_decl_ab mode={mode} persona={persona} ({profileId}) hold={holdSec}s startProbe={startProbeSec}s static={isStatic} set='{setSpec}' ===");

        HMContext context = new();
        context.LoadDefaultProfiles();
        var baseProfile = context.GetProfile(profileId) ?? throw new InvalidOperationException("profile-missing:" + profileId);

        var descriptorHex = baseProfile.DescriptorHex;
        var axisMap = baseProfile.AxisMap;
        if (mode == "swapped")
        {
            var (swappedHex, notes) = SwapDescriptorUsages(profileId, descriptorHex);
            var swappedMap = SwapAxisMap(axisMap);
            var built = new HMProfileBuilder().FromProfile(baseProfile).DescriptorHex(swappedHex).AxisMap(swappedMap).Build();
            Log("  swap notes: " + notes);
            Log("  axisMap: " + string.Join(", ", swappedMap.Select(kv => kv.Key + "=" + kv.Value)));
            baseProfile = built;
            descriptorHex = swappedHex;
            axisMap = swappedMap;
        }
        else if (mode == "perm1")
        {
            // 目标声明序 X, Y, Z, Rx, Rz, Ry（= 把 Rz 与 Rx 对调）。依据 12 轮实测的"单轴模型"：
            //   横向键 ← Z（0x80 中性）; 纵向键 ← Rx（0x80 中性）; RT ← Rz（0 中性）; LT ← Ry（0 中性）
            // ⇒ Z/Rx 落到我们的摇杆字节(0x80)，Rz/Ry 落到我们的扳机字节(0) ⇒ 四个角色同时中性。
            // 字节数据零改动；仍是普通 GD/GamePad（Steam/WGI 照常枚举）。
            string swappedHex; string notes;
            if (persona == "dualsense")
            {
                const string from = "0932093509330934"; // Z,Rz,Rx,Ry
                const string to = "0932093309350934";   // Z,Rx,Rz,Ry
                var n = CountOccurrences(descriptorHex, from);
                if (n != 1) throw new InvalidOperationException($"perm1/dualsense: expected 1 axis block, found {n}");
                swappedHex = descriptorHex.Replace(from, to);
                notes = "dualsense axis block: Z,Rz,Rx,Ry -> Z,Rx,Rz,Ry";
            }
            else
            {
                const string fromA = "0930093109320935"; // X,Y,Z,Rz
                const string toA = "0930093109320933";   // X,Y,Z,Rx
                const string fromB = "050109330934";     // trigger item Rx,Ry
                const string toB = "050109350934";       // trigger item Rz,Ry
                var na = CountOccurrences(descriptorHex, fromA); var nb = CountOccurrences(descriptorHex, fromB);
                if (na != 1 || nb != 1) throw new InvalidOperationException($"perm1/ds4: stick={na} trigger={nb} (expect 1/1)");
                swappedHex = descriptorHex.Replace(fromA, toA).Replace(fromB, toB);
                notes = "ds4: X,Y,Z,Rz->X,Y,Z,Rx ; trigger Rx,Ry->Rz,Ry";
            }
            var permMap = new Dictionary<string, string> { ["0x32"] = "rightStickX", ["0x33"] = "rightStickY", ["0x35"] = "leftTrigger", ["0x34"] = "rightTrigger" };
            baseProfile = new HMProfileBuilder().FromProfile(baseProfile).DescriptorHex(swappedHex).AxisMap(permMap).Build();
            descriptorHex = swappedHex; axisMap = permMap;
            Log("  perm1: " + notes);
        }
        else if (mode is "vendor" or "vendorpad")
        {
            // S0' / S0'':把顶层集合从 GD/GamePad 改为 Vendor(FF00:0001)，其余与 Sony 原文一致。
            // 机制对齐 steamdeck persona 的"全 vendor ⇒ 平台不当手柄"（V16 实测零注入）。
            // vendorpad 追加：末尾挂一个"空 GD/GamePad 集合"（无任何 input 项）——
            // 目的：让 Steam 等"按 GD 集合枚举"的消费者仍能看到'手柄存在'，而导航层无轴可读。
            const string from = "05010905a101";
            const string to = "0600ff0901a101";
            var n = CountOccurrences(descriptorHex, from);
            if (n != 1) throw new InvalidOperationException($"vendor: expected 1 top-level GD/GamePad collection, found {n}");
            descriptorHex = descriptorHex.Replace(from, to);
            if (mode == "vendorpad") descriptorHex += "05010905a101c0"; // 空 GamePad 应用集合
            var built = new HMProfileBuilder().FromProfile(baseProfile).DescriptorHex(descriptorHex).Build();
            baseProfile = built;
            Log($"  {mode} transform: top-level GD/GamePad -> Vendor(FF00:0001)" + (mode == "vendorpad" ? " + empty GD/GamePad collection appended" : "") + $" (len {descriptorHex.Length / 2}B)");
        }
        else
        {
            Log("  original descriptor / axisMap (control)");
        }

        Log($"  profileId={baseProfile.Id} vid={baseProfile.VendorId:X4} pid={baseProfile.ProductId:X4} reportSize={baseProfile.InputReportSize}");
        Log($"  descriptorLen={descriptorHex.Length / 2}B sha256={Sha256Hex(Convert.FromHexString(descriptorHex))}");
        Log("  axisMap: " + string.Join(", ", axisMap.Select(kv => kv.Key + "=" + kv.Value)));

        using var controller = context.CreateController(baseProfile);
        Log($"  controller created: {controller.GetType().Name} profile={controller.Profile?.Id}");

        var neutral = persona == "dualsense" ? NeutralDualSense() : NeutralDs4V2();
        ApplySet(neutral, persona, mode, setSpec);
        Log($"  neutral frame ({neutral.Length}B): {Convert.ToHexString(neutral)[..64]}...");

        using var cts = new CancellationTokenSource();
        _t0 = Environment.TickCount64;
        StartKeySampler(cts.Token);
        long frames = 0;
        var pump = Task.Run(() =>
        {
            var sw = Stopwatch.StartNew();
            var counter = 0L;
            while (!cts.IsCancellationRequested)
            {
                if (!isStatic)
                {
                    if (persona == "dualsense") neutral[6] = (byte)(counter & 0xFF);
                    else
                    {
                        neutral[6] = (byte)(((counter & 0x3F) << 2) & 0xFF);
                        var ts = (ushort)(counter * 1500);
                        neutral[9] = (byte)(ts & 0xFF); neutral[10] = (byte)(ts >> 8);
                    }
                }
                controller.SubmitRawReport(neutral);
                frames++; counter++;
                var target = counter * 8.0;
                var wait = target - sw.Elapsed.TotalMilliseconds;
                if (wait > 1) Thread.Sleep((int)wait);
            }
        });
        Thread.Sleep(700); // 让设备先稳定上线

        string probeVerdict = "(skipped)";
        if (startProbeSec > 0) probeVerdict = StartMenuProbe(startProbeSec);
        else Thread.Sleep((int)(holdSec * 1000));

        cts.Cancel();
        try { pump.Wait(2000); } catch { }
        Log($"  frames submitted={frames} verdict={probeVerdict}");
        controller.Dispose();
        context.Dispose();
        Thread.Sleep(900); // 让注入的按键状态有机会回落，便于观察"是否遗留按住"
        var postKeys = SnapshotDown();
        Log($"  keys down AFTER dispose+0.9s: [{(postKeys.Count == 0 ? "-" : string.Join(",", postKeys))}]");
        Log($"[{DateTime.Now:HH:mm:ss.fff}] DISPOSED (reversible: no system change left behind)");
        return 0;
    }

    // ---- 逐轴注入（按"声明 usage → 负载偏移"，随 mode 的声明序） ----
    private static void ApplySet(byte[] frame, string persona, string mode, string spec)
    {
        if (string.IsNullOrWhiteSpace(spec)) return;
        Dictionary<string, int> map;
        if (persona == "dualsense")
            map = mode == "original"
                ? new() { ["X"] = 0, ["Y"] = 1, ["Z"] = 2, ["Rz"] = 3, ["Rx"] = 4, ["Ry"] = 5 }
                : new() { ["X"] = 0, ["Y"] = 1, ["Rx"] = 2, ["Ry"] = 3, ["Z"] = 4, ["Rz"] = 5 };
        else
            map = mode == "original"
                ? new() { ["X"] = 0, ["Y"] = 1, ["Z"] = 2, ["Rz"] = 3, ["Rx"] = 7, ["Ry"] = 8 }
                : new() { ["X"] = 0, ["Y"] = 1, ["Rx"] = 2, ["Ry"] = 3, ["Z"] = 7, ["Rz"] = 8 };
        foreach (var part in spec.Split(',', StringSplitOptions.RemoveEmptyEntries))
        {
            var kv = part.Split('=');
            var name = kv[0].Trim();
            var val = Convert.ToByte(kv[1].Trim(), 16);
            if (!map.TryGetValue(name, out var off)) throw new InvalidOperationException("unknown axis name: " + name);
            frame[off] = val;
            Log($"  set {name} -> payload[{off}] = 0x{val:X2}");
        }
    }

    // ---- VK_GAMEPAD 按键状态采样（差分：前 / 过程转移 / 后） ----
    private static long _t0;
    private static void StartKeySampler(CancellationToken token)
    {
        Task.Run(() =>
        {
            var local = new Dictionary<string, bool>();
            while (!token.IsCancellationRequested)
            {
                foreach (var (vk, name) in GamepadVks)
                {
                    var down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    local.TryGetValue(name, out var was);
                    if (down != was)
                    {
                        Log($"    key {(down ? "DOWN" : "UP  ")} {name} @+{(Environment.TickCount64 - _t0) / 1000.0:F2}s");
                        local[name] = down;
                    }
                }
                Thread.Sleep(60);
            }
        });
    }

    private static List<string> SnapshotDown()
    {
        var list = new List<string>();
        foreach (var (vk, name) in GamepadVks) if ((GetAsyncKeyState(vk) & 0x8000) != 0) list.Add(name);
        return list;
    }

    // ---- 描述符 usage 交换（只动标签，不动顺序/长度） ----
    private static (string, string) SwapDescriptorUsages(string profileId, string hex)
    {
        if (profileId == "dualsense")
        {
            const string from = "093009310932093509330934"; // X,Y,Z,Rz,Rx,Ry
            const string to = "093009310933093409320935";   // X,Y,Rx,Ry,Z,Rz
            var n = CountOccurrences(hex, from);
            if (n != 1) throw new InvalidOperationException($"dualsense: expected 1 input axis block, found {n}");
            return (hex.Replace(from, to), $"dualsense input axis block: 1 replacement (len unchanged {hex.Length / 2}B)");
        }
        else
        {
            const string fromA = "0930093109320935";      // X,Y,Z,Rz (4 bytes)
            const string toA = "0930093109330934";        // X,Y,Rx,Ry
            const string fromB = "050109330934";          // Rx,Ry trigger item
            const string toB = "050109320935";            // Z,Rz trigger item
            var na = CountOccurrences(hex, fromA); var nb = CountOccurrences(hex, fromB);
            if (na != 1) throw new InvalidOperationException($"dualshock4: expected 1 stick block, found {na}");
            if (nb != 1) throw new InvalidOperationException($"dualshock4: expected 1 trigger item, found {nb}");
            var swapped = hex.Replace(fromA, toA).Replace(fromB, toB);
            return (swapped, $"ds4 input items: 2 replacements (stick {fromA}->{toA}, trigger {fromB}->{toB}; len unchanged {hex.Length / 2}B)");
        }
    }

    private static Dictionary<string, string> SwapAxisMap(Dictionary<string, string> map)
    {
        // 语义跟着"字节位置"走：原 Z(0x32)→ 新 Rx(0x33) 仍是 rightStickX，依此类推
        var pairs = new[] { (UsageZ, UsageRx), (UsageRz, UsageRy), (UsageRx, UsageZ), (UsageRy, UsageRz) };
        var result = new Dictionary<string, string>();
        foreach (var (oldU, newU) in pairs)
        {
            var oldKey = "0x" + oldU.ToString("X2");
            if (map.TryGetValue(oldKey, out var semantic)) result["0x" + newU.ToString("X2")] = semantic;
        }
        return result;
    }

    private static int CountOccurrences(string haystack, string needle)
    {
        var count = 0; var idx = 0;
        while ((idx = haystack.IndexOf(needle, idx, StringComparison.Ordinal)) >= 0) { count++; idx += needle.Length; }
        return count;
    }

    // ---- 中性帧（Sony 语义，静止态）：右摇杆 0x80 居中、扳机 0 ----
    private static byte[] NeutralDualSense()
    {
        var d = new byte[63];
        d[0] = 0x80; d[1] = 0x80; d[2] = 0x80; d[3] = 0x80; // LX LY RX RY
        d[4] = 0x00; d[5] = 0x00;                           // L2 R2
        d[6] = 0x00;                                        // seq
        d[7] = 0x08;                                        // hat neutral (low nibble)
        d[8] = 0x00; d[9] = 0x00;                           // buttons / PS+touch+mute
        d[32] = 0x81; d[36] = 0x82;                         // touch: disabled finger ids
        d[52] = 0x0A;                                       // battery 10/10
        return d;
    }

    private static byte[] NeutralDs4V2()
    {
        var d = new byte[63];
        d[0] = 0x80; d[1] = 0x80; d[2] = 0x80; d[3] = 0x80; // LX LY RX RY
        d[4] = 0x08;                                        // hat neutral (low nibble) + face buttons 0
        d[5] = 0x00; d[6] = 0x00;                           // buttons / PS+touch+counter
        d[7] = 0x00; d[8] = 0x00;                           // L2 R2 analog
        d[29] = 0x0A;                                       // battery 10/10
        d[34] = 0x81; d[38] = 0x82;                         // touch finger ids
        return d;
    }

    // ---- 开始菜单行为探针（对齐 selfloop 的 startClasses / VK_LWIN tap 口径） ----
    private static readonly string[] StartClasses = { "DV2ControlHost", "Windows.UI.Core.CoreWindow", "XamlExplorerHostIslandWindow", "SearchHost", "Start" };
    private static readonly (int Vk, string Name)[] GamepadVks =
    {
        (0xC3,"A"),(0xC4,"B"),(0xC5,"X"),(0xC6,"Y"),(0xC7,"RShoulder"),(0xC8,"LShoulder"),
        (0xC9,"LTrigger"),(0xCA,"RTrigger"),(0xCB,"L3"),(0xCC,"R3"),
        (0xCD,"DUp"),(0xCE,"DDown"),(0xCF,"DLeft"),(0xD0,"DRight"),
        (0xD1,"LS_Up"),(0xD2,"LS_Down"),(0xD3,"LS_Left"),(0xD4,"LS_Right"),
        (0xD7,"RS_Up"),(0xD8,"RS_Down"),(0xD9,"RS_Left"),(0xDA,"RS_Right"),
        (0xDB,"Menu"),(0xDC,"View"),
    };

    private static string StartMenuProbe(double seconds)
    {
        var seen = new List<string>();
        TapWinKey();
        var opened = false;
        var sw = Stopwatch.StartNew();
        while (sw.Elapsed.TotalSeconds < 2.0)
        {
            TrackVks(seen);
            if (StartClasses.Contains(FgClass())) { opened = true; break; }
            Thread.Sleep(40);
        }
        if (!opened)
        {
            TapWinKey(); Thread.Sleep(900); TapWinKey();
            sw.Restart();
            while (sw.Elapsed.TotalSeconds < 2.0) { TrackVks(seen); if (StartClasses.Contains(FgClass())) { opened = true; break; } Thread.Sleep(40); }
        }
        if (!opened) return $"INCONCLUSIVE (VK_LWIN tap did not open menu; fg={FgClass()}) vks=[{string.Join(",", seen.Distinct())}]";

        double? closedAfter = null; sw.Restart();
        var mouse = new HashSet<string>();
        string closedBy = "";
        string prevPos = "";
        while (sw.Elapsed.TotalSeconds < seconds)
        {
            TrackVks(seen);
            if (GetCursorPos(out var pt))
            {
                var pos = $"{pt.X},{pt.Y}";
                if (pos != prevPos) { if (prevPos.Length > 0) mouse.Add(pos); prevPos = pos; }
            }
            if (!StartClasses.Contains(FgClass())) { closedAfter = sw.Elapsed.TotalSeconds; closedBy = FgClass(); break; }
            Thread.Sleep(30);
        }
        var verdict = closedAfter is null ? $"STAYED-OPEN {seconds}s+" : $"CLOSED-AFTER {closedAfter:F2}s closedBy='{closedBy}' mouseMoves={mouse.Count}";
        return $"{verdict} vks=[{string.Join(",", seen.Distinct())}]";
    }

    private static void TrackVks(List<string> seen)
    {
        foreach (var (vk, name) in GamepadVks)
            if ((GetAsyncKeyState(vk) & 0x8000) != 0 && !seen.Contains(name)) seen.Add(name);
    }

    private static string FgClass()
    {
        var h = GetForegroundWindow();
        if (h == IntPtr.Zero) return "";
        var sb = new StringBuilder(256);
        GetClassName(h, sb, sb.Capacity);
        return sb.ToString();
    }

    private static void TapWinKey()
    {
        keybd_event(0x5B, 0, 0, UIntPtr.Zero);
        keybd_event(0x5B, 0, 2, UIntPtr.Zero);
    }

    private static string Sha256Hex(byte[] data) => Convert.ToHexString(SHA256.HashData(data));

    private static void Log(string line)
    {
        Console.WriteLine(line);
        Console.Out.Flush();
    }

    [DllImport("user32.dll")] private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int X; public int Y; }
}