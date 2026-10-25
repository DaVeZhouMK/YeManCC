using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// A single event-triggered ThreadPool job; no resident timer, probes or control work.
internal static class HostCostArchive
{
    internal const long TotalMaxBytes = 100L * 1024 * 1024;
    private const string BaseName = "input-host-cost.log";
    private static readonly Regex ArchiveName = new(
        @"^input-host-cost\.log\.archive\.[0-9]{19}\.[0-9a-f]{24}\.zip$", RegexOptions.CultureInvariant);
    private static int _queued;
    private static long _requestRevision;
    private static long _nextAttemptTick, _completed, _failed, _evicted, _evictedBytes;
    private static int _lastError;
    private static long _receiptFailures;
    private static readonly string WorkerInstanceId = Guid.NewGuid().ToString("N");

    internal static object Snapshot() => new
    {
        schemaVersion = 1, archiveWorkerInstanceId = WorkerInstanceId, archiveWriterPid = Environment.ProcessId, totalMaxBytes = TotalMaxBytes,
        compressedMaxBytes = ArchiveBudget(HostCostLog.FileMaxBytes, TotalMaxBytes),
        workingReserveBytes = 2L * HostCostLog.FileMaxBytes + 65536,
        workerQueued = Volatile.Read(ref _queued) != 0, completed = Interlocked.Read(ref _completed),
        failures = Interlocked.Read(ref _failed), receiptFailures = Interlocked.Read(ref _receiptFailures), evictedVolumes = Interlocked.Read(ref _evicted),
        evictedCompressedBytes = Interlocked.Read(ref _evictedBytes), lastErrorHResult = Volatile.Read(ref _lastError),
        retention = "oldest-archive-commit-first;finite-history;no-event-frequency-inference",
        temporaryBudgetException = "normal-working-files-reserved;legacy-oversize-or-IO-failure-preserves-originals-and-can-exceed-budget"
    };

    internal static void Request(string directory)
    {
        Interlocked.Increment(ref _requestRevision);
        if (Environment.TickCount64 < Interlocked.Read(ref _nextAttemptTick) ||
            Interlocked.CompareExchange(ref _queued, 1, 0) != 0) return;
        try
        {
            if (!ThreadPool.QueueUserWorkItem(_ =>
            {
                long revision = Interlocked.Read(ref _requestRevision);
                try { ProcessPending(directory); }
                catch (Exception ex) { Failed(ex); WriteReceipt(directory, "archive-failed;source-preserved"); }
                finally
                {
                    Volatile.Write(ref _queued, 0);
                    // A producer may seal the next volume while the previous job
                    // is finishing. Only an actual request can schedule a follow-up.
                    if (revision != Interlocked.Read(ref _requestRevision)) Request(directory);
                }
            })) Volatile.Write(ref _queued, 0);
        }
        catch (Exception ex) { Volatile.Write(ref _queued, 0); Failed(ex); }
    }

    private static void Failed(Exception ex)
    {
        Interlocked.Increment(ref _failed);
        Volatile.Write(ref _lastError, ex.HResult);
        Interlocked.Exchange(ref _nextAttemptTick, Environment.TickCount64 + 30000);
    }

    internal static long ArchiveBudget(int activeMaxBytes, long totalMaxBytes) =>
        totalMaxBytes - 3L * activeMaxBytes - Math.Min(65536, activeMaxBytes);

    // Reserve active + one immutable pending + one ZIP temp (all <= 5 MiB in
    // normal operation), plus 64 KiB for ZIP overhead and the bounded receipt.
    // Legacy oversized sources and failed IO are preserved as explicit exceptions.
    // Limits are injectable only for isolated byte-budget fixtures.
    internal static void ProcessPending(string directory, int activeMaxBytes = HostCostLog.FileMaxBytes,
        long totalMaxBytes = TotalMaxBytes)
    {
        var root = Path.GetFullPath(directory);
        if (totalMaxBytes <= activeMaxBytes || activeMaxBytes <= 0) throw new ArgumentOutOfRangeException(nameof(totalMaxBytes));
        long archiveBudget = ArchiveBudget(activeMaxBytes, totalMaxBytes);
        if (archiveBudget <= 0) throw new IOException("No compressed archive budget; source retained");
        using var mutex = new Mutex(false, @"Local\YeManCC.CostArchive." + HostCostLog.LockId(root));
        bool owned = false, success = false;
        try
        {
            try { owned = mutex.WaitOne(0); }
            catch (AbandonedMutexException) { owned = true; }
            if (!owned) return;
            // Fixed exact inputs: restart recovery and migration of the old .1 backup.
            foreach (var suffix in new[] { ".1", ".pending" })
            {
                var source = Path.Combine(root, BaseName + suffix);
                if (!File.Exists(source)) continue;
                Seal(root, source, archiveBudget);
            }
            success = true;
        }
        finally
        {
            if (owned)
            {
                WriteReceipt(root, success ? "archive-pass-complete" : "archive-failed;source-preserved");
                mutex.ReleaseMutex();
            }
        }
    }

    private static void WriteReceipt(string root, string status)
    {
        try
        {
            // Small exportable receipt; process counters are never a lifetime ledger.
            Directory.CreateDirectory(root);
            var path = Path.Combine(root, "input-host-cost-retention.json");
            var bytes = JsonSerializer.SerializeToUtf8Bytes(new
            {
                schemaVersion = 1, observedAtUtc = DateTimeOffset.UtcNow.ToString("O"), status,
                scope = "archive-worker-process-counters;not-atomic-with-log-appends-or-export;not-global-lifetime-history",
                pendingRetained = File.Exists(Path.Combine(root, BaseName + ".pending")),
                legacyRetained = File.Exists(Path.Combine(root, BaseName + ".1")), retention = Snapshot()
            });
            using (var file = new FileStream(path + ".tmp", FileMode.Create, FileAccess.Write, FileShare.Read))
            { file.Write(bytes); file.Flush(true); }
            File.Move(path + ".tmp", path, true);
        }
        catch { Interlocked.Increment(ref _receiptFailures); }
    }

    private static void WriteJson(ZipArchive zip, string name, object value)
    {
        using var output = zip.CreateEntry(name, CompressionLevel.Fastest).Open();
        JsonSerializer.Serialize(output, value);
    }

    private static void Seal(string root, string source, long archiveBudget)
    {
        // FileShare.Read keeps the sealed source immutable throughout verification.
        // The producer renames into .pending and immediately writes a fresh active.
        long sealedBytes;
        string sealedDigest;
        using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.SequentialScan))
        {
            long bytes = input.Length;
            string digest = Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
            sealedBytes = bytes; sealedDigest = digest;
            bool newline = bytes == 0;
            if (bytes != 0) { input.Position = bytes - 1; newline = input.ReadByte() == '\n'; }
            input.Position = 0;
            string volume = BaseName + ".archive." + File.GetLastWriteTimeUtc(source).Ticks.ToString("D19") + "." + digest[..24] + ".zip";
            string target = Path.Combine(root, volume), temp = target + ".tmp";
            if (!File.Exists(target))
            {
                using (var output = new FileStream(temp, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    using (var zip = new ZipArchive(output, ZipArchiveMode.Create, true))
                    {
                        WriteJson(zip, "BEGIN.json", new
                        {
                            schema = "ymcc-log-volume/1", record = "file_begin", volumeId = volume,
                            stream = BaseName, payloadEntry = BaseName, sourceBytes = bytes, sha256 = digest,
                            sourceLastWriteUtc = File.GetLastWriteTimeUtc(source).ToString("O"),
                            archiveWriterPid = Environment.ProcessId,
                            identityScope = "archive-writer-is-not-record-writer;sealed-file-is-not-a-process-session",
                            ordering = "record-order-retained;cross-volume-time-order-not-inferred",
                            sourceName = Path.GetFileName(source)
                        });
                        using (var payload = zip.CreateEntry(BaseName, CompressionLevel.Fastest).Open()) input.CopyTo(payload, 65536);
                        WriteJson(zip, "END.json", new
                        {
                            schema = "ymcc-log-volume/1", record = "file_end", volumeId = volume,
                            sourceBytes = bytes, sha256 = digest, endsWithNewline = newline,
                            completePayloadRetained = true, processSessionComplete = (bool?)null,
                            recordBoundary = newline ? "line-terminated;JSON-validity-not-asserted" : "unterminated-tail-retained;JSON-validity-not-asserted"
                        });
                    }
                    output.Flush(true);
                }
                Verify(temp, bytes, digest, volume);
                long archiveBytes = new FileInfo(temp).Length;
                if (archiveBytes > archiveBudget) throw new IOException("Sealed volume exceeds compressed retention budget; original retained");
                Prune(root, archiveBudget - archiveBytes);
                File.Move(temp, target); // Verified closed ZIP, atomic same-directory commit.
            }
            Verify(target, bytes, digest, volume);
            Prune(root, archiveBudget, target);
        }
        // Never retire the only source before a committed, verified archive exists.
        using var writerMutex = new Mutex(false, @"Local\YeManCC.CostLog." + HostCostLog.LockId(root));
        bool writerOwned = false;
        try
        {
            try { writerOwned = writerMutex.WaitOne(0); }
            catch (AbandonedMutexException) { writerOwned = true; }
            if (!writerOwned) throw new IOException("Writer active; committed archive and source both retained for retry");
            // An older overlapping Host may still replace .1. Revalidate under
            // its existing zero-wait writer lock before retiring that path.
            if (source.EndsWith(".1", StringComparison.Ordinal))
            {
                using var current = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read);
                if (current.Length != sealedBytes || Convert.ToHexString(SHA256.HashData(current)).ToLowerInvariant() != sealedDigest)
                    throw new IOException("Legacy source changed; new bytes retained for a later archive pass");
            }
            File.Delete(source);
        }
        finally { if (writerOwned) writerMutex.ReleaseMutex(); }
        Interlocked.Increment(ref _completed);
    }

    private static void Verify(string path, long bytes, string digest, string volume)
    {
        using var zip = ZipFile.OpenRead(path);
        if (zip.Entries.Count != 3) throw new InvalidDataException("Unexpected volume members");
        var payload = zip.GetEntry(BaseName) ?? throw new InvalidDataException("Payload missing");
        using var beginStream = (zip.GetEntry("BEGIN.json") ?? throw new InvalidDataException("BEGIN missing")).Open();
        using var endStream = (zip.GetEntry("END.json") ?? throw new InvalidDataException("END missing")).Open();
        using var begin = JsonDocument.Parse(beginStream);
        using var end = JsonDocument.Parse(endStream);
        using var data = payload.Open();
        string actual = Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
        if (payload.Length != bytes || actual != digest ||
            begin.RootElement.GetProperty("schema").GetString() != "ymcc-log-volume/1" ||
            end.RootElement.GetProperty("schema").GetString() != "ymcc-log-volume/1" ||
            begin.RootElement.GetProperty("record").GetString() != "file_begin" ||
            end.RootElement.GetProperty("record").GetString() != "file_end" ||
            begin.RootElement.GetProperty("sourceBytes").GetInt64() != bytes ||
            begin.RootElement.GetProperty("volumeId").GetString() != volume ||
            begin.RootElement.GetProperty("sha256").GetString() != digest ||
            end.RootElement.GetProperty("volumeId").GetString() != volume ||
            end.RootElement.GetProperty("sourceBytes").GetInt64() != bytes ||
            end.RootElement.GetProperty("sha256").GetString() != digest ||
            !end.RootElement.GetProperty("completePayloadRetained").GetBoolean())
            throw new InvalidDataException("Volume integrity mismatch");
    }

    private static void Prune(string root, long budget, string? keep = null)
    {
        var archives = new DirectoryInfo(root).EnumerateFiles(BaseName + ".archive.*.zip")
            .Where(file => ArchiveName.IsMatch(file.Name)).OrderBy(file => file.LastWriteTimeUtc).ThenBy(file => file.Name, StringComparer.Ordinal).ToList();
        long total = archives.Sum(file => file.Length);
        foreach (var file in archives)
        {
            if (total <= budget) break;
            if (string.Equals(file.FullName, keep, StringComparison.OrdinalIgnoreCase)) continue;
            long length = file.Length;
            file.Delete(); total -= length;
            Interlocked.Increment(ref _evicted); Interlocked.Add(ref _evictedBytes, length);
        }
        if (total > budget) throw new IOException("Archive retention budget unavailable; original retained");
    }
}
