using Mono.Cecil;
using Mono.Cecil.Cil;

public sealed record Edge(string Assembly,string Caller,int Offset,string Op,string Target);
public sealed record ConsumerResult(int ManagedFiles,List<Edge> HcEdges,List<object> TypeReferences,List<string> Failures,List<string> Reachable,List<object> NativeBoundaries,List<string> CallbackRoots,List<string> RemovedUpstreamTypesWithoutCurrentReferences);
public sealed record Contracts(List<string> Enum,List<string> Format,List<string> Shadow);
public sealed record SafetyResult(bool Passed,List<string> Failures,List<string> Methods,List<string> FrameworkCalls,List<string> BoundNotInvoked,List<string> DynamicDispatch);
public static class Metadata
{
    sealed class Pool : IDisposable
    {
        public readonly DefaultAssemblyResolver Resolver=new();
        public readonly List<ModuleDefinition> Modules=[];
        public Pool(params string[] roots)
        {
            foreach(var r in roots) Resolver.AddSearchDirectory(r);
            Resolver.AddSearchDirectory(Path.GetDirectoryName(typeof(object).Assembly.Location)!);
            foreach(var r in roots) foreach(var p in Directory.EnumerateFiles(r,"*.dll").Order())
            {
                try { Modules.Add(ModuleDefinition.ReadModule(p,new ReaderParameters{AssemblyResolver=Resolver,InMemory=true})); }
                catch(BadImageFormatException) { }
            }
        }
        public TypeDefinition? Find(string name) => Modules.SelectMany(Program.Types).FirstOrDefault(t=>t.FullName==name);
        public void Dispose(){foreach(var m in Modules)m.Dispose();Resolver.Dispose();}
    }
    static bool Own(TypeReference t) => Program.Target(Program.Scope(t));
    static MethodDefinition? Resolve(MethodReference r,List<string> errors)
    {
        try { var d=r.Resolve(); if(d==null)errors.Add("Unresolved method: "+r.FullName); return d; }
        catch(Exception e){errors.Add("Unresolved method: "+r.FullName+" "+e.GetType().Name);return null;}
    }
    public static ConsumerResult ConsumerAudit(string runtime,string shared,string upstream)
    {
        using var pool=new Pool(runtime,shared); using var original=new Pool(upstream);
        var failures=new List<string>();var edges=new List<Edge>();var refs=new List<object>();var roots=new List<MethodDefinition>();
        var currentRefs=new HashSet<string>();
        foreach(var m in pool.Modules)
        {
            foreach(var tr in m.GetTypeReferences().Where(Own))
            {
                currentRefs.Add(tr.FullName); bool resolved;
                try{resolved=tr.Resolve()!=null;}catch{resolved=false;}
                if(!resolved)failures.Add($"{m.Name}: unresolved type {tr.FullName}");
                refs.Add(new{Consumer=m.Name,Type=tr.FullName,Resolved=resolved});
            }
            foreach(var mr in m.GetMemberReferences().Where(x=>x.DeclaringType!=null&&Own(x.DeclaringType)))
            {
                if(mr is MethodReference method) Resolve(method,failures);
                if(mr is FieldReference field) {try {if(field.Resolve()==null)failures.Add("Unresolved field: "+field.FullName);}catch{failures.Add("Unresolved field: "+field.FullName);}}
            }
            foreach(var md in Program.Types(m).SelectMany(t=>t.Methods).Where(x=>x.HasBody)) foreach(var i in md.Body.Instructions)
            {
                if(i.Operand is not MemberReference mr || mr.DeclaringType==null || !Own(mr.DeclaringType)) continue;
                if(m.Name=="HandheldCompanion.dll")
                {
                    edges.Add(new(Program.Scope(mr.DeclaringType),md.FullName,i.Offset,i.OpCode.Name,mr.FullName));
                    if(mr is MethodReference method){var d=Resolve(method,failures);if(d!=null)roots.Add(d);}
                }
            }
        }
        var seen=new HashSet<string>();var queue=new Queue<MethodDefinition>(roots);var boundaries=new List<object>();
        while(queue.TryDequeue(out var md))
        {
            if(!seen.Add(md.FullName))continue;
            foreach(var cc in md.DeclaringType.Methods.Where(x=>x.IsConstructor&&x.IsStatic))queue.Enqueue(cc);
            if(md.IsPInvokeImpl){boundaries.Add(new{Method=md.FullName,Kind="PInvoke-not-executed",Module=md.PInvokeInfo.Module.Name});continue;}
            if(!md.HasBody)continue;
            foreach(var i in md.Body.Instructions)
            {
                if(i.OpCode==OpCodes.Calli)boundaries.Add(new{Method=md.FullName,Kind="calli-not-executed",Module="vtable"});
                if(i.Operand is MethodReference r && Own(r.DeclaringType)){var d=Resolve(r,failures);if(d!=null)queue.Enqueue(d);}
            }
        }
        var cb=new[]{"SharpDX.DirectInput.EnumDevicesCallback","SharpDX.DirectInput.EnumDevicesCallback/DirectInputEnumDevicesDelegate","SharpDX.DirectInput.RawJoystickState"};
        foreach(var name in cb)if(pool.Find(name)==null)failures.Add("Missing current dynamic root: "+name);
        var removed=original.Modules.Where(m=>Program.Target(m.Assembly.Name.Name)).SelectMany(Program.Types)
            .Where(t=>(t.FullName.Contains("Shadow")||t.FullName.Contains("Callback")||t.FullName.Contains("Vtbl"))&&pool.Find(t.FullName)==null&&!currentRefs.Contains(t.FullName))
            .Select(t=>t.FullName).Order().ToList();
        return new(pool.Modules.Count,edges,refs,failures.Distinct().ToList(),seen.Order().ToList(),boundaries,cb.ToList(),removed);
    }
    public static void Inspect(string runtime,string shared,string upstream,string evidence)
    {
        var rows=new List<object>();var inventory=new List<object>();
        foreach(var root in new[]{runtime,shared,upstream})
        {
            using var pool=new Pool(root);
            foreach(var m in pool.Modules)
            {
                var edges=new List<Edge>();
                foreach(var md in Program.Types(m).SelectMany(t=>t.Methods).Where(x=>x.HasBody))foreach(var i in md.Body.Instructions)
                    if(i.Operand is MemberReference {DeclaringType:not null} mr && Own(mr.DeclaringType))edges.Add(new(Program.Scope(mr.DeclaringType),md.FullName,i.Offset,i.OpCode.Name,mr.FullName));
                if(edges.Count!=0)rows.Add(new{Path=m.FileName,Edges=edges});
                if(Program.Target(m.Assembly.Name.Name)) inventory.Add(new{Path=m.FileName,Types=Program.Types(m).Select(t=>new{t.FullName,BaseType=t.BaseType?.FullName,Attributes=Attrs(t.CustomAttributes),Fields=t.Fields.Select(f=>new{f.FullName,f.Offset,Attributes=Attrs(f.CustomAttributes)}),Methods=t.Methods.Select(md=>new{md.FullName,Flags=md.Attributes.ToString(),PInvoke=md.PInvokeInfo?.Module.Name,IL=md.HasBody?md.Body.Instructions.Select(i=>i.ToString()):[]})})});
            }
        }
        Program.Write(Path.Combine(evidence,"inspection-consumers.json"),rows); Program.Write(Path.Combine(evidence,"inspection-sharpdx.json"),inventory);
    }
    static string Arg(CustomAttributeArgument a) => a.Value is CustomAttributeArgument[] arr?"["+string.Join(",",arr.Select(Arg))+"]":a.Value?.ToString()??"null";
    static string[] Attrs(IEnumerable<CustomAttribute> attrs)=>attrs.Select(a=>a.AttributeType.FullName+"("+string.Join(",",a.ConstructorArguments.Select(Arg))+")"+string.Join(",",a.Fields.Select(f=>f.Name+"="+Arg(f.Argument)))+string.Join(",",a.Properties.Select(f=>f.Name+"="+Arg(f.Argument)))).Order().ToArray();
    static string FieldContract(FieldDefinition f)=>$"{f.FullName}|{f.Offset}|{f.Attributes}|{(f.HasConstant?f.Constant:"")}|{(f.HasMarshalInfo?f.MarshalInfo.NativeType:"")}|{string.Join(";",Attrs(f.CustomAttributes))}";
    static string IL(MethodDefinition m)=>!m.HasBody?"":string.Join("\n",m.Body.Instructions.Select(i=>i.OpCode.Code+":"+(i.Operand switch{Instruction branch=>m.Body.Instructions.IndexOf(branch).ToString(),Instruction[] branches=>string.Join(",",branches.Select(b=>m.Body.Instructions.IndexOf(b))),_=>i.Operand?.ToString()??""})));
    public static Contracts Compare(string upstream,string candidate)
    {
        using var a=new Pool(upstream);using var b=new Pool(candidate);
        List<string> CompareTypes(IEnumerable<string> names,bool methods)
        {
            var errors=new List<string>();
            foreach(var name in names)
            {
                var x=a.Find(name);var y=b.Find(name);
                if(x==null){errors.Add("Original type missing: "+name);continue;}
                if(y==null){errors.Add("Type missing: "+name);continue;}
                if(x.BaseType?.FullName!=y.BaseType?.FullName)errors.Add("Base type changed: "+name);
                if(x.PackingSize!=y.PackingSize||x.ClassSize!=y.ClassSize||(x.Attributes&TypeAttributes.LayoutMask)!=(y.Attributes&TypeAttributes.LayoutMask))errors.Add("Layout changed: "+name);
                if(!Attrs(x.CustomAttributes).SequenceEqual(Attrs(y.CustomAttributes)))errors.Add("Custom attributes changed: "+name);
                var fields=x.Fields.Where(f=>!f.IsStatic).Select(FieldContract).ToArray();var fields2=y.Fields.Where(f=>!f.IsStatic).Select(FieldContract).ToArray();
                if(!fields.SequenceEqual(fields2))errors.Add("Fields/layout/attributes changed: "+name);
                foreach(var md in x.Methods.Where(md=>(methods||md.IsConstructor)&&md.Name is not "BeginInvoke" and not "EndInvoke"))
                {
                    var nd=y.Methods.FirstOrDefault(n=>n.FullName==md.FullName);
                    if(nd==null){errors.Add("Method missing: "+md.FullName);continue;}
                    if(!Attrs(md.CustomAttributes).SequenceEqual(Attrs(nd.CustomAttributes)))errors.Add("Method attributes changed: "+md.FullName);
                    if(IL(md)!=IL(nd))errors.Add("Method IL changed: "+md.FullName);
                }
            }
            return errors;
        }
        var en=CompareTypes(new[]{"SharpDX.DirectInput.EnumDevicesCallback","SharpDX.DirectInput.EnumDevicesCallback/DirectInputEnumDevicesDelegate","SharpDX.DirectInput.DeviceInstance","SharpDX.DirectInput.DeviceInstance/__Native"},true);
        // DeviceInstance has public unused helpers removed by trimming. Only the executed marshal-from path is required.
        en.RemoveAll(s=>s.StartsWith("Method missing:")&&!s.Contains("DirectInputEnum")&&!s.Contains("EnumDevicesCallback")&&!s.Contains("__MarshalFrom")&&!s.Contains("::.ctor("));
        var raw=a.Find("SharpDX.DirectInput.RawJoystickState")!;
        var format=CompareTypes(Program.Types(raw).Select(t=>t.FullName).Concat(new[]{"SharpDX.DirectInput.DataFormatAttribute","SharpDX.DirectInput.DataObjectFormatAttribute"}),false);
        var origGet=a.Find("SharpDX.DirectInput.CustomDevice`3")!.Methods.Single(m=>m.Name=="GetDataFormat");var newGet=b.Find("SharpDX.DirectInput.CustomDevice`3")?.Methods.SingleOrDefault(m=>m.Name=="GetDataFormat");
        if(newGet==null||IL(origGet)!=IL(newGet))format.Add("GetDataFormat reflection implementation changed");
        var usedConstructors=Program.Types(raw).SelectMany(t=>t.CustomAttributes.Concat(t.Fields.SelectMany(f=>f.CustomAttributes))).Select(a=>a.Constructor.FullName).ToHashSet();
        format.RemoveAll(s=>s.StartsWith("Method missing: ")&&!usedConstructors.Contains(s[16..]));
        var shadow=CompareTypes(new[]{"SharpDX.ShadowAttribute","SharpDX.ShadowContainer","SharpDX.CppObjectShadow","SharpDX.CppObjectVtbl","SharpDX.Direct3D9.Include","SharpDX.Direct3D9.IncludeShadow","SharpDX.Direct3D9.IncludeShadow/IncludeVtbl","SharpDX.Direct3D9.IncludeShadow/IncludeVtbl/OpenDelegate","SharpDX.Direct3D9.IncludeShadow/IncludeVtbl/CloseDelegate"},true);
        if(b.Find("SharpDX.CppObject")?.Methods.Any(m=>m.Name=="ToCallbackPtr")==false)shadow.Add("Method missing: SharpDX.CppObject.ToCallbackPtr");
        return new(en,format,shadow);
    }
    public static SafetyResult Safety(string root,string kind)
    {
        using var pool=new Pool(root);var errors=new List<string>();var queue=new Queue<MethodDefinition>();var seen=new HashSet<string>();var framework=new HashSet<string>();var bound=new HashSet<string>();var dispatch=new HashSet<string>();
        void Add(string type,params string[] methods){var t=pool.Find(type);if(t==null){errors.Add("Safe root missing: "+type);return;}foreach(var n in methods){var ms=t.Methods.Where(m=>m.Name==n).ToArray();if(ms.Length==0)errors.Add("Safe method missing: "+type+"."+n);foreach(var m in ms)queue.Enqueue(m);}}
        if(kind=="enum")Add("SharpDX.DirectInput.EnumDevicesCallback",".ctor","DirectInputEnumDevicesImpl");
        else if(kind=="format")
        {
            Add("SharpDX.DirectInput.CustomDevice`3","GetDataFormat",".cctor");
            Add("SharpDX.DirectInput.DataFormatAttribute",".ctor");Add("SharpDX.DirectInput.DataObjectFormatAttribute",".ctor");
            // A zero-handle uninitialized Joystick is used; neither its constructor nor disposal is called.
            Add("SharpDX.DirectInput.Joystick");Add("SharpDX.DirectInput.Device");
        }
        else
        {
            Add("SharpDX.CppObject","ToCallbackPtr","NativePointerUpdating","NativePointerUpdated");
            Add("SharpDX.ShadowAttribute",".ctor","Get");Add("SharpDX.ShadowContainer",".ctor","Initialize","Find","Dispose");
            Add("SharpDX.CppObjectShadow","Initialize","Dispose");Add("SharpDX.Direct3D9.IncludeShadow",".ctor",".cctor","get_GetVtbl");
            Add("SharpDX.DisposeBase","Dispose","CheckAndDispose");
        }
        foreach(var mod in pool.Modules.Where(m=>Program.Target(m.Assembly.Name.Name)))
            foreach(var cc in mod.Types.Where(t=>t.Name=="<Module>").SelectMany(t=>t.Methods).Where(x=>x.Name==".cctor"))queue.Enqueue(cc);
        while(queue.TryDequeue(out var m))
        {
            if(!seen.Add(m.Module.Name+":"+m.FullName))continue;
            foreach(var cc in m.DeclaringType.Methods.Where(x=>x.Name==".cctor"))queue.Enqueue(cc);
            if(m.IsPInvokeImpl){errors.Add("Forbidden PInvoke: "+m.FullName);continue;}
            if(!m.HasBody){if(m.IsAbstract)dispatch.Add(m.FullName);continue;}
            foreach(var i in m.Body.Instructions)
            {
                if(i.OpCode==OpCodes.Calli)errors.Add("Forbidden calli: "+m.FullName);
                if(i.Operand is FieldReference f && Own(f.DeclaringType)){var t=pool.Find(f.DeclaringType.FullName);if(t!=null)foreach(var cc in t.Methods.Where(x=>x.Name==".cctor"))queue.Enqueue(cc);}
                if(i.Operand is not MethodReference r)continue;
                if(i.OpCode==OpCodes.Ldftn||i.OpCode==OpCodes.Ldvirtftn){bound.Add(r.FullName);continue;}
                if(Own(r.DeclaringType)){var d=Resolve(r,errors);if(d!=null)queue.Enqueue(d);continue;}
                if(!Program.Scope(r.DeclaringType).StartsWith("System",StringComparison.Ordinal)&&Program.Scope(r.DeclaringType) is not "mscorlib" and not "netstandard")errors.Add("Non-framework call: "+r.FullName);
                if(r.DeclaringType.FullName is "System.Runtime.InteropServices.NativeLibrary" || r.Name.Contains("LoadLibrary",StringComparison.Ordinal))errors.Add("Forbidden native loader: "+r.FullName);
                if(r.DeclaringType.FullName=="System.Runtime.InteropServices.Marshal"&&r.Name is not ("AllocHGlobal" or "FreeHGlobal" or "WriteIntPtr" or "GetFunctionPointerForDelegate" or "PtrToStringUni" or "OffsetOf" or "SizeOf"))errors.Add("Unreviewed Marshal operation: "+r.FullName);
                framework.Add(r.FullName);
            }
        }
        return new(errors.Count==0,errors,seen.Order().ToList(),framework.Order().ToList(),bound.Order().ToList(),dispatch.Order().ToList());
    }
}


