using System.Text;
using System.Text.RegularExpressions;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Buffers.Binary;

namespace HcDeviceCoreTrim;
sealed record IdentityTarget(string Name,string OldToken,string NewToken,string OldPublicKeyHex,string NewPublicKeyHex,string OldFullName,string NewFullName);
sealed class IdentityRewriter(IReadOnlyList<IdentityTarget> targets)
{
    public IReadOnlyList<IdentityTarget> Targets=>targets;
    public string Rewrite(string value)
    {
        string result=value;
        foreach(var t in targets)
        {
            // A preceding binary string-length byte may be a printable word character. Exact complete identity
            // values must therefore be recognized independently of regex text boundaries.
            result=result.Replace(t.OldFullName,t.NewFullName,StringComparison.OrdinalIgnoreCase);
            result=result.Replace(t.Name+", PublicKey="+t.OldPublicKeyHex,t.Name+", PublicKey="+t.NewPublicKeyHex.ToLowerInvariant(),StringComparison.OrdinalIgnoreCase);
            string version=new System.Reflection.AssemblyName(t.OldFullName).Version!.ToString();
            result=result.Replace(t.Name+";v"+version+";"+t.OldToken+";component",t.Name+";v"+version+";"+t.NewToken+";component",StringComparison.OrdinalIgnoreCase);
            result=result.Replace(t.Name+";"+t.OldToken+";component",t.Name+";"+t.NewToken+";component",StringComparison.OrdinalIgnoreCase);
            string name=Regex.Escape(t.Name),token=Regex.Escape(t.OldToken);
            result=Regex.Replace(result,@"(?<p>(?<![\w.])"+name+@",\s*(?:Version=\d+(?:\.\d+){1,3},\s*)?(?:Culture=[\w-]+,\s*)?PublicKeyToken=)"+token+@"(?![0-9a-f])",m=>m.Groups["p"].Value+t.NewToken,RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
            result=Regex.Replace(result,@"(?<p>(?<![\w.])"+name+@";(?:v[^;\x00\r\n]+;)?(?:PublicKeyToken=)?)"+token+@"(?=;component)",m=>m.Groups["p"].Value+t.NewToken,RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
            result=Regex.Replace(result,@"(?<p>(?<![\w.])"+name+@",\s*PublicKey=)"+Regex.Escape(t.OldPublicKeyHex)+@"(?![0-9a-f])",m=>m.Groups["p"].Value+t.NewPublicKeyHex.ToLowerInvariant(),RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
        }
        if(result.Length!=value.Length)throw new InvalidDataException("Identity retarget must preserve text length");return result;
    }
    public byte[] RewriteBytes(byte[] source)
    {
        byte[] bytes=Encoding.Latin1.GetBytes(Rewrite(Encoding.Latin1.GetString(source)));
        // Widened ASCII runs cover UTF-16 strings at either alignment without decoding arbitrary binary data.
        for(int i=0;i+1<bytes.Length;i++)
        {
            if(bytes[i]<32||bytes[i]>126||bytes[i+1]!=0)continue;
            int start=i,end=i;
            while(end+1<bytes.Length&&bytes[end]>=32&&bytes[end]<=126&&bytes[end+1]==0)end+=2;
            char[] chars=new char[(end-start)/2];for(int j=0;j<chars.Length;j++)chars[j]=(char)bytes[start+j*2];
            string text=new(chars),changed=Rewrite(text);
            for(int j=0;j<chars.Length;j++)bytes[start+j*2]=checked((byte)changed[j]);i=end-1;
        }
        return bytes;
    }
    public TextIdentity[] TextIdentities(byte[] bytes)
    {
        const string pattern=@"(?<name>iNKORE[\w.]+),\s*Version=(?<version>[\d.]+),\s*Culture=(?<culture>[\w-]+),\s*PublicKeyToken=(?<token>[a-fA-F0-9]{16})";
        var values=new List<string>{Encoding.Latin1.GetString(bytes)};
        for(int i=0;i+1<bytes.Length;i++)
        {
            if(bytes[i]<32||bytes[i]>126||bytes[i+1]!=0)continue;int start=i,end=i;
            while(end+1<bytes.Length&&bytes[end]>=32&&bytes[end]<=126&&bytes[end+1]==0)end+=2;
            values.Add(Encoding.Unicode.GetString(bytes,start,end-start));i=end-1;
        }
        return values.SelectMany(s=>Regex.Matches(s,pattern).Select(m=>new TextIdentity(m.Groups["name"].Value,m.Groups["version"].Value,m.Groups["token"].Value.ToLowerInvariant()))).DistinctBy(x=>System.Text.Json.JsonSerializer.Serialize(x)).ToArray();
    }
}
sealed record TextIdentity(string Name,string Version,string Token);
sealed record MetadataStream(string Name,int HeaderOffset,int Offset,int Size,byte[] Data);
sealed record MetadataEdit(byte[] Bytes,object[] AssemblyRefEdits,object[] TextEdits,int AppendedBlobBytes);
static class MetadataIdentityEditor
{
    static int Align(int n)=>(n+3)&~3;
    public static List<MetadataStream> Streams(byte[] metadata)
    {
        using var r=new BinaryReader(new MemoryStream(metadata,false),Encoding.UTF8);
        if(r.ReadUInt32()!=0x424a5342)throw new InvalidDataException("Bad metadata root");r.ReadUInt16();r.ReadUInt16();r.ReadUInt32();int version=r.ReadInt32();r.BaseStream.Position+=version;r.ReadUInt16();int count=r.ReadUInt16();
        var streams=new List<MetadataStream>();
        for(int i=0;i<count;i++)
        {
            int h=checked((int)r.BaseStream.Position),offset=r.ReadInt32(),size=r.ReadInt32();var chars=new List<byte>();byte b;
            while((b=r.ReadByte())!=0)chars.Add(b);r.BaseStream.Position=Align(checked((int)r.BaseStream.Position));
            if(offset<0||size<0||offset+(long)size>metadata.Length)throw new InvalidDataException("Invalid metadata stream");
            streams.Add(new(Encoding.ASCII.GetString(chars.ToArray()),h,offset,size,metadata[offset..(offset+size)]));
        }
        return streams;
    }
    public static MetadataEdit Edit(System.Reflection.PortableExecutable.PEReader pe,IdentityRewriter rewriter,string? ownNewPublicKey,Guid mvid)
    {
        var md=pe.GetMetadataReader();byte[] original=pe.GetMetadata().GetContent().ToArray();var streams=Streams(original);
        var data=streams.ToDictionary(s=>s.Name,s=>s.Data.ToArray());string tableName=data.ContainsKey("#~")?"#~":"#-";var tableStream=streams.Single(s=>s.Name==tableName);
        var table=data[tableName];var blobStream=streams.Single(s=>s.Name=="#Blob");var blob=new List<byte>(data["#Blob"]);
        var edits=new List<object>();var textEdits=new List<object>();int indexBytes=(table[6]&4)!=0?4:2;var allocated=new Dictionary<string,int>(StringComparer.Ordinal);
        int Allocate(byte[] bytes)
        {
            string key=Convert.ToHexString(bytes);if(allocated.TryGetValue(key,out int existing))return existing;int handle=blob.Count;
            if(bytes.Length<128)blob.Add((byte)bytes.Length);else if(bytes.Length<16384){blob.Add((byte)(0x80|(bytes.Length>>8)));blob.Add((byte)bytes.Length);}else throw new InvalidDataException("Unexpected identity blob size");
            blob.AddRange(bytes);allocated.Add(key,handle);return handle;
        }
        foreach(var handle in md.AssemblyReferences)
        {
            var row=md.GetAssemblyReference(handle);string name=md.GetString(row.Name);var target=rewriter.Targets.SingleOrDefault(t=>t.Name==name);if(target==null)continue;
            byte[] old=md.GetBlobBytes(row.PublicKeyOrToken);bool full=(row.Flags&System.Reflection.AssemblyFlags.PublicKey)!=0;
            byte[] expectedOld=Convert.FromHexString(full?target.OldPublicKeyHex:target.OldToken),changed=Convert.FromHexString(full?target.NewPublicKeyHex:target.NewToken);
            if(!old.AsSpan().SequenceEqual(expectedOld))throw new InvalidDataException("Unexpected source AssemblyRef identity: "+name);
            int newHandle=Allocate(changed),pos=md.GetTableMetadataOffset(TableIndex.AssemblyRef)-tableStream.Offset+(MetadataTokens.GetRowNumber(handle)-1)*md.GetTableRowSize(TableIndex.AssemblyRef)+12;
            if(indexBytes==2)BinaryPrimitives.WriteUInt16LittleEndian(table.AsSpan(pos,2),checked((ushort)newHandle));else BinaryPrimitives.WriteInt32LittleEndian(table.AsSpan(pos,4),newHandle);
            edits.Add(new{Token=$"0x{MetadataTokens.GetToken(handle):x8}",Name=name,OldBlobHandle=MetadataTokens.GetHeapOffset(row.PublicKeyOrToken),NewBlobHandle=newHandle,OldIdentity=Convert.ToHexString(old).ToLowerInvariant(),NewIdentity=Convert.ToHexString(changed).ToLowerInvariant(),FullPublicKey=full});
        }
        if(indexBytes==2&&blob.Count>=65536)throw new InvalidDataException("Blob heap index-width expansion is not authorized");
        data["#Blob"]=blob.ToArray();
        foreach(string name in new[]{"#Blob","#US","#Strings"})
        {
            if(!data.TryGetValue(name,out var raw))continue;var changed=rewriter.RewriteBytes(raw);
            if(!raw.AsSpan().SequenceEqual(changed))textEdits.Add(new{Stream=name,BeforeSha256=Util.Hash(raw),AfterSha256=Util.Hash(changed),SameLength=raw.Length==changed.Length});data[name]=changed;
        }
        if(ownNewPublicKey!=null)
        {
            var h=md.GetAssemblyDefinition().PublicKey;byte[] old=md.GetBlobBytes(h),key=Convert.FromHexString(ownNewPublicKey);
            if(key.Length!=old.Length)throw new InvalidDataException("Signing identity must preserve original public key blob size");
            int offset=DevelopmentSigning.PublicKeyOffset(md)-blobStream.Offset;key.CopyTo(data["#Blob"],offset);
        }
        var guidStream=streams.Single(s=>s.Name=="#GUID");mvid.ToByteArray().CopyTo(data["#GUID"],PeCompactor.GuidOffset(pe,md)-guidStream.Offset);
        int headerEnd=streams.Min(s=>s.Offset);using var output=new MemoryStream();output.Write(original,0,headerEnd);
        foreach(var s in streams)
        {
            while((output.Position&3)!=0)output.WriteByte(0);int offset=checked((int)output.Position);var payload=data[s.Name];output.Write(payload);long end=output.Position;
            output.Position=s.HeaderOffset;using(var w=new BinaryWriter(output,Encoding.UTF8,true)){w.Write(offset);w.Write(payload.Length);}output.Position=end;
        }
        while((output.Position&3)!=0)output.WriteByte(0);
        return new(output.ToArray(),edits.ToArray(),textEdits.ToArray(),blob.Count-blobStream.Size);
    }
}

