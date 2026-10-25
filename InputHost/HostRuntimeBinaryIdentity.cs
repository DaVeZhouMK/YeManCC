using System.Security.Cryptography;

namespace YeManInputHost.Diagnostics;

// R4: one observation at a successful HELLO. Disk asset bytes and loaded MVIDs
// have separate scopes; this is diagnostic evidence, never controller admission.
internal static class HostRuntimeBinaryIdentity
{
    internal sealed record DiskFile(string path, long? bytes, string? diskSha256, string status);
    internal sealed record LoadedBusiness(string loadedAssemblyPath, string loadedMvid, DiskFile disk);
    internal sealed record VerifiedCore(string loadedAssemblyPath, string loadedMvid, long verifiedDiskBytes,
        string verifiedDiskSha256, string scope);
    internal sealed record Snapshot(int version, string capturedAtUtc, string scope, DiskFile processApphost,
        LoadedBusiness inputHostBusiness, VerifiedCore sdkCore);
    internal interface IReader { DiskFile Read(string path); }
    private sealed class DiskReader : IReader
    {
        public DiskFile Read(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) return new(path ?? "", null, null, "path-missing");
            // Two own Host files only. The 40MB SDK hash is reused from the existing
            // successful verification, not recomputed. No periodic or per-frame reads.
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (stream.Length > 64L * 1024 * 1024) return new(path, stream.Length, null, "file-too-large");
            var bytes = stream.Length;
            var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
            return new(path, bytes, hash, "disk-observed");
        }
    }
    // Diagnostics must fit the existing pipe envelope. Oversize or serialization
    // failure returns the exact original HELLO, including its existing capabilities.
    internal static string AppendToHello(string original, Snapshot? snapshot, int maxMessageBytes)
    {
        try
        {
            if (snapshot is null || !original.EndsWith("}", StringComparison.Ordinal)) return original;
            var identity = System.Text.Json.JsonSerializer.Serialize(snapshot);
            if (System.Text.Encoding.UTF8.GetByteCount(identity) > 8192) return original;
            var candidate = original[..^1] + ",\"hostRuntimeBinaryIdentity\":" + identity + "}";
            return System.Text.Encoding.UTF8.GetByteCount(candidate) <= maxMessageBytes ? candidate : original;
        }
        catch { return original; }
    }
    private static DiskFile Observe(string path, IReader reader)
    {
        try { return reader.Read(path); }
        catch (Exception ex) { return new(path ?? "", null, null, "read-error:" + ex.GetType().Name); }
    }
    internal static Snapshot Capture(string processPath, string businessPath, string businessMvid,
        string corePath, string coreMvid, long verifiedCoreBytes, string verifiedCoreSha256,
        string capturedAtUtc, IReader? reader = null)
    {
        reader ??= new DiskReader();
        return new(1, capturedAtUtc,
            "Host HELLO disk observations plus loaded assembly paths/MVIDs; disk hashes do not prove loaded image bytes or Steam consumption.",
            Observe(processPath, reader), new(businessPath, businessMvid, Observe(businessPath, reader)),
            new(corePath, coreMvid, verifiedCoreBytes, verifiedCoreSha256,
                "SDK disk identity verified by the existing successful HELLO check; loaded MVID is separate."));
    }
}
