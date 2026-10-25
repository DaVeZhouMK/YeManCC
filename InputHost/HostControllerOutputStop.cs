using System;
using System.Reflection;
using System.Threading;
using HIDMaestro;

namespace YeManInputHost.Diagnostics;

// Stops only the pinned SDK's output reader. The caller retains the controller
// and context on false. Asset SHA verification remains at Host admission.
internal static class HostControllerOutputStop
{
    internal const int MaximumJoinMilliseconds = 500;
    internal const string LockedCoreMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    private const BindingFlags Fields = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    internal static bool TryStop(HMController controller, out string detail)
    {
        detail = "sdk-output-stop-binding-failed";
        try { return TryStopAgainstRuntime(controller, new SdkRuntime(), out detail); }
        catch { return false; }
    }

    // Fixtures never load the SDK. Production always binds the exact identity.
    internal interface IRuntime
    {
        Type ControllerType { get; }
        Type CancellationSourceType { get; }
        Type ReaderType { get; }
        bool IsPinnedCore(Type controllerType);
        int ReadJoinBudgetMilliseconds(Type controllerType);
        bool IsCurrentReader(object reader);
        ThreadState ReadThreadState(object reader);
        bool TryEnterCancellationLock(object gate, int milliseconds);
        void ExitCancellationLock(object gate);
        void Cancel(object cancellationSource);
        bool Join(object reader, int milliseconds);
    }

    internal static bool TryStopAgainstRuntime(object? controller, IRuntime runtime, out string detail)
    {
        detail = "sdk-output-stop-binding-failed";
        try
        {
            if (controller is null) { detail = "sdk-output-stop-controller-missing"; return false; }
            var type = controller.GetType();
            if (type != runtime.ControllerType || !type.IsClass || !type.IsSealed || type.IsGenericType || !runtime.IsPinnedCore(type))
            { detail = "sdk-output-stop-core-identity-mismatch"; return false; }
            var ctsField = Field(type, "_outputCts", runtime.CancellationSourceType, true);
            var readerField = Field(type, "_outputThread", runtime.ReaderType, true);
            var viewField = Field(type, "_outputView", typeof(IntPtr), true);
            var gateField = Field(type, "_outputCtsLock", typeof(object), true);
            var closedField = Field(type, "_outputCtsClosed", typeof(bool), false);
            if (ctsField is null || readerField is null || viewField is null || gateField is null || closedField is null)
            { detail = "sdk-output-stop-field-contract-mismatch"; return false; }
            var cts = ctsField.GetValue(controller);
            var reader = readerField.GetValue(controller);
            var gate = gateField.GetValue(controller);
            if (cts is null || cts.GetType() != runtime.CancellationSourceType || gate is null || gate.GetType() != typeof(object) ||
                viewField.GetValue(controller) is not IntPtr view || (reader is not null && reader.GetType() != runtime.ReaderType))
            { detail = "sdk-output-stop-field-value-mismatch"; return false; }
            if (reader is null && view != IntPtr.Zero)
            { detail = "sdk-output-stop-reader-missing-with-mapping"; return false; }
            bool unstarted = false;
            if (reader is not null)
            {
                if (runtime.IsCurrentReader(reader)) { detail = "sdk-output-stop-self-join"; return false; }
                detail = "sdk-output-stop-thread-state-unknown";
                var state = runtime.ReadThreadState(reader);
                unstarted = (state & ~ThreadState.Background) == ThreadState.Unstarted;
                if ((state & ThreadState.Unstarted) != 0 && !unstarted) return false;
                if (unstarted && view != IntPtr.Zero)
                { detail = "sdk-output-stop-unstarted-with-mapping"; return false; }
            }
            detail = "sdk-output-stop-budget-unknown";
            int budget = runtime.ReadJoinBudgetMilliseconds(type);
            if (budget <= 0 || budget > MaximumJoinMilliseconds) return false;
            long started = Environment.TickCount64;
            detail = "sdk-output-stop-lock-timeout";
            if (!runtime.TryEnterCancellationLock(gate, budget)) return false;
            try
            {
                // CloseOutputCts sets Closed under this same lock before it
                // disposes outside it. Never cancel that closed source. A closed
                // source alone is not proof that the reader has terminated.
                detail = "sdk-output-stop-field-value-mismatch";
                if (closedField.GetValue(controller) is not bool closed) return false;
                if (!closed)
                {
                    detail = "sdk-output-stop-cancel-failed";
                    runtime.Cancel(cts);
                }
            }
            finally { runtime.ExitCancellationLock(gate); }
            if (reader is null) { detail = "sdk-output-reader-not-created"; return true; }
            if (unstarted)
            {
                detail = "sdk-output-stop-thread-state-changed";
                if ((runtime.ReadThreadState(reader) & ~ThreadState.Background) != ThreadState.Unstarted) return false;
                detail = "sdk-output-reader-never-started";
                return true;
            }
            detail = "sdk-output-stop-join-failed";
            int remaining = (int)Math.Max(0, budget - Math.Min(budget, Environment.TickCount64 - started));
            if (!runtime.Join(reader, remaining)) { detail = "sdk-output-stop-timeout"; return false; }
            detail = "sdk-output-reader-stopped";
            return true;
        }
        catch { return false; }
    }

    private static FieldInfo? Field(Type owner, string name, Type fieldType, bool readOnly)
    {
        var field = owner.GetField(name, Fields);
        return field is not null && field.DeclaringType == owner && field.IsPrivate && !field.IsStatic &&
            field.IsInitOnly == readOnly && field.FieldType == fieldType ? field : null;
    }

    internal static bool IsPinnedCore(Type type)
    {
        var assembly = type.Assembly.GetName();
        return type.FullName == "HIDMaestro.HMController" && assembly.Name == "HIDMaestro.Core" &&
            assembly.Version == new Version(2, 0, 0, 0) && type.Module.ModuleVersionId == new Guid(LockedCoreMvid);
    }

    internal static int BoundJoinBudget(double factor)
    {
        if (!double.IsFinite(factor) || factor < 0.1 || factor > 100.0) return 0;
        return Math.Min(MaximumJoinMilliseconds, (int)(MaximumJoinMilliseconds * factor));
    }

    internal static int ReadJoinBudget(Type controllerType)
    {
        var scale = controllerType.Assembly.GetType("HIDMaestro.Internal.TimeoutScale", throwOnError: false);
        var factor = scale?.GetField("Factor", BindingFlags.Static | BindingFlags.Public | BindingFlags.DeclaredOnly);
        if (scale is null || factor is null || factor.DeclaringType != scale || !factor.IsStatic || !factor.IsInitOnly ||
            factor.FieldType != typeof(double) || factor.GetValue(null) is not double value) return 0;
        return BoundJoinBudget(value);
    }

    private sealed class SdkRuntime : IRuntime
    {
        public Type ControllerType => typeof(HMController);
        public Type CancellationSourceType => typeof(CancellationTokenSource);
        public Type ReaderType => typeof(Thread);
        public bool IsPinnedCore(Type type) => HostControllerOutputStop.IsPinnedCore(type);
        public int ReadJoinBudgetMilliseconds(Type type) => ReadJoinBudget(type);
        public bool IsCurrentReader(object reader) => ReferenceEquals(reader, Thread.CurrentThread);
        public ThreadState ReadThreadState(object reader) => ((Thread)reader).ThreadState;
        public bool TryEnterCancellationLock(object gate, int ms) => Monitor.TryEnter(gate, ms);
        public void ExitCancellationLock(object gate) => Monitor.Exit(gate);
        public void Cancel(object source) => ((CancellationTokenSource)source).Cancel();
        public bool Join(object reader, int ms) => ((Thread)reader).Join(ms);
    }
}