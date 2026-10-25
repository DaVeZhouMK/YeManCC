using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;

namespace YeMan.SharedRuntime;

// No HC version or installation root is encoded here. The caller supplies its approved
// identity/hash contract. A disk hash is never reported as proof of unknown loaded bytes.
internal static class RuntimeDependencyContract
{
    internal sealed record DiskIdentity(string AssemblyName, string AssemblyIdentity, string Sha256, string ModuleVersionId, long Bytes);
    internal sealed record FailureLayer(int Depth, string ExceptionType, string HResultHex, string? Code,
        string Stage, string? AssemblyName, string? RequestedIdentity, string? DiskIdentity,
        string? LoadedIdentity, string? FileName, string? TargetSite);
    private sealed record Origin(string Path, DiskIdentity Disk);
    private static readonly ConditionalWeakTable<Assembly, Origin> Origins = new();
    private static readonly object Sync = new();

    internal static DiskIdentity InspectDisk(string path)
    {
        using var stream = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return InspectLocked(path, stream);
    }

    private static DiskIdentity InspectLocked(string path, Stream stream)
    {
        stream.Position = 0;
        var sha = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        stream.Position = 0;
        using var pe = new PEReader(stream, PEStreamOptions.LeaveOpen);
        var metadata = pe.GetMetadataReader();
        var mvid = metadata.GetGuid(metadata.GetModuleDefinition().Mvid).ToString("D");
        var identity = AssemblyName.GetAssemblyName(path);
        return new(identity.Name ?? "", identity.FullName, sha, mvid, stream.Length);
    }

    internal static Assembly LoadPinned(AssemblyLoadContext context, AssemblyName requested, string path, string expectedSha256)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(requested);
        ArgumentNullException.ThrowIfNull(expectedSha256);
        var name = requested.Name ?? "";
        if (string.IsNullOrWhiteSpace(name) || name != System.IO.Path.GetFileName(name) || name.Contains(':'))
            throw Failure("REQUEST_NAME_INVALID", "dependency.identity", requested);
        if (expectedSha256.Length != 64 || !expectedSha256.All(Uri.IsHexDigit))
            throw Failure("HASH_CONTRACT_INVALID", "dependency.disk", requested);
        var fullPath = System.IO.Path.GetFullPath(path);
        lock (Sync)
        {
            FileStream locked;
            try { locked = File.Open(fullPath, FileMode.Open, FileAccess.Read, FileShare.Read); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            { throw Failure("RUNTIME_FILE_UNAVAILABLE", "dependency.disk", requested, inner: error); }
            using (locked)
            {
                DiskIdentity disk;
                try { disk = InspectLocked(fullPath, locked); }
                catch (Exception error) when (error is IOException or BadImageFormatException or InvalidOperationException)
                { throw Failure("RUNTIME_IMAGE_INVALID", "dependency.disk", requested, inner: error); }
                if (!disk.Sha256.Equals(expectedSha256, StringComparison.OrdinalIgnoreCase))
                    throw Failure("DISK_HASH_MISMATCH", "dependency.disk", requested, disk);
                var actual = new AssemblyName(disk.AssemblyIdentity);
                if (actual.Name != name || (requested.Version is not null && requested.Version != actual.Version) ||
                    (requested.CultureName is not null && (actual.CultureName ?? "") != requested.CultureName) ||
                    (requested.GetPublicKeyToken() is { } token && !(actual.GetPublicKeyToken() ?? []).SequenceEqual(token)))
                    throw Failure("REQUEST_IDENTITY_MISMATCH", "dependency.identity", requested, disk);

                var existing = context.Assemblies.FirstOrDefault(item => item.GetName().Name == name);
                if (existing is not null)
                {
                    if (!Origins.TryGetValue(existing, out var origin))
                        throw Failure("LOADED_ORIGIN_UNVERIFIED", "dependency.loaded", requested, disk, existing);
                    if (!SamePath(origin.Path, fullPath) || origin.Disk != disk)
                        throw Failure("LOADED_BYTES_CHANGED", "dependency.loaded", requested, disk, existing);
                    ValidateLoaded(context, existing, fullPath, requested, disk);
                    return existing;
                }
                var loaded = context.LoadFromAssemblyPath(fullPath);
                ValidateLoaded(context, loaded, fullPath, requested, disk);
                // Synchronization covers this contract's loaders. A foreign concurrent loader
                // can only be accepted when its exact context/path/MVID agrees with this image.
                Origins.GetValue(loaded, _ => new Origin(fullPath, disk));
                return loaded;
            }
        }
    }

    private static void ValidateLoaded(AssemblyLoadContext context, Assembly loaded, string path, AssemblyName requested, DiskIdentity disk)
    {
        if (!ReferenceEquals(AssemblyLoadContext.GetLoadContext(loaded), context))
            throw Failure("LOADED_CONTEXT_MISMATCH", "dependency.loaded", requested, disk, loaded);
        if (loaded.IsDynamic || string.IsNullOrWhiteSpace(loaded.Location) || !SamePath(loaded.Location, path) ||
            loaded.FullName != disk.AssemblyIdentity || loaded.ManifestModule.ModuleVersionId.ToString("D") != disk.ModuleVersionId)
            throw Failure("LOADED_IMAGE_MISMATCH", "dependency.loaded", requested, disk, loaded);
    }

    private static bool SamePath(string left, string right) => string.Equals(
        System.IO.Path.GetFullPath(left), System.IO.Path.GetFullPath(right),
        OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

    private static RuntimeDependencyFailureException Failure(string code, string stage, AssemblyName requested,
        DiskIdentity? disk = null, Assembly? loaded = null, Exception? inner = null) =>
        new(code, stage, requested.Name ?? "", requested.FullName, disk?.AssemblyIdentity, loaded?.FullName, inner);

    internal static FailureLayer[] DescribeFailure(Exception failure, string stage)
    {
        var result = new List<FailureLayer>();
        var pending = new Queue<Exception>();
        pending.Enqueue(failure);
        while (pending.Count > 0 && result.Count < 16)
        {
            var error = pending.Dequeue();
            var contract = error as RuntimeDependencyFailureException;
            var file = error switch { FileNotFoundException x => x.FileName, FileLoadException x => x.FileName,
                BadImageFormatException x => x.FileName, _ => null };
            result.Add(new(result.Count, error.GetType().FullName ?? error.GetType().Name, $"0x{error.HResult:X8}",
                contract?.Code, contract?.Stage ?? stage, contract?.AssemblySimpleName, contract?.RequestedIdentity,
                contract?.DiskIdentity, contract?.LoadedIdentity, file is null ? null : System.IO.Path.GetFileName(file),
                error.TargetSite?.DeclaringType?.FullName + "." + error.TargetSite?.Name));
            if (error.InnerException is { } inner) pending.Enqueue(inner);
            if (error is ReflectionTypeLoadException types)
                foreach (var item in types.LoaderExceptions) if (item is not null) pending.Enqueue(item);
        }
        return result.ToArray();
    }
}

internal sealed class RuntimeDependencyFailureException : FileLoadException
{
    internal string Code { get; }
    internal string Stage { get; }
    internal string AssemblySimpleName { get; }
    internal string RequestedIdentity { get; }
    internal string? DiskIdentity { get; }
    internal string? LoadedIdentity { get; }
    internal RuntimeDependencyFailureException(string code, string stage, string name, string requested,
        string? disk, string? loaded, Exception? inner)
        : base(code + ": " + name, name + ".dll", inner)
    { Code = code; Stage = stage; AssemblySimpleName = name; RequestedIdentity = requested; DiskIdentity = disk; LoadedIdentity = loaded; }
}

