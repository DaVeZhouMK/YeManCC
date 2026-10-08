using System;
using System.Linq;
using System.Runtime.InteropServices;
using HIDMaestro;

// SteamDeck claim feature-probe: reads back the 0x83 (attributes), 0xAE
// (serials, index 0/1) and 0x87 (settings echo) feature reports from the live
// steam-deck-composite device and compares them against the profile's
// featureStubs. This is the device side of Steam's claim interrogation (39号 M1).
internal static class Program
{
    private static Guid HidGuid = new Guid(0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint CM_Get_Device_Interface_List(ref Guid InterfaceClassGuid, string DeviceID, char[] Buffer, uint BufferLen, uint ulFlags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);
    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_GetFeature(IntPtr HidDeviceObject, byte[] ReportBuffer, uint ReportBufferLength);
    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_SetOutputReport(IntPtr HidDeviceObject, byte[] ReportBuffer, uint ReportBufferLength);

    private static void Main()
    {
        var ctx = new HMContext();
        ctx.LoadDefaultProfiles();
        var profile = ctx.GetProfile("steam-deck-composite") ?? throw new InvalidOperationException("no profile");
        var controller = ctx.CreateController(profile);
        Console.WriteLine("CTRL_OK");
        // usbip 设备可能在首次 SubmitState 后才完整呈现 HID 接口；先发一个
        // 中性帧并等待 PnP 稳定，与 wire 探针时序一致。
        var neutral = new HMGamepadState
        {
            Axes = new System.Collections.Generic.Dictionary<HMAxis, float>
            {
                [HMAxis.X] = 0.5f, [HMAxis.Y] = 0.5f, [HMAxis.Rx] = 0.5f, [HMAxis.Ry] = 0.5f,
                [HMAxis.Z] = 0f, [HMAxis.Rz] = 0f,
            }
        };
        controller.SubmitState(ref neutral);
        System.Threading.Thread.Sleep(500);

        // Send the claim-preamble SET_REPORT Steam performs for indexed serials.
        // 0xAE with param index: [AE][14][index]
        foreach (var idx in new byte[] { 0, 1, 2 })
        {
            var setBuf = new byte[64];
            setBuf[0] = 0xAE; setBuf[1] = 0x14; setBuf[2] = idx;
            var handle = OpenMi02();
            if (handle != IntPtr.Zero)
            {
                HidD_SetOutputReport(handle, setBuf, 64);
                CloseHandle(handle);
            }
            var buf = Feature(0xAE);
            Console.WriteLine("AE_" + idx + " | " + (buf == null ? "READ_FAIL" : BitConverter.ToString(buf)));
        }

        // Plain GET_REPORT probes
        var r83 = Feature(0x83);
        Console.WriteLine("83 | " + (r83 == null ? "READ_FAIL" : BitConverter.ToString(r83)));
        var r87 = Feature(0x87);
        Console.WriteLine("87 | " + (r87 == null ? "READ_FAIL" : BitConverter.ToString(r87)));
        var r8F = Feature(0x8F);
        Console.WriteLine("8F | " + (r8F == null ? "READ_FAIL" : BitConverter.ToString(r8F)));

        Console.WriteLine("EXPECT 83: " + BitConverter.ToString(ProfileFeature83));
        Console.WriteLine("EXPECT AE0: " + BitConverter.ToString(ProfileFeatureAe0));
        Console.WriteLine("EXPECT AE1: " + BitConverter.ToString(ProfileFeatureAe1));
        Console.WriteLine("DONE");
    }

    private static readonly byte[] ProfileFeature83 =
        { 0x83, 0x2d, 0x01, 0x05, 0x12, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x2b, 0x12, 0xa9,
          0x62, 0x04, 0xb7, 0x61, 0x7c, 0x67, 0x09, 0x2e, 0x00, 0x00, 0x00, 0x0b, 0xa0, 0x0f, 0x00, 0x00,
          0x0d, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    private static readonly byte[] ProfileFeatureAe0 =
        { 0xae, 0x14, 0x00, 0x4d, 0x48, 0x44, 0x41, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31 };
    private static readonly byte[] ProfileFeatureAe1 =
        { 0xae, 0x14, 0x01, 0x46, 0x58, 0x31, 0x41, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31 };

    private static byte[]? Feature(byte reportId)
    {
        var path = FindMi02Path();
        if (path == null) { Console.WriteLine("  NO_DEVICE"); return null; }
        var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == new IntPtr(-1)) { Console.WriteLine("  OPEN_FAIL err=" + Marshal.GetLastWin32Error()); return null; }
        try
        {
            var buf = new byte[64];
            buf[0] = reportId;
            if (!HidD_GetFeature(h, buf, 64)) { Console.WriteLine("  GETFEATURE_FAIL " + reportId.ToString("X2") + " err=" + Marshal.GetLastWin32Error()); return null; }
            var len = 64;
            while (len > 1 && buf[len - 1] == 0) len--;
            return buf.Take(len).ToArray();
        }
        finally { CloseHandle(h); }
    }

    private static IntPtr OpenMi02()
    {
        var path = FindMi02Path();
        if (path == null) return IntPtr.Zero;
        var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        return h == new IntPtr(-1) ? IntPtr.Zero : h;
    }

    private static string? FindMi02Path()
    {
        var buf = new char[8192];
        var cr = CM_Get_Device_Interface_List(ref HidGuid, null, buf, 8192, 0);
        if (cr == 26) { buf = new char[65536]; cr = CM_Get_Device_Interface_List(ref HidGuid, null, buf, 65536, 0); }
        if (cr != 0) return null;
        var cur = new System.Text.StringBuilder();
        var seenAny = false;
        var count = 0;
        foreach (var c in buf)
        {
            if (c == '\0')
            {
                if (cur.Length > 0)
                {
                    var s = cur.ToString();
                    cur.Clear();
                    seenAny = true;
                    count++;
                    if (count <= 5 || s.Contains("28de", StringComparison.OrdinalIgnoreCase))
                        Console.WriteLine("  PATH " + s.Substring(0, Math.Min(70, s.Length)));
                    if (s.Contains("vid_28de", StringComparison.OrdinalIgnoreCase) &&
                        s.Contains("pid_1205", StringComparison.OrdinalIgnoreCase) &&
                        s.Contains("mi_02", StringComparison.OrdinalIgnoreCase))
                        return s;
                }
                else if (seenAny) break;
            }
            else { seenAny = true; cur.Append(c); }
        }
        Console.WriteLine("  TOTAL_PATHS=" + count);
        return null;
    }
}
