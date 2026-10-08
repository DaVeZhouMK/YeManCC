using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using HIDMaestro;

// Empirical wire probe for HIDMaestro steam-deck-composite: submits states
// through the real controller and reads back the actual 64-byte Neptune frame
// from the HID device. Verifies DPad/trigger-digital button bits and axis/IMU
// byte placement against HC SteamDeckTarget.BuildReport.
internal static class Program
{
    private const int BufferSize = 64;
    private static Guid HidGuid = new Guid(0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

    [StructLayout(LayoutKind.Sequential)]
    private struct SpiDevInterfaceData
    {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern IntPtr SetupDiGetClassDevs(ref Guid ClassGuid, string Enumerator, IntPtr hwndParent, uint Flags);
    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool SetupDiEnumDeviceInterfaces(IntPtr DeviceInfoSet, IntPtr DeviceInfoData, ref Guid InterfaceClassGuid, uint MemberIndex, IntPtr DeviceInterfaceData);
    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr DeviceInfoSet, IntPtr DeviceInterfaceData, IntPtr DeviceInterfaceDetailData, uint DeviceInterfaceDetailDataSize, out uint RequiredSize, IntPtr DeviceInfoData);
    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);
    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_GetInputReport(IntPtr HidDeviceObject, byte[] ReportBuffer, uint ReportBufferLength);

    private static void Main()
    {
        var ctx = new HMContext();
        ctx.LoadDefaultProfiles();
        var profile = ctx.GetProfile("steam-deck-composite") ?? throw new InvalidOperationException("no profile");
        // Print the codec's canonical axis set for this profile (StandardAxes).
        var sa = HIDMaestro.HMGamepadStateHelpers.StandardAxes(profile, 1f, 1f, 1f, 1f, 1f, 1f);
        Console.WriteLine("STANDARD_AXES: " + string.Join(" ; ", sa.Select(kv => kv.Key + "=" + kv.Value)));
        var controller = ctx.CreateController(profile);
        Console.WriteLine("CTRL_OK " + controller.Profile.Id);

        HMGamepadState Make(float x, float y, float rx, float ry, float z, float rz,
            HMButton buttons = HMButton.None, HMHat hat = HMHat.None,
            short gp = 0, short gy = 0, short gr = 0, short ax = 0, short ay = 0, short az = 0)
        {
            return new HMGamepadState
            {
                Buttons = buttons,
                Hat = hat,
                Axes = new Dictionary<HMAxis, float>
                {
                    [HMAxis.X] = x, [HMAxis.Y] = y, [HMAxis.Rx] = rx, [HMAxis.Ry] = ry,
                    [HMAxis.Z] = z, [HMAxis.Rz] = rz,
                },
                GyroPitch = gp, GyroYaw = gy, GyroRoll = gr,
                AccelX = ax, AccelY = ay, AccelZ = az,
            };
        }

        void Dump(string label, HMGamepadState state)
        {
            controller.SubmitState(ref state);
            System.Threading.Thread.Sleep(60);
            // Dump any byte[] buffers the controller holds (the last encoded frame).
            var seen = new System.Collections.Generic.List<string>();
            foreach (var f in controller.GetType().GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance))
            {
                object? v = null;
                try { v = f.GetValue(controller); } catch { }
                if (v is byte[] arr && arr.Length >= 24)
                {
                    var meaningful = false;
                    foreach (var b in arr) if (b != 0) { meaningful = true; break; }
                    seen.Add(f.Name + "=" + (meaningful ? BitConverter.ToString(arr.Take(64).ToArray()) : "all-zero"));
                }
            }
            Console.WriteLine(label + " | ctrl-fields: " + (seen.Count == 0 ? "none" : string.Join(" ; ", seen)));
        }

        var neutral = Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f);
        Dump("NEUTRAL", neutral);
        Dump("HAT_N", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f, hat: HMHat.North));
        Dump("HAT_E", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f, hat: HMHat.East));
        Dump("HAT_S", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f, hat: HMHat.South));
        Dump("HAT_W", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f, hat: HMHat.West));
        Dump("ABXY_LB_RB", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            buttons: HMButton.A | HMButton.B | HMButton.X | HMButton.Y | HMButton.LeftBumper | HMButton.RightBumper));
        Dump("BACK_START_GUIDE", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            buttons: HMButton.Back | HMButton.Start | HMButton.Guide));
        Dump("STICKS_CLICK", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            buttons: HMButton.LeftStick | HMButton.RightStick));
        Dump("PADDLES", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            buttons: HMButton.LeftPaddle | HMButton.RightPaddle));
        Dump("MISC1_SHARE", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            buttons: HMButton.Misc1 | HMButton.Share));
        Dump("TRIG_FULL_Z", Make(0.5f, 0.5f, 0.5f, 0.5f, 1f, 1f));
        Dump("TRIG_HALF_Z", Make(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f));
        Dump("STICK_FULL", Make(1f, 1f, 1f, 1f, 0f, 0f));
        Dump("STICK_NEG", Make(-1f, -1f, -1f, -1f, 0f, 0f));
        Dump("IMU", Make(0.5f, 0.5f, 0.5f, 0.5f, 0f, 0f,
            gp: 0x1234, gy: 0x5678, gr: unchecked((short)0xF000), ax: 0x1111, ay: 0x2222, az: 0x3333));
        Console.WriteLine("DONE");
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint CM_Get_Device_Interface_List(ref Guid InterfaceClassGuid, string DeviceID, char[] Buffer, uint BufferLen, uint ulFlags);

    private static List<string> FindDevicePaths()
    {
        var result = new List<string>();
        uint bufLen = 8192;
        var buf = new char[bufLen];
        var cr = CM_Get_Device_Interface_List(ref HidGuid, null, buf, bufLen, 0);
        if (cr == 26) // CR_BUFFER_SMALL
        {
            bufLen = 65536;
            buf = new char[bufLen];
            cr = CM_Get_Device_Interface_List(ref HidGuid, null, buf, bufLen, 0);
        }
        if (cr != 0) return result;
        // multi-sz: null-terminated strings, double null terminates the list
        var cur = new System.Text.StringBuilder();
        var ended = false;
        foreach (var c in buf)
        {
            if (c == '\0')
            {
                if (cur.Length > 0)
                {
                    var s = cur.ToString();
                    cur.Clear();
                    if (s.Contains("vid_28de", StringComparison.OrdinalIgnoreCase) &&
                        s.Contains("pid_1205", StringComparison.OrdinalIgnoreCase))
                        result.Add(s);
                }
                else if (result.Count > 0 || ended) break;
                else ended = true;
            }
            else { ended = false; cur.Append(c); }
        }
        return result;
    }
}
