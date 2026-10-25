using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

if (args.Length != 3) throw new ArgumentException("runtime-root sdk-path result-json");
var root = Path.GetFullPath(args[0]);
var context = new ProbeContext(root);
var sdk = context.LoadFromAssemblyPath(Path.GetFullPath(args[1]));
context.BlockNative(sdk);
var sdkTypes = sdk.GetTypes();
var types = sdkTypes.Select(t => new
{
    name = t.FullName,
    baseType = t.BaseType?.FullName,
    interfaces = t.GetInterfaces().Select(x => x.FullName).Order().ToArray(),
    members = t.GetMembers(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
        .Select(x => x.MemberType + ":" + x).Order().ToArray(),
}).OrderBy(x => x.name).ToArray();
var hc = context.LoadFromAssemblyPath(Path.Combine(root, "HandheldCompanion.dll")); context.BlockNative(hc);
var hcAllTypes = hc.GetTypes();
var deviceTypeNames = new[] { "HandheldCompanion.Devices.IDevice", "HandheldCompanion.Devices.ROGAlly", "HandheldCompanion.Devices.ClawA1M",
    "HandheldCompanion.Misc.FanProfile", "HandheldCompanion.Misc.PowerProfile", "HandheldCompanion.Devices.GPDWin4", "HandheldCompanion.Devices.OneXPlayerX1" };
var deviceSurfaces = deviceTypeNames.Select(n =>
{
    var t = hc.GetType(n, throwOnError: false);
    return new { name = n, present = t is not null, methods = t?.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static)
        .Select(m => m.ToString()).Order().ToArray() };
}).ToArray();
var resolvedFramework = new[] { typeof(System.Windows.Media.Color).Assembly, typeof(System.Windows.Threading.Dispatcher).Assembly }
    .Select(a => new { name = a.GetName().Name, location = a.Location }).ToArray();
var result = new { status = "passed-metadata-only", resolvedFramework, sdkTypes = types.Length, hcAllTypes = hcAllTypes.Length, types, deviceSurfaces,
    noInstanceConstructors = true, noHardwareAccess = true, nativeImportsBlocked = true };
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2]))!);
File.WriteAllText(args[2], JsonSerializer.Serialize(result));
Console.WriteLine(JsonSerializer.Serialize(new { result.status, result.sdkTypes, result.noHardwareAccess }));

sealed class ProbeContext : AssemblyLoadContext
{
    private readonly string root;
    private readonly HashSet<string> framework;
    public ProbeContext(string root) : base("sdk-metadata-probe")
    {
        this.root = root;
        framework = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "").Split(Path.PathSeparator)
            .Where(p => p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(), StringComparison.OrdinalIgnoreCase) ||
                p.Contains(Path.DirectorySeparatorChar + "Microsoft.WindowsDesktop.App" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            .Select(Path.GetFileNameWithoutExtension).Where(n => n is not null).ToHashSet(StringComparer.OrdinalIgnoreCase)!;
    }
    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name is not null && framework.Contains(name.Name)) return Default.LoadFromAssemblyName(name);
        var path = Path.Combine(root, name.Name + ".dll");
        if (!File.Exists(path)) throw new FileNotFoundException("Missing reviewed dependency", path);
        var a = LoadFromAssemblyPath(path); BlockNative(a); return a;
    }
    public void BlockNative(Assembly a) => NativeLibrary.SetDllImportResolver(a,
        (name, _, _) => throw new DllNotFoundException("Native import denied in metadata probe: " + name));
    protected override IntPtr LoadUnmanagedDll(string name) => throw new DllNotFoundException("Native import denied: " + name);
}
