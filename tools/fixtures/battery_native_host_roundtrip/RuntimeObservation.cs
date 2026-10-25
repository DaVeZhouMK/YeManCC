using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text.Json;
internal static class BatteryRuntimeObservation
{
    static readonly ConcurrentDictionary<string,string> Loaded=new(StringComparer.OrdinalIgnoreCase);
    static string Allowlist="",Receipt="";
    [ModuleInitializer] internal static void Begin()
    {
        Allowlist=Environment.GetEnvironmentVariable("YMCC_BATTERY_RUNTIME_ALLOWLIST")??throw new IOException("EXPLICIT_RUNTIME_ALLOWLIST_REQUIRED");
        Receipt=Environment.GetEnvironmentVariable("YMCC_BATTERY_RUNTIME_RECEIPT")??throw new IOException("EXPLICIT_RUNTIME_RECEIPT_REQUIRED");
        AppDomain.CurrentDomain.AssemblyLoad+=(_,e)=>Observe(e.LoadedAssembly);
        foreach(var a in AppDomain.CurrentDomain.GetAssemblies())Observe(a);
        AppDomain.CurrentDomain.ProcessExit+=(_,_)=>Finish();
    }
    static void Observe(Assembly a){if(!a.IsDynamic&&!string.IsNullOrEmpty(a.Location))Loaded[a.Location]=a.GetName().Name??"";}
    static void Finish()
    {
        try
        {
            var allowed=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(Allowlist))!;
            var records=Loaded.Select(p=>new {name=p.Value,path=p.Key,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p.Key))).ToLowerInvariant()}).ToArray();
            var unknown=records.Where(p=>!allowed.TryGetValue(Path.GetFullPath(p.path).ToLowerInvariant(),out var h)||h!=p.sha256).ToArray();
            if(unknown.Length!=0)Environment.ExitCode=2;
            File.WriteAllText(Receipt,JsonSerializer.Serialize(new {status=unknown.Length==0?"PASS":"FAIL",loaded=records,unknown,originalNativeImportGuardUnchanged=true},new JsonSerializerOptions {WriteIndented=true}));
        }
        catch(Exception e){Environment.ExitCode=2;File.WriteAllText(Receipt,JsonSerializer.Serialize(new {status="FAIL",error=e.Message}));}
    }
}
