using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Xml.Linq;
namespace WinrtSecondPass;
internal static class Entry
{
 static readonly JsonSerializerOptions Json=new(){WriteIndented=true};
 static string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
 static void Write(string p,object value){Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(p))!);File.WriteAllText(p,JsonSerializer.Serialize(value,Json));}
 static IEnumerable<TypeDefinition> Types(TypeDefinition t){yield return t;foreach(var n in t.NestedTypes)foreach(var x in Types(n))yield return x;}
 static IEnumerable<TypeDefinition> Types(ModuleDefinition m)=>m.Types.SelectMany(Types);
 static DefaultAssemblyResolver Resolver(params string[] roots){var r=new DefaultAssemblyResolver();foreach(var p in roots)r.AddSearchDirectory(p);r.AddSearchDirectory(System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory());r.AddSearchDirectory("C:/Program Files/dotnet/shared/Microsoft.WindowsDesktop.App/10.0.11");return r;}
 static AssemblyDefinition Read(string p,params string[] roots)=>AssemblyDefinition.ReadAssembly(p,new ReaderParameters{AssemblyResolver=Resolver(new[]{Path.GetDirectoryName(p)!}.Concat(roots).ToArray())});
 static string Scope(TypeReference t){while(t is TypeSpecification s)t=s.ElementType;return t.Scope is AssemblyNameReference a?a.Name:t.Scope is ModuleDefinition m?m.Assembly.Name.Name:t.Scope?.Name??"";}
 static readonly string[] Targets=["Microsoft.Windows.SDK.NET","WinRT.Runtime"];
 public static int Main(string[] args)
 {
  try{switch(args[0]){case "dynamic-audit":DynamicAudit.Run(args[1],args[2],args[3],args[4]);break;case "data-audit":DeepVerifier.DataAudit(args[1],args[2],args[3]);break;case "repack-sdk":DeepVerifier.RepackSdk(args[1],args[2]);break;case "exact-il":DeepVerifier.IL(args[1],args[2],args[3]);break;case "signatures":DeepVerifier.Signatures(args[1],args[2]);break;case "inspect":Inspect(args[1],args[2],args[3]);break;case "roots":Roots(args[1],args[2],args[3],args[4]);break;case "contracts-full":Contracts(args[1],args[2],args[3]);break;case "abi":Abi(args[1],args[2],args[3],args[4]);break;default:typeof(global::Program).GetMethod("Main",BindingFlags.Static|BindingFlags.NonPublic)!.Invoke(null,[args]);break;}return Environment.ExitCode;}
  catch(Exception e){Console.Error.WriteLine(e);return 1;}
 }
 static void Inspect(string root,string shared,string output)
 {
  var rows=new List<object>();var dynamicSites=new List<object>();
  foreach(var file in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Concat(Directory.GetFiles(shared,"*.dll",SearchOption.AllDirectories)).Order()){
   AssemblyDefinition a;try{a=Read(file,root,shared);}catch(BadImageFormatException){continue;}
   using(a){var ts=Types(a.MainModule).ToArray();bool target=Targets.Contains(a.Name.Name);rows.Add(new{path=file,name=a.Name.FullName,bytes=new FileInfo(file).Length,sha256=Hash(file),types=ts.Length,methods=ts.Sum(t=>t.Methods.Count),exports=a.MainModule.ExportedTypes.Select(e=>e.FullName).ToArray(),target,typeDetails=target?ts.Select(t=>new{t.FullName,flags=t.Attributes.ToString(),baseType=t.BaseType?.FullName,attributes=t.CustomAttributes.Select(c=>c.AttributeType.FullName).ToArray(),methods=t.Methods.Select(m=>m.FullName).ToArray()}).ToArray():null});
   foreach(var t in ts)foreach(var m in t.Methods.Where(m=>m.HasBody))foreach(var i in m.Body.Instructions){if(i.Operand is MethodReference mr && (mr.DeclaringType.FullName.Contains("Reflection")||mr.Name.Contains("MakeGeneric")||mr.Name.Contains("GetActivation")||mr.Name.Contains("GetType")||mr.Name.Contains("Marshal")||mr.DeclaringType.FullName.Contains("WinRT")))dynamicSites.Add(new{source=a.Name.Name,owner=m.FullName,offset=i.Offset,target=mr.FullName,scope=Scope(mr.DeclaringType)});}
  }}
  Write(output,new{assemblies=rows,dynamicSites,rootPolicy="Every immutable managed runtime file and every pinned shared bin consumer is rooted in full; no static-absence removal rule"});Console.WriteLine($"INSPECT {rows.Count} managed assemblies, {dynamicSites.Count} dynamic/marshalling sites");
 }
 static void Roots(string root,string shared,string mode,string output)
 {
  var xml=new XElement("linker");var roots=new List<object>();
  foreach(var file in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Concat(Directory.GetFiles(shared,"*.dll",SearchOption.AllDirectories)).Order()){
   AssemblyDefinition a;try{a=Read(file,root,shared);}catch(BadImageFormatException){continue;}
   using(a){if(!Targets.Contains(a.Name.Name)){xml.Add(new XElement("assembly",new XAttribute("fullname",a.Name.Name),new XAttribute("preserve","all")));roots.Add(new{assembly=a.Name.Name,policy="immutable-all",types=Types(a.MainModule).Count()});continue;}
    var entry=new XElement("assembly",new XAttribute("fullname",a.Name.Name));
    foreach(var t in Types(a.MainModule)){
     bool keep=mode=="sdk-data-only" || mode=="full-dynamic" || a.Name.Name=="WinRT.Runtime" || t.FullName.StartsWith("ABI.",StringComparison.Ordinal)||t.Namespace.StartsWith("WinRT",StringComparison.Ordinal)||t.Namespace.StartsWith("Windows.Foundation",StringComparison.Ordinal)||t.CustomAttributes.Any(c=>c.AttributeType.FullName.Contains("WinRT")||c.AttributeType.FullName.Contains("WindowsRuntime"))||t.HasGenericParameters;
     if(keep){entry.Add(new XElement("type",new XAttribute("fullname",t.FullName),new XAttribute("preserve",mode=="sdk-data-only"&&a.Name.Name=="Microsoft.Windows.SDK.NET"&&t.FullName=="<PrivateImplementationDetails>"?"methods":"all")));roots.Add(new{assembly=a.Name.Name,type=t.FullName,policy=mode=="full-dynamic"?"full-existing-dynamic-surface":"activation/ABI/generic/reflection-conservative"});}
    }
    xml.Add(entry);
   }
  }
  new XDocument(xml).Save(output);Write(output+".json",new{mode,roots,reflectionCompletenessProven=false});Console.WriteLine($"ROOTS {roots.Count} records ({mode})");
 }
 static void Contracts(string root,string shared,string output)
 {
  var rows=new List<object>();var missing=new List<object>();int tc=0,mc=0,ec=0;
  // Inspect all immutable consumers into the targets and every external reference/export from the targets.
  foreach(var file in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Concat(Directory.GetFiles(shared,"*.dll",SearchOption.AllDirectories)).Order()){
   AssemblyDefinition a;try{a=Read(file,root,shared);}catch(BadImageFormatException){continue;}
   using(a){bool outgoing=Targets.Contains(a.Name.Name);
    void Check(string kind,string target,string scope,Func<string?> resolve){string? error=null,definition=null;try{definition=resolve();if(definition==null)error="unresolved";}catch(Exception ex){error=ex.GetType().Name+": "+ex.Message;}var row=new{source=a.Name.Name,sourceSha256=Hash(file),kind,target,scope,direction=outgoing?"target-outgoing-or-internal":"consumer-incoming",definition,error};rows.Add(row);if(error!=null)missing.Add(row);}
    foreach(var t in a.MainModule.GetTypeReferences())if(outgoing||Targets.Contains(Scope(t))){tc++;Check("TypeRef",t.FullName,Scope(t),()=>t.Resolve()?.FullName);}
    foreach(var m in a.MainModule.GetMemberReferences())if(outgoing||Targets.Contains(Scope(m.DeclaringType))){mc++;Check("MemberRef",m.FullName,Scope(m.DeclaringType),()=>m switch{MethodReference mr=>mr.Resolve()?.FullName,FieldReference fr=>fr.Resolve()?.FullName,_=>null});}
    foreach(var e in a.MainModule.ExportedTypes)if(outgoing||(e.Scope is AssemblyNameReference ar&&Targets.Contains(ar.Name))){ec++;Check("ExportedType",e.FullName,e.Scope.Name,()=>e.Resolve()?.FullName);}
   }
  }
  Write(output,new{status=missing.Count==0?"passed":"failed",typesChecked=tc,membersChecked=mc,exportsChecked=ec,missingCount=missing.Count,missing,records=rows});Console.WriteLine($"CONTRACTS {tc} TypeRef, {mc} MemberRef, {ec} exports; {missing.Count} unresolved");if(missing.Count>0)Environment.ExitCode=1;
 }
 static string V(CustomAttributeArgument v)=>v.Value switch{TypeReference t=>t.FullName,CustomAttributeArgument[] a=>"["+string.Join(",",a.Select(V))+"]",_=>Convert.ToString(v.Value,System.Globalization.CultureInfo.InvariantCulture)??"null"};
 static string Attr(Mono.Cecil.ICustomAttributeProvider p)=>string.Join(";",p.CustomAttributes.Select(a=>a.Constructor.FullName+":"+string.Join(",",a.ConstructorArguments.Select(V))+":"+string.Join(",",a.Fields.Select(v=>v.Name+"="+V(v.Argument)))+":"+string.Join(",",a.Properties.Select(v=>v.Name+"="+V(v.Argument)))).Order());
 static string Generic(IEnumerable<GenericParameter> ps)=>string.Join(";",ps.Select(p=>p.Position+":"+p.Attributes+":"+Attr(p)+":"+string.Join(",",p.Constraints.Select(c=>c.ConstraintType.FullName+":"+Attr(c)))));
 static string Marshal(MarshalInfo? m)=>m==null?"":m.NativeType+":"+JsonSerializer.Serialize(m,m.GetType());
 static string Field(FieldDefinition f)=>f.FullName+":"+f.Attributes+":"+f.Offset+":"+f.HasConstant+":"+f.Constant+":"+Convert.ToHexString(f.InitialValue)+":"+Marshal(f.MarshalInfo)+":"+Attr(f);
 static string Method(MethodDefinition m)=>m.FullName+":"+m.Attributes+":"+m.ImplAttributes+":"+m.CallingConvention+":"+Generic(m.GenericParameters)+":"+Attr(m)+":"+string.Join(";",m.Parameters.Select(p=>p.Name+":"+p.Attributes+":"+p.HasConstant+":"+p.Constant+":"+Marshal(p.MarshalInfo)+":"+Attr(p)))+":"+Marshal(m.MethodReturnType.MarshalInfo)+":"+Attr(m.MethodReturnType)+":"+(m.HasPInvokeInfo?m.PInvokeInfo.Module.Name+":"+m.PInvokeInfo.EntryPoint+":"+m.PInvokeInfo.Attributes:"")+":"+string.Join(";",m.Overrides.Select(o=>o.FullName));
 static void Abi(string before,string after,string shared,string output)
 {
  var results=new List<object>();var failures=new List<string>();
  foreach(var name in Targets){using var a=Read(Path.Combine(before,name+".dll"),shared);using var b=Read(Path.Combine(after,name+".dll"),shared);var old=Types(a.MainModule).ToDictionary(t=>t.FullName);int fields=0,methods=0,layouts=0;
   if(a.Name.FullName!=b.Name.FullName)failures.Add(name+" assembly identity changed");if(Attr(a)!=Attr(b))failures.Add(name+" assembly attributes changed");
   foreach(var t in Types(b.MainModule)){if(!old.TryGetValue(t.FullName,out var o)){failures.Add("new type "+t.FullName);continue;}
    if(t.Attributes!=o.Attributes||t.BaseType?.FullName!=o.BaseType?.FullName||t.PackingSize!=o.PackingSize||t.ClassSize!=o.ClassSize||Attr(t)!=Attr(o)||Generic(t.GenericParameters)!=Generic(o.GenericParameters))failures.Add("type ABI "+t.FullName);
    if(!t.Interfaces.Select(i=>i.InterfaceType.FullName+":"+Attr(i)).SequenceEqual(o.Interfaces.Select(i=>i.InterfaceType.FullName+":"+Attr(i))))failures.Add("interfaces "+t.FullName);
    if(t.IsValueType||t.IsExplicitLayout||t.IsSequentialLayout){layouts++;if(!t.Fields.Where(f=>!f.IsStatic).Select(Field).SequenceEqual(o.Fields.Where(f=>!f.IsStatic).Select(Field)))failures.Add("full native layout "+t.FullName);}
    foreach(var f in t.Fields){fields++;var x=o.Fields.FirstOrDefault(x=>x.FullName==f.FullName);if(x==null||Field(x)!=Field(f))failures.Add("field ABI "+f.FullName);}
    foreach(var m in t.Methods){methods++;var x=o.Methods.FirstOrDefault(x=>x.FullName==m.FullName&&x.GenericParameters.Count==m.GenericParameters.Count);if(x==null||Method(x)!=Method(m))failures.Add("method ABI "+m.FullName);}
    foreach(var p in t.Properties){var x=o.Properties.FirstOrDefault(x=>x.FullName==p.FullName);if(x==null||x.Attributes!=p.Attributes||Attr(x)!=Attr(p)||x.GetMethod?.FullName!=p.GetMethod?.FullName||x.SetMethod?.FullName!=p.SetMethod?.FullName)failures.Add("property ABI "+p.FullName);}
    foreach(var e in t.Events){var x=o.Events.FirstOrDefault(x=>x.FullName==e.FullName);if(x==null||x.Attributes!=e.Attributes||Attr(x)!=Attr(e)||x.AddMethod?.FullName!=e.AddMethod?.FullName||x.RemoveMethod?.FullName!=e.RemoveMethod?.FullName)failures.Add("event ABI "+e.FullName);}
   }
   if(!a.MainModule.ExportedTypes.Select(e=>e.FullName+":"+e.Attributes+":"+e.Scope).SequenceEqual(b.MainModule.ExportedTypes.Select(e=>e.FullName+":"+e.Attributes+":"+e.Scope)))failures.Add(name+" exported type table changed");
   results.Add(new{name,beforeTypes=old.Count,afterTypes=Types(b.MainModule).Count(),fields,methods,nativeLayouts=layouts,beforeBytes=new FileInfo(a.MainModule.FileName).Length,afterBytes=new FileInfo(b.MainModule.FileName).Length});
  }
  Write(output,new{status=failures.Count==0?"passed":"failed",results,failures});Console.WriteLine($"ABI {failures.Count} differences");if(failures.Count>0)Environment.ExitCode=1;
 }
}







