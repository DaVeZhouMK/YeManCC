using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Collections;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;

// Metadata only. No Assembly.Load, target invocation, resolver or type initializer.
internal static class Audit
{
    private const string Removed = "YeManInputHost.HcVerbatim";
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    private static IEnumerable<TypeDefinition> Types(TypeDefinition t) { yield return t; foreach(var n in t.NestedTypes) foreach(var c in Types(n)) yield return c; }
    private static IEnumerable<TypeDefinition> Types(ModuleDefinition m) => m.Types.SelectMany(Types);
    private static bool IsRemoved(TypeDefinition t) => t.FullName.StartsWith(Removed + ".", StringComparison.Ordinal);
    private static string Attr(Mono.Cecil.ICustomAttributeProvider a) => string.Join(";",a.CustomAttributes.Select(x=>x.AttributeType.FullName+":"+Convert.ToHexString(x.GetBlob())).Order());
    private static string Val(object? o) => o switch { null=>"null", byte[] b=>Convert.ToHexString(b), float f=>"f:"+BitConverter.SingleToInt32Bits(f),double d=>"d:"+BitConverter.DoubleToInt64Bits(d),IFormattable f=>f.ToString(null,CultureInfo.InvariantCulture)??"", _=>o.ToString()??"" };
    private static string GP(IGenericParameterProvider p) => string.Join(";",p.GenericParameters.Select(x=>x.Name+":"+x.Attributes+":"+Attr(x)+":"+string.Join(",",x.Constraints.Select(c=>c.ConstraintType.FullName+":"+Attr(c)))));
    private static string Param(ParameterDefinition p) => p.Name+":"+p.Attributes+":"+p.ParameterType.FullName+":"+Val(p.Constant)+":"+Attr(p)+":"+(p.HasMarshalInfo?p.MarshalInfo.NativeType.ToString():"");
    private static string TypeSurface(TypeDefinition t) => string.Join("|",t.Attributes,t.BaseType?.FullName,t.ClassSize,t.PackingSize,GP(t),Attr(t),string.Join(";",t.Interfaces.Select(x=>x.InterfaceType.FullName+":"+Attr(x))));
    private static string InstructionValue(Instruction i,Dictionary<Instruction,int> indexes)
    {
        object? o=i.Operand;
        return i.OpCode.Code+":"+(o switch {
            Instruction b=>"branch:"+indexes[b],Instruction[] b=>"switch:"+string.Join(",",b.Select(x=>indexes[x])),
            MethodReference m=>"method:"+m.FullName,FieldReference f=>"field:"+f.FullName,TypeReference t=>"type:"+t.FullName,
            VariableDefinition v=>"local:"+v.Index,ParameterDefinition p=>"parameter:"+p.Index,
            CallSite c=>"callsite:"+c.FullName,string s=>"string:"+JsonSerializer.Serialize(s),_=>Val(o)});
    }
    private static string Body(MethodDefinition m)
    {
        if(!m.HasBody)return "no-body";
        var b=m.Body;var indexes=b.Instructions.Select((x,i)=>(x,i)).ToDictionary(x=>x.x,x=>x.i);
        string At(Instruction? i)=>i==null?"end":indexes[i].ToString(CultureInfo.InvariantCulture);
        return string.Join("\n",new[]{"locals:"+b.InitLocals+":"+b.MaxStackSize+":"+string.Join(";",b.Variables.Select(x=>x.VariableType.FullName)),
          "handlers:"+string.Join(";",b.ExceptionHandlers.Select(x=>x.HandlerType+":"+x.CatchType?.FullName+":"+At(x.TryStart)+":"+At(x.TryEnd)+":"+At(x.HandlerStart)+":"+At(x.HandlerEnd)+":"+At(x.FilterStart)))}.Concat(b.Instructions.Select(i=>InstructionValue(i,indexes))));
    }
    private static string MethodSurface(MethodDefinition m)=>string.Join("|",m.Attributes,m.ImplAttributes,m.CallingConvention,m.HasThis,m.ExplicitThis,m.ReturnType.FullName,Attr(m),GP(m),
      m.MethodReturnType.Attributes,Val(m.MethodReturnType.Constant),Attr(m.MethodReturnType),string.Join(";",m.Parameters.Select(Param)),string.Join(";",m.Overrides.Select(x=>x.FullName)),m.HasPInvokeInfo?m.PInvokeInfo.Module.Name+":"+m.PInvokeInfo.EntryPoint+":"+m.PInvokeInfo.Attributes:"");
    private static SortedDictionary<string,string> Snapshot(ModuleDefinition m)
    {
        var d=new SortedDictionary<string,string>(StringComparer.Ordinal);
        d["assembly-identity"]=m.Assembly.Name.FullName;
        d["assembly-attributes"]=Attr(m.Assembly);d["module-attributes"]=Attr(m);
        d["assembly-references"]=string.Join(";",m.AssemblyReferences.Select(x=>x.FullName).Order());
        d["module-references"]=string.Join(";",m.ModuleReferences.Select(x=>x.Name).Order());
        d["entry-point"]=m.EntryPoint?.FullName??"";
        foreach(var r in m.Resources) d["resource:"+r.Name]=r.Attributes+":"+(r is EmbeddedResource e?Convert.ToHexString(SHA256.HashData(e.GetResourceData())):r.ToString());
        foreach(var t in Types(m).Where(x=>!IsRemoved(x))) {
            d["type:"+t.FullName]=TypeSurface(t);
            foreach(var f in t.Fields)d["field:"+f.FullName]=string.Join("|",f.Attributes,f.Offset,Val(f.Constant),Val(f.InitialValue),Attr(f),f.HasMarshalInfo?f.MarshalInfo.NativeType.ToString():"");
            foreach(var p in t.Properties)d["property:"+p.FullName]=string.Join("|",p.Attributes,Val(p.Constant),p.GetMethod?.FullName,p.SetMethod?.FullName,Attr(p),string.Join(";",p.OtherMethods.Select(x=>x.FullName)),string.Join(";",p.Parameters.Select(Param)));
            foreach(var e in t.Events)d["event:"+e.FullName]=string.Join("|",e.Attributes,e.AddMethod?.FullName,e.RemoveMethod?.FullName,e.InvokeMethod?.FullName,Attr(e),string.Join(";",e.OtherMethods.Select(x=>x.FullName)));
            foreach(var method in t.Methods) {d["method:"+method.FullName]=MethodSurface(method);d["body:"+method.FullName]=Body(method);}
        }
        return d;
    }
    private static object[] Incoming(ModuleDefinition m)
    {
        var result=new List<object>();
        foreach(var t in Types(m).Where(x=>!IsRemoved(x))) {
            void Add(string kind,string context,string value) {if(value.Contains(Removed,StringComparison.Ordinal))result.Add(new {kind,context,value});}
            Add("type",t.FullName,TypeSurface(t));
            foreach(var f in t.Fields)Add("field",f.FullName,f.FullName+Attr(f));
            foreach(var p in t.Properties)Add("property",p.FullName,p.FullName+Attr(p));
            foreach(var e in t.Events)Add("event",e.FullName,e.FullName+Attr(e));
            foreach(var method in t.Methods) {Add("method",method.FullName,MethodSurface(method)+method.FullName);Add("body",method.FullName,Body(method));}
        }
        return result.ToArray();
    }
    private static object[] DynamicSites(ModuleDefinition m)=>Types(m).Where(t=>!IsRemoved(t)).SelectMany(t=>t.Methods).Where(x=>x.HasBody).SelectMany(method=>method.Body.Instructions.Where(i=>i.Operand is MethodReference mr && (
      mr.DeclaringType.FullName.Contains("System.Reflection") || mr.FullName.Contains("System.Type::GetType") || mr.FullName.Contains("System.Activator") || mr.FullName.Contains("System.Text.Json.JsonSerializer::Deserialize") || mr.FullName.Contains("Newtonsoft.Json.JsonConvert::Deserialize") || mr.FullName.Contains("GetTypes("))).Select(i=>new {method=method.FullName,offset=i.Offset,call=((MethodReference)i.Operand).FullName})).Cast<object>().ToArray();
    public static int Main(string[] args)
    {
        if(args[0]=="scan") {
            var rows=new List<object>();
            foreach(var path in File.ReadAllLines(args[1]).Where(File.Exists).Distinct()) {
                try {using var m=ModuleDefinition.ReadModule(path,new ReaderParameters {ReadingMode=ReadingMode.Deferred});
                    var hits=m.GetTypeReferences().Where(x=>x.FullName.Contains(Removed)).Select(x=>x.FullName).Concat(m.GetMemberReferences().Where(x=>x.FullName.Contains(Removed)).Select(x=>x.FullName)).ToArray();
                    var strings=System.Text.Encoding.Unicode.GetString(File.ReadAllBytes(path)).Contains(Removed, StringComparison.Ordinal);
                    rows.Add(new {path,assembly=m.Assembly.Name.FullName,references=hits,reflectionStringPresent=strings});
                }catch(BadImageFormatException){rows.Add(new {path,native=true});}
            }
            File.WriteAllText(args[2],JsonSerializer.Serialize(rows,Json));return 0;
        }
        using var before=ModuleDefinition.ReadModule(args[0]);using var after=ModuleDefinition.ReadModule(args[1]);
        var b=Snapshot(before);var a=Snapshot(after);var differences=b.Keys.Union(a.Keys).Where(k=>!b.TryGetValue(k,out var bv)||!a.TryGetValue(k,out var av)||bv!=av).Select(k=>new {key=k,before=b.GetValueOrDefault(k),after=a.GetValueOrDefault(k)}).ToArray();
        var expectedPruning = new List<object>();
        var unexpected = new List<object>();
        foreach(var diff in differences) {
            if(diff.key=="assembly-references") {
                var oldRefs=before.AssemblyReferences.Select(x=>x.FullName).ToHashSet();
                var newRefs=after.AssemblyReferences.Select(x=>x.FullName).ToHashSet();
                var deletedRefs=oldRefs.Except(newRefs).ToArray();
                var deletedNames=before.AssemblyReferences.Where(x=>deletedRefs.Contains(x.FullName)).Select(x=>x.Name).ToHashSet();
                var scopedTypes=before.GetTypeReferences().Where(x=>x.Scope is AssemblyNameReference ar && deletedNames.Contains(ar.Name)).Select(x=>x.FullName).ToArray();
                var roots=scopedTypes.Where(n=>a.Values.Any(v=>v.Contains(n,StringComparison.Ordinal))).ToArray();
                if(newRefs.Except(oldRefs).Any() || roots.Any()) unexpected.Add(diff);
                else expectedPruning.Add(new {diff.key,removedAssemblyRefs=deletedRefs,removedScopeTypes=scopedTypes,retainedRoots=roots});
                continue;
            }
            if(diff.after==null && (diff.key.StartsWith("field:<PrivateImplementationDetails>") || diff.key.StartsWith("type:<PrivateImplementationDetails>/"))) {
                var name=diff.key[(diff.key.IndexOf(':')+1)..];
                var users=Types(before).SelectMany(x=>x.Methods).Where(x=>x.HasBody && x.Body.Instructions.Any(i=>i.Operand is MemberReference mr && (mr.FullName==name || mr.DeclaringType?.FullName==name))).ToArray();
                // For helper storage types, locate all fields that have that exact type.
                var owners=Types(before).SelectMany(x=>x.Fields).Where(x=>x.FieldType.FullName==name).ToArray();
                var ownerUsers=owners.SelectMany(f=>Types(before).SelectMany(x=>x.Methods).Where(x=>x.HasBody && x.Body.Instructions.Any(i=>i.Operand is FieldReference fr && fr.FullName==f.FullName))).ToArray();
                var allUsers=users.Concat(ownerUsers).Distinct().ToArray();
                if(allUsers.Length>0 && allUsers.All(x=>IsRemoved(x.DeclaringType)) && !a.Values.Any(v=>v.Contains(name,StringComparison.Ordinal)))
                    expectedPruning.Add(new {diff.key,exclusiveRemovedUsers=allUsers.Select(x=>x.FullName).ToArray()});
                else unexpected.Add(diff);
                continue;
            }
            unexpected.Add(diff);
        }
        var removed=Types(before).Where(IsRemoved).Select(t=>new {name=t.FullName,methods=t.Methods.Count,fields=t.Fields.Count}).ToArray();
        var incoming=Incoming(before);var remaining=Types(after).Where(IsRemoved).Select(x=>x.FullName).ToArray();
        var result=new {pass=unexpected.Count==0 && incoming.Length==0 && remaining.Length==0 && removed.Length>0,
           before=new {bytes=new FileInfo(args[0]).Length,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(args[0]))).ToLowerInvariant()},
           after=new {bytes=new FileInfo(args[1]).Length,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(args[1]))).ToLowerInvariant()},
           comparedRecords=b.Count,retainedTypes=Types(after).Count(),retainedMethods=Types(after).Sum(x=>x.Methods.Count),removed,incoming,remaining,differences,expectedPruning,unexpected,dynamicSites=DynamicSites(before),
           productExecution=false,normalization="Metadata token renumbering and MVID ignored; retained types, members, signatures, attributes, resources and canonical IL compared."};
        File.WriteAllText(args[2],JsonSerializer.Serialize(result,Json));Console.WriteLine(JsonSerializer.Serialize(new {result.pass,result.comparedRecords,result.retainedTypes,result.retainedMethods,removedTypes=removed.Length,differences=differences.Length,unexpected=unexpected.Count,incoming=incoming.Length}));return result.pass?0:3;
    }
}
