using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

// HC-SLIM-01: content-identical utility DLLs have one authoritative shared copy.
// No driver/HIDMaestro binaries or APIs are replaced. Normal (non-thin) builds
// retain the local copy and never reach this resolver. No environment override,
// download, installer, device operation or repair write is permitted here.
internal static class RuntimeSharedDependencies
{
    internal const string RuntimeId = "HC-CANDIDATE-0.32.4.0-06c0b954-20260902";
    private static readonly (string Name, string Sha256)[] Libraries =
    [
        ("Newtonsoft.Json", "A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B"),
        ("Nefarius.Utilities.DeviceManagement", "B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C")
    ];

    [ModuleInitializer]
    internal static void Register() => AssemblyLoadContext.Default.Resolving += Resolve;

    private static Assembly? Resolve(AssemblyLoadContext context, AssemblyName requested)
    {
        var spec = Libraries.FirstOrDefault(x => x.Name == requested.Name);
        if (spec.Name is null) return null;
        string path = VerifiedPath(AppContext.BaseDirectory, requested, spec.Sha256);
        // Recheck under a no-write/no-delete handle held through the loader.
        // Verification must not be separated from loading by a writable gap.
        using var locked = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (!Convert.ToHexString(SHA256.HashData(locked)).Equals(spec.Sha256, StringComparison.Ordinal))
            throw new InvalidDataException("Pinned dependency changed between identity verification and load.");
        return context.LoadFromAssemblyPath(path);
    }

    internal static string VerifiedPath(string hostDirectory, AssemblyName requested, string expectedSha256)
    {
        var host = new DirectoryInfo(Path.GetFullPath(hostDirectory));
        var features = host.Parent;
        var power = features?.Parent;
        if (!host.Name.Equals("virtual-gamepad", StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(features?.Name, "feature-assets", StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(power?.Name, "PowerControl", StringComparison.OrdinalIgnoreCase))
            throw new FileNotFoundException("Shared dependency fallback is restricted to the production PowerControl/feature-assets/virtual-gamepad layout.", requested.Name);
        string path = Path.Combine(power!.FullName, "handheldcompanion-runtime", RuntimeId, requested.Name + ".dll");
        string runtimeRoot = Path.GetDirectoryName(path)!;
        if (!File.Exists(path)) throw new FileNotFoundException("Pinned shared runtime dependency is missing; restore the candidate's original DLL copy or shared runtime.", path);
        for (DirectoryInfo? at = new(runtimeRoot); at is not null && at.FullName.Length >= power.FullName.Length; at = at.Parent)
            if ((at.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Shared runtime must not be redirected by a reparse point: " + at.FullName);
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new IOException("Shared dependency file must not be a reparse point.");
        using (var stream = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(expectedSha256, StringComparison.Ordinal))
                throw new InvalidDataException("Pinned shared dependency SHA256 mismatch: " + path);
        var actual = AssemblyName.GetAssemblyName(path);
        if (!string.Equals(actual.Name, requested.Name, StringComparison.Ordinal) ||
            (requested.Version is not null && actual.Version != requested.Version) ||
            !string.Equals(actual.CultureName ?? "", requested.CultureName ?? "", StringComparison.Ordinal) ||
            !(actual.GetPublicKeyToken() ?? []).SequenceEqual(requested.GetPublicKeyToken() ?? []))
            throw new FileLoadException("Pinned shared dependency identity mismatch.", path);
        return path;
    }

    // Dedicated read-only software check; no target creation or PnP enumeration.
    internal static int RunSelfTest()
    {
        try
        {
            var receipts = new List<object>();
            foreach (var spec in Libraries)
            {
                string expectedPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "handheldcompanion-runtime", RuntimeId, spec.Name + ".dll"));
                var requestedIdentity = AssemblyName.GetAssemblyName(expectedPath);
                var assembly = Assembly.Load(requestedIdentity);
                if (!Path.GetFullPath(assembly.Location).Equals(expectedPath, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Thin candidate did not resolve the actual shared copy: " + assembly.Location);
                // Use the actual full identity for validation; never activate SDK types.
                VerifiedPath(AppContext.BaseDirectory, assembly.GetName(), spec.Sha256);
                receipts.Add(new { name = spec.Name, location = assembly.Location, sha256 = spec.Sha256 });
            }
            Console.WriteLine(JsonSerializer.Serialize(new { status = "SHARED_RUNTIME_BINDING_PASS_NOT_DEVICE_PASS", hardwareWrites = false, deviceAccessed = false, receipts }));
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 3; }
    }
}
