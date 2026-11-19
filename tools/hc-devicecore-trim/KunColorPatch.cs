using System.Buffers.Binary;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using Mono.Cecil;
using Mono.Cecil.Cil;

namespace HcDeviceCoreTrim;

// Reproduce the reviewed KUN-only color correction without rewriting metadata,
// constructors, device lifecycle or another model's hardware protocol.
static class KunColorPatch
{
    internal const string BeforeSha256 = "08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583";
    internal const string AfterSha256 = "4cd3dea283e26db52e10f36e3de74b2514972d6b5e4e99e1ef952a0a277aa13c";
    public static void Build(string input, string output)
    {
        output = Path.GetFullPath(output);
        if (Directory.Exists(output)) throw new IOException("Require a new output directory");
        var original = File.ReadAllBytes(input);
        if (Util.Hash(original) != BeforeSha256) throw new InvalidDataException("KUN_COLOR_PREIMAGE_MISMATCH");
        using var assembly = Mono.Cecil.AssemblyDefinition.ReadAssembly(new MemoryStream(original));
        var type = assembly.MainModule.Types.Single(t => t.FullName == "HandheldCompanion.Devices.AYANEOKUN");
        var method = type.Methods.Single(m => m.Name == "MapColorValues");
        var getters = method.Body.Instructions.Where(i => i.OpCode == OpCodes.Call &&
            i.Operand is MethodReference r && r.DeclaringType.FullName == "System.Windows.Media.Color" &&
            r.Name is "get_R" or "get_G" or "get_B").ToArray();
        var previous = string.Concat(getters.Select(i => ((MethodReference)i.Operand).Name[^1]));
        // Five branch blocks: zones 1, 2, 3, 4, then the unchanged default.
        if (previous != "GRBGBRBRGBGRRGB") throw new InvalidDataException("KUN_COLOR_METHOD_DIFFERED");
        const string corrected = "GRBGBRGRBGBRRGB"; // 1/3: GRB; 2/4: GBR; other: RGB.
        var tokens = getters.Select(i => (MethodReference)i.Operand).GroupBy(m => m.Name[^1])
            .ToDictionary(g => g.Key, g => g.Select(m => m.MetadataToken.ToInt32()).Distinct().Single());
        using var image = new PEReader(new MemoryStream(original));
        var section = image.PEHeaders.SectionHeaders.Single(s => method.RVA >= s.VirtualAddress && method.RVA < s.VirtualAddress + s.SizeOfRawData);
        var bodyOffset = section.PointerToRawData + method.RVA - section.VirtualAddress;
        var headerBytes = (original[bodyOffset] & 3) == 2 ? 1 : (BinaryPrimitives.ReadUInt16LittleEndian(original.AsSpan(bodyOffset, 2)) >> 12) * 4;
        var codeOffset = bodyOffset + headerBytes;
        var code = image.GetMethodBody(method.RVA).GetILBytes()!;
        if (!original.AsSpan(codeOffset, code.Length).SequenceEqual(code)) throw new InvalidDataException("KUN_COLOR_IL_OFFSET_MISMATCH");
        var result = original.ToArray();
        for (var i = 0; i < getters.Length; i++)
        {
            if (previous[i] == corrected[i]) continue;
            var offset = codeOffset + getters[i].Offset + 1;
            if (original[offset - 1] != 0x28 || BinaryPrimitives.ReadInt32LittleEndian(original.AsSpan(offset, 4)) != tokens[previous[i]])
                throw new InvalidDataException("KUN_COLOR_CALL_OPERAND_MISMATCH");
            BinaryPrimitives.WriteInt32LittleEndian(result.AsSpan(offset, 4), tokens[corrected[i]]);
        }
        var differences = Enumerable.Range(0, original.Length).Where(i => original[i] != result[i]).ToArray();
        if (differences.Length != 4 || differences.Any(i => i < codeOffset || i >= codeOffset + code.Length) || Util.Hash(result) != AfterSha256)
            throw new InvalidDataException("KUN_COLOR_PATCH_ESCAPED_APPROVED_BYTES");
        Directory.CreateDirectory(output);
        File.WriteAllBytes(Path.Combine(output, "HandheldCompanion.dll"), result);
        Util.Save(Path.Combine(output, "KUN-COLOR-CORRECTION.json"), new {
            Status = "REPRODUCED_EXACT_KUN_COLOR_CORRECTION", InputSha256 = BeforeSha256,
            Sha256 = AfterSha256, Bytes = result.Length, ChangedFileBytes = differences,
            UnchangedBytesOutsideFourMethodOperands = true, ExistingAssemblyOnly = true,
            UpstreamCommit = "d79d1216892daad2233c7501b6c24e1b070017e7",
            PhysicalEffectVerified = false, HardwareExecuted = false
        });
        Console.WriteLine($"KUN_COLOR_CORRECTION {AfterSha256} changedBytes={differences.Length}");
    }
}

