using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using YeMan.SharedRuntime;

if(args.Length is not (3 or 4)) throw new ArgumentException("repo fixtureRoot resultJson [additionalOmissionsJson]");
string repo=Path.GetFullPath(args[0]), fixture=Path.GetFullPath(args[1]);
if(Directory.Exists(fixture)) throw new IOException("Use a new fixture directory.");
var checks=new List<string>();
void Check(bool ok,string name){if(!ok) throw new InvalidDataException(name);checks.Add(name);}
void Reject(Action action,string expected,string name)
{
    try { action(); } catch(Exception e) when(e.Message.Contains(expected,StringComparison.Ordinal)) { checks.Add(name);return; }
    throw new InvalidDataException("Expected rejection: "+name);
}
using var profile=JsonDocument.Parse(File.ReadAllText(Path.Combine(repo,"deps/ymcc-device-core-runtime/profile.json")));
string runtimeId=profile.RootElement.GetProperty("runtimeId").GetString()!;
string runtime=Path.Combine(fixture,"PowerControl","handheldcompanion-runtime",runtimeId);
string shared=Path.Combine(fixture,"PowerControl","ymcc-shared-runtime");
Directory.CreateDirectory(runtime);Directory.CreateDirectory(shared);
using var specs=JsonDocument.Parse(File.ReadAllText(Path.Combine(repo,"deps/ymcc-shared-runtime/manifest.json")));
var sharedNames=profile.RootElement.GetProperty("sharedDependencies").GetProperty("files").EnumerateArray().Select(x=>x.GetProperty("path").GetString()!).ToHashSet(StringComparer.Ordinal);
var libraries=specs.RootElement.GetProperty("files").EnumerateArray().Where(x=>sharedNames.Contains(x.GetProperty("packagedPath").GetString()!)).Select(x=>new{
    Name=Path.GetFileNameWithoutExtension(x.GetProperty("path").GetString()!),
    File=Path.GetFileName(x.GetProperty("path").GetString()!),
    Sha=x.GetProperty("sha256").GetString()!.ToUpperInvariant(),
}).ToArray();
var removed=libraries.Select(x=>x.File).ToHashSet(StringComparer.OrdinalIgnoreCase);
string[] additionalOmissions=args.Length==4?JsonSerializer.Deserialize<string[]>(File.ReadAllText(args[3]))??throw new InvalidDataException("Invalid omission list"):[];
foreach(string relative in additionalOmissions)
{
    if(Path.IsPathRooted(relative)||relative.Split('/','\\').Any(x=>x is ".." or ".")||relative.Contains(':'))throw new InvalidDataException("Unsafe omission");
    removed.Add(relative.Replace('\\','/'));
}
string source=Path.Combine(repo,"PowerControl","handheldcompanion-runtime",runtimeId);
foreach(string file in Directory.EnumerateFiles(source,"*",SearchOption.AllDirectories))
{
    string relative=Path.GetRelativePath(source,file);
    if(removed.Contains(relative.Replace('\\','/'))) continue;
    string target=Path.Combine(runtime,relative);Directory.CreateDirectory(Path.GetDirectoryName(target)!);File.Copy(file,target);
}
foreach(string omitted in additionalOmissions)Check(!File.Exists(Path.Combine(runtime,omitted)),omitted+": additional candidate file absent");
foreach(var lib in libraries)
{
    string target=Path.Combine(shared,lib.File);
    File.Copy(Path.Combine(repo,"deps/ymcc-shared-runtime/bin",lib.File),target);
    Check(!File.Exists(Path.Combine(runtime,lib.File)),lib.Name+": device copy absent");
    var identity=AssemblyName.GetAssemblyName(target);
    Check(DeviceCoreSharedDependencies.VerifiedPath(runtime,identity,lib.Sha)==target,lib.Name+": shared path, hash and full identity");
    byte[] original=File.ReadAllBytes(target), changed=(byte[])original.Clone();changed[^1]^=1;
    try { File.WriteAllBytes(target,changed);Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,identity,lib.Sha),"SHA256 mismatch",lib.Name+": changed bytes rejected"); }
    finally {File.WriteAllBytes(target,original);}
    string absent=target+".absent";File.Move(target,absent);
    try {Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,identity,lib.Sha),"is missing",lib.Name+": missing shared copy rejected");}
    finally {File.Move(absent,target);}
    var wrong=new AssemblyName(identity.FullName!){Version=new Version(999,0,0,0)};
    Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,wrong,lib.Sha),"identity mismatch",lib.Name+": wrong version rejected");
}
var first=libraries[0];var firstIdentity=AssemblyName.GetAssemblyName(Path.Combine(shared,first.File));
Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(fixture,firstIdentity,first.Sha),"production PowerControl layout","unapproved layout rejected");
Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,new AssemblyName{Name="../escape"},first.Sha),"Invalid shared", "path escape rejected");
var invalidToken=new AssemblyName(firstIdentity.FullName!);invalidToken.SetPublicKeyToken([0,0,0,0,0,0,0,0]);
Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,invalidToken,first.Sha),"identity mismatch","wrong public key token rejected");
var invalidCulture=new AssemblyName(firstIdentity.FullName!){CultureName="fr-FR"};
Reject(()=>DeviceCoreSharedDependencies.VerifiedPath(runtime,invalidCulture,first.Sha),"identity mismatch","wrong culture rejected");
Check(DeviceCoreSharedDependencies.Resolve(AssemblyLoadContext.Default,new AssemblyName("Unknown.Dependency"),runtime) is null,"unknown library not claimed");
using var actualManifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(runtime,"HandheldCompanion.runtime.json")));
Check(DeviceCoreSharedDependencies.ValidateDeclaredManifest(runtime,actualManifest.RootElement).SetEquals(sharedNames),"actual production validator accepts the exact seven shared entries");
string localLhm=Path.Combine(runtime,"LibreHardwareMonitorLib.dll");
const string lhmSha="F7ED30F07EA636C0DDCC5764C15C0A25AE8A0ACA02DED77E4AF24439955487CF";
var lhmIdentity=AssemblyName.GetAssemblyName(localLhm);
Check(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(localLhm)))==lhmSha && !File.Exists(Path.Combine(shared,"LibreHardwareMonitorLib.dll")),"canonical local LHM has its original bytes and no shared duplicate");
byte[] lhmBytes=File.ReadAllBytes(localLhm),badLhm=(byte[])lhmBytes.Clone();badLhm[^1]^=1;
try{File.WriteAllBytes(localLhm,badLhm);Reject(()=>RuntimeDependencyContract.LoadPinned(new ProbeContext(runtime),lhmIdentity,localLhm,lhmSha),"DISK_HASH_MISMATCH","actual pinned loader rejects corrupted canonical LHM");}finally{File.WriteAllBytes(localLhm,lhmBytes);}
string missingLhm=localLhm+".absent";File.Move(localLhm,missingLhm);
try{Reject(()=>RuntimeDependencyContract.LoadPinned(new ProbeContext(runtime),lhmIdentity,localLhm,lhmSha),"RUNTIME_FILE_UNAVAILABLE","actual pinned loader rejects missing canonical LHM");}finally{File.Move(missingLhm,localLhm);}
var context=new ProbeContext(runtime);
Check(DeviceCoreSharedDependencies.Resolve(context,lhmIdentity,runtime) is null,"shared resolver does not claim the canonical local LHM");
var loadedLhm=RuntimeDependencyContract.LoadPinned(context,lhmIdentity,localLhm,lhmSha);
Check(loadedLhm.Location==localLhm,"actual pinned ALC load uses the single canonical local LHM");
foreach(var lib in libraries)
{
    var identity=AssemblyName.GetAssemblyName(Path.Combine(shared,lib.File));
    var loaded=context.LoadFromAssemblyName(identity);
    Check(loaded.Location==Path.Combine(shared,lib.File),lib.Name+": actual ALC load uses shared copy");
}
var bindings=DeviceCoreSharedDependencies.Snapshot();
Check(bindings.Length==libraries.Length&&bindings.All(b=>b.ResolvedPath==Path.Combine(shared,b.AssemblyName+".dll")),"diagnostic snapshot records the actual declared shared bindings");
Assembly hc=context.LoadFromAssemblyPath(Path.Combine(runtime,"HandheldCompanion.dll"));context.BlockNative(hc);
Type[] types=hc.GetTypes();
Check(types.Length==1768,"all 1768 HC types load with the declared shared copies absent locally");
Check(context.NativeAttempts==0,"no non-framework native imports");
var result=new{status="passed",checks=checks.Count,details=checks,fixture,runtime,shared,
  bindings,additionalOmissions,removedCopies=libraries.Select(x=>x.File),removedCopyBytes=libraries.Sum(x=>new FileInfo(Path.Combine(shared,x.File)).Length),
  allHcTypes=types.Length,loadedAssemblies=context.Assemblies.Select(a=>new{identity=a.FullName,path=a.Location}),
  physicalDeviceVerified=false,deviceConstructorsExecuted=false,nativeAttempts=context.NativeAttempts};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2]))!);
File.WriteAllText(args[2],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(new{result.status,result.checks,result.removedCopyBytes,result.allHcTypes}));

sealed class ProbeContext:AssemblyLoadContext
{
    readonly string root;readonly HashSet<string> framework;readonly HashSet<Assembly> blocked=[];
    public int NativeAttempts{get;private set;}
    public ProbeContext(string root):base("shared-device-"+Guid.NewGuid().ToString("N"),false)
    {
        this.root=root;
        framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator)
            .Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))
            .Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);
    }
    protected override Assembly? Load(AssemblyName requested)
    {
        if(requested.Name is not null&&framework.Contains(requested.Name)) return Default.LoadFromAssemblyName(requested);
        string name=requested.Name??throw new FileLoadException("Missing name");
        if(name!=Path.GetFileName(name)||name.Contains(':'))throw new FileLoadException("Invalid dependency name");
        string path=Path.Combine(root,name+".dll");
        Assembly loaded=File.Exists(path)?LoadFromAssemblyPath(path):DeviceCoreSharedDependencies.Resolve(this,requested,root)??throw new FileNotFoundException("No dependency",path);
        BlockNative(loaded);return loaded;
    }
    public void BlockNative(Assembly assembly)
    {
        if(blocked.Add(assembly))NativeLibrary.SetDllImportResolver(assembly,(name,_,_)=>Deny(name));
    }
    IntPtr Deny(string name){NativeAttempts++;throw new DllNotFoundException("Non-framework native import rejected: "+name);}
    protected override IntPtr LoadUnmanagedDll(string name)=>Deny(name);
}
