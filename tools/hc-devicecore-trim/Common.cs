using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;

namespace HcDeviceCoreTrim;
static class Util
{
    public static readonly JsonSerializerOptions Json = new() { WriteIndented = true, PropertyNameCaseInsensitive = true };
    public static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    public static string HashFile(string path) => Hash(File.ReadAllBytes(path));
    public static void Save(string path, object value) { Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!); File.WriteAllText(path, JsonSerializer.Serialize(value, Json)); }
    public static IEnumerable<TypeDefinition> Types(ModuleDefinition m) => m.Types.SelectMany(Walk);
    static IEnumerable<TypeDefinition> Walk(TypeDefinition type) { yield return type; foreach (var t in type.NestedTypes.SelectMany(Walk)) yield return t; }
    public static AssemblyDefinition Read(string path, out DesktopFirstResolver resolver)
    {
        resolver = new DesktopFirstResolver(Path.GetDirectoryName(Path.GetFullPath(path))!);
        return AssemblyDefinition.ReadAssembly(path, new ReaderParameters { AssemblyResolver = resolver, ReadingMode = ReadingMode.Deferred });
    }
}
// WindowsBase in Microsoft.NETCore.App is a facade. Resolve WPF before application/core paths.
sealed class DesktopFirstResolver : DefaultAssemblyResolver
{
    readonly string? desktop;
    readonly Dictionary<string, AssemblyDefinition> desktopAssemblies = new(StringComparer.OrdinalIgnoreCase);
    public DesktopFirstResolver(string inputDirectory)
    {
        var root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "dotnet", "shared", "Microsoft.WindowsDesktop.App");
        desktop = Environment.GetEnvironmentVariable("YMCC_DESKTOP_RUNTIME");
        desktop ??= Directory.Exists(root) ? Directory.GetDirectories(root).Where(p => Version.TryParse(Path.GetFileName(p), out _)).OrderByDescending(p => Version.Parse(Path.GetFileName(p))).FirstOrDefault() : null;
        if (desktop != null) AddSearchDirectory(desktop);
        AddSearchDirectory(inputDirectory);
        AddSearchDirectory(System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory());
    }
    public override AssemblyDefinition Resolve(AssemblyNameReference name, ReaderParameters parameters)
    {
        if (desktop != null && name.Name is "WindowsBase" or "PresentationCore" or "PresentationFramework" or "System.Xaml" or "System.Windows.Controls.Ribbon")
        {
            if (desktopAssemblies.TryGetValue(name.Name, out var cached)) return cached;
            var file = Path.Combine(desktop, name.Name + ".dll");
            if (File.Exists(file)) return desktopAssemblies[name.Name] = AssemblyDefinition.ReadAssembly(file, new ReaderParameters { AssemblyResolver = this });
        }
        return base.Resolve(name, parameters);
    }
    protected override void Dispose(bool disposing)
    {
        foreach (var a in desktopAssemblies.Values) a.Dispose();
        base.Dispose(disposing);
    }
}
sealed record Removal(string Container, string Name, string Sha256, string Evidence);
sealed record TrimPolicy(string InputSha256, string Purpose, List<Removal> Removals, string? NewPublicKeyHex = null, string? SigningKeyPath = null, bool HeadlessUiAssets = false);

