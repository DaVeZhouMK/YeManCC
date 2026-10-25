// Exercise the current Host's actual declaration-binding gate; manifest inputs are inert, not a second fan model.
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
internal static class PayloadBindingProbe
{
    static readonly string[] ExpectedFiles = ["YeManFanHost.exe", "YeManFanHost.dll", "YeManFanBackend.dll", "YeManFanHost.deps.json", "YeManFanHost.runtimeconfig.json"];
    static readonly List<object> Results = [];
    public static int Main(string[] args)
    {
        try
        {
            var dll = Path.GetFullPath(args[0]); var output = Path.GetFullPath(args[1]); Directory.CreateDirectory(output);
            var resolver = new AssemblyDependencyResolver(dll);
            AssemblyLoadContext.Default.Resolving += (_, name) => resolver.ResolveAssemblyToPath(name) is { } p ? AssemblyLoadContext.Default.LoadFromAssemblyPath(p) : null;
            var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
            var flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
            host.GetType("YeManFanHost.Program", true)!.GetProperty("IsSelfTestProcess", flags)!.SetValue(null, true);
            var method = host.GetType("YeManFanHost.RealHcBackend", true)!.GetMethod("ValidateRuntimeManifestBinding", flags)!;
            var backendType = host.GetType("YeManFanHost.RealHcBackend", true)!;
            var routeLookup = backendType.GetMethod("ResolveRouteForSelfTest", flags)!;
            var oldRoute = routeLookup.Invoke(null, ["HandheldCompanion.Devices.GPD.GPDWin5"]);
            var exactRoute = routeLookup.Invoke(null, ["HandheldCompanion.Devices.GPDWin5"]);
            var exactRouteLookup = new { oldIdentityRejected = oldRoute is null, canonicalIdentityResolved = exactRoute is not null, productAliasAdded = false };
            if (!exactRouteLookup.oldIdentityRejected || !exactRouteLookup.canonicalIdentityResolved) throw new InvalidOperationException("EXACT_EXISTING_FACTORY_INPUT_CONTRACT");
            var failures = new List<string>();
            void Case(string name, Action<JsonObject, JsonObject> mutate, string? expectedError = null)
            {
                var root = Path.Combine(output, name); Directory.CreateDirectory(root); var runtimeRoot = Path.Combine(root, "runtime"); Directory.CreateDirectory(runtimeRoot);
                var payloadPath = Path.Combine(root, "YeManFanHost.payload.json"); var runtimePath = Path.Combine(runtimeRoot, "HandheldCompanion.runtime.json");
                var payload = new JsonObject { ["schemaVersion"] = 2, ["runtimeId"] = "inert-reviewed-runtime", ["runtimeManifest"] = "runtime/HandheldCompanion.runtime.json", ["files"] = new JsonArray(ExpectedFiles.Select(p => (JsonNode)new JsonObject { ["path"] = p }).ToArray()) };
                var runtime = new JsonObject { ["schemaVersion"] = 1, ["runtimeId"] = "inert-reviewed-runtime" };
                mutate(payload, runtime); File.WriteAllText(payloadPath, payload.ToJsonString()); File.WriteAllText(runtimePath, runtime.ToJsonString());
                string error = ""; object? value = null;
                try { value = method.Invoke(null, [root, payloadPath, runtimePath]); }
                catch (TargetInvocationException e) when (e.InnerException is not null) { error = e.InnerException.Message; }
                var ok = expectedError is null ? error == "" && Equals(value, "inert-reviewed-runtime") : error.StartsWith("Fan Host runtime binding is invalid: ", StringComparison.Ordinal) && error.Contains(expectedError, StringComparison.Ordinal);
                if (!ok) failures.Add(name + ": " + error);
                Results.Add(new { name, ok, expectedError, error, returnedRuntimeId = value, actualProductMethod = "RealHcBackend.ValidateRuntimeManifestBinding", manifestOnly = true });
            }
            Case("complete-declarations", (_, _) => { });
            Case("case-insensitive-declarations", (p, _) => { foreach (var entry in p["files"]!.AsArray()) entry!["path"] = entry["path"]!.GetValue<string>().ToUpperInvariant(); });
            Case("missing-files-array", (p, _) => p.Remove("files"), "files must be an array");
            Case("wrong-files-type", (p, _) => p["files"] = "not-an-array", "files must be an array");
            foreach (var omitted in ExpectedFiles)
                Case("omitted-" + omitted, (p, _) => { var entries = p["files"]!.AsArray(); entries.Remove(entries.Single(e => e!["path"]!.GetValue<string>() == omitted)); }, "omits required file: " + omitted);
            Case("backend-nested-path-is-not-root-backend", (p, _) => p["files"]!.AsArray().Single(e => e!["path"]!.GetValue<string>() == "YeManFanBackend.dll")!["path"] = "nested/YeManFanBackend.dll", "omits required file: YeManFanBackend.dll");
            Case("wrong-runtime-id", (_, r) => r["runtimeId"] = "other-runtime", "IDs differ");
            Case("wrong-runtime-reference", (p, _) => p["runtimeManifest"] = "other/HandheldCompanion.runtime.json", "differs from the selected runtime root");
            var hcLoaded = AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion");
            var result = new { ok = failures.Count == 0 && Results.Count == 12 && !hcLoaded, checks = Results.Count, exactRouteLookup, results = Results, failures, hostSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(), hcLoaded, productionHostMain = false, actualDllIntegrityValidation = false, physicalAcceptance = false, scope = "Actual current Host manifest declaration gate; no fake payload DLL is loaded, no HC initialization or hardware access" };
            File.WriteAllText(Path.Combine(output, "result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(JsonSerializer.Serialize(result)); return result.ok ? 0 : 1;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 2; }
    }
}
