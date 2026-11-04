using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;

// Cross-process admission for the existing native OneX vendor owner. Native
// publishes an authoritative generation handoff record only after it has
// quiesced and leased the shared collection; this helper can only join with the
// exact current token and endpoint, must match a fresh owner heartbeat, and must
// leave before native stops or rebinds. The per-generation revocation event is
// a notification, not admission proof.
internal sealed class Oxp3OwnerLease : IDisposable
{
    internal const string RevokedEventPrefix = @"Local\YeManCC-OXP3-VendorRevoked-";
    internal const string HandoffRecordName = @"Local\YeManCC-OXP3-OwnerLease";
    internal const uint HandoffMagic = 0x4F585033u;
    internal const uint HandoffVersion = 2u;
    internal const uint X2MiniProModelTag = 2u;
    internal const uint OneXPlayer3ModelTag = 3u;
    internal const int HandoffRecordSize = 1136;
    internal const int MagicOffset = 0;
    internal const int VersionOffset = 4;
    internal const int SequenceOffset = 8;
    internal const int GenerationOffset = 16;
    internal const int TopologyOffset = 24;
    internal const int AdmissionOffset = 32;
    internal const int HeartbeatOffset = 40;
    internal const int StateOffset = 48;
    internal const int ModelTagOffset = 52;
    internal const int TokenOffset = 56;
    internal const int TokenLength = 40;
    internal const int PathOffset = 96;
    internal const int PathLength = 1040;
    internal const uint StateIdle = 0u;
    internal const uint StateAdmitting = 1u;
    internal const uint StateActive = 2u;
    internal const uint StateRetiring = 3u;
    internal const long HeartbeatToleranceMs = 2000;

    private readonly MemoryMappedFile handoffFile;
    private readonly MemoryMappedViewAccessor handoff;
    private EventWaitHandle? revoked;
    private RegisteredWaitHandle? registration;
    private CancellationTokenSource? cancellation;
    private int revokedFlag;
    private int supersededFlag;
    private bool disposed;

    internal string ExpectedPath { get; }
    internal string Generation { get; }
    internal uint ExpectedModelTag { get; }
    internal CancellationToken Token => cancellation?.Token ?? CancellationToken.None;
    internal bool WasRevoked => Volatile.Read(ref revokedFlag) != 0;
    internal bool WasSuperseded => Volatile.Read(ref supersededFlag) != 0;
    internal bool IsCurrent => !WasRevoked && RecordCurrent(handoff, ExpectedPath, Generation, ExpectedModelTag);

    private Oxp3OwnerLease(string expectedPath, string generation, MemoryMappedFile file, MemoryMappedViewAccessor accessor, uint expectedModelTag)
    {
        ExpectedPath = expectedPath;
        Generation = generation;
        ExpectedModelTag = expectedModelTag;
        handoffFile = file;
        handoff = accessor;
    }

    internal static Oxp3OwnerLease Acquire(string expectedPath, string generation,
        CancellationToken cancellationToken = default, uint expectedModelTag = OneXPlayer3ModelTag)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (expectedModelTag is not (X2MiniProModelTag or OneXPlayer3ModelTag))
            throw new ArgumentOutOfRangeException(nameof(expectedModelTag));
        if (string.IsNullOrWhiteSpace(expectedPath) || string.IsNullOrWhiteSpace(generation))
            throw new ArgumentException("OXP3 owner path/generation token is required");
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("OXP3 owner lease requires Windows");
        var canonical = CanonicalToken(generation);
        cancellationToken.ThrowIfCancellationRequested();
        MemoryMappedFile file;
        try
        {
            file = MemoryMappedFile.OpenExisting(HandoffRecordName, MemoryMappedFileRights.Read);
        }
        catch (IOException exception)
        {
            throw new IOException("OXP3 native owner handoff is not published", exception);
        }
        MemoryMappedViewAccessor? accessor = null;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            accessor = file.CreateViewAccessor(0, HandoffRecordSize, MemoryMappedFileAccess.Read);
            if (!RecordCurrent(accessor, expectedPath, canonical, expectedModelTag))
                throw new IOException("OXP3 native owner admission is not current");
            cancellationToken.ThrowIfCancellationRequested();
            EventWaitHandle nativeEvent;
            try
            {
                nativeEvent = EventWaitHandle.OpenExisting(EventNameFor(canonical));
            }
            catch (Exception exception) when (exception is WaitHandleCannotBeOpenedException or UnauthorizedAccessException)
            {
                throw new IOException("OXP3 native owner admission is not active", exception);
            }
            // Revalidate after the event open: a release/re-admission between the
            // two steps must not let this helper join a stale reservation.
            if (!RecordCurrent(accessor, expectedPath, canonical, expectedModelTag))
            {
                nativeEvent.Dispose();
                throw new IOException("OXP3 native owner admission changed while joining");
            }
            var lease = new Oxp3OwnerLease(expectedPath, canonical, file, accessor, expectedModelTag)
            {
                revoked = nativeEvent,
                cancellation = new CancellationTokenSource()
            };
            accessor = null;
            lease.registration = ThreadPool.RegisterWaitForSingleObject(nativeEvent, (_, timedOut) =>
            {
                if (timedOut) return;
                Volatile.Write(ref lease.revokedFlag, 1);
                try { lease.cancellation?.Cancel(); } catch (ObjectDisposedException) { }
            }, null, Timeout.Infinite, true);
            return lease;
        }
        catch
        {
            accessor?.Dispose();
            file.Dispose();
            throw;
        }
    }

    internal static string CanonicalToken(string generation)
    {
        var parts = generation.Split(':');
        if (parts.Length != 3)
            throw new ArgumentException("OXP3 owner token must be 'generation:topology:admission'");
        if (!ulong.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out var current) ||
            !ulong.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var topology) ||
            !ulong.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out var admission) ||
            admission == 0)
            throw new ArgumentException("OXP3 owner token is malformed");
        var canonical = current.ToString(CultureInfo.InvariantCulture) + ":" +
                        topology.ToString(CultureInfo.InvariantCulture) + ":" +
                        admission.ToString(CultureInfo.InvariantCulture);
        if (!string.Equals(canonical, generation, StringComparison.Ordinal))
            throw new ArgumentException("OXP3 owner token is not canonical");
        return canonical;
    }

    internal static string EventNameFor(string generation) =>
        RevokedEventPrefix + CanonicalToken(generation).Replace(':', '-');

    internal void ValidateInterfacePath(string? actualPath)
    {
        if (string.IsNullOrWhiteSpace(actualPath) ||
            !string.Equals(actualPath, ExpectedPath, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("OXP3 owner interface path mismatch");
    }

    // Called before every write and between wait slices. A revoked event, a
    // republished/expired handoff or a stale heartbeat all stop later writes.
    internal void EnsureCurrent()
    {
        if (WasRevoked || !RecordCurrent(handoff, ExpectedPath, Generation, ExpectedModelTag))
        {
            if (!WasRevoked) Volatile.Write(ref supersededFlag, 1);
            throw new OperationCanceledException("OXP3 owner generation or interface is no longer current");
        }
    }

    private static bool RecordCurrent(MemoryMappedViewAccessor accessor, string expectedPath, string generation, uint expectedModelTag)
    {
        for (var attempt = 0; attempt < 8; attempt++)
        {
            var before = accessor.ReadInt64(SequenceOffset);
            if ((before & 1) != 0) { Thread.Sleep(1); continue; }
            var magic = accessor.ReadUInt32(MagicOffset);
            var version = accessor.ReadUInt32(VersionOffset);
            var state = accessor.ReadUInt32(StateOffset);
            var modelTag = accessor.ReadUInt32(ModelTagOffset);
            var admission = accessor.ReadInt64(AdmissionOffset);
            var heartbeat = accessor.ReadInt64(HeartbeatOffset);
            var tokenBytes = new byte[TokenLength];
            accessor.ReadArray(TokenOffset, tokenBytes, 0, TokenLength);
            var pathBytes = new byte[PathLength];
            accessor.ReadArray(PathOffset, pathBytes, 0, PathLength);
            var after = accessor.ReadInt64(SequenceOffset);
            if (before != after || (after & 1) != 0) { Thread.Sleep(1); continue; }
            if (magic != HandoffMagic || version != HandoffVersion || state != StateActive ||
                modelTag != expectedModelTag) return false;
            var token = TrimAscii(tokenBytes);
            if (!string.Equals(token, generation, StringComparison.Ordinal)) return false;
            var admissionText = generation.Split(':')[2];
            if (!ulong.TryParse(admissionText, NumberStyles.None, CultureInfo.InvariantCulture, out var admissionValue) ||
                admissionValue == 0 || admission != unchecked((long)admissionValue)) return false;
            var path = TrimWide(pathBytes);
            if (!string.Equals(path, expectedPath, StringComparison.OrdinalIgnoreCase)) return false;
            var age = unchecked(Environment.TickCount64 - heartbeat);
            return age >= 0 && age <= HeartbeatToleranceMs;
        }
        return false;
    }

    private static string TrimAscii(byte[] bytes)
    {
        var length = Array.IndexOf(bytes, (byte)0);
        if (length < 0) length = bytes.Length;
        return Encoding.ASCII.GetString(bytes, 0, length);
    }

    private static string TrimWide(byte[] bytes)
    {
        var length = 0;
        while (length + 1 < bytes.Length && (bytes[length] != 0 || bytes[length + 1] != 0)) length += 2;
        return Encoding.Unicode.GetString(bytes, 0, length);
    }

    internal static bool PathsMatch(string? expected, string? actual) =>
        !string.IsNullOrWhiteSpace(expected) && !string.IsNullOrWhiteSpace(actual) &&
        string.Equals(expected, actual, StringComparison.OrdinalIgnoreCase);

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        registration?.Unregister(null);
        registration = null;
        cancellation?.Cancel();
        cancellation?.Dispose();
        cancellation = null;
        revoked?.Dispose();
        revoked = null;
        handoff.Dispose();
        handoffFile.Dispose();
    }
}
