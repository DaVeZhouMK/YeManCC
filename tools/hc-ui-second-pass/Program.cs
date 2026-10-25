using Mono.Cecil;
using Mono.Cecil.Cil;
using HcDeviceCoreTrim;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Reflection.PortableExecutable;

if(args.Length<3)throw new ArgumentException("inventory <baseline-runtime> <report> | build <baseline-runtime> <key> <output> | audit <baseline-runtime> <candidate-runtime> <report> | corrupt-signature <input> <output>");
if(args[0]=="corrupt-signature"){
    byte[] b=File.ReadAllBytes(args[1]);using var pe=new PEReader(new MemoryStream(b));var d=pe.PEHeaders.CorHeader!.StrongNameSignatureDirectory;
    var s=pe.PEHeaders.SectionHeaders.Single(s=>d.RelativeVirtualAddress>=s.VirtualAddress&&d.RelativeVirtualAddress<s.VirtualAddress+s.VirtualSize);
    b[s.PointerToRawData+d.RelativeVirtualAddress-s.VirtualAddress]^=1;File.WriteAllBytes(args[2],b);
    Util.Save(args[2]+".json",new{Status="passed",SignatureCorrupted=true,SignatureVerificationRejected=!DevelopmentSigning.Verify(b),OriginalSha256=Util.HashFile(args[1]),CorruptSha256=Util.Hash(b)});return;
}
using var graph=new MethodGraph(args[1]);
if(args[0]=="inventory"){graph.Save(args[2]);return;}
if(args[0]=="build"&&args.Length==4){graph.Build(args[2],args[3]);return;}
if(args[0]=="audit"&&args.Length==4){SecondAudit.Run(graph,args[2],args[3]);return;}
throw new ArgumentException("Invalid command");

sealed class MethodGraph:IDisposable
{
    public static readonly string[] Names=["iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Controls"];
    public string Root {get;}
    public readonly Dictionary<string,AssemblyDefinition> Targets=[];
    public readonly Dictionary<string,MethodDefinition> Methods=[];
    public readonly Dictionary<string,HashSet<string>> Reasons=[];
    public readonly Dictionary<string,List<string>> Edges=[];
    public readonly List<object> ExternalReferences=[];
    public readonly List<object> InputFiles=[];
    public readonly List<object> Dynamic=[];
    public readonly List<object> Interfaces=[];
    public readonly List<string> Strings=[];
    readonly List<IDisposable> owned=[];
    readonly List<AssemblyDefinition> consumers=[];
    readonly Dictionary<string,TypeDefinition> localTypes=[];
    readonly Dictionary<string,bool> boundaryCache=[];
    readonly Queue<MethodDefinition> pending=[];
    readonly HashSet<string> literals=new(StringComparer.Ordinal);
    public static string Key(MethodDefinition m)=>m.Module.Assembly.Name.Name+":"+m.FullName;
    public static string? Target(TypeReference t){while(t is TypeSpecification s)t=s.ElementType;if(t is GenericParameter)return null;return t.Scope is AssemblyNameReference a?a.Name:t.Scope is ModuleDefinition m?m.Assembly.Name.Name:null;}
    public static bool IsUi(TypeReference t)=>Names.Contains(Target(t));
    public static bool IsManaged(string file){using var p=new PEReader(File.OpenRead(file));return p.HasMetadata;}
    public MethodGraph(string root)
    {
        Root=Path.GetFullPath(root);
        var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(Root,"HandheldCompanion.runtime.json")));
        foreach(var entry in manifest.RootElement.GetProperty("files").EnumerateArray()){
            string rel=entry.GetProperty("path").GetString()!;string path=Path.GetFullPath(Path.Combine(Root,rel));
            if(!path.StartsWith(Root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Escaping manifest path");
            string hash=Util.HashFile(path);if(hash!=entry.GetProperty("sha256").GetString())throw new InvalidDataException("Manifest mismatch: "+rel);
            bool pe=Path.GetExtension(path) is ".dll" or ".exe",managed=pe&&IsManaged(path);
            InputFiles.Add(new{Path=rel,Sha256=hash,Bytes=new FileInfo(path).Length,Managed=managed});
            if(!managed)continue;
            var a=Util.Read(path,out var resolver);owned.Add(a);owned.Add(resolver);consumers.Add(a);
            if(Names.Contains(a.Name.Name))Targets.Add(a.Name.Name,a);
        }
        if(Targets.Count!=2)throw new InvalidDataException("Need both UI inputs");
        foreach(var a in consumers)foreach(var t in Util.Types(a.MainModule))localTypes.TryAdd(a.Name.Name+":"+t.FullName,t);
        foreach(var a in Targets.Values)foreach(var t in Util.Types(a.MainModule))foreach(var m in t.Methods){Methods.Add(Key(m),m);Edges.Add(Key(m),[]);}
        foreach(var a in consumers){
            foreach(var m in Util.Types(a.MainModule).SelectMany(t=>t.Methods).Where(m=>m.HasBody))foreach(var i in m.Body.Instructions){
                if(i.Operand is string text){literals.Add(text);Strings.Add(a.Name.Name+":"+m.FullName+":"+text);}
                if(i.Operand is MethodReference mr&&IsDynamic(mr))Dynamic.Add(new{Assembly=a.Name.Name,Method=m.FullName,Offset=i.Offset,Call=mr.FullName,Strings=m.Body.Instructions.Where(i=>i.Operand is string).Select(i=>(string)i.Operand).ToArray(),Policy="All constructors/property/event/virtual roots retained; type/name tokens conservatively rooted; serializers and dynamic binders pin referenced UI types"});
            }
            if(Names.Contains(a.Name.Name))continue;
            foreach(var r in a.MainModule.GetMemberReferences().Where(r=>r.DeclaringType!=null&&Boundary(r.DeclaringType))) {
                string resolved=SecondAudit.Resolve(r);ExternalReferences.Add(new{Consumer=a.Name.Name,Reference=r.FullName,Resolved=resolved});
                if(r is MethodReference mr)Mark(Find(mr),"external:"+a.Name.Name+":"+r.FullName);
            }
            foreach(var r in a.MainModule.GetTypeReferences().Where(Boundary)){
                ExternalReferences.Add(new{Consumer=a.Name.Name,Reference=r.FullName,Resolved=SecondAudit.Resolve(r)});
            }
        }
        foreach(var a in Targets.Values)foreach(var t in Util.Types(a.MainModule)){
            var ifaceNames=new HashSet<string>();
            void Interface(TypeDefinition td,HashSet<string> seen){
                if(!seen.Add(td.Module.Assembly.Name.Name+":"+td.FullName))return;
                foreach(var it in td.Interfaces){var id=it.InterfaceType.Resolve()??throw new InvalidDataException("Unresolved interface");foreach(var im in id.Methods)ifaceNames.Add(im.Name);Interface(id,seen);}
                if(td.BaseType!=null)Interface(td.BaseType.Resolve()??throw new InvalidDataException("Unresolved base type"),seen);
            }
            Interface(t,[]);
            if(ifaceNames.Count>0)Interfaces.Add(new{Assembly=a.Name.Name,Type=t.FullName,InterfaceMethodNames=ifaceNames.Order().ToArray()});
            bool serialized=t.IsSerializable||t.CustomAttributes.Any(c=>c.AttributeType.FullName.Contains("DataContract")||c.AttributeType.FullName.Contains("Json")||c.AttributeType.FullName.Contains("TypeConverter"))||BaseIs(t,"System.Attribute");
            foreach(var m in t.Methods){
                if(m.HasBody&&m.Body.Instructions.Any(i=>i.Operand is MethodReference dr&&IsDynamic(dr)))foreach(var family in t.Methods)Mark(family,"reflection/dynamic/serializer owner family");
                if(m.IsConstructor)Mark(m,"constructor/static initializer/reflection factory");
                if(m.IsSpecialName)Mark(m,"operator/conversion/runtime convention");
                if(m.IsVirtual||m.IsAbstract||m.HasOverrides)Mark(m,"virtual/override/interface dispatch and finalizer");
                if(ifaceNames.Contains(m.Name)||m.Name.Contains('.' )&&ifaceNames.Contains(m.Name[(m.Name.LastIndexOf('.')+1)..]))Mark(m,"interface implementation (including inherited interface)");
                if(m.SemanticsAttributes!=MethodSemanticsAttributes.None)Mark(m,"property/event accessor");
                if(m.HasCustomAttributes)Mark(m,"method custom attributes/serialization callbacks");
                if(m.IsPInvokeImpl||!m.IsIL)Mark(m,"native/runtime implementation");
                if(m.Name.StartsWith("Get",StringComparison.Ordinal)||m.Name.StartsWith("Set",StringComparison.Ordinal)||m.Name.StartsWith("On",StringComparison.Ordinal))Mark(m,"WPF attached property/route/convention callback");
                if(serialized)Mark(m,"serialization/type-converter/attribute type");
                if(literals.Contains(m.Name)||literals.Contains(m.FullName)||literals.Contains(t.FullName))Mark(m,"literal method/type reflection root");
            }
            foreach(var p in t.Properties){Mark(p.GetMethod,"property getter");Mark(p.SetMethod,"property setter");foreach(var m in p.OtherMethods)Mark(m,"property other method");}
            foreach(var e in t.Events){Mark(e.AddMethod,"event add");Mark(e.RemoveMethod,"event remove");Mark(e.InvokeMethod,"event invoke");foreach(var m in e.OtherMethods)Mark(m,"event other method");}
        }
        foreach(var a in consumers)foreach(var t in Util.Types(a.MainModule)){
            Attributes(a);Attributes(a.MainModule);Attributes(t);
            foreach(var f in t.Fields)Attributes(f);foreach(var p in t.Properties)Attributes(p);foreach(var e in t.Events)Attributes(e);
            foreach(var m in t.Methods){Attributes(m);Attributes(m.MethodReturnType);foreach(var p in m.Parameters)Attributes(p);
                if(!m.HasBody)continue;
                bool dynamic=m.Body.Instructions.Any(i=>i.Operand is MethodReference r&&(r.DeclaringType.FullName.StartsWith("Microsoft.CSharp.RuntimeBinder",StringComparison.Ordinal)||r.DeclaringType.FullName.Contains("Serializer",StringComparison.Ordinal)));
                foreach(var i in m.Body.Instructions){
                    if(i.OpCode==OpCodes.Ldtoken&&i.Operand is TypeReference tr&&IsUi(tr))foreach(var tm in LocalType(tr).Methods)Mark(tm,"type token/reflection/serializer");
                    if(dynamic&&i.Operand is MemberReference member&&member.DeclaringType!=null&&IsUi(member.DeclaringType))foreach(var tm in LocalType(member.DeclaringType).Methods)Mark(tm,"dynamic/serializer boundary");
                }
            }
        }
        foreach(var m in Methods.Values){
            foreach(var o in m.Overrides)AddEdge(m,Find(o));
            if(m.HasBody)foreach(var i in m.Body.Instructions)if(i.Operand is MethodReference call)AddEdge(m,Find(call));
        }
        while(pending.TryDequeue(out var m))foreach(string next in Edges[Key(m)])Mark(Methods[next],"IL/delegate/callback closure:"+Key(m));
    }
    public bool Boundary(TypeReference t){
        while(t is TypeSpecification spec)t=spec.ElementType;if(t is GenericParameter)return false;if(IsUi(t))return true;
        string key=Target(t)+":"+t.FullName;if(boundaryCache.TryGetValue(key,out bool cached))return cached;
        boundaryCache[key]=false;
        if(localTypes.TryGetValue(key,out var definition)&&definition.BaseType!=null)boundaryCache[key]=Boundary(definition.BaseType);
        return boundaryCache[key];
    }
    static bool IsDynamic(MethodReference r)=>r.DeclaringType.Namespace.StartsWith("System.Reflection",StringComparison.Ordinal)||r.DeclaringType.FullName.StartsWith("Microsoft.CSharp.RuntimeBinder",StringComparison.Ordinal)||r.DeclaringType.FullName.Contains("Serializer",StringComparison.Ordinal)||r.Name is "GetType" or "GetTypes" or "GetMethod" or "GetMethods" or "CreateInstance" or "CreateDelegate" or "InvokeMember";
    static bool BaseIs(TypeDefinition t,string name){while(t.BaseType!=null){if(t.BaseType.FullName==name)return true;t=t.BaseType.Resolve()??throw new InvalidDataException("Unresolved base");}return false;}
    TypeDefinition LocalType(TypeReference r){while(r is TypeSpecification s)r=s.ElementType;return Util.Types(Targets[Target(r)!].MainModule).SingleOrDefault(t=>t.FullName==r.FullName)??throw new InvalidDataException("Missing token/dynamic type: "+Target(r)+":"+r.FullName);}
    MethodDefinition? Find(MethodReference r){
        if(!Boundary(r.DeclaringType))return null;
        var d=r.Resolve()??throw new InvalidDataException("Unresolved UI member: "+r.FullName);
        return Methods.GetValueOrDefault(Key(d));
    }
    void Attributes(ICustomAttributeProvider p){foreach(var a in p.CustomAttributes){Mark(Find(a.Constructor),"custom attribute constructor");foreach(var n in a.Properties)foreach(var t in Targets.Values.SelectMany(a=>Util.Types(a.MainModule)).Where(t=>t.FullName==a.AttributeType.FullName))foreach(var m in t.Methods.Where(m=>m.Name=="set_"+n.Name))Mark(m,"custom attribute named property");}}
    void AddEdge(MethodDefinition from,MethodDefinition? to){if(to!=null&&!Edges[Key(from)].Contains(Key(to)))Edges[Key(from)].Add(Key(to));}
    void Mark(MethodDefinition? m,string reason){if(m==null)return;string key=Key(m);if(!Methods.ContainsKey(key))return;if(!Reasons.TryGetValue(key,out var reasons)){Reasons.Add(key,reasons=[]);pending.Enqueue(m);}reasons.Add(reason);}
    public string[] Removed=>Methods.Keys.Where(k=>!Reasons.ContainsKey(k)).Order().ToArray();
    public void Save(string path)=>Util.Save(path,new{Status="passed",Root,InputFiles,ExternalReferences,AllManagedConsumersScanned=true,Retained=Reasons.OrderBy(p=>p.Key).Select(p=>new{Method=p.Key,Reasons=p.Value.Order().ToArray(),Callees=Edges[p.Key]}).ToArray(),Removed=Removed.Select(k=>new{Method=k,Bytes=Methods[k].HasBody?Methods[k].Body.CodeSize:0,Incoming=Edges.Where(p=>p.Value.Contains(k)).Select(p=>p.Key).ToArray(),Proof="No retained caller; no constructor/virtual/property/event/DP/reflection/interface/serialization root"}).ToArray(),Interfaces,Dynamic,TotalMethods=Methods.Count,RetainedMethods=Reasons.Count,RemovedMethods=Removed.Length,EstimatedRemovedIlBytes=Removed.Sum(k=>Methods[k].HasBody?Methods[k].Body.CodeSize:0),HardwareExecuted=false});
    public void Build(string keyPath,string output){
        if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new IOException("Output must be empty");Directory.CreateDirectory(output);Save(Path.Combine(output,"method-closure.json"));
        using var rsa=RSA.Create();rsa.ImportFromPem(File.ReadAllText(keyPath));string publicKey=Convert.ToHexString(DevelopmentSigning.PublicKey(rsa));var results=new List<object>();
        foreach(var p in Targets){
            var a=p.Value;var removed=Removed.ToHashSet();foreach(var t in Util.Types(a.MainModule))for(int i=t.Methods.Count-1;i>=0;i--)if(removed.Contains(Key(t.Methods[i])))t.Methods.RemoveAt(i);
            string dest=Path.Combine(output,p.Key+".dll");using var csp=new RSACryptoServiceProvider();csp.ImportParameters(rsa.ExportParameters(true));
            a.MainModule.Mvid=new Guid(SHA256.HashData(Encoding.UTF8.GetBytes("YMCC-UI-SECOND-v1\n"+Util.HashFile(Path.Combine(Root,p.Key+".dll"))+"\n"+string.Join("\n",Removed)))[..16]);
            a.Write(dest,new WriterParameters{StrongNameKeyBlob=csp.ExportCspBlob(true),WriteSymbols=false});File.WriteAllBytes(dest,DevelopmentSigning.Resign(File.ReadAllBytes(dest),keyPath,publicKey));
            bool valid=DevelopmentSigning.Verify(File.ReadAllBytes(dest));if(!valid)throw new InvalidDataException("Signature failed");results.Add(new{Assembly=p.Key,Path=dest,Bytes=new FileInfo(dest).Length,Sha256=Util.HashFile(dest),BaselineBytes=new FileInfo(Path.Combine(Root,p.Key+".dll")).Length,Token=Convert.ToHexString(a.Name.PublicKeyToken).ToLowerInvariant(),RsaSignatureValid=valid,WindowsSignature=DevelopmentSigning.WindowsVerify(dest)});
        }
        Util.Save(Path.Combine(output,"build-result.json"),new{Status="passed",Results=results,RemovedMethods=Removed.Length,HardwareExecuted=false,ProductionModified=false});
    }
    public void Dispose(){foreach(var d in owned)d.Dispose();}
}





