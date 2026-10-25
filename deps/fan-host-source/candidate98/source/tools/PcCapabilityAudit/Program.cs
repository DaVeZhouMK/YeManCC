// Read-only syntax inventory. No HC assemblies are loaded and no device code is executed.
// Constructor flags are source declarations, never successful hardware probes.
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

if (args.Length is < 2 or > 3) throw new ArgumentException("usage: PcCapabilityAudit <HC Devices directory> <output.json> [pinned HC binary]");
var root = Path.GetFullPath(args[0]);
var files = Directory.GetFiles(root, "*.cs", SearchOption.AllDirectories).Order(StringComparer.Ordinal).ToArray();
var classes = new Dictionary<string, Entry>(StringComparer.Ordinal);
var issues = new List<string>();
var identities = new List<object>();
var hashes = new List<object>();
int Line(SyntaxNode node) => node.GetLocation().GetLineSpan().StartLinePosition.Line + 1;
string Leaf(string value) => value.Split('.').Last();
string Text(SyntaxNode node) => node.WithoutTrivia().NormalizeWhitespace().ToFullString();
string Sha(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value))).ToLowerInvariant();

foreach (var path in files)
{
    var text = File.ReadAllText(path);
    var syntax = CSharpSyntaxTree.ParseText(text, new CSharpParseOptions(LanguageVersion.Preview), path);
    foreach (var error in syntax.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error)) issues.Add(error.ToString());
    var unit = syntax.GetCompilationUnitRoot();
    var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
    hashes.Add(new { file = relative, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant() });
    var aliases = unit.Usings.Where(u => u.Alias != null).ToDictionary(u => u.Alias!.Name.Identifier.Text, u => u.Name!.ToString());
    foreach (var declaration in unit.DescendantNodes().OfType<ClassDeclarationSyntax>())
    {
        var name = declaration.Identifier.Text;
        var ns = string.Join(".", declaration.Ancestors().OfType<BaseNamespaceDeclarationSyntax>().Reverse().Select(n => n.Name.ToString()));
        var parent = declaration.BaseList?.Types.FirstOrDefault()?.Type.ToString() ?? "";
        if (aliases.TryGetValue(parent, out var target)) parent = target;
        var entry = new Entry { name = name, fullName = ns + "." + name, parent = Leaf(parent), file = relative, line = Line(declaration), sourceSha256 = Sha(Text(declaration)) };
        if (!classes.TryAdd(name, entry)) { issues.Add("duplicate class name: " + name); continue; }
        foreach (var method in declaration.Members.OfType<MethodDeclarationSyntax>())
        {
            entry.methods.Add(new { name = method.Identifier.Text, line = Line(method), signature = method.ReturnType + " " + method.Identifier + method.ParameterList.ToString(), sourceSha256 = Sha(Text(method)), isOverride = method.Modifiers.Any(SyntaxKind.OverrideKeyword),
                returnExpressions = method.DescendantNodes().OfType<ReturnStatementSyntax>().Select(r => r.Expression is null ? "" : Text(r.Expression)).ToArray(),
                constantBooleanReturn = (method.ExpressionBody?.Expression ?? (method.Body?.Statements.Count == 1 ? (method.Body.Statements[0] as ReturnStatementSyntax)?.Expression : null)) is LiteralExpressionSyntax literal && literal.Token.Value is bool boolean ? (bool?)boolean : null });
            if (name == "IDevice" && method.Identifier.Text == "GetCurrent")
                foreach (var creation in method.DescendantNodes().OfType<ObjectCreationExpressionSyntax>())
                {
                    var type = creation.Type.ToString();
                    if (aliases.TryGetValue(type, out var resolved)) type = resolved;
                    entry.createdClasses.Add(Leaf(type));
                    identities.Add(new { deviceClass = Leaf(type), line = Line(creation), context = creation.Ancestors().OfType<StatementSyntax>().Select(Text).FirstOrDefault(),
                        switches = creation.Ancestors().OfType<SwitchSectionSyntax>().Select(section => new { field = Text(((SwitchStatementSyntax)section.Parent!).Expression), values = section.Labels.OfType<CaseSwitchLabelSyntax>().Select(label => (label.Value as LiteralExpressionSyntax)?.Token.ValueText ?? Text(label.Value)).ToArray(), hasDefault = section.Labels.Any(label => label is DefaultSwitchLabelSyntax) }).Reverse().ToArray(),
                        conditions = creation.Ancestors().Where(n => n is IfStatementSyntax or ElseClauseSyntax or ConditionalExpressionSyntax).Select(n => n switch { IfStatementSyntax cond => Text(cond.Condition), ConditionalExpressionSyntax cond => Text(cond.Condition), _ => "else" }).ToArray() });
                }
        }
        foreach (var assignment in declaration.DescendantNodes().OfType<AssignmentExpressionSyntax>())
        {
            if (assignment.Ancestors().OfType<ClassDeclarationSyntax>().First() != declaration) continue;
            var field = Leaf(assignment.Left.ToString());
            var enclosing = assignment.Ancestors().OfType<MemberDeclarationSyntax>().FirstOrDefault();
            var constructor = enclosing is ConstructorDeclarationSyntax && !((ConstructorDeclarationSyntax)enclosing).Modifiers.Any(SyntaxKind.StaticKeyword);
            var ancestors = assignment.Ancestors().TakeWhile(n => n != enclosing).ToArray();
            var inCallback = ancestors.Any(n => n is AnonymousFunctionExpressionSyntax or LocalFunctionStatementSyntax);
            var conditions = ancestors.Where(n => n is IfStatementSyntax or ElseClauseSyntax or SwitchSectionSyntax or ForStatementSyntax or ForEachStatementSyntax or WhileStatementSyntax or CatchClauseSyntax)
                .Select(n => n switch { IfStatementSyntax i => Text(i.Condition), SwitchSectionSyntax s => string.Join(" ", s.Labels.Select(Text)), _ => n.Kind().ToString() }).Reverse().ToArray();
            var ternary = assignment.Right.DescendantNodesAndSelf().OfType<ConditionalExpressionSyntax>().Any();
            if (field is "Capabilities" or "DynamicLightingCapabilities")
            {
                var enumType = field == "Capabilities" ? "DeviceCapabilities" : "LEDLevel";
                var values = assignment.Right.DescendantNodesAndSelf().OfType<MemberAccessExpressionSyntax>().Where(m => m.Expression.ToString() == enumType).Select(m => m.Name.Identifier.Text).Distinct().ToArray();
                entry.operations.Add(new Operation(field, assignment.OperatorToken.Text, values, constructor && !inCallback, conditions.Length > 0 || ternary, conditions, Line(assignment), Text(assignment.Right)));
                if (constructor && values.Length == 0) issues.Add($"unparsed constructor capability expression: {relative}:{Line(assignment)}");
            }
            if (constructor && !inCallback && new[] { "nTDP", "cTDP", "GfxClock", "gfxClock", "cpuClock", "CpuClock", "Tjmax", "tjmax", "GyroMatrix", "AcceleroMatrix", "UseOpenLib", "ECDetails", "vendorId", "productIds", "BatteryBypassPresets" }.Contains(field))
                entry.properties.Add(new { field, value = Text(assignment.Right), line = Line(assignment), conditional = conditions.Length > 0 });
        }
        foreach (var call in declaration.DescendantNodes().OfType<InvocationExpressionSyntax>())
        {
            if (call.Ancestors().OfType<ClassDeclarationSyntax>().First() != declaration) continue;
            if (call.Expression.ToString().StartsWith("OEMChords.", StringComparison.Ordinal) ||
                call.Expression.ToString().StartsWith("this.OEMChords.", StringComparison.Ordinal))
                entry.oemChords.Add(new { expression = Text(call), line = Line(call) });
        }
    }
}
List<Entry> Chain(Entry item)
{
    var chain = new List<Entry>();
    var seen = new HashSet<string>();
    var current = item;
    while (true)
    {
        if (!seen.Add(current.name)) throw new InvalidDataException("cyclic inheritance: " + item.name);
        chain.Insert(0, current);
        if (!classes.TryGetValue(current.parent, out var next)) break;
        current = next;
    }
    return chain;
}
var devices = classes.Values.Where(e => Chain(e).Any(b => b.name == "IDevice")).OrderBy(e => e.name, StringComparer.Ordinal).ToArray();
var instantiated = classes["IDevice"].createdClasses.Where(classes.ContainsKey).ToHashSet(StringComparer.Ordinal);
foreach (var entry in devices)
{
    var chain = Chain(entry);
    entry.chain = chain.Select(e => e.name).ToArray();
    entry.instantiable = instantiated.Contains(entry.name);
    entry.family = entry.file.Contains('/') ? entry.file.Split('/')[0] : "Generic";
    foreach (var field in new[] { "Capabilities", "DynamicLightingCapabilities" })
    {
        var minimum = new HashSet<string>(); var possible = new HashSet<string>();
        foreach (var op in chain.SelectMany(e => e.operations).Where(op => op.field == field && op.constructor))
        {
            var names = op.values.Where(v => v != "None").ToHashSet();
            if (op.op == "=")
            {
                if (op.conditional) { minimum.Clear(); possible.UnionWith(names); }
                else { minimum = new(names); possible = new(names); }
            }
            else if (op.op is "|=" or "+=") { possible.UnionWith(names); if (!op.conditional) minimum.UnionWith(names); }
            else if (op.op == "-=" || (op.op == "&=" && op.expression.TrimStart().StartsWith('~')))
            { minimum.ExceptWith(names); if (!op.conditional) possible.ExceptWith(names); }
            else if (op.op == "&=") { minimum.IntersectWith(names); if (!op.conditional) possible.IntersectWith(names); }
            else issues.Add($"unsupported capability operator {entry.name}: {op.op}");
        }
        entry.declared[field] = new { minimum = minimum.Order().ToArray(), possible = possible.Order().ToArray() };
    }
}
if (devices.Length == 0 || instantiated.Count == 0) issues.Add("empty inventory");
var output = new { schemaVersion = 1, sourceRoot = root, evidenceLevel = "source-only; constructor declarations plus runtime mutations; no hardware probes", files = hashes, devices, identities, issues, runtime = args.Length == 3 ? BinaryDeviceInventory.Read(args[2]) : null };
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);
File.WriteAllText(args[1], JsonSerializer.Serialize(output, new JsonSerializerOptions { WriteIndented = true }) + "\n");
Console.WriteLine($"PC_CAPABILITY_INVENTORY files={files.Length} deviceClasses={devices.Length} instantiated={devices.Count(d => d.instantiable)} issues={issues.Count}");
if (issues.Count > 0) { foreach (var issue in issues) Console.Error.WriteLine(issue); return 1; }
return 0;

sealed record Operation(string field, string op, string[] values, bool constructor, bool conditional, string[] conditions, int line, string expression);
sealed class Entry
{
    public string name { get; set; } = "";
    public string fullName { get; set; } = "";
    public string parent { get; set; } = "";
    public string file { get; set; } = "";
    public int line { get; set; }
    public string sourceSha256 { get; set; } = "";
    public string family { get; set; } = "";
    public bool instantiable { get; set; }
    public string[] chain { get; set; } = [];
    public Dictionary<string, object> declared { get; } = new();
    public List<Operation> operations { get; } = [];
    public List<object> properties { get; } = [];
    public List<object> methods { get; } = [];
    public List<object> oemChords { get; } = [];
    [System.Text.Json.Serialization.JsonIgnore] public HashSet<string> createdClasses { get; } = [];
}
