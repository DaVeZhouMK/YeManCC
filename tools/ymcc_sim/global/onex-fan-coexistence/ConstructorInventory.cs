using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;

static partial class Harness
{
    static int ConstructorInventory(string[] args)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> list)=>list.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        var types=Types(image.MainModule.Types).ToDictionary(t=>t.FullName);
        var methods=types.Values.SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName);
        var factory=types["HandheldCompanion.Devices.IDevice"].Methods.Single(m=>m.Name=="GetCurrent");
        bool Device(TypeDefinition type){for(var current=type;current is not null;current=types.GetValueOrDefault(current.BaseType?.FullName??""))if(current.FullName=="HandheldCompanion.Devices.IDevice")return true;return false;}
        var roots=factory.Body.Instructions.Where(i=>i.OpCode==OpCodes.Newobj).Select(i=>(MethodReference)i.Operand).Where(m=>types.TryGetValue(m.DeclaringType.FullName,out var t)&&Device(t)).Select(m=>methods[m.FullName]).DistinctBy(m=>m.FullName).ToArray();
        var rows=new List<object>();var closure=new Dictionary<string,MethodDefinition>();
        foreach(var root in roots)
        {
            var queue=new Queue<MethodDefinition>();queue.Enqueue(root);var visited=new HashSet<string>();var edges=new List<object>();
            while(queue.TryDequeue(out var method))
            {
                if(!visited.Add(method.FullName)||!method.HasBody)continue;closure[method.FullName]=method;
                foreach(var instruction in method.Body.Instructions)
                {
                    if(instruction.Operand is MethodReference call)
                    {
                        var bound=call is GenericInstanceMethod generic?generic.ElementMethod:call;
                        var local=methods.GetValueOrDefault(bound.FullName);
                        edges.Add(new{from=method.FullName,opcode=instruction.OpCode.Name,to=call.FullName,local=local is not null,abstractOrExternal=local is null||!local.HasBody});
                        if(local is not null&&local.HasBody)queue.Enqueue(local);
                    }
                    if(instruction.Operand is MemberReference member&&types.TryGetValue(member.DeclaringType?.FullName??"",out var declaring))
                        foreach(var cctor in declaring.Methods.Where(m=>m.IsConstructor&&m.IsStatic))queue.Enqueue(cctor);
                }
            }
            rows.Add(new{type=root.DeclaringType.FullName,originalConstructor=root.FullName,inactive=root.DeclaringType.Name=="SuiPlay0X1",reachableLocalMethods=visited.Order().ToArray(),edges});
        }
        var native=types.Values.SelectMany(t=>t.Methods).Where(m=>m.IsPInvokeImpl&&string.Equals(m.PInvokeInfo.Module.Name,"GamepadMotion.dll",StringComparison.OrdinalIgnoreCase)).Select(m=>new{method=m.FullName,entryPoint=m.PInvokeInfo.EntryPoint,attributes=(int)m.PInvokeInfo.Attributes,returnType=m.ReturnType.FullName,parameters=m.Parameters.Select(p=>new{p.Name,type=p.ParameterType.FullName,attributes=(int)p.Attributes,marshal=p.HasMarshalInfo?p.MarshalInfo.NativeType.ToString():null}).ToArray()}).ToArray();
        var risky=closure.Values.SelectMany(m=>m.Body.Instructions.Where(i=>i.Operand is MethodReference call&&(call.DeclaringType.FullName.StartsWith("System.IO.",StringComparison.Ordinal)||call.DeclaringType.FullName.StartsWith("System.Management.",StringComparison.Ordinal)||call.DeclaringType.FullName.Contains("Thread",StringComparison.Ordinal)||call.DeclaringType.FullName.Contains("Task",StringComparison.Ordinal)||call.DeclaringType.FullName.Contains("NativeLibrary",StringComparison.Ordinal))).Select(i=>new{owner=m.FullName,call=((MethodReference)i.Operand).FullName})).ToArray();
        File.WriteAllText(args[1],JsonSerializer.Serialize(new{status="PASS_INVENTORY_ONLY",hcSha256=Hash(args[0]),factoryConstructors=roots.Length,rows,reachableLocalMethodCount=closure.Count,risky,nativeGamepadMotionDeclarations=native,hardwareIo=false,constructorsExecuted=false,nativeCodeExecuted=false},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"Original factory constructors={roots.Length}, local closure={closure.Count}, native declarations={native.Length}");return 0;
    }
}
