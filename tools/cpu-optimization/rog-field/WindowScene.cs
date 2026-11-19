// Development-only, explicitly invoked external observer. No target UI writes,
// input hooks, payload capture, process memory reads, or injected callbacks.
using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace Ymcc.CpuScene.V1
{
    public sealed class Identity
    {
        public uint ProcessId;
        public string BirthFileTime, ImagePath, Error;
        public bool Alive, HandleCloseAttempted, HandleClosed;
    }
    public struct Sample
    {
        // Kind: 0 initial, 1 heartbeat, 2 closing, 3 WinEvent callback receipt.
        public int Kind, ObjectId, ChildId;
        public uint EventId, EventThreadId, EventTimeMs, ForegroundProcessId, WindowProcessId, WindowThreadId;
        public long BeforeQpc, AfterQpc, ForegroundWindow;
        public bool WindowExists, Visible, Minimized;
    }
    public sealed class Trace
    {
        public string Status = "NOT_STARTED", Error;
        public uint ProcessId;
        public string BirthFileTime, ImagePath;
        public long Window, Frequency, StartedQpc, EndedQpc, AnchorBeforeQpc, AnchorAfterQpc, AnchorUtcTicks;
        public int Count, Capacity, IgnoredObjectEvents;
        public Sample[] Samples;
        public bool CapacityExceeded, HandleCloseAttempted, HandleClosed;
        public bool[] HookCloseAttempted = new bool[3], HookClosed = new bool[3];
    }
    public static class Observer
    {
        [StructLayout(LayoutKind.Sequential)] private struct FileTime { public uint Low, High; public ulong Value { get { return ((ulong)High << 32) | Low; } } }
        [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] private struct Message { public IntPtr Window; public uint Id; public UIntPtr WParam; public IntPtr LParam; public uint Time; public Point Pt; public uint Private; }
        private delegate void EventProc(IntPtr hook, uint eventId, IntPtr hwnd, int objectId, int childId, uint eventThread, uint eventTime);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool GetProcessTimes(IntPtr handle, out FileTime born, out FileTime exited, out FileTime kernel, out FileTime user);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool QueryFullProcessImageName(IntPtr handle, uint flags, StringBuilder name, ref uint size);
        [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll", SetLastError=true)] private static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr module, EventProc callback, uint pid, uint tid, uint flags);
        [DllImport("user32.dll", SetLastError=true)] private static extern bool UnhookWinEvent(IntPtr hook);
        [DllImport("user32.dll", SetLastError=true)] private static extern uint MsgWaitForMultipleObjectsEx(uint count, IntPtr[] handles, uint milliseconds, uint mask, uint flags);
        [DllImport("user32.dll")] private static extern bool PeekMessage(out Message message, IntPtr hwnd, uint min, uint max, uint remove);
        [DllImport("user32.dll")] private static extern bool TranslateMessage(ref Message message);
        [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref Message message);

        private static Identity ReadHeld(IntPtr handle, uint pid)
        {
            var value = new Identity { ProcessId = pid };
            FileTime born, exited, kernel, user;
            if (!GetProcessTimes(handle, out born, out exited, out kernel, out user)) throw new InvalidOperationException("GetProcessTimes:" + Marshal.GetLastWin32Error());
            var path = new StringBuilder(32768); uint length = 32768;
            if (!QueryFullProcessImageName(handle, 0, path, ref length)) throw new InvalidOperationException("QueryFullProcessImageName:" + Marshal.GetLastWin32Error());
            value.BirthFileTime = born.Value.ToString(CultureInfo.InvariantCulture);
            value.ImagePath = path.ToString(); value.Alive = WaitForSingleObject(handle, 0) == 258;
            return value;
        }
        public static Identity ReadIdentity(uint pid)
        {
            var value = new Identity { ProcessId = pid }; IntPtr handle = IntPtr.Zero;
            try {
                handle = OpenProcess(0x101000, false, pid);
                if (handle == IntPtr.Zero) throw new InvalidOperationException("OpenProcess:" + Marshal.GetLastWin32Error());
                value = ReadHeld(handle, pid);
            } catch (Exception error) { value.Error = error.Message; }
            finally { if (handle != IntPtr.Zero) { value.HandleCloseAttempted = true; value.HandleClosed = CloseHandle(handle); } }
            return value;
        }
        private static void Record(Trace trace, IntPtr window, int kind, uint eventId, int objectId, int childId, uint eventThread, uint eventTime)
        {
            if (trace.Count == trace.Samples.Length) { trace.CapacityExceeded = true; return; }
            var sample = new Sample { BeforeQpc = Stopwatch.GetTimestamp(), Kind = kind, EventId = eventId,
                ObjectId = objectId, ChildId = childId, EventThreadId = eventThread, EventTimeMs = eventTime };
            var foreground = GetForegroundWindow(); sample.ForegroundWindow = foreground.ToInt64();
            uint foregroundPid, windowPid;
            GetWindowThreadProcessId(foreground, out foregroundPid); sample.ForegroundProcessId = foregroundPid;
            sample.WindowThreadId = GetWindowThreadProcessId(window, out windowPid); sample.WindowProcessId = windowPid;
            sample.WindowExists = IsWindow(window); sample.Visible = IsWindowVisible(window); sample.Minimized = IsIconic(window);
            sample.AfterQpc = Stopwatch.GetTimestamp(); trace.Samples[trace.Count++] = sample;
            if (!sample.WindowExists || windowPid != trace.ProcessId) trace.Status = "WINDOW_IDENTITY_LOST";
        }
        // Optional entry check for an explicitly requested scene. This is one
        // passive read, not a timer, hook or continuous scene guarantee.
        public static Sample ReadWindowSample(uint pid, long windowValue)
        {
            if (pid == 0 || windowValue == 0) throw new ArgumentException("Explicit PID and HWND required.");
            var trace = new Trace { ProcessId = pid, Samples = new Sample[1] };
            Record(trace, new IntPtr(windowValue), 0, 0, 0, 0, 0, 0);
            return trace.Samples[0];
        }
        public static Trace Capture(uint pid, string birth, string imagePath, long windowValue, int durationMilliseconds)
        {
            if (pid == 0 || windowValue == 0) throw new ArgumentException("Explicit PID and HWND required.");
            if (durationMilliseconds < 10000 || durationMilliseconds > 600000) throw new ArgumentOutOfRangeException("durationMilliseconds");
            ulong parsedBirth;
            if (!UInt64.TryParse(birth, NumberStyles.None, CultureInfo.InvariantCulture, out parsedBirth) || parsedBirth == 0) throw new ArgumentException("Exact FILETIME required.");
            return CaptureCore(pid, birth, imagePath, windowValue, durationMilliseconds, 32768, 0, IntPtr.Zero);
        }
        private static Trace CaptureCore(uint pid, string birth, string imagePath, long windowValue, int durationMs, int capacity, int testMode, IntPtr otherWindow)
        {
            var trace = new Trace { ProcessId = pid, BirthFileTime = birth, Window = windowValue, Frequency = Stopwatch.Frequency, Capacity = capacity, Samples = new Sample[capacity] };
            IntPtr root = IntPtr.Zero; var hooks = new IntPtr[3]; var window = new IntPtr(windowValue); EventProc callback = null;
            try {
                root = OpenProcess(0x101000, false, pid);
                if (root == IntPtr.Zero) throw new InvalidOperationException("OpenProcess:" + Marshal.GetLastWin32Error());
                var identity = ReadHeld(root, pid); trace.ImagePath = identity.ImagePath;
                if (!identity.Alive || identity.BirthFileTime != birth || !String.Equals(identity.ImagePath, imagePath, StringComparison.OrdinalIgnoreCase)) { trace.Status = "PROCESS_IDENTITY_REJECTED"; return trace; }
                uint windowPid;
                if (!IsWindow(window) || GetWindowThreadProcessId(window, out windowPid) == 0 || windowPid != pid) { trace.Status = "WINDOW_IDENTITY_REJECTED"; return trace; }
                callback = (hook, ev, hwnd, obj, child, tid, time) => {
                    try {
                        if (ev != 3 && (hwnd != window || obj != 0 || child != 0)) { ++trace.IgnoredObjectEvents; return; }
                        Record(trace, window, 3, ev, obj, child, tid, time);
                        if (ev == 0x8001 && hwnd == window) trace.Status = "WINDOW_DESTROYED";
                    } catch (Exception error) { trace.Error = error.GetType().Name; trace.Status = "CALLBACK_ERROR"; }
                };
                // OUTOFCONTEXT callbacks run on this thread's message pump; no DLL injection.
                hooks[0] = SetWinEventHook(3, 3, IntPtr.Zero, callback, 0, 0, 0);
                hooks[1] = SetWinEventHook(0x16, 0x17, IntPtr.Zero, callback, pid, 0, 0);
                hooks[2] = SetWinEventHook(0x8001, 0x8003, IntPtr.Zero, callback, pid, 0, 0);
                foreach (var hook in hooks) if (hook == IntPtr.Zero) throw new InvalidOperationException("SetWinEventHook:" + Marshal.GetLastWin32Error());
                trace.AnchorBeforeQpc = Stopwatch.GetTimestamp(); trace.AnchorUtcTicks = DateTime.UtcNow.Ticks; trace.AnchorAfterQpc = Stopwatch.GetTimestamp();
                trace.StartedQpc = Stopwatch.GetTimestamp(); trace.Status = "OBSERVING";
                long deadline = checked(trace.StartedQpc + (long)Math.Ceiling(durationMs * (trace.Frequency / 1000.0)));
                Record(trace, window, 0, 0, 0, 0, 0, 0);
#if YMCC_SCENE_SELFTEST
                if (testMode != 0) {
                    NotifyWinEvent(0x8002, otherWindow, 0, 0); NotifyWinEvent(0x8002, window, -4, 0);
                    int count = testMode == 2 ? capacity * 4 : 1;
                    for (int i = 0; i < count; ++i) { NotifyWinEvent(0x8002, window, 0, 0); NotifyWinEvent(0x8003, window, 0, 0); }
                }
#endif
                var handles = new IntPtr[] { root }; long nextHeartbeat = trace.StartedQpc + trace.Frequency;
                while (trace.Status == "OBSERVING" && !trace.CapacityExceeded) {
                    long now = Stopwatch.GetTimestamp(); if (now >= deadline) break;
                    long due = Math.Min(deadline, nextHeartbeat);
                    uint waitMs = checked((uint)Math.Max(1, Math.Ceiling((due - now) * 1000.0 / trace.Frequency)));
                    uint result = MsgWaitForMultipleObjectsEx(1, handles, waitMs, 0x04ff, 0x0004);
                    if (result == 0) { trace.Status = "PROCESS_EXITED"; break; }
                    if (result != 1 && result != 258) throw new InvalidOperationException("MsgWait:" + result + ":" + Marshal.GetLastWin32Error());
                    Message message; int dispatched = 0;
                    while (dispatched++ < 256 && PeekMessage(out message, IntPtr.Zero, 0, 0, 1)) {
                        if (message.Id == 0x12) { trace.Status = "OBSERVER_QUIT"; break; }
                        TranslateMessage(ref message); DispatchMessage(ref message);
                        if (trace.Status != "OBSERVING" || trace.CapacityExceeded || Stopwatch.GetTimestamp() >= deadline) break;
                    }
                    now = Stopwatch.GetTimestamp();
                    if (now >= nextHeartbeat && trace.Status == "OBSERVING" && !trace.CapacityExceeded) { Record(trace, window, 1, 0, 0, 0, 0, 0); nextHeartbeat = Stopwatch.GetTimestamp() + trace.Frequency; }
                }
                if (trace.CapacityExceeded) trace.Status = "CAPACITY_EXCEEDED";
                if (trace.Status == "OBSERVING") {
                    var closing = ReadHeld(root, pid);
                    if (!closing.Alive || closing.BirthFileTime != birth || !String.Equals(closing.ImagePath, imagePath, StringComparison.OrdinalIgnoreCase)) trace.Status = "CLOSING_IDENTITY_REJECTED";
                    else { Record(trace, window, 2, 0, 0, 0, 0, 0); if (trace.CapacityExceeded) trace.Status = "CAPACITY_EXCEEDED"; else if (trace.Status == "OBSERVING") trace.Status = "SCENE_COLLECTED"; }
                }
            } catch (Exception error) { trace.Error = error.ToString(); trace.Status = "CALL_ERROR"; }
            finally {
                trace.EndedQpc = Stopwatch.GetTimestamp();
                for (int i = 0; i < hooks.Length; ++i) if (hooks[i] != IntPtr.Zero) { trace.HookCloseAttempted[i] = true; trace.HookClosed[i] = UnhookWinEvent(hooks[i]); }
                if (root != IntPtr.Zero) { trace.HandleCloseAttempted = true; trace.HandleClosed = CloseHandle(root); }
                GC.KeepAlive(callback);
            }
            return trace;
        }
#if YMCC_SCENE_SELFTEST
        [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
        [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern void NotifyWinEvent(uint ev, IntPtr hwnd, int obj, int child);
        public static object SelfTest()
        {
            uint pid = (uint)Process.GetCurrentProcess().Id; var identity = ReadIdentity(pid);
            if (!identity.Alive || !identity.HandleClosed) throw new InvalidOperationException("Owned identity control failed.");
            IntPtr first = IntPtr.Zero, other = IntPtr.Zero; int checks = 0;
            try {
                first = CreateWindowEx(0, "STATIC", "", 0, 0, 0, 1, 1, new IntPtr(-3), IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                other = CreateWindowEx(0, "STATIC", "", 0, 0, 0, 1, 1, new IntPtr(-3), IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                if (first == IntPtr.Zero || other == IntPtr.Zero) throw new InvalidOperationException("Owned message-only window creation failed.");
                var initial = ReadWindowSample(pid, first.ToInt64());
                if (!initial.WindowExists || initial.WindowProcessId != pid || initial.Visible || initial.AfterQpc < initial.BeforeQpc) throw new InvalidOperationException("Initial hidden owned-window sample failed."); ++checks;
                var missing = ReadWindowSample(pid, 1);
                if (missing.WindowExists || missing.WindowProcessId != 0) throw new InvalidOperationException("Initial missing window was treated as owned."); ++checks;
                bool rejected = false;
                try { ReadWindowSample(0, first.ToInt64()); } catch (ArgumentException) { rejected = true; }
                if (!rejected) throw new InvalidOperationException("Initial sample accepted an implicit process."); ++checks;
                var normal = CaptureCore(pid, identity.BirthFileTime, identity.ImagePath, first.ToInt64(), 200, 64, 1, other);
                if (normal.Status != "SCENE_COLLECTED" || !normal.HandleClosed || Array.Exists(normal.HookClosed, x => !x)) throw new InvalidOperationException("Real WinEvent control/cleanup failed:" + normal.Status + ":" + normal.Error); ++checks;
                int shows = 0, hides = 0;
                for (int i = 0; i < normal.Count; ++i) { var s = normal.Samples[i]; if (s.Kind == 3 && s.EventId == 0x8002) ++shows; if (s.Kind == 3 && s.EventId == 0x8003) ++hides; if (s.AfterQpc < s.BeforeQpc || !s.WindowExists || s.WindowProcessId != pid || s.Visible) throw new InvalidOperationException("Numeric sample or hidden owned-window binding failed."); }
                if (shows != 1 || hides != 1 || normal.IgnoredObjectEvents < 2) throw new InvalidOperationException("Owned HWND/object filtering failed."); ++checks;
                var wrongBirth = CaptureCore(pid, (UInt64.Parse(identity.BirthFileTime) + 1).ToString(CultureInfo.InvariantCulture), identity.ImagePath, first.ToInt64(), 200, 64, 0, other);
                if (wrongBirth.Status != "PROCESS_IDENTITY_REJECTED" || wrongBirth.Count != 0 || !wrongBirth.HandleClosed || Array.Exists(wrongBirth.HookCloseAttempted, x => x)) throw new InvalidOperationException("Wrong birth admitted hook/scene."); ++checks;
                var wrongWindow = CaptureCore(pid, identity.BirthFileTime, identity.ImagePath, 1, 200, 64, 0, other);
                if (wrongWindow.Status != "WINDOW_IDENTITY_REJECTED" || wrongWindow.Count != 0 || !wrongWindow.HandleClosed) throw new InvalidOperationException("Unknown window admitted."); ++checks;
                var overflow = CaptureCore(pid, identity.BirthFileTime, identity.ImagePath, first.ToInt64(), 200, 8, 2, other);
                if (overflow.Status != "CAPACITY_EXCEEDED" || !overflow.CapacityExceeded || overflow.Count != 8 || !overflow.HandleClosed || Array.Exists(overflow.HookClosed, x => !x)) throw new InvalidOperationException("Overflow was not explicit/cleaned."); ++checks;
                if (!DestroyWindow(first) || !DestroyWindow(other)) throw new InvalidOperationException("Owned window destroy failed.");
                first = IntPtr.Zero; other = IntPtr.Zero; ++checks;
                return new { Status = "PASS", Checks = checks, RealOutOfContextHooks = true, TargetApplicationsControlled = false, OnlyMessageOnlyTestWindows = true };
            } finally { if (first != IntPtr.Zero) DestroyWindow(first); if (other != IntPtr.Zero) DestroyWindow(other); }
        }
#endif
    }
}
