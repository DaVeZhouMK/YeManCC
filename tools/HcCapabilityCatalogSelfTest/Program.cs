// Executes the production catalog and actual Host route consumer; never Host Main or HC.
using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using YeManFanBackend.Capabilities;
if (args.Length != 3) throw new ArgumentException("Expected scratch Host DLL, source catalog JSON and result path");
var hostPath = Path.GetFullPath(args[0]); var catalogPath = Path.GetFullPath(args[1]); var resultPath = Path.GetFullPath(args[2]);
AssemblyLoadContext.Default.Resolving += (_, name) => {
 var candidate = Path.Combine(Path.GetDirectoryName(hostPath)!, name.Name + ".dll");
 return File.Exists(candidate) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(candidate) : null;
};
var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);
const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
var backend = host.GetType("YeManFanHost.RealHcBackend", throwOnError: true)!;
var resolve = backend.GetMethod("ResolveRouteForSelfTest", flags)!;
var routes = backend.GetProperty("FanRoutesForSelfTest", flags)!.GetValue(null)!;
var expectedRoutes = new Dictionary<string, object>(StringComparer.Ordinal);
foreach (var pair in (IEnumerable)routes) {
 var t = pair.GetType(); expectedRoutes.Add((string)t.GetProperty("Key")!.GetValue(pair)!, t.GetProperty("Value")!.GetValue(pair)!);
}
var rows = new List<object>(); var failures = new List<string>();
void Check(string name, bool ok) { rows.Add(new { name, ok }); if (!ok) failures.Add(name); }
object? Resolve(string? identity) => resolve.Invoke(null, new object?[] { identity });
using var catalog = JsonDocument.Parse(File.ReadAllBytes(catalogPath));
var devices = catalog.RootElement.GetProperty("devices").EnumerateArray().ToArray();
Check("all-92-source-classes", devices.Length == 92 && HcDeviceCapabilityCatalog.Devices.Count == 92);
Check("all-80-factory-classes", HcDeviceCapabilityCatalog.Devices.Count(d => d.Instantiable) == 80);
Check("all-70-existing-fan-routes", expectedRoutes.Count == 70);
var registered = expectedRoutes.Select(p => HcDeviceCapabilityCatalog.Find(p.Key)!.DeviceId).ToHashSet(StringComparer.Ordinal);
var successfulDispatches = 0;
foreach (var source in devices) {
 var id = source.GetProperty("deviceId").GetString()!; var descriptor = HcDeviceCapabilityCatalog.Find(id)!;
 Check(id + ":source-hash", descriptor.SourceSha256 == source.GetProperty("sourceSha256").GetString());
 foreach (var alias in source.GetProperty("aliases").EnumerateArray().Select(a => a.GetString()!)) {
  Check(id + ":exact-alias:" + alias, ReferenceEquals(HcDeviceCapabilityCatalog.Find(alias), descriptor)); var actual = Resolve(alias);
  if (registered.Contains(id)) {
   var expected = expectedRoutes.First(p => HcDeviceCapabilityCatalog.Find(p.Key)!.DeviceId == id).Value;
   Check(id + ":actual-host-consumer:" + alias, ReferenceEquals(expected, actual));
   if (ReferenceEquals(expected, actual)) successfulDispatches++;
  } else Check(id + ":no-inherited-fan-route:" + alias, actual is null);
 }
 Check(id + ":spoofed-namespace-denied", Resolve("Unreviewed.Vendor." + source.GetProperty("name").GetString()) is null);
}
foreach (var bad in new string?[] { null, "", " ", "ROGAlly", "hc.device.Unknown", "HandheldCompanion.Devices.ROGAllyX ", "HandheldCompanion.Devices.ROGAllyFake" }) Check("unknown-identity-denied:" + bad, Resolve(bad) is null);
var consumer = new object(); var mutable = new Dictionary<string, object> { ["hc.device.ROGAlly"] = consumer };
var binding = HcDeviceCapabilityCatalog.Bind("test-fan", mutable); mutable.Clear();
Check("binding-retains-consumer-after-caller-mutation", ReferenceEquals(binding.Resolve("HandheldCompanion.Devices.ROGAlly")?.Consumer, consumer));
Check("binding-keeps-domain", binding.Resolve("hc.device.ROGAlly")?.Domain == "test-fan");
Check("unregistered-subclass-stays-unavailable", binding.Resolve("hc.device.ROGAllyX") is null);
bool Reject(Action action) { try { action(); return false; } catch (InvalidOperationException) { return true; } }
Check("unknown-registration-rejected", Reject(() => HcDeviceCapabilityCatalog.Bind("fan", new Dictionary<string, object> { ["Unreviewed.Vendor.ROGAlly"] = consumer })));
Check("conflicting-alias-consumers-rejected", Reject(() => HcDeviceCapabilityCatalog.Bind("fan", new Dictionary<string, object> { ["hc.device.ROGAlly"] = consumer, ["HandheldCompanion.Devices.ROGAlly"] = new object() })));
Check("same-consumer-aliases-coalesce", HcDeviceCapabilityCatalog.Bind("fan", new Dictionary<string, object> { ["hc.device.ROGAlly"] = consumer, ["HandheldCompanion.Devices.ROGAlly"] = consumer }).Bindings.Count == 1);
Check("source-flag-does-not-disable-existing-compat-route", Resolve("hc.device.OneXPlayerX1AMD") is not null && !HcDeviceCapabilityCatalog.Find("hc.device.OneXPlayerX1AMD")!.DeclaredMinimum.HasFlag(HcSourceCapability.FanControl));
Check("asus-writer-consumes-stable-id", FanCapabilityCatalog.ResolveAsusCurve("hc.device.ROGAlly").DeviceId == "asus.rog-ally");
Check("asus-stable-id-keeps-actual-factory-type", FanCapabilityCatalog.ResolveAsusCurve("hc.device.ROGAlly").LegacyFactoryType == "HandheldCompanion.Devices.ROGAlly");
Check("asus-descriptor-exposes-unified-id", FanCapabilityCatalog.ResolveAsusCurve("hc.device.ROGAlly").CapabilityId == "hc.device.ROGAlly");
Check("HC-not-loaded", !AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"));
var result = new { status = failures.Count == 0 ? "PASS" : "FAIL", checks = rows.Count, successfulDispatches, sourceClasses = devices.Length, factoryClasses = 80, registeredFanRoutes = expectedRoutes.Count, HCAssemblyLoaded = false, hardwareIo = false, hostSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(hostPath))).ToLowerInvariant(), catalogSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(catalogPath))).ToLowerInvariant(), failures, rows };
Directory.CreateDirectory(Path.GetDirectoryName(resultPath)!); File.WriteAllText(resultPath, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }) + "\n");
Console.WriteLine(JsonSerializer.Serialize(new { result.status, result.checks, result.successfulDispatches, result.registeredFanRoutes, failures })); return failures.Count == 0 ? 0 : 1;
