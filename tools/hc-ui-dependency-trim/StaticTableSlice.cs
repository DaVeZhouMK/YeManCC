using HcDeviceCoreTrim;
using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Text;

static class StaticTableSlice
{
    public static readonly string[] TypeNames=["iNKORE.UI.WPF.Modern.ThemeKeys","iNKORE.UI.WPF.Modern.Common.IconKeys.SegoeFluentIcons"];
    public sealed record Block(FieldDefinition Field,Instruction[] Instructions);
    public static List<Block> Blocks(TypeDefinition type)
    {
        var ctor=type.Methods.Single(m=>m.IsConstructor&&m.IsStatic);var il=ctor.Body.Instructions;
        if(ctor.Body.ExceptionHandlers.Count!=0||il.Count==0||il[^1].OpCode!=OpCodes.Ret)throw new InvalidDataException("Unexpected table constructor");
        var blocks=new List<Block>();var current=new List<Instruction>();
        foreach(var i in il.Take(il.Count-1))
        {
            current.Add(i);if(i.OpCode!=OpCodes.Stsfld)continue;
            if(i.Operand is not FieldReference r||r.DeclaringType.FullName!=type.FullName)throw new InvalidDataException("Foreign table assignment");
            var field=type.Fields.Single(f=>f.FullName==r.FullName);var code=current.ToArray();
            if(!field.IsStatic||!field.IsInitOnly)throw new InvalidDataException("Table field is mutable");
            if(type.FullName==TypeNames[0])
            {
                if(code.Length!=2||code[0].OpCode!=OpCodes.Ldstr||field.FieldType.FullName!="System.String")throw new InvalidDataException("Theme key initializer is not a pure literal");
            }
            else
            {
                if(code.Length!=4||code[0].OpCode!=OpCodes.Ldc_I4||code[1].OpCode!=OpCodes.Ldc_I4_0||code[2].OpCode!=OpCodes.Call||code[2].Operand is not MethodReference m||m.FullName!="iNKORE.UI.WPF.Modern.Common.IconKeys.FontIconData iNKORE.UI.WPF.Modern.Common.IconKeys.SegoeFluentIcons::CreateIcon(System.Int32,System.Boolean)")throw new InvalidDataException("Segoe icon initializer is not the reviewed pure integer/false helper call");
            }
            blocks.Add(new(field,code));current.Clear();
        }
        if(current.Count!=0||blocks.Count!=type.Fields.Count||blocks.Select(b=>b.Field.FullName).Distinct().Count()!=blocks.Count)throw new InvalidDataException("Initializer/field coverage mismatch");
        return blocks;
    }
    public static object Apply(IReadOnlyDictionary<string,AssemblyDefinition> assemblies,HashSet<TypeDefinition> retained,string root)
    {
        var tables=TypeNames.Select(n=>Util.Types(assemblies["iNKORE.UI.WPF.Modern"].MainModule).Single(t=>t.FullName==n)).ToArray();
        var names=tables.Select(t=>t.FullName).ToHashSet();var required=tables.ToDictionary(t=>t.FullName,t=>new HashSet<string>());var roots=new List<object>();
        void Scan(MethodDefinition m)
        {
            if(!m.HasBody||m.IsConstructor&&m.IsStatic&&names.Contains(m.DeclaringType.FullName))return;
            if(m.Body.Instructions.Any(i=>i.Operand is TypeReference t&&names.Contains(t.FullName))&&m.Body.Instructions.Any(i=>i.Operand is MethodReference r&&r.DeclaringType.FullName=="System.Type"&&r.Name is "GetField" or "GetFields" or "GetMember" or "GetMembers"))throw new InvalidDataException("Table reflection root requires a separate review: "+m.FullName);
            foreach(var i in m.Body.Instructions)
            {
                if(i.Operand is FieldReference f&&names.Contains(f.DeclaringType.FullName)){required[f.DeclaringType.FullName].Add(f.FullName);roots.Add(new{Method=m.FullName,i.Offset,Field=f.FullName});}
                if(i.Operand is string text)foreach(var t in tables)foreach(var f2 in t.Fields.Where(f=>text==f.Name)){required[t.FullName].Add(f2.FullName);roots.Add(new{Method=m.FullName,i.Offset,LiteralField=f2.FullName});}
            }
        }
        foreach(var t in retained)foreach(var m in t.Methods)Scan(m);
        using(var hc=Util.Read(Path.Combine(root,"HandheldCompanion.dll"),out var resolver)){using var clean=resolver;foreach(var t in Util.Types(hc.MainModule))foreach(var m in t.Methods)Scan(m);}
        var results=new List<object>();
        foreach(var t in tables)
        {
            var blocks=Blocks(t);var keep=required[t.FullName];if(keep.Count==0)throw new InvalidDataException("No roots for retained table; type closure should handle it");
            var ctor=t.Methods.Single(m=>m.IsConstructor&&m.IsStatic);var removed=blocks.Where(b=>!keep.Contains(b.Field.FullName)).ToArray();
            foreach(var block in removed){foreach(var i in block.Instructions)ctor.Body.Instructions.Remove(i);t.Fields.Remove(block.Field);}
            results.Add(new{Type=t.FullName,BeforeFields=blocks.Count,Fields=t.Fields.Count,RemovedFields=removed.Select(b=>b.Field.FullName).ToArray(),KeptInitializers=blocks.Where(b=>keep.Contains(b.Field.FullName)).Select(b=>new{Field=b.Field.FullName,Instructions=b.Instructions.Select(i=>i.OpCode.Name+" "+i.Operand?.ToString()).ToArray()}).ToArray(),Policy="Only unused immutable fields and their isolated literal/pure icon initialization blocks are removed; required field initializer instructions preserved exactly"});
        }
        return new{Status="sliced",Tables=results,Roots=roots,HardwareExecuted=false};
    }
}
