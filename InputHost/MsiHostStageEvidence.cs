using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Explicit MSI engineering opt-in only; the pipe-command handler is the sole writer.
// These are local stage observations: Decode is parsed metadata, Submit spans
// TrySubmitFrame encoding plus backend work, and EndWrite is response completion.
// None of those observations establish tuple, consumer, or Steam acceptance.
internal sealed class MsiHostStageEvidence : IDisposable
{
    internal const int Capacity = 1024;
    internal const string MapPrefix = "Local\\YeManCC-MSI-HostStage-v1-";
    private const ulong Magic = 0x315647455453484DUL;
    private const int PublicationOffset = 24;
    private const string SnapshotScope = "Current MSI host stage diagnostic state only; no tuple, consumer, or Steam acceptance guarantee.";

    internal enum Phase : ulong
    {
        Created = 1, ReadPending = 2, ReadReceived = 3, HandlePending = 4,
        CommandDecoded = 5, SubmitFrameHandler = 6, SubmitFrameHandlerReturned = 7,
        HandleReturned = 8, WritePending = 9, WriteReturned = 10, Closed = 11,
        BackendSwitch = 12, BackendSwitchReturned = 13, CostWindow = 14, CostWindowReturned = 15
    }

    // Synthetic tests supply these interfaces; they never use the mapping factory.
    internal interface ISink : IDisposable
    {
        void WriteSequence(ulong value);
        void WriteData(byte[] buffer, int offset, int count);
    }

    internal interface ISinkFactory
    {
        ISink CreateNew(string name, int capacity);
    }

    internal interface IClock
    {
        long Frequency { get; }
        long GetTimestamp();
    }

    private sealed class StopwatchClock : IClock
    {
        internal static readonly StopwatchClock Instance = new();
        public long Frequency => Stopwatch.Frequency;
        public long GetTimestamp() => Stopwatch.GetTimestamp();
    }

    private readonly byte[] _buffer = new byte[Capacity];
    private readonly ISink _sink;
    private readonly string _mapName;
    private ulong _publicationSequence;
    private bool _hasDecoded;
    private bool _disabled;
    private bool _disposed;

    internal static MsiHostStageEvidence? TryCreate(int parentPid, string hostInstanceId)
    {
        try
        {
            return TryCreateCore(parentPid, Environment.ProcessId, hostInstanceId,
                StopwatchClock.Instance, new MappingFactory());
        }
        catch { return null; }
    }

    internal static MsiHostStageEvidence? TryCreateForTest(int parentPid, int hostPid,
        string hostInstanceId, long qpcFrequency, ISinkFactory factory)
    {
        // Compatibility seam for synthetic tests that do not need deterministic ticks.
        return TryCreateCore(parentPid, hostPid, hostInstanceId,
            new TestClock(qpcFrequency), factory);
    }

    internal static MsiHostStageEvidence? TryCreateForTest(int parentPid, int hostPid,
        string hostInstanceId, IClock clock, ISinkFactory factory)
    {
        // Deterministic clock injection is internal and test-only; production uses
        // the actual process ID and Stopwatch clock through TryCreate above.
        return TryCreateCore(parentPid, hostPid, hostInstanceId, clock, factory);
    }

    private static MsiHostStageEvidence? TryCreateCore(int parentPid, int hostPid,
        string hostInstanceId, IClock clock, ISinkFactory factory)
    {
        ISink? sink = null;
        MsiHostStageEvidence? evidence = null;
        try
        {
            if (parentPid <= 0 || hostPid <= 0 || clock is null || clock.Frequency <= 0 ||
                !IsHostInstance(hostInstanceId) || factory is null)
                return null;
            string name = MapPrefix + parentPid.ToString(CultureInfo.InvariantCulture) + "-" +
                hostPid.ToString(CultureInfo.InvariantCulture) + "-" + hostInstanceId;
            sink = factory.CreateNew(name, Capacity);
            if (sink is null) return null;
            evidence = new MsiHostStageEvidence(sink, name, parentPid, hostPid,
                hostInstanceId, clock);
            evidence.Publish(full: true);
            return evidence;
        }
        catch
        {
            try
            {
                if (evidence is not null) evidence.Disable();
                else sink?.Dispose();
            }
            catch { }
            return null;
        }
    }

    private readonly IClock _clock;

    private MsiHostStageEvidence(ISink sink, string mapName, int parentPid, int hostPid,
        string hostInstanceId, IClock clock)
    {
        _sink = sink;
        _mapName = mapName;
        _clock = clock;
        Put(0, Magic);
        Put(8, 1);
        Put(16, Capacity);
        Put(448, 1); // v1 reserved-tail extensionVersion; never inferred from stamps.
        Put(32, (ulong)hostPid);
        Put(40, (ulong)parentPid);
        Put(48, (ulong)clock.Frequency);
        PutText(hostInstanceId, 192, 200, 64, 1);
        Stamp(Phase.Created, -1);
    }

    private static bool IsHostInstance(string? value)
    {
        if (value is null || value.Length != 32) return false;
        for (int i = 0; i < value.Length; ++i)
        {
            char c = value[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        return Guid.TryParseExact(value, "N", out _);
    }

    internal void BeginRead()
    {
        if (_disabled || _disposed) return;
        try
        {
            // Clear active stamps, tuple metadata, character count, and text bits.
            // The completed response sequence and immutable host identity survive.
            _buffer.AsSpan(72, 112).Clear();
            _buffer.AsSpan(264, 184).Clear();
            _buffer.AsSpan(456, 32).Clear(); // Per-request extension stamps; retain marker at 448.
            Put(176, 1);
            _hasDecoded = false;
            Stamp(Phase.ReadPending, 72);
            Publish(textDirty: true);
        }
        catch { Disable(); }
    }

    internal void EndRead(int incomingChars)
    {
        if (_disabled || _disposed) return;
        try
        {
            // Signed slot: retain the supplied observation, including a negative sentinel.
            Put(168, unchecked((ulong)(long)incomingChars));
            Stamp(Phase.ReadReceived, 80);
            Publish();
        }
        catch { Disable(); }
    }

    internal void BeginHandle() => Checkpoint(Phase.HandlePending, 88);

    internal void Decode(string requestId, ulong sequence, string runId, ulong epoch,
        ulong powerGeneration, string command)
    {
        if (_disabled || _disposed) return;
        try
        {
            Put(144, sequence);
            Put(152, epoch);
            Put(160, powerGeneration);
            PutText(requestId, 264, 272, 64, 2);
            PutText(runId, 336, 344, 64, 4);
            PutText(command, 408, 416, 32, 8);
            _hasDecoded = true;
            Stamp(Phase.CommandDecoded, 96);
            Publish(textDirty: true);
        }
        catch { Disable(); }
    }

    // Both checkpoints bracket the whole TrySubmitFrame handler. The cost interval
    // covers legacy synchronous work or the MSI bounded handoff; it excludes the
    // deferred worker. HELLO advertises which MSI diagnostic path was requested.
    internal void BeginSubmit() => Checkpoint(Phase.SubmitFrameHandler, 104);
    internal void EndSubmit() => Checkpoint(Phase.SubmitFrameHandlerReturned, 112);
    // Switch includes report encoding/SDK dispatch. Return includes exceptional unwind, not consumer proof.
    internal void BeginBackend() => Checkpoint(Phase.BackendSwitch, 456);
    internal void EndBackend() => Checkpoint(Phase.BackendSwitchReturned, 464);
    // Due-window diagnostics include flag refresh and ResetCostWindow; return is not an ACK.
    internal void BeginCost() => Checkpoint(Phase.CostWindow, 472);
    internal void EndCost() => Checkpoint(Phase.CostWindowReturned, 480);
    internal void EndHandle() => Checkpoint(Phase.HandleReturned, 120);
    internal void BeginWrite() => Checkpoint(Phase.WritePending, 128);

    internal void EndWrite()
    {
        if (_disabled || _disposed) return;
        try
        {
            Stamp(Phase.WriteReturned, 136);
            // A parse failure has no decoded sequence. This records only completed
            // response writes, even when Decode contained invalid text or a zero tuple.
            if (_hasDecoded) Put(184, Get(144));
            Publish();
        }
        catch { Disable(); }
    }

    private void Checkpoint(Phase phase, int stampOffset)
    {
        if (_disabled || _disposed) return;
        try
        {
            Stamp(phase, stampOffset);
            Publish();
        }
        catch { Disable(); }
    }

    private void Stamp(Phase phase, int stampOffset)
    {
        ulong now = unchecked((ulong)_clock.GetTimestamp());
        Put(56, (ulong)phase);
        Put(64, now);
        if (stampOffset >= 0) Put(stampOffset, now);
    }

    private void PutText(string? value, int lengthOffset, int bytesOffset, int capacity, ulong bit)
    {
        _buffer.AsSpan(bytesOffset, capacity).Clear();
        Put(176, Get(176) & ~bit);
        Put(lengthOffset, ulong.MaxValue); // signed -1 until the entire field validates
        if (value is null || value.Length > capacity) return;
        for (int i = 0; i < value.Length; ++i)
            if (value[i] < 0x20 || value[i] > 0x7e) return;
        for (int i = 0; i < value.Length; ++i)
            _buffer[bytesOffset + i] = (byte)value[i];
        Put(lengthOffset, (ulong)value.Length);
        Put(176, Get(176) | bit);
    }

    private void Put(int offset, ulong value) =>
        BinaryPrimitives.WriteUInt64LittleEndian(_buffer.AsSpan(offset, 8), value);
    private ulong Get(int offset) =>
        BinaryPrimitives.ReadUInt64LittleEndian(_buffer.AsSpan(offset, 8));

    private void Publish(bool textDirty = false, bool full = false)
    {
        // Reserve each attempt, including failed ones, to prevent sequence reuse/ABA.
        // Never publish the consumer's forbidden zero after the theoretical wrap.
        if (_publicationSequence >= ulong.MaxValue - 1)
            throw new InvalidOperationException("Diagnostic publication sequence exhausted.");
        _publicationSequence += 2;
        _sink.WriteSequence(_publicationSequence - 1);
        Thread.MemoryBarrier();
        if (full)
        {
            // Creation and shutdown repair the entire mapping, including after a failed publish.
            _sink.WriteData(_buffer, 0, PublicationOffset);
            _sink.WriteData(_buffer, PublicationOffset + 8, Capacity - PublicationOffset - 8);
        }
        else
        {
            // Text changes only in BeginRead/Decode; identity and extension version are immutable.
            _sink.WriteData(_buffer, 56, textDirty ? 392 : 136);
            _sink.WriteData(_buffer, 456, 32);
        }
        Thread.MemoryBarrier();
        _sink.WriteSequence(_publicationSequence);
    }

    private void Disable()
    {
        _disabled = true;
        Dispose();
    }

    internal object? FatalSnapshot()
    {
        try
        {
            // Only the pipe writer may inspect this state. Allocation is confined
            // to this explicit fatal snapshot, never the normal checkpoints.
            return new
            {
                scope = SnapshotScope,
                mapName = _mapName,
                version = Get(8), capacity = Get(16),
                publicationSequence = _publicationSequence,
                disabled = _disabled, disposed = _disposed,
                hostPid = Get(32), parentPid = Get(40), qpcFrequency = Get(48),
                phase = Get(56), phaseSinceQpc = Get(64),
                readBegin = Get(72), readEnd = Get(80), handleBegin = Get(88),
                decoded = Get(96), submitBegin = Get(104), submitEnd = Get(112),
                handleEnd = Get(120), writeBegin = Get(128), writeEnd = Get(136),
                extensionVersion = Get(448), backendBegin = Get(456), backendEnd = Get(464),
                costBegin = Get(472), costEnd = Get(480),
                backendEndIsConsumerProof = false, costReturnIsAckProof = false,
                requestSequence = Get(144), epoch = Get(152), powerGeneration = Get(160),
                incomingChars = unchecked((long)Get(168)), textValidityBits = Get(176),
                lastCompletedSequence = Get(184),
                hostInstanceLength = unchecked((long)Get(192)), hostInstanceId = Text(192, 200),
                requestIdLength = unchecked((long)Get(264)), requestId = Text(264, 272),
                runIdLength = unchecked((long)Get(336)), runId = Text(336, 344),
                commandLength = unchecked((long)Get(408)), command = Text(408, 416)
            };
        }
        catch { Disable(); return null; }
    }

    private string? Text(int lengthOffset, int bytesOffset)
    {
        long length = unchecked((long)Get(lengthOffset));
        return length < 0 ? null : Encoding.ASCII.GetString(_buffer, bytesOffset, checked((int)length));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { Stamp(Phase.Closed, -1); Publish(full: true); }
        catch { _disabled = true; }
        try { _sink.Dispose(); }
        catch { _disabled = true; }
    }

    private sealed class TestClock : IClock
    {
        private readonly long _frequency;
        public TestClock(long frequency) => _frequency = frequency;
        public long Frequency => _frequency;
        public long GetTimestamp() => Stopwatch.GetTimestamp();
    }

    private sealed class MappingFactory : ISinkFactory
    {
        public ISink CreateNew(string name, int capacity)
        {
            MemoryMappedFile? map = null;
            MemoryMappedViewAccessor? view = null;
            try
            {
                // Collision/creation failure is diagnostic failure; never OpenExisting.
                map = MemoryMappedFile.CreateNew(name, capacity, MemoryMappedFileAccess.ReadWrite);
                view = map.CreateViewAccessor(0, capacity, MemoryMappedFileAccess.ReadWrite);
                return new MappingSink(map, view);
            }
            catch
            {
                try { view?.Dispose(); } catch { }
                try { map?.Dispose(); } catch { }
                throw;
            }
        }
    }

    private sealed class MappingSink : ISink
    {
        private readonly MemoryMappedFile _map;
        private readonly MemoryMappedViewAccessor _view;
        internal MappingSink(MemoryMappedFile map, MemoryMappedViewAccessor view)
        { _map = map; _view = view; }
        public void WriteSequence(ulong value) => _view.Write(PublicationOffset, value);
        public void WriteData(byte[] buffer, int offset, int count) =>
            _view.WriteArray(offset, buffer, offset, count);
        public void Dispose()
        {
            try { _view.Dispose(); }
            finally { _map.Dispose(); }
        }
    }
}
