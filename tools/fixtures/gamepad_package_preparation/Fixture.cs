using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using P = HidMaestroPackagePreparation;

internal static class GamepadPackagePreparationFixture
{
    static int Checks, Cases;
    static readonly List<string> Failures = new();
    static string CurrentCase = "setup";
    static void Check(bool value, string name) { ++Checks; if (!value) Failures.Add(CurrentCase + ": " + name); }
    static string Hash(byte[] value) => Convert.ToHexString(SHA256.HashData(value)).ToLowerInvariant();
    static string Xml(IEnumerable<P.Driver> rows) => "<Drivers>" + string.Join("",rows.Select(r =>
        $"<Driver DriverName='{r.Published}'><OriginalName>{r.Original}</OriginalName><ProviderName>{r.Provider}</ProviderName><DriverVersion>{r.RawVersion}</DriverVersion></Driver>")) + "</Drivers>";
    static Dictionary<string,string> FileHashes(string root) => Directory.GetFiles(root,"*",SearchOption.AllDirectories)
        .ToDictionary(p=>Path.GetRelativePath(root,p),p=>Hash(File.ReadAllBytes(p)));
    sealed class FakePreparationRuntime : P.IPreparationRuntime
    {
        internal Queue<P.Observation> Observations = new();
        internal P.OfficialOutcome Outcome;
        internal int Calls, Reads;
        internal bool ThrowSdk, ThrowPost;
        internal List<object> Trail = new();
        public P.Observation Observe(string phase) {
            ++Reads;
            if(ThrowPost && phase=="after-official")throw new InvalidDataException("private post-enumeration failed");
            return Observations.Dequeue();
        }
        public P.OfficialCall PrepareOfficial(P.SdkContract sdk) {
            ++Calls;
            if(ThrowSdk)throw new InvalidOperationException("private fake SDK lock timeout/deferred");
            return new(Outcome,"private-fake-DriverDeploy.Run(null)","metadata-bound-FullDeploy-single-terminal");
        }
        public void Record(string phase, object evidence) => Trail.Add(new {phase,evidence});
    }
    static int Main(string[] args)
    {
        if(args.Length is not (2 or 3)) return 2;
        string sdkPath=Path.GetFullPath(args[0]), evidence=Path.GetFullPath(args[1]);
        Directory.CreateDirectory(evidence);
        string versionRoot=Path.Combine(evidence,"private-version"),rawStage=Path.Combine(versionRoot,"driver"),package=Path.Combine(versionRoot,"package");
        Directory.CreateDirectory(rawStage);
        string resultPath=Path.Combine(evidence,"RESULT.json");
        try {
            Check(new FileInfo(sdkPath).Length==P.CoreBytes && Hash(File.ReadAllBytes(sdkPath))==P.CoreSha.ToLowerInvariant(),"exact pinned 2.0 binary");
            if(Failures.Count!=0)throw new InvalidDataException("SDK identity mismatch before loading metadata");
            var sdk=AssemblyLoadContext.Default.LoadFromAssemblyPath(sdkPath);
            var contract=P.ReadSdkMetadata(sdk,rawStage);
            Check(contract.Manifest==P.EmbeddedManifest && contract.Mvid==P.CoreMvid,"exact metadata contract and manifest");
            Check(contract.PackageStage==package,"2.0 raw and package sibling directories derived without SDK getter");
            Check(P.DefaultRawStage().EndsWith(Path.Combine("2.0.0-894aad9e4aa7365e","driver"),StringComparison.OrdinalIgnoreCase),"actual decompiled 2.0 protected staging version tag");
            var embedded=P.Infs.ToDictionary(name=>name,name=> {
                using var input=sdk.GetManifestResourceStream(P.NativePrefix+name)!;
                using var memory=new MemoryStream();input.CopyTo(memory);return memory.ToArray();
            });
            Check(contract.Infs.All(i=>i.RawVersion=="10/19/2026,2.0.0.2201"),"current two-INF driver version from approved actual 2.0 resources");
            var current=new List<P.Driver> {new("oem118.inf","hidmaestro.inf","HIDMaestro","10/19/2026 2.0.0.2201"),
                new("oem119.inf","hidmaestro_xusb.inf","HIDMaestro","10/19/2026 2.0.0.2201")};
            var old=current.Select(d=>d with {RawVersion="08/21/2026 1.4.7.12"}).ToList();
            var previous=current.Select(d=>d with {RawVersion="10/13/2026 1.12.0.2302"}).ToList();
            ++Cases; CurrentCase="observed-pnputil-schema";
            string Inventory(string osVersion, string command="/enum-drivers /format xml") =>
                $"<PnpUtil Version='{osVersion}' Command='{command}'>" + Xml(current)["<Drivers>".Length..^"</Drivers>".Length] + "</PnpUtil>";
            foreach(string osVersion in new[]{"10.0.26200","99.0.99999"}) {
                try { Check(P.ParseDrivers(Inventory(osVersion)).SequenceEqual(current),"PnpUtil schema retains exact driver rows without OS version ceiling"); }
                catch(Exception ex) { Check(false,"PnpUtil schema rejected: "+ex.GetType().Name); }
            }
            List<P.Driver>? capturedInventoryRows=null;
            if(args.Length==3) {
                try { capturedInventoryRows=P.ParseDrivers(File.ReadAllText(args[2])); Check(capturedInventoryRows.Count>0,"captured local PnpUtil sample accepted by actual parser"); }
                catch(Exception ex) { Check(false,"captured local PnpUtil sample rejected: "+ex.GetType().Name); }
            }
            foreach(string unknown in new[]{"<FutureInventory />",Inventory(""),Inventory("10.0.26200","/enum-devices /connected /format xml"),"<PnpUtil Version='10.0.26200' />"}) {
                bool advisory=false;
                try { P.ParseDrivers(unknown); }
                catch(P.InventorySchemaUnrecognizedException) { advisory=true; }
                Check(advisory,"unknown inventory or wrong command never proves missing packages");
            }
            Check(P.InventoryUnrecognizedExit==18,"unrecognized inventory has its own non-ready result");
            var resultMethod=typeof(Program).GetMethod("SetupResult",BindingFlags.Static|BindingFlags.NonPublic)!;
            (string,int) Result(string hh,int code)=>(ValueTuple<string,int>)resultMethod.Invoke(null,new object[]{hh,code,true})!;
            Check(Result("READY",18)==("DRIVER_INVENTORY_UNRECOGNIZED",18),"unknown inventory stays advisory, never Ready");
            Check(Result("REBOOT_REQUIRED",18)==("REBOOT_REQUIRED",3010),"real reboot requirement is retained");
            Check(Result("PRESENT_NOT_READY_NO_REINSTALL",18)==("DEPENDENCY_NOT_READY",11),"real dependency fault is retained");
            ++Cases; CurrentCase="maintenance-deferred-result-contract";
            Check(P.MaintenanceDeferredExit==19,"deferred maintenance is distinct from Ready and dependency failure");
            var exitMethod=typeof(Program).GetMethod("ExitFor",BindingFlags.Static|BindingFlags.NonPublic)!;
            Check((int)exitMethod.Invoke(null,new object[]{"DRIVER_MAINTENANCE_DEFERRED"})! == 19,"actual inner result exit is advisory 19, not success");
            Check(Result("READY",19)==("DRIVER_MAINTENANCE_DEFERRED",19),"installed packages and skipped maintenance survive outer aggregation");
            Check(Result("REBOOT_REQUIRED",19)==("REBOOT_REQUIRED",3010),"deferred maintenance never masks reboot");
            foreach(var hh in new[]{"MISSING","PRESENT_NOT_READY_NO_REINSTALL","ADMIN_REQUIRED","CLOSE_CONTROLLER_APPS_FIRST"})
                Check(Result(hh,19)==("DEPENDENCY_NOT_READY",11),"deferred maintenance never masks a HidHide fault");
            Check(((ValueTuple<string,int>)resultMethod.Invoke(null,new object[]{"READY",19,false})!).Item1!="DRIVER_MAINTENANCE_DEFERRED","check-only cannot invent a skipped maintenance result");
            ++Cases; CurrentCase="raw-inventory-before-parse";
            string capture=Path.Combine(evidence,"startup-inventory");
            string raw="<FutureInventory note='真实数据' />";
            var captured=P.CaptureInventory(capture,raw,0,false);
            Check(captured.WriteStatus=="retained"&&!captured.Truncated&&File.ReadAllText(captured.Path)==raw,"raw unknown output retained before schema rejection");
            Check(captured.Scope.Contains("not-empty-store"),"raw inventory is never device readiness proof");
            foreach(string suffix in new[]{".drivers.xml",".before-drivers.xml",".after-drivers.xml"}) {
                var item=P.CaptureInventory(capture,raw,0,false,suffix);
                Check(item.Path==capture+suffix&&File.Exists(item.Path),"initial and before/after export paths preserved");
            }
            var large=P.CaptureInventory(capture,new string('x',P.MaximumInventoryCharacters+1),1,true);
            Check(large.Truncated&&large.RetainedCharacters==P.MaximumInventoryCharacters&&large.ExitCode==1&&large.TimedOut,"raw capture byte work bounded and original failure retained");
            string blocked=Path.Combine(evidence,"capture-blocked");Directory.CreateDirectory(blocked+".drivers.xml");
            var failedCapture=P.CaptureInventory(blocked,Xml(current),0,false);
            Check(failedCapture.WriteStatus=="unavailable"&&failedCapture.Error!=null&&P.ParseDrivers(Xml(current)).Count==2,"evidence IO failure does not change valid inventory");
            var idle=new P.MaintenanceFacts(true,true,true,false,false);
            void Seed(bool rawCats=false,bool signedPackage=true,bool signedCats=true) {
                Directory.CreateDirectory(rawStage);
                foreach(var name in P.Infs) {
                    File.WriteAllBytes(Path.Combine(rawStage,name),embedded[name]);
                    var rawCat=Path.Combine(rawStage,Path.ChangeExtension(name,".cat"));
                    if(rawCats)File.WriteAllText(rawCat,"PRIVATE-CATALOG-PRESENCE-NOT-SIGNATURE");
                    else if(File.Exists(rawCat))File.Delete(rawCat);
                    var inf=Path.Combine(package,name);var cat=Path.Combine(package,Path.ChangeExtension(name,".cat"));
                    if(signedPackage) {Directory.CreateDirectory(package);File.WriteAllBytes(inf,embedded[name]);}
                    else if(File.Exists(inf))File.Delete(inf);
                    if(signedPackage && signedCats)File.WriteAllText(cat,"PRIVATE-CATALOG-PRESENCE-NOT-SIGNATURE");
                    else if(File.Exists(cat))File.Delete(cat);
                }
            }
            P.Review Run(string name,IReadOnlyList<P.Driver> drivers,string expected,bool prepare=true,P.MaintenanceFacts? facts=null,
                    P.SdkContract? selected=null,string? expectedRaw=null,string? expectedPackage=null) {
                ++Cases;CurrentCase=name;var before=FileHashes(versionRoot);
                var review=Program.InspectCurrentPackage(prepare,selected??contract,P.ParseDrivers(Xml(drivers)),facts??idle);
                Check(review.Status==expected,$"store status expected={expected} actual={review.Status}");
                var observed=facts??idle;
                var productionFacts=new Program.Facts(true,observed.Admin,observed.BlockingClients,true,
                    drivers.Count(P.Owned),drivers.Any(P.Owned),observed.OwnDevices,
                    P.HistoricalPairPresent(drivers),P.HistoricalPairPresent(drivers));
                if(!prepare && review.Inventory.Packages.Count!=0)
                    Check(Program.DecideForPreparation(productionFacts,review,false)==expected,"actual check dispatch stays read-only");
                Check(review.StoreReady==(expected is "DRIVER_STORE_READY" or "DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED"),"store version readiness remains distinct from binding");
                Check(review.SdkCalls.Count==0 && !review.InstallAttempted,"no SDK methods invoked");
                Check(!review.Maintenance.AutomaticWritesEnabled && !review.Maintenance.LifecycleExclusive && !review.Maintenance.OperationDeadlineProven,"no invented maintenance ownership or deadline");
                Check(!review.Staging.WritesIssued && !review.SignedPackage.WritesIssued && !review.HardwareVerified,"no staging write or device claim");
                Check(review.Scope.Contains("not-catalog-signature-validity-or-device-binding-proof"),"presence is not trust or binding proof");
                Check(review.Staging.ArtifactRole=="raw-driver" && !review.Staging.CatalogRequiredHere,"raw 2.0 driver does not require generated CAT");
                Check(review.SignedPackage.ArtifactRole=="signed-package" && review.SignedPackage.CatalogRequiredHere,"CAT observation belongs to signed package");
                Check(review.Staging.SignatureVerification=="not-performed" && review.SignedPackage.SignatureVerification=="not-performed","placeholder CATs explicitly unverified");
                string expectedMaintenance = !prepare ? "check-only-no-SDK-methods" :
                    review.Inventory.Packages.Count!=0 && !review.Inventory.PreparationKnown ? "unverified-package-metadata-no-automatic-maintenance" :
                    "official-2.0-deploy-policy-eligible-after-guards";
                Check(review.Maintenance.Status==expectedMaintenance,"maintenance status follows inventory facts expected="+expectedMaintenance+" actual="+review.Maintenance.Status);
                if(expectedRaw is not null)Check(review.Staging.Status==expectedRaw,"raw evidence expected="+expectedRaw+" actual="+review.Staging.Status);
                if(expectedPackage is not null)Check(review.SignedPackage.Status==expectedPackage,"signed package expected="+expectedPackage+" actual="+review.SignedPackage.Status);
                Check(FileHashes(versionRoot).OrderBy(p=>p.Key).SequenceEqual(before.OrderBy(p=>p.Key)),"actual consumer writes no staging file");
                File.WriteAllText(Path.Combine(evidence,$"case-{Cases:D3}.json"),JsonSerializer.Serialize(new {name,prepare,review},new JsonSerializerOptions{WriteIndented=true}));
                return review;
            }
            Seed(signedPackage:false);
            var fault=Run("field-legacy17-pair-with-sdk200",old,"DRIVER_PACKAGE_VERSION_MISMATCH",expectedRaw:"CURRENT_RAW_DRIVER_INF_PRESENT",expectedPackage:"SDK_STAGING_INF_UNPROVEN");
            Check(fault.Inventory.Packages.All(p=>p.Classification=="known-legacy"),"exact original field version retained");
            Check(P.HistoricalPairPresent(old),"weak SDK presence could still be true");
            Run("previous112-pair-with-sdk200",previous,"DRIVER_PACKAGE_VERSION_MISMATCH");
            Run("known-field-check-only",old,"DRIVER_PACKAGE_VERSION_MISMATCH",false);
            Seed();
            Run("current-pair-no-raw-CAT-is-healthy200",current,"DRIVER_STORE_READY",expectedRaw:"CURRENT_RAW_DRIVER_INF_PRESENT",expectedPackage:"SIGNED_PACKAGE_FILES_PRESENT_UNVERIFIED");
            Run("current-check-no-raw-CAT",current,"DRIVER_STORE_READY",false);
            Run("healthy-existing-target-not-reconfigured",current,"DRIVER_STORE_READY",true,idle with {OwnDevices=true,BlockingClients=true});
            Run("old17-with-new-package-files-not-current",old,"DRIVER_PACKAGE_VERSION_MISMATCH");
            Run("old112-with-new-package-files-not-current",previous,"DRIVER_PACKAGE_VERSION_MISMATCH");
            Run("one-new-one112-not-complete",[current[0],previous[1]],"DRIVER_PACKAGE_VERSION_MISMATCH");
            Run("current-and-known-old-packages-coexist-no-deletion",current.Concat(previous.Select((p,i)=>p with {Published=$"oem{220+i}.inf"})).Concat(old.Select((p,i)=>p with {Published=$"oem{320+i}.inf"})).ToList(),"DRIVER_STORE_READY");
            Run("one-current-INF-not-complete",[current[0]],"DRIVER_PACKAGE_INCOMPLETE");
            var unrelated=new P.Driver("oem5.inf","unrelated.inf","Other","01/01/2026 1.0.0.0");
            Run("unrelated-store-is-not-owned",[unrelated],"MISSING_DRIVER");
            Run("unrelated-package-does-not-change-ready",current.Append(unrelated).ToList(),"DRIVER_STORE_READY");
            Run("foreign-provider-unproven",current.Select(p=>p with {Provider="OtherVendor"}).ToList(),"DRIVER_PACKAGE_IDENTITY_UNPROVEN");
            Run("forged-published-path",[current[0] with {Published="../oem1.inf"},current[1]],"DRIVER_PACKAGE_IDENTITY_UNPROVEN");
            Run("duplicate-published-name",[current[0],current[1] with {Published=current[0].Published}],"DRIVER_PACKAGE_IDENTITY_UNPROVEN");
            Run("newer-store-preserved-protocol-unverified",current.Select(p=>p with {RawVersion="10/20/2026 2.1.0.1"}).ToList(),"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED");
            Run("version-absent-unproven",current.Select(p=>p with {RawVersion=""}).ToList(),"DRIVER_PACKAGE_VERSION_UNPROVEN");
            Run("right-number-wrong-date",current.Select(p=>p with {RawVersion="08/21/2026 2.0.0.2201"}).ToList(),"DRIVER_PACKAGE_VERSION_UNPROVEN");
            Run("right-date-wrong-number",current.Select(p=>p with {RawVersion="10/19/2026 1.4.7.12"}).ToList(),"DRIVER_PACKAGE_VERSION_UNPROVEN");
            Seed(signedPackage:false);
            Run("AlreadyCurrent-may-have-no-signed-package-stage",current,"DRIVER_STORE_READY",expectedPackage:"SDK_STAGING_INF_UNPROVEN");
            Seed(signedCats:false);
            Run("missing-package-CAT-is-independent-unproven-evidence",current,"DRIVER_STORE_READY",expectedPackage:"SDK_STAGING_CATALOG_MISSING");
            Run("check-package-CAT-missing-never-signs",current,"DRIVER_STORE_READY",false,expectedPackage:"SDK_STAGING_CATALOG_MISSING");
            Seed();File.WriteAllBytes(Path.Combine(package,"hidmaestro.cat"),[]);
            Run("empty-package-CAT-not-promoted-to-valid",current,"DRIVER_STORE_READY",expectedPackage:"SDK_STAGING_CATALOG_EMPTY");
            Seed();
            using(var held=new FileStream(Path.Combine(package,"hidmaestro.cat"),FileMode.Open,FileAccess.Read,FileShare.None)) {
                ++Cases;CurrentCase="locked-signed-CAT";
                var review=Program.InspectCurrentPackage(true,contract,current,idle);
                Check(review.Status=="DRIVER_STORE_READY" && review.SignedPackage.Status=="SDK_STAGING_CATALOG_UNPROVEN","CAT read failure never changes package-store version facts");
                Check(review.SdkCalls.Count==0 && !review.InstallAttempted,"locked CAT never repaired");
            }
            Seed();File.Delete(Path.Combine(rawStage,"hidmaestro.inf"));
            Run("raw-INF-missing-is-only-evidence",current,"DRIVER_STORE_READY",expectedRaw:"SDK_STAGING_INF_UNPROVEN");
            Seed();File.AppendAllText(Path.Combine(rawStage,"hidmaestro.inf"),"\n;changed\n");
            Run("changed-raw-INF-reported-with-hash",current,"DRIVER_STORE_READY",expectedRaw:"SDK_STAGING_INF_IDENTITY_MISMATCH");
            Seed();File.WriteAllBytes(Path.Combine(rawStage,"hidmaestro.inf"),new byte[P.MaximumInfBytes+1]);
            Run("raw-INF-read-budget",current,"DRIVER_STORE_READY",expectedRaw:"SDK_STAGING_INF_UNPROVEN");
            Seed();File.WriteAllText(Path.Combine(package,"hidmaestro.inf"),"[Version]\nDriverVer=10/19/2026,2.0.0.2201\nCatalogFile=../escape.cat\n");
            Run("signed-INF-path-escape-unproven",current,"DRIVER_STORE_READY",expectedPackage:"SDK_STAGING_INF_UNPROVEN");
            Seed();
            var absent=contract with {Stage=Path.Combine(evidence,"never-created-driver"),PackageStage=Path.Combine(evidence,"never-created-package")};
            Run("check-never-extracts-missing-stage",current,"DRIVER_STORE_READY",false,selected:absent,expectedRaw:"SDK_STAGING_INF_UNPROVEN",expectedPackage:"SDK_STAGING_INF_UNPROVEN");
            Check(!Directory.Exists(absent.Stage)&&!Directory.Exists(absent.PackageStage),"no staging getter or creation by check");
            ++Cases;CurrentCase="original-clean-first-install-dispatch";
            var emptyReview=Program.InspectCurrentPackage(true,contract,[unrelated],idle);
            var freshFacts=new Program.Facts(true,true,false,true,0,false,false,false,false);
            Check(Program.DecideForPreparation(freshFacts,emptyReview,true)=="INSTALL_FRESH","original clean first-install policy retained");
            Check(Program.DecideForPreparation(freshFacts,emptyReview,false)=="MISSING_DRIVER","check never enters fresh-install");
            foreach(var facts in new[]{freshFacts with {Admin=false},freshFacts with {ClientsRunning=true},freshFacts with {OwnDevices=true},
                    freshFacts with {RepositoryRemnants=true},freshFacts with {SdkReady=true},freshFacts with {Hash=false},freshFacts with {EnumValid=false}})
                Check(Program.DecideForPreparation(facts,emptyReview,true)!="INSTALL_FRESH","original fresh install safeguards retained");
            for(int mask=0;mask<32;++mask) {
                var facts=new P.MaintenanceFacts((mask&1)!=0,(mask&2)!=0,(mask&4)!=0,(mask&8)!=0,(mask&16)!=0);
                Run("read-only-review-observed-flags-"+mask,previous,"DRIVER_PACKAGE_VERSION_MISMATCH",true,facts);
            }
            foreach(var bad in new[]{"garbage","<OtherSchema />","<Drivers />","<Drivers><Other /></Drivers>","<Drivers><Driver /></Drivers>",
                "<!DOCTYPE Drivers [<!ENTITY x SYSTEM 'file:///never-read'>]><Drivers><Driver>&x;</Driver></Drivers>",
                "<Drivers><Driver><DriverVersion>x</DriverVersion><DriverVersion>y</DriverVersion></Driver></Drivers>",
                new string('x',P.MaximumInventoryCharacters+1)}) {
                ++Cases;CurrentCase="invalid-inventory";bool threw=false;try{P.ParseDrivers(bad);}catch{threw=true;}Check(threw,"invalid inventory never proves empty store");
            }
            ++Cases;CurrentCase="SDK-contract-revoked";bool rejected=false;
            try{Program.InspectCurrentPackage(true,contract with {Mvid=Guid.Empty.ToString()},current,idle);}catch{rejected=true;}
            Check(rejected,"exact SDK identity remains mandatory");
            Seed();
            P.Observation Snapshot(IReadOnlyList<P.Driver> rows,P.MaintenanceFacts? guard=null,bool? remnants=null) =>
                new(P.ParseDrivers(Xml(rows)),guard??idle,remnants??rows.Any(P.Owned));
            P.PreparationResult PrepareCase(string name,P.Observation initial,P.Observation recheck,P.Observation post,
                    P.OfficialOutcome outcome,string expected,int calls=1,bool prepare=true,bool sdkThrows=false,bool postThrows=false,int? reads=null) {
                ++Cases;CurrentCase="prepare/"+name;
                var runtime=new FakePreparationRuntime {Outcome=outcome,ThrowSdk=sdkThrows,ThrowPost=postThrows};
                runtime.Observations.Enqueue(recheck);runtime.Observations.Enqueue(post);
                var beforeFiles=FileHashes(versionRoot);
                var result=P.Execute(prepare,contract,initial,runtime);
                Check(result.Status==expected,"actual startup consumer expected="+expected+" actual="+result.Status);
                Check(runtime.Calls==calls,"exact number of fake official SDK calls");
                if(reads.HasValue)Check(runtime.Reads==reads.Value,"exact number of runtime re-observations");
                Check(result.SdkCallAttempted==(calls==1),"attempt receipt not inferred from store presence");
                Check(!result.BindingEvidence.Contains("binding-complete"),"package preparation never manufactures device binding proof");
                if(calls==1 && !sdkThrows && Enum.IsDefined(outcome))Check(result.Official?.Outcome==outcome,"real enum-shaped official result preserved");
                if(!prepare)Check(runtime.Reads==0 && runtime.Calls==0,"check makes no SDK method or extra runtime calls");
                Check(FileHashes(versionRoot).OrderBy(x=>x.Key).SequenceEqual(beforeFiles.OrderBy(x=>x.Key)),"only private fake terminal; staging left unchanged");
                File.WriteAllText(Path.Combine(evidence,$"case-{Cases:D3}-prepare.json"),JsonSerializer.Serialize(new {name,result,runtime.Calls,runtime.Reads,runtime.Trail},new JsonSerializerOptions{WriteIndented=true}));
                return result;
            }
            // A proved supply pair survives extra unknown versions; metadata uncertainty
            // still prevents automatic maintenance and never proves a live binding.
            void SupplyCase(string name,IReadOnlyList<P.Driver> drivers,string expected,bool ready,
                    string[] expectedClasses,bool currentComplete=false) {
                foreach(bool prepare in new[]{false,true}) {
                    string mode=prepare?"-prepare":"-check";
                    var review=Run(name+mode,drivers,expected,prepare);
                    Check(review.Inventory.Packages.Select(p=>p.Driver).SequenceEqual(drivers),"all input rows and raw versions retained in order");
                    Check(review.Inventory.Packages.Select(p=>p.Classification).SequenceEqual(expectedClasses),"every row classification retained");
                    Check(review.StoreReady==ready && review.Inventory.AtLeastCurrentComplete==ready,"only a complete proved pair supplies the store");
                    Check(review.Inventory.CurrentComplete==currentComplete,"exact-current pair remains distinct from newer or mixed supply");
                    Check(!review.Inventory.PreparationKnown,"uncertain or invalid extra metadata never authorizes automatic maintenance");
                    if(ready) {
                        Check(review.Inventory.AdditionalVersionsUnproven,"AdditionalVersionsUnproven explicitly records uncertain extra versions");
                        Check(review.Inventory.NewerPresent==(expected=="DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED"),"newer supply keeps its protocol-unverified status");
                    }
                    var facts=new Program.Facts(true,idle.Admin,idle.BlockingClients,true,
                        drivers.Count(P.Owned),drivers.Any(P.Owned),idle.OwnDevices,
                        P.HistoricalPairPresent(drivers),P.HistoricalPairPresent(drivers));
                    Check(Program.DecideForPreparation(facts,review,prepare)==expected,"actual check and prepare dispatch both preserve the supply decision without maintenance");
                    var observed=Snapshot(drivers);
                    var result=PrepareCase(name+mode,observed,observed,observed,P.OfficialOutcome.Upgraded,expected,
                        calls:0,prepare:prepare,reads:0);
                    Check(result.Decision==expected && result.Before.Status==expected,"Execute retains the actual inspection and dispatch result");
                    Check(result.Before.StoreReady==ready && result.Before.Inventory.AtLeastCurrentComplete==ready,"Execute preserves supply readiness without upgrading evidence");
                    Check(!result.Before.Inventory.PreparationKnown,"Execute preserves the prohibition on automatic maintenance");
                    Check(result.Before.Inventory.Packages.SequenceEqual(review.Inventory.Packages),"Execute retains every original row and classification");
                    if(ready)Check(result.Before.Inventory.AdditionalVersionsUnproven,"Execute preserves AdditionalVersionsUnproven");
                    Check(!result.SdkCallAttempted && result.Official is null && result.After is null,"no official call or fabricated post-install observation");
                    Check(!review.HardwareVerified && !result.Before.HardwareVerified && !result.Before.InstallAttempted,
                        "supply observations do not claim hardware verification or installation");
                    Check(result.BindingEvidence=="not-observed","no device binding success inferred from store supply");
                }
            }
            var unknownOld=current.Select((p,i)=>p with {Published=$"oem{420+i}.inf",RawVersion="10/12/2026 1.10.0.1"}).ToArray();
            var newerSupply=current.Select(p=>p with {RawVersion="10/20/2026 2.1.0.1"}).ToArray();
            var currentAndUnknown=current.Concat(unknownOld).ToArray();
            var newerAndUnknown=newerSupply.Concat(unknownOld).ToArray();
            var mixedAndUnknown=new[]{current[0],newerSupply[1]}.Concat(unknownOld).ToArray();
            SupplyCase("supply-current-pair-plus-unknown-old110",currentAndUnknown,"DRIVER_STORE_READY",true,
                ["current","current","unknown-version","unknown-version"],currentComplete:true);
            SupplyCase("supply-newer-pair-plus-unknown-old110",newerAndUnknown,"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED",true,
                ["newer-preserved-protocol-unverified","newer-preserved-protocol-unverified","unknown-version","unknown-version"]);
            SupplyCase("supply-mixed-current-newer-plus-unknown-old110",mixedAndUnknown,"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED",true,
                ["current","newer-preserved-protocol-unverified","unknown-version","unknown-version"]);
            SupplyCase("supply-incomplete-pair-plus-unknown-still-rejected",new[]{current[0]}.Concat(unknownOld).ToArray(),
                "DRIVER_PACKAGE_VERSION_UNPROVEN",false,["current","unknown-version","unknown-version"]);
            SupplyCase("supply-unknown-only-still-rejected",unknownOld,"DRIVER_PACKAGE_VERSION_UNPROVEN",false,
                ["unknown-version","unknown-version"]);
            SupplyCase("supply-current-pair-plus-foreign-provider-still-rejected",
                [current[0],current[1],unknownOld[0] with {Provider="OtherVendor"}],"DRIVER_PACKAGE_IDENTITY_UNPROVEN",false,
                ["current","current","identity-unproven"]);
            SupplyCase("supply-current-pair-plus-duplicate-published-still-rejected",
                [current[0],current[1],unknownOld[0] with {Published=current[0].Published}],"DRIVER_PACKAGE_IDENTITY_UNPROVEN",false,
                ["current","current","unknown-version"]);
            SupplyCase("supply-current-pair-plus-case-folded-duplicate-published-still-rejected",
                [current[0],current[1],unknownOld[0] with {Published=current[0].Published.ToUpperInvariant()}],"DRIVER_PACKAGE_IDENTITY_UNPROVEN",false,
                ["current","current","unknown-version"]);
            SupplyCase("supply-current-pair-plus-invalid-published-still-rejected",
                [current[0],current[1],unknownOld[0] with {Published="../oem420.inf"}],"DRIVER_PACKAGE_IDENTITY_UNPROVEN",false,
                ["current","current","identity-unproven"]);
            var malformedExtra=unknownOld[1] with {RawVersion="not-a-driver-version"};
            SupplyCase("supply-current-pair-plus-malformed-extra",[current[0],current[1],malformedExtra],"DRIVER_STORE_READY",true,
                ["current","current","unproven"],currentComplete:true);
            SupplyCase("supply-newer-pair-plus-malformed-extra",[newerSupply[0],newerSupply[1],malformedExtra],
                "DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED",true,
                ["newer-preserved-protocol-unverified","newer-preserved-protocol-unverified","unproven"]);
            SupplyCase("supply-current-pair-plus-empty-version-extra",[current[0],current[1],malformedExtra with {RawVersion=""}],
                "DRIVER_STORE_READY",true,["current","current","unproven"],currentComplete:true);
            SupplyCase("supply-mixed-pair-plus-unknown-and-malformed-extras",
                [current[0],newerSupply[1],unknownOld[0],malformedExtra],"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED",true,
                ["current","newer-preserved-protocol-unverified","unknown-version","unproven"]);
            SupplyCase("supply-incomplete-pair-plus-malformed-still-rejected",[current[0],malformedExtra],
                "DRIVER_PACKAGE_VERSION_UNPROVEN",false,["current","unproven"]);
            SupplyCase("supply-malformed-only-still-rejected",[malformedExtra],"DRIVER_PACKAGE_VERSION_UNPROVEN",false,["unproven"]);
            var currentState=Snapshot(current);var oldState=Snapshot(previous);var originalOldState=Snapshot(old);
            var freshState=Snapshot([unrelated],remnants:false);
            PrepareCase("fresh-keeps-original-install",freshState,freshState,currentState,P.OfficialOutcome.Installed,"DRIVER_PREPARED_DEVICE_UNVERIFIED");
            PrepareCase("known112-actually-enters-official-upgrade",oldState,oldState,currentState,P.OfficialOutcome.Upgraded,"DRIVER_PREPARED_DEVICE_UNVERIFIED");
            PrepareCase("known17-actually-enters-official-upgrade",originalOldState,originalOldState,currentState,P.OfficialOutcome.Upgraded,"DRIVER_PREPARED_DEVICE_UNVERIFIED");
            PrepareCase("current-official-AlreadyCurrent",currentState,currentState,currentState,P.OfficialOutcome.AlreadyCurrent,"DRIVER_STORE_READY");
            var newer=Snapshot(current.Select(p=>p with {RawVersion="10/20/2026 2.1.0.1"}).ToList());
            PrepareCase("official-KeptNewer-no-downgrade",newer,newer,newer,P.OfficialOutcome.KeptNewer,"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED");
            var liveOld=Snapshot(previous,idle with {OwnDevices=true});var liveCurrent=Snapshot(current,idle with {OwnDevices=true});
            var live=PrepareCase("live-owner-added-store-is-not-rebound",liveOld,liveOld,liveCurrent,P.OfficialOutcome.Upgraded,"DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN");
            Check(live.BindingEvidence.Contains("may-retain-old-driver"),"added-versus-replaced and binding remain honest");
            PrepareCase("create-appeared-after-preflight",oldState,oldState,liveCurrent,P.OfficialOutcome.Upgraded,"DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN");
            var half=Snapshot([current[0]]);
            PrepareCase("known-half-package-official-repairs-store",half,half,currentState,P.OfficialOutcome.Installed,"DRIVER_PREPARED_DEVICE_UNVERIFIED");
            PrepareCase("check-old-never-enters-SDK",oldState,oldState,currentState,P.OfficialOutcome.Upgraded,"DRIVER_PACKAGE_VERSION_MISMATCH",0,false);
            PrepareCase("check-current-even-with-client-never-enters-SDK",Snapshot(current,idle with {BlockingClients=true}),currentState,currentState,P.OfficialOutcome.AlreadyCurrent,"DRIVER_STORE_READY",0,false);
            PrepareCase("check-newer-preserves-without-SDK",newer,newer,newer,P.OfficialOutcome.KeptNewer,"DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED",0,false);
            var blockedCurrent=Snapshot(current,idle with {BlockingClients=true});
            var deferred=PrepareCase("ROGX-current-store-other-YeManCC-blocks-maintenance",blockedCurrent,currentState,currentState,
                P.OfficialOutcome.AlreadyCurrent,"DRIVER_MAINTENANCE_DEFERRED",0,reads:0);
            Check(deferred.Decision=="CLOSE_CONTROLLER_APPS_FIRST" && deferred.Before.StoreReady && deferred.Official is null &&
                deferred.After is null && deferred.BindingEvidence=="not-observed","blocked maintenance retains refusal reason and independent store proof without binding or SDK result");
            var recheckDeferred=PrepareCase("ROGX-current-store-InputHost-appears-at-recheck",currentState,blockedCurrent,currentState,
                P.OfficialOutcome.AlreadyCurrent,"DRIVER_MAINTENANCE_DEFERRED",0,reads:1);
            Check(recheckDeferred.Decision=="CLOSE_CONTROLLER_APPS_FIRST" && recheckDeferred.Before.Maintenance.Observed.BlockingClients,
                "recheck keeps latest blocking observation");
            if(capturedInventoryRows is not null) {
                var capturedRows=capturedInventoryRows;
                var capturedObservation=Snapshot(capturedRows);
                var capturedBlocked=capturedObservation with {Facts=idle with {BlockingClients=true}};
                var capturedReview=Program.InspectCurrentPackage(true,contract,capturedRows,idle);
                string expected=capturedReview.StoreReady && capturedReview.Inventory.PreparationKnown
                    ? "DRIVER_MAINTENANCE_DEFERRED" : capturedReview.Inventory.PreparationKnown || capturedRows.Count(P.Owned)==0
                    ? "CLOSE_CONTROLLER_APPS_FIRST" : capturedReview.Status;
                var initialField=PrepareCase("captured-pnputil-other-client-at-initial",capturedBlocked,capturedObservation,capturedObservation,
                    P.OfficialOutcome.AlreadyCurrent,expected,0,reads:0);
                Check(initialField.Before.Inventory.Packages.SequenceEqual(capturedReview.Inventory.Packages),"captured package rows retained without substituting synthetic version evidence");
                if(capturedReview.Inventory.PreparationKnown) {
                    var recheckField=PrepareCase("captured-pnputil-InputHost-at-recheck",capturedObservation,capturedBlocked,capturedObservation,
                        P.OfficialOutcome.AlreadyCurrent,expected,0,reads:1);
                    Check(recheckField.Decision=="CLOSE_CONTROLLER_APPS_FIRST","captured-store recheck retains client refusal");
                }
            }
            PrepareCase("newer-store-blocked-no-protocol-claim",newer with {Facts=idle with {BlockingClients=true}},newer,newer,
                P.OfficialOutcome.KeptNewer,"DRIVER_MAINTENANCE_DEFERRED",0,reads:0);
            PrepareCase("current-disappears-before-client-recheck",currentState,oldState with {Facts=idle with {BlockingClients=true}},currentState,
                P.OfficialOutcome.Upgraded,"CLOSE_CONTROLLER_APPS_FIRST",0,reads:1);
            PrepareCase("current-appears-before-client-recheck",oldState,blockedCurrent,currentState,
                P.OfficialOutcome.Upgraded,"DRIVER_MAINTENANCE_DEFERRED",0,reads:1);
            PrepareCase("missing-pair-still-needs-dependency",freshState with {Facts=idle with {BlockingClients=true}},freshState,currentState,
                P.OfficialOutcome.Installed,"CLOSE_CONTROLLER_APPS_FIRST",0,reads:0);
            PrepareCase("half-pair-still-needs-dependency",half with {Facts=idle with {BlockingClients=true}},half,currentState,
                P.OfficialOutcome.Installed,"CLOSE_CONTROLLER_APPS_FIRST",0,reads:0);
            PrepareCase("current-with-unknown-extra-still-refuses-maintenance",Snapshot(currentAndUnknown,idle with {BlockingClients=true}),currentState,currentState,
                P.OfficialOutcome.AlreadyCurrent,"DRIVER_STORE_READY",0,reads:0);
            PrepareCase("current-invalid-owner-not-relabelled",blockedCurrent with {Facts=idle with {BlockingClients=true,StartupOwnerValid=false}},currentState,currentState,
                P.OfficialOutcome.AlreadyCurrent,"INVALID_STARTUP_OWNER",0,reads:0);
            PrepareCase("current-unverified-SDK-not-relabelled",blockedCurrent with {SdkIdentity=false},currentState,currentState,
                P.OfficialOutcome.AlreadyCurrent,"SDK_IDENTITY_MISMATCH",0,reads:0);
            PrepareCase("current-unverified-enum-not-relabelled",blockedCurrent with {EnumValid=false},currentState,currentState,
                P.OfficialOutcome.AlreadyCurrent,"ENUMERATION_UNPROVEN",0,reads:0);
            PrepareCase("admin-gate",Snapshot(previous,idle with {Admin=false}),oldState,currentState,P.OfficialOutcome.Upgraded,"ADMIN_REQUIRED",0);
            PrepareCase("owner-gate",Snapshot(previous,idle with {StartupOwnerValid=false}),oldState,currentState,P.OfficialOutcome.Upgraded,"INVALID_STARTUP_OWNER",0);
            PrepareCase("client-gate",Snapshot(previous,idle with {BlockingClients=true}),oldState,currentState,P.OfficialOutcome.Upgraded,"CLOSE_CONTROLLER_APPS_FIRST",0);
            PrepareCase("install-mutex-gate",Snapshot(previous,idle with {InstallMutexHeld=false}),oldState,currentState,P.OfficialOutcome.Upgraded,"SETUP_ALREADY_RUNNING",0);
            PrepareCase("unknown-old-version-remains-unproven",Snapshot(previous.Select(x=>x with {RawVersion="10/12/2026 1.10.0.1"}).ToList()),oldState,currentState,P.OfficialOutcome.Upgraded,"DRIVER_PACKAGE_VERSION_UNPROVEN",0);
            PrepareCase("foreign-provider-remains-unproven",Snapshot(previous.Select(x=>x with {Provider="Other"}).ToList()),oldState,currentState,P.OfficialOutcome.Upgraded,"DRIVER_PACKAGE_IDENTITY_UNPROVEN",0);
            PrepareCase("SDK-identity-gate",oldState with {SdkIdentity=false},oldState,currentState,P.OfficialOutcome.Upgraded,"SDK_IDENTITY_MISMATCH",0);
            PrepareCase("enum-validity-gate",oldState with {EnumValid=false},oldState,currentState,P.OfficialOutcome.Upgraded,"ENUMERATION_UNPROVEN",0);
            PrepareCase("owner-changes-before-call",oldState,oldState with {Facts=idle with {StartupOwnerValid=false}},currentState,P.OfficialOutcome.Upgraded,"INVALID_STARTUP_OWNER",0);
            PrepareCase("client-appears-before-call",oldState,oldState with {Facts=idle with {BlockingClients=true}},currentState,P.OfficialOutcome.Upgraded,"CLOSE_CONTROLLER_APPS_FIRST",0);
            PrepareCase("SDK-changes-before-call",oldState,oldState with {SdkIdentity=false},currentState,P.OfficialOutcome.Upgraded,"SDK_IDENTITY_MISMATCH",0);
            PrepareCase("enum-return-is-not-enough-old-remains",oldState,oldState,oldState,P.OfficialOutcome.Upgraded,"INSTALL_POSTCONDITION_UNPROVEN");
            PrepareCase("KeptNewer-contradicted-by-current-only-post",newer,newer,currentState,P.OfficialOutcome.KeptNewer,"INSTALL_POSTCONDITION_UNPROVEN");
            PrepareCase("post-enum-unknown",oldState,oldState,currentState with {EnumValid=false},P.OfficialOutcome.Upgraded,"INSTALL_POSTCONDITION_UNPROVEN");
            PrepareCase("post-SDK-identity-unknown",oldState,oldState,currentState with {SdkIdentity=false},P.OfficialOutcome.Upgraded,"INSTALL_POSTCONDITION_UNPROVEN");
            PrepareCase("owner-lost-during-official-call",oldState,oldState,currentState with {Facts=idle with {StartupOwnerValid=false}},P.OfficialOutcome.Upgraded,"INSTALL_POSTCONDITION_UNPROVEN");
            PrepareCase("new-live-client-reserved-before-PnP",oldState,oldState,currentState with {Facts=idle with {BlockingClients=true}},P.OfficialOutcome.Upgraded,"DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN");
            PrepareCase("SDK-fault-deferred-no-retry",oldState,oldState,currentState,P.OfficialOutcome.Upgraded,"OFFICIAL_PREPARATION_FAILED_OR_DEFERRED",sdkThrows:true);
            PrepareCase("post-enumeration-throws-no-retry",oldState,oldState,currentState,P.OfficialOutcome.Upgraded,"INSTALL_POSTCONDITION_UNPROVEN",postThrows:true);
            PrepareCase("undefined-official-enum-never-ready",oldState,oldState,currentState,(P.OfficialOutcome)99,"OFFICIAL_PREPARATION_FAILED_OR_DEFERRED");
            var report=new {status=Failures.Count==0?"PASS":"FAIL",cases=Cases,checks=Checks,failures=Failures,
                sdk="approved-original-2.0.0-x64",actualProductionConsumer="Program.InspectCurrentPackage + DecideForPreparation + HidMaestroPackagePreparation.Execute",
                sdkLoadedForMetadataOnly=true,sdkConstructorsInvoked=0,sdkMethodsInvoked=0,fakeOfficialPreparationTerminal=true,
                deviceOrRegistryOrCertificateApisInvoked=false,normalSetupMainExecuted=false,
                externalLeafSubstitutions=new[]{"driver XML","private raw/package staging files","owner/client/idle observations"},
                catalogProofScope="signed-package file presence only; no signature/trust/binding proof; raw directory intentionally requires no CAT"};
            File.WriteAllText(resultPath,JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
            Console.WriteLine(JsonSerializer.Serialize(report));return Failures.Count==0?0:1;
        }catch(Exception ex){File.WriteAllText(resultPath,JsonSerializer.Serialize(new{status="ERROR",cases=Cases,checks=Checks,error=ex.ToString(),failures=Failures},new JsonSerializerOptions{WriteIndented=true}));Console.Error.WriteLine(ex);return 2;}
    }
}
