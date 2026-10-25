using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HcDeviceCoreTrim;

if(args.Length<3)throw new ArgumentException("inventory <runtime> <out> | build <runtime> <key> <out> <resources|types>");
if(args[0]=="audit" && args.Length==4){CandidateAudit.Run(args[1],args[2],args[3]);return;}
if(args[0]=="detail"&&args.Length==4){InterfaceDetail.Dump(args[1],args[2],args[3]);return;}
if(args[0]=="inventory") { new Closure(args[1]).WriteInventory(args[2]); return; }
if(args[0]=="build" && args.Length==5) { new Closure(args[1]).Build(args[2],args[3],args[4]);return; }
throw new ArgumentException("Invalid command");

sealed class Closure
{
    public static readonly string[] Names=["iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Controls"];
    readonly string root;readonly Dictionary<string,AssemblyDefinition> assemblies=[];readonly List<IDisposable> dispose=[];
    readonly Dictionary<string,TypeDefinition> definitions=[];readonly HashSet<TypeDefinition> retained=[];readonly Queue<TypeDefinition> pending=[];
    readonly List<object> edges=[];readonly List<object> external=[];
    bool closed;
    public Closure(string root)
    {
        this.root=Path.GetFullPath(root);
        foreach(string name in Names)
        {
            var a=Util.Read(Path.Combine(this.root,name+".dll"),out var resolver);assemblies.Add(name,a);dispose.Add(a);dispose.Add(resolver);
            foreach(var t in Util.Types(a.MainModule))definitions.Add(name+":"+t.FullName,t);
        }
    }
    string? Target(TypeReference t)
    {
        if(t is TypeSpecification s)return Target(s.ElementType);
        string? scope=t.Scope is AssemblyNameReference a?a.Name:t.Scope is ModuleDefinition m?m.Assembly.Name.Name:null;
        return scope!=null&&Names.Contains(scope)?scope:null;
    }
    void Add(TypeReference? t,string from)
    {
        if(t==null)return;
        if(t is GenericInstanceType g)foreach(var a in g.GenericArguments)Add(a,from);
        if(t is RequiredModifierType rm)Add(rm.ModifierType,from);
        if(t is OptionalModifierType om)Add(om.ModifierType,from);
        if(t is FunctionPointerType fp){Add(fp.ReturnType,from);foreach(var p in fp.Parameters)Add(p.ParameterType,from);}
        if(t is TypeSpecification s){Add(s.ElementType,from);return;}
        if(t is GenericParameter)return;
        string? name=Target(t);if(name==null)return;
        if(!definitions.TryGetValue(name+":"+t.FullName,out var definition))throw new InvalidDataException("Missing target type: "+name+":"+t.FullName);
        edges.Add(new{From=from,To=name+":"+t.FullName});
        if(retained.Add(definition))pending.Enqueue(definition);
        if(definition.DeclaringType!=null)Add(definition.DeclaringType,from+" declaring type");
    }
    void Attr(ICustomAttributeProvider item,string from)
    {
        foreach(var a in item.CustomAttributes){Member(a.Constructor,from+" attribute");foreach(var x in a.ConstructorArguments)Arg(x,from);foreach(var x in a.Fields)Arg(x.Argument,from);foreach(var x in a.Properties)Arg(x.Argument,from);}
    }
    void Arg(CustomAttributeArgument a,string from)
    { Add(a.Type,from);if(a.Value is TypeReference t)Add(t,from);if(a.Value is CustomAttributeArgument[] values)foreach(var x in values)Arg(x,from); }
    void Generic(IGenericParameterProvider item,string from)
    {foreach(var p in item.GenericParameters){Attr(p,from);foreach(var c in p.Constraints){Add(c.ConstraintType,from);Attr(c,from);}}}
    void Member(MemberReference m,string from)
    {
        if(m is TypeReference t){Add(t,from);return;}
        Add(m.DeclaringType,from);
        if(m is FieldReference f)Add(f.FieldType,from);
        if(m is MethodReference method){Add(method.ReturnType,from);foreach(var p in method.Parameters)Add(p.ParameterType,from);if(method is GenericInstanceMethod g)foreach(var a in g.GenericArguments)Add(a,from);Generic(method,from);}
    }
    void Type(TypeDefinition t)
    {
        string from=t.Module.Assembly.Name.Name+":"+t.FullName;
        Add(t.BaseType,from+" base");Attr(t,from);Generic(t,from);
        foreach(var i in t.Interfaces){Add(i.InterfaceType,from+" interface");Attr(i,from);}
        foreach(var f in t.Fields){Add(f.FieldType,from+" field "+f.Name);Attr(f,from);}
        foreach(var p in t.Properties){Add(p.PropertyType,from+" property "+p.Name);Attr(p,from);foreach(var x in p.Parameters)Add(x.ParameterType,from);}
        foreach(var e in t.Events){Add(e.EventType,from+" event "+e.Name);Attr(e,from);}
        foreach(var m in t.Methods)
        {
            string mf=m.FullName;Member(m,mf+" signature");Attr(m,mf);Attr(m.MethodReturnType,mf);Generic(m,mf);
            foreach(var p in m.Parameters)Attr(p,mf);
            foreach(var o in m.Overrides)Member(o,mf+" override");
            if(!m.HasBody)continue;
            foreach(var v in m.Body.Variables)Add(v.VariableType,mf+" local");
            foreach(var h in m.Body.ExceptionHandlers)Add(h.CatchType,mf+" catch");
            foreach(var i in m.Body.Instructions)
            {
                if(i.Operand is MemberReference r)Member(r,mf+" IL_"+i.Offset.ToString("x4"));
                if(i.Operand is CallSite call){Add(call.ReturnType,mf);foreach(var p in call.Parameters)Add(p.ParameterType,mf);}
            }
        }
    }
    void Close()
    {
        if(closed)return;closed=true;
        foreach(string file in Directory.EnumerateFiles(root,"*.dll"))
        {
            AssemblyDefinition? a=null;DesktopFirstResolver? resolver=null;
            try
            {
                a=Util.Read(file,out resolver);if(Names.Contains(a.Name.Name))continue;
                var refs=a.MainModule.GetTypeReferences().Where(t=>Target(t)!=null).ToArray();
                if(refs.Length==0)continue;
                external.Add(new{Assembly=a.Name.Name,Types=refs.Select(t=>new{t.FullName,Scope=Target(t)}).ToArray()});
                foreach(var t in refs)Add(t,a.Name.Name+" TypeRef "+t.FullName);
                // HC's complete signatures and IL are roots, without executing any device code.
                foreach(var t in Util.Types(a.MainModule))Type(t);
            }
            catch(BadImageFormatException){}
            finally{a?.Dispose();resolver?.Dispose();}
        }
        Add(definitions[Names[0]+":iNKORE.UI.WPF.Modern.Controls.FontIcon"],"reviewed constructor root");
        Add(definitions[Names[0]+":iNKORE.UI.WPF.Modern.Common.FontIconSource"],"reviewed constructor root");
        Add(definitions[Names[1]+":iNKORE.UI.WPF.Modern.Controls.Primitives.CommandBarFlyoutCommandBarTemplateSettings"],"reviewed constructor root");
        foreach(var a in assemblies.Values){Attr(a,"assembly attributes");Attr(a.MainModule,"module attributes");Add(a.MainModule.Types[0],"module initializer");}
        while(pending.TryDequeue(out var next))Type(next);
    }
    public void WriteInventory(string path)
    {
        Close();Util.Save(path,new{Status="closed",ExternalRoots=external,Assemblies=assemblies.Select(pair=>new{Assembly=pair.Key,InputSha256=Util.HashFile(Path.Combine(root,pair.Key+".dll")),Types=Util.Types(pair.Value.MainModule).Count(),RetainedTypes=retained.Count(t=>t.Module==pair.Value.MainModule),RemovedTypes=Util.Types(pair.Value.MainModule).Where(t=>!retained.Contains(t)).Select(t=>t.FullName).ToArray(),Retained=retained.Where(t=>t.Module==pair.Value.MainModule).Select(t=>t.FullName).Order().ToArray()}).ToArray(),Edges=edges.DistinctBy(e=>JsonSerializer.Serialize(e)).ToArray()});
    }
    static string Body(MethodDefinition m)
    {
        if(!m.HasBody)return "<none>";
        string Operand(object? o)=>o switch {null=>"",Instruction i=>"IL_"+i.Offset.ToString("x4"),Instruction[] many=>string.Join(",",many.Select(i=>"IL_"+i.Offset.ToString("x4"))),MemberReference r=>r.FullName,ParameterDefinition p=>"arg:"+p.Index,VariableDefinition v=>"local:"+v.Index,CallSite c=>c.FullName,_=>o.ToString()??""};
        return string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Name+" "+Operand(i.Operand)))+"\nlocals:"+string.Join("|",m.Body.Variables.Select(v=>v.VariableType.FullName))+"\nhandlers:"+string.Join("|",m.Body.ExceptionHandlers.Select(h=>$"{h.HandlerType}:{h.TryStart?.Offset}:{h.TryEnd?.Offset}:{h.HandlerStart?.Offset}:{h.HandlerEnd?.Offset}:{h.FilterStart?.Offset}:{h.CatchType?.FullName}"));
    }
    public void Build(string keyPath,string output,string mode)
    {
        if(mode is not "resources" and not "types" and not "folded" and not "tables")throw new ArgumentException("Invalid build mode");
        var pinnedBodies=assemblies.ToDictionary(p=>p.Key,p=>Util.Types(p.Value.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Body));
        object? foldEvidence=mode is "folded" or "tables"?FoldGlyphs.Apply(assemblies,root):null;
        Close();output=Path.GetFullPath(output);if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new IOException("Refuse nonempty output");Directory.CreateDirectory(output);
        object? tableEvidence=mode=="tables"?StaticTableSlice.Apply(assemblies,retained,root):null;
        WriteInventory(Path.Combine(output,"type-closure.json"));
        if(tableEvidence!=null)Util.Save(Path.Combine(output,"static-table-slice.json"),tableEvidence);
        if(foldEvidence!=null)Util.Save(Path.Combine(output,"glyph-fold.json"),foldEvidence);
        var results=new List<object>();using var rsa=RSA.Create();rsa.ImportFromPem(File.ReadAllText(keyPath));string publicKey=Convert.ToHexString(DevelopmentSigning.PublicKey(rsa));
        foreach(var pair in assemblies)
        {
            var a=pair.Value;string input=Path.Combine(root,pair.Key+".dll"),dest=Path.Combine(output,pair.Key+".dll");
            var resource=a.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name==pair.Key+".g.resources");
            var blob=ResourceBlob.Read(resource.GetResourceData());byte[] empty=blob.Write([]);
            var originalBodies=Util.Types(a.MainModule).SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName,Body);
            var originalTypes=Util.Types(a.MainModule).Select(t=>t.FullName).ToArray();
            a.MainModule.Resources.Remove(resource);a.MainModule.Resources.Add(new EmbeddedResource(resource.Name,resource.Attributes,empty));
            if(mode is "types" or "folded" or "tables")
            {
                void Remove(Mono.Collections.Generic.Collection<TypeDefinition> types){for(int i=types.Count-1;i>=0;i--){if(!retained.Contains(types[i]))types.RemoveAt(i);else Remove(types[i].NestedTypes);}}
                Remove(a.MainModule.Types);
            }
            using var csp=new RSACryptoServiceProvider();csp.ImportParameters(rsa.ExportParameters(true));
            a.MainModule.Mvid=new Guid(SHA256.HashData(Encoding.UTF8.GetBytes("YMCC-UI-TRIM-v1\n"+mode+"\n"+Util.HashFile(input)))[..16]);
            a.Write(dest,new WriterParameters{StrongNameKeyBlob=csp.ExportCspBlob(true),WriteSymbols=false});
            File.WriteAllBytes(dest,DevelopmentSigning.Resign(File.ReadAllBytes(dest),keyPath,publicKey));
            using var candidate=Util.Read(dest,out var cr);using var cleanup=cr;
            var methods=Util.Types(candidate.MainModule).SelectMany(t=>t.Methods).ToArray();var mismatches=methods.Where(m=>!originalBodies.TryGetValue(m.FullName,out var body)||body!=Body(m)).Select(m=>m.FullName).ToArray();
            if(mismatches.Length!=0)throw new InvalidDataException("Retained method IL changed: "+string.Join(";",mismatches.Take(10)));
            var changedMethods=methods.Where(m=>pinnedBodies[pair.Key][m.FullName]!=Body(m)).Select(m=>new{Method=m.FullName,BeforeSha256=Util.Hash(Encoding.UTF8.GetBytes(pinnedBodies[pair.Key][m.FullName])),AfterSha256=Util.Hash(Encoding.UTF8.GetBytes(Body(m)))}).ToArray();
            var allowedChanges=new HashSet<string>();if(mode is "folded" or "tables")allowedChanges.Add("System.Void iNKORE.UI.WPF.Modern.Controls.SettingsCard::.cctor()");if(mode=="tables")foreach(string t in StaticTableSlice.TypeNames)allowedChanges.Add("System.Void "+t+"::.cctor()");
            if(changedMethods.Any(m=>!allowedChanges.Contains(m.Method)))throw new InvalidDataException("Unexpected method rewrite");
            if(!DevelopmentSigning.Verify(File.ReadAllBytes(dest)))throw new InvalidDataException("Bad signature");
            results.Add(new{Assembly=pair.Key,InputSha256=Util.HashFile(input),InputBytes=new FileInfo(input).Length,Path=dest,Sha256=Util.HashFile(dest),Bytes=new FileInfo(dest).Length,BeforeTypes=originalTypes.Length,Types=Util.Types(candidate.MainModule).Count(),Methods=methods.Length,ChangedMethods=changedMethods,RetainedMethodBodiesIdentical=changedMethods.Length==0,AllOtherRetainedMethodBodiesIdentical=true,RemovedResourceEntries=blob.Entries.Select(e=>new{e.Name,e.Bytes,Sha256=Util.Hash(e.Raw)}).ToArray(),ResourceBytesBefore=resource.GetResourceData().Length,ResourceBytesAfter=empty.Length,RsaSignatureValid=true,WindowsSignature=DevelopmentSigning.WindowsVerify(dest),PublicKeyToken=Convert.ToHexString(candidate.Name.PublicKeyToken).ToLowerInvariant()});
        }
        Util.Save(Path.Combine(output,"build-result.json"),new{Status="candidate-built",Mode=mode,Results=results,HardwareExecuted=false,ProductionModified=false,Pending="Strict path loading, all HC GetTypes, seven pure constructors, negative dependency gates"});
        Console.WriteLine(JsonSerializer.Serialize(new{Status="candidate-built",Mode=mode,Report=Path.Combine(output,"build-result.json")}));
    }
}
