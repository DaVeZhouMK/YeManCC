using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text.Json;
internal static class SupplementalAssemblyObserver
{
    static readonly object Gate = new();
    static readonly List<object> Targets = [];
    [ModuleInitializer] internal static void Start()
    {
        AppDomain.CurrentDomain.AssemblyLoad += (_, e) => Observe(e.LoadedAssembly);
        AppDomain.CurrentDomain.ProcessExit += (_, _) =>
        {
            var file = Environment.GetEnvironmentVariable("YMCC_FAN_SUPPLEMENTAL_ASSEMBLY_OBSERVATION");
            if (string.IsNullOrWhiteSpace(file)) return;
            lock (Gate) File.WriteAllText(file, JsonSerializer.Serialize(new { kind = "actual-loaded-supplemental-assemblies", pid = Environment.ProcessId, observedExit = true, targets = Targets }, new JsonSerializerOptions { WriteIndented = true }));
        };
    }
    static void Observe(Assembly assembly)
    {
        var name = assembly.GetName().Name;
        if (name is not ("YeManFanHost" or "YeManFanBackend" or "HandheldCompanion")) return;
        var location = assembly.Location;
        lock (Gate) Targets.Add(new { name, path = location, mvid = assembly.ManifestModule.ModuleVersionId, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(location))).ToLowerInvariant() });
    }
}
