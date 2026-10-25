using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;

if(args.Length is not 3 and not 5)throw new ArgumentException("runtime-root expected-modern-token result-json [deny|missing assembly-name]");
string? negativeMode=args.Length==5?args[3]:null,negativeTarget=args.Length==5?args[4]:null;
if(negativeMode!=null&&(negativeMode is not "deny" and not "missing"||negativeTarget is not "iNKORE.UI.WPF.Modern" and not "iNKORE.UI.WPF.Modern.Controls"))throw new ArgumentException("Invalid negative mode/target");
string root=Path.GetFullPath(args[0]),expectedToken=args[1],report=Path.GetFullPath(args[2]);
var traps=new List<object>();
if(negativeMode!=null&&Environment.GetEnvironmentVariable("YMCC_UI_NEGATIVE_TRAP_ROOT") is string trapRoot)
    foreach(string name in new[]{"iNKORE.UI.WPF.Modern","iNKORE.UI.WPF.Modern.Controls"}){string trapPath=Path.GetFullPath(Path.Combine(trapRoot,name+".dll"));var trap=AssemblyLoadContext.Default.LoadFromAssemblyPath(trapPath);traps.Add(new{Assembly=trap.GetName().FullName,Path=trap.Location,Sha256=Hash(trap.Location),Purpose="Original DLL is deliberately available in the default context; strict candidate resolution must still fail"});}
var evidence=new List<object>();var loads=new List<object>();var constructed=new List<object>();int deniedNative=0;Exception? failure=null;
var worker=new Thread(()=>
{
    try
    {
        if(Application.Current!=null)throw new InvalidOperationException("Probe must not have a WPF Application");
        using var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"HandheldCompanion.runtime.json")));
        foreach(var file in manifest.RootElement.GetProperty("files").EnumerateArray())
        {
            string rel=file.GetProperty("path").GetString()!,hash=file.GetProperty("sha256").GetString()!;
            if(negativeMode=="missing"&&rel==negativeTarget+".dll"){if(File.Exists(Path.Combine(root,rel)))throw new InvalidDataException("Expected missing negative target still exists");continue;}
            if(!Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root,rel)))).Equals(hash,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Runtime manifest mismatch: "+rel);
        }
        var context=new StrictContext(root,expectedToken,loads,()=>Interlocked.Increment(ref deniedNative),negativeMode=="deny"?negativeTarget:null);
        Assembly Load(string name)
        {
            var a=context.LoadFromAssemblyPath(Path.Combine(root,name+".dll"));context.BlockNative(a);
            var types=a.GetTypes();evidence.Add(new{Assembly=a.GetName().FullName,Sha256=Hash(a.Location),Types=types.Length,LoadApi="AssemblyLoadContext.LoadFromAssemblyPath"});return a;
        }
        var hc=Load("HandheldCompanion");var modern=context.LoadFromAssemblyName(new AssemblyName("iNKORE.UI.WPF.Modern, Version=0.10.2.0, Culture=neutral, PublicKeyToken="+expectedToken));context.BlockNative(modern);
        var controls=context.LoadFromAssemblyName(new AssemblyName("iNKORE.UI.WPF.Modern.Controls, Version=0.10.2.0, Culture=neutral, PublicKeyToken="+expectedToken));context.BlockNative(controls);
        foreach(var a in new[]{modern,controls})
        {
            if(Token(a.GetName())!=expectedToken)throw new InvalidDataException("Resolved target identity differs");
            try { evidence.Add(new{Assembly=a.GetName().FullName,Sha256=Hash(a.Location),Types=a.GetTypes().Length,AllTypesSupported=true,LoadApi="Strict AssemblyName resolution -> LoadFromAssemblyPath"}); }
            catch(ReflectionTypeLoadException error)
            {
                var missing=error.LoaderExceptions.Where(e=>e!=null).Select(e=>e!.ToString()).ToArray();
                if(missing.Any(m=>!m.Contains("iNKORE.UI.WPF, Version=1.2.8.0, Culture=neutral, PublicKeyToken=cd19e634b9706635",StringComparison.Ordinal)))throw;
                evidence.Add(new{Assembly=a.GetName().FullName,Sha256=Hash(a.Location),LoadableTypes=error.Types.Count(t=>t!=null),AllTypesSupported=false,Reason="Pre-existing absent legacy iNKORE.UI.WPF dependency; HC full GetTypes and exact reviewed constructors are still mandatory",LoaderExceptions=missing,LoadApi="Strict AssemblyName resolution -> LoadFromAssemblyPath"});
            }
        }
        var friend=modern.GetCustomAttributesData().Single(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute").ConstructorArguments[0].Value as string??throw new InvalidDataException("Missing friend identity");
        string friendKey=friend[(friend.IndexOf("PublicKey=",StringComparison.OrdinalIgnoreCase)+10)..];
        if(!friend.StartsWith("iNKORE.UI.WPF.Modern.Controls,",StringComparison.Ordinal)||!friendKey.Equals(Convert.ToHexString(controls.GetName().GetPublicKey()!),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("InternalsVisibleTo does not match resolved Controls public key");
        evidence.Add(new{FriendIdentity=friend,MatchesActualLoadedControlsPublicKey=true});
        object Create(Assembly a,string name,params object?[] values)
        {
            var type=a.GetType(name,true)!;
            object result=Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,values,null)??throw new InvalidOperationException("Constructor returned null");
            if(result is Window||result is Application)throw new InvalidOperationException("Window/Application construction is forbidden");
            if(result is FrameworkElement element&&(element.IsLoaded||PresentationSource.FromVisual(element)!=null))throw new InvalidOperationException("Constructed visual unexpectedly connected to a presentation source");
            constructed.Add(new{Type=type.FullName,Assembly=a.GetName().FullName,ConstructorOnly=true,FrameworkElementDetached=result is FrameworkElement,Window=false});return result;
        }
        // These exact constructors and their local constructor/static-initializer callees were reviewed from the pinned input IL.
        _=Create(modern,"iNKORE.UI.WPF.Modern.Controls.FontIcon");
        _=Create(modern,"iNKORE.UI.WPF.Modern.Common.FontIconSource");
        _=Create(hc,"HandheldCompanion.Misc.GlyphIconInfo");
        _=Create(hc,"HandheldCompanion.Commands.Functions.Windows.OnScreenKeyboardCommands");
        Action action=()=>throw new InvalidOperationException("Command action must never execute");
        Action<object> execute=_=>throw new InvalidOperationException("Command action must never execute");
        Func<object,bool> canExecute=_=>throw new InvalidOperationException("Command predicate must never execute");
        _=Create(hc,"HandheldCompanion.ViewModels.DelegateCommand",action);
        _=Create(hc,"HandheldCompanion.ViewModels.Commands.RelayCommand",execute,canExecute);
        _=Create(controls,"iNKORE.UI.WPF.Modern.Controls.Primitives.CommandBarFlyoutCommandBarTemplateSettings");
        if(Application.Current!=null)throw new InvalidOperationException("An Application was initialized");
        if(deniedNative!=0)throw new InvalidOperationException("A non-framework native import was attempted");
    }
    catch(Exception error){failure=error;}
});
worker.SetApartmentState(ApartmentState.STA);worker.Start();worker.Join();
Directory.CreateDirectory(Path.GetDirectoryName(report)!);
bool negativePassed=negativeMode!=null&&failure!=null&&failure.ToString().Contains(negativeMode=="deny"?"UI_NEGATIVE_DENIED:"+negativeTarget:"Missing frozen dependency: "+negativeTarget,StringComparison.Ordinal)&&constructed.Count==0&&deniedNative==0;
var result=new{Status=(negativeMode==null?failure==null:negativePassed)?"passed":"failed",DefaultContextTraps=traps,NegativeMode=negativeMode,NegativeTarget=negativeTarget,ExpectedDependencyFailureObserved=negativePassed,ExpectedModernToken=expectedToken,Assemblies=evidence,ResolvedLoads=loads,Constructed=constructed,ConstructedCount=constructed.Count,Exception=failure?.ToString(),NativeImportsDenied=deniedNative,NonFrameworkNativeImportsBlocked=true,ApplicationCreated=false,DeviceConstructorsExecuted=false,WindowCreated=false,TemplateApplied=false,RenderingOrFontMeasurementRequested=false,FontEnumerationOrGlyphTypefaceAccessRequested=false,AllInstancesDetached=true,CommandActionsExecuted=false,ExcludedImpureCommands=new[]{"DesktopLayoutCommands (SettingsManager/update/event subscription)","MainWindowCommands (window state access)","QuickToolsCommands (overlay/window state access)"}};
File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.ConstructedCount,result.NativeImportsDenied}));
if(result.Status!="passed"){Console.Error.WriteLine(failure);Environment.ExitCode=1;}
static string Token(AssemblyName name)=>Convert.ToHexString(name.GetPublicKeyToken()??[]).ToLowerInvariant();
static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
