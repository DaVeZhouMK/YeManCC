using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;

/// <summary>Runs the pinned HC software fan callback against an in-memory ACPI boundary.</summary>
public sealed class HcBinaryAdapter : IDisposable
{
    private const string ExpectedHcSha256 = "0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49";
    private const string ExpectedSentrySha256 = "d55ec4e68438fd515ae059f36c5fea4c5f5db3e610d042c7fcee318b2e0b1ee0";
    private readonly object gate = new();
    private readonly IsolatedHcContext context;
    private readonly MemoryAcpiProxy transport;
    private readonly MethodInfo open;
    private readonly MethodInfo close;
    private readonly MethodInfo callback;
    private readonly Type rogType;
    private readonly Type profileType;
    private readonly Type fanType;
    private readonly FieldInfo fanMode;
    private readonly FieldInfo fanSpeeds;
    private readonly FieldInfo profileFan;
    private readonly object software;
    private readonly object background;
    private bool disposed;

    public string AssemblyVersion { get; }
    public int AllTypeCount { get; }
    public string Sha256 { get; }
    public bool HandleReleased => !transport.HandleOpen;
    public byte[] DefaultCpuCurve => ReadDefaultCurve("defaultCPUFan");
    public byte[] DefaultGpuCurve => ReadDefaultCurve("defaultGPUFan");
    private byte[] ReadDefaultCurve(string field)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        return (byte[])((byte[])(rogType.GetField(field, BindingFlags.Static | BindingFlags.NonPublic)!
            .GetValue(null) ?? throw new InvalidDataException("HC default curve is missing"))).Clone();
    }


    public HcBinaryAdapter(string hcDllPath, string expectedHcSha256)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(hcDllPath);
        string fullPath = Path.GetFullPath(hcDllPath);
        string directory = Path.GetDirectoryName(fullPath)!;
        byte[] hcImage = ReadPinned(fullPath, expectedHcSha256);
        ReadPinned(Path.Combine(directory, "Sentry.dll"), ExpectedSentrySha256);
        Sha256 = Convert.ToHexString(SHA256.HashData(hcImage)).ToLowerInvariant();

        // The ALC denies native loading even before the per-assembly resolver can be installed.
        context = new IsolatedHcContext(directory);
        Assembly hc = context.LoadFromAssemblyPath(fullPath);
        context.BlockNativeImports(hc);
        AllTypeCount = hc.GetTypes().Length;
        AssemblyVersion = hc.GetName().Version?.ToString()
            ?? throw new InvalidDataException("HC has no assembly version.");

        Type acpi = RequireType(hc, "HandheldCompanion.Devices.ASUS.AsusACPI");
        Type transportInterface = RequireType(hc, "HandheldCompanion.Devices.ASUS.IAsusAcpiTransport");
        transport = (MemoryAcpiProxy)DispatchProxy.Create(transportInterface, typeof(MemoryAcpiProxy));
        Invoke(RequireMethod(acpi, "SetTransportForInjection"), null, transport);
        if (!ReferenceEquals(acpi.GetProperty("CurrentTransport")!.GetValue(null), transport))
            throw new InvalidOperationException("HC did not retain the in-memory ACPI transport.");
        context.ThrowIfNativeImportAttempted();

        // Injection precedes all HC data/device allocation, including enum boxing.
        open = RequireMethod(acpi, "Open");
        close = RequireMethod(acpi, "Close");
        rogType = RequireType(hc, "HandheldCompanion.Devices.ROGAlly");
        profileType = RequireType(hc, "HandheldCompanion.Misc.PowerProfile");
        fanType = RequireType(hc, "HandheldCompanion.Misc.FanProfile");
        if (profileType.TypeInitializer is not null || fanType.TypeInitializer is not null)
            throw new InvalidDataException("The audited profile types must have no static constructor.");
        callback = RequireMethod(rogType, "PowerProfileManager_Applied");
        fanMode = RequireField(fanType, "fanMode");
        fanSpeeds = RequireField(fanType, "fanSpeeds");
        profileFan = RequireField(profileType, "<FanProfile>k__BackingField");
        software = Enum.Parse(fanMode.FieldType, "Software");
        background = Enum.Parse(RequireType(hc, "HandheldCompanion.UpdateSource"), "Background");
    }

    public IReadOnlyList<byte[]> ApplySoftware(double[] points)
    {
        ArgumentNullException.ThrowIfNull(points);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            transport.Begin();
            try
            {
                if (Invoke(open, null) is not true)
                    throw new InvalidOperationException("The in-memory HC ACPI handle did not open.");
                object rog = RuntimeHelpers.GetUninitializedObject(rogType);
                object profile = RuntimeHelpers.GetUninitializedObject(profileType);
                object fan = RuntimeHelpers.GetUninitializedObject(fanType);
                fanMode.SetValue(fan, software);
                fanSpeeds.SetValue(fan, (double[])points.Clone());
                profileFan.SetValue(profile, fan);
                Invoke(callback, rog, profile, background);
                context.ThrowIfNativeImportAttempted();
                transport.ThrowIfFaulted();
                if (transport.Packets.Count != 3)
                    throw new InvalidDataException($"HC callback produced {transport.Packets.Count} packets; expected three.");
                return transport.Packets.Select(packet => (byte[])packet.Clone()).ToArray();
            }
            finally
            {
                try
                {
                    Invoke(close, null);
                    transport.ThrowIfFaulted();
                    context.ThrowIfNativeImportAttempted();
                    if (transport.HandleOpen)
                        throw new InvalidOperationException("The in-memory HC ACPI handle was not closed.");
                }
                finally { transport.End(); }
            }
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            try { Invoke(close, null); }
            finally { transport.End(); }
            // Retain the rejecting proxy: null would restore HC's real KernelTransport.
            // This fixture uses a process-local ALC; disposing never runs HC device cleanup.
        }
    }

    private static byte[] ReadPinned(string path, string expected)
    {
        byte[] image = File.ReadAllBytes(path);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(image)), expected, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Unreviewed binary: {path}");
        return image;
    }

    private static Type RequireType(Assembly assembly, string name) => assembly.GetType(name, throwOnError: true)!;
    private static MethodInfo RequireMethod(Type type, string name) =>
        type.GetMethod(name, BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance)
        ?? throw new MissingMethodException(type.FullName, name);
    private static FieldInfo RequireField(Type type, string name) =>
        type.GetField(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
        ?? throw new MissingFieldException(type.FullName, name);
    private static object? Invoke(MethodInfo method, object? target, params object?[] arguments)
    {
        try { return method.Invoke(target, arguments); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(error.InnerException).Throw();
            throw;
        }
    }

    private sealed class IsolatedHcContext : AssemblyLoadContext
    {
        private readonly string directory;
        private readonly HashSet<string> framework;
        private Exception? nativeImportFailure;

        public IsolatedHcContext(string directory) : base("fan-hc-binary-" + Guid.NewGuid().ToString("N"), isCollectible: false)
        {
            this.directory = directory;
            string trusted = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? string.Empty;
            framework = trusted.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Where(path => path.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(), StringComparison.OrdinalIgnoreCase)
                    || path.Contains(Path.DirectorySeparatorChar + "Microsoft.WindowsDesktop.App" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                .Select(path => Path.GetFileNameWithoutExtension(path)).ToHashSet(StringComparer.OrdinalIgnoreCase);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            if (AssemblyName.ReferenceMatchesDefinition(name, typeof(HcBinaryAdapter).Assembly.GetName()))
                return typeof(HcBinaryAdapter).Assembly;
            if (name.Name is not null && framework.Contains(name.Name))
                return Default.LoadFromAssemblyName(name);
            string simple = name.Name ?? throw new FileLoadException("Dependency has no simple name.");
            if (simple != Path.GetFileName(simple) || simple.Contains(':'))
                throw new FileLoadException("Invalid dependency name: " + simple);
            string path = Path.Combine(directory, simple + ".dll");
            if (!File.Exists(path))
                throw new FileNotFoundException("HC dependency is not in its specified directory.", path);
            byte[] image = string.Equals(simple, "Sentry", StringComparison.OrdinalIgnoreCase)
                ? ReadPinned(path, ExpectedSentrySha256) : File.ReadAllBytes(path);
            using var stream = new MemoryStream(image, writable: false);
            Assembly dependency = LoadFromAssemblyPath(Path.GetFullPath(path));
            BlockNativeImports(dependency);
            return dependency;
        }

        public void BlockNativeImports(Assembly assembly) => NativeLibrary.SetDllImportResolver(assembly,
            (name, _, _) => DenyNative(name));
        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName) => DenyNative(unmanagedDllName);
        private IntPtr DenyNative(string name)
        {
            var error = new DllNotFoundException("Native imports are blocked in the HC fan fixture: " + name);
            nativeImportFailure ??= error;
            throw error;
        }
        public void ThrowIfNativeImportAttempted()
        {
            if (nativeImportFailure is not null) ExceptionDispatchInfo.Capture(nativeImportFailure).Throw();
        }
    }

    // Public and non-sealed so DispatchProxy can derive its in-memory implementation.
    public class MemoryAcpiProxy : DispatchProxy
    {
        private static readonly IntPtr FakeHandle = new(0x5150);
        private bool active;
        private Exception? failure;
        internal bool HandleOpen { get; private set; }
        internal List<byte[]> Packets { get; } = new();
        internal void Begin()
        {
            if (active || HandleOpen) throw new InvalidOperationException("The previous fake ACPI session is still active.");
            Packets.Clear();
            failure = null;
            active = true;
        }
        internal void End() { active = false; }
        internal void ThrowIfFaulted()
        {
            if (failure is not null) ExceptionDispatchInfo.Capture(failure).Throw();
        }
        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            try
            {
                if (targetMethod is null || args is null) throw new InvalidOperationException("Invalid transport dispatch.");
                switch (targetMethod.Name)
                {
                    case "IsHandleValid":
                        return active && HandleOpen && (IntPtr)args[0]! == FakeHandle;
                    case "TryOpen":
                        if (!active || HandleOpen) throw new InvalidOperationException("Unexpected fake handle open.");
                        HandleOpen = true;
                        args[0] = FakeHandle;
                        args[1] = 0;
                        return true;
                    case "Control":
                        if (!active || !HandleOpen || (IntPtr)args[0]! != FakeHandle)
                            throw new InvalidOperationException("Control requires the current fake handle.");
                        var input = (byte[])args[2]!;
                        var output = (byte[])args[3]!;
                        if ((uint)args[1]! != 0x0022240Cu || input.Length != 28 || output.Length != 16)
                            throw new InvalidDataException("Unexpected HC fan control packet shape.");
                        Packets.Add((byte[])input.Clone());
                        Array.Clear(output, 0, output.Length);
                        args[4] = checked((uint)output.Length);
                        args[5] = 0;
                        return true;
                    case "Close":
                        if (!HandleOpen || (IntPtr)args[0]! != FakeHandle)
                            throw new InvalidOperationException("Unexpected fake handle close.");
                        HandleOpen = false;
                        return null;
                    default:
                        throw new MissingMethodException("Unreviewed ACPI transport member: " + targetMethod.Name);
                }
            }
            catch (Exception error)
            {
                failure ??= error;
                throw;
            }
        }
    }
}



