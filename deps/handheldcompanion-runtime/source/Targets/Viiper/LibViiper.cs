using System;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace HandheldCompanion.Targets.Viiper
{
    internal static class LibViiper
    {
        private const string DllName = "libviiper";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int viiper_init(string listenAddr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void viiper_shutdown();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_bus_create(uint busId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_bus_remove(uint busId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int viiper_device_add(uint busId, string typeName, out uint deviceId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int viiper_device_add_ex(uint busId, string typeName, ushort vid, ushort pid, out uint deviceId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_device_remove(uint busId, uint deviceId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_device_attach(uint busId, uint deviceId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr viiper_list_device_types();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_device_set_input(uint busId, uint deviceId, byte[] data, int len);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void FeedbackCallback(uint busId, uint deviceId, IntPtr data, int len, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int viiper_device_set_feedback_callback(uint busId, uint deviceId, FeedbackCallback cb, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr viiper_last_error();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void viiper_free_string(IntPtr s);

        public static string? GetLastError()
        {
            IntPtr ptr = viiper_last_error();
            if (ptr == IntPtr.Zero)
                return null;

            try
            {
                return Marshal.PtrToStringAnsi(ptr);
            }
            finally
            {
                viiper_free_string(ptr);
            }
        }

        public static string[] GetDeviceTypes()
        {
            IntPtr ptr = viiper_list_device_types();
            if (ptr == IntPtr.Zero)
                return Array.Empty<string>();

            try
            {
                string? json = Marshal.PtrToStringAnsi(ptr);
                if (string.IsNullOrEmpty(json))
                    return Array.Empty<string>();

                return JsonSerializer.Deserialize<string[]>(json) ?? Array.Empty<string>();
            }
            finally
            {
                viiper_free_string(ptr);
            }
        }
    }
}
