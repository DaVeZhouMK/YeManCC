using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Emit=System.Reflection.Emit.OpCodes;
if(args.Length!=4)throw new ArgumentException("runtime-root expected-hc-sha256 candidate-or-baseline report-json");
string root=Path.GetFullPath(args[0]),expected=args[1],mode=args[2],report=Path.GetFullPath(args[3]);
if(mode!="candidate"&&mode!="baseline")throw new ArgumentException("Invalid mode");
var nativeAttempts=new List<object>();var checks=new List<string>();var resolved=new List<object>();var timing=new List<object>();Exception? failure=null;object? prefixEvidence=null;int allTypes=0;bool expectedBaselineFailure=false;
string[] Modules()=>Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Where(m=>m.ModuleName.Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)).Select(m=>m.FileName).ToArray();
var modulesBefore=Modules();var libraries=Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories).Where(p=>Path.GetFileName(p).Equals("SDL3.dll",StringComparison.OrdinalIgnoreCase)).ToArray();
void Check(bool valid,string name){if(!valid)throw new InvalidOperationException(name);checks.Add(name);}
try
{
    Check(OperatingSystem.IsWindows(),"Windows QPC/QPF profile");Check(libraries.Length==0,"SDL3.dll physically absent from the complete runtime directory");Check(modulesBefore.Length==0,"Fresh process initially has no SDL3 module");
    string hcPath=Path.Combine(root,"HandheldCompanion.dll");Check(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(hcPath))).Equals(expected,StringComparison.OrdinalIgnoreCase),"Pinned HC SHA256");
    var context=new TimerContext(root,nativeAttempts,resolved);var hc=context.LoadFromAssemblyPath(hcPath);context.Block(hc);allTypes=hc.GetTypes().Length;
    var sdlt=hc.GetType("HandheldCompanion.Controllers.SDLController",true)!;var dummy=hc.GetType("HandheldCompanion.Controllers.Dummies.DummyXbox360Controller",true)!;
    // No constructor is invoked. This instance only carries the exact field-initializer prefix below.
    object instance=RuntimeHelpers.GetUninitializedObject(dummy);GC.SuppressFinalize(instance);
    using var cecil=AssemblyDefinition.ReadAssembly(hcPath);var ctor=cecil.MainModule.Types.Single(t=>t.FullName==sdlt.FullName).Methods.Single(m=>m.Name==".ctor"&&m.Parameters.Count==0);
    var instructions=ctor.Body.Instructions.TakeWhile(i=>i.Offset<57).ToArray();
    Check(instructions.Length==17&&ctor.Body.Instructions.Single(i=>i.Offset==57).OpCode==Mono.Cecil.Cil.OpCodes.Ldarg_0&&ctor.Body.Instructions.Single(i=>i.Offset==58).Operand is MethodReference parent&&parent.FullName=="System.Void HandheldCompanion.Controllers.IController::.ctor()","Exact prefix ends at IL_0039 before the IController base call");
    Check(instructions[^1].Offset==52&&instructions[^1].OpCode==Mono.Cecil.Cil.OpCodes.Stfld&&((FieldReference)instructions[^1].Operand).Name=="freq","Prefix includes the full original field initialization");
    var dynamic=new DynamicMethod("ExactReviewedSdlDefaultFieldInitializers",typeof(void),[typeof(object)],hc.ManifestModule,true);var generator=dynamic.GetILGenerator();
    foreach(var il in instructions)
    {
        switch(il.OpCode.Code)
        {
            case Code.Ldarg_0: generator.Emit(Emit.Ldarg_0);generator.Emit(Emit.Castclass,sdlt);break;
            case Code.Ldsfld: generator.Emit(Emit.Ldsfld,hc.ManifestModule.ResolveField(((FieldReference)il.Operand).MetadataToken.ToInt32())!);break;
            case Code.Stfld: generator.Emit(Emit.Stfld,hc.ManifestModule.ResolveField(((FieldReference)il.Operand).MetadataToken.ToInt32())!);break;
            case Code.Call:
                var method=hc.ManifestModule.ResolveMethod(((MethodReference)il.Operand).MetadataToken.ToInt32()) as MethodInfo??throw new InvalidDataException("Only static methods in the prefix");
                Check(method.IsStatic,"Reviewed prefix call is static: "+method.DeclaringType?.FullName+"."+method.Name);generator.Emit(Emit.Call,method);break;
            case Code.Conv_R_Un: generator.Emit(Emit.Conv_R_Un);break;
            case Code.Conv_R4: generator.Emit(Emit.Conv_R4);break;
            default:throw new InvalidDataException("Unreviewed prefix opcode: "+il.OpCode);
        }
    }
    generator.Emit(Emit.Ret);var initialize=dynamic.CreateDelegate<Action<object>>();
    if(mode=="candidate")
    {
        var helper=hc.GetType("HandheldCompanion.YmccDeviceCore.BclPerformanceTimer",true)!;Check(helper.TypeInitializer==null,"BCL helper has no type initializer");
        var counter=helper.GetMethod("GetCounter",BindingFlags.Static|BindingFlags.NonPublic)!;var frequency=helper.GetMethod("GetFrequency",BindingFlags.Static|BindingFlags.NonPublic)!;
        ulong Counter()=>(ulong)counter.Invoke(null,null)!;ulong Frequency()=>(ulong)frequency.Invoke(null,null)!;
        Check(WindowsClock.QueryPerformanceFrequency(out long qpf)&&qpf>0&&Stopwatch.IsHighResolution,"System QPF available and high resolution");Check(Frequency()==unchecked((ulong)qpf)&&Frequency()==unchecked((ulong)Stopwatch.Frequency),"Frequency uses QPC ticks per second without rescaling");
        ulong previous=Counter();for(int i=0;i<10000;i++){ulong next=Counter();if(next<previous)throw new InvalidOperationException("Counter regressed");previous=next;}checks.Add("10000 consecutive raw counters are monotonic");
        for(int i=0;i<128;i++){Check(WindowsClock.QueryPerformanceCounter(out long begin),"QPC lower bracket available");ulong value=Counter();Check(WindowsClock.QueryPerformanceCounter(out long end),"QPC upper bracket available");if(value<unchecked((ulong)begin)||value>unchecked((ulong)end))throw new InvalidOperationException("BCL helper counter differs from raw QPC domain");if(i<4)timing.Add(new{Lower=begin,Counter=value,Upper=end});}checks.Add("128 raw QPC brackets contain helper output");
        long start=Stopwatch.GetTimestamp();ulong first=Counter();Thread.Sleep(20);ulong last=Counter();long stop=Stopwatch.GetTimestamp();double seconds=(last-first)/(double)Frequency();Check(last>first&&seconds>0&&seconds<=(stop-start)/(double)Stopwatch.Frequency,"Relative time uses the unchanged counter/frequency units");
        var conversion=new DynamicMethod("ExactUnsignedBitConversion",typeof(ulong),[typeof(long)]);var conv=conversion.GetILGenerator();conv.Emit(Emit.Ldarg_0);conv.Emit(Emit.Conv_U8);conv.Emit(Emit.Ret);var convert=conversion.CreateDelegate<Func<long,ulong>>();foreach(long value in new[]{0L,1L,long.MaxValue,long.MinValue,-1L})Check(convert(value)==unchecked((ulong)value),"conv.u8 preserves 64 bits for "+value);
        WindowsClock.QueryPerformanceCounter(out long beforePrefix);initialize(instance);WindowsClock.QueryPerformanceCounter(out long afterPrefix);
        var flags=BindingFlags.NonPublic|BindingFlags.Public|BindingFlags.Instance;ulong initializedCounter=(ulong)sdlt.GetField("lastCounter",flags)!.GetValue(instance)!;float initializedFrequency=(float)sdlt.GetField("freq",flags)!.GetValue(instance)!;
        Check(initializedCounter>=unchecked((ulong)beforePrefix)&&initializedCounter<=unchecked((ulong)afterPrefix),"Real field initializer prefix stores a QPC counter");Check(initializedFrequency==(float)unchecked((ulong)qpf),"Original conv.r.un / conv.r4 frequency storage is preserved");Check((IntPtr)sdlt.GetField("gamepad",flags)!.GetValue(instance)! == IntPtr.Zero,"No real gamepad handle created");
        prefixEvidence=new{Bytes=57,Source="Actual loaded candidate SDLController default constructor IL, through IL_0034 stfld freq",InstanceAllocation="RuntimeHelpers.GetUninitializedObject(DummyXbox360Controller), no constructor",InitializedCounter=initializedCounter,InitializedFrequency=initializedFrequency,RawSystemFrequency=qpf,RelativeSeconds=seconds,BaseConstructorExecuted=false,ControllerManagerTypeInitializerExecuted=false,FullDummyConstructorExecuted=false};
        Check(nativeAttempts.Count==0,"No HC/SDL3-CS or ALC non-framework native import was attempted");
    }
    else
    {
        try{initialize(instance);throw new InvalidOperationException("Original SDL timer prefix unexpectedly succeeded without SDL3.dll");}
        catch(DllNotFoundException){Check(nativeAttempts.Any(x=>JsonSerializer.Serialize(x).Contains("SDL3",StringComparison.OrdinalIgnoreCase)),"Original prefix was blocked specifically at SDL3 native timer import");expectedBaselineFailure=true;}
        prefixEvidence=new{Bytes=57,ExpectedNativeFailure=true,BaseConstructorExecuted=false,FullDummyConstructorExecuted=false};
    }
    Check(Modules().Length==0,"No SDL3 process module after the complete probe");
}
catch(Exception error){failure=error;}
var result=new{Status=failure==null?"passed":"failed",Mode=mode,ProcessId=Environment.ProcessId,LoadApi="AssemblyLoadContext.LoadFromAssemblyPath",HcSha256=expected,HcAllGetTypes=allTypes,RuntimeRoot=root,SdlDllFiles=libraries,ProcessModulesBefore=modulesBefore,ProcessModulesAfter=Modules(),AllSdlNamedModules=Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Where(m=>m.ModuleName.Contains("SDL3",StringComparison.OrdinalIgnoreCase)).Select(m=>new{m.ModuleName,m.FileName}).ToArray(),NativeAttempts=nativeAttempts,NativeAttemptCount=nativeAttempts.Count,AllNonFrameworkNativeImportsBlocked=true,ResolvedLoads=resolved,Prefix=prefixEvidence,TimingSamples=timing,ExpectedBaselineFailure=expectedBaselineFailure,Checks=checks,Exception=failure?.ToString(),DeviceConstructorsExecuted=false,FullControllerConstructorsExecuted=false,SdlInitExecuted=false,ProductHostExecuted=false,WindowOrRendering=false};
Directory.CreateDirectory(Path.GetDirectoryName(report)!);File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.Mode,result.HcAllGetTypes,result.NativeAttemptCount,result.ProcessId}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}
static class WindowsClock
{
    [DllImport("kernel32.dll",ExactSpelling=true)][return:MarshalAs(UnmanagedType.Bool)]internal static extern bool QueryPerformanceCounter(out long counter);
    [DllImport("kernel32.dll",ExactSpelling=true)][return:MarshalAs(UnmanagedType.Bool)]internal static extern bool QueryPerformanceFrequency(out long frequency);
}
sealed class TimerContext:AssemblyLoadContext
{
    readonly string root;readonly List<object> attempts,loads;readonly HashSet<string> framework;readonly HashSet<Assembly> blocked=[];
    public TimerContext(string root,List<object> attempts,List<object> loads):base("sdl-timer-software",false)
    {
        this.root=root;this.attempts=attempts;this.loads=loads;framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);
    }
    public void Block(Assembly a){if(blocked.Add(a))NativeLibrary.SetDllImportResolver(a,(name,assembly,_)=>{attempts.Add(new{Library=name,Assembly=assembly.GetName().Name,Stage="DllImportResolver"});throw new DllNotFoundException("Non-framework native import blocked: "+name);});}
    protected override Assembly? Load(AssemblyName requested)
    {
        if(requested.Name!=null&&framework.Contains(requested.Name))return Default.LoadFromAssemblyName(requested);
        string name=requested.Name??throw new FileLoadException("Missing assembly name");if(name!=Path.GetFileName(name)||name.Contains(':'))throw new FileLoadException("Invalid assembly name");string path=Path.Combine(root,name+".dll");if(!File.Exists(path))throw new FileNotFoundException("Missing frozen metadata dependency",path);
        var a=LoadFromAssemblyPath(path);Block(a);loads.Add(new{Requested=requested.FullName,Resolved=a.FullName,Path=path});return a;
    }
    protected override IntPtr LoadUnmanagedDll(string name){attempts.Add(new{Library=name,Stage="ALC.LoadUnmanagedDll"});throw new DllNotFoundException("Non-framework native load blocked: "+name);}
}