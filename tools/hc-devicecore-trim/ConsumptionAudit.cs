using Mono.Cecil;
using Mono.Cecil.Cil;

namespace HcDeviceCoreTrim;
sealed class ConsumptionAudit
{
    public MethodDefinition[] Methods { get; }
    readonly Dictionary<string,MethodDefinition> methods;
    readonly Dictionary<string,TypeDefinition> types;
    public HashSet<string> DeviceClosure { get; }
    public HashSet<string> FactoryConstructorClosure { get; }
    public List<object> InternalReferencesNotResolved { get; }=[];
    public ConsumptionAudit(AssemblyDefinition assembly)
    {
        var ts=Util.Types(assembly.MainModule).ToArray();Methods=ts.SelectMany(t=>t.Methods).ToArray();
        methods=Methods.ToDictionary(m=>m.FullName,StringComparer.Ordinal);types=ts.ToDictionary(t=>t.FullName,StringComparer.Ordinal);
        DeviceClosure=Closure(Methods.Where(m=>m.DeclaringType.FullName.StartsWith("HandheldCompanion.Devices.",StringComparison.Ordinal)));
        FactoryConstructorClosure=Closure(Methods.Where(m=>m.DeclaringType.FullName=="HandheldCompanion.Managers.ManagerFactory"&&m.Name==".cctor"));
    }
    HashSet<string> Closure(IEnumerable<MethodDefinition> roots)
    {
        var seen=new HashSet<string>(StringComparer.Ordinal);var queue=new Queue<MethodDefinition>(roots);
        void AddType(string name)
        {
            if(!types.TryGetValue(name,out var t))return;
            foreach(var c in t.Methods.Where(m=>m.Name==".cctor"))queue.Enqueue(c);
        }
        while(queue.TryDequeue(out var m))
        {
            if(!seen.Add(m.FullName))continue;AddType(m.DeclaringType.FullName);
            if(!m.HasBody)continue;
            foreach(var i in m.Body.Instructions)
            {
                if(i.Operand is MemberReference member&&member.DeclaringType!=null)AddType(member.DeclaringType.GetElementType().FullName);
                if(i.Operand is MethodReference mr)
                {
                    var element=mr.GetElementMethod();
                    if(methods.TryGetValue(element.FullName,out var target))queue.Enqueue(target);
                    else if(types.TryGetValue(element.DeclaringType.GetElementType().FullName,out var declaring))
                    {
                        // Generic instantiation changes the rendered signature. Conservatively include every matching overload.
                        var candidates=declaring.Methods.Where(x=>x.Name==element.Name&&x.Parameters.Count==element.Parameters.Count).ToArray();
                        foreach(var targetMethod in candidates)queue.Enqueue(targetMethod);
                        if(candidates.Length==0)InternalReferencesNotResolved.Add(new{Caller=m.FullName,Target=element.FullName});
                    }
                    if(mr is GenericInstanceMethod gm)foreach(var t in gm.GenericArguments)
                        if(types.TryGetValue(t.GetElementType().FullName,out var state)&&state.Name.Contains("d__",StringComparison.Ordinal))foreach(var method in state.Methods)queue.Enqueue(method);
                }
                if(i.Operand is TypeReference tr)AddType(tr.GetElementType().FullName);
            }
        }
        return seen;
    }
    public IEnumerable<(MethodDefinition Method,string Value)> Strings()=>Methods.Where(m=>m.HasBody).SelectMany(m=>m.Body.Instructions.Where(i=>i.Operand is string).Select(i=>(m,(string)i.Operand)));
    public object Report(AssemblyDefinition assembly)
    {
        var strings=Strings().ToArray();
        var resourceAccess=Methods.Where(m=>m.HasBody).SelectMany(m=>m.Body.Instructions.Where(i=>i.Operand is MethodReference mr&&(mr.Name.Contains("Resource",StringComparison.Ordinal)||mr.DeclaringType.FullName=="HandheldCompanion.Properties.Resources")).Select(i=>new{Caller=m.FullName,Target=((MethodReference)i.Operand).FullName,DeviceReachable=DeviceClosure.Contains(m.FullName),FactoryConstructorReachable=FactoryConstructorClosure.Contains(m.FullName)})).ToArray();
        return new{Assembly=assembly.Name.FullName,DeviceRootPolicy="All methods of all HandheldCompanion.Devices types (including nested callbacks), plus referenced internal methods and static initializers; async state machines included",FactoryRootPolicy="ManagerFactory .cctor and its transitive constructor/static-initializer/delegate method references; Manager.Start is not assumed",DeviceClosureMethods=DeviceClosure.Count,FactoryConstructorClosureMethods=FactoryConstructorClosure.Count,InternalReferencesNotResolved,DeviceClosure=DeviceClosure.Order().ToArray(),FactoryConstructorClosure=FactoryConstructorClosure.Order().ToArray(),DeviceUiResourceStrings=strings.Where(s=>DeviceClosure.Contains(s.Method.FullName)&&(s.Value.Contains(".xaml",StringComparison.OrdinalIgnoreCase)||s.Value.Contains(".png",StringComparison.OrdinalIgnoreCase)||s.Value.Contains("DeviceImages",StringComparison.OrdinalIgnoreCase))).Select(s=>new{Method=s.Method.FullName,s.Value}).ToArray(),FactoryXInputResourceConsumers=resourceAccess.Where(x=>x.FactoryConstructorReachable&&x.Target.Contains("XInput",StringComparison.OrdinalIgnoreCase)).ToArray(),ResourceAccess=resourceAccess,Limitations="Static IL consumer evidence only; reflection, external callbacks and WPF theme resolution require their own consumer contract. This is not hardware or complete lifecycle validation."};
    }
}
