$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class DisplayPower
{
    private const uint WS_POPUP = 0x80000000u;
    private const uint WM_SYSCOMMAND = 0x0112;
    private const uint SC_MONITORPOWER = 0xF170;
    private const int MonitorOff = 2;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowEx(uint extendedStyle, string className, string windowName,
        uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr data);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr DefWindowProc(IntPtr window, uint message, UIntPtr command, IntPtr value);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr window);

    public static void RequestOff()
    {
        // Use an owned window so the request does not broadcast to other apps.
        IntPtr window = CreateWindowEx(0, "STATIC", null, WS_POPUP, 0, 0, 0, 0,
            IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        if (window == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            DefWindowProc(window, WM_SYSCOMMAND, new UIntPtr(SC_MONITORPOWER), new IntPtr(MonitorOff));
        }
        finally
        {
            if (!DestroyWindow(window))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }
}
"@

[DisplayPower]::RequestOff()
