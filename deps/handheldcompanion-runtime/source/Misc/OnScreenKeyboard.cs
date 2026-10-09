using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using static HandheldCompanion.WinAPI;

namespace HandheldCompanion.Misc
{
    public static class OnScreenKeyboard
    {
        static OnScreenKeyboard()
        {
            var version = Environment.OSVersion.Version;
            switch (version.Major)
            {
                case 6:
                    switch (version.Minor)
                    {
                        case 2:
                            // Windows 10 (ok)
                            break;
                    }
                    break;
                default:
                    break;
            }
        }

        private static void StartTabTip()
        {
            var p = Process.Start(@"C:\Program Files\Common Files\Microsoft Shared\ink\TabTip.exe");
            nint handle = IntPtr.Zero;
            while ((handle = FindWindow("IPTIP_Main_Window", string.Empty)) == IntPtr.Zero)
            {
                Thread.Sleep(100);
            }
        }

        public static void ToggleVisibility()
        {
            try
            {
                Type? type = Type.GetTypeFromCLSID(Guid.Parse("4ce576fa-83dc-4F88-951c-9d0782b4e376"));
                if (type is null)
                    return;

                ITipInvocation? instance = (ITipInvocation?)Activator.CreateInstance(type);
                if (instance is null)
                    return;

                instance.Toggle(GetDesktopWindow());
                Marshal.ReleaseComObject(instance);
            }
            catch { }
        }

        public static void Show()
        {
            nint handle = FindWindow("IPTIP_Main_Window", string.Empty);
            if (handle == IntPtr.Zero) // nothing found
            {
                StartTabTip();
                Thread.Sleep(100);
            }
            // on some devices starting TabTip don't show keyboard, on some does  ¯\_(ツ)_/¯
            if (!IsOpen())
            {
                ToggleVisibility();
            }
        }

        public static void Hide()
        {
            if (IsOpen())
            {
                ToggleVisibility();
            }
        }


        public static bool Close()
        {
            // find it
            nint handle = FindWindow("IPTIP_Main_Window", string.Empty);
            bool active = handle != IntPtr.Zero;
            if (active)
            {
                // don't check style - just close
                SendMessage(handle, WM_SYSCOMMAND, (IntPtr)SC_CLOSE, IntPtr.Zero);
            }
            return active;
        }

        public static bool IsOpen()
        {
            return GetIsOpen1709() ?? GetIsOpenLegacy();
        }

        private static bool? GetIsOpen1709()
        {
            // if there is a top-level window - the keyboard is closed
            nint wnd = FindWindowEx(IntPtr.Zero, IntPtr.Zero, WindowClass1709, WindowCaption1709);
            if (wnd != IntPtr.Zero)
                return false;

            nint parent = IntPtr.Zero;
            for (; ; )
            {
                parent = FindWindowEx(IntPtr.Zero, parent, WindowParentClass1709);
                if (parent == IntPtr.Zero)
                    return null; // no more windows, keyboard state is unknown

                // if it's a child of a WindowParentClass1709 window - the keyboard is open
                wnd = FindWindowEx(parent, IntPtr.Zero, WindowClass1709, WindowCaption1709);
                if (wnd != IntPtr.Zero)
                    return true;
            }
        }

        private static bool GetIsOpenLegacy()
        {
            nint wnd = FindWindowEx(IntPtr.Zero, IntPtr.Zero, WindowClass);
            if (wnd == IntPtr.Zero)
                return false;

            WindowStyle style = GetWindowStyle(wnd);
            return style.HasFlag(WindowStyle.Visible) && !style.HasFlag(WindowStyle.Disabled);
        }

        private const string WindowClass = "IPTip_Main_Window";
        private const string WindowParentClass1709 = "ApplicationFrameWindow";
        private const string WindowClass1709 = "Windows.UI.Core.CoreWindow";
        private const string WindowCaption1709 = "Microsoft Text Input Application";

        private enum WindowStyle : uint
        {
            Disabled = 0x08000000,
            Visible = 0x10000000,
        }

        private static WindowStyle GetWindowStyle(IntPtr wnd)
        {
            return (WindowStyle)GetWindowLong(wnd, GWL_STYLE);
        }

    }


    [ComImport]
    [Guid("37c994e7-432b-4834-a2f7-dce1f13b834b")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ITipInvocation
    {
        void Toggle(IntPtr hwnd);
    }
}
