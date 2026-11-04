using System;
using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Reuses the old one-shot archive worker only to retire exact owned legacy
// volumes. No compression, polling, new thread or controller work.
internal static class HostCostArchive
{
    internal const long TotalMaxBytes = 20_000_000;
    private const string BaseName = "input-host-cost.log";
    private static readonly Regex ArchiveName = new(
        @"^input-host-cost\.log\.archive\.[0-9]{19}\.[0-9a-f]{24}\.zip(?:\.tmp)?$", RegexOptions.CultureInvariant);
    private static int _queued, _lastError;
    private static long _completed, _failed, _evicted, _evictedBytes, _receiptFailures;
    internal static object Snapshot() => new {
        schemaVersion = 2, totalMaxBytes = TotalMaxBytes, compressedMaxBytes = 0,
        workerQueued = Volatile.Read(ref _queued) != 0, completed = Interlocked.Read(ref _completed),
        failures = Interlocked.Read(ref _failed), receiptFailures = Interlocked.Read(ref _receiptFailures),
        evictedVolumes = Interlocked.Read(ref _evicted), evictedCompressedBytes = Interlocked.Read(ref _evictedBytes),
        lastErrorHResult = Volatile.Read(ref _lastError),
        retention = "single-active-newest-lines;legacy-volumes-retired;no-new-ZIPs",
        temporaryBudgetException = "legacy-cleanup-or-IO-failure-is-explicit;active-writes-do-not-wait-for-archives"
    };
    internal static void Request(string directory)
    {
        if (Interlocked.CompareExchange(ref _queued, 1, 0) != 0) return;
        try {
            if (!ThreadPool.QueueUserWorkItem(_ => {
                try { ProcessPending(directory); }
                catch (Exception ex) { Interlocked.Increment(ref _failed); Volatile.Write(ref _lastError, ex.HResult); }
                finally { Volatile.Write(ref _queued, 0); }
            })) { Interlocked.Increment(ref _failed); Volatile.Write(ref _queued, 0); }
        } catch (Exception ex) { Interlocked.Increment(ref _failed); Volatile.Write(ref _lastError, ex.HResult); Volatile.Write(ref _queued, 0); }
    }
    internal static void ProcessPending(string directory)
    {
        var root = Path.GetFullPath(directory);
        if (!Directory.Exists(root)) return;
        // Same named ownership lock as the old archive worker. If an old worker
        // still owns it, leave its files alone; export excludes those volumes.
        using var mutex = new Mutex(false, @"Local\YeManCC.CostArchive." + HostCostLog.LockId(root));
        bool owned = false;
        try {
            try { owned = mutex.WaitOne(0); } catch (AbandonedMutexException) { owned = true; }
            if (!owned) return;
            foreach (var path in Directory.EnumerateFiles(root, "input-host-cost.log*", SearchOption.TopDirectoryOnly)) {
                var name = Path.GetFileName(path);
                if (name != BaseName + ".1" && name != BaseName + ".pending" && !ArchiveName.IsMatch(name)) continue;
                try {
                    var info = new FileInfo(path);
                    if ((info.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                    long size = info.Length; File.Delete(path);
                    Interlocked.Increment(ref _evicted); Interlocked.Add(ref _evictedBytes, size);
                } catch (Exception ex) { Interlocked.Increment(ref _failed); Volatile.Write(ref _lastError, ex.HResult); }
            }
            Interlocked.Increment(ref _completed);
            try {
                var path = Path.Combine(root, "input-host-cost-retention.json");
                var bytes = JsonSerializer.SerializeToUtf8Bytes(new {
                    schema = "ymcc-cost-retention/2", policy = "single-active-20MB;newest-overwrites-oldest",
                    archiveWriterPid = Environment.ProcessId, observedAtUtc = DateTimeOffset.UtcNow,
                    historyComplete = false, legacyVolumesNoLongerExported = true, retention = Snapshot()
                });
                File.WriteAllBytes(path + ".tmp", bytes); File.Move(path + ".tmp", path, true);
            } catch { Interlocked.Increment(ref _receiptFailures); }
        } finally { if (owned) mutex.ReleaseMutex(); }
    }
}
