using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

internal static class Program
{
 const BindingFlags F=BindingFlags.Instance|BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
 static Assembly host=null!;static Type backendType=null!;static int checks;static readonly List<string> failures=[];
 static void Set(object b,string n,object? v)=>backendType.GetField(n,F)!.SetValue(b,v);
 static long Get(object b,string n)=>(long)backendType.GetField(n,F)!.GetValue(b)!;
 static void Check(string name,Action action){checks++;try{action();}catch(Exception e){failures.Add(name+": "+e.Message);}}
 static void Require(bool v,string why){if(!v)throw new InvalidOperationException(why);}
 sealed class Fixture : IDisposable
 {
  public object Backend {get;}
  public int Calls;
  readonly ManualResetEventSlim arrived=new();readonly Timer timer;
  public Fixture(string family="MSI.ClawCG3EM"){
   var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",F)!.Invoke(null,[Array.Empty<string>()]);
   Backend=Activator.CreateInstance(backendType,F,null,[options],null)!;
   var route=backendType.GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,["HandheldCompanion.Devices."+family]);
   Require(route is not null,"fixture route missing");
   Set(Backend,"routeOverrideForSelfTest",route);Set(Backend,"activeCurveDuties",new[]{100d,100d,100d,100d});backendType.GetProperty("IsOpen",F)!.SetValue(Backend,true);
   timer=new Timer(_=>{Interlocked.Increment(ref Calls);arrived.Set();},null,Timeout.Infinite,Timeout.Infinite);Set(Backend,"powerProfileWatchdog",timer);
  }
  public void Pending(long observed,long settled,long deferred){Set(Backend,"msiFirmwareModeObservedRevision",observed);Set(Backend,"msiFirmwareModeSettledRevision",settled);Set(Backend,"msiFirmwareModeDeferredRevision",deferred);}
  public bool Notify(long revision)=>(bool)backendType.GetMethod("NotifyMsiFirmwareModeChanged",F)!.Invoke(Backend,[revision])!;
  public void Armed(){Require(arrived.Wait(2000),"accepted retry failed to arm existing timer");}
  public void Dispose(){timer.DisposeAsync().AsTask().GetAwaiter().GetResult();Set(Backend,"powerProfileWatchdog",null);Set(Backend,"activeCurveDuties",null);Set(Backend,"activeFanProfile",null);backendType.GetProperty("IsOpen",F)!.SetValue(Backend,false);((IDisposable)Backend).Dispose();arrived.Dispose();}
 }
 public static int Main(string[] args){
  if(args.Length!=1)throw new ArgumentException("exact Host DLL required");var path=Path.GetFullPath(args[0]);AssemblyLoadContext.Default.Resolving+=(_,n)=>{var p=Path.Combine(Path.GetDirectoryName(path)!,n.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};host=AssemblyLoadContext.Default.LoadFromAssemblyPath(path);backendType=host.GetType("YeManFanHost.RealHcBackend",true)!;
  foreach(var family in new[]{"ClawA1M","ClawA2VM","ClawBZ2EM","ClawCG3EM"}){
   Check(family+" late older busy settlement still permits latest pending retry",()=>{using var f=new Fixture("MSI."+family);f.Pending(2,0,2);Set(f.Backend,"msiFirmwareModeDeferredRevision",1L);Require(f.Notify(2),"latest pending revision lost retry behind older deferred marker");f.Armed();Require(Get(f.Backend,"msiFirmwareModeDeferredRevision")==0,"deferred obligation not consumed");Require(!f.Notify(2),"same consumed marker rearmed repeatedly");});
   Check(family+" exact latest deferred retry remains supported",()=>{using var f=new Fixture("MSI."+family);f.Pending(2,0,2);Require(f.Notify(2),"current deferred edge rejected");f.Armed();Require(!f.Notify(2),"duplicate retry after consume");});
  }
  Check("pending notification without actual busy evidence cannot rearm",()=>{using var f=new Fixture();f.Pending(2,0,0);Require(!f.Notify(2)&&f.Calls==0,"ordinary pending heartbeat rearmed");});
  Check("completed latest revision cannot replay even if obsolete busy evidence remains",()=>{using var f=new Fixture();f.Pending(2,2,1);Require(!f.Notify(2)&&f.Calls==0,"settled revision replayed");});
  Check("obsolete caller cannot consume deferred evidence",()=>{using var f=new Fixture();f.Pending(3,0,2);Require(!f.Notify(2),"old revision rearmed");Require(Get(f.Backend,"msiFirmwareModeDeferredRevision")==2,"old caller consumed latest obligation");});
  Check("future marker is never consumed by older pending revision",()=>{using var f=new Fixture();f.Pending(2,0,3);Require(!f.Notify(2),"future evidence misbound");Require(Get(f.Backend,"msiFirmwareModeDeferredRevision")==3,"future marker consumed");});
  Check("two simultaneous keepalives consume a late busy marker at most once",()=>{using var f=new Fixture();f.Pending(2,0,1);using var start=new ManualResetEventSlim();var a=Task.Run(()=>{start.Wait();return f.Notify(2);});var b=Task.Run(()=>{start.Wait();return f.Notify(2);});start.Set();Require(Task.WaitAll([a,b],2000),"notification did not settle");Require((a.Result?1:0)+(b.Result?1:0)==1,"retry lost or duplicated");f.Armed();});
  Check("new revision still arms once through original ingress",()=>{using var f=new Fixture();f.Pending(0,0,0);Require(f.Notify(1),"new edge rejected");f.Armed();Require(Get(f.Backend,"msiFirmwareModeObservedRevision")==1,"observed revision not published");Require(!f.Notify(1),"ordinary duplicate became retry");});
  Check("no active curve cannot revive control",()=>{using var f=new Fixture();f.Pending(2,0,1);Set(f.Backend,"activeCurveDuties",null);Require(!f.Notify(2)&&f.Calls==0,"OFF rearmed");});
  Check("closed backend cannot revive control",()=>{using var f=new Fixture();f.Pending(2,0,1);backendType.GetProperty("IsOpen",F)!.SetValue(f.Backend,false);Require(!f.Notify(2)&&f.Calls==0,"closed session rearmed");});
  foreach(var family in new[]{"ROGAlly","GPD.GPDWin5","Lenovo.LegionGoTablet2","AYANEO2","OneXPlayerX1Mini"})Check(family+" does not consume MSI replay evidence",()=>{using var f=new Fixture(family);f.Pending(2,0,1);Require(!f.Notify(2)&&f.Calls==0,"non-MSI replayed");Require(Get(f.Backend,"msiFirmwareModeDeferredRevision")==1,"non-MSI consumed MSI state");});
  Check("zero and negative revision cannot arm",()=>{using var f=new Fixture();f.Pending(2,0,1);Require(!f.Notify(0)&&!f.Notify(-1)&&f.Calls==0,"invalid revision rearmed");});
  Check("current sleep epoch remains write-closed",()=>{using var f=new Fixture();f.Pending(2,0,1);Set(f.Backend,"suspendGateEpoch",1L);Require(!f.Notify(2)&&f.Calls==0,"stale power epoch rearmed");Require(Get(f.Backend,"msiFirmwareModeDeferredRevision")==1,"sleep boundary consumed pending obligation");});
  Check("close boundary keeps deferred notification read-only",()=>{using var f=new Fixture();f.Pending(2,0,1);Set(f.Backend,"closeWriteBlocked",1);Require(!f.Notify(2)&&f.Calls==0,"close boundary rearmed");});
  Check("unreturned operation keeps deferred notification read-only",()=>{using var f=new Fixture();f.Pending(2,0,1);Set(f.Backend,"operationTimedOut",true);Require(!f.Notify(2)&&f.Calls==0,"unknown HC owner rearmed");});
  Check("actual one-shot Timer can overlap an unfinished previous callback",()=>{
   using var first=new ManualResetEventSlim();using var second=new ManualResetEventSlim();using var release=new ManualResetEventSlim();int order=0;
   var timer=new Timer(_=>{if(Interlocked.Increment(ref order)==1){first.Set();release.Wait();}else second.Set();},null,Timeout.Infinite,Timeout.Infinite);
   try{timer.Change(0,Timeout.Infinite);Require(first.Wait(5000),"first timer callback did not enter");timer.Change(0,Timeout.Infinite);Require(second.Wait(5000),"runtime did not exhibit the required callback overlap");}
   finally{release.Set();timer.DisposeAsync().AsTask().GetAwaiter().GetResult();}
  });
  Check("no HC assembly or hardware boundary loaded",()=>Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC unexpectedly loaded"));
  Console.WriteLine(JsonSerializer.Serialize(new{ok=failures.Count==0,checks,failures,hostDllSha256=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant(),scope="actual-notify-and-one-shot-timer-with-injected-late-busy-settlement-no-hc-device-no-cpu-benchmark",hardwareCalls=0}));return failures.Count==0?0:1;
 }
}