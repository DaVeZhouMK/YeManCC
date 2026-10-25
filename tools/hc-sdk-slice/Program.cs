using Mono.Cecil;
using System.Text.Json;
using System.Xml.Linq;

static IEnumerable<TypeDefinition> Types(ModuleDefinition m) => m.Types.SelectMany(Flatten);
static IEnumerable<TypeDefinition> Flatten(TypeDefinition t) => new[] { t }.Concat(t.NestedTypes.SelectMany(Flatten));
static string Key(TypeReference t) => t.FullName.Replace('/', '+');
using var source = AssemblyDefinition.ReadAssembly(args[1]);
using var candidate = AssemblyDefinition.ReadAssembly(args[2]);
if (args[0] == "descriptor")
{
    var names = Types(candidate.MainModule).Where(t => t.Name != "<Module>").Select(t => t.FullName).ToHashSet();
    // Preserve all members of every kept SDK type. Include WinRT projection/ABI pairs by name,
    // and the whole sensor family used by IDevice.PullSensors rather than only its current calls.
    var originals = Types(source.MainModule).ToArray();
    if (args.Length > 4)
        foreach (var file in Directory.EnumerateFiles(args[4], "*.dll"))
        {
            AssemblyDefinition consumer;
            try { consumer = AssemblyDefinition.ReadAssembly(file); } catch (BadImageFormatException) { continue; }
            using (consumer)
                foreach (var tr in consumer.MainModule.GetTypeReferences())
                    if (tr.Scope.Name == source.Name.Name) names.Add(tr.FullName);
        }
    foreach (var t in originals)
        if (t.Namespace.StartsWith("Windows.Devices.Sensors", StringComparison.Ordinal) ||
            t.Namespace.StartsWith("ABI.Windows.Devices.Sensors", StringComparison.Ordinal)) names.Add(t.FullName);
    foreach (var t in originals)
        for (var parent = t.DeclaringType; parent is not null; parent = parent.DeclaringType)
            if (names.Contains(parent.FullName)) { names.Add(t.FullName); break; }
    var element = new XElement("assembly", new XAttribute("fullname", source.Name.Name),
        names.OrderBy(x => x).Select(n => new XElement("type", new XAttribute("fullname", n), new XAttribute("preserve", "all"))));
    new XDocument(new XElement("linker", element)).Save(args[3]);
    Console.WriteLine(JsonSerializer.Serialize(new { rootedTypes = names.Count, originalTypes = originals.Length }));
}
else if (args[0] == "verify")
{
    var kept = Types(candidate.MainModule).ToDictionary(Key);
    var originals = Types(source.MainModule).ToDictionary(Key);
    var removed = originals.Keys.Except(kept.Keys).ToArray();
    var missingReferences = new List<string>();
    foreach (var file in Directory.EnumerateFiles(args[3], "*.dll"))
    {
        AssemblyDefinition consumer;
        try { consumer = AssemblyDefinition.ReadAssembly(file); } catch (BadImageFormatException) { continue; }
        using (consumer)
        {
            foreach (var tr in consumer.MainModule.GetTypeReferences())
                if (tr.Scope.Name == source.Name.Name && !kept.ContainsKey(Key(tr)))
                    missingReferences.Add(Path.GetFileName(file) + ":" + Key(tr));
        }
    }
    var missingStaticMembers = new List<string>();
    foreach (var file in Directory.EnumerateFiles(args[3], "*.dll"))
    {
        AssemblyDefinition consumer;
        try { consumer = AssemblyDefinition.ReadAssembly(file); } catch (BadImageFormatException) { continue; }
        using (consumer)
        foreach (var member in consumer.MainModule.GetMemberReferences())
        {
            if (member.DeclaringType.Scope.Name != source.Name.Name) continue;
            if (!kept.TryGetValue(Key(member.DeclaringType), out var type)) continue;
            // Do not compare generic-instantiated parameter strings; the retained type must
            // keep at least the original method/field name plus method arity.
            bool present = member switch
            {
                MethodReference method => type.Methods.Any(m => m.Name == method.Name && m.Parameters.Count == method.Parameters.Count && m.GenericParameters.Count == method.GenericParameters.Count),
                FieldReference field => type.Fields.Any(f => f.Name == field.Name),
                _ => true,
            };
            if (!present) missingStaticMembers.Add(Path.GetFileName(file) + ":" + member.FullName);
        }
    }
    var output = new { originalBytes = new FileInfo(args[1]).Length, candidateBytes = new FileInfo(args[2]).Length,
        originalTypes = originals.Count, keptTypes = kept.Count, removedTypes = removed.Length,
        missingStaticTypeReferences = missingReferences, missingStaticMembers, reflectionCoverage = "descriptor rooted full members and sensors; runtime probes still required" };
    File.WriteAllText(args[4], JsonSerializer.Serialize(output, new JsonSerializerOptions { WriteIndented = true }));
    Console.WriteLine(JsonSerializer.Serialize(new { output.originalBytes, output.candidateBytes, output.keptTypes, missingStaticTypeReferences = missingReferences.Count }));
    if (missingReferences.Count > 0 || missingStaticMembers.Count > 0) Environment.ExitCode = 1;
}
else throw new ArgumentException("descriptor|verify required");
