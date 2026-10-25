using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

internal static class ControllerInventory
{
    private static string Code(SyntaxNode node) => node.ReplaceTrivia(node.DescendantTrivia(descendIntoTrivia: true), (_, _) => default).NormalizeWhitespace().ToFullString();

    internal static void Write(string sourceDirectory, string destination)
    {
        var root = Path.GetFullPath(sourceDirectory);
        var rows = new List<object>();
        var issues = new List<string>();
        var classCount = 0;
        foreach (var file in Directory.GetFiles(root, "*.cs", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
        {
            var bytes = File.ReadAllBytes(file);
            var tree = CSharpSyntaxTree.ParseText(File.ReadAllText(file), new CSharpParseOptions(LanguageVersion.Preview), file);
            issues.AddRange(tree.GetDiagnostics().Where(item => item.Severity == DiagnosticSeverity.Error).Select(item => item.ToString()));
            var syntax = tree.GetCompilationUnitRoot();
            var declarations = syntax.DescendantNodes().OfType<ClassDeclarationSyntax>().ToArray();
            classCount += declarations.Length;
            var memberAccesses = syntax.DescendantNodes().OfType<MemberAccessExpressionSyntax>().ToArray();
            string[] Names(string qualifier) => memberAccesses.Where(item => item.Expression.ToString() == qualifier)
                .Select(item => item.Name.Identifier.Text).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
            var classes = declarations.Select(declaration => new
            {
                name = declaration.Identifier.Text,
                @namespace = string.Join(".", declaration.Ancestors().OfType<BaseNamespaceDeclarationSyntax>().Reverse().Select(item => item.Name.ToString())),
                @abstract = declaration.Modifiers.Any(SyntaxKind.AbstractKeyword),
                bases = declaration.BaseList is null ? null : string.Join(", ", declaration.BaseList.Types.Select(item => item.Type.ToString())),
                assignments = declaration.DescendantNodes().OfType<AssignmentExpressionSyntax>()
                    .Where(item => item.Left.ToString() is "Capabilities" or "this.Capabilities")
                    .Select(item => new
                    {
                        expression = Code(item),
                        line = item.GetLocation().GetLineSpan().StartLinePosition.Line + 1,
                        constructor = item.Ancestors().OfType<ConstructorDeclarationSyntax>().Any(),
                        conditional = item.Ancestors().Any(parent => parent is IfStatementSyntax or SwitchSectionSyntax or LambdaExpressionSyntax)
                            || item.Right.DescendantNodesAndSelf().OfType<ConditionalExpressionSyntax>().Any()
                    }).ToArray(),
                methods = declaration.Members.OfType<MethodDeclarationSyntax>().Select(method => new
                {
                    name = method.Identifier.Text,
                    line = method.GetLocation().GetLineSpan().StartLinePosition.Line + 1,
                    sourceSha256 = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(Code(method)))).ToLowerInvariant(),
                    body = method.Body is not null ? Code(method.Body) : method.ExpressionBody is not null ? Code(method.ExpressionBody.Expression) : ""
                }).ToArray()
            }).ToArray();
            rows.Add(new
            {
                file = "Controllers/" + Path.GetRelativePath(root, file).Replace('\\', '/'),
                sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(),
                classes,
                literalCapabilities = Names("ControllerCapabilities"),
                sourceButtonsMentioned = Names("ButtonFlags")
            });
        }
        var result = new { sourceDirectory = root, fileCount = rows.Count, classDeclarationCount = classCount, files = rows, issues, assemblyLoaded = false, hardwareIo = false };
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(destination))!);
        File.WriteAllText(destination, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }) + "\n");
    }
}
