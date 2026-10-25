using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length == 2 && args[0] == "--resolve-one")
{
    string childExpected = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", RuntimeSharedDependencies.SharedDirectoryName, args[1] + ".dll"));
    var childAssembly = Assembly.Load(AssemblyName.GetAssemblyName(childExpected));
    if (childAssembly.Location != childExpected) throw new Exception("Resolver did not load independent shared copy.");
    Console.WriteLine(childAssembly.Location);
    return;
}
if (args.Length != 2) throw new ArgumentException("usage: <independent-deps-root> <new-fixture-root>");
string deps = Path.GetFullPath(args[0]);
string fixture = Path.GetFullPath(args[1]);
if (Directory.Exists(fixture)) throw new InvalidOperationException("Fixture root must be new.");
Directory.CreateDirectory(fixture);
string power = Path.Combine(fixture, "PowerControl");
string host = Path.Combine(power, "feature-assets", "virtual-gamepad");
string shared = Path.Combine(power, RuntimeSharedDependencies.SharedDirectoryName);
Directory.CreateDirectory(host);
Directory.CreateDirectory(shared);
foreach (string suffix in new[] { ".dll", ".deps.json", ".runtimeconfig.json" })
    File.Copy(Path.Combine(AppContext.BaseDirectory, "DependencyPolicySelfTest" + suffix), Path.Combine(host, "DependencyPolicySelfTest" + suffix));
var checks = new List<string>();
var binaries = new List<object>();
void Check(bool value, string label) { if (!value) throw new Exception(label); checks.Add(label); }
void Reject(Action action, string label)
{
    bool rejected = false;
    try { action(); } catch (Exception) { rejected = true; }
    Check(rejected, label);
}
var resolve = typeof(RuntimeSharedDependencies).GetMethod("Resolve", BindingFlags.NonPublic | BindingFlags.Static)!;
using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(deps, "manifest.json")));
foreach (var entry in manifest.RootElement.GetProperty("files").EnumerateArray())
{
    string source = Path.Combine(deps, entry.GetProperty("path").GetString()!);
    string name = Path.GetFileNameWithoutExtension(source);
    var identity = AssemblyName.GetAssemblyName(source);
    string expected = entry.GetProperty("sha256").GetString()!.ToUpperInvariant();
    Check(identity.FullName == entry.GetProperty("assemblyIdentity").GetString(), name + ": pinned assembly identity");
    using (var stream = File.OpenRead(source))
    using (var pe = new PEReader(stream))
    {
        var metadata = pe.GetMetadataReader();
        var references = metadata.AssemblyReferences.Select(h => {
            var a = metadata.GetAssemblyReference(h);
            return new { name = metadata.GetString(a.Name), version = a.Version.ToString() };
        }).ToArray();
        Check(!references.Any(a => a.name.Equals("HandheldCompanion", StringComparison.OrdinalIgnoreCase)), name + ": no HC assembly reference");
        binaries.Add(new { name, identity = identity.FullName, references });
    }
    if (!entry.GetProperty("sharedFallback").GetBoolean()) continue;
    string file = Path.Combine(shared, Path.GetFileName(source));
    byte[] original = File.ReadAllBytes(source);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, identity, expected), name + ": missing dependency rejected");
    File.WriteAllBytes(file, original);
    Check(RuntimeSharedDependencies.VerifiedPath(host, identity, expected) == file, name + ": independent path accepted");
    Reject(() => RuntimeSharedDependencies.VerifiedPath(fixture, identity, expected), name + ": unrelated host layout rejected");
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, identity, new string('0', 64)), name + ": wrong lock hash rejected");
    var wrongVersion = new AssemblyName(identity.FullName) { Version = new Version(0, 0, 0, 0) };
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, wrongVersion, expected), name + ": wrong version rejected");
    var wrongToken = new AssemblyName(identity.FullName); wrongToken.SetPublicKeyToken([1, 2, 3, 4, 5, 6, 7, 8]);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, wrongToken, expected), name + ": wrong signing token rejected");
    var wrongCulture = new AssemblyName(identity.FullName) { CultureName = "en-US" };
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, wrongCulture, expected), name + ": wrong culture rejected");
    byte[] corrupt = (byte[])original.Clone(); corrupt[^1] ^= 1; File.WriteAllBytes(file, corrupt);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, identity, expected), name + ": tamper rejected");
    File.WriteAllBytes(file, original);
    using (var child = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("dotnet")
    {
        UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
        ArgumentList = { Path.Combine(host, "DependencyPolicySelfTest.dll"), "--resolve-one", name }
    })!)
    {
        string childOutput = child.StandardOutput.ReadToEnd();
        string childError = child.StandardError.ReadToEnd();
        child.WaitForExit();
        Check(child.ExitCode == 0 && childOutput.Trim() == file, name + ": actual resolver loads independent copy " + childError);
    }
    // A matching original DLL left in the HC location cannot rescue a missing independent copy.
    File.Delete(file);
    string legacy = Path.Combine(power, "handheldcompanion-runtime", RuntimeSharedDependencies.RuntimeId);
    Directory.CreateDirectory(legacy);
    File.Copy(source, Path.Combine(legacy, Path.GetFileName(source)), overwrite: true);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host, identity, expected), name + ": legacy HC copy is not a fallback");
    File.WriteAllBytes(file, original);
}
foreach (string name in new[] { "HIDMaestro.Core", "HandheldCompanion", "USBIP", "UnlistedDependency" })
    Check(resolve.Invoke(null, [AssemblyLoadContext.Default, new AssemblyName(name)]) is null, name + ": no unlisted/driver SDK redirection");
// Junction creation requires no symbolic-link privilege on Windows. Verify directory redirection is rejected.
string redirected = Path.Combine(fixture, "redirected");
Directory.CreateDirectory(redirected);
string junctionPower = Path.Combine(redirected, "PowerControl");
string junctionHost = Path.Combine(junctionPower, "feature-assets", "virtual-gamepad");
Directory.CreateDirectory(junctionHost);
string junction = Path.Combine(junctionPower, RuntimeSharedDependencies.SharedDirectoryName);
using (var process = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("cmd.exe")
{
    UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
    ArgumentList = { "/d", "/c", "mklink", "/J", junction, shared }
})!)
{
    process.WaitForExit();
    if (process.ExitCode != 0) throw new IOException("Could not create the isolated junction fixture: " + process.StandardError.ReadToEnd());
}
Check((File.GetAttributes(junction) & FileAttributes.ReparsePoint) != 0, "fixture junction created");
string newtonsoft = Path.Combine(shared, "Newtonsoft.Json.dll");
Reject(() => RuntimeSharedDependencies.VerifiedPath(junctionHost, AssemblyName.GetAssemblyName(newtonsoft), Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(newtonsoft)))) , "redirected shared directory rejected");
Console.WriteLine(JsonSerializer.Serialize(new { status = "DEPENDENCY_POLICY_PASS", productStarted = false, deviceAccessed = false, checks, binaries }));
