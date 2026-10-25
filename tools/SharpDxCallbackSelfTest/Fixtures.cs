using Mono.Cecil;
using Mono.Cecil.Cil;
public sealed record FixtureSpec(string Name,string Kind,bool Execute,string ExpectedFailure);
public static class Fixtures
{
    public static readonly FixtureSpec[] Specs = [
        new("enum-return-zero","enum",true,"enum-return"),
        new("enum-invoke-int64","enum",false,"Method missing"),
        new("enum-cdecl","enum",false,"Custom attributes changed"),
        new("enum-native-layout","enum",false,"Fields/layout/attributes changed"),
        new("raw-missing-format-attribute","format",true,"format-content"),
        new("raw-missing-x-attribute","format",true,"format-content"),
        new("include-missing-shadow-attribute","shadow",true,"shadow-pointer"),
        new("include-missing-shadow-constructor","shadow",true,"safety"),
        new("core-missing-callbackptr","shadow",true,"safety")
    ];
    public static void Mutate(string root,string name)
    {
        var dll=name.StartsWith("core-")?"SharpDX.dll":name.StartsWith("include-")?"SharpDX.Direct3D9.dll":"SharpDX.DirectInput.dll";
        var path=Path.Combine(root,dll);
        using var m=ModuleDefinition.ReadModule(path,new ReaderParameters{InMemory=true});
        TypeDefinition T(string n)=>Program.Types(m).Single(t=>t.FullName==n);
        switch(name)
        {
            case "enum-return-zero":
                var md=T("SharpDX.DirectInput.EnumDevicesCallback").Methods.Single(x=>x.Name=="DirectInputEnumDevicesImpl");
                var ret=md.Body.Instructions.Last(i=>i.OpCode==OpCodes.Ret);var prev=md.Body.Instructions[md.Body.Instructions.IndexOf(ret)-1];
                Program.Require(prev.OpCode==OpCodes.Ldc_I4_1,"Unexpected original callback return IL");prev.OpCode=OpCodes.Ldc_I4_0;break;
            case "enum-invoke-int64":T("SharpDX.DirectInput.EnumDevicesCallback/DirectInputEnumDevicesDelegate").Methods.Single(x=>x.Name=="Invoke").ReturnType=m.TypeSystem.Int64;break;
            case "enum-cdecl":
                var attr=T("SharpDX.DirectInput.EnumDevicesCallback/DirectInputEnumDevicesDelegate").CustomAttributes.Single(a=>a.AttributeType.Name=="UnmanagedFunctionPointerAttribute");
                attr.ConstructorArguments[0]=new CustomAttributeArgument(attr.ConstructorArguments[0].Type,2);break;
            case "enum-native-layout":
                var type=T("SharpDX.DirectInput.DeviceInstance/__Native");type.Fields.Remove(type.Fields.Single(f=>f.Name=="__InstanceName259"));break;
            case "raw-missing-format-attribute":
                var raw=T("SharpDX.DirectInput.RawJoystickState");raw.CustomAttributes.Remove(raw.CustomAttributes.Single(a=>a.AttributeType.Name=="DataFormatAttribute"));break;
            case "raw-missing-x-attribute":
                var x=T("SharpDX.DirectInput.RawJoystickState").Fields.Single(f=>f.Name=="X");x.CustomAttributes.Clear();break;
            case "include-missing-shadow-attribute":
                var inc=T("SharpDX.Direct3D9.Include");inc.CustomAttributes.Remove(inc.CustomAttributes.Single(a=>a.AttributeType.Name=="ShadowAttribute"));break;
            case "include-missing-shadow-constructor":
                var sh=T("SharpDX.Direct3D9.IncludeShadow");sh.Methods.Remove(sh.Methods.Single(md=>md.Name==".ctor"));break;
            case "core-missing-callbackptr":
                var cpp=T("SharpDX.CppObject");cpp.Methods.Single(md=>md.Name=="ToCallbackPtr").Name="ToCallbackPtr_RemovedForControl";break;
            default:throw new ArgumentException("Unknown fixture "+name);
        }
        m.Write(path);
    }
}

