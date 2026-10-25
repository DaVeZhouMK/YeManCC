using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using Mono.Cecil;
using System.Text;
using System.Buffers.Binary;

namespace HcDeviceCoreTrim;
static class ClosureVerification
{
    public static object Compare(string sourcePath,string candidatePath,TrimPolicy removalPolicy,IdentityRewriter rewriter,byte[] expectedMetadata,bool rekey,string evidenceDirectory)
    {
        byte[] before=File.ReadAllBytes(sourcePath),after=File.ReadAllBytes(candidatePath);
        using var bp=new PEReader(new MemoryStream(before,false));using var ap=new PEReader(new MemoryStream(after,false));var bm=bp.GetMetadataReader();var am=ap.GetMetadataReader();
        MetadataPeRepack.ValidateLayout(after);
        if(!ap.GetMetadata().GetContent().AsSpan().SequenceEqual(expectedMetadata))throw new InvalidDataException("Candidate metadata differs from the exact approved transformation");
        foreach(TableIndex table in Enum.GetValues<TableIndex>().Where(t=>(int)t<45))if(bm.GetTableRowCount(table)!=am.GetTableRowCount(table))throw new InvalidDataException("Metadata table row count changed: "+table);
        using var ba=Util.Read(sourcePath,out var br);using var brd=br;using var aa=Util.Read(candidatePath,out var ar);using var ard=ar;
        if(ba.Name.Name!=aa.Name.Name||ba.Name.Version!=aa.Name.Version||ba.Name.Culture!=aa.Name.Culture||ba.MainModule.Mvid==aa.MainModule.Mvid)throw new InvalidDataException("Unexpected definition identity");
        var target=rewriter.Targets.SingleOrDefault(t=>t.Name==ba.Name.Name);
        if(rekey)
        {
            if(target==null||!aa.Name.PublicKey.AsSpan().SequenceEqual(Convert.FromHexString(target.NewPublicKeyHex))||!DevelopmentSigning.Verify(before)||!DevelopmentSigning.Verify(after))throw new InvalidDataException("Signing identity/signature mismatch");
        }
        else if(ba.Name.FullName!=aa.Name.FullName)throw new InvalidDataException("Unsigned consumer binding identity changed");
        var beforeFriends=ba.CustomAttributes.Where(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute").Select(a=>(string)a.ConstructorArguments[0].Value).ToArray();
        var afterFriends=aa.CustomAttributes.Where(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute").Select(a=>(string)a.ConstructorArguments[0].Value).ToArray();
        if(!beforeFriends.Select(rewriter.Rewrite).SequenceEqual(afterFriends))throw new InvalidDataException("Friend declaration differs from approved full-public-key retarget");
        var beforeRefs=ba.MainModule.AssemblyReferences.ToArray();var afterRefs=aa.MainModule.AssemblyReferences.ToArray();var referenceRows=new List<object>();
        if(beforeRefs.Length!=afterRefs.Length)throw new InvalidDataException("AssemblyRef count changed");
        for(int i=0;i<beforeRefs.Length;i++)
        {
            var x=beforeRefs[i];var y=afterRefs[i];var change=rewriter.Targets.SingleOrDefault(t=>t.Name==x.Name);
            byte[] expected=change==null?x.PublicKeyToken:Convert.FromHexString(change.NewToken);
            if(x.MetadataToken!=y.MetadataToken||x.Name!=y.Name||x.Version!=y.Version||x.Culture!=y.Culture||!expected.AsSpan().SequenceEqual(y.PublicKeyToken))throw new InvalidDataException("AssemblyRef did not follow selected-name retarget policy: "+x.Name);
            referenceRows.Add(new{Token=$"0x{x.MetadataToken.ToUInt32():x8}",Before=x.FullName,After=y.FullName,Selected=change!=null});
        }
        var btypes=Util.Types(ba.MainModule).ToArray();var atypes=Util.Types(aa.MainModule).ToArray();
        if(!btypes.Select(t=>(t.MetadataToken.ToUInt32(),t.FullName)).SequenceEqual(atypes.Select(t=>(t.MetadataToken.ToUInt32(),t.FullName))))throw new InvalidDataException("Type token/name changed");
        var afields=atypes.SelectMany(t=>t.Fields).ToDictionary(f=>f.MetadataToken.ToUInt32());int initialValues=0;
        foreach(var f in btypes.SelectMany(t=>t.Fields))
        {
            if(!afields.TryGetValue(f.MetadataToken.ToUInt32(),out var other)||f.FullName!=other.FullName||f.RVA!=other.RVA||!f.InitialValue.AsSpan().SequenceEqual(other.InitialValue))throw new InvalidDataException("Field identity/RVA/initializer changed: "+f.FullName);
            if(f.RVA!=0)initialValues++;
        }
        var amethods=atypes.SelectMany(t=>t.Methods).ToDictionary(m=>m.MetadataToken.ToUInt32());var methodRows=new List<object>();int bodies=0;
        foreach(var m in btypes.SelectMany(t=>t.Methods))
        {
            if(!amethods.TryGetValue(m.MetadataToken.ToUInt32(),out var other)||m.FullName!=other.FullName)throw new InvalidDataException("Method token/name/signature changed");
            var bh=MetadataTokens.MethodDefinitionHandle(checked((int)m.MetadataToken.RID));var ah=MetadataTokens.MethodDefinitionHandle(checked((int)other.MetadataToken.RID));int rva=bm.GetMethodDefinition(bh).RelativeVirtualAddress;
            if(rva!=am.GetMethodDefinition(ah).RelativeVirtualAddress)throw new InvalidDataException("Method RVA changed");
            byte[] raw=rva==0?[]:bp.GetSectionData(rva).GetContent(0,bp.GetMethodBody(rva).Size).ToArray(),changed=rva==0?[]:ap.GetSectionData(rva).GetContent(0,ap.GetMethodBody(rva).Size).ToArray();
            if(!raw.AsSpan().SequenceEqual(changed))throw new InvalidDataException("Method header/IL/EH bytes changed: "+m.FullName);
            string semantic=Verification.Semantic(m),afterSemantic=Verification.Semantic(other);
            if(semantic!=afterSemantic)
            {
                // Only known assembly-qualified ldstr values may change through the string heap; instruction bytes stay exact.
                if(!m.HasBody||m.Body.Instructions.Count!=other.Body.Instructions.Count)throw new InvalidDataException("Method semantics differ");
                for(int i=0;i<m.Body.Instructions.Count;i++)
                {
                    var bi=m.Body.Instructions[i];var ai=other.Body.Instructions[i];
                    if(bi.OpCode!=ai.OpCode||bi.Offset!=ai.Offset)throw new InvalidDataException("IL instruction changed");
                    if(bi.Operand is string str&&ai.Operand is string actual){if(rewriter.Rewrite(str)!=actual)throw new InvalidDataException("Unapproved ldstr change");}
                    else if(bi.Operand?.ToString()!=ai.Operand?.ToString())throw new InvalidDataException("Unapproved operand change");
                }
            }
            if(m.HasBody)bodies++;methodRows.Add(new{Token=$"0x{m.MetadataToken.ToUInt32():x8}",Method=m.FullName,Rva=rva,RawBytesEqual=true,RawSha256=Util.Hash(raw),BeforeSemanticSha256=Util.Hash(Encoding.UTF8.GetBytes(semantic)),AfterSemanticSha256=Util.Hash(Encoding.UTF8.GetBytes(afterSemantic)),IdentityTextRetargetOnly=semantic!=afterSemantic});
        }
        if(amethods.Count!=methodRows.Count)throw new InvalidDataException("Extra methods");
        var resources=new List<object>();int removed=0,retained=0,bamlChanged=0;var aresources=aa.MainModule.Resources.OfType<EmbeddedResource>().ToDictionary(r=>r.Name);
        foreach(var b in ba.MainModule.Resources.OfType<EmbeddedResource>())
        {
            if(!aresources.TryGetValue(b.Name,out var a))throw new InvalidDataException("Manifest resource removed");byte[] bd=b.GetResourceData(),ad=a.GetResourceData();
            if(!b.Name.EndsWith(".resources",StringComparison.Ordinal)){if(!bd.AsSpan().SequenceEqual(ad))throw new InvalidDataException("Opaque resource changed");retained++;continue;}
            var bb=ResourceBlob.Read(bd);var ab=ResourceBlob.Read(ad);if(!bb.Header.AsSpan().SequenceEqual(ab.Header)||!bb.Types.SequenceEqual(ab.Types))throw new InvalidDataException("Resource reader/type table changed");
            var entries=ab.Entries.ToDictionary(e=>e.Name,StringComparer.Ordinal);var remove=removalPolicy.Removals.Where(r=>r.Container==b.Name).ToDictionary(r=>r.Name,StringComparer.Ordinal);
            foreach(var entry in bb.Entries)
            {
                if(remove.TryGetValue(entry.Name,out var rm))
                {
                    if(Util.Hash(entry.Raw)!=rm.Sha256||entries.ContainsKey(entry.Name))throw new InvalidDataException("Removal policy mismatch");removed++;resources.Add(new{Container=b.Name,entry.Name,Action="remove-reviewed-headless-asset",Bytes=entry.Raw.Length});continue;
                }
                if(!entries.TryGetValue(entry.Name,out var actual))throw new InvalidDataException("Unapproved resource removal");
                bool eligible=entry.Name.EndsWith(".baml",StringComparison.OrdinalIgnoreCase)||entry.TypeCode==1;byte[] expected=eligible?rewriter.RewriteBytes(entry.Raw):entry.Raw;
                if(entry.Type!=actual.Type||!expected.AsSpan().SequenceEqual(actual.Raw))throw new InvalidDataException("Retained resource has unapproved changes: "+entry.Name);
                bool changed=!entry.Raw.AsSpan().SequenceEqual(actual.Raw);if(changed)bamlChanged++;retained++;
                resources.Add(new{Container=b.Name,entry.Name,Action=changed?"identity-text-retarget-only":"byte-identical",Bytes=entry.Raw.Length});
            }
            if(entries.Count!=bb.Entries.Count-remove.Count)throw new InvalidDataException("Extra resource keys");
        }
        foreach(var s in bp.PEHeaders.SectionHeaders.Where(s=>s.Name is ".rsrc" or ".reloc"))
        {
            var t=ap.PEHeaders.SectionHeaders.Single(x=>x.Name==s.Name);if(s.VirtualAddress!=t.VirtualAddress||!before.AsSpan(s.PointerToRawData,s.SizeOfRawData).SequenceEqual(after.AsSpan(t.PointerToRawData,t.SizeOfRawData)))throw new InvalidDataException("Native resource/relocation bytes changed");
        }
        Directory.CreateDirectory(evidenceDirectory);Util.Save(Path.Combine(evidenceDirectory,"methods.json"),methodRows);Util.Save(Path.Combine(evidenceDirectory,"resources.json"),resources);Util.Save(Path.Combine(evidenceDirectory,"references.json"),referenceRows);
        return new{Passed=true,InputSha256=Util.Hash(before),OutputSha256=Util.Hash(after),InputBytes=before.Length,OutputBytes=after.Length,BeforeIdentity=ba.Name.FullName,AfterIdentity=aa.Name.FullName,Methods=methodRows.Count,MethodBodies=bodies,Types=btypes.Length,Fields=afields.Count,FieldRvaInitialValues=initialValues,AllMethodBodiesHeadersLocalsEhByteIdentical=true,AllDefinitionAndReferenceTokensPreserved=true,AllFieldRvasAndInitialValuesPreserved=true,ExactApprovedMetadataTransformation=true,ResourcesRetained=retained,ResourcesRemoved=removed,ResourcesIdentityTextRetargeted=bamlChanged,NativeResourceAndRelocationBytesPreserved=true,StrongNameRekeyed=rekey,StrongNameValid=rekey?DevelopmentSigning.Verify(after):(bool?)null,Authenticode="not signed; development strong name is binding identity, not publisher authentication",DeviceOrWindowConstructorsExecuted=false};
    }
}

