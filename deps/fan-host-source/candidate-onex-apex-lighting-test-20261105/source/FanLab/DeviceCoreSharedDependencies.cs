using System.IO;
using System.Collections.Concurrent;
using System.Text.Json;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;

namespace YeMan.SharedRuntime;

// Shared copies retain their original pinned bytes. This resolver is reached only
// when the device runtime has no local copy; legacy diagnostic layouts keep theirs.
internal static class DeviceCoreSharedDependencies
{
    internal sealed record BindingEvidence(string AssemblyName, string AssemblyIdentity, string ResolvedPath, string Sha256);
    private static readonly ConcurrentDictionary<string, BindingEvidence> Bindings = new(StringComparer.Ordinal);
    internal static BindingEvidence[] Snapshot() => Bindings.Values.OrderBy(x => x.AssemblyName, StringComparer.Ordinal).ToArray();

    private static readonly (string Name, string Sha256)[] Libraries =
    [
        ("Newtonsoft.Json", "A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B"),
        ("Nefarius.Utilities.DeviceManagement", "B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C"),
        ("Nefarius.Drivers.HidHide", "0A59C8A7C5D921A8561B47B2D9D2657A998676EFE1A3E8E31991E9721AB0D88F"),
        ("RAMSPDToolkit-NDD", "607E2F3FC56F2313A503F29A9C6C6F4D1A70B926A821B13148CE70567E886501"),
        ("Nefarius.Vicius.Abstractions", "51F380A12A82E925308E5D6255218DF283B692F37B9E991C6CE5E63F3E11D8FA"),
        ("Microsoft.Extensions.DependencyInjection.Abstractions", "621F220163119DFB08C8D1DD5BC0DF6C038BD9BAAB0662050042ACD7F906B7B7"),
        ("Microsoft.Extensions.Logging.Abstractions", "5DCB4934CB0DCC5547AEAEBEBC5BB687CC2522390B7032D68713B29AF64F7FD5"),
    ];

    internal static HashSet<string> ValidateDeclaredManifest(string deviceRuntimeDirectory, JsonElement runtimeManifest)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (!runtimeManifest.TryGetProperty("sharedDependencies", out var contract)) return names;
        if (contract.GetProperty("directory").GetString() != "PowerControl/ymcc-shared-runtime" ||
            !contract.TryGetProperty("files", out var files) || files.ValueKind != JsonValueKind.Array || files.GetArrayLength() != Libraries.Length)
            throw new InvalidDataException("Device shared dependency contract is invalid.");
        foreach (var entry in files.EnumerateArray())
        {
            string file = entry.GetProperty("path").GetString() ?? "";
            var spec = Libraries.FirstOrDefault(x => x.Name + ".dll" == file);
            if (spec.Name is null || !names.Add(file) ||
                !string.Equals(entry.GetProperty("sha256").GetString(), spec.Sha256, StringComparison.OrdinalIgnoreCase) ||
                File.Exists(Path.Combine(deviceRuntimeDirectory, file)))
                throw new InvalidDataException("Invalid, duplicated or local shared device dependency: " + file);
            string path = VerifiedPath(deviceRuntimeDirectory, new AssemblyName(spec.Name), spec.Sha256);
            if (new FileInfo(path).Length != entry.GetProperty("bytes").GetInt64())
                throw new InvalidDataException("Device shared dependency byte size mismatch: " + file);
        }
        return names;
    }

    internal static Assembly? Resolve(AssemblyLoadContext context, AssemblyName requested, string deviceRuntimeDirectory)
    {
        var spec = Libraries.FirstOrDefault(x => x.Name == requested.Name);
        if (spec.Name is null) return null;
        string path = VerifiedPath(deviceRuntimeDirectory, requested, spec.Sha256);
        using var locked = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (!Convert.ToHexString(SHA256.HashData(locked)).Equals(spec.Sha256, StringComparison.Ordinal))
            throw new InvalidDataException("Shared device dependency changed before loading: " + requested.Name);
        var loaded = RuntimeDependencyContract.LoadPinned(context, requested, path, spec.Sha256);
        Bindings[spec.Name] = new(spec.Name, loaded.FullName ?? spec.Name, loaded.Location, spec.Sha256);
        return loaded;
    }

    internal static string VerifiedPath(string deviceRuntimeDirectory, AssemblyName requested, string expectedSha256)
    {
        if (string.IsNullOrWhiteSpace(requested.Name) || requested.Name != Path.GetFileName(requested.Name) || requested.Name.Contains(':'))
            throw new FileLoadException("Invalid shared device dependency name.");
        var runtime = new DirectoryInfo(Path.GetFullPath(deviceRuntimeDirectory));
        var runtimes = runtime.Parent;
        var power = runtimes?.Parent;
        if (!string.Equals(runtimes?.Name, "handheldcompanion-runtime", StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(power?.Name, "PowerControl", StringComparison.OrdinalIgnoreCase))
            throw new FileNotFoundException("Shared device dependencies require the production PowerControl layout.", requested.Name);
        string sharedRoot = Path.Combine(power!.FullName, "ymcc-shared-runtime");
        string path = Path.Combine(sharedRoot, requested.Name + ".dll");
        if (!File.Exists(path)) throw new FileNotFoundException("Pinned shared device dependency is missing.", path);
        for (DirectoryInfo? at = new(sharedRoot); at is not null && at.FullName.Length >= power.FullName.Length; at = at.Parent)
            if ((at.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Shared device dependency directory must not be redirected: " + at.FullName);
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Shared device dependency must not be redirected: " + path);
        using (var stream = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(expectedSha256, StringComparison.Ordinal))
                throw new InvalidDataException("Pinned shared device dependency SHA256 mismatch: " + path);
        var actual = AssemblyName.GetAssemblyName(path);
        if (actual.Name != requested.Name || (requested.Version is not null && actual.Version != requested.Version) ||
            !string.Equals(actual.CultureName ?? "", requested.CultureName ?? "", StringComparison.Ordinal) ||
            (requested.GetPublicKeyToken() is { } token && !(actual.GetPublicKeyToken() ?? []).SequenceEqual(token)))
            throw new FileLoadException("Pinned shared device dependency identity mismatch.", path);
        return path;
    }
}
