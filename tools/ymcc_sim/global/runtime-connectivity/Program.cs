using System.IO;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using YeMan.SharedRuntime;

var options = new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
if (args.Length != 1) throw new ArgumentException("Expected configuration.json");
var config = JsonNode.Parse(File.ReadAllText(args[0]))!;
string Value(string name) => config[name]!.GetValue<string>();
var evidence = Value("evidenceDir");
var bootstrapStage = "compatibility.runtime-input";
AppDomain.CurrentDomain.UnhandledException += (_, eventArgs) => {
    if (eventArgs.ExceptionObject is not Exception failure) return;
    var rejection = new { schemaVersion = 1, status = "FAIL", stage = bootstrapStage,
        checks = new[] { new { id = "compatibility.required-runtime-interface", status = "FAIL",
            inputConsumerStateFeedback = new { input = "supplied runtime and Host API contract", consumer = bootstrapStage, state = "missing-or-invalid-interface",
                feedback = RuntimeDependencyContract.DescribeFailure(failure, bootstrapStage) } } },
        executedProductModules = Array.Empty<string>(), compatibility = (object?)null,
        remainingGaps = new[] { "required CLR/managed interface was not executable; no fallback accepted" } };
    File.WriteAllText(Path.Combine(evidence, "probe-result.json"), JsonSerializer.Serialize(rejection, options));
};
var runtime = Value("runtimeRoot");
var shared = Value("sharedRuntimeRoot");
var hostPath = Value("hostPath");
var manifestText = File.ReadAllText(Path.Combine(runtime, "HandheldCompanion.runtime.json"));
using var manifest = JsonDocument.Parse(manifestText);
var sharedFiles = manifest.RootElement.GetProperty("sharedDependencies").GetProperty("files").EnumerateArray().ToArray();
var probeFile = Value("probeAssembly") + ".dll";
if (!sharedFiles.Any(item => item.GetProperty("path").GetString() == probeFile)) throw new InvalidDataException("Probe library is not declared by manifest");
var sourceDisk = RuntimeDependencyContract.InspectDisk(Path.Combine(shared, probeFile));
var requested = new AssemblyName(sourceDisk.AssemblyIdentity);
var checks = new List<object>();
var baselines = new List<object>();
var contexts = new List<AssemblyLoadContext>();
var fixtureBytes = 0L;
const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
AssemblyLoadContext.Default.Resolving += (context, name) => {
    var path = Path.Combine(Path.GetDirectoryName(hostPath)!, name.Name + ".dll");
    return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
};
bootstrapStage = "compatibility.shipped-host-resolver";
var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);
var helper = host.GetType("YeMan.SharedRuntime.DeviceCoreSharedDependencies", true)!;
var baselineResolve = helper.GetMethod("Resolve", flags)!.CreateDelegate<Func<AssemblyLoadContext, AssemblyName, string, Assembly?>>();
var baselineValidate = helper.GetMethod("ValidateDeclaredManifest", flags)!.CreateDelegate<Func<string, JsonElement, HashSet<string>>>();
var candidateHostPath = Value("candidateHostPath");
var candidateHostContext = Context("actual-formal-Host");
candidateHostContext.Resolving += (context, name) => { var candidatePath = Path.Combine(Path.GetDirectoryName(candidateHostPath)!, name.Name + ".dll"); return File.Exists(candidatePath) ? context.LoadFromAssemblyPath(candidatePath) : null; };
bootstrapStage = "compatibility.formal-host-resolver";
var candidateHost = candidateHostContext.LoadFromAssemblyPath(candidateHostPath);
var candidateHelper = candidateHost.GetType("YeMan.SharedRuntime.DeviceCoreSharedDependencies", true)!;
var candidateResolve = candidateHelper.GetMethod("Resolve", flags)!.CreateDelegate<Func<AssemblyLoadContext, AssemblyName, string, Assembly?>>();
var candidateValidate = candidateHelper.GetMethod("ValidateDeclaredManifest", flags)!.CreateDelegate<Func<string, JsonElement, HashSet<string>>>();
bootstrapStage = "compatibility.formal-pinned-load-contract";
var candidateContract = candidateHost.GetType("YeMan.SharedRuntime.RuntimeDependencyContract", true)!;
var candidateLoadPinned = candidateContract.GetMethod("LoadPinned", flags)!.CreateDelegate<Func<AssemblyLoadContext, AssemblyName, string, string, Assembly>>();
var describe = candidateContract.GetMethod("DescribeFailure", flags)!.CreateDelegate<Func<Exception, string, object>>();
bool ContractError(Exception error, string code) => error.GetType().FullName == "YeMan.SharedRuntime.RuntimeDependencyFailureException" && (string?)error.GetType().GetProperty("Code", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(error) == code;
bootstrapStage = "compatibility.formal-initialization-stage";
var stageMethod = candidateHost.GetType("YeManFanHost.RealHcBackend", true)!.GetMethod("RunHcInitializationStage", flags) ?? throw new MissingMethodException("RealHcBackend", "RunHcInitializationStage");
var runStage = stageMethod.CreateDelegate<Action<string, Action, Action<string, object?>?>>();

bootstrapStage = "actual-load-and-fault-injection";
void Require(bool value, string code) { if (!value) throw new ProbeContractAssertion(code); }
JsonElement ProductionFailures(JsonElement detail) { Require(detail.TryGetProperty("dependencyFailures", out var failures), "PRODUCTION_DEPENDENCY_DIAGNOSTIC_FIELD_MISSING"); return failures; }
AssemblyLoadContext Context(string id) { var context = new AssemblyLoadContext("ymcc-runtime-" + id, true); contexts.Add(context); return context; }
string Fixture(string id)
{
    var power = Path.Combine(evidence, "fixtures", id, "PowerControl");
    var root = Path.Combine(power, "handheldcompanion-runtime", manifest.RootElement.GetProperty("runtimeId").GetString()!);
    var dest = Path.Combine(power, "ymcc-shared-runtime");
    Directory.CreateDirectory(root); Directory.CreateDirectory(dest);
    File.WriteAllText(Path.Combine(root, "HandheldCompanion.runtime.json"), manifestText);
    foreach (var item in sharedFiles)
    {
        var file = item.GetProperty("path").GetString()!;
        Require(file == Path.GetFileName(file), "UNSAFE_FIXTURE_FILE");
        var path = Path.Combine(shared, file);
        File.Copy(path, Path.Combine(dest, file)); fixtureBytes += new FileInfo(path).Length;
    }
    return root;
}
string SharedFile(string root) => Path.Combine(Directory.GetParent(Directory.GetParent(root)!.FullName)!.FullName, "ymcc-shared-runtime", probeFile);
object Loaded(Assembly assembly) => new { identity = assembly.FullName, moduleVersionId = assembly.ManifestModule.ModuleVersionId,
    locationFile = Path.GetFileName(assembly.Location), loadContext = AssemblyLoadContext.GetLoadContext(assembly)?.Name };
object PublicApi(Assembly assembly)
{
    var type = assembly.GetType("Newtonsoft.Json.JsonConvert", true)!;
    var method = type.GetMethod("SerializeObject", BindingFlags.Public | BindingFlags.Static, new[] { typeof(object) })
        ?? throw new MissingMethodException(type.FullName, "SerializeObject(object)");
    var result = method.Invoke(null, new object[] { "ymcc-runtime-connectivity" });
    Require((string?)result == "\"ymcc-runtime-connectivity\"", "ACTUAL_LIBRARY_API_RESULT_INVALID");
    return new { type = type.FullName, method = method.ToString(), output = result };
}
void Case(string id, string input, string consumer, Func<object> action)
{
    try { var detail = action(); checks.Add(new { id, status = "PASS", inputConsumerStateFeedback = new { source = "source-derived-fixture", input, consumer, state = "contract-satisfied", feedback = detail } }); }
    catch (Exception error) { checks.Add(new { id, status = "FAIL", inputConsumerStateFeedback = new { source = "source-derived-fixture", input, consumer, state = "contract-failed", feedback = new { assertionCode = (error as ProbeContractAssertion)?.Code, exceptionChain = describe(error, id) } } }); }
}
object Reject(Action action, Func<Exception, bool> predicate, string stage)
{
    Exception? observed = null;
    try { action(); } catch (Exception error) { observed = error; }
    Require(observed is not null, "EXPECTED_REJECTION_WAS_ACCEPTED");
    Require(predicate(observed!), "WRONG_REJECTION_CAUSE");
    return new { rejected = true, stage, exceptionChain = describe(observed!, stage) };
}
byte[] MutateModuleVersion(byte[] bytes)
{
    using var stream = new MemoryStream(bytes, false);
    using var pe = new PEReader(stream);
    var reader = pe.GetMetadataReader();
    var current = reader.GetGuid(reader.GetModuleDefinition().Mvid).ToByteArray();
    var hits = new List<int>();
    for (var i = 0; i <= bytes.Length - current.Length; i++) if (bytes.AsSpan(i, current.Length).SequenceEqual(current)) hits.Add(i);
    Require(hits.Count == 1, "MVID_FIXTURE_NOT_UNIQUE");
    var changed = (byte[])bytes.Clone();
    var replacement = SHA256.HashData(bytes).AsSpan(0, 16).ToArray();
    Require(!replacement.SequenceEqual(current), "MVID_FIXTURE_DID_NOT_CHANGE");
    replacement.CopyTo(changed, hits[0]);
    return changed;
}

Case("baseline.disk-and-manifest", "current supplied manifest plus declared shared payload", "shipped Host.ValidateDeclaredManifest", () => {
    var root = Fixture("baseline");
    Require(baselineValidate(root, manifest.RootElement).Count == sharedFiles.Length, "MANIFEST_NOT_CONSUMED");
    var assembly = baselineResolve(Context("baseline"), requested, root)!;
    Require(assembly is not null && assembly.FullName == sourceDisk.AssemblyIdentity, "CURRENT_CLR_LOAD_IDENTITY_MISMATCH");
    return new { disk = sourceDisk, loaded = Loaded(assembly!), api = PublicApi(assembly!) };
});
Case("product.valid-load-and-repeat", "approved current shared dependency; repeated request", "formal DeviceCoreSharedDependencies.Resolve + RuntimeDependencyContract", () => {
    var root = Fixture("positive");
    Require(candidateValidate(root, manifest.RootElement).Count == sharedFiles.Length, "FORMAL_MANIFEST_NOT_CONSUMED");
    var context = Context("positive");
    var first = candidateResolve(context, requested, root)!;
    var second = candidateResolve(context, requested, root)!;
    Require(ReferenceEquals(first, second), "REPEATED_LOAD_CHANGED_INSTANCE");
    return new { disk = sourceDisk, loaded = Loaded(first), repeatedRequestReusesVerifiedOrigin = true, api = PublicApi(first) };
});
Case("negative.production-layout", "valid file inputs beneath an incorrect production root", "formal shared resolver.VerifiedPath", () => {
    var wrong = Path.Combine(evidence, "fixtures", "wrong-root", "device-core"); Directory.CreateDirectory(wrong);
    return Reject(() => candidateResolve(Context("layout"), requested, wrong), error => error is FileNotFoundException, "dependency.layout");
});
Case("negative.missing-after-validation", "remove only declared copied dependency after successful manifest validation", "actual reflection/type initializer -> CLR Resolving -> formal resolver -> formal Host initialization stage", () => {
    var root = Fixture("missing");
    Require(candidateValidate(root, manifest.RootElement).Count == sharedFiles.Length, "MISSING_PRECONDITION_NOT_VALIDATED");
    File.Delete(SharedFile(root));
    var context = Context("missing");
    context.Resolving += (target, identity) => candidateResolve(target, identity, root);
    var diagnostic = new List<JsonElement>(); Exception? original = null;
    DeferredDependencyEntry.Load = () => { context.LoadFromAssemblyName(requested); };
    var rejection = Reject(() => runStage("assembly.load", () => {
        try { typeof(DeferredDependencyFixture).GetMethod(nameof(DeferredDependencyFixture.Trigger))!.Invoke(null, null); }
        catch (Exception error) { original = error; throw; }
    }, (eventName, detail) => diagnostic.Add(JsonSerializer.SerializeToElement(new { eventName, detail }, options))),
        error => ReferenceEquals(error, original) && error is TargetInvocationException { InnerException: TypeInitializationException } &&
            JsonSerializer.SerializeToElement(describe(error, "assembly.load"), options).EnumerateArray().Any(item => item.GetProperty("exceptionType").GetString() == typeof(FileNotFoundException).FullName), "assembly.load");
    DeferredDependencyEntry.Load = null;
    Require(diagnostic.Count == 1 && diagnostic[0].GetProperty("eventName").GetString() == "hc.initialization.failure" &&
        diagnostic[0].GetProperty("detail").GetProperty("stage").GetString() == "assembly.load", "PRODUCTION_DIAGNOSTIC_STAGE_MISSING");
    var dependencyFailures = ProductionFailures(diagnostic[0].GetProperty("detail"));
    Require(dependencyFailures.ValueKind == JsonValueKind.Array && dependencyFailures.EnumerateArray().Any(item => item.GetProperty("fileName").GetString() == probeFile), "PRODUCTION_MISSING_LIBRARY_DIAGNOSTIC_NOT_CONNECTED");
    // Persist allowlisted chain only; original product stacks are inspected in memory.
    return new { rejection, productionFailureEvent = "hc.initialization.failure", productionStage = "assembly.load", originalExceptionPreserved = true,
        failedLibrary = probeFile, productionDependencyFailures = dependencyFailures, contextLoadedDependency = context.Assemblies.Any(x => x.GetName().Name == requested.Name) };
});
Case("negative.requested-version", "correct pinned disk bytes with incompatible requested assembly version", "formal shared resolver identity check", () => {
    var root = Fixture("version"); var wrong = new AssemblyName(requested.FullName) { Version = new Version(65534, 0, 0, 0) };
    return new { requested = wrong.FullName, disk = sourceDisk, rejection = Reject(() => candidateResolve(Context("version"), wrong, root),
        error => error is FileLoadException, "dependency.identity") };
});
Case("negative.local-shadow", "approved identical shared file copied into local runtime routing slot", "formal production manifest consumer", () => {
    var root = Fixture("local-shadow"); File.Copy(SharedFile(root), Path.Combine(root, probeFile));
    return Reject(() => candidateValidate(root, manifest.RootElement), error => error is InvalidDataException, "manifest.shared-routing");
});
Case("negative.manifest-size", "manifest reports different byte identity while on-disk hash stays approved", "formal production manifest parser", () => {
    var root = Fixture("manifest-size"); var altered = JsonNode.Parse(manifestText)!;
    var entry = altered["sharedDependencies"]!["files"]!.AsArray().Single(x => x!["path"]!.GetValue<string>() == probeFile)!;
    entry["bytes"] = entry["bytes"]!.GetValue<long>() + 1;
    using var document = JsonDocument.Parse(altered.ToJsonString());
    return Reject(() => candidateValidate(root, document.RootElement), error => error is InvalidDataException, "manifest.identity");
});
Case("negative.disk-content", "append isolated DLL bytes without changing managed identity", "formal shared resolver pinned disk contract", () => {
    var root = Fixture("disk-content"); var file = SharedFile(root);
    using (var changed = new FileStream(file, FileMode.Append, FileAccess.Write)) changed.WriteByte(0x59);
    var disk = RuntimeDependencyContract.InspectDisk(file);
    Require(disk.AssemblyIdentity == sourceDisk.AssemblyIdentity && disk.Sha256 != sourceDisk.Sha256, "DISK_MUTATION_NOT_REAL");
    return new { disk, rejection = Reject(() => candidateResolve(Context("disk-content"), requested, root), error => error is InvalidDataException, "dependency.disk") };
});
Case("negative.preloaded-stale-image", "same managed identity but altered MVID loaded before current approved disk request", "shipped resolver before repair; formal resolver after repair", () => {
    var root = Fixture("preloaded"); var stale = MutateModuleVersion(File.ReadAllBytes(SharedFile(root)));
    var preloadedHash = Convert.ToHexString(SHA256.HashData(stale)).ToLowerInvariant();
    var stalePath = Path.Combine(evidence, "fixtures", "preloaded", "stale-" + probeFile); File.WriteAllBytes(stalePath, stale);
    Require(AssemblyName.GetAssemblyName(stalePath).FullName == requested.FullName, "STALE_IDENTITY_CHANGED");
    var oldContext = Context("pre-fix-stale"); using var oldStream = new MemoryStream(stale, false);
    var previous = oldContext.LoadFromStream(oldStream);
    Require(previous.ManifestModule.ModuleVersionId.ToString("D") != sourceDisk.ModuleVersionId, "STALE_MODULE_NOT_INJECTED");
    try {
        var returned = baselineResolve(oldContext, requested, root)!;
        baselines.Add(new { id = "preloaded-stale-image", accepted = true, sameStaleInstance = ReferenceEquals(previous, returned),
            loaded = Loaded(returned), disk = sourceDisk, preloadedHash, baselineResult = "disk-verified-but-stale-image-returned" });
    } catch (Exception error) { baselines.Add(new { id = "preloaded-stale-image", accepted = false, exceptionChain = describe(error, "dependency.loaded") }); }
    var context = Context("post-fix-stale"); using var stream = new MemoryStream(stale, false); var loaded = context.LoadFromStream(stream);
    var rejection = Reject(() => candidateResolve(context, requested, root),
        error => ContractError(error, "LOADED_ORIGIN_UNVERIFIED"), "dependency.loaded");
    return new { disk = sourceDisk, preloaded = Loaded(loaded), preloadedHash, rejection, productHookExecuted = true };
});
Case("negative.changed-approved-candidate", "load approved image, then supply newly approved same-identity candidate in same context", "formal RuntimeDependencyContract.LoadPinned", () => {
    var root = Fixture("candidate-change"); var context = Context("candidate-change");
    var first = candidateLoadPinned(context, requested, SharedFile(root), sourceDisk.Sha256);
    var candidateRoot = Path.Combine(evidence, "fixtures", "candidate-change", "candidate"); Directory.CreateDirectory(candidateRoot);
    var candidate = Path.Combine(candidateRoot, probeFile); File.WriteAllBytes(candidate, MutateModuleVersion(File.ReadAllBytes(SharedFile(root))));
    var disk = RuntimeDependencyContract.InspectDisk(candidate);
    return new { current = Loaded(first), candidate = disk, rejection = Reject(() => candidateLoadPinned(context, requested, candidate, disk.Sha256),
        error => ContractError(error, "LOADED_BYTES_CHANGED"), "dependency.loaded") };
});
Case("negative.invalid-managed-image", "non-PE bytes supplied with their own declared SHA", "formal RuntimeDependencyContract actual PE and CLR identity parser", () => {
    var path = Path.Combine(evidence, "fixtures", "invalid-managed-image.dll"); var bytes = new byte[] { 0x59, 0x4d, 0x43, 0x43 }; File.WriteAllBytes(path, bytes);
    var hash = Convert.ToHexString(SHA256.HashData(bytes));
    return Reject(() => candidateLoadPinned(Context("invalid-image"), requested, path, hash),
        error => ContractError(error, "RUNTIME_IMAGE_INVALID") && error.InnerException is BadImageFormatException, "dependency.disk");
});
byte[] MutateBooleanFormatter(byte[] bytes)
{
    using var stream = new MemoryStream(bytes, false); using var pe = new PEReader(stream);
    var reader = pe.GetMetadataReader();
    var typeHandle = reader.TypeDefinitions.Single(handle => { var type = reader.GetTypeDefinition(handle); return reader.GetString(type.Namespace) == "Newtonsoft.Json" && reader.GetString(type.Name) == "JsonConvert"; });
    var methods = reader.GetTypeDefinition(typeHandle).GetMethods().Where(handle => {
        var method = reader.GetMethodDefinition(handle);
        return reader.GetString(method.Name) == "ToString" && reader.GetBlobBytes(method.Signature).SequenceEqual(new byte[] { 0x00, 0x01, 0x0e, 0x02 });
    }).ToArray();
    Require(methods.Length == 1, "BOOLEAN_API_FIXTURE_SIGNATURE_CHANGED");
    var definition = reader.GetMethodDefinition(methods[0]); var rva = definition.RelativeVirtualAddress;
    var body = pe.GetMethodBody(rva); var il = body.GetILBytes()!;
    var branches = Enumerable.Range(0, il.Length).Where(i => il[i] is 0x2c or 0x2d).ToArray();
    Require(branches.Length == 1, "BOOLEAN_API_FIXTURE_IL_CHANGED");
    var section = pe.PEHeaders.SectionHeaders.Single(value => rva >= value.VirtualAddress && rva < value.VirtualAddress + value.VirtualSize);
    var bodyOffset = section.PointerToRawData + rva - section.VirtualAddress;
    var headerSize = (bytes[bodyOffset] & 3) == 2 ? 1 : (bytes[bodyOffset + 1] >> 4) * 4;
    var changed = (byte[])bytes.Clone(); var at = bodyOffset + headerSize + branches[0];
    Require(changed[at] == il[branches[0]], "BOOLEAN_API_FIXTURE_OFFSET_INVALID");
    changed[at] = changed[at] == 0x2c ? (byte)0x2d : (byte)0x2c;
    return changed;
}

Case("negative.preloaded-same-mvid-different-il", "same identity and MVID with a changed actual JSON Boolean formatter body", "shipped resolver evidence before repair; formal Host resolver after repair", () => {
    var root = Fixture("same-mvid-il"); var stale = MutateBooleanFormatter(File.ReadAllBytes(SharedFile(root)));
    var stalePath = Path.Combine(evidence, "fixtures", "same-mvid-il", "changed-" + probeFile); File.WriteAllBytes(stalePath, stale);
    var staleDisk = RuntimeDependencyContract.InspectDisk(stalePath);
    Require(staleDisk.AssemblyIdentity == sourceDisk.AssemblyIdentity && staleDisk.ModuleVersionId == sourceDisk.ModuleVersionId && staleDisk.Sha256 != sourceDisk.Sha256, "IL_MUTATION_IDENTITY_NOT_PRESERVED");
    string? Format(Assembly image) => (string?)image.GetType("Newtonsoft.Json.JsonConvert", true)!.GetMethod("ToString", new[] { typeof(bool) })!.Invoke(null, new object[] { true });
    var oldContext = Context("pre-fix-same-mvid"); using var oldStream = new MemoryStream(stale, false); var previous = oldContext.LoadFromStream(oldStream);
    Require(Format(previous) == "false", "MUTATED_REAL_API_DID_NOT_EXECUTE");
    try {
        var returned = baselineResolve(oldContext, requested, root)!;
        var snapshot = helper.GetMethod("Snapshot", flags)!.Invoke(null, null)!;
        baselines.Add(new { id = "preloaded-same-mvid-different-il", accepted = true, sameStaleInstance = ReferenceEquals(previous, returned),
            loaded = Loaded(returned), actualBooleanFormatterResult = Format(returned), expectedApprovedResult = "true", disk = sourceDisk,
            preloaded = staleDisk, productBindingEvidence = JsonSerializer.SerializeToElement(snapshot, options), baselineResult = "pinned-disk-hash-reported-for-different-executing-il" });
    } catch (Exception error) { baselines.Add(new { id = "preloaded-same-mvid-different-il", accepted = false, exceptionChain = describe(error, "dependency.loaded") }); }
    var context = Context("post-fix-same-mvid"); using var input = new MemoryStream(stale, false); var loaded = context.LoadFromStream(input);
    var actualDiagnostics = new List<JsonElement>();
    var rejection = Reject(() => runStage("assembly.load", () => { candidateResolve(context, requested, root); },
        (eventName, detail) => actualDiagnostics.Add(JsonSerializer.SerializeToElement(new { eventName, detail }, options))),
        error => ContractError(error, "LOADED_ORIGIN_UNVERIFIED"), "dependency.loaded");
    Require(actualDiagnostics.Count == 1, "PRODUCTION_CONTRACT_FAILURE_EVENT_MISSING");
    var failures = ProductionFailures(actualDiagnostics[0].GetProperty("detail"));
    Require(failures.EnumerateArray().Any(item => item.GetProperty("code").GetString() == "LOADED_ORIGIN_UNVERIFIED" &&
        item.GetProperty("assemblyName").GetString() == requested.Name && item.GetProperty("requestedIdentity").GetString() == requested.FullName), "PRODUCTION_CONTRACT_IDENTITY_DIAGNOSTIC_NOT_CONNECTED");
    return new { disk = sourceDisk, preloaded = staleDisk, alteredActualApiResult = Format(loaded), rejection,
        productionFailureEvent = actualDiagnostics[0].GetProperty("eventName").GetString(), productionDependencyFailures = failures, productHookExecuted = true };
});
var diskIdentities = sharedFiles.Select(item => {
    var file = item.GetProperty("path").GetString()!;
    return new { file, identity = RuntimeDependencyContract.InspectDisk(Path.Combine(shared, file)) };
}).ToArray();
var passed = checks.All(item => JsonSerializer.SerializeToElement(item).GetProperty("status").GetString() == "PASS");
var report = new { schemaVersion = 1, status = passed ? "PASS" : "FAIL", checks, beforeRepair = baselines,
    executedProductModules = new[] { "FanLab/DeviceCoreSharedDependencies.cs", "FanLab/RuntimeDependencyContract.cs", "YeManFanHost.dll:DeviceCoreSharedDependencies", "YeManFanHost.dll:RealHcBackend.RunHcInitializationStage", probeFile },
    compatibility = new { schemaVersion = 1, runtimeId = manifest.RootElement.GetProperty("runtimeId").GetString(),
        coreIdentity = RuntimeDependencyContract.InspectDisk(Path.Combine(runtime, "HandheldCompanion.dll")), shared = diskIdentities,
        requiredInterfaces = new[] { "DeviceCoreSharedDependencies.ValidateDeclaredManifest(string,JsonElement)", "DeviceCoreSharedDependencies.Resolve(AssemblyLoadContext,AssemblyName,string)",
            "RealHcBackend.RunHcInitializationStage(string,Action,Action<string,object>)", "Newtonsoft.Json.JsonConvert.SerializeObject(object)", "RuntimeDependencyContract.LoadPinned(AssemblyLoadContext,AssemblyName,string,string)" } },
    safety = new { hardwareAcceptance = false, managerConstructorsExecuted = false, managerStartsCalled = false, deviceOpenCalled = false,
        hostStarted = false, userConfigurationRead = false, actualClrLoad = true, fixtureBytesCopied = fixtureBytes, fixtureLibraryCount = sharedFiles.Length },
    remainingGaps = new[] { "physical EC/HID/OEM driver operations and actual fan RPM remain hardware-only", "13 manager construction is separate existing gate; not rerun or replaced", "mock Host public API is separate existing gate; no Host started here" },
    timestampUtc = DateTime.UtcNow.ToString("O") };
File.WriteAllText(Path.Combine(evidence, "probe-result.json"), JsonSerializer.Serialize(report, options));
Console.WriteLine(JsonSerializer.Serialize(new { report.status, checks = checks.Count, fixtureBytesCopied = fixtureBytes }, options));
foreach (var context in contexts) context.Unload();
Environment.ExitCode = passed ? 0 : 1;






sealed class ProbeContractAssertion(string code) : Exception(code) { internal string Code { get; } = code; }


static class DeferredDependencyEntry { internal static Action? Load; }
static class DeferredDependencyFixture { static DeferredDependencyFixture() { DeferredDependencyEntry.Load!(); } public static void Trigger() { } }

