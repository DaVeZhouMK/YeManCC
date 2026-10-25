using System;
using System.Reflection;
using System.Threading;
using HIDMaestro;

namespace YeManInputHost.Diagnostics;

// Release-only barrier for the pinned SDK's existing output reader. It neither
// disposes SDK resources nor changes their registrations, mappings or fields.
// The caller must retain controller/context and stop release on any false.
// SDK asset SHA verification remains at the existing Host admission boundary.
internal static class HostControllerOutputStop
{
    internal const int MaximumJoinMilliseconds = 500;
    internal const string LockedCoreMvid = "5f1d5fa1-a18d-44e5-aba0-a8dfb3fabdf8";
    private const BindingFlags InstanceFields = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    // No FieldInfo factory or target binding runs before this release call.
    internal static bool TryStop(HMController controller, out string detail)
    {
        detail = "sdk-output-stop-binding-failed";
        try { return TryStopAgainstRuntime(controller, new SdkRuntime(), out detail); }
        catch { return false; }
    }

    // Pure fixtures supply ordinary fake field types and operations. Production
    // always uses SdkRuntime and the fixed core identity above, never this seam.
    internal interface IRuntime
    {
        Type ControllerType { get; }
        Type CancellationSourceType { get; }
        Type ReaderType { get; }
        bool IsPinnedCore(Type controllerType);
        int ReadJoinBudgetMilliseconds(Type controllerType);
        bool IsCurrentReader(object reader);
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
            if (type != runtime.ControllerType || !type.IsClass || !type.IsSealed || type.IsGenericType ||
                !runtime.IsPinnedCore(type))
            { detail = "sdk-output-stop-core-identity-mismatch"; return false; }
            var ctsType = runtime.CancellationSourceType;
            var readerType = runtime.ReaderType;
            var ctsField = Field(type, "_outputCts", ctsType);
            var readerField = Field(type, "_outputThread", readerType);
            var viewField = Field(type, "_outputView", typeof(IntPtr));
            if (ctsField is null || readerField is null || viewField is null)
            { detail = "sdk-output-stop-field-contract-mismatch"; return false; }
            var cts = ctsField.GetValue(controller);
            var reader = readerField.GetValue(controller);
            var view = viewField.GetValue(controller);
            if (cts is null || cts.GetType() != ctsType || view is not IntPtr outputView ||
                (reader is not null && reader.GetType() != readerType))
            { detail = "sdk-output-stop-field-value-mismatch"; return false; }

            // Exact ctor sets _outputView=0 on output setup failure. A null
            // readonly thread plus zero view is the proved no-reader case.
            // A non-null reader MUST still be joined even if its view is zero.
            if (reader is null && outputView != IntPtr.Zero)
            { detail = "sdk-output-stop-reader-missing-with-mapping"; return false; }
            var budget = 0;
            if (reader is not null)
            {
                detail = "sdk-output-stop-self-join";
                if (runtime.IsCurrentReader(reader)) return false;
                detail = "sdk-output-stop-budget-unknown";
                budget = runtime.ReadJoinBudgetMilliseconds(type);
                if (budget <= 0 || budget > MaximumJoinMilliseconds) return false;
            }
            detail = "sdk-output-stop-cancel-failed";
            runtime.Cancel(cts); // Same CTS/action as SDK Dispose, without Dispose.
            if (reader is null) { detail = "sdk-output-reader-not-created"; return true; }
            detail = "sdk-output-stop-join-failed";
            if (!runtime.Join(reader, budget)) { detail = "sdk-output-stop-timeout"; return false; }
            detail = "sdk-output-reader-stopped";
            return true; // Join(true), not IsAlive(false), proves termination.
        }
        catch { return false; } // Fixed detail only: no SDK exception/path leakage.
    }

    private static FieldInfo? Field(Type owner, string name, Type fieldType)
    {
        var field = owner.GetField(name, InstanceFields);
        return field is not null && field.DeclaringType == owner && field.IsPrivate &&
            !field.IsStatic && field.IsInitOnly && field.FieldType == fieldType ? field : null;
    }

    internal static int BoundJoinBudget(double factor)
    {
        if (!double.IsFinite(factor) || factor < 0.1 || factor > 100.0) return 0;
        return Math.Min(MaximumJoinMilliseconds, (int)(MaximumJoinMilliseconds * factor));
    }

    private sealed class SdkRuntime : IRuntime
    {
        public Type ControllerType => typeof(HMController);
        public Type CancellationSourceType => typeof(CancellationTokenSource);
        public Type ReaderType => typeof(Thread);
        public bool IsPinnedCore(Type type)
        {
            var assembly = type.Assembly.GetName();
            return type.FullName == "HIDMaestro.HMController" && assembly.Name == "HIDMaestro.Core" &&
                assembly.Version == new Version(1, 12, 0, 0) &&
                type.Module.ModuleVersionId == new Guid(LockedCoreMvid);
        }
        public int ReadJoinBudgetMilliseconds(Type controllerType)
        {
            // Read the SDK's already-resolved factor, not a newly sampled env
            // setting. This is a metadata-bound field read, not an SDK method
            // invocation. Cap an enlarged factor without extending small ones.
            var scale = controllerType.Assembly.GetType("HIDMaestro.Internal.TimeoutScale", throwOnError: false);
            var factor = scale?.GetField("Factor", BindingFlags.Static | BindingFlags.Public | BindingFlags.DeclaredOnly);
            if (scale is null || factor is null || factor.DeclaringType != scale || !factor.IsStatic ||
                !factor.IsInitOnly || factor.FieldType != typeof(double) || factor.GetValue(null) is not double value ||
                BoundJoinBudget(value) == 0)
                return 0;
            return BoundJoinBudget(value);
        }
        public bool IsCurrentReader(object reader) => ReferenceEquals(reader, Thread.CurrentThread);
        public void Cancel(object cancellationSource) => ((CancellationTokenSource)cancellationSource).Cancel();
        public bool Join(object reader, int milliseconds) => ((Thread)reader).Join(milliseconds);
    }
}