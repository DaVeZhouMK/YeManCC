using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length != 2) throw new ArgumentException("<assembly> <json-output>");
using var stream = File.OpenRead(args[0]);
var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
stream.Position = 0;
using var pe = new PEReader(stream);
var reader = pe.GetMetadataReader();
var definition = reader.GetAssemblyDefinition();
string Name(string ns, string name) => string.IsNullOrEmpty(ns) ? name : ns + "." + name;
var assemblyReferences = reader.AssemblyReferences.Select(h => {
    var item = reader.GetAssemblyReference(h);
    return new { name = reader.GetString(item.Name), version = item.Version.ToString(),
        publicKeyOrToken = Convert.ToHexString(reader.GetBlobBytes(item.PublicKeyOrToken)).ToLowerInvariant() };
}).OrderBy(x => x.name).ToArray();
var nativeModuleReferences = Enumerable.Range(1, reader.GetTableRowCount(TableIndex.ModuleRef))
    .Select(index => reader.GetString(reader.GetModuleReference(MetadataTokens.ModuleReferenceHandle(index)).Name))
    .OrderBy(x => x).ToArray();
var typeReferences = reader.TypeReferences.Select(h => {
    var item = reader.GetTypeReference(h);
    return Name(reader.GetString(item.Namespace), reader.GetString(item.Name));
}).OrderBy(x => x).ToArray();
var typeDefinitions = reader.TypeDefinitions.Select(h => {
    var item = reader.GetTypeDefinition(h);
    return Name(reader.GetString(item.Namespace), reader.GetString(item.Name));
}).OrderBy(x => x).ToArray();
var blockedPrefixes = new[] { "HandheldCompanion", "HidLibrary", "System.Windows", "Windows.", "Microsoft.Windows.SDK", "WinRT", "Shared" };
bool Blocked(string value) => blockedPrefixes.Any(p => value.StartsWith(p, StringComparison.Ordinal));
var blockedTypeReferences = typeReferences.Where(Blocked).ToArray();
var blockedTypeDefinitions = typeDefinitions.Where(Blocked).ToArray();
var blockedAssemblyReferences = assemblyReferences.Where(x => Blocked(x.name) ||
    new[] { "PresentationCore", "PresentationFramework", "WindowsBase" }.Contains(x.name)).ToArray();
// Walk the user-string heap without loading or executing the inspected assembly.
var content = pe.GetMetadata().GetContent();
var start = reader.GetHeapMetadataOffset(HeapIndex.UserString);
var size = reader.GetHeapSize(HeapIndex.UserString);
var identityStrings = new HashSet<string>();
for (var position = 1; position < size; )
{
    var offset = position;
    var first = content[start + position++];
    int length;
    if ((first & 0x80) == 0) length = first;
    else if ((first & 0xC0) == 0x80) length = ((first & 0x3F) << 8) | content[start + position++];
    else if ((first & 0xE0) == 0xC0)
    {
        length = ((first & 0x1F) << 24) | (content[start + position] << 16) |
            (content[start + position + 1] << 8) | content[start + position + 2];
        position += 3;
    }
    else throw new BadImageFormatException("invalid user-string heap length");
    if (length == 0) continue;
    var value = reader.GetUserString(MetadataTokens.UserStringHandle(offset));
    if (value.Contains("HandheldCompanion", StringComparison.Ordinal)) identityStrings.Add(value);
    position += length;
}
var report = new { inspectedPath = Path.GetFullPath(args[0]), sha256 = hash,
    assemblyName = reader.GetString(definition.Name), assemblyVersion = definition.Version.ToString(),
    assemblyReferences, nativeModuleReferences, typeReferences, typeDefinitions, blockedAssemblyReferences,
    blockedTypeReferences, blockedTypeDefinitions, hcNamedStringLiterals = identityStrings.OrderBy(x => x).ToArray(),
    inspectedAssemblyLoaded = false, hardwareIo = false };
var json = JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
File.WriteAllText(args[1], json);
Console.WriteLine(json);
return blockedAssemblyReferences.Length + blockedTypeReferences.Length + blockedTypeDefinitions.Length == 0 ? 0 : 1;
