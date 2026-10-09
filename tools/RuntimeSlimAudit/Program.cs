using Mono.Cecil;
using System.Security.Cryptography;
using System.Text.Json;
using System.Xml.Linq;

// Never load or execute inspected assemblies. Cecil reads metadata only.
if (args.Length == 2 && args[0] == "--selftest") return AuditSelfTest.Run(args[1]);
if (args.Length == 3 && args[0] == "--hc-closure") return HcClosure.Run(args[1], args[2]);
if (args.Length != 3) throw new ArgumentException("usage: RuntimeSlimAudit <payload-root> <json-out> <sdk-roots-xml-out>");
string root = Path.GetFullPath(args[0]);
string[] targets = ["Microsoft.Windows.SDK.NET", "HandheldCompanion", "iNKORE.UI.WPF.Modern", "libVIIPER"];
var files = new List<object>();
var edges = new List<Edge>();
var native = new List<object>();
var errors = new List<object>();
var skippedNativeBodies = new List<object>();
var sdkTypes = new SortedSet<string>(StringComparer.Ordinal);
foreach (string path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).Order())
{
    if (!path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && !path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) continue;
    string rel = Path.GetRelativePath(root, path).Replace('\\', '/');
    using var bytes = File.OpenRead(path);
    string sha = Convert.ToHexString(SHA256.HashData(bytes));
    files.Add(new { path = rel, bytes = bytes.Length, sha256 = sha });
    try
    {
        using var module = ModuleDefinition.ReadModule(path, new ReaderParameters { ReadingMode = ReadingMode.Deferred, ReadSymbols = false });
        foreach (var reference in module.GetTypeReferences())
        {
            string assembly = Scope(reference);
            if (assembly == "Microsoft.Windows.SDK.NET") sdkTypes.Add(reference.FullName);
            if (targets.Contains(assembly) && module.Assembly?.Name.Name != assembly) edges.Add(new(rel, "TYPE", "", reference.FullName, assembly, ""));
        }
        foreach (var reference in module.GetMemberReferences())
        {
            string assembly = Scope(reference.DeclaringType);
            if (targets.Contains(assembly) && module.Assembly?.Name.Name != assembly) edges.Add(new(rel, "MEMBER", "", reference.FullName, assembly, ""));
        }
        foreach (var type in AllTypes(module.Types))
        foreach (var method in type.Methods)
        {
            if (method.HasPInvokeInfo && method.PInvokeInfo is not null)
            {
                string import = method.PInvokeInfo.Module.Name;
                native.Add(new { consumer = rel, method = method.FullName, module = import, entryPoint = method.PInvokeInfo.EntryPoint });
                if (import.Contains("VIIPER", StringComparison.OrdinalIgnoreCase)) edges.Add(new(rel, "PINVOKE", method.FullName, method.PInvokeInfo.EntryPoint, "libVIIPER", import));
            }
            if (method.HasPInvokeInfo && method.PInvokeInfo is null) native.Add(new { consumer = rel, method = method.FullName, module = "MIXED_MODE_IMPORT_IN_NATIVE_PE_DIRECTORY", entryPoint = "" });
            if ((module.Attributes & ModuleAttributes.ILOnly) == 0) { skippedNativeBodies.Add(new { consumer = rel, method = method.FullName, reason = "MIXED_MODE_IL_BODY_NOT_SCANNED" }); continue; }
            if (!method.HasBody) continue;
            if (!method.IsIL || !method.IsManaged) { skippedNativeBodies.Add(new { consumer = rel, method = method.FullName, implAttributes = method.ImplAttributes.ToString() }); continue; }
            if (method.Body is null) { errors.Add(new { consumer = rel, method = method.FullName, error = "Managed IL body unexpectedly absent" }); continue; }
            foreach (var instruction in method.Body.Instructions)
            {
                if (instruction.Operand is MemberReference member)
                {
                    var declared = member is TypeReference tr ? tr : member.DeclaringType;
                    string assembly = Scope(declared);
                    if (targets.Contains(assembly) && module.Assembly?.Name.Name != assembly) edges.Add(new(rel, instruction.OpCode.Name, method.FullName, member.FullName, assembly, ""));
                }
                else if (instruction.Operand is string text && (targets.Any(t => text.Contains(t, StringComparison.OrdinalIgnoreCase)) || text.StartsWith("Windows.", StringComparison.Ordinal)))
                    edges.Add(new(rel, "STRING_NOT_CALL_PROOF", method.FullName, text, "DYNAMIC_REVIEW_REQUIRED", ""));
            }
        }
    }
    catch (BadImageFormatException) { /* Native DLL; explicitly reported in file inventory. */ }
    catch (Exception ex) { errors.Add(new { consumer = rel, error = ex.ToString() }); }
}
var doc = new XDocument(new XElement("linker", new XElement("assembly", new XAttribute("fullname", "Microsoft.Windows.SDK.NET"),
    sdkTypes.Select(t => new XElement("type", new XAttribute("fullname", t.Replace('/', '+')), new XAttribute("preserve", "all"))))));
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);
File.WriteAllText(args[1], JsonSerializer.Serialize(new { schema = "HC_SIZE_ORDER_METADATA_AUDIT_V1", generatedUtc = DateTime.UtcNow, root, readOnly = true, targetCodeExecuted = false, priority = targets, files, edges, pinvokes = native, sdkRootTypes = sdkTypes, skippedNativeBodies, errors, notProven = new[] { "complete reflection/callback closure", "device equivalence", "removal approval" } }, new JsonSerializerOptions { WriteIndented = true }));
doc.Save(args[2]);
Console.WriteLine($"metadata audit: files={files.Count} edges={edges.Count} sdkRootTypes={sdkTypes.Count} errors={errors.Count}");
return errors.Count == 0 ? 0 : 3;
static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> types) => types.SelectMany(t => new[] { t }.Concat(AllTypes(t.NestedTypes)));
static string Scope(TypeReference? type)
{
    if (type is null) return "UNKNOWN";
    if (type is TypeSpecification spec) return Scope(spec.ElementType);
    if (type.DeclaringType is not null) return Scope(type.DeclaringType);
    return type.Scope switch { AssemblyNameReference a => a.Name, ModuleDefinition m => m.Assembly?.Name.Name ?? m.Name, _ => type.Scope?.Name ?? "UNKNOWN" };
}
record Edge(string Consumer, string Kind, string Caller, string Target, string Assembly, string NativeModule);
