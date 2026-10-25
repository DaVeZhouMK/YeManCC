using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using SRM=System.Reflection.Metadata;

if(args.Length!=4)throw new ArgumentException("input-runtime candidate-sdl slice-json report-json");
string root=Path.GetFullPath(args[0]),file=Path.GetFullPath(args[1]);
using var slice=JsonDocument.Parse(File.ReadAllText(args[2]));
string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
if(Hash(file)!=slice.RootElement.GetProperty("OutputSha256").GetString()||Hash(Path.Combine(root,"SDL3-CS.dll"))!=slice.RootElement.GetProperty("InputSha256").GetString())throw new InvalidDataException("Audit input pin mismatch");
var resolver=new AuditResolver();resolver.AddSearchDirectory(root);resolver.AddSearchDirectory(System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory());
using var candidate=AssemblyDefinition.ReadAssembly(file,new ReaderParameters{AssemblyResolver=resolver});resolver.Candidate=candidate;
using var baseline=AssemblyDefinition.ReadAssembly(Path.Combine(root,"SDL3-CS.dll"));
var sourceTypes=All(baseline.MainModule.Types).ToDictionary(t=>t.FullName);
var keptTypes=All(candidate.MainModule.Types).ToDictionary(t=>t.FullName);
var keptMethods=keptTypes.Values.SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName);
var keptFields=keptTypes.Values.SelectMany(t=>t.Fields).ToDictionary(f=>f.FullName);
var errors=new List<string>();var roots=new List<object>();var resolutions=new List<object>();int typeRefs=0,memberRefs=0,internalRefs=0,methodParity=0,marshalParity=0,removedGroups=0;
using var peSource=new PEReader(File.OpenRead(Path.Combine(root,"SDL3-CS.dll")));using var peCandidate=new PEReader(File.OpenRead(file));
var metaSource=SRM.PEReaderExtensions.GetMetadataReader(peSource);var metaCandidate=SRM.PEReaderExtensions.GetMetadataReader(peCandidate);
foreach(string path in Directory.GetFiles(root,"*.dll").Where(p=>Path.GetFileName(p)!="SDL3-CS.dll").Order())
{
    AssemblyDefinition a;try{a=AssemblyDefinition.ReadAssembly(path,new ReaderParameters{AssemblyResolver=resolver});}catch(BadImageFormatException){continue;}
    using(a)
    {
        int tr=0,mr=0;
        foreach(var t in a.MainModule.GetTypeReferences().Where(t=>Scope(t)=="SDL3-CS")){Resolve(t,Path.GetFileName(path));tr++;}
        foreach(var m in a.MainModule.GetMemberReferences().Where(m=>Scope(m.DeclaringType)=="SDL3-CS")){Resolve(m,Path.GetFileName(path));mr++;}
        if(tr+mr>0)roots.Add(new{File=Path.GetFileName(path),Sha256=Hash(path),Types=tr,Members=mr});typeRefs+=tr;memberRefs+=mr;
    }
}
foreach(var t in keptTypes.Values)
{
    var original=sourceTypes[t.FullName];
    if(t.Attributes!=original.Attributes||t.PackingSize!=original.PackingSize||t.ClassSize!=original.ClassSize||t.BaseType?.FullName!=original.BaseType?.FullName)errors.Add("Retained type shape changed: "+t.FullName);
    foreach(var f in t.Fields)
    {
        var before=original.Fields.Single(x=>x.FullName==f.FullName);
        if(before.Attributes!=f.Attributes||before.Offset!=f.Offset||before.FieldType.FullName!=f.FieldType.FullName||!Equals(before.Constant,f.Constant))errors.Add("Field signature/offset/constant changed: "+f.FullName);
        string MarshalBlob(SRM.MetadataReader reader,FieldDefinition field)
        {
            var fd=reader.GetFieldDefinition((SRM.FieldDefinitionHandle)MetadataTokens.EntityHandle(field.MetadataToken.ToInt32()));
            var blob=fd.GetMarshallingDescriptor();return blob.IsNil?"":Convert.ToHexString(reader.GetBlobBytes(blob));
        }
        if(MarshalBlob(metaSource,before)!=MarshalBlob(metaCandidate,f))errors.Add("Field marshal blob changed: "+f.FullName);marshalParity++;
    }
    foreach(var m in t.Methods)
    {
        var before=original.Methods.Single(x=>x.FullName==m.FullName);
        if(m.Name==".cctor"&&t.FullName=="SDL3.SDL"&&slice.RootElement.GetProperty("Mode").GetString()=="delegate-leaf")
        {
            var ii=before.Body.Instructions;
            if(before.Body.HasExceptionHandlers||before.Body.HasVariables||ii.Any(i=>i.OpCode.FlowControl is FlowControl.Branch or FlowControl.Cond_Branch or FlowControl.Throw))errors.Add("Initializer no longer straight line");
            for(int i=0;i+3<ii.Count;)
            {
                if(ii[i].OpCode.Code==Code.Ldnull&&ii[i+1].OpCode.Code==Code.Ldftn&&ii[i+2].OpCode.Code==Code.Newobj&&ii[i+3].OpCode.Code==Code.Stsfld&&ii[i+3].Operand is FieldReference fr&&!keptFields.ContainsKey(fr.FullName))
                {
                    var f=fr.Resolve();var ctor=((MethodReference)ii[i+2].Operand).Resolve();
                    if(!f.IsPrivate||!f.IsStatic||ctor.DeclaringType.BaseType.FullName!="System.MulticastDelegate"||!ctor.IsRuntime)errors.Add("Removed initializer was not a pure private delegate assignment: "+f.FullName);
                    for(int n=0;n<4;n++)ii.RemoveAt(i);removedGroups++;
                }
                else i++;
            }
        }
        if(Body(before)!=Body(m))errors.Add("Kept method IL/import/signature differs: "+m.FullName);else methodParity++;
        Dictionary<int,string> ParameterMarshals(SRM.MetadataReader reader,MethodDefinition method)
        {
            var md=reader.GetMethodDefinition((SRM.MethodDefinitionHandle)MetadataTokens.EntityHandle(method.MetadataToken.ToInt32()));
            return md.GetParameters().Select(h=>reader.GetParameter(h)).ToDictionary(p=>p.SequenceNumber,p=>p.GetMarshallingDescriptor().IsNil?"":Convert.ToHexString(reader.GetBlobBytes(p.GetMarshallingDescriptor())));
        }
        var originalMarshals=ParameterMarshals(metaSource,before);var finalMarshals=ParameterMarshals(metaCandidate,m);
        if(JsonSerializer.Serialize(originalMarshals)!=JsonSerializer.Serialize(finalMarshals))errors.Add("Parameter/return marshal blobs changed: "+m.FullName);marshalParity+=originalMarshals.Count;
        if(m.HasBody)foreach(var instruction in m.Body.Instructions)
        {
            if(instruction.Operand is MemberReference reference&&Scope(reference is TypeReference tt?tt:reference.DeclaringType)=="SDL3-CS"){Resolve(reference,"retained IL "+m.FullName);internalRefs++;}
        }
    }
}
foreach(var tr in candidate.MainModule.GetTypeReferences().Where(t=>Scope(t)=="SDL3-CS")){Resolve(tr,"candidate TypeRef");internalRefs++;}
foreach(var mr in candidate.MainModule.GetMemberReferences().Where(m=>Scope(m.DeclaringType)=="SDL3-CS")){Resolve(mr,"candidate MemberRef");internalRefs++;}
if(removedGroups!=slice.RootElement.GetProperty("RemovedInitializerGroups").GetInt32())errors.Add("Independent initializer group count disagrees");
if(candidate.Name.FullName!=baseline.Name.FullName)errors.Add("Assembly identity changed");
if(!candidate.Name.PublicKeyToken.SequenceEqual(baseline.Name.PublicKeyToken))errors.Add("Assembly key token changed");
var result=new{Status=errors.Count==0?"passed":"failed",Candidate=file,Sha256=Hash(file),Bytes=new FileInfo(file).Length,AssemblyIdentity=candidate.Name.FullName,AllExternalSdlTypeRefsResolved=typeRefs,AllExternalSdlMemberRefsResolved=memberRefs,InternalMemberAndTypeReferencesResolved=internalRefs,KeptMethodParity=methodParity,MarshalDescriptorParity=marshalParity,IndependentlyApprovedRemovedDelegateGroups=removedGroups,RetainedNativeImports=keptMethods.Values.Count(m=>m.HasPInvokeInfo),RootAssemblies=roots,Resolutions=resolutions,Errors=errors,NativeOrHardwareExecuted=false,HcFileModified=false,FullControllerConstructorsExecuted=false};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[3]))!);File.WriteAllText(args[3],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.AllExternalSdlTypeRefsResolved,result.AllExternalSdlMemberRefsResolved,result.InternalMemberAndTypeReferencesResolved,result.KeptMethodParity,result.MarshalDescriptorParity,result.RetainedNativeImports,result.IndependentlyApprovedRemovedDelegateGroups,Errors=errors.Count}));if(errors.Count>0)Environment.ExitCode=1;
void Resolve(MemberReference reference,string source)
{
    try
    {
        if(reference is GenericParameter gp)
        {
            bool ownerKept=gp.Owner is MethodReference ownerMethod&&keptMethods.ContainsKey(ownerMethod.FullName)||gp.Owner is TypeReference ownerType&&keptTypes.ContainsKey(ownerType.FullName);
            if(!ownerKept)throw new InvalidOperationException("Generic parameter owner was removed");
            resolutions.Add(new{Source=source,Reference=gp.FullName,Resolved="generic parameter of "+gp.Owner});return;
        }
        var resolved=reference switch{TypeReference t=>(MemberReference?)t.Resolve(),MethodReference m=>m.Resolve(),FieldReference f=>f.Resolve(),_=>null};
        if(resolved==null||!ReferenceEquals(resolved is TypeDefinition resolvedType?resolvedType.Module:resolved.DeclaringType.Module,candidate.MainModule))throw new InvalidOperationException("Resolved outside candidate");
        bool retained=resolved switch{TypeDefinition t=>keptTypes.ContainsKey(t.FullName),MethodDefinition m=>keptMethods.ContainsKey(m.FullName),FieldDefinition f=>keptFields.ContainsKey(f.FullName),_=>false};
        if(!retained)throw new InvalidOperationException("Resolved a removed member");
        resolutions.Add(new{Source=source,Reference=reference.FullName,Resolved=resolved.FullName});
    }
    catch(Exception error){errors.Add(source+": "+reference.FullName+": "+error.Message);}
}
static string? Scope(TypeReference t)=>t is TypeSpecification s?Scope(s.ElementType):t.DeclaringType!=null?Scope(t.DeclaringType):t.Scope is ModuleDefinition module?module.Assembly.Name.Name:t.Scope?.Name;
static IEnumerable<TypeDefinition> All(IEnumerable<TypeDefinition> ts){foreach(var t in ts){yield return t;foreach(var n in All(t.NestedTypes))yield return n;}}
static string Body(MethodDefinition m)
{
    Dictionary<Instruction,int>? positions=m.HasBody?m.Body.Instructions.Select((x,n)=>(x,n)).ToDictionary(z=>z.x,z=>z.n):null;
    string Val(object? value)=>value switch{null=>"",Instruction i=>"@"+positions![i],Instruction[] ii=>string.Join("/",ii.Select(i=>"@"+positions![i])),MemberReference r=>r.FullName,ParameterDefinition p=>"param:"+p.Index,VariableDefinition v=>"local:"+v.Index,CallSite c=>c.FullName,_=>value.ToString()??""};
    return JsonSerializer.Serialize(new{m.FullName,m.Attributes,m.ImplAttributes,m.CallingConvention,m.HasThis,m.ExplicitThis,Return=m.ReturnType.FullName,Parameters=m.Parameters.Select(p=>new{p.Name,p.Attributes,Type=p.ParameterType.FullName}),PInvoke=m.HasPInvokeInfo?new{m.PInvokeInfo.EntryPoint,Module=m.PInvokeInfo.Module.Name,m.PInvokeInfo.Attributes}:null,Code=m.HasBody?new{m.Body.InitLocals,Locals=m.Body.Variables.Select(v=>v.VariableType.FullName),IL=m.Body.Instructions.Select(i=>new{Code=i.OpCode.Code.ToString(),Operand=Val(i.Operand)}),EH=m.Body.ExceptionHandlers.Select(e=>new{e.HandlerType,Type=e.CatchType?.FullName,TryStart=Val(e.TryStart),TryEnd=Val(e.TryEnd),Start=Val(e.HandlerStart),End=Val(e.HandlerEnd),Filter=Val(e.FilterStart)})}:null});
}
sealed class AuditResolver:DefaultAssemblyResolver
{
    public AssemblyDefinition? Candidate{get;set;}
    public override AssemblyDefinition Resolve(AssemblyNameReference name)=>name.Name=="SDL3-CS"&&Candidate!=null?Candidate:base.Resolve(name);
    public override AssemblyDefinition Resolve(AssemblyNameReference name,ReaderParameters parameters)=>name.Name=="SDL3-CS"&&Candidate!=null?Candidate:base.Resolve(name,parameters);
}



