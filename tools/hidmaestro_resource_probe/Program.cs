using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;

if (args.Length is < 2 or > 3) throw new ArgumentException("HIDMaestro.Core.dll path, output directory and optional profile required");
var profileName = args.Length == 3 ? args[2] : "dualshock-4-v2";
if (profileName.Any(character => !char.IsAsciiLetterOrDigit(character) && character != '-')) throw new ArgumentException("invalid profile name");
var assemblyPath = Path.GetFullPath(args[0]);
var outputDirectory = Path.GetFullPath(args[1]);
Directory.CreateDirectory(outputDirectory);
var root = Path.GetDirectoryName(assemblyPath)!;
AssemblyLoadContext.Default.Resolving += (_, name) =>
{
    var dependency = Path.Combine(root, name.Name + ".dll");
    return File.Exists(dependency) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(dependency) : null;
};
var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath);
var rows = new List<object>();
foreach (var resourceName in assembly.GetManifestResourceNames().Where(n => n.Contains(profileName, StringComparison.OrdinalIgnoreCase)))
{
    await using var stream = assembly.GetManifestResourceStream(resourceName) ?? throw new InvalidOperationException(resourceName);
    var target = Path.Combine(outputDirectory, profileName + ".json");
    await using (var file = File.Create(target)) await stream.CopyToAsync(file);
    rows.Add(new { resourceName, output = target, bytes = new FileInfo(target).Length, sha256 = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(target))) });
}
Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { assembly = assembly.FullName, resources = rows }, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
