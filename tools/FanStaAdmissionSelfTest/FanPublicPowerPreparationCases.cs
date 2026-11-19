using System.IO;
using System.Linq.Expressions;
using System.Reflection;
using System.Runtime.Loader;

internal static class FanPublicPowerPreparationCases
{
    private const BindingFlags Flags=BindingFlags.Instance|BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
    private static object? Field(object value,string name)=>value.GetType().GetField(name,Flags)!.GetValue(value);
    private static object? Call(object value,string name,params object?[] args)
    {
        var type=value as Type??value.GetType();var method=type.GetMethods(Flags).Single(m=>m.Name==name&&!m.IsGenericMethod&&m.GetParameters().Length>=args.Length&&m.GetParameters().Skip(args.Length).All(p=>p.IsOptional));
        try{return method.Invoke(value is Type?null:value,args.Concat(Enumerable.Repeat<object?>(Type.Missing,method.GetParameters().Length-args.Length)).ToArray());}
        catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    private static Exception? Error(Action action){try{action();return null;}catch(Exception error){return error;}}
    private static string Code(Exception? error)=>error?.GetType().GetProperty("Code",Flags)?.GetValue(error)?.ToString()??"none";
    internal static void Run(Assembly host,Func<object> create,Action<object,Action,string> legacy,Action<string,bool,string> check,Action<object> scenario,string selection)
    {
        var names=new[]{"prepare-msi","prepare-onex","prepare-source-open"};
        if(selection=="prepare-msi-registration")
        {
            var backend=create();using var scope=FanPublicPowerScopePort.Create(host,2);IDisposable? request=null;var ok=true;
            void C(string text,bool value){ok&=value;check(selection+": "+text,value,"");}
            try
            {
                var route=Call(backend.GetType(),"ResolveRouteForSelfTest","HandheldCompanion.Devices.ClawA2VM");
                backend.GetType().GetField("routeOverrideForSelfTest",Flags)!.SetValue(backend,route);
                backend.GetType().GetField("device",Flags)!.SetValue(backend,new MsiPreparationDevicePort());
                request=(IDisposable)Call(backend,"BindImportedPowerForRequest",scope,scope.Purpose)!;
                scope.Revoke();
                // Candidate predicate throws at original orchestrator's very
                // first Current, before its real WMI/PnP functions can run.
                var error=Error(()=>Call(backend,"PrepareMsiWmiRegistrationBeforeOpen",(long)Field(backend,"suspendGateEpoch")!));
                C("original registration wrapper keeps public cancellation code",Code(error)=="FAN_PUBLIC_POWER_SUPERSEDED");
                C("cancelled preparation never consumes PnP restart attempt",Field(backend,"msiWmiRegistrationRestartAttempted")is false);
                request.Dispose();request=null;
                C("original wrapper returns only local borrow",scope.Disposals==1&&scope.HandlesClosed&&scope.Settlements==0);
            }
            catch(Exception error){ok=false;check(selection+": original wrapper completed",false,error.ToString());}
            finally{request?.Dispose();backend.GetType().GetField("device",Flags)!.SetValue(backend,null);Call(backend,"Dispose");}
            scenario(new{name=selection,ok,actualRegistrationOrchestratorEntry=true,NativeAndDeviceIdentityArePorts=true,physicalWrites=0});return;
        }
        if(selection!="prepare-all"&&!names.Contains(selection,StringComparer.Ordinal))throw new ArgumentException("Unknown public preparation case");
        var leaf=AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(Path.GetDirectoryName(host.Location)!,"YeManFanBackend.dll"));
        foreach(var name in names.Where(n=>selection=="prepare-all"||n==selection))
        {
            var backend=create();using var scope=FanPublicPowerScopePort.Create(host,2);IDisposable? request=null;FanPreparationTransportPort? terminal=null;
            var ok=true;void C(string text,bool value){ok&=value;check(name+": "+text,value,"");}
            try
            {
                if(name=="prepare-source-open")
                {
                    Call(backend,"Dispose");
                    var directory=Environment.GetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT")!;Directory.CreateDirectory(directory);
                    var auth=Path.Combine(directory,"public-preparation-authorization.md");
                    File.WriteAllText(auth,"hardwareWriteAuthorizationGranted: true\nallMappedFanRoutesAuthorized: true\n");
                    const string token="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
                    var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",Flags)!.Invoke(null,new object[]{new[]{"--allow-hardware-writes","--authorization",auth,"--confirm",token,"--session-token",token}})!;
                    backend=Activator.CreateInstance(host.GetType("YeManFanHost.RealHcBackend",true)!,Flags,null,new[]{options},null)!;
                    terminal=FanPreparationTransportPort.Create(leaf);
                    var contract=leaf.GetType("YeManFanBackend.Capabilities.IPinnedFanTransport",true)!;
                    Func<object> factory=()=>{scope.Revoke();return terminal;};
                    var exact=Expression.Lambda(typeof(Func<>).MakeGenericType(contract),Expression.Convert(Expression.Invoke(Expression.Constant(factory)),contract)).Compile();
                    Call(backend,"InstallPinnedSourceConsumerForSelfTest","AYANEO","AYANEO 3",exact);
                    request=(IDisposable)Call(backend,"BindImportedPowerForRequest",scope,scope.Purpose)!;
                    Action open=()=>Call(backend,"OpenPinnedSourceCore");
                    var error=Error(()=>Call(backend,"OnStaPowerBounded",open,name));
                    C("actual factory cancellation preserves normal public code",Code(error)=="FAN_PUBLIC_POWER_SUPERSEDED");
                    C("retired source factory cannot continue Initialize or read",terminal.Initializations==0&&terminal.Reads==0&&terminal.Writes==0);
                    C("actual source owner returns its exact terminal",terminal.Disposals==1&&!terminal.Ready&&Field(backend,"openAttempted")is false);
                    C("original source cleanup is still callable",Error(()=>legacy(backend,()=>Call(backend,"RetirePinnedSourceCore"),name+"-return"))is null);
                }
                else
                {
                    request=(IDisposable)Call(backend,"BindImportedPowerForRequest",scope,scope.Purpose)!;
                    var predicate=name=="prepare-msi"?"MsiWmiOpenStillCurrent":"OneXFanOpenStillCurrent";
                    var epoch=(long)Field(backend,"suspendGateEpoch")!;
                    C("valid FanResume retains original preparation admission",Call(backend,predicate,epoch)is true);
                    scope.Revoke();
                    C("retired public scope cannot continue local preparation",Code(Error(()=>Call(backend,predicate,epoch)))=="FAN_PUBLIC_POWER_SUPERSEDED");
                    C("local suspend epoch was not fabricated",Equals(Field(backend,"suspendGateEpoch"),epoch));
                }
                request.Dispose();request=null;
                C("actual returned request closes import without resource ACK",scope.Disposals==1&&scope.HandlesClosed&&scope.Settlements==0);
            }
            catch(Exception error){ok=false;check(name+": original preparation completed",false,error.ToString());}
            finally{request?.Dispose();Call(backend,"Dispose");}
            scenario(new{name,ok,actualHostPreparationPredicate=true,actualSourceSession=name=="prepare-source-open",NativeAndKernelArePorts=true,physicalWrites=0});
        }
    }
}
public sealed class MsiPreparationDevicePort { public string WmiPath => "MSI_ACPI.InstanceName='ACPI\\PNP0C14\\MSI_0'"; }
public class FanPreparationTransportPort:DispatchProxy
{
    public int Initializations,Reads,Writes,Disposals;public bool Ready=true;
    public static FanPreparationTransportPort Create(Assembly leaf)=>(FanPreparationTransportPort)DispatchProxy.Create(leaf.GetType("YeManFanBackend.Capabilities.IPinnedFanTransport",true)!,typeof(FanPreparationTransportPort));
    protected override object? Invoke(MethodInfo? method,object?[]? args)
    {
        switch(method!.Name){case "get_EndpointIdentity":return "fixture:public-preparation";case "get_IsReady":return Ready;case "Initialize":Initializations++;return null;case "ReadByte":Reads++;return (byte)0;case "WriteByte":Writes++;return null;case "PrepareRetirement":return null;case "Dispose":Disposals++;Ready=false;return null;default:throw new NotSupportedException(method.Name);}
    }
}
