using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

if(args.Length!=4)throw new ArgumentException("baseline-root candidate-root shared-bin report-json");
const BindingFlags Flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly;
var results=new List<object>();var errorsBySide=new List<Dictionary<string,string>>();var native=new List<string>();
foreach(var root in args.Take(2)){
 var failures=new List<object>();var byMember=new Dictionary<string,string>();var bindings=new List<object>();
 int signatures=0;int? typeCount=null;bool complete=false;string? fatal=null;StrictContext? c=null;
 try{
  c=new StrictContext(root,args[2],native,bindings);
  var a=c.LoadPinned(new AssemblyName("HandheldCompanion"));
  var types=a.GetTypes(); // Never substitute ReflectionTypeLoadException.Types or filter failed types.
  typeCount=types.Length;
  foreach(var t in types)foreach(var m in t.GetMethods(Flags)){
   string key=t.FullName+"::"+m.Name+"@"+m.MetadataToken.ToString("x8");
   try{_ = m.ReturnType.AssemblyQualifiedName;foreach(var p in m.GetParameters())_ = p.ParameterType.AssemblyQualifiedName;signatures++;}
   catch(Exception ex){string missing=ex is FileNotFoundException f?f.FileName??ex.Message:ex.Message;byMember[key]=missing;failures.Add(new{type=t.FullName,method=m.Name,token=m.MetadataToken,exception=ex.GetType().FullName,missing,message=ex.Message});}
  }
  complete=true;
 }catch(Exception ex){fatal=ex.ToString();byMember["<scan-incomplete>"]=ex.Message;failures.Add(new{type="<scan-incomplete>",method="<preflight-or-GetTypes>",token=0,exception=ex.GetType().FullName,missing=ex is FileNotFoundException f?f.FileName:ex.Message,message=ex.Message});}
 results.Add(new{runtime=Path.GetFullPath(root),status=failures.Count==0&&complete?"passed":"failed",allGetTypes=typeCount,resolvedSignatures=signatures,failureCount=failures.Count,failures,scanComplete=complete,fatal,
  manifestSha256=c?.ManifestSha256,sharedManifestSha256=c?.SharedManifestSha256,approvedManagedAssemblies=c?.ApprovedAssemblyCount,bindings});errorsBySide.Add(byMember);
}
var candidateOnly=errorsBySide[1].Where(kv=>!errorsBySide[0].ContainsKey(kv.Key)||errorsBySide[0][kv.Key]!=kv.Value).Select(kv=>new{member=kv.Key,missing=kv.Value}).ToArray();
var report=new{status=errorsBySide.All(x=>x.Count==0)?"passed":"failed-existing-signatures",results,candidateOnlyFailures=candidateOnly,nativeAttempts=native,allGetTypesSuppression=false,signatureErrorsExplicitlyRecorded=true,constructorsExecuted=false,hostExecuted=false,hardwareAccess=false,
 resolverPolicy="Only hash-checked runtime manifest files (recursive paths explicitly listed there), its exact seven shared dependencies and the trusted installed .NET/Desktop framework; exact application assembly name/version/culture/public-key-token; no machine search, unlisted files, or native imports"};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[3]))!);
File.WriteAllText(args[3],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(new{report.status,baselineFailures=errorsBySide[0].Count,candidateFailures=errorsBySide[1].Count,candidateOnly=candidateOnly.Length,nativeAttempts=native.Count}));
if(errorsBySide.Any(x=>x.Count>0))Environment.ExitCode=1;

sealed class StrictContext:AssemblyLoadContext{
 readonly string root,shared;readonly List<string> attempts;readonly List<object> bindings;
 readonly Dictionary<string,string> framework=new(StringComparer.OrdinalIgnoreCase);
 readonly Dictionary<string,(string path,AssemblyName identity,string sha,string source)> approved=new(StringComparer.OrdinalIgnoreCase);
 readonly HashSet<Assembly> installed=[];
 public string ManifestSha256{get;private set;}="";
 public string SharedManifestSha256{get;private set;}="";
 public int ApprovedAssemblyCount=>approved.Count;
 public StrictContext(string root,string shared,List<string> attempts,List<object> bindings):base("strict-hc-"+Guid.NewGuid(),false){
  this.root=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);this.shared=Path.GetFullPath(shared).TrimEnd(Path.DirectorySeparatorChar);this.attempts=attempts;this.bindings=bindings;
  string core=Path.GetFullPath(RuntimeEnvironment.GetRuntimeDirectory());
  string desktop=Path.Combine(Directory.GetParent(core.TrimEnd(Path.DirectorySeparatorChar))!.Parent!.FullName,"Microsoft.WindowsDesktop.App")+Path.DirectorySeparatorChar;
  foreach(var path in ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator)){
   if(path.StartsWith(core,StringComparison.OrdinalIgnoreCase)||path.StartsWith(desktop,StringComparison.OrdinalIgnoreCase))framework[Path.GetFileNameWithoutExtension(path)]=path;
  }
  string manifestPath=Path.Combine(this.root,"HandheldCompanion.runtime.json");ManifestSha256=Hash(manifestPath);
  using var manifest=JsonDocument.Parse(File.ReadAllText(manifestPath));
  var listed=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  foreach(var file in manifest.RootElement.GetProperty("files").EnumerateArray()){
   string relative=file.GetProperty("path").GetString()!;if(!listed.Add(relative))throw new InvalidDataException("DUPLICATE_RUNTIME_ENTRY: "+relative);
   Add(this.root,relative,file.GetProperty("sha256").GetString()!,null,"runtime-manifest");
  }
  var contract=manifest.RootElement.GetProperty("sharedDependencies");
  if(contract.GetProperty("directory").GetString()!="PowerControl/ymcc-shared-runtime")throw new InvalidDataException("SHARED_DIRECTORY_CONTRACT");
  string sharedManifest=Path.Combine(this.shared,"manifest.json");
  // The existing CLI also accepts the development bin directory; only its immediate manifest is allowed.
  if(!File.Exists(sharedManifest)&&Path.GetFileName(this.shared).Equals("bin",StringComparison.OrdinalIgnoreCase))sharedManifest=Path.Combine(Directory.GetParent(this.shared)!.FullName,"manifest.json");
  SharedManifestSha256=Hash(sharedManifest);
  if(!SharedManifestSha256.Equals(contract.GetProperty("manifestSha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("SHARED_MANIFEST_HASH_MISMATCH");
  var sharedEntries=contract.GetProperty("files").EnumerateArray().ToArray();
  if(sharedEntries.Length!=7)throw new InvalidDataException("EXACT_SEVEN_SHARED_DEPENDENCIES_REQUIRED");
  var sharedNames=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  foreach(var file in sharedEntries){string relative=file.GetProperty("path").GetString()!;if(!sharedNames.Add(relative))throw new InvalidDataException("DUPLICATE_SHARED_ENTRY: "+relative);Add(this.shared,relative,file.GetProperty("sha256").GetString()!,file.GetProperty("bytes").GetInt64(),"seven-shared-contract");}
 }
 static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
 static string SafePath(string directory,string relative){
  if(string.IsNullOrWhiteSpace(relative)||Path.IsPathRooted(relative)||relative.Contains('\\')||relative.Contains(':')||relative.Split('/').Any(s=>s is "" or "." or ".."))throw new InvalidDataException("UNSAFE_MANIFEST_PATH: "+relative);
  string full=Path.GetFullPath(Path.Combine(directory,relative.Replace('/',Path.DirectorySeparatorChar)));
  if(!full.StartsWith(directory+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("MANIFEST_PATH_ESCAPE: "+relative);
  for(string? node=full;node!=null&&node.Length>=directory.Length;node=Path.GetDirectoryName(node)){if((File.GetAttributes(node)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("REPARSE_PATH_NOT_ALLOWED: "+node);if(node.Equals(directory,StringComparison.OrdinalIgnoreCase))break;}
  return full;
 }
 void Add(string directory,string relative,string hash,long? bytes,string source){
  string path=SafePath(directory,relative);string actual=Hash(path);
  if(hash.Length!=64||!actual.Equals(hash,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("PINNED_HASH_MISMATCH: "+relative);
  if(bytes!=null&&new FileInfo(path).Length!=bytes)throw new InvalidDataException("PINNED_LENGTH_MISMATCH: "+relative);
  if(!path.EndsWith(".dll",StringComparison.OrdinalIgnoreCase))return;
  AssemblyName identity;try{identity=AssemblyName.GetAssemblyName(path);}catch(BadImageFormatException){return;} // Listed native files are hashed, never loaded.
  if(identity.Name==null||!approved.TryAdd(identity.Name,(path,identity,actual,source)))throw new InvalidDataException("AMBIGUOUS_APPROVED_ASSEMBLY: "+identity.Name);
 }
 public void Block(Assembly a){if(installed.Add(a))NativeLibrary.SetDllImportResolver(a,(n,_,_)=>{attempts.Add(n);throw new DllNotFoundException("Native denied: "+n);});}
 public Assembly LoadPinned(AssemblyName requested){
  if(requested.Name==null||!approved.TryGetValue(requested.Name,out var pin))throw new FileNotFoundException("Dependency is outside runtime manifest and seven-shared whitelist",requested.FullName);
  bool version=requested.Version==null||requested.Version==pin.identity.Version;
  bool culture=string.Equals(requested.CultureName??"",pin.identity.CultureName??"",StringComparison.OrdinalIgnoreCase);
  byte[]? expectedToken=requested.GetPublicKeyToken();bool token=expectedToken==null||(expectedToken.SequenceEqual(pin.identity.GetPublicKeyToken()??[]));
  if(!version||!culture||!token)throw new FileLoadException("APPROVED_ASSEMBLY_IDENTITY_MISMATCH: "+requested.FullName+" -> "+pin.identity.FullName);
  if(Hash(pin.path)!=pin.sha)throw new InvalidDataException("APPROVED_FILE_CHANGED_DURING_SCAN: "+pin.path);
  var a=LoadFromAssemblyPath(pin.path);Block(a);bindings.Add(new{requested=requested.FullName,resolved=a.GetName().FullName,path=pin.path,sha256=pin.sha,source=pin.source});return a;
 }
 protected override Assembly? Load(AssemblyName requested){
  if(requested.Name!=null&&framework.TryGetValue(requested.Name,out var platform))return Default.LoadFromAssemblyPath(platform);
  return LoadPinned(requested);
 }
 protected override IntPtr LoadUnmanagedDll(string n){attempts.Add(n);throw new DllNotFoundException("Native denied: "+n);}
}
