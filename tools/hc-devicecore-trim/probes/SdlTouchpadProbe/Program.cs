using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
if(args.Length!=4)throw new ArgumentException("runtime-root-or-oracle-sdl-path expected-sha256 candidate-zero|candidate-nonzero|baseline-zero|native-null-oracle report-json");
string input=Path.GetFullPath(args[0]),expected=args[1],mode=args[2],report=Path.GetFullPath(args[3]);
string[] NativeSdl()=>Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Where(m=>m.ModuleName.Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)).Select(m=>m.FileName).ToArray();
var before=NativeSdl();var attempts=new List<object>();var operations=new List<object>();var resolvers=new List<string>();var loads=new List<object>();Exception? failure=null;int allTypes=0;object? oracle=null;
void Check(bool yes,string message){if(!yes)throw new InvalidOperationException(message);}
try
{
    Check(before.Length==0,"Fresh process must not already contain native SDL3");
    if(mode=="native-null-oracle")
    {
        Check(Path.GetFileName(input).Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase),"Oracle requires the pinned original native SDL3.dll");Check(Hash(input)==expected,"Oracle file hash mismatch");Check(expected=="2a2704678bf6c9c6a944270ab35079df76f5afe92b780394ed72d9c8218b98d8","Only the reviewed local SDL 3.4.12 oracle is allowed");
        IntPtr handle=NativeLibrary.Load(input);try
        {
            var touchpads=Marshal.GetDelegateForFunctionPointer<NullTouchpads>(NativeLibrary.GetExport(handle,"SDL_GetNumGamepadTouchpads"));
            var fingers=Marshal.GetDelegateForFunctionPointer<NullFingers>(NativeLibrary.GetExport(handle,"SDL_GetNumGamepadTouchpadFingers"));
            int pads=touchpads(IntPtr.Zero),count=fingers(IntPtr.Zero,0);Check(pads==0&&count==0,"Local original SDL NULL-handle count semantics differ");
            oracle=new{File=input,Sha256=expected,FileVersion=FileVersionInfo.GetVersionInfo(input).FileVersion,GetNumGamepadTouchpads=pads,GetNumGamepadTouchpadFingers=count,TouchpadIndex=0,NativeCalls=2,AllHandlesZero=true,ModulesDuringCall=NativeSdl(),SdlInitCalled=false,DeviceEnumerationCalled=false,NonzeroHandleCalled=false};
        }
        finally{NativeLibrary.Free(handle);}
    }
    else
    {
        Check(mode is "candidate-zero" or "candidate-nonzero" or "baseline-zero","Unknown probe mode");Check(!Directory.EnumerateFiles(input,"*",SearchOption.AllDirectories).Any(p=>Path.GetFileName(p).Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)),"Native SDL3.dll must be physically absent recursively");
        string hcPath=Path.Combine(input,"HandheldCompanion.dll");Check(Hash(hcPath)==expected,"Pinned HC hash mismatch");var context=new GuardContext(input,attempts,resolvers,loads);var hc=context.LoadFromAssemblyPath(hcPath);context.Block(hc);allTypes=hc.GetTypes().Length;
        var sdl=hc.GetType("HandheldCompanion.Controllers.SDLController",true)!;var pads=sdl.GetMethod("GetTouchpads",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!;var fingers=sdl.GetMethod("GetTouchpadFingers",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!;var init=sdl.GetMethod("InitializeInputOutput",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!;
        Check(pads.MetadataToken==0x06002847&&fingers.MetadataToken==0x06002848&&init.MetadataToken==0x0600284f,"Reviewed method tokens changed");
        foreach(string name in new[]{"HandheldCompanion.Controllers.Dummies.DummyXbox360Controller","HandheldCompanion.Controllers.Dummies.DummySwitchProController"})
        {
            var receiver=hc.GetType(name,true)!;Check(receiver.GetMethod("GetTouchpads",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.DeclaringType==sdl&&receiver.GetMethod("InitializeInputOutput",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.DeclaringType==sdl,"Receiver dispatch differs from the reviewed SDL implementation");
            object instance=RuntimeHelpers.GetUninitializedObject(receiver);GC.SuppressFinalize(instance);
            FieldInfo Field(string name){for(Type? t=receiver;t!=null;t=t.BaseType){var f=t.GetField(name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.DeclaredOnly);if(f!=null)return f;}throw new MissingFieldException(name);}
            var gamepad=Field("gamepad");gamepad.SetValue(instance,mode=="candidate-nonzero"?new IntPtr(0x1234):IntPtr.Zero);
            foreach(string fieldName in new[]{"_supportedButtons","_supportedAxes"}){var f=Field(fieldName);object empty=f.FieldType.GetProperty("Empty",BindingFlags.Public|BindingFlags.Static)!.GetValue(null)!;f.SetValue(instance,empty);}
            Field("HasGyro").SetValue(instance,false);
            foreach(string fieldName in new[]{"SourceButtons","SourceAxis","TargetButtons","TargetAxis"}){var f=Field(fieldName);Check(f.FieldType.IsGenericType&&f.FieldType.GetGenericTypeDefinition()==typeof(List<>),"Only BCL List fields may be allocated");f.SetValue(instance,Activator.CreateInstance(f.FieldType));}
            void Invoke(MethodInfo method,object?[] values,bool expectNative)
            {
                string operation=name+" -> "+method.DeclaringType!.FullName+"::"+method.Name+"("+string.Join(",",values.Select(v=>v?.ToString()))+")";context.Operation=operation;int start=attempts.Count;
                if(expectNative)
                {
                    Exception? blocked=null;try{method.Invoke(instance,values);}catch(TargetInvocationException error){blocked=error.GetBaseException();}
                    Check(blocked is DllNotFoundException&&attempts.Count==start+1,"Expected one original SDL native import to be denied: "+operation);
                    string? library=JsonSerializer.SerializeToElement(attempts[^1]).GetProperty("Library").GetString();Check(library is "SDL3" or "SDL3.dll","Expected original SDL native library, found "+library);
                    operations.Add(new{Receiver=name,Method=method.Name,Arguments=values,Gamepad=gamepad.GetValue(instance)!.ToString(),ExpectedNativeDenied=true,NativeAttempts=attempts.Count-start,Exception=blocked!.Message});
                }
                else
                {
                    object? result=method.Invoke(instance,values);Check(attempts.Count==start,"Zero-handle method attempted native access");if(method.ReturnType==typeof(int))Check((int)result! ==0,"Zero-handle count must be zero");
                    operations.Add(new{Receiver=name,Method=method.Name,Arguments=values,Gamepad=gamepad.GetValue(instance)!.ToString(),Result=result,NativeAttempts=0,ActualUnmodifiedInitializeInputOutput=method==init});
                }
            }
            bool negative=mode!="candidate-zero";Invoke(pads,[],negative);
            foreach(int index in mode=="candidate-zero"?new[]{int.MinValue,-1,0,1,int.MaxValue}:new[]{0})Invoke(fingers,[index],negative);
            if(mode!="candidate-nonzero")Invoke(init,[],mode=="baseline-zero");
            if(mode=="candidate-zero")foreach(string fieldName in new[]{"SourceButtons","SourceAxis","TargetButtons","TargetAxis"})Check((int)Field(fieldName).FieldType.GetProperty("Count")!.GetValue(Field(fieldName).GetValue(instance))! ==0,"Zero-state initialization unexpectedly changed managed button/axis lists");
        }
        Check(resolvers.Any(n=>n.StartsWith("HandheldCompanion,",StringComparison.Ordinal))&&resolvers.Any(n=>n.StartsWith("SDL3-CS,",StringComparison.Ordinal)),"HC and SDL3-CS must both have native rejection resolvers");
        if(mode=="candidate-zero")Check(attempts.Count==0,"Positive zero-handle test must have no native attempts");
        Check(NativeSdl().Length==0,"No native SDL3 process module may be loaded");
    }
}
catch(Exception error){failure=error;}
var result=new{Status=failure==null?"passed":"failed",Mode=mode,ProcessId=Environment.ProcessId,Input=input,ExpectedSha256=expected,LoadApi=mode=="native-null-oracle"?"NativeLibrary.Load (two authorized NULL-handle queries only)":"AssemblyLoadContext.LoadFromAssemblyPath",HcAllGetTypes=allTypes,ProcessModulesBefore=before,ProcessModulesAfter=NativeSdl(),AllSdlNamedModules=Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Where(m=>m.ModuleName.Contains("SDL3",StringComparison.OrdinalIgnoreCase)).Select(m=>new{m.ModuleName,m.FileName}).ToArray(),NativeAttempts=attempts,NativeAttemptCount=attempts.Count,NativeResolversInstalled=resolvers,ResolvedLoads=loads,Operations=operations,Oracle=oracle,Exception=failure?.ToString(),AllNonFrameworkNativeDenied=mode!="native-null-oracle",ObjectCreation="GetUninitializedObject + SuppressFinalize; seed only zero/sentinel gamepad, false HasGyro, BCL FrozenSet.Empty and empty BCL List fields",FullDummyConstructorExecuted=false,DeviceConstructorsExecuted=false,ManagerFactoryOrControllerManagerInitializerExecuted=false,SdlInitExecuted=false,DeviceEnumerationExecuted=false,ProductHostExecuted=false,WindowOrRendering=false};
Directory.CreateDirectory(Path.GetDirectoryName(report)!);File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.Mode,result.ProcessId,result.HcAllGetTypes,result.NativeAttemptCount,Operations=operations.Count}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}
static string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate int NullTouchpads(IntPtr gamepad);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate int NullFingers(IntPtr gamepad,int touchpad);
sealed class GuardContext:AssemblyLoadContext
{
    readonly string root;readonly List<object> attempts,loads;readonly List<string> installed;readonly HashSet<string> framework;readonly HashSet<Assembly> blocked=[];public string Operation {get;set;}="metadata-load";
    public GuardContext(string root,List<object> attempts,List<string> installed,List<object> loads):base("sdl-touchpad-software",false){this.root=root;this.attempts=attempts;this.installed=installed;this.loads=loads;framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);}
    public void Block(Assembly a){if(blocked.Add(a)){NativeLibrary.SetDllImportResolver(a,(name,assembly,_)=>{attempts.Add(new{Library=name,Assembly=assembly.GetName().Name,Operation,Stage="DllImportResolver"});throw new DllNotFoundException("Native access denied: "+name);});installed.Add(a.FullName!);}}
    protected override Assembly? Load(AssemblyName requested){if(requested.Name!=null&&framework.Contains(requested.Name))return Default.LoadFromAssemblyName(requested);string name=requested.Name??throw new FileLoadException("Missing name");if(name!=Path.GetFileName(name)||name.Contains(':'))throw new FileLoadException("Invalid dependency name");var a=LoadFromAssemblyPath(Path.Combine(root,name+".dll"));Block(a);loads.Add(new{Requested=requested.FullName,Resolved=a.FullName});return a;}
    protected override IntPtr LoadUnmanagedDll(string name){attempts.Add(new{Library=name,Operation,Stage="ALC.LoadUnmanagedDll"});throw new DllNotFoundException("Native loading denied: "+name);}
}