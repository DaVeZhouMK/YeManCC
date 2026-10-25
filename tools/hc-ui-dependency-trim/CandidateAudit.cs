using TypeSpecification = Mono.Cecil.TypeSpecification;
using ModuleDefinition = Mono.Cecil.ModuleDefinition;
using TypeReference = Mono.Cecil.TypeReference;
using TypeDefinition = Mono.Cecil.TypeDefinition;
using MemberReference = Mono.Cecil.MemberReference;
using MethodDefinition = Mono.Cecil.MethodDefinition;
using FieldDefinition = Mono.Cecil.FieldDefinition;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using HcDeviceCoreTrim;

static class CandidateAudit
{
    static bool IsTarget(TypeReference t)
    {
        if(t is TypeSpecification s)return IsTarget(s.ElementType);
        return t.Scope is AssemblyNameReference a&&Closure.Names.Contains(a.Name)||t.Scope is ModuleDefinition m&&Closure.Names.Contains(m.Assembly.Name.Name);
    }
    static string Attrs(ICustomAttributeProvider p)=>string.Join("|",p.CustomAttributes.Select(a=>a.Constructor.FullName+"("+string.Join(",",a.ConstructorArguments.Select(Arg))+")"+string.Join(",",a.Fields.Select(v=>v.Name+"="+Arg(v.Argument)))+string.Join(",",a.Properties.Select(v=>v.Name+"="+Arg(v.Argument)))));
    static string Arg(CustomAttributeArgument a)=>a.Type.FullName+":"+(a.Value is CustomAttributeArgument[] arr?string.Join(",",arr.Select(Arg)):a.Value is TypeReference t?t.FullName:a.Value?.ToString()??"null");
    static string Generics(IGenericParameterProvider p)=>string.Join("|",p.GenericParameters.Select(g=>g.Name+":"+g.Attributes+":"+string.Join(",",g.Constraints.Select(c=>c.ConstraintType.FullName+Attrs(c)))+":"+Attrs(g)));
    static object Shape(TypeDefinition t,HashSet<string>? fieldFilter=null)=>new{t.FullName,Attributes=(int)t.Attributes,Base=t.BaseType?.FullName,t.PackingSize,t.ClassSize,Generics=Generics(t),CustomAttributes=Attrs(t),Interfaces=t.Interfaces.Select(i=>i.InterfaceType.FullName+Attrs(i)).ToArray(),Fields=t.Fields.Where(f=>fieldFilter==null||fieldFilter.Contains(f.FullName)).Select(f=>new{f.FullName,Attributes=(int)f.Attributes,f.Offset,f.HasConstant,Constant=f.Constant?.ToString(),RvaData=Util.Hash(f.InitialValue),CustomAttributes=Attrs(f)}).ToArray(),Methods=t.Methods.Select(m=>new{m.FullName,Attributes=(int)m.Attributes,ImplAttributes=(int)m.ImplAttributes,m.CallingConvention,ReturnAttributes=Attrs(m.MethodReturnType),Generics=Generics(m),CustomAttributes=Attrs(m),Parameters=m.Parameters.Select(p=>new{p.Name,p.ParameterType.FullName,Attributes=(int)p.Attributes,p.HasConstant,Constant=p.Constant?.ToString(),CustomAttributes=Attrs(p)}).ToArray(),Overrides=m.Overrides.Select(o=>o.FullName).ToArray(),m.HasPInvokeInfo,Native=m.HasPInvokeInfo?m.PInvokeInfo.Module.Name+":"+m.PInvokeInfo.EntryPoint:null}).ToArray(),Properties=t.Properties.Select(p=>new{p.FullName,Attributes=(int)p.Attributes,Get=p.GetMethod?.FullName,Set=p.SetMethod?.FullName,CustomAttributes=Attrs(p)}).ToArray(),Events=t.Events.Select(e=>new{e.FullName,Attributes=(int)e.Attributes,Add=e.AddMethod?.FullName,Remove=e.RemoveMethod?.FullName,Invoke=e.InvokeMethod?.FullName,CustomAttributes=Attrs(e)}).ToArray()};
    static string Resolve(MemberReference r)=>r switch{TypeReference t=>Identity(t.Resolve()),MethodReference m=>Identity(m.Resolve()),FieldReference f=>Identity(f.Resolve()),_=>throw new NotSupportedException(r.GetType().Name)};
    static string Identity(IMemberDefinition? d)=>d switch{TypeDefinition t=>t.Module.Assembly.Name.FullName+":"+t.FullName,MethodDefinition m=>m.Module.Assembly.Name.FullName+":"+m.FullName,FieldDefinition f=>f.Module.Assembly.Name.FullName+":"+f.FullName,null=>"null",_=>throw new NotSupportedException()};
    static string[] Instructions(MethodDefinition m)
    {
        if(!m.HasBody)return ["<no body>"];
        var code=m.Body.Instructions;
        string O(object? o)=>o switch {null=>"",Mono.Cecil.Cil.Instruction i=>"instruction:"+code.IndexOf(i),Mono.Cecil.Cil.Instruction[] arr=>string.Join(",",arr.Select(i=>code.IndexOf(i))),MemberReference r=>r.FullName,ParameterDefinition p=>"arg:"+p.Index,Mono.Cecil.Cil.VariableDefinition v=>"local:"+v.Index,_=>o.ToString()??""};
        return code.Select(i=>i.OpCode.Name+" "+O(i.Operand)).ToArray();
    }
    static string CheckBody(MethodDefinition before,MethodDefinition after,bool tableSliced)
    {
        if(before.HasBody!=after.HasBody)return "FAILED";
        if(before.HasBody&&(before.Body.InitLocals!=after.Body.InitLocals||!before.Body.Variables.Select(v=>v.VariableType.FullName).SequenceEqual(after.Body.Variables.Select(v=>v.VariableType.FullName))))return "FAILED";
        var a=Instructions(before);var b=Instructions(after);
        if(a.SequenceEqual(b))return "exact-original";
        if(before.FullName=="System.Void iNKORE.UI.WPF.Modern.Controls.SettingsCard::.cctor()")
        {
            int p=Array.FindIndex(a,x=>x=="ldsfld iNKORE.UI.WPF.Modern.Common.IconKeys.FontIconData iNKORE.UI.WPF.Modern.Common.IconKeys.FluentSystemIcons::ChevronRight_16_Regular");
            if(p<0||a[p+1]!="stloc.0 "||a[p+2]!="ldloca.s local:0"||a[p+3]!="call System.String iNKORE.UI.WPF.Modern.Common.IconKeys.FontIconData::get_Glyph()")return "FAILED";
            a[p]="ldstr "+char.ConvertFromUtf32(62127);a[p+1]=a[p+2]=a[p+3]="nop ";return a.SequenceEqual(b)?"exact-glyph-fold-F2AF":"FAILED";
        }
        if(tableSliced&&before.IsConstructor&&before.IsStatic&&StaticTableSlice.TypeNames.Contains(before.DeclaringType.FullName))
        {
            var retained=after.DeclaringType.Fields.Select(f=>f.FullName).ToHashSet();var blocks=StaticTableSlice.Blocks(before.DeclaringType);
            var expected=blocks.Where(x=>retained.Contains(x.Field.FullName)).SelectMany(x=>x.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand?.ToString())).Append("ret ").ToArray();
            return expected.SequenceEqual(b)?"exact-required-immutable-initializer-blocks":"FAILED";
        }
        return "FAILED";
    }
    public static void Run(string originalRoot,string candidateRoot,string report)
    {
        var bodies=new List<object>();var resources=new List<object>();var shapes=new List<object>();var references=new List<object>();var dynamic=new List<object>();var pe=new List<object>();var errors=new List<string>();var removed=new HashSet<string>();
        foreach(string name in Closure.Names)
        {
            string beforePath=Path.Combine(originalRoot,name+".dll"),afterPath=Path.Combine(candidateRoot,name+".dll");using var before=Util.Read(beforePath,out var br);using var bclean=br;using var after=Util.Read(afterPath,out var ar);using var aclean=ar;
            var original=Util.Types(before.MainModule).ToDictionary(t=>t.FullName);var candidates=Util.Types(after.MainModule).ToArray();foreach(var t in original.Values)if(candidates.All(c=>c.FullName!=t.FullName))removed.Add(t.FullName);
            foreach(var t in candidates)
            {
                if(!original.TryGetValue(t.FullName,out var b)){errors.Add("New unapproved type: "+t.FullName);continue;}
                HashSet<string>? fieldFilter=StaticTableSlice.TypeNames.Contains(t.FullName)&&t.Fields.Count<b.Fields.Count?t.Fields.Select(f=>f.FullName).ToHashSet():null;
                if(JsonSerializer.Serialize(Shape(b,fieldFilter))!=JsonSerializer.Serialize(Shape(t)))errors.Add("Retained shape changed: "+name+":"+t.FullName);
                foreach(var m in t.Methods){var previous=b.Methods.Single(x=>x.FullName==m.FullName);var proof=CheckBody(previous,m,fieldFilter!=null);bodies.Add(new{Assembly=name,Method=m.FullName,Proof=proof});if(proof=="FAILED")errors.Add("Unapproved method body change: "+m.FullName);}
                shapes.Add(new{Assembly=name,t.FullName,ShapeSha256=Util.Hash(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(Shape(t))))});
            }
            if(!before.MainModule.Resources.Select(r=>r.Name).Order().SequenceEqual(after.MainModule.Resources.Select(r=>r.Name).Order()))errors.Add("Resource set differs");
            foreach(var r in after.MainModule.Resources.OfType<EmbeddedResource>()){var previous=before.MainModule.Resources.OfType<EmbeddedResource>().Single(x=>x.Name==r.Name);bool ui=r.Name==name+".g.resources";bool valid=ui?ResourceBlob.Read(r.GetResourceData()).Entries.Count==0:r.GetResourceData().AsSpan().SequenceEqual(previous.GetResourceData());resources.Add(new{Assembly=name,r.Name,EmptyUiContainer=ui,Passed=valid});if(!valid)errors.Add("Resource validation failed: "+r.Name);}
            using var stream=File.OpenRead(afterPath);using var reader=new PEReader(stream);var md=reader.GetMetadataReader();var header=reader.PEHeaders;
            pe.Add(new{Assembly=name,LargestFieldCounts=Util.Types(after.MainModule).OrderByDescending(t=>t.Fields.Count).Take(8).Select(t=>new{t.FullName,FieldCount=t.Fields.Count,MethodCount=t.Methods.Count}).ToArray(),FileBytes=new FileInfo(afterPath).Length,MetadataBytes=header.CorHeader!.MetadataDirectory.Size,ManagedResourceBytes=header.CorHeader.ResourcesDirectory.Size,NativeResourceBytes=header.PEHeader!.ResourceTableDirectory.Size,Sections=header.SectionHeaders.Select(s=>new{s.Name,s.VirtualSize,s.SizeOfRawData}).ToArray()});
        }
        foreach(string name in new[]{"HandheldCompanion"}.Concat(Closure.Names))
        {
            using var before=Util.Read(Path.Combine(originalRoot,name+".dll"),out var br);using var bc=br;using var after=Util.Read(Path.Combine(candidateRoot,name+".dll"),out var ar);using var ac=ar;
            var oldRefs=before.MainModule.GetMemberReferences().Where(m=>m.DeclaringType!=null&&IsTarget(m.DeclaringType)).GroupBy(m=>m.FullName).ToDictionary(g=>g.Key,g=>g.First());
            var all=after.MainModule.GetMemberReferences().Where(m=>m.DeclaringType!=null&&IsTarget(m.DeclaringType)).Cast<MemberReference>().Concat(after.MainModule.GetTypeReferences().Where(IsTarget)).DistinctBy(r=>r.FullName);
            foreach(var r in all)
            {
                try
                {
                    string current=Resolve(r);MemberReference baseline=r is TypeReference?before.MainModule.GetTypeReferences().First(t=>t.FullName==r.FullName):oldRefs[r.FullName];string previous=Resolve(baseline);
                    if(previous!=current)errors.Add("Resolved interface identity changed: "+name+":"+r.FullName);
                    references.Add(new{Consumer=name,Member=r.FullName,Baseline=previous,Candidate=current,Identical=previous==current});
                }
                catch(Exception e){errors.Add("Resolution failed: "+name+":"+r.FullName+": "+e.Message);}
            }
            foreach(var t in Util.Types(after.MainModule))foreach(var method in t.Methods.Where(m=>m.HasBody))foreach(var i in method.Body.Instructions)
            {
                if(i.Operand is string text && removed.Any(n=>n!="<Module>"&&text.Contains(n,StringComparison.Ordinal)))errors.Add("Removed type literal remains: "+method.FullName+": "+text);
                if(i.Operand is MethodReference call&&(call.DeclaringType.Namespace.StartsWith("System.Reflection",StringComparison.Ordinal)||call.Name is "GetType" or "GetTypes" or "GetExportedTypes" or "CreateInstance" or "GetManifestResourceStream" or "LoadComponent" or "ApplyTemplate"))dynamic.Add(new{Assembly=name,Method=method.FullName,Offset=i.Offset,Call=call.FullName,Strings=method.Body.Instructions.Where(x=>x.Operand is string).Select(x=>(string)x.Operand).Distinct().ToArray()});
            }
        }
        var result=new{Status=errors.Count==0?"passed":"failed",MethodBodies=bodies,ResourceEvidence=resources,RetainedMethodBodyChecks=bodies.Count,RetainedTypeShapes=shapes.Count,InterfaceReferences=references.Count,Errors=errors,ShapeEvidence=shapes,ReferenceEvidence=references,RemovedTypes=removed.Order().ToArray(),DynamicAndResourceCalls=dynamic,PeSizes=pe,HardwareExecuted=false,DeviceProtocolsChanged=false,Scope="Exact complete retained type/member shape and referenced member resolution; dynamic/render/template calls recorded, not runtime approved"};
        Util.Save(report,result);Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.RetainedTypeShapes,result.InterfaceReferences,ErrorCount=errors.Count}));if(errors.Count!=0)throw new InvalidDataException(string.Join("\n",errors.Take(12)));
    }
}
