using System;
using System.Reflection;
using System.Threading;
using HIDMaestro;

namespace YeManInputHost.Diagnostics;

// The 2.0 clock closes its own event/timer. There is no _idleCts to dispose.
// Keep the existing capture/return receipt for Host release ordering, and share
// the same stop proof with raw-report ownership. Never write an SDK field.
internal static class HostControllerIdleCtsRelease
{
    private const BindingFlags Fields = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
    internal interface IRuntime
    {
        Type ControllerType { get; }
        Type ClockInterfaceType { get; }
        Type ReaderType { get; }
        bool IsPinnedCore(Type type);
        bool IsPinnedClock(Type type);
        int ReadJoinBudgetMilliseconds(Type type);
        bool IsCurrentReader(object reader);
        ThreadState ReadThreadState(object reader);
        bool HasTerminated(object reader);
        object ReadClockThread(object clock);
        void StopClock(object clock, int milliseconds);
    }

    internal static bool TryStopInputClock(HMController controller, out string detail)
    {
        detail = "sdk-idle-clock-binding-failed";
        try { return TryStopInputClockAgainstRuntime(controller, new SdkRuntime(), out detail); }
        catch { return false; }
    }

    internal static bool TryStopInputClockAgainstRuntime(object? controller, IRuntime runtime, out string detail) =>
        TryCaptureAgainstRuntime(controller, runtime, out _, out detail);

    internal static bool TryCapture(HMController controller, out Captured? captured, out string detail)
    {
        captured = null;
        detail = "sdk-idle-clock-binding-failed";
        try { return TryCaptureAgainstRuntime(controller, new SdkRuntime(), out captured, out detail); }
        catch { return false; }
    }

    internal static bool TryCaptureAgainstRuntime(object? controller, IRuntime runtime, out Captured? captured, out string detail)
    {
        captured = null;
        detail = "sdk-idle-clock-binding-failed";
        try
        {
            if (controller is null) { detail = "sdk-idle-clock-controller-missing"; return false; }
            var type = controller.GetType();
            if (type != runtime.ControllerType || !type.IsClass || !type.IsSealed || type.IsGenericType || !runtime.IsPinnedCore(type))
            { detail = "sdk-idle-clock-core-identity-mismatch"; return false; }
            var clockType = runtime.ClockInterfaceType;
            var clockField = Field(type, "_idleClock", clockType);
            var threadField = Field(type, "_idleThread", runtime.ReaderType);
            var disposedField = Field(type, "_disposed", typeof(bool));
            var stop = clockType.GetMethod("Stop", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly,
                binder: null, types: new[] { typeof(int) }, modifiers: null);
            var getter = clockType.GetProperty("Thread", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)?.GetMethod;
            if (clockField is null || threadField is null || disposedField is null || !clockType.IsInterface ||
                stop is null || stop.DeclaringType != clockType || !stop.IsAbstract || stop.IsGenericMethod || stop.ReturnType != typeof(void) ||
                getter is null || getter.DeclaringType != clockType || !getter.IsAbstract || getter.ReturnType != runtime.ReaderType || getter.GetParameters().Length != 0)
            { detail = "sdk-idle-clock-field-contract-mismatch"; return false; }
            var clock = clockField.GetValue(controller);
            var thread = threadField.GetValue(controller);
            if (disposedField.GetValue(controller) is not false)
            { detail = "sdk-idle-clock-controller-disposed"; return false; }
            if (clock is null)
            {
                if (thread is not null) { detail = "sdk-idle-clock-thread-without-clock"; return false; }
                captured = new Captured(controller, null, null, clockField, threadField, disposedField, runtime);
                detail = "sdk-idle-clock-not-created";
                return true;
            }
            if (!clockType.IsInstanceOfType(clock) || !runtime.IsPinnedClock(clock.GetType()) || thread is null ||
                thread.GetType() != runtime.ReaderType || !ReferenceEquals(runtime.ReadClockThread(clock), thread))
            { detail = "sdk-idle-clock-field-value-mismatch"; return false; }
            if (runtime.IsCurrentReader(thread)) { detail = "sdk-idle-clock-self-join"; return false; }
            detail = "sdk-idle-clock-thread-state-unknown";
            var state = runtime.ReadThreadState(thread);
            bool unstarted = (state & ~ThreadState.Background) == ThreadState.Unstarted;
            if ((state & ThreadState.Unstarted) != 0 && !unstarted) return false;
            detail = "sdk-idle-clock-budget-unknown";
            int budget = runtime.ReadJoinBudgetMilliseconds(type);
            if (budget <= 0 || budget > HostControllerOutputStop.MaximumJoinMilliseconds) return false;
            detail = "sdk-idle-clock-stop-failed";
            runtime.StopClock(clock, budget); // DeviceClock.Stop owns cancellation, wake and cleanup.
            detail = "sdk-idle-clock-identity-changed";
            if (!ReferenceEquals(clockField.GetValue(controller), clock) || !ReferenceEquals(threadField.GetValue(controller), thread)) return false;
            detail = "sdk-idle-clock-timeout";
            if (!Stopped(runtime, thread, unstarted)) return false;
            captured = new Captured(controller, clock, thread, clockField, threadField, disposedField, runtime);
            detail = unstarted ? "sdk-idle-clock-never-started" : "sdk-idle-clock-stopped";
            return true;
        }
        catch { return false; }
    }

    private static bool Stopped(IRuntime runtime, object reader, bool allowUnstarted)
    {
        if (runtime.IsCurrentReader(reader)) return false;
        var state = runtime.ReadThreadState(reader);
        if ((state & ThreadState.Unstarted) != 0)
            return allowUnstarted && (state & ~ThreadState.Background) == ThreadState.Unstarted;
        return runtime.HasTerminated(reader); // Join(0), not IsAlive(false).
    }

    private static FieldInfo? Field(Type owner, string name, Type fieldType)
    {
        var field = owner.GetField(name, Fields);
        return field is not null && field.DeclaringType == owner && field.IsPrivate && !field.IsStatic &&
            !field.IsInitOnly && field.FieldType == fieldType ? field : null;
    }

    internal sealed class Captured
    {
        private readonly object _gate = new();
        private readonly object _controller;
        private readonly object? _clock, _reader;
        private readonly FieldInfo _clockField, _readerField, _disposedField;
        private readonly IRuntime _runtime;
        private bool _disposeReturned;
        internal Captured(object controller, object? clock, object? reader, FieldInfo clockField,
            FieldInfo readerField, FieldInfo disposedField, IRuntime runtime)
        {
            _controller = controller; _clock = clock; _reader = reader; _clockField = clockField;
            _readerField = readerField; _disposedField = disposedField; _runtime = runtime;
        }
        internal void MarkControllerDisposeReturned() { lock (_gate) _disposeReturned = true; }
        internal bool TryRelease(out string detail)
        {
            lock (_gate)
            {
                detail = "sdk-idle-clock-dispose-not-returned";
                if (!_disposeReturned) return false;
                try
                {
                    detail = "sdk-idle-clock-captured-identity-changed";
                    if (!ReferenceEquals(_clockField.GetValue(_controller), _clock) || !ReferenceEquals(_readerField.GetValue(_controller), _reader) ||
                        _disposedField.GetValue(_controller) is not true) return false;
                    detail = "sdk-idle-clock-reader-not-stopped";
                    if (_reader is not null && !Stopped(_runtime, _reader, allowUnstarted: true)) return false;
                    detail = "sdk-idle-clock-release-complete";
                    return true; // The SDK owns and has closed the clock's resources.
                }
                catch { return false; }
            }
        }
    }

    private sealed class SdkRuntime : IRuntime
    {
        public Type ControllerType => typeof(HMController);
        public Type ClockInterfaceType => typeof(HMController).Assembly.GetType("HIDMaestro.Internal.IDeviceClock", throwOnError: true)!;
        public Type ReaderType => typeof(Thread);
        public bool IsPinnedCore(Type type) => HostControllerOutputStop.IsPinnedCore(type);
        public bool IsPinnedClock(Type type) => type.Assembly == typeof(HMController).Assembly && type.IsSealed &&
            type.IsGenericType && type.GetGenericTypeDefinition().FullName == "HIDMaestro.Internal.DeviceClock`1" &&
            type.GetGenericArguments().Length == 1 && type.GetGenericArguments()[0].FullName == "HIDMaestro.Internal.StreamTick";
        public int ReadJoinBudgetMilliseconds(Type type) => HostControllerOutputStop.ReadJoinBudget(type);
        public bool IsCurrentReader(object reader) => ReferenceEquals(reader, Thread.CurrentThread);
        public ThreadState ReadThreadState(object reader) => ((Thread)reader).ThreadState;
        public bool HasTerminated(object reader) => ((Thread)reader).Join(0);
        public object ReadClockThread(object clock) => ClockInterfaceType.GetProperty("Thread")!.GetValue(clock)!;
        public void StopClock(object clock, int ms) => ClockInterfaceType.GetMethod("Stop")!.Invoke(clock, new object[] { ms });
    }
}