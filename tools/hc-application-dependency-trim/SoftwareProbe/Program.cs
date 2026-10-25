using System.IO;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Security.Cryptography;

if(args.Length is not (3 or 4))throw new ArgumentException("runtime-root repository-root report-json");
string root=Path.GetFullPath(args[0]),repo=Path.GetFullPath(args[1]),report=Path.GetFullPath(args[2]);
string scratch=Path.Combine(Path.GetDirectoryName(report)!,Path.GetFileNameWithoutExtension(report)+"-files");Directory.CreateDirectory(scratch);
var results=new List<object>();var loads=new List<object>();int denied=0;Exception? failure=null;
var context=new SoftwareContext(root,loads,()=>denied++,args.Length==4?args[3]:null);
try {
    foreach(var (name,configPath) in new[]{("YeManFanHost","FanLab/real-host/YeManFanHost.json"),("YeManLightSetter","FanLab/LightSetter/YeManLightSetter.json")}) {
        var config=JsonNode.Parse(File.ReadAllText(Path.Combine(repo,configPath)))!;
        string path=Path.Combine(scratch,name+".log");
        config["Serilog"]!["WriteTo"]![0]!["Args"]!["path"]=path;
        File.WriteAllText(Path.Combine(scratch,name+".json"),config.ToJsonString());
    }
    Directory.SetCurrentDirectory(scratch);AppContext.SetData("APP_CONTEXT_BASE_DIRECTORY",scratch);
    using(context.EnterContextualReflection()) {
        var shared=context.LoadFromAssemblyPath(Path.Combine(root,"Shared.dll"));context.BlockNative(shared);
        var log=shared.GetType("HandheldCompanion.Shared.LogManager",true)!;
        foreach(string name in new[]{"YeManFanHost","YeManLightSetter"}) {
            log.GetMethod("Initialize")!.Invoke(null,[name]);
            foreach(string level in new[]{"LogInformation","LogWarning","LogError","LogDebug","LogCritical"}) {
                var m=log.GetMethod(level);if(m==null)continue;
                m.Invoke(null,["YMCC application dependency probe {0}",new object[]{name+"-"+level}]);
                // Also cover the existing literal-brace trace fix with an empty argument array.
                m.Invoke(null,["literal { braces }",Array.Empty<object>()]);
                results.Add(new{name,level,status="invoked-real-Shared-logger"});
            }
        }
        // Flush the real configured sink without initiating telemetry or network requests.
        var serilog=context.Assemblies.First(a=>a.GetName().Name=="Serilog");
        serilog.GetType("Serilog.Log")?.GetMethod("CloseAndFlush",BindingFlags.Public|BindingFlags.Static,[])?.Invoke(null,null);
    }
    foreach(string name in new[]{"YeManFanHost","YeManLightSetter"}) {
        var files=Directory.GetFiles(scratch,name+"*.log");
        if(files.Length!=1)throw new InvalidDataException("Expected real log sink output for "+name);
        using var stream=new FileStream(files[0],FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete); using var reader=new StreamReader(stream); string text=reader.ReadToEnd();
        foreach(string level in new[]{"LogInformation","LogWarning","LogError","LogCritical"}) if(!text.Contains(name+"-"+level,StringComparison.Ordinal))throw new InvalidDataException("Missing configured log output "+name+"-"+level);
        if(!text.Contains("literal { braces }",StringComparison.Ordinal))throw new InvalidDataException("Literal braces lost");
        results.Add(new{name,status="real-file-contents-verified",bytes=new FileInfo(files[0]).Length});
    }
    if(denied!=0)throw new InvalidOperationException("Unexpected native import");
} catch(Exception ex) {failure=ex is TargetInvocationException {InnerException:not null} tie?tie.InnerException:ex;}
var record=new{status=failure==null?"passed":"failed",mode="real Shared.LogManager Initialize + both production JSON configurations; only sink path redirected",results,loads,nativeAttempts=denied,networkRequests=false,deviceConstructors=false,hostStarted=false,exception=failure?.ToString()};
File.WriteAllText(report,JsonSerializer.Serialize(record,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{record.status,count=results.Count,record.nativeAttempts}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}

sealed class SoftwareContext(string root,List<object> loads,Action denied,string? sharedRoot):AssemblyLoadContext("application-software-probe",false)
{
    readonly HashSet<string> framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);
    readonly HashSet<Assembly> blocked=[];
    protected override Assembly? Load(AssemblyName name) {
        if(name.Name!=null&&framework.Contains(name.Name))return Default.LoadFromAssemblyName(name);
        string path=Path.Combine(root,(name.Name??throw new FileLoadException("No assembly name"))+".dll");
        if(!File.Exists(path)&&sharedRoot!=null) {
            using var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(sharedRoot))!,"manifest.json")));
            var row=manifest.RootElement.GetProperty("files").EnumerateArray().FirstOrDefault(r=>Path.GetFileName(r.GetProperty("path").GetString()!)==name.Name+".dll");
            if(row.ValueKind==JsonValueKind.Object){path=Path.Combine(sharedRoot,name.Name+".dll");if(!File.Exists(path)||!Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).Equals(row.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Shared authority pin mismatch: "+path);}
        }
        if(!File.Exists(path))throw new FileNotFoundException("Dependency outside frozen candidate is forbidden",path);
        var a=LoadFromAssemblyPath(path);BlockNative(a);loads.Add(new{name=name.FullName,path,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant()});return a;
    }
    public void BlockNative(Assembly a){if(blocked.Add(a))NativeLibrary.SetDllImportResolver(a,(name,_,_)=>{denied();throw new DllNotFoundException("Native import blocked: "+name);});}
    protected override IntPtr LoadUnmanagedDll(string name){denied();throw new DllNotFoundException("Native import blocked: "+name);}
}



