using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length != 4 || args[0] != "slice") throw new ArgumentException("slice runtime-root untouched|delegate-leaf output-directory");
string root=Path.GetFullPath(args[1]),mode=args[2],output=Path.GetFullPath(args[3]);
if(mode is not ("untouched" or "delegate-leaf"))throw new ArgumentException("Unknown mode");
if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new IOException("Output directory must be empty");
Directory.CreateDirectory(output);
var slicer=new Slicer(root,mode,output);
slicer.Run();

sealed class Slicer
{
    const string AssemblyName="SDL3-CS";
    readonly string root,mode,output;
    readonly DefaultAssemblyResolver resolver=new();
    readonly AssemblyDefinition sdl;
    readonly HashSet<TypeDefinition> types=[];
    readonly HashSet<MethodDefinition> methods=[];
    readonly HashSet<FieldDefinition> fields=[];
    readonly HashSet<PropertyDefinition> properties=[];
    readonly HashSet<EventDefinition> events=[];
    readonly Queue<Action> work=new();
    readonly List<object> roots=[];
    readonly Dictionary<FieldDefinition,Instruction[]> delegateGroups=[];
    readonly List<object> removedInitializers=[];
    readonly HashSet<string> strings=[];
    readonly Dictionary<string,TypeDefinition> allTypes;
    readonly TypeDefinition main;
    readonly MethodDefinition initializer;
    readonly string inputHash;
    readonly Dictionary<string,string> inputMethods,inputTypes,inputFields;
    int initializerGroups;
    public Slicer(string root,string mode,string output)
    {
        this.root=root;this.mode=mode;this.output=output;
        resolver.AddSearchDirectory(root);resolver.AddSearchDirectory(System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory());
        sdl=AssemblyDefinition.ReadAssembly(Path.Combine(root,AssemblyName+".dll"),new ReaderParameters{AssemblyResolver=resolver});
        resolver.Register(sdl);
        allTypes=All(sdl.MainModule.Types).ToDictionary(t=>t.FullName);
        main=allTypes["SDL3.SDL"];initializer=main.Methods.Single(m=>m.Name==".cctor");
        inputHash=Hash(Path.Combine(root,AssemblyName+".dll"));
        inputMethods=allTypes.Values.SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,MethodSemantic);
        inputTypes=allTypes.Values.ToDictionary(t=>t.FullName,TypeSemantic);
        inputFields=allTypes.Values.SelectMany(t=>t.Fields).ToDictionary(f=>f.FullName,FieldSemantic);
        if(mode=="delegate-leaf")ParseDelegateInitializer();
    }
    public void Run()
    {
        foreach(var path in Directory.GetFiles(root,"*.dll").Order(StringComparer.Ordinal))
        {
            if(Path.GetFileName(path)==AssemblyName+".dll")continue;
            AssemblyDefinition assembly;
            try{assembly=AssemblyDefinition.ReadAssembly(path,new ReaderParameters{AssemblyResolver=resolver});}
            catch(BadImageFormatException){continue;}
            using(assembly)
            {
                var tt=assembly.MainModule.GetTypeReferences().Where(t=>Scope(t)==AssemblyName).ToArray();
                var mm=assembly.MainModule.GetMemberReferences().Where(m=>Scope(m.DeclaringType)==AssemblyName).ToArray();
                if(tt.Length+mm.Length>0)roots.Add(new{File=Path.GetFileName(path),Sha256=Hash(path),Types=tt.Select(t=>t.FullName),Members=mm.Select(m=>m.FullName)});
                foreach(var t in tt)MarkType(t);
                foreach(var m in mm)MarkMember(m);
                foreach(var t in All(assembly.MainModule.Types))foreach(var m in t.Methods.Where(m=>m.HasBody))foreach(var i in m.Body.Instructions)if(i.OpCode.Code==Code.Ldstr&&i.Operand is string s)strings.Add(s);
            }
        }
        MarkType(sdl.MainModule.Types.Single(t=>t.Name=="<Module>"));
        Attributes(sdl);Attributes(sdl.MainModule);
        while(work.TryDequeue(out var action))action();
        var reflectionCandidates=strings.Where(s=>s.Contains("SDL3",StringComparison.OrdinalIgnoreCase)||s.Contains("SDL_",StringComparison.Ordinal)).Order().ToArray();
        // Exact assembly-qualified/type-name literals are additional conservative roots.
        foreach(var s in reflectionCandidates)
        {
            string name=s.Split(',')[0].Trim().Replace('+','/');
            if(allTypes.TryGetValue(name,out var t))MarkType(t);
        }
        while(work.TryDequeue(out var action))action();
        if(mode=="delegate-leaf")
        {
            foreach(var pair in delegateGroups.Where(p=>!fields.Contains(p.Key)))
            {
                removedInitializers.Add(new{Field=pair.Key.FullName,StartOffset=pair.Value[0].Offset,EndOffset=pair.Value[^1].Offset,Instructions=pair.Value.Select(i=>new{Op=i.OpCode.Name,Operand=Operand(i.Operand)})});
                foreach(var i in pair.Value)initializer.Body.Instructions.Remove(i);
            }
        }
        var expectedInitializer=MethodSemantic(initializer);
        var removedTypes=allTypes.Values.Where(t=>!types.Contains(t)).Select(t=>t.FullName).Order().ToArray();
        var removedMethods=allTypes.Values.SelectMany(t=>t.Methods).Where(m=>!methods.Contains(m)).Select(m=>m.FullName).Order().ToArray();
        var removedFields=allTypes.Values.SelectMany(t=>t.Fields).Where(f=>!fields.Contains(f)).Select(f=>f.FullName).Order().ToArray();
        Prune(sdl.MainModule.Types);
        string seed=inputHash+":"+mode+":v1";
        sdl.MainModule.Mvid=new Guid(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(seed)).AsSpan(0,16));
        string candidate=Path.Combine(output,AssemblyName+".dll");
        sdl.Write(candidate,new WriterParameters{WriteSymbols=false,Timestamp=0});
        using var read=AssemblyDefinition.ReadAssembly(candidate,new ReaderParameters{AssemblyResolver=resolver});
        var finalTypes=All(read.MainModule.Types).ToDictionary(t=>t.FullName);
        var errors=new List<string>();
        foreach(var t in finalTypes.Values)
        {
            if(inputTypes[t.FullName]!=TypeSemantic(t))errors.Add("Type metadata/layout changed: "+t.FullName);
            foreach(var f in t.Fields)if(inputFields[f.FullName]!=FieldSemantic(f))errors.Add("Field metadata/layout changed: "+f.FullName);
            foreach(var m in t.Methods)
            {
                string expected=m.FullName==initializer.FullName?expectedInitializer:inputMethods[m.FullName];
                if(MethodSemantic(m)!=expected)errors.Add("Method body/import changed: "+m.FullName);
            }
        }
        var imports=finalTypes.Values.SelectMany(t=>t.Methods).Where(m=>m.HasPInvokeInfo).Select(m=>new{m.FullName,m.PInvokeInfo.EntryPoint,Module=m.PInvokeInfo.Module.Name,Attributes=m.PInvokeInfo.Attributes.ToString()}).ToArray();
        var record=new{Status=errors.Count==0?"STATIC-PASSED-RUNTIME-PENDING":"failed",Mode=mode,InputFile=Path.Combine(root,AssemblyName+".dll"),InputSha256=inputHash,InputBytes=new FileInfo(Path.Combine(root,AssemblyName+".dll")).Length,OutputFile=candidate,OutputSha256=Hash(candidate),OutputBytes=new FileInfo(candidate).Length,AssemblyIdentity=read.Name.FullName,Mvid=read.MainModule.Mvid,OriginalTypes=allTypes.Count,RetainedTypes=finalTypes.Count,OriginalMethods=inputMethods.Count,RetainedMethods=finalTypes.Values.Sum(t=>t.Methods.Count),OriginalFields=inputFields.Count,RetainedFields=finalTypes.Values.Sum(t=>t.Fields.Count),RetainedNativeImports=imports.Length,InitializerGroups=initializerGroups,RemovedInitializerGroups=removedInitializers.Count,RootAssemblies=roots,ReflectionCandidateStrings=reflectionCandidates,Errors=errors,HcModified=false,NativeImplementationAdded=false,Resources=read.MainModule.Resources.Select(r=>new{r.Name,r.ResourceType}),Policy="Root every SDL TypeRef/MemberRef in all runtime managed assemblies, not only headless roots. Preserve all retained instance field layouts and enum constants; all delegate runtime methods; virtual/interface methods; all non-leaf initializers. Optional prune only exact ldnull/ldftn/newobj/stsfld unused private static delegate assignments.",HardwareExecuted=false};
        Save("slice.json",record);Save("removed-types.json",removedTypes);Save("removed-methods.json",removedMethods);Save("removed-fields.json",removedFields);Save("removed-delegate-initializers.json",removedInitializers);Save("retained-imports.json",imports);Save("retained-types.json",finalTypes.Keys.Order());
        Console.WriteLine(JsonSerializer.Serialize(new{record.Status,record.Mode,record.OutputBytes,record.OutputSha256,record.RetainedTypes,record.RetainedMethods,record.RemovedInitializerGroups,Errors=errors.Count}));
        if(errors.Count!=0)throw new InvalidOperationException("Candidate differs from allowed semantics");
    }
    void ParseDelegateInitializer()
    {
        var body=initializer.Body;var ii=body.Instructions;
        if(body.HasExceptionHandlers||body.HasVariables||ii.Count<1||ii[^1].OpCode.Code!=Code.Ret||ii.Any(i=>i.OpCode.FlowControl is FlowControl.Branch or FlowControl.Cond_Branch or FlowControl.Throw))throw new InvalidOperationException($"Unrecognized SDL initializer; do not rewrite EH={body.HasExceptionHandlers} Locals={body.HasVariables} Count={ii.Count} Last={ii[^1].OpCode.Code}");
        for(int i=0;i<ii.Count-1;i++)
        {
            if(i+3>=ii.Count||ii[i].OpCode.Code!=Code.Ldnull||ii[i+1].OpCode.Code!=Code.Ldftn||ii[i+2].OpCode.Code!=Code.Newobj||ii[i+3].OpCode.Code!=Code.Stsfld)continue;
            var field=((FieldReference)ii[i+3].Operand).Resolve();var ctor=((MethodReference)ii[i+2].Operand).Resolve();var target=((MethodReference)ii[i+1].Operand).Resolve();
            if(field.DeclaringType!=main||!field.IsPrivate||!field.IsStatic||ctor.DeclaringType.BaseType.FullName!="System.MulticastDelegate"||!ctor.IsRuntime||ctor.Parameters.Count!=2||ctor.Parameters[0].ParameterType.FullName!="System.Object"||ctor.Parameters[1].ParameterType.FullName!="System.IntPtr"||!target.IsStatic||target.Module!=sdl.MainModule)throw new InvalidOperationException($"Initializer is not a pure private static managed delegate assignment: {field.FullName}; field={field.Attributes} ctor={ctor.Attributes} impl={ctor.ImplAttributes} target={target.Attributes} base={ctor.DeclaringType.BaseType.FullName}");
            if(field.FieldType.FullName!=ctor.DeclaringType.FullName)throw new InvalidOperationException("Delegate field type mismatch");
            delegateGroups.Add(field,[ii[i],ii[i+1],ii[i+2],ii[i+3]]);i+=3;
        }
        initializerGroups=delegateGroups.Count;
    }
    void MarkType(TypeReference? tr)
    {
        if(tr==null)return;
        if(tr is GenericInstanceType gi){foreach(var a in gi.GenericArguments)MarkType(a);MarkType(gi.ElementType);return;}
        if(tr is FunctionPointerType fp){MarkType(fp.ReturnType);foreach(var p in fp.Parameters)MarkType(p.ParameterType);return;}
        if(tr is RequiredModifierType rm){MarkType(rm.ModifierType);MarkType(rm.ElementType);return;}
        if(tr is OptionalModifierType om){MarkType(om.ModifierType);MarkType(om.ElementType);return;}
        if(tr is TypeSpecification ts){MarkType(ts.ElementType);return;}
        if(tr is GenericParameter||Scope(tr)!=AssemblyName)return;
        var td=tr.Resolve()??throw new InvalidOperationException("Unresolved type: "+tr.FullName);
        if(td.Module!=sdl.MainModule)throw new InvalidOperationException("SDL root escaped candidate module");
        if(!types.Add(td))return;
        work.Enqueue(()=>
        {
            MarkType(td.DeclaringType);MarkType(td.BaseType);Attributes(td);Generics(td.GenericParameters);
            foreach(var iface in td.Interfaces){MarkType(iface.InterfaceType);Attributes(iface);}
            foreach(var f in td.Fields.Where(f=>!f.IsStatic||f.IsLiteral))MarkField(f);
            foreach(var m in td.Methods.Where(m=>m.IsStatic&&m.IsConstructor||m.IsVirtual||td.BaseType?.FullName=="System.MulticastDelegate"))MarkMethod(m);
        });
    }
    void MarkMember(MemberReference mr)
    {
        switch(mr)
        {
            case TypeReference t:MarkType(t);break;
            case MethodReference m:
                MarkType(m.DeclaringType);MarkType(m.ReturnType);foreach(var p in m.Parameters)MarkType(p.ParameterType);
                if(m is GenericInstanceMethod gi)foreach(var a in gi.GenericArguments)MarkType(a);
                if(Scope(m.DeclaringType)==AssemblyName)MarkMethod(m.Resolve()??throw new InvalidOperationException("Unresolved SDL method: "+m.FullName));break;
            case FieldReference f:
                MarkType(f.DeclaringType);MarkType(f.FieldType);
                if(Scope(f.DeclaringType)==AssemblyName)MarkField(f.Resolve()??throw new InvalidOperationException("Unresolved SDL field: "+f.FullName));break;
        }
    }
    void MarkMethod(MethodDefinition m)
    {
        if(m.Module!=sdl.MainModule||!methods.Add(m))return;
        work.Enqueue(()=>
        {
            MarkType(m.DeclaringType);MarkType(m.ReturnType);Attributes(m);Attributes(m.MethodReturnType);Generics(m.GenericParameters);
            foreach(var p in m.Parameters){MarkType(p.ParameterType);Attributes(p);}
            foreach(var o in m.Overrides)MarkMember(o);
            foreach(var p in m.DeclaringType.Properties.Where(p=>p.GetMethod==m||p.SetMethod==m||p.OtherMethods.Contains(m)))
            {
                if(properties.Add(p)){MarkType(p.PropertyType);foreach(var a in p.Parameters)MarkType(a.ParameterType);Attributes(p);}
            }
            foreach(var e in m.DeclaringType.Events.Where(e=>e.AddMethod==m||e.RemoveMethod==m||e.InvokeMethod==m||e.OtherMethods.Contains(m)))if(events.Add(e)){MarkType(e.EventType);Attributes(e);}
            if(!m.HasBody)return;
            foreach(var v in m.Body.Variables)MarkType(v.VariableType);
            foreach(var h in m.Body.ExceptionHandlers)MarkType(h.CatchType);
            HashSet<Instruction>? ignored=m==initializer&&mode=="delegate-leaf"?delegateGroups.Values.SelectMany(v=>v).ToHashSet():null;
            foreach(var i in m.Body.Instructions)
            {
                if(ignored?.Contains(i)==true)continue;
                if(i.Operand is MemberReference r)MarkMember(r);
                else if(i.Operand is CallSite c){MarkType(c.ReturnType);foreach(var p in c.Parameters)MarkType(p.ParameterType);}
                else if(i.OpCode.Code==Code.Ldstr&&i.Operand is string text)strings.Add(text);
            }
        });
    }
    void MarkField(FieldDefinition f)
    {
        if(f.Module!=sdl.MainModule||!fields.Add(f))return;
        work.Enqueue(()=>
        {
            MarkType(f.DeclaringType);MarkType(f.FieldType);Attributes(f);
            if(mode=="delegate-leaf"&&delegateGroups.TryGetValue(f,out var instructions))foreach(var i in instructions)if(i.Operand is MemberReference mr)MarkMember(mr);
        });
    }
    void Generics(IEnumerable<GenericParameter> parameters){foreach(var p in parameters){Attributes(p);foreach(var c in p.Constraints){MarkType(c.ConstraintType);Attributes(c);}}}
    void Attributes(ICustomAttributeProvider p)
    {
        if(!p.HasCustomAttributes)return;
        foreach(var a in p.CustomAttributes)
        {
            MarkMember(a.Constructor);
            foreach(var v in a.ConstructorArguments)Argument(v);
            foreach(var v in a.Fields)Argument(v.Argument);
            foreach(var v in a.Properties)Argument(v.Argument);
        }
    }
    void Argument(CustomAttributeArgument a){MarkType(a.Type);if(a.Value is TypeReference t)MarkType(t);else if(a.Value is CustomAttributeArgument[] aa)foreach(var x in aa)Argument(x);}
    void Prune(Mono.Collections.Generic.Collection<TypeDefinition> list)
    {
        foreach(var t in list.ToArray())
        {
            if(!types.Contains(t)){list.Remove(t);continue;}
            Prune(t.NestedTypes);
            foreach(var m in t.Methods.ToArray())if(!methods.Contains(m))t.Methods.Remove(m);
            foreach(var f in t.Fields.ToArray())if(!fields.Contains(f))t.Fields.Remove(f);
            foreach(var p in t.Properties.ToArray())if(!properties.Contains(p))t.Properties.Remove(p);
            foreach(var e in t.Events.ToArray())if(!events.Contains(e))t.Events.Remove(e);
        }
    }
    static string? Scope(TypeReference t)=>t is TypeSpecification s?Scope(s.ElementType):t.DeclaringType!=null?Scope(t.DeclaringType):t.Scope is ModuleDefinition module?module.Assembly.Name.Name:t.Scope?.Name;
    static IEnumerable<TypeDefinition> All(IEnumerable<TypeDefinition> ts){foreach(var t in ts){yield return t;foreach(var n in All(t.NestedTypes))yield return n;}}
    static string Hash(string file)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    void Save(string name,object value)=>File.WriteAllText(Path.Combine(output,name),JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true}));
    static string Attrs(ICustomAttributeProvider p)=>string.Join(";",p.CustomAttributes.Select(a=>a.AttributeType.FullName+":"+Convert.ToHexString(a.GetBlob())));
    static string FieldSemantic(FieldDefinition f)=>JsonSerializer.Serialize(new{f.FullName,f.Attributes,f.Offset,f.Constant,Initial=f.InitialValue==null?null:Convert.ToHexString(f.InitialValue),Attrs=Attrs(f),Marshal=f.HasMarshalInfo?f.MarshalInfo.NativeType.ToString():null});
    static string TypeSemantic(TypeDefinition t)=>JsonSerializer.Serialize(new{t.FullName,t.Attributes,Base=t.BaseType?.FullName,t.PackingSize,t.ClassSize,Interfaces=t.Interfaces.Select(i=>i.InterfaceType.FullName),Generics=t.GenericParameters.Select(g=>new{g.Name,g.Attributes,Constraints=g.Constraints.Select(c=>c.ConstraintType.FullName)}),Attrs=Attrs(t)});
    static string Operand(object? operand)=>operand switch{null=>"",Instruction i=>"@"+i.Offset,Instruction[] ii=>string.Join(",",ii.Select(i=>"@"+i.Offset)),MemberReference mr=>mr.FullName,ParameterDefinition p=>"p"+p.Index,VariableDefinition v=>"v"+v.Index,CallSite c=>c.FullName,_=>operand.ToString()??""};
    static string MethodSemantic(MethodDefinition m)
    {
        Dictionary<Instruction,int>? indices=m.HasBody?m.Body.Instructions.Select((i,n)=>(i,n)).ToDictionary(p=>p.i,p=>p.n):null;
        string Op(object? value)=>value switch{Instruction i=>"@"+indices![i],Instruction[] ii=>string.Join(",",ii.Select(i=>"@"+indices![i])),_=>Operand(value)};
        return JsonSerializer.Serialize(new{m.FullName,m.Attributes,m.ImplAttributes,m.CallingConvention,m.HasThis,m.ExplicitThis,Attrs=Attrs(m),ReturnAttrs=Attrs(m.MethodReturnType),Parameters=m.Parameters.Select(p=>new{p.Name,p.Attributes,p.Constant,Attrs=Attrs(p),Marshal=p.HasMarshalInfo?p.MarshalInfo.NativeType.ToString():null}),Import=m.HasPInvokeInfo?new{m.PInvokeInfo.EntryPoint,Module=m.PInvokeInfo.Module.Name,m.PInvokeInfo.Attributes}:null,Body=m.HasBody?new{m.Body.InitLocals,Locals=m.Body.Variables.Select(v=>v.VariableType.FullName),Instructions=m.Body.Instructions.Select(i=>new{Op=i.OpCode.Name,Arg=Op(i.Operand)}),EH=m.Body.ExceptionHandlers.Select(h=>new{h.HandlerType,Catch=h.CatchType?.FullName,TryStart=Op(h.TryStart),TryEnd=Op(h.TryEnd),HandlerStart=Op(h.HandlerStart),HandlerEnd=Op(h.HandlerEnd),FilterStart=Op(h.FilterStart)})}:null});
    }
}
sealed class DefaultAssemblyResolver : BaseAssemblyResolver
{
    readonly Dictionary<string,AssemblyDefinition> registered=new(StringComparer.OrdinalIgnoreCase);
    public void Register(AssemblyDefinition assembly)=>registered[assembly.Name.Name]=assembly;
    public override AssemblyDefinition Resolve(AssemblyNameReference name){if(registered.TryGetValue(name.Name,out var a))return a;return base.Resolve(name);}
}





