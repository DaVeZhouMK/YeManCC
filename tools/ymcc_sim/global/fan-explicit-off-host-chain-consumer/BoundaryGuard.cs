using System.IO;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

internal static class ExplicitChainBoundary
{
    private const BindingFlags All = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static readonly ConcurrentQueue<object> Loads = new();
    private static readonly ConcurrentQueue<string> Denied = new();
    private static readonly ConcurrentQueue<string> NativeAttempts = new();
    private static readonly List<object> Engines = new();
    private static string Root = "";
    private static string Payload = "";
    private static string ExpectedHost = "";
    private static int OptionsChecked;
    private static string Hash(string file) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    private static object? Property(object value, string name) => value.GetType().GetProperty(name, All)!.GetValue(value);
    private static void Require(bool condition, string code)
    {
        if (condition) return;
        Denied.Enqueue(code);
        throw new InvalidOperationException("EXPLICIT_CHAIN_BOUNDARY:" + code);
    }

    [ModuleInitializer]
    internal static void Initialize()
    {
        Root = Path.GetFullPath(Environment.GetEnvironmentVariable("B0_CHAIN_GUARD_ROOT") ?? throw new InvalidOperationException("OWNED_GUARD_ROOT_REQUIRED"));
        Payload = Path.GetFullPath(Environment.GetEnvironmentVariable("B0_CHAIN_PAYLOAD_ROOT") ?? throw new InvalidOperationException("PRIVATE_PAYLOAD_ROOT_REQUIRED"));
        ExpectedHost = Environment.GetEnvironmentVariable("B0_CHAIN_HOST_SHA256") ?? throw new InvalidOperationException("EXPLICIT_HOST_IDENTITY_REQUIRED");
        Directory.CreateDirectory(Root);
        AppDomain.CurrentDomain.AssemblyLoad += (_, item) =>
        {
            var assembly = item.LoadedAssembly;
            var name = assembly.GetName().Name ?? "";
            Require(!name.StartsWith("HandheldCompanion", StringComparison.OrdinalIgnoreCase), "HC_ASSEMBLY_LOAD_FORBIDDEN");
            if (name is not ("YeManFanHost" or "YeManFanBackend")) return;
            var file = Path.GetFullPath(assembly.Location);
            Require(file.StartsWith(Payload + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase), "TARGET_ASSEMBLY_OUTSIDE_PRIVATE_PAYLOAD");
            var actual = Hash(file);
            if (name == "YeManFanHost") Require(actual == ExpectedHost, "ACTUAL_LOADED_HOST_IDENTITY_DIFFERS");
            NativeLibrary.SetDllImportResolver(assembly, (library, _, _) =>
            {
                NativeAttempts.Enqueue(library);
                throw new DllNotFoundException("TARGET_NATIVE_ACCESS_BLOCKED:" + library);
            });
            Loads.Enqueue(new { path = file, sha256 = actual, identity = assembly.FullName, mvid = assembly.ManifestModule.ModuleVersionId });
        };
        AppDomain.CurrentDomain.ProcessExit += (_, _) =>
        {
            var disposed = Engines.Select(engine => engine.GetType().GetField("disposed", All)?.GetValue(engine)).ToArray();
            var result = new
            {
                schemaVersion = 1,
                kind = "actual-original-host-harness-boundary",
                pid = Environment.ProcessId,
                observedProcessExit = true,
                optionsChecked = OptionsChecked,
                enginesObserved = Engines.Count,
                engineDisposedFlags = disposed,
                loadedTargetAssemblies = Loads.ToArray(),
                denied = Denied.ToArray(),
                targetNativeAttempts = NativeAttempts.ToArray(),
                mainInvokedByHarness = false,
                entryScope = "Original reflection harness directly constructs mock FanHostEngine/LocalFanHostServer; normal Host Main is never called",
                mockLegacyWriteFlagsAreNotPhysicalEvidence = true,
                physicalAcceptance = false,
            };
            File.WriteAllText(Path.Combine(Root, "managed-runtime-boundary.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        };
    }

    internal static void RequireMockOptions(object options)
    {
        Require(Equals(Property(options, "MockHandshake"), true), "EXPLICIT_MOCK_HANDSHAKE_REQUIRED");
        Require(Equals(Property(options, "RealBackend"), false), "REAL_BACKEND_FORBIDDEN");
        Require(Equals(Property(options, "AllowHardwareWrites"), false), "HARDWARE_WRITE_AUTHORIZATION_FORBIDDEN");
        Require(Property(options, "ParentPid") is null && string.IsNullOrWhiteSpace((string?)Property(options, "ParentProcessName")), "REAL_PARENT_PROBE_FORBIDDEN");
        OptionsChecked++;
    }

    internal static void ObserveMockEngine(object engine)
    {
        Require(engine.GetType().GetField("realBackend", All)!.GetValue(engine) is null, "ACTUAL_REAL_BACKEND_MUST_BE_NULL");
        Engines.Add(engine);
    }
}

