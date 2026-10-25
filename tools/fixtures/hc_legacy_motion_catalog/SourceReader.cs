// Syntax-only HC declaration reader. Never references or executes HC assemblies.
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

static class Reader
{
    static readonly string[] Kinds = ["GyroMatrix", "AcceleroMatrix"];
    static string Leaf(string s) => s.Split('.').Last();
    static int Line(SyntaxNode n) => n.GetLocation().GetLineSpan().StartLinePosition.Line + 1;
    static string Text(SyntaxNode n) => n.WithoutTrivia().NormalizeWhitespace().ToFullString();
    static Exception Bad(string s) => new InvalidDataException(s);
    static ExpressionSyntax Unwrap(ExpressionSyntax e) => e is ParenthesizedExpressionSyntax p ? Unwrap(p.Expression) : e;
    static double Unit(ExpressionSyntax e)
    {
        e = Unwrap(e);
        double sign = 1;
        if (e is PrefixUnaryExpressionSyntax u && (u.IsKind(SyntaxKind.UnaryMinusExpression) || u.IsKind(SyntaxKind.UnaryPlusExpression)))
        { sign = u.IsKind(SyntaxKind.UnaryMinusExpression) ? -1 : 1; e = Unwrap(u.Operand); }
        if (e is not LiteralExpressionSyntax l || !l.IsKind(SyntaxKind.NumericLiteralExpression)) throw Bad("unknown unit expression: " + Text(e));
        double value = sign * Convert.ToDouble(l.Token.Value, CultureInfo.InvariantCulture);
        if (value != 1 && value != -1) throw Bad("non-unit sign: " + Text(e));
        return value;
    }
    static double[] Axis(ExpressionSyntax e)
    {
        if (e is not ObjectCreationExpressionSyntax o || o.Type.ToString() != "Vector3" || o.Initializer != null || o.ArgumentList?.Arguments.Count != 3)
            throw Bad("unknown Axis expression: " + Text(e));
        if (o.ArgumentList.Arguments.Any(a => a.NameColon != null || a.RefKindKeyword.RawKind != 0)) throw Bad("unsupported Axis arguments");
        return o.ArgumentList.Arguments.Select(a => Unit(a.Expression)).ToArray();
    }
    static char Letter(ExpressionSyntax e) => e is LiteralExpressionSyntax l && l.Token.Value is char c && "XYZ".Contains(c) ? c : throw Bad("unknown swap expression: " + Text(e));
    static int[] Swap(ExpressionSyntax e)
    {
        if (e is ImplicitObjectCreationExpressionSyntax implicitCreation && implicitCreation.ArgumentList.Arguments.Count == 0)
            e = SyntaxFactory.ObjectCreationExpression(SyntaxFactory.ParseTypeName("SortedDictionary<char,char>")).WithArgumentList(implicitCreation.ArgumentList).WithInitializer(implicitCreation.Initializer);
        if (e is not ObjectCreationExpressionSyntax o || o.Type is not GenericNameSyntax generic || generic.Identifier.Text != "SortedDictionary" || generic.TypeArgumentList.Arguments.Count != 2 || generic.TypeArgumentList.Arguments.Any(t => t is not PredefinedTypeSyntax p || !p.Keyword.IsKind(SyntaxKind.CharKeyword)) || (o.ArgumentList?.Arguments.Count ?? 0) != 0 || o.Initializer == null)
            throw Bad("unknown AxisSwap expression: " + Text(e));
        var pairs = new Dictionary<char, char>();
        foreach (var item in o.Initializer.Expressions)
        {
            if (item is not InitializerExpressionSyntax pair || pair.Expressions.Count != 2) throw Bad("unknown swap pair");
            if (!pairs.TryAdd(Letter(pair.Expressions[0]), Letter(pair.Expressions[1]))) throw Bad("duplicate swap key");
        }
        if (pairs.Count != 3 || pairs.Values.Distinct().Count() != 3) throw Bad("non-permutation swap");
        return "XYZ".Select(c => "XYZ".IndexOf(pairs[c])).ToArray();
    }
    public static Matrix Parse(ExpressionSyntax e)
    {
        InitializerExpressionSyntax? init;
        if (e is ImplicitObjectCreationExpressionSyntax i && i.ArgumentList.Arguments.Count == 0) init = i.Initializer;
        else if (e is ObjectCreationExpressionSyntax o && o.Type.ToString() == "IMUMatrix" && (o.ArgumentList?.Arguments.Count ?? 0) == 0) init = o.Initializer;
        else throw Bad("unknown matrix expression: " + Text(e));
        var matrix = new Matrix();
        var seen = new HashSet<string>();
        foreach (var item in init?.Expressions ?? default)
        {
            if (item is not AssignmentExpressionSyntax a || !a.IsKind(SyntaxKind.SimpleAssignmentExpression) || a.Left is not IdentifierNameSyntax name || !seen.Add(name.Identifier.Text)) throw Bad("unsupported matrix initializer");
            switch (name.Identifier.Text)
            {
                case "Axis": matrix.axis = Axis(a.Right); break;
                case "AxisSwap": matrix.swap = Swap(a.Right); break;
                default: throw Bad("unknown matrix member: " + name);
            }
        }
        return matrix;
    }
    static object Inventory(string root)
    {
        var classes = new List<Device>();
        var files = new List<object>();
        var factoryClasses = new SortedSet<string>(StringComparer.Ordinal);
        string factorySwitch = "";
        object? defaults = null;
        foreach (var file in Directory.GetFiles(Path.Combine(root, "Devices"), "*.cs", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
        {
            var relative = Path.GetRelativePath(root, file).Replace('\\', '/');
            var bytes = File.ReadAllBytes(file);
            var tree = CSharpSyntaxTree.ParseText(System.Text.Encoding.UTF8.GetString(bytes), new CSharpParseOptions(LanguageVersion.Preview), relative);
            if (tree.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error)) throw Bad("C# parse failed: " + relative);
            var unit = tree.GetCompilationUnitRoot();
            files.Add(new { path = relative, sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant() });
            var aliases = unit.Usings.Where(u => u.Alias != null).ToDictionary(u => u.Alias!.Name.Identifier.Text, u => u.Name!.ToString());
            foreach (var c in unit.DescendantNodes().OfType<ClassDeclarationSyntax>())
            {
                var d = new Device { name = c.Identifier.Text, path = relative, line = Line(c), parent = Leaf(c.BaseList?.Types.FirstOrDefault()?.Type.ToString() ?? "") };
                if (aliases.TryGetValue(d.parent, out var target)) d.parent = Leaf(target);
                var ctors = c.Members.OfType<ConstructorDeclarationSyntax>().Where(x => !x.Modifiers.Any(SyntaxKind.StaticKeyword)).ToArray();
                if (ctors.Length > 1 || ctors.Any(x => x.ParameterList.Parameters.Count != 0 || x.ExpressionBody != null || x.Initializer != null && (x.Initializer.IsKind(SyntaxKind.ThisConstructorInitializer) || x.Initializer.ArgumentList.Arguments.Count != 0)))
                    d.issues.Add("unsupported constructor overload/initializer");
                foreach (var field in c.Members.OfType<FieldDeclarationSyntax>().SelectMany(f => f.Declaration.Variables).Where(v => Kinds.Contains(v.Identifier.Text) && v.Initializer != null))
                    d.issues.Add("unsupported matrix field initializer at " + Line(field));
                foreach (var a in c.DescendantNodes().OfType<AssignmentExpressionSyntax>())
                {
                    if (a.Ancestors().OfType<ClassDeclarationSyntax>().First() != c) continue;
                    var lhs = a.Left.ToString();
                    if (!Kinds.Any(k => lhs.Contains(k, StringComparison.Ordinal))) continue;
                    try
                    {
                        var kind = Kinds.SingleOrDefault(k => lhs == k || lhs == "this." + k) ?? throw Bad("matrix member mutation");
                        var ctor = a.Ancestors().OfType<ConstructorDeclarationSyntax>().FirstOrDefault();
                        if (!a.IsKind(SyntaxKind.SimpleAssignmentExpression) || ctor == null || ctor.Modifiers.Any(SyntaxKind.StaticKeyword) || a.Parent is not ExpressionStatementSyntax statement || statement.Parent != ctor.Body)
                            throw Bad("matrix assignment outside unconditional constructor statement");
                        if (ctor.DescendantNodes().OfType<ReturnStatementSyntax>().Any(r => r.SpanStart < a.SpanStart && !r.Ancestors().Any(n => n is AnonymousFunctionExpressionSyntax or LocalFunctionStatementSyntax)))
                            throw Bad("matrix assignment after possible constructor return");
                        d.assignments.Add(new Assignment { kind = kind, line = Line(a), expression = Text(a.Right), matrix = Parse(a.Right) });
                    }
                    catch (InvalidDataException ex) { d.issues.Add(relative + ":" + Line(a) + ": " + ex.Message); }
                }
                foreach (var identifier in c.DescendantNodes().OfType<IdentifierNameSyntax>().Where(n => Kinds.Contains(n.Identifier.Text)))
                {
                    if (identifier.Ancestors().OfType<ClassDeclarationSyntax>().First() != c) continue;
                    ExpressionSyntax reference = identifier.Parent is MemberAccessExpressionSyntax member && member.Name == identifier && member.Expression is ThisExpressionSyntax ? member : identifier;
                    if (reference.Parent is AssignmentExpressionSyntax assignment && assignment.Left == reference && assignment.IsKind(SyntaxKind.SimpleAssignmentExpression)) continue;
                    d.issues.Add(relative + ":" + Line(identifier) + ": unsupported matrix reference/mutation");
                }
                if (d.name == "IDevice")
                {
                    var method = c.Members.OfType<MethodDeclarationSyntax>().Single(m => m.Identifier.Text == "GetCurrent");
                    var sw = method.DescendantNodes().OfType<SwitchStatementSyntax>().Single(s => s.Expression.ToString() == "ManufacturerName");
                    factorySwitch = Text(sw);
                    foreach (var creation in sw.DescendantNodes().OfType<ObjectCreationExpressionSyntax>())
                    {
                        var type = creation.Type.ToString();
                        factoryClasses.Add(Leaf(aliases.TryGetValue(type, out var resolved) ? resolved : type));
                    }
                }
                classes.Add(d);
            }
            foreach (var s in unit.DescendantNodes().OfType<StructDeclarationSyntax>().Where(s => s.Identifier.Text == "IMUMatrix"))
            {
                var ctor = s.Members.OfType<ConstructorDeclarationSyntax>().Single();
                var assignments = ctor.Body!.Statements.OfType<ExpressionStatementSyntax>().Select(e => e.Expression).OfType<AssignmentExpressionSyntax>().ToArray();
                var axis = Axis(assignments.Single(a => a.Left.ToString() == "Axis").Right);
                var swapExpr = assignments.Single(a => a.Left.ToString() == "AxisSwap").Right;
                // Target-typed new() in IMUMatrix's constructor has the declared dictionary type.
                var swap = Swap(swapExpr is ImplicitObjectCreationExpressionSyntax imp ? SyntaxFactory.ObjectCreationExpression(SyntaxFactory.ParseTypeName("SortedDictionary<char,char>")).WithArgumentList(imp.ArgumentList).WithInitializer(imp.Initializer) : swapExpr);
                if (!axis.SequenceEqual(new double[] { 1, 1, 1 }) || !swap.SequenceEqual(new[] { 0, 1, 2 })) throw Bad("IMUMatrix default changed");
                defaults = new { axis, swap, sourceClass = "IMUMatrix", line = Line(ctor), syntax = Text(s) };
            }
        }
        if (defaults == null || factorySwitch.Length == 0 || classes.Select(c => c.name).Distinct().Count() != classes.Count) throw Bad("missing/ambiguous type or factory");
        return new { schema = 1, files, classes, factoryClasses, factorySwitch, defaults };
    }
    public static void Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--faults")
        {
            var cases = JsonSerializer.Deserialize<List<Fault>>(File.ReadAllText(args[1]))!;
            var result = new List<object>();
            foreach (var f in cases)
            {
                bool rejected = false; string error = ""; Matrix? matrix = null;
                try
                {
                    var expression = SyntaxFactory.ParseExpression(f.expression);
                    if (expression.ContainsDiagnostics) throw Bad("invalid C# expression");
                    matrix = Parse(expression);
                    if (f.expected != null && (!matrix.axis.SequenceEqual(f.expected.axis) || !matrix.swap.SequenceEqual(f.expected.swap))) throw Bad("wrong parsed matrix: " + f.name);
                }
                catch (InvalidDataException e) { rejected = true; error = e.Message; }
                if (rejected != f.reject) throw Bad("fault expectation failed: " + f.name);
                result.Add(new { f.name, rejected, error, matrix });
            }
            Console.WriteLine(JsonSerializer.Serialize(result));
        }
        else if (args.Length == 3 && args[0] == "--source-faults")
        {
            var cases = JsonSerializer.Deserialize<List<SourceFault>>(File.ReadAllText(args[1]))!;
            var results = new List<object>();
            const string common = """
                using System; using System.Numerics; using System.Collections.Generic;
                public struct IMUMatrix {
                    public Vector3 Axis; public SortedDictionary<char,char> AxisSwap;
                    public IMUMatrix() { Axis = new Vector3(1,1,1); AxisSwap = new() {{'X','X'},{'Y','Y'},{'Z','Z'}}; }
                }
                public class IDevice {
                    public IMUMatrix GyroMatrix, AcceleroMatrix;
                    public IDevice() { GyroMatrix = new(); AcceleroMatrix = new(); }
                    public static IDevice GetCurrent() { string ManufacturerName = "test"; switch(ManufacturerName) { case "test": return new Child(); } return null; }
                }
                """;
            foreach (var f in cases)
            {
                if (f.name.Any(c => !char.IsAsciiLetterOrDigit(c) && c != '-')) throw Bad("bad fixture name");
                var dir = Path.Combine(Path.GetFullPath(args[2]), f.name, "Devices");
                Directory.CreateDirectory(dir);
                File.WriteAllText(Path.Combine(dir, "Types.cs"), common + "\n" + f.declaration);
                var inventory = JsonSerializer.SerializeToElement(Inventory(Path.GetDirectoryName(dir)!));
                var issues = inventory.GetProperty("classes").EnumerateArray().SelectMany(c => c.GetProperty("issues").EnumerateArray().Select(i => i.GetString())).ToArray();
                if ((issues.Length > 0) != f.reject) throw Bad("source fault expectation failed: " + f.name);
                results.Add(new { f.name, rejected = issues.Length > 0, issues });
            }
            Console.WriteLine(JsonSerializer.Serialize(results));
        }
        else if (args.Length == 1) Console.WriteLine(JsonSerializer.Serialize(Inventory(Path.GetFullPath(args[0]))));
        else throw Bad("usage: reader <HC source root> | --faults <json>");
    }
}
sealed class Matrix { public double[] axis { get; set; } = [1, 1, 1]; public int[] swap { get; set; } = [0, 1, 2]; }
sealed class Assignment { public string kind { get; set; } = ""; public int line { get; set; } public string expression { get; set; } = ""; public Matrix matrix { get; set; } = new(); }
sealed class Device { public string name { get; set; } = ""; public string parent { get; set; } = ""; public string path { get; set; } = ""; public int line { get; set; } public List<Assignment> assignments { get; } = []; public List<string> issues { get; } = []; }
sealed class Fault { public string name { get; set; } = ""; public string expression { get; set; } = ""; public bool reject { get; set; } public Matrix? expected { get; set; } }

sealed class SourceFault { public string name { get; set; } = ""; public string declaration { get; set; } = ""; public bool reject { get; set; } }
