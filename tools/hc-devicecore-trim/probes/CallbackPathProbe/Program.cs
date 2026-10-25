using System.IO;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length != 4) throw new ArgumentException("hc-path expected-sha256 golden-json-or-dash output-json");
var checks = new List<string>();
void Check(bool condition, string name) { if (!condition) throw new InvalidOperationException(name); checks.Add(name); }
using var hc = new HcBinaryAdapter(args[0],args[1]);
using var golden = args[2] == "-" ? null : JsonDocument.Parse(File.ReadAllText(args[2]));
var samples = new Dictionary<string,double[]>
{
    ["normal"] = new double[] { 0,0,10,20,30,40,50,60,70,80,100 },
    ["edited"] = new double[] { 0,0,10,20,30,47,50,60,70,80,100 },
    ["bounds-and-monotonic"] = new double[] { 100,0,-1,500,5,5,30,0,99.8,100,100 },
    ["malformed-default"] = new double[] { 20,40 },
};
var packets = new Dictionary<string,string[]>();
foreach (var sample in samples)
{
    var actual = hc.ApplySoftware(sample.Value);
    Check(actual.Count == 3, sample.Key + ": actual callback three writes");
    packets[sample.Key] = actual.Select(Convert.ToHexString).ToArray();
    if (golden is not null)
        for (var i=0;i<3;i++) Check(golden.RootElement.GetProperty("packets").GetProperty(sample.Key)[i].GetString() == packets[sample.Key][i],
            sample.Key + ": packet " + i + " identical");
    Check(hc.HandleReleased, sample.Key + ": handle released");
}
var cpu = Convert.ToHexString(hc.DefaultCpuCurve); var gpu = Convert.ToHexString(hc.DefaultGpuCurve);
if (golden is not null)
{
    Check(golden.RootElement.GetProperty("defaultCpu").GetString() == cpu, "default CPU table unchanged");
    Check(golden.RootElement.GetProperty("defaultGpu").GetString() == gpu, "default GPU table unchanged");
}
var result = new { status="passed", mode=golden is null?"golden-capture":"candidate-compare", count=checks.Count, checks, packets,
    defaultCpu=cpu, defaultGpu=gpu, binarySha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(args[0]))),
    hardwareAccess=false, noDeviceConstructors=true, realHcBinary=true, loadApi="AssemblyLoadContext.LoadFromAssemblyPath", allGetTypes=hc.AllTypeCount, nativeImportsBlocked=true };
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[3]))!);
File.WriteAllText(args[3],JsonSerializer.Serialize(result,new JsonSerializerOptions { WriteIndented=true }));
Console.WriteLine(JsonSerializer.Serialize(new { result.status, result.mode, result.count, result.hardwareAccess }));

