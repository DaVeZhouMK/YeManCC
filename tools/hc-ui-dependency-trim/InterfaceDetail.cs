using HcDeviceCoreTrim;
using Mono.Cecil;
using System.Text.Json;
static class InterfaceDetail
{
    public static void Dump(string path,string name,string report)
    {
        using var a=Util.Read(path,out var r);using var c=r;var t=Util.Types(a.MainModule).Single(t=>t.FullName==name);
        Util.Save(report,new{t.FullName,Base=t.BaseType?.FullName,Fields=t.Fields.Select(f=>new{f.Name,Type=f.FieldType.FullName,Attributes=f.Attributes.ToString(),f.HasConstant,Constant=f.HasConstant?f.Constant:null,Bytes=f.InitialValue.Length}).ToArray(),Methods=t.Methods.Select(m=>new{m.FullName,Attributes=m.Attributes.ToString(),Instructions=m.HasBody?m.Body.Instructions.Count:0,Calls=m.HasBody?m.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>new{i.Offset,Method=((MethodReference)i.Operand).FullName}).DistinctBy(i=>i.Method).ToArray():null,IL=m.HasBody?m.Body.Instructions.Take(150).Select(i=>new{i.Offset,Op=i.OpCode.Name,Operand=i.Operand?.ToString()}).ToArray():null}).ToArray()});
        Console.WriteLine(report);
    }
}
