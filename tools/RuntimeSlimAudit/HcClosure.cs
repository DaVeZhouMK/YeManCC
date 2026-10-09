using Mono.Cecil;
using System.Text.Json;

// Deliberately over-approximates callbacks, overloads and device virtual dispatch.
// Absence in this graph is not a deletion gate without external/dynamic-root review.
internal static class HcClosure
{
    public static int Run(string assemblyPath, string output)
    {
        using var module = ModuleDefinition.ReadModule(assemblyPath);
        var types = Flatten(module.Types).ToArray();
        var byType = types.ToDictionary(t => t.FullName);
        var methods = types.SelectMany(t => t.Methods).ToArray();
        var byMember = methods.GroupBy(m => (m.DeclaringType.FullName, m.Name)).ToDictionary(g => g.Key, g => g.ToArray());
        var seen = new HashSet<MethodDefinition>();
        var queue = new Queue<MethodDefinition>();
        var parent = new Dictionary<MethodDefinition, MethodDefinition?>();
        var roots = new List<string>();
        var unresolved = new SortedSet<string>();
        var reflection = new List<object>();
        void Add(MethodDefinition m, MethodDefinition? from)
        {
            if (!seen.Add(m)) return;
            parent[m] = from; queue.Enqueue(m);
            if (from is null) roots.Add(m.FullName);
            foreach (var cctor in m.DeclaringType.Methods.Where(x => x.Name == ".cctor")) Add(cctor, m);
        }
        void AddMember(TypeReference type, string name, MethodDefinition from)
        {
            if (type is TypeSpecification spec) type = spec.ElementType;
            if (!byType.TryGetValue(type.FullName, out var definition)) return;
            if (byMember.TryGetValue((definition.FullName, name), out var matches)) foreach (var m in matches) Add(m, from);
            else if (definition.BaseType is not null) AddMember(definition.BaseType, name, from);
        }
        bool Assignable(TypeDefinition candidate, TypeReference target)
        {
            string Name(TypeReference t) => t is TypeSpecification spec ? Name(spec.ElementType) : t.FullName;
            var pending = new Queue<TypeReference>(); pending.Enqueue(candidate);
            var visited = new HashSet<string>();
            while (pending.Count > 0)
            {
                var current = pending.Dequeue(); string name = Name(current);
                if (!visited.Add(name)) continue;
                if (name == Name(target)) return true;
                if (!byType.TryGetValue(name, out var definition)) continue;
                if (definition.BaseType is not null) pending.Enqueue(definition.BaseType);
                foreach (var iface in definition.Interfaces) pending.Enqueue(iface.InterfaceType);
            }
            return false;
        }
        // All device implementations and callbacks, not only the tested manufacturer.
        foreach (var type in types.Where(t => t.Namespace.StartsWith("HandheldCompanion.Devices", StringComparison.Ordinal)))
        foreach (var method in type.Methods) Add(method, null);
        foreach (var type in types.Where(t => t.FullName == "HandheldCompanion.App" || t.FullName is "HandheldCompanion.Managers.ControllerManager" or "HandheldCompanion.Managers.SystemManager"))
        foreach (var method in type.Methods.Where(m => m.Name == ".cctor" || m.Name.StartsWith("get_") || m.Name.StartsWith("set_") || (type.Name == "SystemManager" && (m.Name.StartsWith("add_") || m.Name.StartsWith("remove_") || m.Name.Contains("SystemEvents"))))) Add(method,null);
        while (queue.Count > 0)
        {
            var method = queue.Dequeue();
            foreach (var attribute in method.CustomAttributes.Where(a => a.AttributeType.Name.EndsWith("StateMachineAttribute", StringComparison.Ordinal)))
            foreach (var argument in attribute.ConstructorArguments)
                if (argument.Value is TypeReference state && byType.TryGetValue(state.FullName, out var stateType)) foreach (var m in stateType.Methods) Add(m, method);
            if (!method.HasBody) continue;
            foreach (var instruction in method.Body.Instructions)
            {
                if (instruction.Operand is MethodReference called)
                {
                    AddMember(called.DeclaringType, called.Name, method);
                    if (instruction.OpCode.Code == Mono.Cecil.Cil.Code.Callvirt && byType.ContainsKey(called.DeclaringType.FullName))
                        foreach (var m in methods.Where(m => m.IsVirtual && m.Name == called.Name && m.Parameters.Count == called.Parameters.Count && Assignable(m.DeclaringType, called.DeclaringType))) Add(m, method);
                    if (called.DeclaringType.Namespace.StartsWith("System.Reflection", StringComparison.Ordinal) || called.Name is "GetType" or "GetMethod" or "CreateInstance")
                        reflection.Add(new { caller = method.FullName, target = called.FullName, note = "REQUIRES_MANUAL_BRANCH_AND_STRING_REVIEW" });
                }
                else if (instruction.Operand is FieldReference field && byType.TryGetValue(field.DeclaringType.FullName, out var owner))
                    foreach (var cctor in owner.Methods.Where(m => m.Name == ".cctor")) Add(cctor, method);
            }
        }
        var viiper = seen.Where(m => m.HasPInvokeInfo && m.PInvokeInfo?.Module.Name.Contains("viiper", StringComparison.OrdinalIgnoreCase) == true).ToArray();
        string[] Trace(MethodDefinition method)
        {
            var trace = new List<string>();
            for (MethodDefinition? at = method; at is not null; at = parent[at]) trace.Add(at.FullName);
            trace.Reverse();return trace.ToArray();
        }
        File.WriteAllText(output, JsonSerializer.Serialize(new { schema = "HC_CONSERVATIVE_METHOD_CLOSURE_V1", assemblyPath, targetCodeExecuted = false, roots, reachableMethods = seen.Select(m=>m.FullName).Order().ToArray(), viiperPaths = viiper.Select(m => new { method = m.FullName, path = Trace(m) }), reflectionSites = reflection.Distinct().ToArray(), unresolved, limitations = new[] { "external libraries can call back through reflection", "external delegates other than rooted HC callbacks require review", "branch conditions are not interpreted", "all-device root set is intentionally over-broad", "no device or production equivalence is asserted" } },new JsonSerializerOptions { WriteIndented=true }));
        Console.WriteLine($"HC conservative closure: roots={roots.Count} reachable={seen.Count} viiperImports={viiper.Length} reflectionSites={reflection.Count}");return 0;
    }
    static IEnumerable<TypeDefinition> Flatten(IEnumerable<TypeDefinition> types)=>types.SelectMany(t=>new[]{t}.Concat(Flatten(t.NestedTypes)));
}
