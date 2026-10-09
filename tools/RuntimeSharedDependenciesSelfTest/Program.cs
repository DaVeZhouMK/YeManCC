using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length != 2) throw new ArgumentException("usage: <original-hc-runtime> <fixture-root-in-workspace>");
string source = Path.GetFullPath(args[0]);
string fixture = Path.GetFullPath(args[1]);
string allowed = Path.GetFullPath(@"G:\YeManCC-Work\Mainline\Build\Validation\HC-SIZE-ORDER-20261007") + Path.DirectorySeparatorChar;
if (!fixture.StartsWith(allowed, StringComparison.OrdinalIgnoreCase) || Directory.Exists(fixture))
    throw new InvalidOperationException("Test writes require a new directory strictly inside the specified workspace validation root.");
string host = Path.Combine(fixture, "PowerControl", "feature-assets", "virtual-gamepad");
string shared = Path.Combine(fixture, "PowerControl", "handheldcompanion-runtime", RuntimeSharedDependencies.RuntimeId);
Directory.CreateDirectory(host); Directory.CreateDirectory(shared);
var checks = new List<string>();
void Check(bool value, string label) { if (!value) throw new Exception(label); checks.Add(label); }
void Reject(Action action, string label) { bool rejected=false; try { action(); } catch (Exception) { rejected=true; } Check(rejected,label); }
foreach (string name in new[] { "Newtonsoft.Json", "Nefarius.Utilities.DeviceManagement" })
{
    string original = Path.Combine(source,name+".dll");
    string file = Path.Combine(shared,name+".dll");
    byte[] bytes = File.ReadAllBytes(original);
    string hash = Convert.ToHexString(SHA256.HashData(bytes));
    var identity = AssemblyName.GetAssemblyName(original);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host,identity,hash),name+": missing shared DLL fail-closed");
    File.WriteAllBytes(file,bytes);
    Check(RuntimeSharedDependencies.VerifiedPath(host,identity,hash)==file,name+": exact stock hash/identity/path accepted");
    Reject(() => RuntimeSharedDependencies.VerifiedPath(fixture,identity,hash),name+": malformed package layout rejected");
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host,identity,new string('0',64)),name+": different content hash rejected");
    var wrongVersion = new AssemblyName(identity.FullName) { Version = new Version(0,0,0,0) };
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host,wrongVersion,hash),name+": version mismatch rejected");
    var wrongToken = new AssemblyName(identity.FullName);wrongToken.SetPublicKeyToken([1,2,3,4,5,6,7,8]);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host,wrongToken,hash),name+": signing token mismatch rejected");
    byte[] corrupt = (byte[])bytes.Clone();corrupt[^1]^=1;File.WriteAllBytes(file,corrupt);
    Reject(() => RuntimeSharedDependencies.VerifiedPath(host,identity,hash),name+": payload tamper rejected before binding");
    File.WriteAllBytes(file,bytes);
    Check(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file)))==hash,name+": fixture restored exactly");
}
var resolver = typeof(RuntimeSharedDependencies).GetMethod("Resolve",BindingFlags.NonPublic|BindingFlags.Static)!;
Check(resolver.Invoke(null,[AssemblyLoadContext.Default,new AssemblyName("HIDMaestro.Core")]) is null,"resolver must not redirect HIDMaestro/driver SDK");
Check(resolver.Invoke(null,[AssemblyLoadContext.Default,new AssemblyName("UnlistedDependency")]) is null,"resolver cannot redirect unlisted assemblies");
Console.WriteLine(JsonSerializer.Serialize(new { status="SHARED_DEPENDENCY_POLICY_SOFTWARE_PASS", hardwareWrites=false, deviceAccessed=false, checks, fixture },new JsonSerializerOptions{WriteIndented=true}));
