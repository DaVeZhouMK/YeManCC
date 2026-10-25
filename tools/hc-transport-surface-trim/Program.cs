using Mono.Cecil;
using System.Security.Cryptography;
using System.Text.Json;
using System.Xml.Linq;

static class Program
{
    static readonly JsonSerializerOptions Json=new(){WriteIndented=true};
    static IEnumerable<TypeDefinition> Types(TypeDefinition t){yield return t;foreach(var n in t.NestedTypes)foreach(var x in Types(n))yield return x;}
    static IEnumerable<TypeDefinition> Types(AssemblyDefinition a)=>a.MainModule.Types.SelectMany(Types);
    static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
    static void Write(string path,object value){Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);File.WriteAllText(path,JsonSerializer.Serialize(value,Json));}
    static AssemblyDefinition Read(string path){var r=new DefaultAssemblyResolver();r.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);var core=System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory();r.AddSearchDirectory(core);var shared=Directory.GetParent(core.TrimEnd(Path.DirectorySeparatorChar))!.Parent!.FullName;var desktop=Path.Combine(shared,"Microsoft.WindowsDesktop.App",new DirectoryInfo(core).Name);if(Directory.Exists(desktop))r.AddSearchDirectory(desktop);return AssemblyDefinition.ReadAssembly(path,new ReaderParameters{AssemblyResolver=r});}
    static string Scope(TypeReference t){var scope=t.Scope is AssemblyNameReference a?a.Name:t.Scope is ModuleDefinition m?m.Assembly.Name.Name:t.Scope.Name;if(scope is "mscorlib" or "netstandard" or "Microsoft.Win32.Primitives" || scope.StartsWith("System.",StringComparison.Ordinal)){var resolved=t.Resolve();if(resolved!=null)return resolved.Module.Assembly.Name.Name;}return scope;}
    static string Value(CustomAttributeArgument a)=>a.Value switch{TypeReference t=>t.FullName+"["+Scope(t)+"]",CustomAttributeArgument[] array=>"["+string.Join(",",array.Select(Value))+"]",_=>a.Value?.ToString()??"null"};
    static string Attrs(ICustomAttributeProvider p)=>string.Join(";",p.CustomAttributes.Select(a=>a.AttributeType.FullName+":"+a.Constructor.FullName+":"+string.Join(",",a.ConstructorArguments.Select(Value))+":"+string.Join(",",a.Fields.Select(f=>f.Name+"="+Value(f.Argument)))+":"+string.Join(",",a.Properties.Select(f=>f.Name+"="+Value(f.Argument)))).Order());
    static string Marshal(MarshalInfo? m)=>m switch {
        null=>"none",ArrayMarshalInfo a=>$"{m.NativeType}|{a.ElementType}|{a.Size}|{a.SizeParameterIndex}|{a.SizeParameterMultiplier}",
        FixedArrayMarshalInfo a=>$"{m.NativeType}|{a.ElementType}|{a.Size}",FixedSysStringMarshalInfo s=>$"{m.NativeType}|{s.Size}",
        SafeArrayMarshalInfo a=>$"{m.NativeType}|{a.ElementType}",CustomMarshalInfo c=>$"{m.NativeType}|{c.Guid}|{c.UnmanagedType}|{c.ManagedType?.FullName}|{c.Cookie}",_=>m.NativeType.ToString()};
    static bool Layout(TypeDefinition t)=>t.IsValueType||t.IsSequentialLayout||t.IsExplicitLayout;
    static bool Delegate(TypeDefinition t)=>t.BaseType?.FullName=="System.MulticastDelegate";
    static bool Lifecycle(MethodDefinition m)=>m.IsConstructor||m.Name is "Dispose" or "Finalize" or "Close" or "Disconnect" or "Stop" or "Cleanup" or "ReleaseHandle"||m.Name.StartsWith("add_",StringComparison.Ordinal)||m.Name.StartsWith("remove_",StringComparison.Ordinal);
    static string MethodKey(MethodDefinition m)=>m.FullName+"|arity="+m.GenericParameters.Count;
    static string Pinvoke(MethodDefinition m)=>m.PInvokeInfo==null?"none":m.FullName+"|"+m.PInvokeInfo.Module?.Name+"|"+m.PInvokeInfo.EntryPoint+"|"+m.PInvokeInfo.Attributes+"|"+m.CallingConvention+"|"+m.ImplAttributes+"|"+Attrs(m)+"|ret="+Marshal(m.MethodReturnType.MarshalInfo)+"|"+string.Join(";",m.Parameters.Select(p=>p.ParameterType.FullName+":"+p.Attributes+":"+Marshal(p.MarshalInfo)+":"+Attrs(p)));
    static string Shape(TypeDefinition t)=>t.Attributes+"|"+t.BaseType?.FullName+"|pack="+t.PackingSize+"|size="+t.ClassSize+"|"+Attrs(t)+"|"+string.Join(";",t.Fields.Select(f=>f.FullName+":"+f.Attributes+":"+f.Offset+":"+f.Constant+":"+Marshal(f.MarshalInfo)+":"+Attrs(f)+":"+Convert.ToBase64String(f.InitialValue)));
    static void Inventory(string root,string output)
    {
        var records=new List<object>();foreach(string path in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Order()) {
            AssemblyDefinition a;try{a=Read(path);}catch(BadImageFormatException){continue;}
            using(a){var ts=Types(a).ToArray();records.Add(new{path=Path.GetRelativePath(root,path),name=a.Name.Name,identity=a.Name.FullName,sha256=Hash(path),bytes=new FileInfo(path).Length,architecture=a.MainModule.Architecture.ToString(),moduleAttributes=a.MainModule.Attributes.ToString(),ilOnly=(a.MainModule.Attributes&ModuleAttributes.ILOnly)!=0,types=ts.Length,methods=ts.Sum(t=>t.Methods.Count),nativeMethods=ts.SelectMany(t=>t.Methods).Where(m=>m.IsNative||m.IsUnmanaged).Select(m=>new{m.FullName,flags=m.ImplAttributes.ToString(),m.RVA}),pinvokes=ts.SelectMany(t=>t.Methods).Where(m=>m.HasPInvokeInfo).Select(m=>new{m.FullName,module=m.PInvokeInfo?.Module?.Name,entryPoint=m.PInvokeInfo?.EntryPoint}),references=a.MainModule.AssemblyReferences.Select(n=>n.FullName)});}
        }Write(output,new{schemaVersion=1,hardwareExecuted=false,assemblies=records});Console.WriteLine($"MANAGED INVENTORY {records.Count}");
    }
    static void Roots(string path,string output)
    {
        using var a=Read(path);var ts=Types(a).ToArray();var ar=new XElement("assembly",new XAttribute("fullname",a.Name.Name));int nativeTypes=0,lifecycle=0;
        foreach(var t in ts){if(Layout(t)||Delegate(t)||t.Methods.Any(m=>m.HasPInvokeInfo)){ar.Add(new XElement("type",new XAttribute("fullname",t.FullName),new XAttribute("preserve","all")));nativeTypes++;}else{var ms=t.Methods.Where(Lifecycle).ToArray();if(ms.Length>0){var e=new XElement("type",new XAttribute("fullname",t.FullName));foreach(var m in ms){e.Add(new XElement("method",new XAttribute("signature",m.FullName[(m.ReturnType.FullName.Length+1)..].Replace(t.FullName+"::",""))));lifecycle++;}ar.Add(e);}}}
        // Method signature descriptors must include return type. Emit exact overloads from Cecil's name/parameter table.
        foreach(var e in ar.Elements("type"))foreach(var m in e.Elements("method").ToArray()){string type=e.Attribute("fullname")!.Value;string sig=m.Attribute("signature")!.Value;var def=ts.First(t=>t.FullName==type).Methods.FirstOrDefault(d=>d.FullName[(d.ReturnType.FullName.Length+1)..].Replace(type+"::","")==sig);if(def!=null)m.SetAttributeValue("signature",def.ReturnType.FullName+" "+def.Name+(def.GenericParameters.Count>0?"``"+def.GenericParameters.Count:"")+"("+string.Join(",",def.Parameters.Select(p=>p.ParameterType.FullName))+")");}
        new XDocument(new XElement("linker",ar)).Save(output);Write(output+".json",new{assembly=a.Name.FullName,nativeAndLayoutTypesFullyRooted=nativeTypes,lifecycleMethodsExplicitlyRooted=lifecycle,preserveAllOriginalPInvokes=true,preserveEveryOriginalValueTypeLayout=true,hardwareAccess=false});Console.WriteLine($"TRANSPORT ROOTS {nativeTypes} native/layout types; {lifecycle} lifecycle methods");
    }
    static void NativeVerify(string original,string candidate,string output)
    {
        using var a=Read(original);using var b=Read(candidate);var ts=Types(a).ToArray();var bs=Types(b).ToDictionary(t=>t.FullName);var failures=new List<string>();int layouts=0,imports=0,lifecycle=0;
        if(a.Name.FullName!=b.Name.FullName)failures.Add("assembly identity");
        foreach(var t in ts){if(Layout(t)||Delegate(t)){layouts++;if(!bs.TryGetValue(t.FullName,out var bt))failures.Add("missing ABI type: "+t.FullName);else if(Shape(t)!=Shape(bt))failures.Add("changed ABI type: "+t.FullName+" BEFORE "+Shape(t)+" AFTER "+Shape(bt));}
            foreach(var m in t.Methods){if(!m.HasPInvokeInfo&&!Lifecycle(m))continue;if(m.HasPInvokeInfo)imports++;if(Lifecycle(m))lifecycle++;var cm=bs.GetValueOrDefault(t.FullName)?.Methods.FirstOrDefault(x=>MethodKey(x)==MethodKey(m));if(cm==null)failures.Add("missing import/lifecycle: "+MethodKey(m));else if(m.HasPInvokeInfo&&Pinvoke(m)!=Pinvoke(cm))failures.Add("changed PInvoke: "+MethodKey(m));}
        }
        var resources=new List<object>();foreach(var r in a.MainModule.Resources.OfType<EmbeddedResource>()){if(r.Name.Contains("ILLink",StringComparison.OrdinalIgnoreCase)||r.Name.EndsWith(".pdb",StringComparison.OrdinalIgnoreCase))continue;var br=b.MainModule.Resources.OfType<EmbeddedResource>().FirstOrDefault(x=>x.Name==r.Name);var before=Convert.ToHexString(SHA256.HashData(r.GetResourceData()));var after=br==null?null:Convert.ToHexString(SHA256.HashData(br.GetResourceData()));resources.Add(new{r.Name,before,after});if(before!=after)failures.Add("changed resource: "+r.Name);}
        Write(output,new{status=failures.Count==0?"passed":"failed",original=Hash(original),candidate=Hash(candidate),allOriginalAbiTypesChecked=layouts,allOriginalPInvokesChecked=imports,allOriginalLifecycleMethodsChecked=lifecycle,embeddedPayloads=resources,failures,hardwareExecuted=false});Console.WriteLine($"NATIVE SURFACE {layouts} layouts; {imports} original imports; {lifecycle} lifecycle methods; {failures.Count} differences");if(failures.Count>0)Environment.ExitCode=1;
    }

    static void Reverse(string root,string targetsFile,string output)
    {
        var targets=JsonSerializer.Deserialize<string[]>(File.ReadAllText(targetsFile))!.ToHashSet(StringComparer.Ordinal);var rows=new List<object>();var strings=new List<object>();var reflection=new List<object>();var refs=new List<object>();
        string? Target(TypeReference? t){if(t==null||t is GenericParameter)return null;if(t is GenericInstanceType gi){var found=gi.GenericArguments.Select(Target).FirstOrDefault(x=>x!=null);if(found!=null)return found;}while(t is TypeSpecification spec){if(spec.ElementType==null)return null;t=spec.ElementType;}if(t.Scope==null)return null;string name=t.Scope is AssemblyNameReference a?a.Name:t.Scope is ModuleDefinition m?m.Assembly.Name.Name:t.Scope.Name;return targets.Contains(name)?name:null;}
        foreach(string path in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Order()) {
            AssemblyDefinition a;try{a=Read(path);}catch(BadImageFormatException){continue;}using(a){string source=a.Name.Name;foreach(var r in a.MainModule.AssemblyReferences.Where(r=>targets.Contains(r.Name)))refs.Add(new{source,target=r.Name,sourceFile=Path.GetRelativePath(root,path),r.FullName});
                foreach(var t in Types(a)){if(t.BaseType!=null&&Target(t.BaseType) is string b)rows.Add(new{source,target=b,owner=t.FullName,kind="base",member=t.BaseType.FullName});foreach(var f in t.Fields)if(Target(f.FieldType) is string ft)rows.Add(new{source,target=ft,owner=t.FullName,kind="field",member=f.FullName});
                    foreach(var m in t.Methods){foreach(var tr in m.Parameters.Select(p=>p.ParameterType).Append(m.ReturnType)){if(Target(tr) is string param)rows.Add(new{source,target=param,owner=t.FullName,kind="method-signature",member=m.FullName});}
                        if(!m.HasBody)continue;foreach(var ins in m.Body.Instructions){
                            if(ins.Operand is MemberReference mr&&mr.DeclaringType!=null&&Target(mr.DeclaringType) is string dst)rows.Add(new{source,target=dst,owner=t.FullName,kind="IL:"+ins.OpCode,member=m.FullName,called=mr.FullName});
                            if(ins.Operand is string str&&targets.Any(n=>str.Contains(n,StringComparison.OrdinalIgnoreCase)))strings.Add(new{source,owner=t.FullName,method=m.FullName,str});
                            if(ins.Operand is MethodReference method&&method.DeclaringType.FullName is "System.Reflection.Assembly" or "System.Type" or "System.Activator"&&method.Name is "Load" or "LoadFrom" or "LoadFile" or "GetType" or "CreateInstance")reflection.Add(new{source,owner=t.FullName,method=m.FullName,api=method.FullName});
                        }
                    }
                }
            }
        }
        Write(output,new{schemaVersion=1,mode="metadata assembly refs + actual IL calls/fields/signatures + literal reflection candidates",targets=targets.Order(),assemblyReferences=refs,edges=rows,stringOwners=strings,reflectionSites=reflection,limitation="Indirect computed assembly names and native runtime activation are not resolved by absence of a literal; no deletion approval from name search alone",hardwareExecuted=false});Console.WriteLine($"REVERSE {refs.Count} AssemblyRefs; {rows.Count} edges; {strings.Count} literals");
    }
    static void Mutation(string before,string kind,string after,string report)
    {
        using var a=Read(before);if((a.MainModule.Attributes&ModuleAttributes.ILOnly)==0)throw new InvalidOperationException("Never write mixed mode");string changed;
        if(kind=="layout"){var t=Types(a).First(t=>Layout(t)&&t.ClassSize>0);changed=t.FullName+" native ClassSize "+t.ClassSize+" -> "+(t.ClassSize+4);t.ClassSize+=4;}
        else if(kind=="pinvoke"){var m=Types(a).SelectMany(t=>t.Methods).First(m=>m.HasPInvokeInfo);changed=m.FullName+" native entry renamed";m.PInvokeInfo.EntryPoint+="__negative_fixture";}
        else if(kind=="lifecycle"){var t=Types(a).First(t=>t.Methods.Any(m=>m.Name=="Dispose"));var m=t.Methods.First(m=>m.Name=="Dispose");changed=m.FullName+" protected lifecycle API renamed";m.Name+="__negative_fixture";}
        else throw new ArgumentException("layout/pinvoke/lifecycle");
        a.Write(after);Write(report,new{kind,changed,beforeSha256=Hash(before),afterSha256=Hash(after),fixtureOnly=true,executed=false});
    }

    static void Main(string[] args){switch(args[0]){case "reverse":Reverse(args[1],args[2],args[3]);break;case "mutation":Mutation(args[1],args[2],args[3],args[4]);break;case "inventory":Inventory(args[1],args[2]);break;case "roots":Roots(args[1],args[2]);break;case "nativeverify":NativeVerify(args[1],args[2],args[3]);break;default:throw new ArgumentException("inventory / roots / nativeverify");}}
}




