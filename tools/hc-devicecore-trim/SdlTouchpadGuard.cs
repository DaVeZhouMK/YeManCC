using System.Buffers.Binary;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;
using Mono.Cecil;
namespace HcDeviceCoreTrim;
sealed record TouchpadMethod(uint Token,string FullName,string SemanticSha256);
sealed record TouchpadPolicy(string InputSha256,List<TouchpadMethod> Methods);
static class SdlTouchpadGuard
{
    static int Align(int n,int a)=>checked((n+a-1)/a*a);
    static void Put(byte[] bytes,int p,int value)=>BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(p,4),value);
    public static void Build(string input,string policyPath,string output)
    {
        var policy=System.Text.Json.JsonSerializer.Deserialize<TouchpadPolicy>(File.ReadAllText(policyPath),Util.Json)??throw new InvalidDataException("Invalid touchpad policy");
        byte[] before=File.ReadAllBytes(input);if(Util.Hash(before)!=policy.InputSha256)throw new InvalidDataException("Pinned timer-derived input hash differs");
        if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new InvalidDataException("New empty output directory required");Directory.CreateDirectory(output);
        using var bp=new PEReader(new MemoryStream(before,false));var h=bp.PEHeaders;var ph=h.PEHeader!;var bm=bp.GetMetadataReader();
        if(ph.CertificateTableDirectory.Size!=0||!bm.GetAssemblyDefinition().PublicKey.IsNil||(h.CorHeader!.Flags&CorFlags.StrongNameSigned)!=0)throw new InvalidDataException("Only the pinned unsigned derivative is approved");
        if(policy.Methods.Count!=2||!policy.Methods.Select(m=>m.Token).Order().SequenceEqual(new uint[]{0x06002847,0x06002848}))throw new InvalidDataException("Exactly two approved getters required");
        using var ba=Util.Read(input,out var br);using var bcleanup=br;
        var bmethods=Util.Types(ba.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.MetadataToken.ToUInt32());
        var plans=new List<(Mono.Cecil.MethodDefinition Method,byte[] Body,byte[] Guard,int Rva)>();var last=h.SectionHeaders[^1];int cursor=Align(last.VirtualSize,4);
        if(last.Name!=".ymcmd"||last.PointerToRawData+last.SizeOfRawData!=before.Length||h.CorHeader.MetadataDirectory.RelativeVirtualAddress!=last.VirtualAddress)throw new InvalidDataException("Expected isolated final metadata/helper section");
        if(!before.AsSpan(last.PointerToRawData+last.VirtualSize,last.SizeOfRawData-last.VirtualSize).SequenceEqual(new byte[last.SizeOfRawData-last.VirtualSize]))throw new InvalidDataException("Unexpected nonzero final section padding");
        foreach(var approved in policy.Methods)
        {
            var m=bmethods[approved.Token];if(m.FullName!=approved.FullName||!m.HasBody||Util.Hash(Encoding.UTF8.GetBytes(Verification.Semantic(m)))!=approved.SemanticSha256)throw new InvalidDataException("Reviewed getter differs");
            if(m.DeclaringType.FullName!="HandheldCompanion.Controllers.SDLController"||m.ReturnType.FullName!="System.Int32"||m.Body.HasExceptionHandlers||m.Body.HasVariables||m.Body.InitLocals)throw new InvalidDataException("Only the reviewed tiny getter bodies are allowed");
            var il=m.Body.Instructions;if(il[0].OpCode!=Mono.Cecil.Cil.OpCodes.Ldarg_0||il[1].OpCode!=Mono.Cecil.Cil.OpCodes.Ldfld||il[1].Operand is not FieldReference field||field.Name!="gamepad"||field.FieldType.FullName!="System.IntPtr")throw new InvalidDataException("Expected original gamepad field load");
            string target=m.Name=="GetTouchpads"?"System.Int32 SDL3.SDL::GetNumGamepadTouchpads(System.IntPtr)":"System.Int32 SDL3.SDL::GetNumGamepadTouchpadFingers(System.IntPtr,System.Int32)";
            if(il[^2].OpCode!=Mono.Cecil.Cil.OpCodes.Call||il[^2].Operand is not MethodReference mr||mr.FullName!=target||il[^1].OpCode!=Mono.Cecil.Cil.OpCodes.Ret||il.Count!=(m.Name=="GetTouchpads"?4:5))throw new InvalidDataException("Expected original straight native wrapper call and return");
            int oldPos=PeCompactor.Offset(h,checked((int)m.RVA));if((before[oldPos]&3)!=2)throw new InvalidDataException("Original getter must have a tiny header");
            byte[] original=bp.GetMethodBody(checked((int)m.RVA)).GetILBytes()??throw new InvalidDataException("Missing IL");byte[] guard=new byte[10];guard[0]=0x02;guard[1]=0x7b;Put(guard,2,unchecked((int)field.MetadataToken.ToUInt32()));guard[6]=0x2d;guard[7]=2;guard[8]=0x16;guard[9]=0x2a;
            byte[] code=guard.Concat(original).ToArray();if(code.Length>=64)throw new InvalidDataException("Tiny guard overflow");byte[] body=new byte[code.Length+1];body[0]=checked((byte)((code.Length<<2)|2));code.CopyTo(body,1);
            plans.Add((m,body,guard,last.VirtualAddress+cursor));cursor=Align(cursor+body.Length,4);
        }
        int rawSize=Align(Math.Max(cursor,last.SizeOfRawData),ph.FileAlignment);byte[] after=new byte[last.PointerToRawData+rawSize];before.CopyTo(after,0);
        int metadata=PeCompactor.Offset(h,h.CorHeader.MetadataDirectory.RelativeVirtualAddress),methodRows=bm.GetTableMetadataOffset(TableIndex.MethodDef),rowSize=bm.GetTableRowSize(TableIndex.MethodDef);
        foreach(var plan in plans){plan.Body.CopyTo(after,last.PointerToRawData+plan.Rva-last.VirtualAddress);Put(after,metadata+methodRows+checked(((int)plan.Method.MetadataToken.RID-1)*rowSize),plan.Rva);}
        Guid mvid=new(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(policy.InputSha256+"|YMCC-SDL-zero-touchpad-guard-v1"))[..16]);mvid.ToByteArray().CopyTo(after,metadata+PeCompactor.GuidOffset(bp,bm));
        int table=h.PEHeaderStartOffset+h.CoffHeader.SizeOfOptionalHeader,lastHeader=table+(h.SectionHeaders.Length-1)*40;
        Put(after,lastHeader+8,cursor);Put(after,lastHeader+16,rawSize);Put(after,h.PEHeaderStartOffset+56,Align(last.VirtualAddress+Math.Max(cursor,rawSize),ph.SectionAlignment));Put(after,h.PEHeaderStartOffset+8,ph.SizeOfInitializedData-last.SizeOfRawData+rawSize);
        MetadataPeRepack.Rechecksum(after);MetadataPeRepack.ValidateLayout(after);string dll=Path.Combine(output,"HandheldCompanion.dll");File.WriteAllBytes(dll,after);File.Copy(policyPath,Path.Combine(output,"policy.json"));
        using var ap=new PEReader(new MemoryStream(after,false));var am=ap.GetMetadataReader();using var aa=Util.Read(dll,out var ar);using var acleanup=ar;
        if(aa.Name.FullName!=ba.Name.FullName||aa.MainModule.Mvid==ba.MainModule.Mvid)throw new InvalidDataException("Unexpected identity transformation");
        byte[] bmeta=bp.GetMetadata().GetContent().ToArray(),ameta=ap.GetMetadata().GetContent().ToArray();Array.Clear(bmeta,PeCompactor.GuidOffset(bp,bm),16);Array.Clear(ameta,PeCompactor.GuidOffset(ap,am),16);
        foreach(var plan in plans){int pos=methodRows+checked(((int)plan.Method.MetadataToken.RID-1)*rowSize);Array.Clear(bmeta,pos,4);Array.Clear(ameta,pos,4);}
        if(!bmeta.AsSpan().SequenceEqual(ameta))throw new InvalidDataException("Unapproved metadata change beyond MVID/two getter RVAs");
        var amethods=Util.Types(aa.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.MetadataToken.ToUInt32());if(amethods.Count!=bmethods.Count)throw new InvalidDataException("Methods added/removed");var rows=new List<object>();int bodies=0;
        byte[] Raw(PEReader pe,Mono.Cecil.MethodDefinition m)=>m.RVA==0?[]:pe.GetSectionData(checked((int)m.RVA)).GetContent(0,pe.GetMethodBody(checked((int)m.RVA)).Size).ToArray();
        foreach(var m in bmethods.Values)
        {
            var a=amethods[m.MetadataToken.ToUInt32()];if(m.FullName!=a.FullName)throw new InvalidDataException("Method identity changed");var plan=plans.SingleOrDefault(p=>p.Method.MetadataToken==m.MetadataToken);byte[] b=Raw(bp,m),ab=Raw(ap,a);bool edit=plan.Method!=null;
            if(edit)
            {
                if(a.RVA!=plan.Rva||!ab.AsSpan().SequenceEqual(plan.Body))throw new InvalidDataException("Unexpected guarded method bytes");
                if(!ap.GetMethodBody(checked((int)a.RVA)).GetILBytes()!.AsSpan(10).SequenceEqual(bp.GetMethodBody(checked((int)m.RVA)).GetILBytes()))throw new InvalidDataException("Original nonzero getter path differs");
            }
            else if(m.RVA!=a.RVA||!b.AsSpan().SequenceEqual(ab)||Verification.Semantic(m)!=Verification.Semantic(a))throw new InvalidDataException("Unapproved method change: "+m.FullName);
            if(m.HasBody)bodies++;rows.Add(new{Token=$"0x{m.MetadataToken.ToUInt32():x8}",Method=m.FullName,Action=edit?"prepend-10-byte-zero-gamepad-return-zero; original nonzero IL tail byte-identical":"byte-and-semantic-identical",BeforeRva=m.RVA,AfterRva=a.RVA,BeforeRawSha256=Util.Hash(b),AfterRawSha256=Util.Hash(ab)});
        }
        var afields=Util.Types(aa.MainModule).SelectMany(t=>t.Fields).ToDictionary(f=>f.MetadataToken.ToUInt32());int initialValues=0;foreach(var f in Util.Types(ba.MainModule).SelectMany(t=>t.Fields)){var other=afields[f.MetadataToken.ToUInt32()];if(f.FullName!=other.FullName||f.RVA!=other.RVA||!f.InitialValue.AsSpan().SequenceEqual(other.InitialValue))throw new InvalidDataException("Field identity/initial value changed");if(f.RVA!=0)initialValues++;}
        foreach(var r in ba.MainModule.Resources.OfType<EmbeddedResource>()){var other=aa.MainModule.Resources.OfType<EmbeddedResource>().Single(x=>x.Name==r.Name);if(!r.GetResourceData().AsSpan().SequenceEqual(other.GetResourceData()))throw new InvalidDataException("Resource changed");}
        Util.Save(Path.Combine(output,"methods.json"),rows);Util.Save(Path.Combine(output,"guard-il.json"),plans.Select(p=>new{Method=p.Method.FullName,Token=$"0x{p.Method.MetadataToken.ToUInt32():x8}",BeforeRva=p.Method.RVA,AfterRva=p.Rva,IL=amethods[p.Method.MetadataToken.ToUInt32()].Body.Instructions.Select(i=>i.ToString()).ToArray()}));
        Util.Save(Path.Combine(output,"verification.json"),new{Passed=true,InputSha256=Util.Hash(before),Sha256=Util.Hash(after),OriginalMethods=bmethods.Count,OriginalBodies=bodies,UnchangedOriginalMethods=bmethods.Count-2,GuardedMethods=2,AddedTypes=0,AddedMethods=0,MetadataIdenticalExceptMvidAndTwoMethodRvas=true,AllTokensAndAssemblyReferencesPreserved=true,NonzeroMethodIlTailsByteIdentical=true,TimerPatchAndHelpersByteIdentical=true,FieldRvaInitializersPreserved=initialValues,AllResourcesByteIdentical=true,DeviceEcAndControllerProtocolIlPreserved=true});
        Util.Save(Path.Combine(output,"CANDIDATE.json"),new{Status="STATIC-VERIFIED-RUNTIME-VALIDATION-PENDING",RuntimeId="YMCC-DEVICECORE-SDL-PASSIVE-GUARDS-20261018-"+Util.Hash(after)[..16],Sha256=Util.Hash(after),InputSha256=Util.Hash(before),Bytes=after.Length,AddedBytes=after.Length-before.Length,Mvid=mvid,AssemblyName=aa.Name.FullName,StrongName="unsigned",Authenticode="NotSigned",Methods=policy.Methods,RuntimeValidationPending=true});
        Console.WriteLine($"SDL TOUCHPAD CANDIDATE {dll} {after.Length} {Util.Hash(after)}");
    }
}