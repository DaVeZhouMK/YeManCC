using System.Collections;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;

if(args.Length!=3)throw new ArgumentException("runtime shared-bin report");
string root=Path.GetFullPath(args[0]),shared=Path.GetFullPath(args[1]),output=Path.GetFullPath(args[2]);
var rows=new List<object>();var loads=new List<object>();var native=new List<object>();var allTypes=new List<object>();
var context=new SoftwareContext(root,shared,loads,native);
var assemblies=new Dictionary<string,Assembly>();
Assembly Load(string name){if(!assemblies.TryGetValue(name,out var a)){a=context.LoadFromAssemblyName(new AssemblyName(name));assemblies[name]=a;}return a;}
Type T(string name,string assembly="WinRT.Runtime")=>Load(assembly).GetType(name,true)!;
object? Call(Type type,string name,params object?[] values){var methods=type.GetMethods(BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic).Where(m=>m.Name==name&&m.GetParameters().Length==values.Length).ToArray();var m=methods.Where(m=>m.GetParameters().Select((p,i)=>values[i]==null||p.ParameterType.IsInstanceOfType(values[i])).All(b=>b)).OrderByDescending(m=>m.GetParameters().Select((p,i)=>values[i]?.GetType()==p.ParameterType?1:0).Sum()).FirstOrDefault()??(methods.Length==1?methods[0]:throw new MissingMethodException(type.FullName,name));return m.Invoke(null,values);}
void Test(string name,Func<object?> action){try{rows.Add(new{name,status="passed",value=action(),error=(string?)null});}catch(Exception e){while(e is TargetInvocationException&&e.InnerException!=null)e=e.InnerException;rows.Add(new{name,status="failed",value=(object?)null,error=e.GetType().FullName+": "+e.Message,trace=e.ToString()});}}
string TypeNames(Type[] ts)=>Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(string.Join("\n",ts.Select(t=>t.FullName).Order())))).ToLowerInvariant();
// GetTypes never allocates a device; every managed immutable consumer is included, even unrelated baseline failures.
foreach(var path in Directory.GetFiles(root,"*.dll",SearchOption.AllDirectories).Concat(Directory.GetFiles(shared,"*.dll",SearchOption.AllDirectories)).Order()){
 try{var n=AssemblyName.GetAssemblyName(path).Name!;var a=Load(n);try{var ts=a.GetTypes();allTypes.Add(new{name=n,status="passed",types=ts.Length,typeNamesSha256=TypeNames(ts),errors=Array.Empty<string>()});}catch(ReflectionTypeLoadException e){allTypes.Add(new{name=n,status="failed",types=e.Types.Count(t=>t!=null),typeNamesSha256=TypeNames(e.Types.Where(t=>t!=null).Cast<Type>().ToArray()),errors=e.LoaderExceptions.Where(e=>e!=null).Select(e=>e!.GetType().Name+": "+e.Message).Distinct().Order().ToArray()});}}
 catch(BadImageFormatException){}catch(Exception e){allTypes.Add(new{name=Path.GetFileNameWithoutExtension(path),status="failed",types=0,typeNamesSha256="",errors=new[]{e.GetType().Name+": "+e.Message}});}
}
Test("geometry-point-rect-size",()=>{
 var p=Activator.CreateInstance(T("Windows.Foundation.Point"),[3.5,4.25])!;var q=Activator.CreateInstance(T("Windows.Foundation.Point"),[7.5,9.25])!;var r=Activator.CreateInstance(T("Windows.Foundation.Rect"),[p,q])!;return new{point=p.ToString(),rect=r.ToString(),contains=r.GetType().GetMethod("Contains",[p.GetType()])!.Invoke(r,[p]),width=r.GetType().GetProperty("Width")!.GetValue(r),height=r.GetType().GetProperty("Height")!.GetValue(r)};
});
foreach(string s in new[]{"","YMCC软件 WinRT Ω 中文 \U0001F3AE","a\0b"}){
 string value=s;Test("hstring-roundtrip-"+(s==""?"empty":s.Contains('\0')?"embedded-nul":"unicode"),()=>{var m=T("WinRT.MarshalString");var h=(IntPtr)Call(m,"FromManaged",value)!;try{string? back=(string?)Call(m,"FromAbi",h);if(back!=value)throw new InvalidDataException("HSTRING changed");return back;}finally{Call(m,"DisposeAbi",h);}});
}
foreach(var type in new[]{typeof(int),typeof(string),typeof(Guid),typeof(DateTimeOffset),typeof(TimeSpan),typeof(IEnumerable<int>),typeof(IList<string>),typeof(IReadOnlyList<int>),typeof(IDictionary<string,object>),typeof(KeyValuePair<string,int>),typeof(int?),typeof(EventHandler<int>)}){
 var generic=type;Test("guid-signature-"+generic.ToString(),()=>new{signature=Call(T("WinRT.GuidGenerator"),"GetSignature",generic)?.ToString(),iid=Call(T("WinRT.GuidGenerator"),generic.IsGenericType?"CreateIID":"GetIID",generic)?.ToString()});
}
Test("reflection-generic-blittable-array",()=>{
 var m=T("WinRT.MarshalBlittable`1").MakeGenericType(typeof(int));var input=new[]{int.MinValue,-1,0,1,int.MaxValue};var abi=Call(m,"FromManagedArray",input)!;try{var back=(int[])Call(m,"FromAbiArray",abi)!;if(!input.SequenceEqual(back))throw new InvalidDataException("Array changed");return back;}finally{Call(m,"DisposeAbiArray",abi);}
});
Test("reflection-generic-nonblittable-bool-array",()=>{
 var m=T("WinRT.MarshalNonBlittable`1").MakeGenericType(typeof(bool));var input=new[]{true,false,true};var abi=Call(m,"FromManagedArray",input)!;try{var back=(bool[])Call(m,"FromAbiArray",abi)!;if(!input.SequenceEqual(back))throw new InvalidDataException("Array changed");return back;}finally{Call(m,"DisposeAbiArray",abi);}
});
Test("reflection-string-array",()=>{
 var m=T("WinRT.MarshalString");var input=new[]{"","中文","alpha"};var abi=Call(m,"FromManagedArray",(object)input)!;try{var back=(string[])Call(m,"FromAbiArray",abi)!;if(!input.SequenceEqual(back))throw new InvalidDataException("Array changed");return back;}finally{Call(m,"DisposeAbiArray",abi);}
});
Test("activation-factory-valueset",()=>{var r=Call(T("WinRT.ActivationFactory"),"Get","Windows.Foundation.Collections.ValueSet")!;try{return new{type=r.GetType().FullName,nonNull=true};}finally{((IDisposable)r).Dispose();}});
Test("activation-valueset-generic-boxing",()=>{
 var instance=Activator.CreateInstance(T("Windows.Foundation.Collections.ValueSet","Microsoft.Windows.SDK.NET"))!;var d=(IDictionary<string,object>)instance;d.Add("count",42);d.Add("name","YMCC 中文");d.Add("flag",true);d.Add("guid",new Guid("00112233-4455-6677-8899-aabbccddeeff"));d.Add("array",new[]{1,2,3});
 var values=d.OrderBy(p=>p.Key).Select(p=>new{p.Key,type=p.Value.GetType().FullName,value=p.Value is int[] ia?string.Join(",",ia):Convert.ToString(p.Value,CultureInfo.InvariantCulture)}).ToArray();if((int)d["count"]!=42)throw new InvalidDataException("ValueSet changed");d.Remove("flag");return new{values,afterRemove=d.Count,contains=d.ContainsKey("flag")};
});
Test("activation-api-information-local",()=>Call(T("Windows.Foundation.Metadata.ApiInformation","Microsoft.Windows.SDK.NET"),"IsTypePresent","Windows.Foundation.Collections.ValueSet"));
Test("reflection-async-action-task-bridge",()=>{
 var ext=T("System.WindowsRuntimeSystemExtensions","Microsoft.Windows.SDK.NET");var action=Call(ext,"AsAsyncAction",Task.CompletedTask)!;var actionType=T("Windows.Foundation.IAsyncAction","Microsoft.Windows.SDK.NET");var asTask=ext.GetMethods(BindingFlags.Public|BindingFlags.Static).Single(m=>m.Name=="AsTask"&&!m.IsGenericMethod&&m.GetParameters().Length==1&&m.GetParameters()[0].ParameterType==actionType);var task=(Task)asTask.Invoke(null,[action])!;task.GetAwaiter().GetResult();return new{completed=task.IsCompletedSuccessfully};
});
Test("reflection-async-operation-task-bridge",()=>{
 var ext=T("System.WindowsRuntimeSystemExtensions","Microsoft.Windows.SDK.NET");var toAsync=ext.GetMethods(BindingFlags.Public|BindingFlags.Static).Single(m=>m.Name=="AsAsyncOperation"&&m.IsGenericMethod&&m.GetParameters().Length==1).MakeGenericMethod(typeof(int));var operation=toAsync.Invoke(null,[Task.FromResult(123)])!;var asTask=ext.GetMethods(BindingFlags.Public|BindingFlags.Static).Single(m=>m.Name=="AsTask"&&m.IsGenericMethod&&m.GetGenericArguments().Length==1&&m.GetParameters().Length==1&&m.GetParameters()[0].ParameterType.Name.StartsWith("IAsyncOperation`",StringComparison.Ordinal)).MakeGenericMethod(typeof(int));var task=(Task<int>)asTask.Invoke(null,[operation])!;return new{result=task.GetAwaiter().GetResult(),completed=task.IsCompletedSuccessfully};
});
Test("sdk-reflection-enum-surface",()=>{
 var enums=Load("Microsoft.Windows.SDK.NET").GetTypes().Where(t=>t.IsEnum).OrderBy(t=>t.FullName).ToArray();var entries=enums.SelectMany(t=>Enum.GetNames(t).Select(n=>t.FullName+":"+n+"="+Enum.Format(t,Enum.Parse(t,n),"D"))).ToArray();return new{enumTypes=enums.Length,fields=entries.Length,sha256=Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(string.Join("\n",entries)))).ToLowerInvariant()};
});
var constructors=new List<object>();Exception? constructionFailure=null;
var worker=new Thread(()=>{
 try{if(Application.Current!=null)throw new InvalidOperationException("Application exists");object Create(string assembly,string name,params object?[] values){var x=Activator.CreateInstance(T(name,assembly),BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance,null,values,null)!;if(x is Window or Application)throw new InvalidOperationException("Forbidden window");if(x is FrameworkElement e&&(e.IsLoaded||PresentationSource.FromVisual(e)!=null))throw new InvalidOperationException("Attached visual");constructors.Add(new{type=name,detached=true});return x;}
 Create("iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Controls.FontIcon");Create("iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Common.FontIconSource");Create("HandheldCompanion","HandheldCompanion.Misc.GlyphIconInfo");Create("HandheldCompanion","HandheldCompanion.Commands.Functions.Windows.OnScreenKeyboardCommands");
 Action action=()=>throw new InvalidOperationException("Action must not execute");Action<object> execute=_=>throw new InvalidOperationException("Action must not execute");Func<object,bool> can=_=>throw new InvalidOperationException("Predicate must not execute");Create("HandheldCompanion","HandheldCompanion.ViewModels.DelegateCommand",action);Create("HandheldCompanion","HandheldCompanion.ViewModels.Commands.RelayCommand",execute,can);Create("iNKORE.UI.WPF.Modern.Controls","iNKORE.UI.WPF.Modern.Controls.Primitives.CommandBarFlyoutCommandBarTemplateSettings");if(Application.Current!=null)throw new InvalidOperationException("Application created");
 }catch(Exception e){constructionFailure=e;}
});worker.SetApartmentState(ApartmentState.STA);worker.Start();if(!worker.Join(30000))throw new TimeoutException("Constructors exceeded budget");
rows.Add(new{name="pure-detached-constructors",status=constructionFailure==null?"passed":"failed",value=(object)new{count=constructors.Count,constructors},error=constructionFailure?.ToString()});
int failed=rows.Count(r=>r.GetType().GetProperty("status")!.GetValue(r)?.ToString()=="failed");
Directory.CreateDirectory(Path.GetDirectoryName(output)!);File.WriteAllText(output,JsonSerializer.Serialize(new{status=failed==0?"passed":"failed",failed,tests=rows,allTypes,loads,native,nativePolicy="Only local COM/HSTRING WinRT system DLLs permitted for the two targets; all other application native imports denied",hardwareAccess=false,hostStarted=false,sdlStarted=false,networkRequests=false,applicationCreated=Application.Current!=null},new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"SOFTWARE {rows.Count-failed}/{rows.Count}; constructors {constructors.Count}; all GetTypes {allTypes.Count}");if(failed>0)Environment.ExitCode=1;

sealed class SoftwareContext:AssemblyLoadContext
{
 readonly string root,shared;readonly List<object> loads,native;readonly HashSet<string> framework;readonly HashSet<Assembly> blocked=[];
 public SoftwareContext(string root,string shared,List<object> loads,List<object> native):base("winrt-software",false){this.root=root;this.shared=shared;this.loads=loads;this.native=native;framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains("Microsoft.WindowsDesktop.App",StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);}
 protected override Assembly? Load(AssemblyName n){if(framework.Contains(n.Name!))return Default.LoadFromAssemblyName(n);string p=Path.Combine(root,n.Name+".dll");if(!File.Exists(p))p=Directory.GetFiles(root,n.Name+".dll",SearchOption.AllDirectories).SingleOrDefault()??Path.Combine(shared,n.Name+".dll");if(!File.Exists(p))throw new FileNotFoundException("Outside pinned consumer set: "+n.FullName);var a=LoadFromAssemblyPath(p);loads.Add(new{requested=n.FullName,actual=a.GetName().FullName,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant(),source=Path.GetDirectoryName(p)==root?"runtime":"shared"});if(blocked.Add(a))NativeLibrary.SetDllImportResolver(a,(name,assembly,search)=>{
 bool target=assembly.GetName().Name is "Microsoft.Windows.SDK.NET" or "WinRT.Runtime";bool system=name.Equals("combase.dll",StringComparison.OrdinalIgnoreCase)||name.Equals("ole32.dll",StringComparison.OrdinalIgnoreCase)||name.Equals("kernel32.dll",StringComparison.OrdinalIgnoreCase)||name.StartsWith("api-ms-win-core-winrt-",StringComparison.OrdinalIgnoreCase)||name.StartsWith("api-ms-win-core-com-",StringComparison.OrdinalIgnoreCase);
 native.Add(new{assembly=assembly.GetName().Name,name,allowed=target&&system});if(!target||!system)throw new DllNotFoundException("Application native blocked: "+name);return NativeLibrary.Load(name,assembly,DllImportSearchPath.System32);});return a;}
 protected override IntPtr LoadUnmanagedDll(string name)=>throw new DllNotFoundException("Unresolved native blocked: "+name);
}





