using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;

// Metadata only: no Assembly.Load, dependency resolution, device construction or IO.
internal static class BinaryDeviceInventory
{
    public static object Read(string path)
    {
        var bytes = File.ReadAllBytes(path);
        using var stream = new MemoryStream(bytes, writable: false);
        using var pe = new PEReader(stream);
        var md = pe.GetMetadataReader();
        string Name(TypeDefinitionHandle h)
        {
            var t = md.GetTypeDefinition(h);
            return md.GetString(t.Namespace) + "." + md.GetString(t.Name);
        }
        bool Device(TypeDefinitionHandle h)
        {
            var seen = new HashSet<TypeDefinitionHandle>();
            while (!h.IsNil && seen.Add(h))
            {
                var t = md.GetTypeDefinition(h);
                if (md.GetString(t.Name) == "IDevice" && md.GetString(t.Namespace) == "HandheldCompanion.Devices") return true;
                if (t.BaseType.Kind != HandleKind.TypeDefinition) return false;
                h = (TypeDefinitionHandle)t.BaseType;
            }
            return false;
        }
        var types = md.TypeDefinitions.Where(Device).Select(h => new { name = md.GetString(md.GetTypeDefinition(h).Name), fullName = Name(h) }).OrderBy(t => t.fullName, StringComparer.Ordinal).ToArray();
        if (types.Length == 0 || types.GroupBy(t => t.name, StringComparer.Ordinal).Any(g => g.Count() != 1)) throw new InvalidDataException("Empty or ambiguous runtime device metadata");
        var assembly = md.GetAssemblyDefinition();
        return new { sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(), assemblyName = md.GetString(assembly.Name), assemblyVersion = assembly.Version.ToString(), types, assemblyLoaded = false };
    }
}
