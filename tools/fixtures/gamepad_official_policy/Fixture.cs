using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Text.Json;
using HIDMaestro;
using HIDMaestro.Internal;
using D=HIDMaestro.Internal.DriverDeploy;

namespace HIDMaestro
{
 public enum HMDriverInstallOutcome { AlreadyCurrent,Installed,Upgraded,KeptNewer }
}
namespace HIDMaestro.Internal
{
 internal static class AdminNamespace {
  internal static readonly string Prefix="Local\\YMCC-official-policy-"+Guid.NewGuid().ToString("N")+"-";
  internal static string Name(string leaf) { if(leaf!="Deploy")throw new Exception("unexpected lock leaf");return Prefix+leaf; }
 }
 internal static class TimeoutScale {internal static int Apply(int value)=>Math.Min(value,5000);}
 internal static class DeviceOrchestrator {internal static void LogDiag(string value){} }
 internal static class ProtectedStaging {internal static void PruneOldVersions()=>throw new Exception("unsubstituted terminal");}
 internal static class PnputilHelper {internal static void MarkInstalledConfirmed()=>throw new Exception("unsubstituted terminal");}
 internal static class MachineClaims {
  internal sealed record ControllerEntry(string Backend,string Token);
  internal sealed record OwnerRecord(bool Live,bool Self,List<ControllerEntry> Controllers);
 }
 internal sealed class OwnerCensus {
  internal enum Owner {Dead,DeadLegacy,Live,Unknown}
  internal sealed record DeviceGroup(string Key,Owner Owner);
  internal List<DeviceGroup> Devices=new();internal List<MachineClaims.OwnerRecord> Records=new();
  internal static OwnerCensus Take()=>throw new Exception("unsubstituted terminal");
 }
 // The wrapper body is generated verbatim from the actual pinned DLL source.
 internal static partial class DriverBuilder {
  internal static string[] PackageInfs={"hidmaestro.inf","hidmaestro_xusb.inf"};
  internal static bool WillTakeFastPath()=>throw new Exception("unsubstituted terminal");
  internal static string? InstalledManifestHash()=>throw new Exception("unsubstituted terminal");
  internal static bool DriverStoreHasBothPackages()=>throw new Exception("unsubstituted terminal");
  internal static void DeployLocked()=>throw new Exception("unsubstituted terminal");
  internal static void AddPackagesLocked(IReadOnlyCollection<string> infs)=>throw new Exception("unsubstituted terminal");
  internal static void RecordInstalledPayload()=>throw new Exception("unsubstituted terminal");
 }
}
internal static class OfficialPolicyFixture
{
 static int checks,cases;static List<string> failures=new();static string active="setup";
 static void Check(bool value,string name){++checks;if(!value)failures.Add(active+": "+name);}
 static readonly string[] infs={"hidmaestro.inf","hidmaestro_xusb.inf"};
 static D.DriverStamp Stamp(string version,string date="2026-10-19")=>new(DateTime.Parse(date,System.Globalization.CultureInfo.InvariantCulture),new Version(version));
 sealed class Fake : D.DeployMachine {
  internal readonly Dictionary<string,List<D.DriverStamp>> Store=new();
  internal OwnerCensus Owners=new();internal int Deploys,Adds,Records,Prunes,Confirmed,CensusReads;
  internal bool Fast;internal string Manifest="old-private-manifest";internal bool Pause;
  internal readonly ManualResetEventSlim Entered=new(false),Release=new(false);
  internal Fake(){foreach(var inf in infs)Store[inf]=new();}
  internal void Set(string version,string date="2026-10-19"){foreach(var inf in infs)Store[inf]=new(){Stamp(version,date)};}
  internal override List<D.DriverStamp> Stamps(string inf)=>new(Store[inf]);
  internal override D.DriverStamp Embedded(string inf)=>Stamp("2.0.0.2201");
  internal override OwnerCensus Census(){Interlocked.Increment(ref CensusReads);return Owners;}
  internal override bool FastPath()=>Fast;
  internal override void MarkConfirmed()=>++Confirmed;
  internal override string? ManifestHash()=>Manifest;
  internal override bool StoreHasBothPackages()=>infs.All(i=>Store[i].Count!=0);
  internal override void Deploy(){
   ++Deploys;Entered.Set();if(Pause&&!Release.Wait(5000))throw new Exception("private release event timeout");
   foreach(var inf in infs)Store[inf]=new(){Embedded(inf)};Manifest="current-private-manifest";
  }
  internal override void AddPackages(IReadOnlyCollection<string> names){++Adds;foreach(var inf in names)Store[inf].Add(Embedded(inf));}
  internal override void RecordPayload(){++Records;Manifest="current-private-manifest";}
  internal override void PruneStaging()=>++Prunes;
 }
 static Fake Use(Fake machine){D.Machine=machine;D.ForgetConfirmed();return machine;}
 static void Run(string name,Fake machine,HMDriverInstallOutcome expected,int deploys,int adds){
  ++cases;active=name;Use(machine);var outcome=D.Run();
  Check(outcome==expected,"official outcome");Check(machine.Deploys==deploys,"deploy terminal count");Check(machine.Adds==adds,"add terminal count");
  Check(machine.Prunes==(machine.Fast?0:1),"actual finally prune behavior");
 }
 static MachineClaims.OwnerRecord Record(string token,bool self=false,string backend="umdf")=>new(true,self,new(){new(backend,token)});
 static int Main(string[] args){
  string destination=args[0];System.IO.Directory.CreateDirectory(destination);
  try {
   Run("empty-store",new Fake(),HMDriverInstallOutcome.Installed,1,0);
   var old=new Fake();old.Set("1.12.0.2302","2026-10-13");Run("known112-no-holders",old,HMDriverInstallOutcome.Upgraded,1,0);
   old=new Fake();old.Set("1.4.7.12","2026-08-21");Run("known17-no-holders",old,HMDriverInstallOutcome.Upgraded,1,0);
   foreach(var owner in new[]{OwnerCensus.Owner.Live,OwnerCensus.Owner.Unknown}){
    var held=new Fake();held.Set("1.12.0.2302","2026-10-13");held.Owners.Devices.Add(new("held",owner));
    Run("present-owner-"+owner,held,HMDriverInstallOutcome.Upgraded,0,1);
    Check(held.Store[infs[0]].Any(x=>x.Version.Major==1),"old loaded-package candidate preserved beside added version");
   }
   var beforePnp=new Fake();beforePnp.Set("1.12.0.2302","2026-10-13");beforePnp.Owners.Records.Add(Record("reserved-before-PnP"));
   Run("live-record-before-any-PnP",beforePnp,HMDriverInstallOutcome.Upgraded,0,1);
   var self=new Fake();self.Set("1.12.0.2302","2026-10-13");self.Owners.Records.Add(Record("our-in-flight",true));
   Run("FullDeploy-null-protects-self-record",self,HMDriverInstallOutcome.Upgraded,0,1);
   foreach(var owner in new[]{OwnerCensus.Owner.Dead,OwnerCensus.Owner.DeadLegacy}){
    var dead=new Fake();dead.Set("1.12.0.2302","2026-10-13");dead.Owners.Devices.Add(new("dead",owner));
    Run("proven-dead-owner-"+owner,dead,HMDriverInstallOutcome.Upgraded,1,0);
   }
   var newer=new Fake();newer.Set("2.1.0.1","2026-10-20");Run("newer-two-packages-kept",newer,HMDriverInstallOutcome.KeptNewer,0,0);
   Check(newer.Store.Values.All(v=>v.Single().Version==new Version("2.1.0.1")),"newer version not downgraded");
   var mixed=new Fake();mixed.Set("2.1.0.1","2026-10-20");mixed.Store[infs[1]]=new(){Stamp("1.12.0.2302","2026-10-13")};
   Run("newer-main-older-half-adds-only-missing",mixed,HMDriverInstallOutcome.Installed,0,1);
   Check(mixed.Store[infs[0]].Single().Version==new Version("2.1.0.1"),"newer main untouched while companion added");
   var current=new Fake();current.Set("2.0.0.2201");Run("current-record-normalization",current,HMDriverInstallOutcome.AlreadyCurrent,0,0);
   var half=new Fake();half.Store[infs[0]].Add(Stamp("2.0.0.2201"));Run("current-main-missing-half",half,HMDriverInstallOutcome.Installed,0,1);
   var fast=new Fake{Fast=true};fast.Set("2.0.0.2201");Run("same-build-fast-path",fast,HMDriverInstallOutcome.AlreadyCurrent,0,0);Check(fast.Confirmed==1,"fast path confirms without deployment");
   ++cases;active="actual-public-FullDeploy-forwarder";var wrapper=Use(new Fake());Check(DriverBuilder.FullDeploy(false),"unchanged official bool wrapper returns true");Check(wrapper.Deploys==1,"wrapper uses same one actual policy terminal");
   ++cases;active="EnsureForCreate-reentrant-policy";var create=Use(new Fake());create.Set("1.12.0.2302","2026-10-13");create.Owners.Records.Add(Record("other-reserved"));
   Check(D.EnsureForCreate("ours"),"actual create invokes nested official policy");Check(create.Deploys==0&&create.Adds==1,"reserved live record prevents destructive deploy during create");
   // Actual Acquire and actual DeployMachine.Lock are used by both methods;
   // only AdminNamespace.Name redirects to a private unique Local mutex.
   ++cases;active="shared-private-mutex-run-blocks-create";var shared=Use(new Fake{Pause=true});Exception? firstError=null,secondError=null;
   var first=new Thread(()=>{try{D.Run();}catch(Exception e){firstError=e;}});
   first.Start();Check(shared.Entered.Wait(3000),"first deploy is inside official shared lock");
   using var secondStarted=new ManualResetEventSlim(false);bool createReturned=false;
   var second=new Thread(()=>{try{secondStarted.Set();D.EnsureForCreate("new");createReturned=true;}catch(Exception e){secondError=e;}});
   second.Start();Check(secondStarted.Wait(3000),"create thread started");
   Check(!second.Join(120),"create cannot pass actual SDK preparation gate while deploy is held");
   shared.Release.Set();Check(first.Join(4000)&&second.Join(4000),"both calls settle on private lock release");
   Check(firstError is null&&secondError is null&&createReturned,"same official lock settles without deadlock");
   ++cases;active="shared-private-mutex-create-blocks-run";shared=Use(new Fake{Pause=true});firstError=null;secondError=null;
   var creator=new Thread(()=>{try{D.EnsureForCreate("ours");}catch(Exception e){firstError=e;}});
   creator.Start();Check(shared.Entered.Wait(3000),"create is inside nested official Run under same gate");
   bool deployReturned=false;using var waiterStarted=new ManualResetEventSlim(false);
   var waiter=new Thread(()=>{try{waiterStarted.Set();D.Run();deployReturned=true;}catch(Exception e){secondError=e;}});
   waiter.Start();Check(waiterStarted.Wait(3000),"second deploy thread started");Check(!waiter.Join(120),"deploy cannot cross in-flight create preparation gate");
   shared.Release.Set();Check(creator.Join(4000)&&waiter.Join(4000),"reentrant create and waiting deploy settle");Check(firstError is null&&secondError is null&&deployReturned,"SDK mutex is shared and reentrant");
   var report=new{status=failures.Count==0?"PASS":"FAIL",cases,checks,failures,
    actualOfficialPolicySourceCompiled=true,privateRealNamedMutex=true,actualSdkAssemblyMethodsInvoked=false,
    realDeviceDriverRegistryCertificateOperations=false,
    scope="shared SDK create-preparation/deploy gate plus live owner records; does not claim mutex covers entire PnP"};
   System.IO.File.WriteAllText(System.IO.Path.Combine(destination,"RESULT.json"),JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
   Console.WriteLine(JsonSerializer.Serialize(report));return failures.Count==0?0:1;
  }catch(Exception e){System.IO.File.WriteAllText(System.IO.Path.Combine(destination,"RESULT.json"),JsonSerializer.Serialize(new{status="ERROR",error=e.ToString(),cases,checks,failures}));Console.Error.WriteLine(e);return 2;}
 }
}
