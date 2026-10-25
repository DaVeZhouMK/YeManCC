using System.Buffers.Binary;
using System.Reflection.PortableExecutable;
using System.Text;

namespace HcDeviceCoreTrim;
// Move metadata out of its original physical range, retaining EVERY original code/resource/FieldRVA address.
// The old metadata virtual range becomes zero-fill. File size grows only by section/page alignment, not a duplicate metadata image.
static class MetadataPeRepack
{
    static int Align(int n,int a)=>checked((n+a-1)/a*a);
    static int Read(byte[] b,int p)=>BinaryPrimitives.ReadInt32LittleEndian(b.AsSpan(p,4));
    static void Put(byte[] b,int p,int v)=>BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(p,4),v);
    public static byte[] Replace(byte[] source,byte[] metadata)
    {
        using var pe=new PEReader(new MemoryStream(source,false));var h=pe.PEHeaders;var ph=h.PEHeader??throw new InvalidDataException("Missing PE header");var cor=h.CorHeader??throw new InvalidDataException("Missing CLR header");
        if(ph.CertificateTableDirectory.Size!=0)throw new InvalidDataException("Authenticode-bearing PE requires a separate signing workflow");
        var text=h.SectionHeaders[0];if(text.Name!=".text")throw new InvalidDataException("First section must be .text");
        int oldMeta=PeCompactor.Offset(h,cor.MetadataDirectory.RelativeVirtualAddress),oldMetaEnd=oldMeta+cor.MetadataDirectory.Size;
        int res=PeCompactor.Offset(h,cor.ResourcesDirectory.RelativeVirtualAddress);
        if(oldMeta<text.PointerToRawData||oldMetaEnd>res||res>=text.PointerToRawData+text.SizeOfRawData)throw new InvalidDataException("Unsupported metadata/resource layout");
        int resourceVa=cor.ResourcesDirectory.RelativeVirtualAddress/ph.SectionAlignment*ph.SectionAlignment;
        int resourceRaw=PeCompactor.Offset(h,resourceVa),codeRaw=Align(oldMeta-text.PointerToRawData,ph.FileAlignment);
        if(text.VirtualAddress+codeRaw>resourceVa||resourceRaw<oldMeta)throw new InvalidDataException("Metadata interval cannot be compacted without moving original code");
        int resourceRawSize=text.PointerToRawData+text.SizeOfRawData-resourceRaw;
        int optional=h.PEHeaderStartOffset,table=optional+h.CoffHeader.SizeOfOptionalHeader,newCount=h.SectionHeaders.Length+2;
        int newHeaders=Align(Math.Max(ph.SizeOfHeaders,table+newCount*40),ph.FileAlignment);
        var headers=new byte[newHeaders];source.AsSpan(0,ph.SizeOfHeaders).CopyTo(headers);
        // The new resource section occupies the original text virtual suffix. Existing subsequent sections retain RVAs.
        var parts=new List<(byte[] Header,byte[] Bytes)>();
        byte[] codeHeader=source[table..(table+40)];Put(codeHeader,8,resourceVa-text.VirtualAddress);Put(codeHeader,16,codeRaw);
        var codeBytes=new byte[codeRaw];source.AsSpan(text.PointerToRawData,oldMeta-text.PointerToRawData).CopyTo(codeBytes);parts.Add((codeHeader,codeBytes));
        byte[] resourceHeader=new byte[40];Encoding.ASCII.GetBytes(".ymccr").CopyTo(resourceHeader,0);
        Put(resourceHeader,8,text.VirtualAddress+text.VirtualSize-resourceVa);Put(resourceHeader,12,resourceVa);Put(resourceHeader,16,resourceRawSize);Put(resourceHeader,36,unchecked((int)text.SectionCharacteristics));
        parts.Add((resourceHeader,source[resourceRaw..(text.PointerToRawData+text.SizeOfRawData)]));
        for(int i=1;i<h.SectionHeaders.Length;i++)
        {
            var s=h.SectionHeaders[i];if(s.PointerToRelocations!=0||s.PointerToLineNumbers!=0)throw new InvalidDataException("COFF relocation/line-number pointers not supported");
            parts.Add((source[(table+i*40)..(table+(i+1)*40)],source[s.PointerToRawData..(s.PointerToRawData+s.SizeOfRawData)]));
        }
        if(h.SectionHeaders.Max(s=>s.PointerToRawData+s.SizeOfRawData)!=source.Length||h.CoffHeader.PointerToSymbolTable!=0)throw new InvalidDataException("Unexpected PE overlay/symbol data");
        int metadataVa=ph.SizeOfImage;byte[] metadataHeader=new byte[40];Encoding.ASCII.GetBytes(".ymcmd").CopyTo(metadataHeader,0);
        Put(metadataHeader,8,metadata.Length);Put(metadataHeader,12,metadataVa);Put(metadataHeader,16,Align(metadata.Length,ph.FileAlignment));Put(metadataHeader,36,unchecked((int)(SectionCharacteristics.ContainsInitializedData|SectionCharacteristics.MemRead)));
        byte[] metadataBytes=new byte[Align(metadata.Length,ph.FileAlignment)];metadata.CopyTo(metadataBytes,0);parts.Add((metadataHeader,metadataBytes));
        int cursor=newHeaders;
        for(int i=0;i<parts.Count;i++){Put(parts[i].Header,20,cursor);parts[i].Header.CopyTo(headers,table+i*40);cursor+=parts[i].Bytes.Length;}
        var output=new byte[cursor];headers.CopyTo(output,0);cursor=newHeaders;foreach(var part in parts){part.Bytes.CopyTo(output,cursor);cursor+=part.Bytes.Length;}
        BinaryPrimitives.WriteUInt16LittleEndian(output.AsSpan(h.CoffHeaderStartOffset+2,2),checked((ushort)newCount));Put(output,optional+60,newHeaders);Put(output,optional+56,Align(metadataVa+metadata.Length,ph.SectionAlignment));
        Put(output,optional+4,parts.Where(p=>(unchecked((SectionCharacteristics)(uint)Read(p.Header,36))&SectionCharacteristics.ContainsCode)!=0).Sum(p=>p.Bytes.Length));
        Put(output,optional+8,parts.Where(p=>(unchecked((SectionCharacteristics)(uint)Read(p.Header,36))&SectionCharacteristics.ContainsInitializedData)!=0).Sum(p=>p.Bytes.Length));
        int NewOffset(int rva)
        {
            foreach(var part in parts){int va=Read(part.Header,12),size=Read(part.Header,16);if(rva>=va&&rva<va+size)return Read(part.Header,20)+rva-va;}
            throw new InvalidDataException("Unmapped preserved RVA");
        }
        int corPos=NewOffset(ph.CorHeaderTableDirectory.RelativeVirtualAddress);
        Put(output,corPos+8,metadataVa);Put(output,corPos+12,metadata.Length);
        var debug=ph.DebugTableDirectory;
        if(debug.Size!=0)
        {
            int before=PeCompactor.Offset(h,debug.RelativeVirtualAddress),after=NewOffset(debug.RelativeVirtualAddress);
            for(int i=0;i<debug.Size;i+=28)
            {
                int pointer=Read(source,before+i+24),rva=Read(source,before+i+20);
                if(pointer!=0){if(rva==0)throw new InvalidDataException("Debug payload without RVA not supported");Put(output,after+i+24,NewOffset(rva));}
            }
        }
        Rechecksum(output);ValidateLayout(output);return output;
    }
    public static void Rechecksum(byte[] bytes)
    {
        using var pe=new PEReader(new MemoryStream(bytes,false));int p=pe.PEHeaders.PEHeaderStartOffset+64;Put(bytes,p,0);uint sum=0;
        for(int i=0;i<bytes.Length;i+=2){sum+=(uint)(bytes[i]|(i+1<bytes.Length?bytes[i+1]<<8:0));sum=(sum&0xffff)+(sum>>16);}sum=(sum&0xffff)+(sum>>16);sum+=checked((uint)bytes.Length);Put(bytes,p,unchecked((int)sum));
    }
    public static void ValidateLayout(byte[] bytes)
    {
        using var pe=new PEReader(new MemoryStream(bytes,false));var h=pe.PEHeaders;var ph=h.PEHeader!;int expected=Align(ph.SizeOfHeaders,ph.SectionAlignment);
        foreach(var s in h.SectionHeaders)
        {
            if(s.VirtualAddress!=expected||s.PointerToRawData%ph.FileAlignment!=0||s.SizeOfRawData%ph.FileAlignment!=0||s.PointerToRawData+s.SizeOfRawData>bytes.Length)throw new InvalidDataException("Invalid disk image section layout: "+s.Name);
            expected=Align(s.VirtualAddress+Math.Max(s.VirtualSize,s.SizeOfRawData),ph.SectionAlignment);
        }
        if(expected!=ph.SizeOfImage)throw new InvalidDataException("PE SizeOfImage mismatch");
    }
}

