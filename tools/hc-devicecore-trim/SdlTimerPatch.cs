using System.Buffers.Binary;
using System.Collections.Immutable;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;
using Mono.Cecil;
using Mono.Cecil.Cil;
namespace HcDeviceCoreTrim;
sealed record TimerSite(int Offset,uint OriginalToken,string OriginalTarget,string Helper);
sealed record TimerPolicy(string InputSha256,uint ConstructorToken,string ConstructorSemanticSha256,List<TimerSite> Sites);
sealed record TimerProduct(byte[] Bytes,byte[] Metadata,uint CounterToken,uint FrequencyToken,Guid Mvid);
static class SdlTimerPatch
{
    internal const string HelperType="HandheldCompanion.YmccDeviceCore.BclPerformanceTimer";
    static int Align(int n,int a)=>checked((n+a-1)/a*a);
    static void Put(byte[] b,int p,int v)=>BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(p,4),v);
    static void Index(BinaryWriter w,int value,int size){if(size==2)w.Write(checked((ushort)value));else w.Write(value);}
    static int Width(int count)=>count>=65536?4:2;
    public static void Build(string input,string policyPath,string output)
    {
        output=Path.GetFullPath(output);if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new InvalidDataException("New empty output required");Directory.CreateDirectory(output);
        var policy=System.Text.Json.JsonSerializer.Deserialize<TimerPolicy>(File.ReadAllText(policyPath),Util.Json)??throw new InvalidDataException("Bad timer policy");
        byte[] before=File.ReadAllBytes(input);if(Util.Hash(before)!=policy.InputSha256)throw new InvalidDataException("Pinned input hash mismatch");
        var built=Transform(before,input,policy);string destination=Path.Combine(output,"HandheldCompanion.dll");File.WriteAllBytes(destination,built.Bytes);
        File.Copy(policyPath,Path.Combine(output,"policy.json"));Verify(input,destination,policy,built,output);
        Util.Save(Path.Combine(output,"CANDIDATE.json"),new{Status="STATIC-VERIFIED-RUNTIME-VALIDATION-PENDING",RuntimeId="YMCC-DEVICECORE-SDL-TIMER-20261018-"+Util.Hash(built.Bytes)[..16],InputSha256=Util.Hash(before),Sha256=Util.Hash(built.Bytes),Bytes=built.Bytes.Length,AddedBytes=built.Bytes.Length-before.Length,built.Mvid,AssemblyIdentity="HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null",StrongName="unsigned",Authenticode="NotSigned",Purpose="YMCC derivative: passive default SDLController timer uses BCL QPC/QPF; device constructors/protocol unchanged",Sites=policy.Sites,HelperType,CounterToken=$"0x{built.CounterToken:x8}",FrequencyToken=$"0x{built.FrequencyToken:x8}",RuntimeValidationPending=true});
        Console.WriteLine($"SDL TIMER CANDIDATE {destination} {built.Bytes.Length} {Util.Hash(built.Bytes)}");
    }
    static TimerProduct Transform(byte[] before,string input,TimerPolicy policy)
    {
        using var pe=new PEReader(new MemoryStream(before,false));var h=pe.PEHeaders;var ph=h.PEHeader!;var md=pe.GetMetadataReader();
        if(ph.CertificateTableDirectory.Size!=0||!md.GetAssemblyDefinition().PublicKey.IsNil||(h.CorHeader!.Flags&CorFlags.StrongNameSigned)!=0)throw new InvalidDataException("This bounded patch accepts only the pinned unsigned HC derivative");
        using var a=Util.Read(input,out var resolver);using var cleanup=resolver;
        var allTypes=Util.Types(a.MainModule).ToArray();if(allTypes.Any(t=>t.FullName==HelperType))throw new InvalidDataException("Timer helper already exists");
        var method=allTypes.SelectMany(t=>t.Methods).Single(m=>m.MetadataToken.ToUInt32()==policy.ConstructorToken);
        if(method.FullName!="System.Void HandheldCompanion.Controllers.SDLController::.ctor()"||!method.HasBody||policy.Sites.Count!=2||Util.Hash(Encoding.UTF8.GetBytes(Verification.Semantic(method)))!=policy.ConstructorSemanticSha256)throw new InvalidDataException("Reviewed default constructor differs");
        foreach(var site in policy.Sites)
        {
            var il=method.Body.Instructions.Single(i=>i.Offset==site.Offset);
            if(il.OpCode!=OpCodes.Call||il.Operand is not MethodReference mr||mr.MetadataToken.ToUInt32()!=site.OriginalToken||mr.FullName!=site.OriginalTarget||mr.DeclaringType.FullName!="SDL3.SDL"||mr.ReturnType.FullName!="System.UInt64"||mr.Parameters.Count!=0||site.Helper!=(mr.Name=="GetPerformanceCounter"?"GetCounter":"GetFrequency"))throw new InvalidDataException("Approved call instruction differs");
        }
        if(policy.Sites.Select(s=>s.Helper).Distinct().Count()!=2)throw new InvalidDataException("Expected exactly counter and frequency");
        var stopwatch=a.MainModule.GetTypeReferences().Single(t=>t.FullName=="System.Diagnostics.Stopwatch");
        var timestamp=a.MainModule.GetMemberReferences().OfType<MethodReference>().Single(m=>m.DeclaringType.FullName==stopwatch.FullName&&m.Name=="GetTimestamp"&&m.ReturnType.FullName=="System.Int64"&&m.Parameters.Count==0&&!m.HasThis);
        var objectType=a.MainModule.GetTypeReferences().Single(t=>t.FullName=="System.Object");
        int oldTypes=md.GetTableRowCount(TableIndex.TypeDef),oldMethods=md.GetTableRowCount(TableIndex.MethodDef),oldMembers=md.GetTableRowCount(TableIndex.MemberRef);
        uint counterToken=0x06000000u+checked((uint)oldMethods+1),frequencyToken=counterToken+1,frequencyFieldToken=0x0a000000u+checked((uint)oldMembers+1);
        byte[] originalMeta=pe.GetMetadata().GetContent().ToArray();var streams=MetadataIdentityEditor.Streams(originalMeta);var data=streams.ToDictionary(s=>s.Name,s=>s.Data.ToArray());
        string tableName=data.ContainsKey("#~")?"#~":"#-";var tableStream=streams.Single(s=>s.Name==tableName);var oldTable=data[tableName];
        if((oldTable[6]&0x40)!=0)throw new InvalidDataException("Extra metadata table data unsupported");
        int stringSize=(oldTable[6]&1)!=0?4:2,blobSize=(oldTable[6]&4)!=0?4:2;
        var strings=new List<byte>(data["#Strings"]);var blobs=new List<byte>(data["#Blob"]);
        int Str(string value){int p=strings.Count;strings.AddRange(Encoding.UTF8.GetBytes(value));strings.Add(0);return p;}
        int Blob(byte[] value){if(value.Length>=128)throw new InvalidDataException("Tiny helper signatures only");int p=blobs.Count;blobs.Add((byte)value.Length);blobs.AddRange(value);return p;}
        int helperName=Str("BclPerformanceTimer"),helperNs=Str("HandheldCompanion.YmccDeviceCore"),counterName=Str("GetCounter"),frequencyName=Str("GetFrequency"),fieldName=Str("Frequency"),u64Signature=Blob([0,0,0x0b]),fieldSignature=Blob([6,0x0a]);
        if((stringSize==2&&strings.Count>=65536)||(blobSize==2&&blobs.Count>=65536))throw new InvalidDataException("Heap width expansion refused");
        byte[] Row(Action<BinaryWriter> write){using var s=new MemoryStream();using(var w=new BinaryWriter(s,Encoding.UTF8,true))write(w);return s.ToArray();}
        int typeDefRefSize=Math.Max(md.GetTableRowCount(TableIndex.TypeDef)+1,Math.Max(md.GetTableRowCount(TableIndex.TypeRef),md.GetTableRowCount(TableIndex.TypeSpec)))>=16384?4:2;
        int memberParentSize=new[]{oldTypes+1,md.GetTableRowCount(TableIndex.TypeRef),md.GetTableRowCount(TableIndex.ModuleRef),oldMethods+2,md.GetTableRowCount(TableIndex.TypeSpec)}.Max()>=8192?4:2;
        byte[] typeRow=Row(w=>{w.Write(0x00100180);Index(w,helperName,stringSize);Index(w,helperNs,stringSize);Index(w,checked((int)objectType.MetadataToken.RID<<2)|1,typeDefRefSize);Index(w,md.GetTableRowCount(TableIndex.Field)+1,Width(md.GetTableRowCount(TableIndex.Field)));Index(w,oldMethods+1,Width(oldMethods+2));});
        byte[] MethodRow(int name)=>Row(w=>{w.Write(0);w.Write((ushort)0);w.Write((ushort)0x0093);Index(w,name,stringSize);Index(w,u64Signature,blobSize);Index(w,md.GetTableRowCount(TableIndex.Param)+1,Width(md.GetTableRowCount(TableIndex.Param)));});
        byte[] memberRow=Row(w=>{Index(w,checked((int)stopwatch.MetadataToken.RID<<3)|1,memberParentSize);Index(w,fieldName,stringSize);Index(w,fieldSignature,blobSize);});
        var additions=new Dictionary<int,byte[]>{{2,typeRow},{6,MethodRow(counterName).Concat(MethodRow(frequencyName)).ToArray()},{10,memberRow}};
        var addedCounts=new Dictionary<int,int>{{2,1},{6,2},{10,1}};
        ulong valid=BinaryPrimitives.ReadUInt64LittleEndian(oldTable.AsSpan(8,8));using var tableOutput=new MemoryStream();tableOutput.Write(oldTable,0,24);using(var w=new BinaryWriter(tableOutput,Encoding.UTF8,true))
        {
            for(int i=0;i<64;i++)if((valid&(1UL<<i))!=0)w.Write(md.GetTableRowCount((TableIndex)i)+(addedCounts.TryGetValue(i,out int n)?n:0));
            for(int i=0;i<64;i++)if((valid&(1UL<<i))!=0)
            {
                int count=md.GetTableRowCount((TableIndex)i),size=md.GetTableRowSize((TableIndex)i),pos=md.GetTableMetadataOffset((TableIndex)i)-tableStream.Offset;
                w.Write(oldTable,pos,checked(count*size));if(additions.TryGetValue(i,out var extra)){if(extra.Length!=size*addedCounts[i])throw new InvalidDataException("Table width changed");w.Write(extra);}
            }
        }
        data[tableName]=tableOutput.ToArray();data["#Strings"]=strings.ToArray();data["#Blob"]=blobs.ToArray();
        Guid mvid=new(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(policy.InputSha256+"|YMCC-SDL-default-timer-v1"))[..16]);var guidStream=streams.Single(s=>s.Name=="#GUID");mvid.ToByteArray().CopyTo(data["#GUID"],PeCompactor.GuidOffset(pe,md)-guidStream.Offset);
        using var metaOutput=new MemoryStream();metaOutput.Write(originalMeta,0,streams.Min(s=>s.Offset));
        foreach(var stream in streams){while((metaOutput.Position&3)!=0)metaOutput.WriteByte(0);int offset=checked((int)metaOutput.Position);metaOutput.Write(data[stream.Name]);long end=metaOutput.Position;metaOutput.Position=stream.HeaderOffset;using(var w=new BinaryWriter(metaOutput,Encoding.UTF8,true)){w.Write(offset);w.Write(data[stream.Name].Length);}metaOutput.Position=end;}
        while((metaOutput.Position&3)!=0)metaOutput.WriteByte(0);byte[] metadata=metaOutput.ToArray();
        var last=h.SectionHeaders[^1];int oldMetaOffset=PeCompactor.Offset(h,h.CorHeader.MetadataDirectory.RelativeVirtualAddress);
        if(last.Name!=".ymcmd"||h.CorHeader.MetadataDirectory.RelativeVirtualAddress!=last.VirtualAddress||oldMetaOffset!=last.PointerToRawData||last.PointerToRawData+last.SizeOfRawData!=before.Length||!before.AsSpan(oldMetaOffset+h.CorHeader.MetadataDirectory.Size,before.Length-oldMetaOffset-h.CorHeader.MetadataDirectory.Size).SequenceEqual(new byte[before.Length-oldMetaOffset-h.CorHeader.MetadataDirectory.Size]))throw new InvalidDataException("Expected the reviewed final isolated metadata section with zero padding");
        int codeRelative=Align(metadata.Length,4),counterRva=last.VirtualAddress+codeRelative,frequencyRva=counterRva+8;
        using(var provider=MetadataReaderProvider.FromMetadataImage(ImmutableArray.CreateRange(metadata)))
        {
            var fresh=provider.GetMetadataReader();for(int i=0;i<64;i++)if((valid&(1UL<<i))!=0&&fresh.GetTableRowSize((TableIndex)i)!=md.GetTableRowSize((TableIndex)i))throw new InvalidDataException("Metadata index widths changed");
            int rows=fresh.GetTableMetadataOffset(TableIndex.MethodDef),size=fresh.GetTableRowSize(TableIndex.MethodDef);Put(metadata,rows+oldMethods*size,counterRva);Put(metadata,rows+(oldMethods+1)*size,frequencyRva);
        }
        int virtualSize=codeRelative+16,rawSize=Align(virtualSize,ph.FileAlignment);var after=new byte[last.PointerToRawData+rawSize];before.AsSpan(0,last.PointerToRawData).CopyTo(after);metadata.CopyTo(after,last.PointerToRawData);
        void Helper(int pos,byte opcode,uint token){after[pos]=0x1e;after[pos+1]=opcode;Put(after,pos+2,unchecked((int)token));after[pos+6]=0x6e;after[pos+7]=0x2a;}
        Helper(last.PointerToRawData+codeRelative,0x28,timestamp.MetadataToken.ToUInt32());Helper(last.PointerToRawData+codeRelative+8,0x7e,frequencyFieldToken);
        int sectionTable=h.PEHeaderStartOffset+h.CoffHeader.SizeOfOptionalHeader,lastHeader=sectionTable+(h.SectionHeaders.Length-1)*40;Put(after,lastHeader+8,virtualSize);Put(after,lastHeader+16,rawSize);Put(after,h.PEHeaderStartOffset+56,Align(last.VirtualAddress+Math.Max(virtualSize,rawSize),ph.SectionAlignment));Put(after,h.PEHeaderStartOffset+8,ph.SizeOfInitializedData-last.SizeOfRawData+rawSize);
        int cor=PeCompactor.Offset(h,ph.CorHeaderTableDirectory.RelativeVirtualAddress);Put(after,cor+12,metadata.Length);
        int methodOffset=PeCompactor.Offset(h,checked((int)method.RVA));int header=(before[methodOffset]&3)==2?1:(BinaryPrimitives.ReadUInt16LittleEndian(before.AsSpan(methodOffset,2))>>12)*4;
        foreach(var site in policy.Sites)Put(after,methodOffset+header+site.Offset+1,unchecked((int)(site.Helper=="GetCounter"?counterToken:frequencyToken)));
        MetadataPeRepack.Rechecksum(after);MetadataPeRepack.ValidateLayout(after);return new(after,metadata,counterToken,frequencyToken,mvid);
    }
    static void Verify(string input,string candidate,TimerPolicy policy,TimerProduct expected,string output)
    {
        byte[] before=File.ReadAllBytes(input),after=File.ReadAllBytes(candidate);if(!after.AsSpan().SequenceEqual(expected.Bytes))throw new InvalidDataException("Output differs from the complete approved image transformation");
        using var bp=new PEReader(new MemoryStream(before,false));using var ap=new PEReader(new MemoryStream(after,false));var bm=bp.GetMetadataReader();var am=ap.GetMetadataReader();
        using var ba=Util.Read(input,out var br);using var bcleanup=br;using var aa=Util.Read(candidate,out var ar);using var acleanup=ar;
        if(ba.Name.FullName!=aa.Name.FullName||ba.MainModule.Mvid==aa.MainModule.Mvid)throw new InvalidDataException("Unexpected assembly/MVID identity");
        var originals=Util.Types(ba.MainModule).SelectMany(t=>t.Methods).ToArray();var candidates=Util.Types(aa.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.MetadataToken.ToUInt32());var rows=new List<object>();int bodies=0,unchanged=0,patched=0;
        foreach(var method in originals)
        {
            var other=candidates[method.MetadataToken.ToUInt32()];if(other.FullName!=method.FullName)throw new InvalidDataException("Original method identity changed");
            byte[] Raw(PEReader pe,Mono.Cecil.MethodDefinition m)=>m.RVA==0?[]:pe.GetSectionData(checked((int)m.RVA)).GetContent(0,pe.GetMethodBody(checked((int)m.RVA)).Size).ToArray();
            byte[] b=Raw(bp,method),a=Raw(ap,other),approved=b.ToArray();bool edit=method.MetadataToken.ToUInt32()==policy.ConstructorToken;
            if(edit){int header=(b[0]&3)==2?1:(BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(0,2))>>12)*4;foreach(var site in policy.Sites)Put(approved,header+site.Offset+1,unchecked((int)(site.Helper=="GetCounter"?expected.CounterToken:expected.FrequencyToken)));patched++;}
            if(method.RVA!=other.RVA||!approved.AsSpan().SequenceEqual(a))throw new InvalidDataException("Unapproved raw body/header/EH change: "+method.FullName);
            string bs=Verification.Semantic(method),as_=Verification.Semantic(other);if(!edit&&bs!=as_)throw new InvalidDataException("Unapproved semantic change");
            if(method.HasBody)bodies++;if(!edit)unchanged++;
            rows.Add(new{Token=$"0x{method.MetadataToken.ToUInt32():x8}",Method=method.FullName,Rva=method.RVA,Action=edit?"two-call-target-operands-only":"byte-and-semantically-identical",BeforeRawSha256=Util.Hash(b),AfterRawSha256=Util.Hash(a),BeforeSemanticSha256=Util.Hash(Encoding.UTF8.GetBytes(bs)),AfterSemanticSha256=Util.Hash(Encoding.UTF8.GetBytes(as_))});
        }
        if(patched!=1||candidates.Count!=originals.Length+2||!candidates[expected.CounterToken].FullName.Contains(HelperType)||!candidates[expected.FrequencyToken].FullName.Contains(HelperType))throw new InvalidDataException("Unexpected added methods");
        var tables=new List<object>();for(int i=0;i<64;i++)
        {
            int count=bm.GetTableRowCount((TableIndex)i);if(count==0)continue;int size=bm.GetTableRowSize((TableIndex)i);var b=bp.GetMetadata().GetContent(bm.GetTableMetadataOffset((TableIndex)i),count*size);var a=ap.GetMetadata().GetContent(am.GetTableMetadataOffset((TableIndex)i),count*size);if(!b.AsSpan().SequenceEqual(a.AsSpan()))throw new InvalidDataException("Original metadata rows changed: "+i);tables.Add(new{Table=((TableIndex)i).ToString(),OriginalRows=count,AddedRows=am.GetTableRowCount((TableIndex)i)-count,AllOriginalRowsByteIdentical=true});
        }
        var fields=Util.Types(aa.MainModule).SelectMany(t=>t.Fields).ToDictionary(f=>f.MetadataToken.ToUInt32());int initialValues=0;
        foreach(var field in Util.Types(ba.MainModule).SelectMany(t=>t.Fields)){var other=fields[field.MetadataToken.ToUInt32()];if(field.FullName!=other.FullName||field.RVA!=other.RVA||!field.InitialValue.AsSpan().SequenceEqual(other.InitialValue))throw new InvalidDataException("Field identity/initial value changed");if(field.RVA!=0)initialValues++;}
        int resources=0;foreach(var resource in ba.MainModule.Resources.OfType<EmbeddedResource>()){var other=aa.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name==resource.Name);if(!resource.GetResourceData().AsSpan().SequenceEqual(other.GetResourceData()))throw new InvalidDataException("Resource changed");resources++;}
        foreach(var s in bp.PEHeaders.SectionHeaders.Where(s=>s.Name is ".rsrc" or ".reloc")){var t=ap.PEHeaders.SectionHeaders.Single(t=>t.Name==s.Name);if(s.VirtualAddress!=t.VirtualAddress||!before.AsSpan(s.PointerToRawData,s.SizeOfRawData).SequenceEqual(after.AsSpan(t.PointerToRawData,t.SizeOfRawData)))throw new InvalidDataException("Native resource/relocation changed");}
        Util.Save(Path.Combine(output,"methods.json"),rows);Util.Save(Path.Combine(output,"metadata-tables.json"),tables);Util.Save(Path.Combine(output,"helper-il.json"),new[]{candidates[expected.CounterToken],candidates[expected.FrequencyToken]}.Select(m=>new{Method=m.FullName,Token=$"0x{m.MetadataToken.ToUInt32():x8}",IL=m.Body.Instructions.Select(i=>i.ToString()).ToArray()}));
        Util.Save(Path.Combine(output,"verification.json"),new{Passed=true,InputSha256=Util.Hash(before),Sha256=Util.Hash(after),OriginalMethods=originals.Length,OriginalBodies=bodies,UnchangedOriginalMethods=unchanged,PatchedOriginalMethods=patched,ChangedCallOperands=2,AddedTypes=1,AddedMethods=2,OriginalMetadataRowsByteIdentical=true,OriginalMethodsRvasPreserved=true,AllFieldRvasAndInitialValuesPreserved=initialValues,EmbeddedResourceContainersByteIdentical=resources,AssemblyReferencesByteIdentical=true,DeviceAndEcIlUnchanged=true,FullImageMatchesApprovedTransformation=true});
    }
}