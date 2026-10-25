using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static class Program
{
    static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    static readonly Guid Sha256Id = new("8829d00f-11b8-4213-878b-770e8597ac16");
    static readonly Guid Sha1Id = new("ff1816ec-aa5e-4d10-87f7-6f4963833460");
    static void Need(bool value, string code) { if (!value) throw new InvalidDataException(code); }
    static string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
    static bool Inside(string root, string file) => Path.GetFullPath(file).StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    static void Regular(string file)
    {
        Need(File.Exists(file), "SOURCE_ARCHIVE_FILE_MISSING: " + file);
        for (var cursor = Path.GetFullPath(file); cursor is not null; cursor = Path.GetDirectoryName(cursor))
            Need((File.GetAttributes(cursor) & FileAttributes.ReparsePoint) == 0, "SOURCE_ARCHIVE_REPARSE_REFUSED");
    }
    static string CheckDocument(MetadataReader pdb, Document document, string file)
    {
        Regular(file);
        var algorithm = pdb.GetGuid(document.HashAlgorithm);
        var data = File.ReadAllBytes(file);
        var digest = algorithm == Sha256Id ? SHA256.HashData(data) : algorithm == Sha1Id ? SHA1.HashData(data) : throw new InvalidDataException("SOURCE_ARCHIVE_PDB_HASH_ALGORITHM");
        Need(digest.AsSpan().SequenceEqual(pdb.GetBlobBytes(document.Hash)), "SOURCE_ARCHIVE_DOCUMENT_CHECKSUM_MISMATCH: " + file);
        return Hash(file);
    }
    static object InspectModule(string name, string artifactRoot, Func<string, string, string> selectDocument, List<object> documents)
    {
        var dll = Path.Combine(artifactRoot, name + ".dll"); var pdbFile = Path.Combine(artifactRoot, name + ".pdb");
        Regular(dll); Regular(pdbFile);
        using var stream = File.OpenRead(dll); using var pe = new PEReader(stream);
        var metadata = pe.GetMetadataReader();
        Need(metadata.IsAssembly && metadata.GetString(metadata.GetAssemblyDefinition().Name) == name, "SOURCE_ARCHIVE_MODULE_IDENTITY");
        var debug = pe.ReadDebugDirectory().Where(x => x.Type == DebugDirectoryEntryType.CodeView).ToArray();
        Need(debug.Length == 1, "SOURCE_ARCHIVE_ONE_CODEVIEW_REQUIRED");
        var codeView = pe.ReadCodeViewDebugDirectoryData(debug[0]);
        using var pdbStream = File.OpenRead(pdbFile); using var provider = MetadataReaderProvider.FromPortablePdbStream(pdbStream);
        var pdb = provider.GetMetadataReader(); var id = new BlobContentId(pdb.DebugMetadataHeader!.Id);
        Need(codeView.Guid == id.Guid && debug[0].Stamp == id.Stamp && codeView.Age == 1, "SOURCE_ARCHIVE_DLL_PDB_MISMATCH");
        foreach (var handle in pdb.Documents)
        {
            var doc = pdb.GetDocument(handle); var original = pdb.GetString(doc.Name);
            var selected = selectDocument(name, original);
            var digest = CheckDocument(pdb, doc, selected);
            documents.Add(new { module = name, originalPath = original, selectedPath = selected, sha256 = digest,
                pdbHashAlgorithm = pdb.GetGuid(doc.HashAlgorithm).ToString(), pdbChecksum = Convert.ToHexString(pdb.GetBlobBytes(doc.Hash)).ToLowerInvariant() });
        }
        var optionRecords = pdb.CustomDebugInformation.Select(h => pdb.GetCustomDebugInformation(h))
            .Where(row => pdb.GetGuid(row.Kind) == new Guid("b5feec05-8cd0-4a83-96da-466284bb4bd8") && MetadataTokens.GetToken(row.Parent) == 1).ToArray();
        Need(optionRecords.Length == 1, "SOURCE_ARCHIVE_COMPILATION_OPTIONS_REQUIRED");
        var optionParts = System.Text.Encoding.UTF8.GetString(pdb.GetBlobBytes(optionRecords[0].Value)).Split('\0');
        Need(optionParts.Length % 2 == 1 && optionParts[^1].Length == 0, "SOURCE_ARCHIVE_COMPILATION_OPTIONS_SHAPE");
        var options = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < optionParts.Length - 1; i += 2) Need(options.TryAdd(optionParts[i], optionParts[i + 1]), "SOURCE_ARCHIVE_DUPLICATE_COMPILATION_OPTION");
        Need(options.TryGetValue("optimization", out var optimization) && optimization == "release", "SOURCE_ARCHIVE_RELEASE_BUILD_REQUIRED");
        var defines = options.GetValueOrDefault("define", "").Split(',');
        Need(defines.Contains("RELEASE") && !defines.Contains("DEBUG") && !defines.Contains("YEMAN_ASYNC_BOUNDARY_TEST"), "SOURCE_ARCHIVE_TEST_BUILD_REFUSED");
        var custom = pdb.CustomDebugInformation.Select(handle => {
            var row = pdb.GetCustomDebugInformation(handle);
            return new { parentToken = MetadataTokens.GetToken(row.Parent), kind = pdb.GetGuid(row.Kind).ToString(), valueBase64 = Convert.ToBase64String(pdb.GetBlobBytes(row.Value)) };
        }).ToArray();
        return new { name, dllSha256 = Hash(dll), pdbSha256 = Hash(pdbFile),
            mvid = metadata.GetGuid(metadata.GetModuleDefinition().Mvid).ToString(), codeViewGuid = codeView.Guid.ToString(), stamp = debug[0].Stamp,
            pdbIdGuid = id.Guid.ToString(), pdbIdStamp = id.Stamp, documentCount = pdb.Documents.Count, compilationOptions = options, customDebugRecords = custom };
    }
    static object Inspect(string repo, string artifacts, string buildRoot)
    {
        Need(Inside(buildRoot, artifacts), "SOURCE_ARCHIVE_ARTIFACT_OUTSIDE_BUILD");
        var documents = new List<object>(); var modules = new List<object>();
        string Select(string module, string original)
        {
            var path = Path.GetFullPath(original);
            var inSource = Inside(repo, path);
            var leaf = Path.GetFileName(path);
            var generated = leaf is "YeManFanHost.GlobalUsings.g.cs" or "YeManFanHost.AssemblyInfo.cs" or "YeManFanBackend.GlobalUsings.g.cs" or "YeManFanBackend.AssemblyInfo.cs" or ".NETCoreApp,Version=v10.0.AssemblyAttributes.cs";
            Need(inSource || Inside(buildRoot, path) && generated, "SOURCE_ARCHIVE_UNQUALIFIED_DOCUMENT: " + original);
            return path;
        }
        foreach (var name in new[] { "YeManFanHost", "YeManFanBackend" }) modules.Add(InspectModule(name, artifacts, Select, documents));
        return new { schemaVersion = 1, status = "PE_PDB_AND_CURRENT_SOURCE_MATCH", repoRoot = Path.GetFullPath(repo), artifactsRoot = Path.GetFullPath(artifacts), buildRoot = Path.GetFullPath(buildRoot), modules, documents };
    }
    static object VerifyArchive(string bundle, string expectedManifest)
    {
        var manifestFile = Path.Combine(bundle, "manifest.json"); Regular(manifestFile);
        Need(Hash(manifestFile) == expectedManifest.ToLowerInvariant(), "SOURCE_ARCHIVE_EXTERNAL_MANIFEST_PIN_MISMATCH");
        var manifest = JsonNode.Parse(File.ReadAllText(manifestFile))!;
        Need(manifest["schemaVersion"]!.GetValue<int>() == 1 && manifest["kind"]!.GetValue<string>() == "ymcc-host-component-source-bundle", "SOURCE_ARCHIVE_SCHEMA");
        var rows = manifest["files"]!.AsArray(); var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string Member(string relative)
        {
            Need(!Path.IsPathRooted(relative) && !relative.Contains(':') && !relative.Contains('\\') && relative.Split('/').All(p => p.Length > 0 && p is not "." and not ".."), "SOURCE_ARCHIVE_MEMBER_PATH");
            var file = Path.GetFullPath(Path.Combine(bundle, relative)); Need(Inside(bundle, file), "SOURCE_ARCHIVE_MEMBER_ESCAPE"); Regular(file); return file;
        }
        foreach (var row in rows)
        {
            var relative = row!["path"]!.GetValue<string>(); Need(relative != "manifest.json" && seen.Add(relative), "SOURCE_ARCHIVE_DUPLICATE_MEMBER");
            var file = Member(relative); Need(new FileInfo(file).Length == row["bytes"]!.GetValue<long>() && Hash(file) == row["sha256"]!.GetValue<string>(), "SOURCE_ARCHIVE_MEMBER_CHANGED: " + relative);
        }
        foreach (var file in Directory.EnumerateFiles(bundle, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(bundle, file).Replace('\\', '/'); Need(relative == "manifest.json" || seen.Contains(relative), "SOURCE_ARCHIVE_UNLISTED_FILE");
        }
        var sourceRows = manifest["sourceInputs"]!.AsArray(); var sourceNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var sourceByName = new Dictionary<string, JsonNode>(StringComparer.Ordinal);
        var boundSources = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in sourceRows)
        {
            var relative = row!["path"]!.GetValue<string>(); Need(sourceNames.Add(relative), "SOURCE_ARCHIVE_DUPLICATE_SOURCE");
            Need(row["archivePath"]!.GetValue<string>() == "source/" + relative, "SOURCE_ARCHIVE_SOURCE_PATH_ASSOCIATION");
            Need(Hash(Member(row["archivePath"]!.GetValue<string>())) == row["sha256"]!.GetValue<string>(), "SOURCE_ARCHIVE_SOURCE_CHANGED");
            sourceByName.Add(relative, row);
        }
        var observed = new List<object>(); var modules = new List<object>(); var moduleRows = manifest["modules"]!.AsArray();
        Need(moduleRows.Count == 2, "SOURCE_ARCHIVE_MODULE_COUNT");
        var moduleNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (var module in moduleRows)
        {
            var name = module!["name"]!.GetValue<string>(); Need((name is "YeManFanHost" or "YeManFanBackend") && moduleNames.Add(name), "SOURCE_ARCHIVE_MODULE_NAME");
            var docs = module["documents"]!.AsArray(); var counts = new HashSet<string>(StringComparer.Ordinal);
            string Select(string _, string original)
            {
                var matches = docs.Where(d => d!["originalPath"]!.GetValue<string>() == original).ToArray();
                Need(matches.Length == 1 && counts.Add(original), "SOURCE_ARCHIVE_DOCUMENT_SET");
                return Member(matches[0]!["archivePath"]!.GetValue<string>());
            }
            Need(Hash(Member("artifacts/" + name + ".dll")) == module["dllSha256"]!.GetValue<string>() && Hash(Member("artifacts/" + name + ".pdb")) == module["pdbSha256"]!.GetValue<string>(), "SOURCE_ARCHIVE_ARTIFACT_IDENTITY");
            var start = observed.Count;
            var inspected = InspectModule(name, Path.Combine(bundle, "artifacts"), Select, observed);
            modules.Add(inspected);
            var actualModule = JsonSerializer.SerializeToNode(inspected)!;
            Need(actualModule["mvid"]!.GetValue<string>() == module["mvid"]!.GetValue<string>() && actualModule["codeViewGuid"]!.GetValue<string>() == module["codeViewGuid"]!.GetValue<string>() && actualModule["stamp"]!.GetValue<uint>() == module["stamp"]!.GetValue<uint>(), "SOURCE_ARCHIVE_MODULE_METADATA_MISMATCH");
            foreach (var item in observed.Skip(start))
            {
                var actual = JsonSerializer.SerializeToNode(item)!;
                var advertised = docs.Single(d => d!["originalPath"]!.GetValue<string>() == actual["originalPath"]!.GetValue<string>())!;
                Need(new[] { "sha256", "pdbHashAlgorithm", "pdbChecksum" }.All(key => actual[key]!.GetValue<string>() == advertised[key]!.GetValue<string>()), "SOURCE_ARCHIVE_DOCUMENT_MANIFEST_MISMATCH");
                if (advertised["generated"]!.GetValue<bool>())
                {
                    Need(advertised["sourceRelative"] is null && advertised["archivePath"]!.GetValue<string>().StartsWith("generated/" + name + "/", StringComparison.Ordinal), "SOURCE_ARCHIVE_GENERATED_ASSOCIATION");
                }
                else
                {
                    var relative = advertised["sourceRelative"]!.GetValue<string>();
                    Need(sourceByName.TryGetValue(relative, out var sourceRow) && sourceRow["archivePath"]!.GetValue<string>() == advertised["archivePath"]!.GetValue<string>() && sourceRow["sha256"]!.GetValue<string>() == advertised["sha256"]!.GetValue<string>(), "SOURCE_ARCHIVE_DOCUMENT_SOURCE_ASSOCIATION");
                    boundSources.Add(relative);
                }
            }
            Need(counts.Count == docs.Count, "SOURCE_ARCHIVE_DOCUMENT_SET_INCOMPLETE");
        }
        Need(sourceByName.Keys.Where(p => p.EndsWith(".cs", StringComparison.Ordinal)).All(boundSources.Contains), "SOURCE_ARCHIVE_UNBOUND_COMPILE_SOURCE");
        return new { schemaVersion = 1, status = "PORTABLE_ARCHIVE_PE_PDB_DOCUMENTS_VERIFIED", manifestSha256 = expectedManifest, modules, documentChecks = observed.Count, physicalHardwareCalls = 0 };
    }
    public static int Main(string[] args)
    {
        try
        {
            object result = args.Length == 5 && args[0] == "inspect" ? Inspect(args[1], args[2], args[3]) :
                args.Length == 4 && args[0] == "verify" ? VerifyArchive(args[1], args[2]) : throw new ArgumentException("inspect repo artifacts buildRoot output | verify bundle manifestSha256 output");
            File.WriteAllText(args[^1], JsonSerializer.Serialize(result, JsonOptions) + Environment.NewLine); Console.WriteLine("SOURCE_ARCHIVE_METADATA_OK"); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}