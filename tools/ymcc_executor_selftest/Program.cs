using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

// Deliberately hardware-free. This is NOT the product and never loads FanHost,
// InputHost, WebView2, drivers, an EC library or installed YMCC payloads.
internal static class Program
{
    private delegate IntPtr WndProc(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    private static readonly WndProc Proc = WindowMessage;
    private static string? stateFile;
    private static string[] arguments = [];
    private static bool ignoreExit;
    private static int childPid;
    private const uint WM_APP_EXIT = 0x0406;
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    private struct WindowClass
    {
        public uint Size, Style; public IntPtr Proc; public int ClassExtra, WindowExtra;
        public IntPtr Instance, Icon, Cursor, Background; public string? Menu;
        public string ClassName; public IntPtr SmallIcon;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Message { public IntPtr Window; public uint Id; public IntPtr WParam, LParam; public uint Time; public int X,Y; public uint Private; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern ushort RegisterClassEx(ref WindowClass wc);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr data);
    [DllImport("user32.dll")] private static extern int GetMessage(out Message message, IntPtr window, uint min, uint max);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref Message message);
    [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern void PostQuitMessage(int code);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr DefWindowProc(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? name);
    private static void Evidence(string action)
    {
        if (stateFile is null) return;
        File.AppendAllText(stateFile, JsonSerializer.Serialize(new { scope="diagnostic-fixture-not-product", action, pid=Environment.ProcessId, childPid, timestampUtc=DateTime.UtcNow, arguments }) + Environment.NewLine);
    }
    private static IntPtr WindowMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam)
    {
        if (message is WM_APP_EXIT or 0x0010)
        {
            Evidence(ignoreExit ? "exit-ignored-for-negative-test" : "graceful-exit-received");
            if (!ignoreExit) { DestroyWindow(window); PostQuitMessage(0); }
            return IntPtr.Zero;
        }
        return DefWindowProc(window, message, wParam, lParam);
    }
    private static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--child")
        {
            try { using var parent=Process.GetProcessById(int.Parse(args[1])); parent.WaitForExit(); } catch (ArgumentException) { }
            return 0;
        }
        arguments=args;
        for (int i=0;i<args.Length;i++)
        {
            if (args[i] == "--state-file" && i+1<args.Length) stateFile=Path.GetFullPath(args[++i]);
            if (args[i] == "--ignore-exit") ignoreExit=true;
        }
        var wc=new WindowClass { Size=(uint)Marshal.SizeOf<WindowClass>(), Proc=Marshal.GetFunctionPointerForDelegate(Proc), Instance=GetModuleHandle(null), ClassName="QQ" };
        if (RegisterClassEx(ref wc)==0) return 2;
        var window=CreateWindowEx(0,"QQ","YMCC hardware-free lifecycle fixture",0,0,0,0,0,IntPtr.Zero,IntPtr.Zero,wc.Instance,IntPtr.Zero);
        if (window==IntPtr.Zero) return 3;
        var childPath=Path.Combine(AppContext.BaseDirectory,"YeManInputHost.exe");
        using var child=Process.Start(new ProcessStartInfo(childPath) { UseShellExecute=false, CreateNoWindow=true, ArgumentList={ "--child", Environment.ProcessId.ToString() } });
        childPid=child?.Id ?? 0;
        Evidence("ready");
        while (GetMessage(out var message,IntPtr.Zero,0,0)>0) DispatchMessage(ref message);
        Evidence("root-exiting");
        return 0;
    }
}