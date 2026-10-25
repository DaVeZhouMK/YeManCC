using Mono.Cecil;
using System.Security.Cryptography;
using System.Text.Json;

if(args.Length!=3)throw new ArgumentException("runtime deviceAuditJson outputJson");
string root=Path.GetFullPath(args[0]);
using var audit=JsonDocument.Parse(File.ReadAllText(args[1]));
var deviceClosure=audit.RootElement.GetProperty("DeviceClosure").EnumerateArray().Select(x=>x.GetString()!).ToHashSet(StringComparer.Ordinal);
var factoryClosure=audit.RootElement.GetProperty("FactoryConstructorClosure").EnumerateArray().Select(x=>x.GetString()!).ToHashSet(StringComparer.Ordinal);
var metadataProblems=new List<string>();var allCalls=new List<NativeCall>();var imports=new List<NativeImport>();var strings=new List<Literal>();var modules=new List<object>();
foreach(string path in Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories).Where(p=>Path.GetExtension(p) is ".dll" or ".exe"))
{
    AssemblyDefinition assembly;
    try {assembly=AssemblyDefinition.ReadAssembly(path);}catch(BadImageFormatException){continue;}
    using(assembly)
    {
        modules.Add(new{path=Path.GetRelativePath(root,path),assembly=assembly.Name.FullName,mvid=assembly.MainModule.Mvid,
          sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant()});
        foreach(var type in assembly.MainModule.Types.SelectMany(AllTypes))
        {
            foreach(var method in type.Methods)
            {
                bool device=assembly.Name.Name=="HandheldCompanion"&&deviceClosure.Contains(method.FullName);
                bool factory=assembly.Name.Name=="HandheldCompanion"&&factoryClosure.Contains(method.FullName);
                if(method.HasPInvokeInfo)
                {
                    var info=method.PInvokeInfo;
                    if(info?.Module is null)metadataProblems.Add(assembly.Name.Name+": "+method.FullName);
                    else imports.Add(new(assembly.Name.Name,method.FullName,info.Module.Name,info.EntryPoint,device,factory));
                }
                if(method.HasBody)foreach(var instruction in method.Body.Instructions)
                    {
                        if(instruction.Operand is string text)strings.Add(new(assembly.Name.Name,method.FullName,text,device,factory));
                        if(instruction.Operand is MethodReference target)allCalls.Add(new(assembly.Name.Name,method.FullName,target.GetElementMethod().FullName,device,factory));
                    }
            }
        }
    }
}
string Normalize(string name)=>Path.GetFileNameWithoutExtension(name.Replace('\\','/')).ToLowerInvariant();
var nativeMethods=imports.Select(i=>i.Method).ToHashSet(StringComparer.Ordinal);
var nativeCalls=allCalls.Where(c=>nativeMethods.Contains(c.Target)).ToArray();
var files=new List<object>();
foreach(string path in Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories))
{
    string relative=Path.GetRelativePath(root,path).Replace('\\','/');
    string normalized=Normalize(relative),filename=Path.GetFileName(relative);
    var native=imports.Where(i=>Normalize(i.Module)==normalized).ToArray();
    var literals=strings.Where(s=>s.Value.Contains(filename,StringComparison.OrdinalIgnoreCase)||
       (normalized.Length>5&&s.Value.Equals(normalized,StringComparison.OrdinalIgnoreCase))).ToArray();
    bool ec=relative is "Resources/inpoutx64.dll" or "UEFIVaribleDll.dll" or "Resources/msiapcfg.dll" or "Resources/PawnIO/RyzenSMU.bin";
    files.Add(new{path=relative,bytes=new FileInfo(path).Length,ecOrFirmwareExempt=ec,nativeImports=native,nativeCallSites=nativeCalls.Where(c=>native.Any(i=>i.Method==c.Target)).ToArray(),literalOwners=literals});
}
var result=new{schemaVersion=1,mode="native reverse imports plus forward HC device/static-factory closure",modules,
  files,allNativeImports=imports,nativeMetadataProblems=metadataProblems,deviceRootMethods=deviceClosure.Count,factoryRootMethods=factoryClosure.Count,
  knownLimitations=new[]{"Direct native import module names and IL literals only; concatenated or generated names need manual owner review.","HC call closure is inherited from the exact input audit; external virtual dispatch, reflection, runtime callbacks and device lifecycle require extra proof.","Absence from this conservative direct-call closure alone does not authorize deletion."},physicalDeviceVerified=false,hardwareExecuted=false};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2]))!);
File.WriteAllText(args[2],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(new{status="passed",managedModules=modules.Count,nativeImports=imports.Count,files=files.Count}));
static IEnumerable<TypeDefinition> AllTypes(TypeDefinition t){yield return t;foreach(var n in t.NestedTypes.SelectMany(AllTypes))yield return n;}
sealed record NativeImport(string Assembly,string Method,string Module,string EntryPoint,bool DeviceClosure,bool FactoryConstructorClosure);
sealed record Literal(string Assembly,string Method,string Value,bool DeviceClosure,bool FactoryConstructorClosure);

sealed record NativeCall(string Assembly,string Method,string Target,bool DeviceClosure,bool FactoryConstructorClosure);
