using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if(args.Length!=7)throw new ArgumentException("runtime hc-sha sdl-sha retained-type-list golden-or-dash report mode");
string root=Path.GetFullPath(args[0]),report=Path.GetFullPath(args[5]);
var attempts=new List<object>();var loads=new List<object>();var checks=new List<string>();
string[] NativeModules()=>Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Where(m=>m.ModuleName.Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)).Select(m=>m.FileName).ToArray();
void Check(bool value,string message){if(!value)throw new InvalidOperationException(message);checks.Add(message);}
var before=NativeModules();Exception? failure=null;int hcTypes=0,sdlTypes=0,layouts=0,enums=0,instanceFields=0;var surfaces=new SortedDictionary<string,object>(StringComparer.Ordinal);
try
{
    Check(before.Length==0,"Fresh process contains no native SDL3 module");
    Check(!Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories).Any(p=>Path.GetFileName(p).Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)),"SDL3.dll physically absent recursively");
    string Hash(string file)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root,file)))).ToLowerInvariant();
    Check(Hash("HandheldCompanion.dll")==args[1],"HC hash pinned");Check(Hash("SDL3-CS.dll")==args[2],"SDL3-CS hash pinned");
    var context=new GuardContext(root,attempts,loads);var hc=context.LoadFromAssemblyPath(Path.Combine(root,"HandheldCompanion.dll"));context.Block(hc);hcTypes=hc.GetTypes().Length;Check(hcTypes==1768,"All 1768 HC types path-loaded");
    var sdl=context.LoadFromAssemblyPath(Path.Combine(root,"SDL3-CS.dll"));context.Block(sdl);sdlTypes=sdl.GetTypes().Length;
    var names=JsonSerializer.Deserialize<string[]>(File.ReadAllText(args[3]))!;
    foreach(var name in names.Where(n=>n!="<Module>"))
    {
        var t=sdl.GetType(name.Replace('/','+'),true)!;
        var fields=t.GetFields(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly);
        var enumConstants=t.IsEnum?fields.Where(f=>f.IsLiteral).Select(f=>new{f.Name,Value=f.GetRawConstantValue()}).OrderBy(f=>f.Name).ToArray():null;
        if(t.IsEnum)enums++;
        var layout=t.StructLayoutAttribute;
        int? size=null;if(t.IsValueType&&!t.IsEnum){size=Marshal.SizeOf(t);layouts++;}
        var ff=fields.Where(f=>!f.IsStatic).Select(f=>
        {
            long? offset=t.IsValueType&&!t.IsEnum?Marshal.OffsetOf(t,f.Name).ToInt64():null;instanceFields++;
            return new{f.Name,Type=f.FieldType.FullName,Attributes=(int)f.Attributes,Offset=offset,Marshal=CustomAttributeData.GetCustomAttributes(f).Select(a=>a.ToString()).Order().ToArray()};
        }).OrderBy(f=>f.Name).ToArray();
        surfaces.Add(name,new{FullName=t.FullName,Attributes=(int)t.Attributes,Base=t.BaseType?.FullName,Layout=layout==null?null:new{Value=layout.Value.ToString(),layout.Pack,layout.Size,CharSet=layout.CharSet.ToString()},Size=size,EnumUnderlying=t.IsEnum?Enum.GetUnderlyingType(t).FullName:null,EnumConstants=enumConstants,Fields=ff});
    }
    if(args[4]!="-")
    {
        using var golden=JsonDocument.Parse(File.ReadAllText(args[4]));
        Check(golden.RootElement.GetProperty("Status").GetString()=="passed","Golden probe passed");
        string actual=JsonSerializer.Serialize(surfaces),expected=golden.RootElement.GetProperty("Surfaces").GetRawText();
        using var aa=JsonDocument.Parse(actual);using var ee=JsonDocument.Parse(expected);
        Check(JsonElement.DeepEquals(aa.RootElement,ee.RootElement),"All retained SDL type metadata, enum constants, field layouts and marshal attributes equal original");
    }
    Check(attempts.Count==0,"No non-framework native imports attempted");Check(NativeModules().Length==0,"No native SDL3 process module after reflection/layout operations");
}
catch(Exception e){failure=e;}
var record=new{Status=failure==null?"passed":"failed",Mode=args[6],ProcessId=Environment.ProcessId,HcAllGetTypes=hcTypes,SdlAllGetTypes=sdlTypes,ComparedTypes=surfaces.Count,ComparedStructLayouts=layouts,ComparedEnums=enums,ComparedInstanceFields=instanceFields,Surfaces=surfaces,Checks=checks,NativeAttempts=attempts,Loads=loads,NativeModulesBefore=before,NativeModulesAfter=NativeModules(),Exception=failure?.ToString(),HardwareExecuted=false,DeviceConstructorsExecuted=false,FullControllerConstructorsExecuted=false};
Directory.CreateDirectory(Path.GetDirectoryName(report)!);File.WriteAllText(report,JsonSerializer.Serialize(record,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{record.Status,record.ProcessId,record.HcAllGetTypes,record.SdlAllGetTypes,record.ComparedTypes,record.ComparedStructLayouts,record.ComparedEnums,record.ComparedInstanceFields,NativeAttempts=attempts.Count}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}

sealed class GuardContext:AssemblyLoadContext
{
    readonly string root;readonly List<object> attempts,loads;readonly HashSet<Assembly> blocked=[];readonly HashSet<string> framework;
    public GuardContext(string root,List<object> attempts,List<object> loads):base("sdl-surface",false)
    {
        this.root=root;this.attempts=attempts;this.loads=loads;
        framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);
    }
    public void Block(Assembly a){if(blocked.Add(a))NativeLibrary.SetDllImportResolver(a,(name,assembly,_)=>{attempts.Add(new{Library=name,Assembly=assembly.GetName().Name,Stage="DllImportResolver"});throw new DllNotFoundException("Non-framework native import blocked: "+name);});}
    protected override Assembly? Load(AssemblyName requested)
    {
        if(requested.Name!=null&&framework.Contains(requested.Name))return Default.LoadFromAssemblyName(requested);
        string name=requested.Name??throw new FileLoadException("Assembly name missing");if(name!=Path.GetFileName(name)||name.Contains(':'))throw new FileLoadException("Invalid assembly name");string path=Path.Combine(root,name+".dll");if(!File.Exists(path))throw new FileNotFoundException("Frozen dependency absent",path);
        var a=LoadFromAssemblyPath(path);Block(a);loads.Add(new{Requested=requested.FullName,Resolved=a.FullName,Path=path});return a;
    }
    protected override IntPtr LoadUnmanagedDll(string name){attempts.Add(new{Library=name,Stage="ALC.LoadUnmanagedDll"});throw new DllNotFoundException("Native load blocked: "+name);}
}

