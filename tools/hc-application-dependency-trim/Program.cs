using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;

internal static partial class Program
{
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    static readonly List<AssemblyDefinition> Assemblies = [];
    static readonly List<object> Edges = [];
    static readonly List<string> AttributeDecodeErrors = [];
    static string[] ExtraReferenceDirectories = [];
    static readonly HashSet<string> EdgeKeys = [];
    static string Hash(string p) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
    static IEnumerable<TypeDefinition> Types(TypeDefinition t) { yield return t; foreach (var n in t.NestedTypes) foreach (var x in Types(n)) yield return x; }
    static IEnumerable<TypeDefinition> Types(ModuleDefinition m) => m.Types.SelectMany(Types);
    static string SemanticScope(TypeReference r) { var s=Scope(r); if(s is "System.Runtime" or "mscorlib" or "netstandard" or "Microsoft.Win32.Primitives" || s.StartsWith("System.",StringComparison.Ordinal)) { try { var d=r.Resolve(); if(d != null)return d.Module.Assembly.Name.Name; } catch(AssemblyResolutionException) {} } return s; }
    static string Scope(TypeReference r) => r.Scope is AssemblyNameReference a ? a.Name : r.Scope is ModuleDefinition m ? m.Assembly.Name.Name : r.Scope.Name;
    static void Write(string p, object v) { Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(p))!); File.WriteAllText(p, JsonSerializer.Serialize(v, Json), new UTF8Encoding(false)); }
    static void Add(TypeReference? type, string source, string owner, string kind, string target)
    {
        if (type == null) return;
        if (type is GenericInstanceType gi) foreach (var a in gi.GenericArguments) Add(a, source, owner, kind+"/generic", target);
        if (type is TypeSpecification ts) { Add(ts.ElementType, source, owner, kind, target); return; }
        if (type is GenericParameter) return;
        var scope = Scope(type); if (scope == source) return; if (scope is "mscorlib" or "netstandard" or "WindowsBase" or "PresentationCore" or "PresentationFramework" or "Microsoft.Windows.SDK.NET" or "WinRT.Runtime" || scope.StartsWith("System.",StringComparison.Ordinal) || scope.StartsWith("iNKORE.",StringComparison.Ordinal)) return;
        var key = source + "|" + owner + "|" + kind + "|" + scope + "|" + type.FullName + "|" + target;
        if (EdgeKeys.Add(key)) Edges.Add(new { source, owner, kind, targetAssembly=scope, targetType=type.FullName, target });
    }
    static AssemblyDefinition Read(string path) { var r=new DefaultAssemblyResolver(); r.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!); foreach(var directory in ExtraReferenceDirectories)r.AddSearchDirectory(directory); var core=System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory(); r.AddSearchDirectory(core); var shared=Directory.GetParent(core.TrimEnd(Path.DirectorySeparatorChar))!.Parent!.FullName; var desktop=Path.Combine(shared,"Microsoft.WindowsDesktop.App",new DirectoryInfo(core).Name); if(Directory.Exists(desktop))r.AddSearchDirectory(desktop); return AssemblyDefinition.ReadAssembly(path,new ReaderParameters{AssemblyResolver=r}); }
    static void Attributes(ICustomAttributeProvider p, string source, string owner)
    {
        foreach (var a in p.CustomAttributes) {
            Add(a.AttributeType,source,owner,"attribute",a.Constructor.FullName);
            try { foreach(var v in a.ConstructorArguments.Concat(a.Fields.Select(x=>x.Argument)).Concat(a.Properties.Select(x=>x.Argument))) if(v.Value is TypeReference r) Add(r,source,owner,"attribute-value",r.FullName); } catch(AssemblyResolutionException ex) { AttributeDecodeErrors.Add(source+"|"+owner+"|"+ex.Message); }
        }
    }
    static void Member(MemberReference mr, string src, string owner, string kind)
    {
        Add(mr.DeclaringType,src,owner,kind,mr.FullName);
        if(mr is MethodReference m) { Add(m.ReturnType,src,owner,kind+"/return",m.FullName);foreach(var p in m.Parameters)Add(p.ParameterType,src,owner,kind+"/param",m.FullName); if(m is GenericInstanceMethod gm)foreach(var a in gm.GenericArguments)Add(a,src,owner,kind+"/generic",m.FullName); }
        if(mr is FieldReference f)Add(f.FieldType,src,owner,kind+"/field",f.FullName);
    }
    static void Scan(string runtime,string output)
    {
        var inventory=new List<object>();
        foreach(var file in Directory.GetFiles(runtime,"*",SearchOption.AllDirectories).Order()) {
            AssemblyDefinition asm;
            try { asm=Read(file); }
            catch(BadImageFormatException){inventory.Add(new{path=Path.GetRelativePath(runtime,file),bytes=new FileInfo(file).Length,sha256=Hash(file),managed=false});continue;}
            Assemblies.Add(asm);var src=asm.Name.Name;var types=Types(asm.MainModule).ToArray();
            inventory.Add(new{path=Path.GetRelativePath(runtime,file),bytes=new FileInfo(file).Length,sha256=Hash(file),managed=true,assembly=src,typeCount=types.Length,methodCount=types.Sum(t=>t.Methods.Count),references=asm.MainModule.AssemblyReferences.Select(a=>a.FullName),resources=asm.MainModule.Resources.Select(r=>new{r.Name,kind=r.ResourceType.ToString()}),pinvokes=types.SelectMany(t=>t.Methods).Where(m=>m.HasPInvokeInfo).Select(m=>new{method=m.FullName,module=m.PInvokeInfo?.Module?.Name,entryPoint=m.PInvokeInfo?.EntryPoint,attributes=m.PInvokeInfo?.Attributes.ToString()})});
            Attributes(asm,src,"<assembly>");
            foreach(var t in types) {
                Add(t.BaseType,src,t.FullName,"base",t.BaseType?.FullName??""); Attributes(t,src,t.FullName);
                foreach(var i in t.Interfaces)Add(i.InterfaceType,src,t.FullName,"interface",i.InterfaceType.FullName);
                foreach(var f in t.Fields){Add(f.FieldType,src,f.FullName,"field",f.FieldType.FullName);Attributes(f,src,f.FullName);}
                foreach(var p in t.Properties)Add(p.PropertyType,src,p.FullName,"property",p.PropertyType.FullName);
                foreach(var e in t.Events)Add(e.EventType,src,e.FullName,"event",e.EventType.FullName);
                foreach(var m in t.Methods) {
                    Add(m.ReturnType,src,m.FullName,"return",m.ReturnType.FullName); Attributes(m,src,m.FullName);
                    foreach(var p in m.Parameters){Add(p.ParameterType,src,m.FullName,"parameter",p.ParameterType.FullName);Attributes(p,src,m.FullName);}
                    foreach(var ov in m.Overrides)Member(ov,src,m.FullName,"override");
                    if(!m.HasBody)continue;
                    foreach(var v in m.Body.Variables)Add(v.VariableType,src,m.FullName,"local",v.VariableType.FullName);
                    foreach(var h in m.Body.ExceptionHandlers)Add(h.CatchType,src,m.FullName,"catch",h.CatchType?.FullName??"");
                    foreach(var i in m.Body.Instructions) {
                        if(i.Operand is TypeReference tr)Add(tr,src,m.FullName,"IL:"+i.OpCode,tr.FullName);
                        else if(i.Operand is MemberReference mr)Member(mr,src,m.FullName,"IL:"+i.OpCode);
                    }
                }
            }
        }
        Write(output,new{schemaVersion=1,mode="bidirectional-metadata-and-IL",hardwareExecuted=false,resourceLimitation="Resource names inventoried; arbitrary BAML/JSON reflection is not proven by IL edges",files=inventory,edges=Edges,attributeDecodeErrors=AttributeDecodeErrors});
        Console.WriteLine($"SCANNED {inventory.Count} files; {Edges.Count} reverse edges");
    }
    static string Fmt(object? operand, MethodDefinition m)
    {
        return operand switch {
            null=>"",Instruction i=>"@"+m.Body.Instructions.IndexOf(i),Instruction[] a=>string.Join(",",a.Select(i=>"@"+m.Body.Instructions.IndexOf(i))),
            TypeReference t=>t.FullName+"["+SemanticScope(t)+"]", MemberReference mr=>mr.FullName+"["+SemanticScope(mr.DeclaringType)+"]",
            VariableDefinition v=>"V"+v.Index+":"+v.VariableType.FullName, ParameterDefinition p=>"P"+p.Index+":"+p.ParameterType.FullName,
            float v=>v.ToString("R",System.Globalization.CultureInfo.InvariantCulture),double v=>v.ToString("R",System.Globalization.CultureInfo.InvariantCulture),_=>operand.ToString()??""};
    }
    static string Canon(MethodDefinition m) => string.Join("\n",new[]{m.FullName,"arity:"+m.GenericParameters.Count,m.Attributes.ToString(),m.ImplAttributes.ToString(),m.HasPInvokeInfo?m.PInvokeInfo.Module.Name+":"+m.PInvokeInfo.EntryPoint+":"+m.PInvokeInfo.Attributes:"",m.HasBody?"init:"+m.Body.InitLocals:"no-body"}.Concat(!m.HasBody?[]:m.Body.Variables.Select(v=>"var:"+v.VariableType.FullName).Concat(m.Body.Instructions.Select(i=>i.OpCode+" "+Fmt(i.Operand,m))).Concat(m.Body.ExceptionHandlers.Select(h=>"eh:"+h.HandlerType+":"+h.CatchType?.FullName+":"+Fmt(h.TryStart,m)+":"+Fmt(h.TryEnd,m)+":"+Fmt(h.HandlerStart,m)+":"+Fmt(h.HandlerEnd,m)+":"+Fmt(h.FilterStart,m)))));
    static bool Device(TypeDefinition t) => t.FullName.StartsWith("HandheldCompanion.Devices.",StringComparison.Ordinal);
    static void Roots(string runtime,string repo,string output)
    {
        using var hc=Read(Path.Combine(runtime,"HandheldCompanion.dll"));
        using var shared=Read(Path.Combine(runtime,"Shared.dll"));
        var types=Types(hc.MainModule).ToArray();var names=types.Where(Device).Select(t=>t.FullName).ToHashSet();
        // FanHost and LightSetter use reflection. Preserve every statically named HC type and all its members conservatively.
        foreach(var path in new[]{"FanLab/real-host","FanLab/LightSetter"}) foreach(var file in Directory.GetFiles(Path.Combine(repo,path),"*.cs",SearchOption.AllDirectories)) {
            if(file.Contains(Path.DirectorySeparatorChar+"obj"+Path.DirectorySeparatorChar)||file.Contains(Path.DirectorySeparatorChar+"bin"+Path.DirectorySeparatorChar))continue;
            foreach(Match match in Regex.Matches(File.ReadAllText(file),"HandheldCompanion\\.[A-Za-z0-9_.+`]+")){var name=match.Value.Replace('+','/');if(types.Any(t=>t.FullName==name))names.Add(name);}
        }
        foreach(var name in new[]{"HandheldCompanion.Managers.ManagerFactory","HandheldCompanion.MotherboardInfo","HandheldCompanion.Misc.FanProfile","HandheldCompanion.Misc.PowerProfile","HandheldCompanion.UpdateSource"})if(types.Any(t=>t.FullName==name))names.Add(name);
        // Reflection reads factory fields then invokes instance methods by string; preserve those manager APIs.
        var factory=types.FirstOrDefault(t=>t.FullName=="HandheldCompanion.Managers.ManagerFactory"); if(factory!=null)foreach(var field in factory.Fields){if(types.Any(t=>t.FullName==field.FieldType.FullName))names.Add(field.FieldType.FullName);}
        // App is only used for four path fields. Do not root unused window/startup methods merely because it holds paths.
        names.Remove("HandheldCompanion.App");
        var root=new XElement("linker",new XElement("assembly",new XAttribute("fullname",hc.Name.Name),names.Order().Select(n=>new XElement("type",new XAttribute("fullname",n),new XAttribute("preserve","all"))),new XElement("type",new XAttribute("fullname","HandheldCompanion.App"),new XAttribute("preserve","fields"))),new XElement("assembly",new XAttribute("fullname",shared.Name.Name),Types(shared.MainModule).Where(t=>t.FullName=="HandheldCompanion.Shared.LogManager").Select(t=>new XElement("type",new XAttribute("fullname",t.FullName),new XAttribute("preserve","all")))));
        using var comInventory = InspectCom(runtime);
        foreach (var assembly in ComRoots(comInventory).Elements()) { var existing = root.Elements("assembly").FirstOrDefault(e => (string?)e.Attribute("fullname") == (string?)assembly.Attribute("fullname")); if (existing != null) existing.ReplaceWith(assembly); else root.Add(assembly); }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);new XDocument(root).Save(output);
        Write(output+".json",new{status="conservative-software-roots",deviceTypeCount=types.Count(Device),rootTypeCount=names.Count,names=names.Order(),appPathFieldsOnly=!comInventory.Protected.ContainsKey(hc.Name.Name),comAbiWholeAssemblyCopies=comInventory.Protected.Keys.Order(StringComparer.Ordinal).ToArray(),managerFactoryAllMembers=true,sharedLogManagerAllMembers=true,reflectionCompletenessProven=false});
        Console.WriteLine($"ROOTS {names.Count}; device types {types.Count(Device)}");
    }
    static void Verify(string original,string candidate,string output,string? rootsJson=null)
    {
        using var a=Read(original); using var b=Read(candidate);
        var rootNames=new HashSet<string>();if(rootsJson!=null){using var r=JsonDocument.Parse(File.ReadAllText(rootsJson));rootNames=r.RootElement.GetProperty("names").EnumerateArray().Select(x=>x.GetString()!).ToHashSet();}var originalTypes=Types(a.MainModule).Where(t=>Device(t)||rootNames.Contains(t.FullName)).ToArray();var ct=Types(b.MainModule).ToDictionary(t=>t.FullName);var failures=new List<string>();int methods=0,fields=0;
        foreach(var t in originalTypes) {
            if(!ct.TryGetValue(t.FullName,out var c)){failures.Add("missing type: "+t.FullName);continue;}
            if(t.Attributes!=c.Attributes||t.BaseType?.FullName!=c.BaseType?.FullName)failures.Add("type attrs/base: "+t.FullName);
            foreach(var f in t.Fields){fields++;var cf=c.Fields.FirstOrDefault(x=>x.FullName==f.FullName);if(cf==null||cf.Attributes!=f.Attributes||!Equals(cf.Constant,f.Constant)||!cf.InitialValue.SequenceEqual(f.InitialValue))failures.Add("field: "+f.FullName);}
            foreach(var m in t.Methods){methods++;var cm=c.Methods.FirstOrDefault(x=>x.FullName==m.FullName&&x.GenericParameters.Count==m.GenericParameters.Count);if(cm==null)failures.Add("missing method: "+m.FullName);else if(Canon(m)!=Canon(cm))failures.Add("changed method IL: "+m.FullName);}
        }
        var comAbi = CompareCom(Path.GetDirectoryName(Path.GetFullPath(original))!, Path.GetDirectoryName(Path.GetFullPath(candidate))!);
        failures.AddRange(comAbi.Failures.Select(f => "COM ABI: " + f));
        Write(output,new{status=failures.Count==0?"passed":"failed",comAbi,comparison="all original Devices types including nested callbacks; method attributes, PInvoke, instruction operands, branches, locals, EH, fields and constants",deviceTypes=originalTypes.Count(Device),protectedTypes=originalTypes.Length,additionalReflectionRoots=rootsJson,methods,fields,originalSha256=Hash(original),candidateSha256=Hash(candidate),originalBytes=new FileInfo(original).Length,candidateBytes=new FileInfo(candidate).Length,failures,physicalDevicesVerified=false});
        Console.WriteLine($"DEVICE IL {methods} methods; {failures.Count} differences");if(failures.Count>0)Environment.ExitCode=1;
    }
    static void Dump(string path,string pattern,string output)
    {
        using var a=Read(path);var rows=Types(a.MainModule).SelectMany(t=>t.Methods).Where(m=>Regex.IsMatch(m.FullName,pattern)).Select(m=>new{m.FullName,il=Canon(m)}).ToArray();Write(output,rows);Console.WriteLine($"DUMPED {rows.Length} methods");
    }

    static void Contracts(string runtime,string targetsFile,string output,string? sharedRoot=null)
    {
        var selected=JsonSerializer.Deserialize<string[]>(File.ReadAllText(targetsFile))!.ToHashSet(StringComparer.OrdinalIgnoreCase);
        ExtraReferenceDirectories=sharedRoot==null?[runtime]:[runtime,sharedRoot];
        if(sharedRoot!=null){using var pin=JsonDocument.Parse(File.ReadAllText(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(sharedRoot))!,"manifest.json")));foreach(var file in pin.RootElement.GetProperty("files").EnumerateArray()){var p=Path.Combine(sharedRoot,Path.GetFileName(file.GetProperty("path").GetString()!));if(Hash(p)!=file.GetProperty("sha256").GetString())throw new InvalidDataException("Shared file pin mismatch: "+p);}}
        var missing=new List<object>();int typesChecked=0,membersChecked=0,exportsChecked=0;
        foreach(var file in Directory.GetFiles(runtime,"*.dll",SearchOption.AllDirectories).Concat(sharedRoot==null?[]:Directory.GetFiles(sharedRoot,"*.dll",SearchOption.AllDirectories)).Order()) {
            AssemblyDefinition asm;try{asm=Read(file);}catch(BadImageFormatException){continue;}
            using(asm) {
                foreach(var t in asm.MainModule.GetTypeReferences()) {
                    if(!selected.Contains(Scope(t)))continue;typesChecked++;
                    try{if(t.Resolve()==null)missing.Add(new{source=asm.Name.Name,kind="type",target=t.FullName,scope=Scope(t),error="not resolved"});}
                    catch(Exception e)when(e is AssemblyResolutionException or ResolutionException or ArgumentException){missing.Add(new{source=asm.Name.Name,kind="type",target=t.FullName,scope=Scope(t),error=e.Message});}
                }
                foreach(var m in asm.MainModule.GetMemberReferences()) {
                    if(!selected.Contains(Scope(m.DeclaringType)))continue;membersChecked++;
                    try{IMemberDefinition? d=m switch{MethodReference mr=>mr.Resolve(),FieldReference fr=>fr.Resolve(),_=>null};if(d==null)missing.Add(new{source=asm.Name.Name,kind="member",target=m.FullName,scope=Scope(m.DeclaringType),error="not resolved"});}
                    catch(Exception e)when(e is AssemblyResolutionException or ResolutionException or ArgumentException){missing.Add(new{source=asm.Name.Name,kind="member",target=m.FullName,scope=Scope(m.DeclaringType),error=e.Message});}
                }
                foreach(var e in asm.MainModule.ExportedTypes) {
                    if(e.Scope is not AssemblyNameReference ar||!selected.Contains(ar.Name))continue;exportsChecked++;
                    try{if(e.Resolve()==null)missing.Add(new{source=asm.Name.Name,kind="export",target=e.FullName,scope=ar.Name,error="not resolved"});}
                    catch(Exception ex)when(ex is AssemblyResolutionException or ResolutionException or ArgumentException){missing.Add(new{source=asm.Name.Name,kind="export",target=e.FullName,scope=ar.Name,error=ex.Message});}
                }
            }
        }
        Write(output,new{status=missing.Count==0?"passed":"failed",typesChecked,membersChecked,exportsChecked,missing,sharedAuthority=sharedRoot,limit="Static metadata references only; reflection strings and resources require separate coverage",hardwareAccess=false});
        Console.WriteLine($"CONTRACTS {typesChecked} types; {membersChecked} members; {exportsChecked} exports; {missing.Count} missing");if(missing.Count>0)Environment.ExitCode=1;
    }
    static void ReflectionStrings(string path,string pattern,string output)
    {
        using var a=Read(path);var rows=Types(a.MainModule).SelectMany(t=>t.Methods).Where(m=>m.HasBody).SelectMany(m=>m.Body.Instructions.Where(i=>i.OpCode==OpCodes.Ldstr&&Regex.IsMatch((string)i.Operand,pattern,RegexOptions.IgnoreCase)).Select(i=>new{method=m.FullName,value=(string)i.Operand,offset=i.Offset})).ToArray();Write(output,rows);Console.WriteLine($"STRINGS {rows.Length}");
    }


    static void VerifyLeaves(string originalRoot,string candidateRoot,string targetsFile,string output)
    {
        var names=JsonSerializer.Deserialize<string[]>(File.ReadAllText(targetsFile))!;var results=new List<object>();int failures=0;
        foreach(var name in names) {
            string before=Path.Combine(originalRoot,name+".dll"),after=Path.Combine(candidateRoot,name+".dll");
            if(!File.Exists(after)){results.Add(new{name,status="absent-candidate",beforeBytes=new FileInfo(before).Length});continue;}
            using var a=Read(before);using var b=Read(after);var at=Types(a.MainModule).ToDictionary(t=>t.FullName);var differences=new List<string>();int methods=0,layouts=0;
            if(a.Name.FullName!=b.Name.FullName)differences.Add("assembly identity changed");
            foreach(var t in Types(b.MainModule)) {
                if(!at.TryGetValue(t.FullName,out var old)){differences.Add("new type "+t.FullName);continue;}
                if(t.BaseType?.FullName!=old.BaseType?.FullName)differences.Add("base type "+t.FullName);
                if(t.IsSequentialLayout||t.IsExplicitLayout){layouts++;var of=old.Fields.Where(f=>!f.IsStatic).Select(f=>f.FullName+":"+f.Offset+":"+f.Attributes+":"+f.HasMarshalInfo).ToArray();var nf=t.Fields.Where(f=>!f.IsStatic).Select(f=>f.FullName+":"+f.Offset+":"+f.Attributes+":"+f.HasMarshalInfo).ToArray();if(!of.SequenceEqual(nf)||t.ClassSize!=old.ClassSize||t.PackingSize!=old.PackingSize)differences.Add("native layout "+t.FullName);}
                foreach(var m in t.Methods){methods++;var om=old.Methods.FirstOrDefault(x=>x.FullName==m.FullName&&x.GenericParameters.Count==m.GenericParameters.Count);if(om==null)differences.Add("new method "+m.FullName);else if(Canon(om)!=Canon(m))differences.Add("method IL "+m.FullName);}
            }
            failures+=differences.Count;results.Add(new{name,status=differences.Count==0?"passed":"failed",methods,nativeLayouts=layouts,beforeBytes=new FileInfo(before).Length,afterBytes=new FileInfo(after).Length,beforeSha256=Hash(before),afterSha256=Hash(after),differences});
        }
        var comAbi = CompareCom(originalRoot, candidateRoot); failures += comAbi.Failures.Length;
        Write(output,new{status=failures==0?"passed":"failed",comAbi,normalization="Cecil resolves framework forwarded types to their actual definition assembly; all opcodes, operands, EH, locals and method flags otherwise exact",results,failures,hardwareAccess=false});Console.WriteLine($"LEAF IL {names.Length} libraries; {failures} differences");if(failures>0)Environment.ExitCode=1;
    }


    static string AttributeValue(CustomAttributeArgument a)=>a.Value switch {TypeReference tr=>tr.FullName+"["+SemanticScope(tr)+"]",CustomAttributeArgument[] array=>"["+string.Join(",",array.Select(AttributeValue))+"]",_=>a.Value?.ToString()??"null"};
    static string Attrs(ICustomAttributeProvider item)=>string.Join(";",item.CustomAttributes.Select(a=>a.AttributeType.FullName+":"+a.Constructor.FullName+":"+string.Join(",",a.ConstructorArguments.Select(AttributeValue))+":"+string.Join(",",a.Fields.Select(f=>f.Name+"="+AttributeValue(f.Argument)))+":"+string.Join(",",a.Properties.Select(f=>f.Name+"="+AttributeValue(f.Argument)))).Order());
    static void VerifyReflectionSurface(string originalRoot,string candidateRoot,string targetsFile,string output)
    {
        var names=JsonSerializer.Deserialize<string[]>(File.ReadAllText(targetsFile))!.Where(n=>Regex.IsMatch(n,"^(Sentry$|Serilog|protobuf-net|IGDB$|craftersmine\\.|RestEase$|Microsoft\\.Win32\\.TaskScheduler$|ricaun\\.|Microsoft\\.Extensions\\.(Configuration|Options|DependencyModel|Logging$))")).ToArray();
        var results=new List<object>();int total=0;
        foreach(string name in names) {
            using var a=Read(Path.Combine(originalRoot,name+".dll"));using var b=Read(Path.Combine(candidateRoot,name+".dll"));var bt=Types(b.MainModule).ToDictionary(t=>t.FullName);var diff=new List<string>();int methods=0,fields=0,properties=0,events=0;
            foreach(var t in Types(a.MainModule)) {
                if(!bt.TryGetValue(t.FullName,out var c)){diff.Add("missing type "+t.FullName);continue;}
                var oldFlags=t.Attributes;var newFlags=c.Attributes;if(!t.Methods.Any(m=>m.IsConstructor&&m.IsStatic)&&!c.Methods.Any(m=>m.IsConstructor&&m.IsStatic)){oldFlags&=~TypeAttributes.BeforeFieldInit;newFlags&=~TypeAttributes.BeforeFieldInit;}if(oldFlags!=newFlags||Attrs(t)!=Attrs(c))diff.Add("type attributes "+t.FullName);
                foreach(var f in t.Fields){fields++;var cf=c.Fields.FirstOrDefault(x=>x.FullName==f.FullName);if(cf==null||cf.Attributes!=f.Attributes||!Equals(cf.Constant,f.Constant)||Attrs(f)!=Attrs(cf))diff.Add("field "+f.FullName);}
                foreach(var m in t.Methods){methods++;var cm=c.Methods.FirstOrDefault(x=>x.FullName==m.FullName&&x.GenericParameters.Count==m.GenericParameters.Count);if(cm==null||Canon(cm)!=Canon(m)||Attrs(cm)!=Attrs(m))diff.Add("method "+m.FullName+" arity "+m.GenericParameters.Count);}
                foreach(var prop in t.Properties){properties++;var cp=c.Properties.FirstOrDefault(x=>x.FullName==prop.FullName);if(cp==null||cp.Attributes!=prop.Attributes||Attrs(cp)!=Attrs(prop))diff.Add("property "+prop.FullName);}
                foreach(var e in t.Events){events++;var ce=c.Events.FirstOrDefault(x=>x.FullName==e.FullName);if(ce==null||ce.Attributes!=e.Attributes||Attrs(ce)!=Attrs(e))diff.Add("event "+e.FullName);}
            }
            total+=diff.Count;results.Add(new{name,status=diff.Count==0?"passed":"failed",types=Types(a.MainModule).Count(),methods,fields,properties,events,differences=diff});
        }
        Write(output,new{status=total==0?"passed":"failed",mode="complete reflection families; all original types, methods, fields, properties, events and decoded attribute values",normalizations="Attribute Type values resolve framework forwarders; BeforeFieldInit flag ignored only if both types have no static constructor",results,totalDifferences=total,physicalDeviceVerified=false});Console.WriteLine($"REFLECTION SURFACE {names.Length} libraries; {total} differences");if(total>0)Environment.ExitCode=1;
    }


    // A managed call graph cannot prove that native COM vtable slots or callbacks are unused.
    // Keep ABI owners and callback implementations byte-for-byte. Dependencies are reported, not recursively protected.
    sealed class ComInventory : IDisposable
    {
        public Dictionary<string, AssemblyDefinition> Loaded { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, string> Paths { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, HashSet<string>> Protected { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, TypeDefinition> BoundaryTypes { get; } = new(StringComparer.Ordinal);
        public List<string> UnavailableDependencies { get; } = [];
        public void Protect(string assembly, string reason)
        {
            if (!Protected.TryGetValue(assembly, out var reasons)) Protected[assembly] = reasons = new(StringComparer.Ordinal);
            reasons.Add(reason);
        }
        public void Dispose() { foreach (var a in Loaded.Values) a.Dispose(); }
    }
    static bool HasAttribute(ICustomAttributeProvider p, string name) => p.CustomAttributes.Any(a => a.AttributeType.FullName == name);
    static bool ExplicitComVisible(ICustomAttributeProvider p) => p.CustomAttributes.Any(a => a.AttributeType.FullName == "System.Runtime.InteropServices.ComVisibleAttribute" && a.ConstructorArguments.Count == 1 && a.ConstructorArguments[0].Value is true);
    static string ComKey(TypeReference t)
    {
        while (t is TypeSpecification s && s.ElementType != null) t = s.ElementType;
        return Scope(t) + "|" + t.FullName;
    }
    static IEnumerable<string> ComReasons(TypeDefinition t)
    {
        if (t.IsImport || HasAttribute(t, "System.Runtime.InteropServices.ComImportAttribute")) yield return "ComImport";
        if (HasAttribute(t, "System.Runtime.InteropServices.GuidAttribute") && HasAttribute(t, "System.Runtime.InteropServices.InterfaceTypeAttribute")) yield return "Guid+InterfaceType";
        if (ExplicitComVisible(t)) yield return "ComVisible(true)";
        foreach (var a in t.CustomAttributes)
            if (a.AttributeType.FullName is "System.Runtime.InteropServices.ClassInterfaceAttribute" or "System.Runtime.InteropServices.ComSourceInterfacesAttribute" or "System.Runtime.InteropServices.UnmanagedFunctionPointerAttribute" or "System.Runtime.InteropServices.Marshalling.GeneratedComInterfaceAttribute" or "System.Runtime.InteropServices.Marshalling.GeneratedComClassAttribute") yield return a.AttributeType.Name;
        if (t.Methods.Any(m => m.HasPInvokeInfo || HasAttribute(m, "System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute"))) yield return "native interop/callback boundary";
    }
    static ComInventory InspectCom(string input)
    {
        var result = new ComInventory();
        try {
            var files = File.Exists(input) ? new[] { Path.GetFullPath(input) } : Directory.GetFiles(input, "*.dll", SearchOption.AllDirectories).Order(StringComparer.Ordinal).ToArray();
            foreach (var file in files) {
                AssemblyDefinition a;
                try { a = Read(file); } catch (BadImageFormatException) { continue; }
                if (result.Loaded.ContainsKey(a.Name.Name)) { a.Dispose(); throw new InvalidDataException("Ambiguous assembly input: " + file); }
                result.Loaded.Add(a.Name.Name, a); result.Paths.Add(a.Name.Name, Path.GetFullPath(file));
            }
            foreach (var a in result.Loaded.Values) {
                if (ExplicitComVisible(a)) result.Protect(a.Name.Name, "assembly ComVisible(true)");
                foreach (var t in Types(a.MainModule)) foreach (var reason in ComReasons(t)) {
                    result.BoundaryTypes.TryAdd(ComKey(t), t); result.Protect(a.Name.Name, reason + ": " + t.FullName);
                }
            }
            // Unannotated delegates in a native signature are callbacks too. Resolve only inside
            // the supplied inventory; external types remain explicit unresolved requirements.
            void Callback(TypeReference reference, string owner)
            {
                if (reference is GenericInstanceType generic) foreach (var arg in generic.GenericArguments) Callback(arg, owner);
                if (reference is FunctionPointerType fp) { Callback(fp.ReturnType, owner); foreach (var p in fp.Parameters) Callback(p.ParameterType, owner); return; }
                while (reference is TypeSpecification spec && spec.ElementType != null) reference = spec.ElementType;
                var scope = Scope(reference);
                if (!result.Loaded.TryGetValue(scope, out var assembly)) return;
                var type = Types(assembly.MainModule).FirstOrDefault(t => t.FullName == reference.FullName);
                if (type?.BaseType?.FullName != "System.MulticastDelegate") return;
                result.BoundaryTypes.TryAdd(ComKey(type), type);
                result.Protect(assembly.Name.Name, "native signature callback: " + type.FullName + " <- " + owner);
            }
            foreach (var assembly in result.Loaded.Values) foreach (var type in Types(assembly.MainModule)) {
                foreach (var method in type.Methods.Where(m => m.HasPInvokeInfo || ComReasons(type).Any())) {
                    Callback(method.ReturnType, method.FullName);
                    foreach (var parameter in method.Parameters) Callback(parameter.ParameterType, method.FullName);
                }
                foreach (var field in type.Fields.Where(f => f.HasMarshalInfo || type.IsExplicitLayout || type.IsSequentialLayout)) Callback(field.FieldType, field.FullName);
            }
            // Unmarked derived interfaces and implementations in other assemblies are native ABI too.
            bool changed;
            do {
                changed = false;
                // An attributed child may inherit an unmarked contract in another supplied DLL.
                foreach (var boundary in result.BoundaryTypes.Values.ToArray()) {
                    foreach (var reference in boundary.Interfaces.Select(i => i.InterfaceType).Concat(boundary.BaseType == null ? [] : new[] { boundary.BaseType })) {
                        if (!result.Loaded.TryGetValue(Scope(reference), out var owner)) continue;
                        var parent = Types(owner.MainModule).FirstOrDefault(t => t.FullName == reference.FullName);
                        if (parent == null || result.BoundaryTypes.ContainsKey(ComKey(parent))) continue;
                        result.BoundaryTypes.Add(ComKey(parent), parent);
                        result.Protect(owner.Name.Name, "ABI inherited base contract: " + parent.FullName + " <- " + boundary.FullName); changed = true;
                    }
                }
                foreach (var a in result.Loaded.Values) foreach (var t in Types(a.MainModule)) {
                    if (result.BoundaryTypes.ContainsKey(ComKey(t))) continue;
                    var bases = t.Interfaces.Select(i => i.InterfaceType).Concat(t.BaseType == null ? [] : new[] { t.BaseType });
                    var boundary = bases.FirstOrDefault(b => result.BoundaryTypes.ContainsKey(ComKey(b)));
                    if (boundary == null) continue;
                    result.BoundaryTypes.Add(ComKey(t), t); result.Protect(a.Name.Name, "COM interface/base implementation: " + t.FullName + " -> " + ComKey(boundary)); changed = true;
                }
            } while (changed);
            foreach (var name in result.Protected.Keys) foreach (var dependency in result.Loaded[name].MainModule.AssemblyReferences)
                if (!result.Loaded.ContainsKey(dependency.Name)) result.UnavailableDependencies.Add(name + " -> " + dependency.FullName);
            return result;
        } catch { result.Dispose(); throw; }
    }
    static XElement ComRoots(ComInventory inventory) => new("linker", inventory.Protected.Keys.Order(StringComparer.Ordinal).Select(n => new XElement("assembly", new XAttribute("fullname", n), new XElement("type", new XAttribute("fullname", "*"), new XAttribute("preserve", "all")))));
    static void AuditCom(string input, string output, string? descriptor)
    {
        using var inventory = InspectCom(input);
        var rows = inventory.Protected.OrderBy(p => p.Key, StringComparer.Ordinal).Select(p => new {
            name = p.Key, path = inventory.Paths[p.Key], sha256 = Hash(inventory.Paths[p.Key]), bytes = new FileInfo(inventory.Paths[p.Key]).Length,
            reasons = p.Value.Order(StringComparer.Ordinal).ToArray(), requiredAction = "preserve-reviewed-boundary-types"
        }).ToArray();
        if (descriptor != null) { Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(descriptor))!); new XDocument(ComRoots(inventory)).Save(descriptor); }
        Write(output, new { status = "passed", policy = "preserve actual COM/interop boundary types and callback contracts; audited non-ABI trimming is allowed; conservative linker roots do not require upstream-identical output", input = Path.GetFullPath(input), managedAssemblies = inventory.Loaded.Count, protectedAssemblies = rows, boundaryTypes = inventory.BoundaryTypes.Keys.Order(StringComparer.Ordinal).ToArray(), protectionScope = "Only identified ABI owner/callback/implementation assemblies; unrelated referenced libraries are not implicitly protected", availableDependencies = inventory.Protected.Keys.SelectMany(n => inventory.Loaded[n].MainModule.AssemblyReferences.Where(d => inventory.Loaded.ContainsKey(d.Name)).Select(d => new { owner = n, dependency = d.FullName, independentlyProtected = inventory.Protected.ContainsKey(d.Name) })).ToArray(), unavailableDependencies = inventory.UnavailableDependencies.Order(StringComparer.Ordinal).ToArray(), dependencyBoundary = "Only supplied assemblies are inspected; external dependencies still require normal resolver/package gates. Input must be the approved full original, not a previously trimmed baseline.", targetCodeExecuted = false, hardwareAccess = false });
        Console.WriteLine($"COM AUDIT {rows.Length}/{inventory.Loaded.Count} assemblies contain protected ABI contracts");
    }
    static string ComMarshal(MarshalInfo? m) => m switch {
        null => "", ArrayMarshalInfo a => $"{a.NativeType}:{a.ElementType}:{a.Size}:{a.SizeParameterIndex}:{a.SizeParameterMultiplier}",
        FixedArrayMarshalInfo a => $"{a.NativeType}:{a.ElementType}:{a.Size}", SafeArrayMarshalInfo a => $"{a.NativeType}:{a.ElementType}",
        FixedSysStringMarshalInfo a => $"{a.NativeType}:{a.Size}", CustomMarshalInfo a => $"{a.NativeType}:{a.Guid}:{a.UnmanagedType}:{a.ManagedType?.FullName}:{a.Cookie}", _ => m.NativeType.ToString()
    };
    static string ComMethod(MethodDefinition m) => string.Join("|", m.FullName, m.Attributes, m.ImplAttributes, m.CallingConvention, Attrs(m), Attrs(m.MethodReturnType), ComMarshal(m.MethodReturnType.MarshalInfo), m.PInvokeInfo is { } import ? $"{import.Module?.Name ?? "<missing-module>"}:{import.EntryPoint}:{import.Attributes}" : (m.IsPInvokeImpl ? "<missing-implmap>" : ""),
        string.Join(";", m.Parameters.Select(p => p.Name + ":" + p.ParameterType.FullName + "[" + SemanticScope(p.ParameterType) + "]:" + p.Attributes + ":" + p.HasConstant + ":" + p.Constant + ":" + ComMarshal(p.MarshalInfo) + ":" + Attrs(p))),
        string.Join(";", m.GenericParameters.Select(p => p.Position + ":" + p.Attributes + ":" + Attrs(p) + ":" + string.Join(",", p.Constraints.Select(c => c.ConstraintType.FullName)))),
        string.Join(";", m.Overrides.Select(o => o.FullName)), m.HasBody ? Canon(m) : "no-body");
    static string ComField(FieldDefinition field) => string.Join("|", field.Name, field.FieldType.FullName, SemanticScope(field.FieldType),
        field.Attributes, field.Offset, field.HasConstant, field.Constant, ComMarshal(field.MarshalInfo), Attrs(field),
        Convert.ToHexString(field.InitialValue));
    sealed record ComComparison(string Status, int ProtectedAssemblies, object[] Results, string[] Failures);
    static ComComparison CompareCom(string original, string candidate)
    {
        using var before = InspectCom(original); using var after = InspectCom(candidate);
        var failures = new List<string>(); var rows = new List<object>();
        foreach (var name in before.Protected.Keys.Order(StringComparer.Ordinal)) {
            var differences = new List<string>(); var old = before.Loaded[name]; var originalHash = Hash(before.Paths[name]);
            string? candidateHash = null;
            if (!after.Loaded.TryGetValue(name, out var current)) differences.Add("missing protected assembly");
            else {
                candidateHash = Hash(after.Paths[name]);
                if (old.Name.FullName != current.Name.FullName) differences.Add("assembly identity changed");
                // Different bytes can be a valid derivative. Verify the actual ABI below.
                var currentTypes = Types(current.MainModule).ToDictionary(t => t.FullName);
                foreach (var t in Types(old.MainModule).Where(t => before.BoundaryTypes.ContainsKey(ComKey(t)))) {
                    if (!currentTypes.TryGetValue(t.FullName, out var c)) { differences.Add("missing COM/callback type: " + t.FullName); continue; }
                    if (t.Attributes != c.Attributes || t.BaseType?.FullName != c.BaseType?.FullName || Attrs(t) != Attrs(c)) differences.Add("COM/callback type flags, attributes or base changed: " + t.FullName);
                    if (t.PackingSize != c.PackingSize || t.ClassSize != c.ClassSize) differences.Add("COM/callback native packing or class size changed: " + t.FullName);
                    if (!t.Fields.Select(ComField).SequenceEqual(c.Fields.Select(ComField))) differences.Add("COM/callback ordered fields, layout or marshaling changed: " + t.FullName);
                    if (!t.Interfaces.Select(i => ComKey(i.InterfaceType) + ":" + Attrs(i)).SequenceEqual(c.Interfaces.Select(i => ComKey(i.InterfaceType) + ":" + Attrs(i)))) differences.Add("COM inheritance changed: " + t.FullName);
                    if (!t.Methods.Select(ComMethod).SequenceEqual(c.Methods.Select(ComMethod))) differences.Add("COM/callback ordered methods, signatures, marshaling or IL changed: " + t.FullName);
                }
            }
            failures.AddRange(differences.Select(d => name + ": " + d));
            rows.Add(new { name, originalPath = before.Paths[name], originalSha256 = originalHash, candidateSha256 = candidateHash, differences });
        }
        return new(failures.Count == 0 ? "passed" : "failed", before.Protected.Count, rows.ToArray(), failures.ToArray());
    }
    static void VerifyCom(string original, string candidate, string output)
    {
        var comparison = CompareCom(original, candidate);
        Write(output, new { status = comparison.Status, protectedAssemblies = comparison.ProtectedAssemblies, results = comparison.Results, failures = comparison.Failures, policy = "actual ABI compatibility: ordered COM inheritance/methods/fields, packing/layout, attributes, PreserveSig/marshalling and native callback implementation IL; unrelated byte changes alone do not reject derivatives", constructorsExecuted = false, targetCodeExecuted = false, hardwareAccess = false });
        Console.WriteLine($"COM VERIFY {comparison.ProtectedAssemblies} protected assemblies; {comparison.Failures.Length} differences");
        if (comparison.Failures.Length != 0) Environment.ExitCode = 1;
    }

    static void Main(string[] args)
    {
        if (args.Length == 0) throw new ArgumentException("scan, roots, verify, leafverify, contracts, surface, dump, strings, auditcom, verifycom");
        switch(args[0]){case "selftestcom":SelfTestCom(args[1]);break;case "auditcom":AuditCom(args[1],args[2],args.Length>3?args[3]:null);break;case "verifycom":VerifyCom(args[1],args[2],args[3]);break;case "surface":VerifyReflectionSurface(args[1],args[2],args[3],args[4]);break;case "leafverify":VerifyLeaves(args[1],args[2],args[3],args[4]);break;case "contracts":Contracts(args[1],args[2],args[3],args.Length>4?args[4]:null);break;case "strings":ReflectionStrings(args[1],args[2],args[3]);break;case "scan":Scan(args[1],args[2]);break;case "roots":Roots(args[1],args[2],args[3]);break;case "verify":Verify(args[1],args[2],args[3],args.Length>4?args[4]:null);break;case "dump":Dump(args[1],args[2],args[3]);break;default:throw new ArgumentException("scan, roots, verify, dump");}
    }
}





