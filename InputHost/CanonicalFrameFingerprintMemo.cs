using System;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;

// Memoizes only the exact bytes already formatted by CanonicalFrameHash.
// This is independent of report submission, ACKs, and protocol identity.
internal static class CanonicalFrameFingerprintMemo
{
    [ThreadStatic]
    private static Entry? s_lastSuccessful;

    private sealed class Entry
    {
        internal readonly byte[] Bytes = new byte[256];
        internal int Length;
        internal string Fingerprint = string.Empty;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static string GetOrCompute(ReadOnlySpan<byte> canonical)
    {
        var previous = s_lastSuccessful;
        if (previous is not null && previous.Length == canonical.Length &&
            canonical.SequenceEqual(previous.Bytes.AsSpan(0, previous.Length)))
            return previous.Fingerprint;

        return ComputeAndRemember(canonical, previous);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static string ComputeAndRemember(ReadOnlySpan<byte> canonical, Entry? previous)
    {
        Span<byte> hash = stackalloc byte[32];
        SHA256.HashData(canonical, hash);
        var fingerprint = Convert.ToHexStringLower(hash);

        // Formatting in the caller bounds its buffer to 256 bytes. Complete
        // both SHA and encoding before publishing or replacing any cache entry.
        if (canonical.Length <= 256)
        {
            var next = previous ?? new Entry();
            canonical.CopyTo(next.Bytes);
            next.Length = canonical.Length;
            next.Fingerprint = fingerprint;
            s_lastSuccessful = next;
        }
        return fingerprint;
    }
}
