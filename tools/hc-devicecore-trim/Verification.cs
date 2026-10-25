using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Text;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

namespace HcDeviceCoreTrim;
static class Verification
{
    public static object Compare(string original, string candidate, TrimPolicy policy, string methodsFile)
    {
        byte[] before=File.ReadAllBytes(original), after=File.ReadAllBytes(candidate);
        if(Util.Hash(before)!=policy.InputSha256) throw new InvalidDataException("Input hash differs from pinned policy");
        using var bp=new PEReader(new MemoryStream(before,false)); using var ap=new PEReader(new MemoryStream(after,false));
        var bm=bp.GetMetadataReader(); var am=ap.GetMetadataReader();
        byte[] bmeta=bp.GetMetadata().GetContent().ToArray(), ameta=ap.GetMetadata().GetContent().ToArray();
        if(bmeta.Length!=ameta.Length) throw new InvalidDataException("Metadata length changed");
        // The complete metadata image must match after masking only explicitly allowed identity/layout edits.
        Array.Clear(bmeta,PeCompactor.GuidOffset(bp,bm),16); Array.Clear(ameta,PeCompactor.GuidOffset(ap,am),16);
        foreach(var md in new[]{bm,am})
        {
            byte[] bytes=ReferenceEquals(md,bm)?bmeta:ameta;
            int start=md.GetTableMetadataOffset(TableIndex.ManifestResource), row=md.GetTableRowSize(TableIndex.ManifestResource);
            for(int i=0;i<md.GetTableRowCount(TableIndex.ManifestResource);i++) Array.Clear(bytes,start+i*row,4);
        }
        if(policy.NewPublicKeyHex!=null)
        {
            byte[] bk=bm.GetBlobBytes(bm.GetAssemblyDefinition().PublicKey),ak=am.GetBlobBytes(am.GetAssemblyDefinition().PublicKey);
            if(bk.Length!=ak.Length||!Convert.ToHexString(ak).Equals(policy.NewPublicKeyHex,StringComparison.OrdinalIgnoreCase)||bk.AsSpan().SequenceEqual(ak))throw new InvalidDataException("New YMCC public identity differs from policy or original identity was reused");
            Array.Clear(bmeta,DevelopmentSigning.PublicKeyOffset(bm),bk.Length);Array.Clear(ameta,DevelopmentSigning.PublicKeyOffset(am),ak.Length);
            if(!DevelopmentSigning.Verify(before)||!DevelopmentSigning.Verify(after))throw new InvalidDataException("Original or YMCC strong-name signature invalid");
        }
        if(!bmeta.AsSpan().SequenceEqual(ameta)) throw new InvalidDataException("Unexpected metadata change (types/members/tokens/signatures/attributes/heaps)");
        using var ba=Util.Read(original,out var br); using var ar=br;
        using var aa=Util.Read(candidate,out var cr); using var dr=cr;
        if(ba.Name.Name!=aa.Name.Name||ba.Name.Version!=aa.Name.Version||ba.Name.Culture!=aa.Name.Culture||(policy.NewPublicKeyHex==null&&ba.Name.FullName!=aa.Name.FullName)) throw new InvalidDataException("Assembly binding identity changed");
        if(ba.MainModule.Mvid==aa.MainModule.Mvid)throw new InvalidDataException("Candidate must have a new explicit MVID identity");
        int initialValues=0;
        var afterFields=Util.Types(aa.MainModule).SelectMany(t=>t.Fields).ToDictionary(f=>f.MetadataToken.ToUInt32());
        foreach(var f in Util.Types(ba.MainModule).SelectMany(t=>t.Fields).Where(f=>f.RVA!=0))
        {
            var other=afterFields[f.MetadataToken.ToUInt32()];
            if(f.RVA!=other.RVA||!f.InitialValue.AsSpan().SequenceEqual(other.InitialValue))throw new InvalidDataException("FieldRVA/initial bytes changed: "+f.FullName);
            initialValues++;
        }
        var bmethods=Util.Types(ba.MainModule).SelectMany(t=>t.Methods).ToArray();
        var amethods=Util.Types(aa.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.MetadataToken.ToUInt32());
        var rows=new List<object>();
        foreach(var m in bmethods)
        {
            if(!amethods.TryGetValue(m.MetadataToken.ToUInt32(),out var other)||m.FullName!=other.FullName) throw new InvalidDataException("Method identity changed");
            var def=bm.GetMethodDefinition(MetadataTokens.MethodDefinitionHandle(checked((int)m.MetadataToken.RID)));
            var otherDef=am.GetMethodDefinition(MetadataTokens.MethodDefinitionHandle(checked((int)other.MetadataToken.RID)));
            byte[] raw=def.RelativeVirtualAddress==0?[]:bp.GetSectionData(def.RelativeVirtualAddress).GetContent(0,bp.GetMethodBody(def.RelativeVirtualAddress).Size).ToArray();
            byte[] otherRaw=otherDef.RelativeVirtualAddress==0?[]:ap.GetSectionData(otherDef.RelativeVirtualAddress).GetContent(0,ap.GetMethodBody(otherDef.RelativeVirtualAddress).Size).ToArray();
            if(def.RelativeVirtualAddress!=otherDef.RelativeVirtualAddress||!raw.AsSpan().SequenceEqual(otherRaw)) throw new InvalidDataException("Raw method body/header/EH changed: "+m.FullName);
            string semantic=Semantic(m), otherSemantic=Semantic(other);
            if(semantic!=otherSemantic) throw new InvalidDataException("IL semantics changed: "+m.FullName);
            rows.Add(new { Token=$"0x{m.MetadataToken.ToUInt32():x8}", Method=m.FullName, HasBody=m.HasBody, Rva=def.RelativeVirtualAddress, BodyBytes=raw.Length, RawBodySha256=Util.Hash(raw), SemanticSha256=Util.Hash(Encoding.UTF8.GetBytes(semantic)), Equal=true });
        }
        if(amethods.Count!=bmethods.Length) throw new InvalidDataException("Method count differs");
        var bres=ba.MainModule.Resources.Cast<EmbeddedResource>().ToDictionary(r=>r.Name); var ares=aa.MainModule.Resources.Cast<EmbeddedResource>().ToDictionary(r=>r.Name);
        if(!bres.Keys.Order().SequenceEqual(ares.Keys.Order())) throw new InvalidDataException("Manifest resource names changed");
        int retained=0, removed=0; var resourceRows=new List<object>();
        foreach(var pair in bres)
        {
            byte[] bd=pair.Value.GetResourceData(), ad=ares[pair.Key].GetResourceData();
            var removals=policy.Removals.Where(r=>r.Container==pair.Key).ToDictionary(r=>r.Name);
            if(removals.Count==0)
            {
                if(!bd.AsSpan().SequenceEqual(ad)) throw new InvalidDataException("Unchanged manifest resource differs: "+pair.Key);
                int count=pair.Key.EndsWith(".resources",StringComparison.Ordinal)?ResourceBlob.Read(bd).Entries.Count:1; retained+=count; resourceRows.Add(new {Name=pair.Key,Retained=count,Removed=0,RawBytesEqual=true}); continue;
            }
            var bblob=ResourceBlob.Read(bd); var ablob=ResourceBlob.Read(ad);
            if(!bblob.Header.AsSpan().SequenceEqual(ablob.Header)||!bblob.Types.SequenceEqual(ablob.Types)) throw new InvalidDataException("Resource reader/type table changed");
            var dict=ablob.Entries.ToDictionary(e=>e.Name,StringComparer.Ordinal);
            foreach(var entry in bblob.Entries)
            {
                if(removals.TryGetValue(entry.Name,out var removal))
                {
                    if(dict.ContainsKey(entry.Name)||Util.Hash(entry.Raw)!=removal.Sha256) throw new InvalidDataException("Removed-entry policy differs"); removed++;
                }
                else
                {
                    if(!dict.TryGetValue(entry.Name,out var other)||entry.Type!=other.Type||!entry.Raw.AsSpan().SequenceEqual(other.Raw)) throw new InvalidDataException("Retained entry differs: "+entry.Name);
                    retained++;
                }
            }
            if(dict.Count!=bblob.Entries.Count-removals.Count) throw new InvalidDataException("Unexpected new resource entries");
            resourceRows.Add(new {Name=pair.Key,Retained=dict.Count,Removed=removals.Count,RawBytesEqual=true});
        }
        foreach(var section in bp.PEHeaders.SectionHeaders.Where(s=>s.Name!=".text"))
        {
            var other=ap.PEHeaders.SectionHeaders.Single(s=>s.Name==section.Name);
            if(section.VirtualAddress!=other.VirtualAddress||!before.AsSpan(section.PointerToRawData,section.SizeOfRawData).SequenceEqual(after.AsSpan(other.PointerToRawData,other.SizeOfRawData))) throw new InvalidDataException("Non-text/native resource section changed");
        }
        Util.Save(methodsFile,rows);
        return new { Passed=true, OriginalBytes=before.Length,CandidateBytes=after.Length,SavedBytes=before.Length-after.Length,OriginalSha256=Util.Hash(before),CandidateSha256=Util.Hash(after),MethodsCompared=rows.Count,MethodBodiesCompared=bmethods.Count(m=>m.HasBody),TypeDefinitions=bm.TypeDefinitions.Count,FieldDefinitions=bm.FieldDefinitions.Count,FieldRvaInitialValuesCompared=initialValues,AllFieldRvasAndInitialValuesPreserved=true,MetadataEqualExceptMvidAndManifestResourceOffsets=policy.NewPublicKeyHex==null,MetadataEqualExceptExplicitMvidResourceOffsetsAndNewSigningKey=true,OriginalAssemblyIdentity=ba.Name.FullName,CandidateAssemblyIdentity=aa.Name.FullName,StrongNameIdentityChanged=policy.NewPublicKeyHex!=null,AllTokensAndMemberIdentitiesPreserved=true,AllMethodBodiesHeadersLocalsAndExceptionHandlersByteIdentical=true,RetainedResourceEntries=retained,RemovedResourceEntries=removed,ResourceDetails=resourceRows,NativeResourceSectionsByteIdentical=true,OriginalMvid=ba.MainModule.Mvid,CandidateMvid=aa.MainModule.Mvid,EntryPointPreserved=ba.MainModule.EntryPoint?.MetadataToken==aa.MainModule.EntryPoint?.MetadataToken,HardwareExecuted=false,InspectedAssemblyLoadedIntoRuntime=false };
    }
    public static string Semantic(Mono.Cecil.MethodDefinition m)
    {
        if(!m.HasBody)return m.FullName+"|no-body";
        var b=m.Body; var text=new StringBuilder(m.FullName).Append('|').Append(b.InitLocals).Append('|').Append(b.MaxStackSize).Append('|').Append(b.LocalVarToken.ToUInt32());
        foreach(var v in b.Variables)text.Append("|local:").Append(v.Index).Append(':').Append(v.VariableType.FullName);
        foreach(var i in b.Instructions)text.Append("\n").Append(i.Offset).Append(':').Append(i.OpCode.Code).Append(':').Append(Operand(i.Operand));
        foreach(var e in b.ExceptionHandlers)text.Append("\nEH:").Append(e.HandlerType).Append(':').Append(e.TryStart?.Offset).Append(':').Append(e.TryEnd?.Offset).Append(':').Append(e.HandlerStart?.Offset).Append(':').Append(e.HandlerEnd?.Offset).Append(':').Append(e.FilterStart?.Offset).Append(':').Append(e.CatchType?.FullName);
        return text.ToString();
    }
    static string Operand(object? value)=>value switch
    {
        null=>"", Instruction i=>"target:"+i.Offset, Instruction[] a=>"targets:"+string.Join(',',a.Select(i=>i.Offset)),
        Mono.Cecil.Cil.VariableDefinition v=>"local:"+v.Index, Mono.Cecil.ParameterDefinition p=>"arg:"+p.Index,
        Mono.Cecil.MemberReference r=>r.MetadataToken.ToUInt32()+":"+r.FullName,
        string s=>"string:"+Convert.ToBase64String(Encoding.Unicode.GetBytes(s)),
        float f=>"f32:"+BitConverter.SingleToInt32Bits(f),double d=>"f64:"+BitConverter.DoubleToInt64Bits(d),
        IFormattable f=>f.ToString(null,System.Globalization.CultureInfo.InvariantCulture), _=>value.ToString()??""
    };
}





