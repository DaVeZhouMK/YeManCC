using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Called only by the existing opt-in cost producers. Compression never runs here.
internal static class HostCostLog
{
    internal const int FileMaxBytes = 5 * 1024 * 1024;
    internal const int RecordMaxBytes = 64 * 1024;
    private static readonly UTF8Encoding Utf8 = new(false);
    private static long _rotations, _ioFailures, _contended, _oversize, _archiveBackpressure;
    private static string? _recoveryRoot;

    internal static object Snapshot() => new
    {
        schemaVersion = 2, fileMaxBytes = FileMaxBytes, recordMaxBytes = RecordMaxBytes,
        retainedBytesBudget = HostCostArchive.TotalMaxBytes,
        budgetScope = "active-and-one-pending-and-one-ZIP-temp-reserved;remaining-budget-compressed-archives;legacy-and-failure-exceptions-explicit",
        archive = HostCostArchive.Snapshot(), rotations = Interlocked.Read(ref _rotations),
        ioFailures = Interlocked.Read(ref _ioFailures), contended = Interlocked.Read(ref _contended),
        oversizeRows = Interlocked.Read(ref _oversize), archiveBackpressure = Interlocked.Read(ref _archiveBackpressure),
        scope = "writer-process-counters;not-atomic-with-export-or-other-Host-processes"
    };

    internal static bool Append(string directory, string line,
        int fileMaxBytes = FileMaxBytes, int recordMaxBytes = RecordMaxBytes, bool scheduleArchive = true)
    {
        // Injectable limits/dispatch are for isolated file fixtures only.
        try
        {
            if (fileMaxBytes <= 0 || recordMaxBytes <= 0 || string.IsNullOrEmpty(line)) return false;
            var bytes = Utf8.GetBytes(line);
            if (bytes.Length + 1 > recordMaxBytes)
            {
                Interlocked.Increment(ref _oversize);
                bytes = JsonSerializer.SerializeToUtf8Bytes(new
                {
                    t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), writerPid = Environment.ProcessId,
                    kind = "cost-record-omitted", reason = "diagnostic-record-byte-budget",
                    originalBytes = bytes.Length,
                    originalSha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(),
                    completePayloadRetained = false
                });
            }
            if (bytes.Length + 1 > recordMaxBytes || bytes.Length + 1 > fileMaxBytes) return false;
            var root = Path.GetFullPath(directory);
            bool archiveRequest = false;
            if (scheduleArchive && !string.Equals(Volatile.Read(ref _recoveryRoot), root, StringComparison.OrdinalIgnoreCase))
            {
                Volatile.Write(ref _recoveryRoot, root);
                archiveRequest = true; // Schedule after the active writer has released its lock.
            }
            var id = LockId(root);
            using var mutex = new Mutex(false, @"Local\YeManCC.CostLog." + id);
            bool owned = false;
            try
            {
                try { owned = mutex.WaitOne(0); }
                catch (AbandonedMutexException) { owned = true; }
                if (!owned) { Interlocked.Increment(ref _contended); return false; }
                Directory.CreateDirectory(root);
                var path = Path.Combine(root, "input-host-cost.log");
                FileStream? stream = null;
                try
                {
                    stream = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read);
                    long size = stream.Length;
                    bool separator = false;
                    if (size > 0) { stream.Seek(-1, SeekOrigin.End); separator = stream.ReadByte() != '\n'; }
                    if (size + (separator ? 1 : 0) + bytes.Length + 1 > fileMaxBytes)
                    {
                        stream.Dispose(); stream = null;
                        // One immutable pending slot bounds backlog. Never overwrite evidence
                        // when compression/IO is unavailable, and never wait for the worker.
                        if (File.Exists(path + ".pending"))
                        {
                            Interlocked.Increment(ref _archiveBackpressure);
                            archiveRequest = true;
                            return false;
                        }
                        File.Move(path, path + ".pending");
                        Interlocked.Increment(ref _rotations);
                        archiveRequest = true;
                        stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
                        separator = false;
                    }
                    else stream.Seek(0, SeekOrigin.End);
                    if (separator) stream.WriteByte((byte)'\n');
                    stream.Write(bytes); stream.WriteByte((byte)'\n');
                    stream.Flush();
                    return true;
                }
                finally { stream?.Dispose(); }
            }
            finally
            {
                if (owned) mutex.ReleaseMutex();
                if (archiveRequest && scheduleArchive) HostCostArchive.Request(root);
            }
        }
        catch { Interlocked.Increment(ref _ioFailures); return false; }
    }

    internal static string LockId(string root) =>
        Convert.ToHexString(SHA256.HashData(Utf8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())))[..24];
}
