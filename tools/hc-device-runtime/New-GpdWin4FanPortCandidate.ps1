# Produce a review-only HC candidate. No assembly load, device access or install.
param(
    [Parameter(Mandatory)][string]$InputDll,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$extractor = @"
using System;
using System.IO;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

public static class GpdWin4FanPortCandidate
{
    private static readonly byte[] Before = { 0x03, 0x1f, 0x2e, 0xfe, 0x01, 0x16, 0xfe, 0x01 };
    // Both 0x2e and 0x2f select slot 0. Every other input keeps its old slot.
    // Same-size IL expression: ((port ^ 0x2e) > 1u) ? 1 : 0.
    private static readonly byte[] After = { 0x03, 0x1f, 0x2e, 0x61, 0x17, 0xfe, 0x03, 0x00 };
    private const int SlotExpressionOffset = 29;

    public static int[] Rewrite(byte[] input, byte[] output)
    {
        using var pe = new PEReader(new MemoryStream(input));
        var md = pe.GetMetadataReader();
        var methods = new int[2];
        int found = 0;
        foreach (var handle in md.TypeDefinitions)
        {
            var type = md.GetTypeDefinition(handle);
            if (md.GetString(type.Namespace) != "HandheldCompanion" || md.GetString(type.Name) != "OpenLibSys") continue;
            foreach (var methodHandle in type.GetMethods())
            {
                var method = md.GetMethodDefinition(methodHandle);
                var name = md.GetString(method.Name);
                if (name != "ReadIoPortByte" && name != "WriteIoPortByte") continue;
                int rva = method.RelativeVirtualAddress;
                int sectionIndex = pe.PEHeaders.GetContainingSectionIndex(rva);
                if (sectionIndex < 0 || found == methods.Length) throw new InvalidDataException("Unexpected method section");
                var section = pe.PEHeaders.SectionHeaders[sectionIndex];
                int body = section.PointerToRawData + rva - section.VirtualAddress;
                var il = pe.GetMethodBody(rva).GetILBytes();
                int code = body + ((input[body] & 3) == 2 ? 1 : (input[body + 1] >> 4) * 4);
                int start = code + SlotExpressionOffset;
                if (!input.AsSpan(code, il.Length).SequenceEqual(il) || !input.AsSpan(start, Before.Length).SequenceEqual(Before))
                    throw new InvalidDataException(name + " slot selector preimage mismatch");
                After.CopyTo(output, start);
                methods[found++] = start;
            }
        }
        if (found != methods.Length || methods[0] == methods[1]) throw new InvalidDataException("Expected two distinct slot selectors");
        return methods;
    }

    public static int[] Replay(byte[] input, byte[] output, int[] offsets)
    {
        int unchanged = 0;
        foreach (int offset in offsets)
        {
            var oldSlot = Selector(input, offset);
            var newSlot = Selector(output, offset);
            // The pinned PawnIO module permits the configured index/data pair.
            // Reproduce the Win4 data-port rejection on both original paths.
            if (Allowed(oldSlot, 0x2f) || !Allowed(newSlot, 0x2f)) throw new InvalidDataException("Win4 counterexample did not flip");
            foreach (int port in new[] { 0x2e, 0x4e, 0x4f })
                if (!Allowed(oldSlot, port) || !Allowed(newSlot, port)) throw new InvalidDataException("A working port regressed");
            for (int port = 0; port <= ushort.MaxValue; port++)
                if (port != 0x2f)
                {
                    if (oldSlot(0, port) != newSlot(0, port)) throw new InvalidDataException("Changed an unrelated port");
                    unchanged++;
                }
        }
        int bytesChanged = 0;
        for (int i = 0; i < input.Length; i++)
        {
            if (input[i] == output[i]) continue;
            if (Array.TrueForAll(offsets, start => i < start || i >= start + After.Length))
                throw new InvalidDataException("Changed bytes outside the selected expressions");
            bytesChanged++;
        }
        return new[] { unchanged, bytesChanged };
    }

    private static bool Allowed(Func<int, int, int> select, int port)
    {
        int index = select(0, port) == 0 ? 0x2e : 0x4e;
        return port == index || port == index + 1;
    }

    private static Func<int, int, int> Selector(byte[] image, int offset)
    {
        var method = new DynamicMethod("RunVerifiedSlotExpression", typeof(int), new[] { typeof(int), typeof(int) });
        var info = method.GetDynamicILInfo();
        var code = new byte[Before.Length + 1];
        Array.Copy(image, offset, code, 0, Before.Length);
        code[Before.Length] = 0x2a; // ret; execute only the verified integer expression.
        info.SetLocalSignature(new byte[] { 0x07, 0x00 });
        info.SetCode(code, 2);
        return (Func<int, int, int>)method.CreateDelegate(typeof(Func<int, int, int>));
    }
}
"@
$refs = @([System.Reflection.Metadata.PEReaderExtensions].Assembly.Location,
          [System.Collections.Immutable.ImmutableArray].Assembly.Location,
          (Join-Path $PSHOME 'ref/System.Memory.dll'),
          (Join-Path $PSHOME 'ref/System.Reflection.Emit.Lightweight.dll'))
Add-Type -ReferencedAssemblies $refs -TypeDefinition $extractor
$source = (Resolve-Path -LiteralPath $InputDll).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Require a new output directory' }
$inputBytes = [IO.File]::ReadAllBytes($source)
$inputHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($inputBytes)).ToLowerInvariant()
if ($inputHash -cne '4cd3dea283e26db52e10f36e3de74b2514972d6b5e4e99e1ef952a0a277aa13c') {
    throw 'GPD_WIN4_FAN_PORT_PREIMAGE_MISMATCH'
}
$candidate = [byte[]]$inputBytes.Clone()
$offsets = [GpdWin4FanPortCandidate]::Rewrite($inputBytes, $candidate)
$checks = [GpdWin4FanPortCandidate]::Replay($inputBytes, $candidate, $offsets)
$candidateHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($candidate)).ToLowerInvariant()
if ($checks[1] -ne 10 -or $candidateHash -cne 'f79edd9dc1c60d5b5e9f3c456174b3c049ea6087ea0c988facd95bc1c174b93c') {
    throw 'GPD_WIN4_FAN_PORT_CANDIDATE_MISMATCH'
}
[void][IO.Directory]::CreateDirectory($output)
[IO.File]::WriteAllBytes((Join-Path $output 'HandheldCompanion.dll'), $candidate)
$receipt = [ordered]@{
    scope = 'offline exact slot selector IL and pinned PawnIO port-pair contract'
    inputSha256 = $inputHash
    candidateSha256 = $candidateHash
    pawnIoModuleSha256 = '3dcf8b2bc80ff642d97c4608511a818642b5bf315ff53df3df393d043e71d101'
    patchedMethodCount = $offsets.Count
    changedFileBytes = $checks[1]
    unchangedOtherSelectors = $checks[0]
    originalDataPortAccepted = $false
    candidateDataPortAccepted = $true
    hardwareExecuted = $false
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding utf8
$receipt
