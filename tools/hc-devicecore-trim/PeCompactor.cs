using System.Buffers.Binary;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

namespace HcDeviceCoreTrim;

// Surgical PE resource compaction: code and all definition/reference tokens keep their original RVAs/identities.
// Only the managed resource directory, ManifestResource offsets, MVID and displaced debug/raw section pointers change.
static class PeCompactor
{
    public static int Offset(PEHeaders h, int rva)
    {
        foreach (var s in h.SectionHeaders)
            if (rva >= s.VirtualAddress && rva < (long)s.VirtualAddress + s.SizeOfRawData) return checked(s.PointerToRawData + rva - s.VirtualAddress);
        throw new InvalidDataException($"Unmapped RVA {rva:x}");
    }
    public static int GuidOffset(PEReader pe, MetadataReader md)
    {
        var bytes = pe.GetMetadata().GetContent().ToArray(); var id = md.GetGuid(md.GetModuleDefinition().Mvid).ToByteArray();
        int start = md.GetHeapMetadataOffset(HeapIndex.Guid), length = md.GetHeapSize(HeapIndex.Guid);
        var hits = new List<int>();
        for (int i = start; i + 16 <= start + length; i += 16) if (bytes.AsSpan(i, 16).SequenceEqual(id)) hits.Add(i);
        if (hits.Count != 1) throw new InvalidDataException("MVID is not uniquely located in #GUID");
        return hits[0];
    }
    static int Align(int n, int a) => checked((n + a - 1) / a * a);
    static int I32(byte[] bytes, int pos) => BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(pos, 4));
    static void Put(byte[] bytes, int pos, int value) => BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(pos, 4), value);
    public static byte[] Compact(byte[] input, Dictionary<string, byte[]> replacement, Guid newMvid, bool rekey = false)
    {
        using var pe = new PEReader(new MemoryStream(input, false)); var h = pe.PEHeaders; var md = pe.GetMetadataReader();
        var cor = h.CorHeader ?? throw new InvalidDataException("Not managed"); var ph = h.PEHeader ?? throw new InvalidDataException("Missing optional header");
        if (!rekey && ((cor.Flags & (CorFlags.StrongNameSigned | CorFlags.NativeEntryPoint)) != 0 || cor.StrongNameSignatureDirectory.Size != 0 || md.GetBlobBytes(md.GetAssemblyDefinition().PublicKey).Length != 0))
            throw new InvalidDataException("Signed/delay-signed input requires an explicit new signing workflow; refused");
        if (ph.CertificateTableDirectory.Size != 0) throw new InvalidDataException("Authenticode certificate present; refused");
        if (cor.ManagedNativeHeaderDirectory.Size != 0 || cor.VtableFixupsDirectory.Size != 0 || cor.ExportAddressTableJumpsDirectory.Size != 0 || cor.CodeManagerTableDirectory.Size != 0)
            throw new InvalidDataException("ReadyToRun/mixed/native managed image not supported");
        if ((cor.Flags & CorFlags.ILOnly) == 0) throw new InvalidDataException("IL-only image required");
        var rd = cor.ResourcesDirectory; if (rd.Size == 0) throw new InvalidDataException("No managed resources");
        int start = Offset(h, rd.RelativeVirtualAddress), oldEnd = checked(start + rd.Size), endRva = checked(rd.RelativeVirtualAddress + rd.Size);
        var section = h.SectionHeaders.Single(s => start >= s.PointerToRawData && oldEnd <= s.PointerToRawData + s.SizeOfRawData);
        if (cor.MetadataDirectory.RelativeVirtualAddress + cor.MetadataDirectory.Size > rd.RelativeVirtualAddress) throw new InvalidDataException("Metadata must precede resources");
        foreach (var handle in md.MethodDefinitions)
        {
            int rva = md.GetMethodDefinition(handle).RelativeVirtualAddress;
            if (rva != 0 && rva + pe.GetMethodBody(rva).Size > rd.RelativeVirtualAddress) throw new InvalidDataException("Method not before resources");
        }
        var resources = md.ManifestResources.ToArray(); var offsets = new List<int>();
        using var stream = new MemoryStream(); using var w = new BinaryWriter(stream);
        foreach (var handle in resources)
        {
            var r = md.GetManifestResource(handle); if (!r.Implementation.IsNil) throw new InvalidDataException("Linked resources require a separate policy");
            string name = md.GetString(r.Name); int oldOffset = checked(start + (int)r.Offset); int length = I32(input, oldOffset);
            if (length < 0 || oldOffset + 4L + length > oldEnd) throw new InvalidDataException("Resource outside directory");
            while ((stream.Position & 7) != 0) w.Write((byte)0);
            offsets.Add(checked((int)stream.Position));
            var bytes = replacement.TryGetValue(name, out var changed) ? changed : input[(oldOffset+4)..(oldOffset+4+length)];
            w.Write(bytes.Length); w.Write(bytes);
        }
        var content = stream.ToArray();
        int optional=h.PEHeaderStartOffset, sectionTable=optional+h.CoffHeader.SizeOfOptionalHeader;
        int newHeaderEnd=sectionTable+(h.SectionHeaders.Length+1)*40;
        int headerGrowth=Align(Math.Max(ph.SizeOfHeaders,newHeaderEnd),ph.FileAlignment)-ph.SizeOfHeaders;
        int oldHeaderEnd=sectionTable+h.SectionHeaders.Length*40;
        if(!input.AsSpan(oldHeaderEnd,ph.SizeOfHeaders-oldHeaderEnd).SequenceEqual(new byte[ph.SizeOfHeaders-oldHeaderEnd]))throw new InvalidDataException("Nonzero header padding");
        if(section.Name!=".text"||h.SectionHeaders[0].Name!=".text")throw new InvalidDataException("Managed resources must be in first .text section");
        int tailRva=endRva/ph.SectionAlignment*ph.SectionAlignment;
        int tailPad=endRva-tailRva, textLength=start-section.PointerToRawData+content.Length;
        int textRaw=Align(textLength,ph.FileAlignment), tailRaw=Align(tailPad+section.PointerToRawData+section.SizeOfRawData-oldEnd,ph.FileAlignment);
        if(section.VirtualAddress+textRaw>tailRva)throw new InvalidDataException("Insufficient saving to split resource tail without changing any original RVAs");
        int tailPointer=section.PointerToRawData+headerGrowth+textRaw, oldTextEnd=section.PointerToRawData+section.SizeOfRawData;
        int newTextEnd=tailPointer+tailRaw, delta=oldTextEnd-newTextEnd;
        if(delta<=0)throw new InvalidDataException("No physical bytes saved");
        var output=new byte[input.Length-delta];
        input.AsSpan(0,ph.SizeOfHeaders).CopyTo(output);input.AsSpan(ph.SizeOfHeaders,start-ph.SizeOfHeaders).CopyTo(output.AsSpan(ph.SizeOfHeaders+headerGrowth));content.CopyTo(output,start+headerGrowth);
        input.AsSpan(oldEnd,oldTextEnd-oldEnd).CopyTo(output.AsSpan(tailPointer+tailPad));
        input.AsSpan(oldTextEnd).CopyTo(output.AsSpan(newTextEnd));
        int MapRaw(int pointer)
        {
            if(pointer>=oldEnd && pointer<oldTextEnd)return tailPointer+tailPad+pointer-oldEnd;
            if(pointer>=oldTextEnd)return pointer-delta;
            if(pointer>=start)throw new InvalidDataException("Raw pointer into compacted resource directory");
            return pointer>=ph.SizeOfHeaders ? pointer+headerGrowth : pointer;
        }
        // Preserve original tail RVAs, including ALL FieldRVA initial values and debug records.
        Put(output,h.CorHeaderStartOffset+headerGrowth+28,content.Length);
        int meta=Offset(h,cor.MetadataDirectory.RelativeVirtualAddress)+headerGrowth, table=md.GetTableMetadataOffset(TableIndex.ManifestResource), rowSize=md.GetTableRowSize(TableIndex.ManifestResource);
        for(int i=0;i<offsets.Count;i++)Put(output,meta+table+i*rowSize,offsets[i]);
        newMvid.ToByteArray().CopyTo(output,meta+GuidOffset(pe,md));
        int sectionCount=h.SectionHeaders.Length;
        BinaryPrimitives.WriteInt16LittleEndian(output.AsSpan(h.CoffHeaderStartOffset+2,2),checked((short)(sectionCount+1)));
        for(int i=sectionCount-1;i>=1;i--)
        {
            var sec=h.SectionHeaders[i];int oldHeader=sectionTable+i*40,newHeader=oldHeader+40;
            input.AsSpan(oldHeader,40).CopyTo(output.AsSpan(newHeader));
            Put(output,newHeader+20,MapRaw(sec.PointerToRawData));
            foreach(int rawPointer in new[]{24,28}){int ptr=I32(input,oldHeader+rawPointer);if(ptr!=0)Put(output,newHeader+rawPointer,MapRaw(ptr));}
        }
        Put(output,optional+60,ph.SizeOfHeaders+headerGrowth);
        Put(output,sectionTable+8,tailRva-section.VirtualAddress);Put(output,sectionTable+16,textRaw);Put(output,sectionTable+20,section.PointerToRawData+headerGrowth);
        int tailHeader=sectionTable+40;Array.Clear(output,tailHeader,40);
        System.Text.Encoding.ASCII.GetBytes(".ymcct").CopyTo(output,tailHeader);
        Put(output,tailHeader+8,section.VirtualAddress+section.VirtualSize-tailRva);
        Put(output,tailHeader+12,tailRva);Put(output,tailHeader+16,tailRaw);Put(output,tailHeader+20,tailPointer);
        var tailCharacteristics=ph.AddressOfEntryPoint>=tailRva ? section.SectionCharacteristics : SectionCharacteristics.ContainsInitializedData|SectionCharacteristics.MemRead;
        Put(output,tailHeader+36,unchecked((int)tailCharacteristics));
        if((section.SectionCharacteristics&SectionCharacteristics.ContainsCode)!=0)Put(output,optional+4,ph.SizeOfCode-section.SizeOfRawData+textRaw+((tailCharacteristics&SectionCharacteristics.ContainsCode)!=0?tailRaw:0));
        Put(output,optional+8,ph.SizeOfInitializedData+((tailCharacteristics&SectionCharacteristics.ContainsInitializedData)!=0?tailRaw:0));
        var debug=ph.DebugTableDirectory;
        if(debug.Size!=0)
        {
            if(debug.Size%28!=0)throw new InvalidDataException("Malformed debug directory");
            int dp=Offset(h,debug.RelativeVirtualAddress),newDp=MapRaw(dp);
            for(int i=0;i<debug.Size;i+=28){int ptr=I32(input,dp+i+24);if(ptr!=0)Put(output,newDp+i+24,MapRaw(ptr));}
        }
        if(h.CoffHeader.PointerToSymbolTable!=0)throw new InvalidDataException("COFF symbol table not supported");
        Put(output,optional+64,0);
        uint checksum=0;
        for(int i=0;i<output.Length;i+=2){checksum+=(uint)(output[i]|(i+1<output.Length?output[i+1]<<8:0));checksum=(checksum&0xffff)+(checksum>>16);}
        checksum=(checksum&0xffff)+(checksum>>16);checksum+=checked((uint)output.Length);Put(output,optional+64,unchecked((int)checksum));
        using var validate=new PEReader(new MemoryStream(output,false));_=validate.GetMetadataReader().GetModuleDefinition();
        if(validate.PEHeaders.SectionHeaders.Any(s=>s.PointerToRawData+s.SizeOfRawData>output.Length))throw new InvalidDataException("Compacted section extends outside file");
        return output;
    }
}


