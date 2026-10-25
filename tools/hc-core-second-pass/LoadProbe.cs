using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Security.Cryptography;
using System.Windows;
using System.Collections;

if(args.Length is not (5 or 6))throw new ArgumentException("baseline-root candidate-root shared-bin root-provenance-json output-json");
var evidence=new List<object>();var failures=new List<string>();var blocked=new List<string>();
const BindingFlags Flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly;
var contexts=new List<StrictContext>();
try{
 var assemblies=new List<Assembly>();foreach(string root in args.Take(2)){var c=new StrictContext(root,args[2],blocked);contexts.Add(c);var hc=c.LoadFromAssemblyPath(Path.Combine(Path.GetFullPath(root),"HandheldCompanion.dll"));c.Block(hc);assemblies.Add(hc);}
 var before=assemblies[0];var after=assemblies[1];var bt=before.GetTypes();var at=after.GetTypes(); // No ReflectionTypeLoadException recovery: every remaining type must load.
 var map=at.ToDictionary(t=>t.FullName!);var id=before.GetType("HandheldCompanion.Devices.IDevice",true)!;var devices=bt.Where(t=>t!=id&&!t.IsAbstract&&id.IsAssignableFrom(t)).ToArray();
 foreach(var t in devices){if(!map.TryGetValue(t.FullName!,out var ct))throw new TypeLoadException("Missing original device "+t.FullName);_ = ct.GetConstructors(Flags);_ = ct.GetMethods(Flags);_ = ct.GetFields(Flags);_ = ct.GetProperties(Flags);}
 var primaryDevices=devices.Where(t=>t.Namespace=="HandheldCompanion.Devices").ToArray(); if(primaryDevices.Length!=77)throw new InvalidDataException("Original primary Devices namespace count was "+primaryDevices.Length);
 using var roots=JsonDocument.Parse(File.ReadAllText(args[3]));var rootNames=roots.RootElement.GetProperty("rootTypes").EnumerateArray().Select(x=>x.GetString()!).ToArray();
 var rootResults=new List<object>();foreach(var n in rootNames){var t=before.GetType(n.Replace('/','+'),true)!;var ct=after.GetType(n.Replace('/','+'),true)!;var old=t.GetMembers(Flags).Select(m=>m.MemberType+":"+m).Order().ToArray();var now=ct.GetMembers(Flags).Select(m=>m.MemberType+":"+m).Order().ToArray();var missing=old.Except(now).ToArray();if(missing.Length>0)failures.Add("Reflection surface "+n+": "+string.Join(";",missing));rootResults.Add(new{type=n,originalMembers=old.Length,candidateMembers=now.Length,missing});}
 var factoryBefore=before.GetType("HandheldCompanion.Managers.ManagerFactory",true)!;var factory=after.GetType(factoryBefore.FullName!,true)!;var managers=new List<object>();foreach(var f in factoryBefore.GetFields(Flags)){var cf=factory.GetField(f.Name,Flags)??throw new MissingFieldException(factory.FullName,f.Name);var mt=cf.FieldType;if(mt.FullName!=f.FieldType.FullName)throw new TypeLoadException(f.Name+" changed field type");var status=mt.GetProperty("Status",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)??throw new MissingMemberException(mt.FullName,"Status");var oldCtors=f.FieldType.GetConstructors(Flags).Select(x=>x.ToString()).Order().ToArray();var newCtors=mt.GetConstructors(Flags).Select(x=>x.ToString()).Order().ToArray();if(!oldCtors.SequenceEqual(newCtors))failures.Add("Manager constructor set "+mt.FullName);managers.Add(new{field=f.Name,type=mt.FullName,constructors=newCtors,status=status.ToString(),staticInitializerPreserved=(f.FieldType.TypeInitializer is null)==(mt.TypeInitializer is null)});}
 var ppm=after.GetType("HandheldCompanion.Managers.PowerProfileManager",true)!;_ = ppm.GetMethod("CloneProfile",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)??throw new MissingMethodException(ppm.FullName,"CloneProfile");foreach(var name in new[]{"Applied","Discarded"}){var e=ppm.GetEvent(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)??throw new MissingMemberException(ppm.FullName,name);_ = e.EventHandlerType?.GetMethod("Invoke")??throw new MissingMemberException("Event Invoke "+name);}
 var app=after.GetType("HandheldCompanion.App",true)!;foreach(var n in new[]{"SettingsPath","LogsPath","InstallPath","GameControllerDbPath"}){var f=app.GetField(n,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)??throw new MissingFieldException(app.FullName,n);if(f.FieldType!=typeof(string))throw new InvalidDataException("App path field type changed");}
 // Force signature resolution without running a type initializer or member.
 int resolved=0;if(args.Length==6)foreach(var t in at){_ = t.BaseType;_ = t.GetInterfaces();foreach(var f in t.GetFields(Flags))_ = f.FieldType;foreach(var m in t.GetMethods(Flags)){_ = m.ReturnType;foreach(var p in m.GetParameters())_ = p.ParameterType;resolved++;}foreach(var p in t.GetProperties(Flags))_ = p.PropertyType;foreach(var e in t.GetEvents(Flags))_ = e.EventHandlerType;}
 evidence.Add(new{originalGetTypes=bt.Length,candidateGetTypes=at.Length,concreteDevices=devices.Length,primaryDevices=primaryDevices.Length,deviceNames=devices.Select(x=>x.FullName).Order(),deletedTypes=bt.Select(t=>t.FullName).Except(at.Select(t=>t.FullName)).Order(),rootResults,factoryManagers=managers,resolvedMethodSignatures=resolved});
}catch(Exception e){failures.Add(e.ToString());}
var result=new{status=failures.Count==0?"passed":"failed",evidence,failures,nativeAttempts=blocked,noReflectionLoadExceptionSuppression=true,constructorsExecuted=false,staticInitializersExecuted=false,productHostExecuted=false,hardwareAccess=false};File.WriteAllText(args[4],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.status,failures=result.failures.Count,nativeAttempts=blocked.Count}));if(failures.Count>0)Environment.ExitCode=1;
sealed class StrictContext:AssemblyLoadContext{
 readonly string root,shared;readonly List<string> attempts;readonly HashSet<string> framework;readonly HashSet<Assembly> installed=[];
 public StrictContext(string root,string shared,List<string> attempts):base("strict-hc-"+Guid.NewGuid(),false){this.root=Path.GetFullPath(root);this.shared=Path.GetFullPath(shared);this.attempts=attempts;framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains("Microsoft.WindowsDesktop.App",StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);}
 public void Block(Assembly a){if(installed.Add(a))NativeLibrary.SetDllImportResolver(a,(n,_,_)=>{attempts.Add(n);throw new DllNotFoundException("Native denied: "+n);});}
 protected override Assembly? Load(AssemblyName n){if(n.Name!=null&&framework.Contains(n.Name))return Default.LoadFromAssemblyName(n);var p=Path.Combine(root,n.Name+".dll");if(!File.Exists(p))p=Path.Combine(shared,n.Name+".dll");if(!File.Exists(p))throw new FileNotFoundException("Pinned dependency absent",p);var a=LoadFromAssemblyPath(p);Block(a);return a;}
 protected override IntPtr LoadUnmanagedDll(string n){attempts.Add(n);throw new DllNotFoundException("Native denied: "+n);}
}




