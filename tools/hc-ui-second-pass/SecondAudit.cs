using Mono.Cecil;
using Mono.Cecil.Cil;
using HcDeviceCoreTrim;
using System.Text;
using System.Text.Json;

static class SecondAudit
{
    public static string Resolve(MemberReference r)=>r switch{
        TypeReference t=>Identity(t.Resolve()),MethodReference m=>Identity(m.Resolve()),FieldReference f=>Identity(f.Resolve()),_=>throw new NotSupportedException(r.GetType().Name)};
    static string Identity(IMemberDefinition? d)=>d switch{TypeDefinition t=>t.Module.Assembly.Name.FullName+":"+t.FullName,MethodDefinition m=>m.Module.Assembly.Name.FullName+":"+m.FullName,FieldDefinition f=>f.Module.Assembly.Name.FullName+":"+f.FullName,_=>throw new InvalidDataException("Unresolved member")};
    static string Ty(TypeReference? t)=>t==null?"":t.FullName+"@"+t.Scope?.ToString();
    static string Attr(ICustomAttributeProvider p)=>string.Join("|",p.CustomAttributes.Select(a=>a.Constructor.FullName+"("+string.Join(",",a.ConstructorArguments.Select(Arg))+")"+string.Join(",",a.Fields.Select(v=>v.Name+"="+Arg(v.Argument)))+string.Join(",",a.Properties.Select(v=>v.Name+"="+Arg(v.Argument)))));
    static string Arg(CustomAttributeArgument a)=>Ty(a.Type)+":"+(a.Value is CustomAttributeArgument[] arr?string.Join(",",arr.Select(Arg)):a.Value is TypeReference t?Ty(t):a.Value?.ToString()??"null");
    static string Generics(IGenericParameterProvider p)=>string.Join("|",p.GenericParameters.Select(g=>g.Name+":"+g.Attributes+":"+string.Join(",",g.Constraints.Select(c=>Ty(c.ConstraintType)+Attr(c)))+":"+Attr(g)));
    static object Shape(TypeDefinition t,HashSet<string>? keep=null)=>new{
        t.FullName,Attributes=(int)t.Attributes,Base=Ty(t.BaseType),t.PackingSize,t.ClassSize,Generics=Generics(t),CustomAttributes=Attr(t),Interfaces=t.Interfaces.Select(i=>Ty(i.InterfaceType)+Attr(i)).ToArray(),
        Fields=t.Fields.Select(f=>new{f.FullName,Type=Ty(f.FieldType),Attributes=(int)f.Attributes,f.Offset,f.HasConstant,Constant=f.Constant?.ToString(),RvaData=Util.Hash(f.InitialValue),CustomAttributes=Attr(f),f.HasMarshalInfo,Marshal=f.HasMarshalInfo?f.MarshalInfo.NativeType.ToString():null}).ToArray(),
        Methods=t.Methods.Where(m=>keep==null||keep.Contains(m.FullName)).Select(m=>new{m.FullName,Return=Ty(m.ReturnType),Attributes=(int)m.Attributes,ImplAttributes=(int)m.ImplAttributes,m.CallingConvention,ReturnAttrs=Attr(m.MethodReturnType),Generics=Generics(m),CustomAttributes=Attr(m),Parameters=m.Parameters.Select(p=>new{p.Name,Type=Ty(p.ParameterType),Attributes=(int)p.Attributes,p.HasConstant,Constant=p.Constant?.ToString(),CustomAttributes=Attr(p),p.HasMarshalInfo,Marshal=p.HasMarshalInfo?p.MarshalInfo.NativeType.ToString():null}).ToArray(),Overrides=m.Overrides.Select(o=>o.FullName).ToArray(),m.HasPInvokeInfo,Native=m.HasPInvokeInfo?m.PInvokeInfo.Module.Name+":"+m.PInvokeInfo.EntryPoint+":"+m.PInvokeInfo.Attributes:null}).ToArray(),
        Properties=t.Properties.Select(p=>new{p.FullName,Attributes=(int)p.Attributes,Get=p.GetMethod?.FullName,Set=p.SetMethod?.FullName,Other=p.OtherMethods.Select(m=>m.FullName).ToArray(),CustomAttributes=Attr(p),p.HasConstant,Constant=p.Constant?.ToString()}).ToArray(),
        Events=t.Events.Select(e=>new{e.FullName,Attributes=(int)e.Attributes,Add=e.AddMethod?.FullName,Remove=e.RemoveMethod?.FullName,Invoke=e.InvokeMethod?.FullName,Other=e.OtherMethods.Select(m=>m.FullName).ToArray(),CustomAttributes=Attr(e)}).ToArray()
    };
    static string Body(MethodDefinition m){
        if(!m.HasBody)return "<no body>";var body=m.Body;var code=body.Instructions;
        int Index(Instruction? i)=>i==null?-1:code.IndexOf(i);
        string Operand(object? o)=>o switch{
            null=>"",Instruction i=>"instruction:"+Index(i),Instruction[] arr=>string.Join(",",arr.Select(Index)),TypeReference r=>Ty(r),MemberReference r=>r.FullName+"@"+Ty(r.DeclaringType),ParameterDefinition p=>"arg:"+p.Index,VariableDefinition v=>"local:"+v.Index,CallSite c=>c.FullName+":"+c.CallingConvention,_=>o.ToString()??""};
        return JsonSerializer.Serialize(new{body.InitLocals,body.MaxStackSize,Variables=body.Variables.Select(v=>Ty(v.VariableType)),Instructions=code.Select(i=>i.OpCode.Name+" "+Operand(i.Operand)),Handlers=body.ExceptionHandlers.Select(h=>new{h.HandlerType,TryStart=Index(h.TryStart),TryEnd=Index(h.TryEnd),HandlerStart=Index(h.HandlerStart),HandlerEnd=Index(h.HandlerEnd),FilterStart=Index(h.FilterStart),Catch=Ty(h.CatchType)})});
    }
    public static void Run(MethodGraph graph,string candidateRoot,string report){
        var errors=new List<string>();var bodies=new List<object>();var shapes=new List<object>();var refs=new List<object>();var callbacks=new List<object>();var resources=new List<object>();var signatures=new List<object>();
        int expectedRetained=graph.Methods.Count-graph.Removed.Length;var expectedRemoved=graph.Removed.ToHashSet();var actualRemoved=new HashSet<string>();
        foreach(string name in MethodGraph.Names){
            var before=graph.Targets[name];using var after=Util.Read(Path.Combine(candidateRoot,name+".dll"),out var resolver);using var cleanup=resolver;
            if(before.Name.FullName!=after.Name.FullName||Attr(before)!=Attr(after)||Attr(before.MainModule)!=Attr(after.MainModule)||before.MainModule.Kind!=after.MainModule.Kind||before.MainModule.RuntimeVersion!=after.MainModule.RuntimeVersion||!before.MainModule.AssemblyReferences.Select(a=>a.FullName).SequenceEqual(after.MainModule.AssemblyReferences.Select(a=>a.FullName)))errors.Add("Assembly metadata drift: "+name);
            var old=Util.Types(before.MainModule).ToDictionary(t=>t.FullName);var current=Util.Types(after.MainModule).ToDictionary(t=>t.FullName);
            if(!old.Keys.Order().SequenceEqual(current.Keys.Order()))errors.Add("Type set drift: "+name);
            foreach(var t in current.Values){
                var original=old[t.FullName];var kept=t.Methods.Select(m=>m.FullName).ToHashSet();
                string os=JsonSerializer.Serialize(Shape(original,kept)),cs=JsonSerializer.Serialize(Shape(t));bool same=os==cs;
                shapes.Add(new{Assembly=name,Type=t.FullName,ExactRetainedShape=same,BaselineSha256=Util.Hash(Encoding.UTF8.GetBytes(os)),CandidateSha256=Util.Hash(Encoding.UTF8.GetBytes(cs))});if(!same)errors.Add("Retained member/type shape drift: "+name+":"+t.FullName);
                foreach(var m in original.Methods.Where(m=>!kept.Contains(m.FullName)))actualRemoved.Add(MethodGraph.Key(m));
                foreach(var m in t.Methods){
                    var om=original.Methods.SingleOrDefault(p=>p.FullName==m.FullName);if(om==null){errors.Add("Added method: "+m.FullName);continue;}
                    string ob=Body(om),cb=Body(m);bool exact=ob==cb;bodies.Add(new{Assembly=name,Method=m.FullName,Proof=exact?"exact-previous-candidate":"FAILED",BaselineIlSha256=Util.Hash(Encoding.UTF8.GetBytes(ob)),CandidateIlSha256=Util.Hash(Encoding.UTF8.GetBytes(cb)),Instructions=m.HasBody?m.Body.Instructions.Count:0});if(!exact)errors.Add("Retained IL changed: "+m.FullName);
                    if(!m.HasBody)continue;
                    foreach(var i in m.Body.Instructions){
                        if(i.Operand is MethodReference mr&&MethodGraph.IsUi(mr.DeclaringType)){
                            try{var resolved=mr.Resolve()??throw new InvalidDataException("Unresolved retained call");if(expectedRemoved.Contains(MethodGraph.Key(resolved)))errors.Add("Call to removed method: "+mr.FullName);}catch(Exception e){errors.Add("Retained method resolution: "+mr.FullName+":"+e.Message);}
                        }
                        if(i.OpCode==OpCodes.Ldftn||i.OpCode==OpCodes.Ldvirtftn){var mr2=(MethodReference)i.Operand;bool ui=MethodGraph.IsUi(mr2.DeclaringType);string resolved=Resolve(mr2);callbacks.Add(new{Assembly=name,Owner=m.FullName,Offset=i.Offset,DelegateTarget=mr2.FullName,Resolved=resolved,UiTarget=ui,Kind="Actual IL delegate target; includes DP callbacks and closures",OwnerIlExact=exact});}
                        if(i.Operand is MethodReference dp&&dp.DeclaringType.FullName=="System.Windows.DependencyProperty"&&dp.Name.StartsWith("Register",StringComparison.Ordinal))callbacks.Add(new{Assembly=name,Owner=m.FullName,Offset=i.Offset,Registration=dp.FullName,OwnerIlExact=exact,Kind="WPF DP registration; entire owner IL and all transitive delegate/closure targets preserved"});
                    }
                }
            }
            if(!before.MainModule.Resources.Select(r=>r.Name).SequenceEqual(after.MainModule.Resources.Select(r=>r.Name)))errors.Add("Resource set drift: "+name);
            foreach(var r in after.MainModule.Resources){var previous=before.MainModule.Resources.Single(x=>x.Name==r.Name);bool same=r.Attributes==previous.Attributes&&r.ResourceType==previous.ResourceType&&(r is not EmbeddedResource er||previous is EmbeddedResource pr&&er.GetResourceData().AsSpan().SequenceEqual(pr.GetResourceData()));resources.Add(new{Assembly=name,r.Name,Exact=same});if(!same)errors.Add("Resource bytes drift");}
            bool valid=DevelopmentSigning.Verify(File.ReadAllBytes(Path.Combine(candidateRoot,name+".dll")));signatures.Add(new{Assembly=name,RsaSignatureValid=valid,Token=Convert.ToHexString(after.Name.PublicKeyToken).ToLowerInvariant(),Claim="Existing development key; no publisher certification"});if(!valid)errors.Add("Invalid development signature");
        }
        if(!actualRemoved.SetEquals(expectedRemoved))errors.Add("Removed set differs from independently recomputed closure");
        foreach(string file in Directory.EnumerateFiles(candidateRoot,"*",SearchOption.AllDirectories).Where(p=>Path.GetExtension(p) is ".dll" or ".exe")){
            if(!MethodGraph.IsManaged(file))continue;string rel=Path.GetRelativePath(candidateRoot,file);using var after=Util.Read(file,out var ar);using var ac=ar;using var before=Util.Read(Path.Combine(graph.Root,rel),out var br);using var bc=br;
            if(!MethodGraph.Names.Contains(after.Name.Name)&&Util.HashFile(file)!=Util.HashFile(Path.Combine(graph.Root,rel)))errors.Add("Non UI input changed: "+rel);
            var oldRefs=before.MainModule.GetMemberReferences().Where(r=>r.DeclaringType!=null&&graph.Boundary(r.DeclaringType)).Cast<MemberReference>().Concat(before.MainModule.GetTypeReferences().Where(graph.Boundary)).GroupBy(r=>r.FullName).ToDictionary(g=>g.Key,g=>g.First());
            var newRefs=after.MainModule.GetMemberReferences().Where(r=>r.DeclaringType!=null&&graph.Boundary(r.DeclaringType)).Cast<MemberReference>().Concat(after.MainModule.GetTypeReferences().Where(graph.Boundary)).DistinctBy(r=>r.FullName);
            foreach(var r in newRefs){try{string previous=Resolve(oldRefs[r.FullName]),current=Resolve(r);bool same=previous==current;refs.Add(new{Consumer=after.Name.Name,Reference=r.FullName,Baseline=previous,Candidate=current,Identical=same});if(!same)errors.Add("Reference identity drift: "+r.FullName);}catch(Exception e){errors.Add("Reference failed: "+r.FullName+":"+e.Message);}}
            if(!MethodGraph.Names.Contains(after.Name.Name))foreach(var r in oldRefs.Keys)if(!after.MainModule.GetMemberReferences().Cast<MemberReference>().Concat(after.MainModule.GetTypeReferences()).Any(n=>n.FullName==r))errors.Add("Lost external reference: "+r);
        }
        if(bodies.Count!=expectedRetained)errors.Add("Retained method count differs");
        var result=new{Status=errors.Count==0?"passed":"failed",BaselineRoot=graph.Root,CandidateRoot=candidateRoot,Errors=errors,RetainedMethodBodyChecks=bodies.Count,RetainedTypeShapes=shapes.Count,InterfaceReferences=refs.Count,RemovedMethods=actualRemoved.Order().ToArray(),MethodBodies=bodies,ShapeEvidence=shapes,ReferenceEvidence=refs,PropertyAndDpCallbacks=callbacks,Resources=resources,Signatures=signatures,EveryExternalManagedAssemblyScanned=true,EveryRetainedBodyCompared=true,RemovedIlReplacedWithStub=false,HardwareExecuted=false};
        Util.Save(report,result);Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.RetainedMethodBodyChecks,result.RetainedTypeShapes,result.InterfaceReferences,Removed=actualRemoved.Count,Errors=errors.Take(12)}));if(errors.Count!=0)throw new InvalidDataException("Audit failed");
    }
}

