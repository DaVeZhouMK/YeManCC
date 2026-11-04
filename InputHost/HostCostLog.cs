using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Opt-in diagnostics only. One bounded active JSONL; no compressed volumes.
internal static class HostCostLog
{
    internal const int FileMaxBytes = 20_000_000;
    internal const int RecordMaxBytes = 64 * 1024;
    private static readonly UTF8Encoding Utf8 = new(false);
    private static long _compactions, _ioFailures, _contended, _oversize;
    private static string? _recoveryRoot;

    internal static object Snapshot() => new
    {
        schemaVersion = 3, fileMaxBytes = FileMaxBytes, recordMaxBytes = RecordMaxBytes,
        retainedBytesBudget = HostCostArchive.TotalMaxBytes,
        budgetScope = "single-active-file;newest-complete-lines;no-new-archives;legacy-cleanup-status-explicit",
        archive = HostCostArchive.Snapshot(), compactions = Interlocked.Read(ref _compactions), rotations = 0,
        ioFailures = Interlocked.Read(ref _ioFailures), contended = Interlocked.Read(ref _contended),
        oversizeRows = Interlocked.Read(ref _oversize), archiveBackpressure = 0,
        scope = "active-payload-byte-cap;receipt-excluded;writer-process-counters;not-atomic-with-export-or-other-Host-processes"
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
                        // New records replace the oldest diagnostics. An old pending
                        // ZIP must never reject the current row or start compression.
                        stream = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read);
                        CompactNewest(stream, fileMaxBytes, bytes.Length + 1);
                        Interlocked.Increment(ref _compactions);
                        archiveRequest = true; // Retry one-shot legacy cleanup, never compression.
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

    private static void CompactNewest(FileStream stream, int cap, int incomingBytes)
    {
        long original = stream.Length;
        // Amortize compaction: keep at most half the file, leaving room for new rows.
        long keep = Math.Max(0, Math.Min(cap / 2L, cap - incomingBytes - 2048L));
        long start = Math.Max(0, original - keep);
        var buffer = new byte[65536];
        if (start > 0 && start < original)
        {
            stream.Position = start - 1;
            if (stream.ReadByte() != '\n')
            {
                stream.Position = start;
                bool boundary = false;
                while (stream.Position < original && !boundary)
                {
                    long begin = stream.Position;
                    int count = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, original - begin));
                    if (count == 0) throw new EndOfStreamException("Cost log changed during compaction");
                    int newline = Array.IndexOf(buffer, (byte)'\n', 0, count);
                    if (newline >= 0) { start = begin + newline + 1; boundary = true; }
                }
                if (!boundary) start = original; // An oversized partial row is not a complete retained record.
            }
        }
        if (keep == 0) start = original;
        var compactionId = Guid.NewGuid().ToString("N");
        bool endedWithNewline = original == 0;
        if (original > 0) { stream.Position = original - 1; endedWithNewline = stream.ReadByte() == '\n'; }
        byte[] beginMarker = JsonSerializer.SerializeToUtf8Bytes(new {
            @event = "log-retention-begin", compactionId, policy = "newest-lines-single-active-20MB", writerPid = Environment.ProcessId,
            sourceBytes = original, droppedPrefixBytes = start, retainedSourceBytes = original - start,
            historyComplete = false, sourceEndedWithNewline = endedWithNewline, sourceOrder = "retained-file-order;not-process-session-proof"
        });
        byte[] endMarker = JsonSerializer.SerializeToUtf8Bytes(new {
            @event = "log-retention-end", compactionId, retainedSourceBytes = original - start,
            historyComplete = false, partialTailRetained = original > start && !endedWithNewline, newRowsFollow = true
        });
        // Production caps leave ample marker space; small fixture caps can keep
        // only the incoming row. Every write, including markers, stays under cap.
        if (beginMarker.Length + endMarker.Length + original - start + incomingBytes + 3 > cap)
        { stream.SetLength(0); stream.Position = 0; return; }
        stream.Position = 0; stream.Write(beginMarker); stream.WriteByte((byte)'\n');
        long writeAt = stream.Position, readAt = start;
        while (readAt < original)
        {
            stream.Position = readAt;
            int count = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, original - readAt));
            if (count == 0) throw new EndOfStreamException("Cost log changed during compaction");
            stream.Position = writeAt; stream.Write(buffer, 0, count);
            readAt += count; writeAt += count;
        }
        stream.Position = writeAt;
        if (writeAt > 0) { stream.Position = writeAt - 1; if (stream.ReadByte() != '\n') { stream.Position = writeAt; stream.WriteByte((byte)'\n'); } }
        stream.Write(endMarker); stream.WriteByte((byte)'\n');
        stream.SetLength(stream.Position);
    }

    internal static string LockId(string root) =>
        Convert.ToHexString(SHA256.HashData(Utf8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())))[..24];
}
