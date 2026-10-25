using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using Microsoft.Win32;
using Driver = HidMaestroPackagePreparation.Driver;
using PackagePreparation = HidMaestroPackagePreparation;

// GP-PREREQ-2: locked HIDMaestro first-install engine; AutoSetup owns startup integration.
// Never constructs controllers, changes persona, or performs a global device sweep.
internal static partial class Program
{
    const string CoreSha = "8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C";
    const string CoreMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    static readonly string[] Infs = ["hidmaestro.inf", "hidmaestro_xusb.inf"];
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    sealed record Command(int? Exit, bool TimedOut, string Output, string Error);
    internal sealed record Facts(bool Hash, bool Admin, bool ClientsRunning,
        bool EnumValid, int OwnedPackages, bool RepositoryRemnants, bool OwnDevices,
        bool SdkReady, bool CompletePackages);
    // Original clean first-install policy; established packages use the exact current-package review below.
    static string Decide(Facts f, bool prepare)
    {
        if (!f.Hash) return "SDK_IDENTITY_MISMATCH";
        if (!f.EnumValid) return "ENUMERATION_UNPROVEN";
        if (f.SdkReady && f.CompletePackages) return "DRIVER_STORE_READY";
        if (f.SdkReady || f.OwnedPackages != 0 || f.RepositoryRemnants || f.OwnDevices)
            return "PARTIAL_OR_CONFLICTING_STATE";
        if (!prepare) return "MISSING_DRIVER";
        if (!f.Admin) return "ADMIN_REQUIRED";
        if (f.ClientsRunning) return "CLOSE_CONTROLLER_APPS_FIRST";
        return "INSTALL_FRESH";
    }
    internal static string DecideForPreparation(Facts facts, PackagePreparation.Review review, bool prepare)
    {
        if (!facts.Hash || !facts.EnumValid) return Decide(facts, prepare);
        if (!prepare) return review.Inventory.Packages.Count!=0 ? review.Status : Decide(facts,false);
        if (review.Inventory.Packages.Count!=0 && !review.Inventory.PreparationKnown) return review.Status;
        var guard=review.Maintenance.Observed;
        if (!guard.StartupOwnerValid) return "INVALID_STARTUP_OWNER";
        if (!facts.Admin) return "ADMIN_REQUIRED";
        if (facts.ClientsRunning) return "CLOSE_CONTROLLER_APPS_FIRST";
        if (!guard.InstallMutexHeld) return "SETUP_ALREADY_RUNNING";
        return review.Inventory.PreparationKnown ? "PREPARE_OFFICIAL" : Decide(facts,true);
    }
    static string Reg(string key, string name) => Convert.ToString(Registry.GetValue(key, name, "")) ?? "";
    static string FileSha(string p) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p)));
    static List<Driver> ParseDrivers(string text) => PackagePreparation.ParseDrivers(text);
    static bool Owned(Driver d) => PackagePreparation.Owned(d);
    internal static PackagePreparation.Review InspectCurrentPackage(bool prepare, PackagePreparation.SdkContract sdk,
        IReadOnlyList<Driver> drivers, PackagePreparation.MaintenanceFacts maintenance) =>
        PackagePreparation.Inspect(prepare, sdk, drivers, maintenance);
    static Command Pnp(params string[] args)
    {
        var si = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "pnputil.exe"))
        { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var a in args) si.ArgumentList.Add(a);
        using var p = Process.Start(si) ?? throw new InvalidOperationException("pnputil did not start");
        var o = p.StandardOutput.ReadToEndAsync(); var e = p.StandardError.ReadToEndAsync();
        if (!p.WaitForExit(30000))
        {
            // Only this read-only enumeration child is bounded/killed, never an installer or YMCC.
            p.Kill(entireProcessTree: true); p.WaitForExit();
            return new(null, true, o.GetAwaiter().GetResult(), e.GetAwaiter().GetResult());
        }
        return new(p.ExitCode, false, o.GetAwaiter().GetResult(), e.GetAwaiter().GetResult());
    }
    static string[] BlockingClients()
    {
        var blocked = new List<string>();
        string[] names = ["YeManCC", "YeManInputHost", "HandheldCompanion", "XboxGamingBarHelper", "HIDMaestro"];
        foreach (string name in names)
        foreach (var process in Process.GetProcessesByName(name))
        using (process) {
            if (name == "YeManCC" && process.Id == StartupOwnerPid && ValidStartupOwner()) continue;
            blocked.Add(name + ":" + process.Id);
        }
        if (StartupOwnerPid != 0 && !ValidStartupOwner()) blocked.Add("startup-owner-exited-or-changed");
        return blocked.ToArray();
    }
    static bool RepoHasRemnants()
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "DriverStore", "FileRepository");
        return Directory.EnumerateDirectories(dir).Any(p => Infs.Any(i => Path.GetFileName(p).StartsWith(i + "_", StringComparison.OrdinalIgnoreCase)));
    }
    static MethodInfo Method(Type t, string name) => t.GetMethod(name, BindingFlags.Public | BindingFlags.Static)
        ?? throw new MissingMethodException(t.FullName, name);
    static object? Invoke(MethodInfo m, params object?[] args)
    {
        try { return m.Invoke(null, args); }
        catch (TargetInvocationException e) { throw new InvalidOperationException(e.InnerException?.ToString() ?? e.ToString(), e.InnerException); }
    }
    static int ExitFor(string s) => s switch
    { "DRIVER_STORE_READY" or "DRIVER_PREPARED_DEVICE_UNVERIFIED" or
        "DRIVER_NEWER_PRESERVED_DEVICE_UNVERIFIED" or "DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN" => 0, "MISSING_DRIVER" => 10,
      "DRIVER_INVENTORY_UNRECOGNIZED" => PackagePreparation.InventoryUnrecognizedExit,
      "DRIVER_MAINTENANCE_DEFERRED" => PackagePreparation.MaintenanceDeferredExit,
      "SDK_IDENTITY_MISMATCH" => 14, "ADMIN_REQUIRED" => 15,
      "CLOSE_CONTROLLER_APPS_FIRST" => 13, _ => 11 };
    static int HidMaestroMain(string[] args)
    {
        if (args.Length == 1 && args[0] == "--selftest") return SelfTest();
        if (args.Length == 2 && args[0] == "--selftest-sdk-contract") {
            var sdk = Path.GetFullPath(args[1]);
            if (new FileInfo(sdk).Length != 63076352 || FileSha(sdk) != CoreSha) return 14;
            var a = AssemblyLoadContext.Default.LoadFromAssemblyPath(sdk);
            if (a.ManifestModule.ModuleVersionId.ToString() != CoreMvid) return 14;
            var expectedStage = PackagePreparation.DefaultRawStage();
            var metadata = PackagePreparation.ReadSdkMetadata(a, expectedStage);
            Console.WriteLine(JsonSerializer.Serialize(new { status="SDK_CONTRACT_PASS", metadataOnly=true,
                sdkMethodsInvoked=0, metadata.Manifest, metadata.Infs, metadata.StageSource }));
            return 0;
        }
        bool prepare = false; bool modeSet = false; string root = @"C:\SOFT\YeMan";
        string report = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
            "GP-PREREQ-1", "prerequisites-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + ".json");
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--prepare" || args[i] == "--check") {
                if (modeSet) { Console.Error.WriteLine("Specify exactly one mode."); return 2; }
                modeSet = true; prepare = args[i] == "--prepare";
            }
            else if (args[i] == "--install-root" && i + 1 < args.Length) root = args[++i];
            else if (args[i] == "--report" && i + 1 < args.Length) report = args[++i];
            else { Console.Error.WriteLine("usage: --check|--prepare [--install-root PATH] [--report PATH]"); return 2; }
        }
        root = Path.GetFullPath(root); report = Path.GetFullPath(report);
        if (File.Exists(report)) { Console.Error.WriteLine("Refusing to overwrite an existing report."); return 2; }
        Directory.CreateDirectory(Path.GetDirectoryName(report)!);
        var log = new Dictionary<string, object?> { ["batch"] = "GP-PREREQ-1", ["time"] = DateTimeOffset.Now,
            ["mode"] = prepare ? "prepare" : "check", ["installRoot"] = root, ["hardwareTested"] = false, ["installAttempted"] = false };
        void Save() => File.WriteAllText(report, JsonSerializer.Serialize(log, Json), new UTF8Encoding(false));
        string status = "UNPROVEN";
        Mutex? installMutex = null; bool mutexHeld = false;
        try
        {
            const string bios = @"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS";
            string maker = Reg(bios, "SystemManufacturer"), product = Reg(bios, "SystemProductName"), board = Reg(bios, "BaseBoardProduct");
            string cpu = Reg(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0", "ProcessorNameString");
            log["machine"] = new { maker, product, board, cpu, purpose = "diagnostic-only; no model restriction" };
            string sdkPath = Path.Combine(root, "PowerControl", "feature-assets", "virtual-gamepad", "HIDMaestro.Core.dll");
            string marker = Path.Combine(Path.GetDirectoryName(sdkPath)!, "test-auto-install.flag");
            log["autoInstallMarkerExists"] = File.Exists(marker);
            bool hash = File.Exists(sdkPath) && new FileInfo(sdkPath).Length == 63076352 && FileSha(sdkPath) == CoreSha;
            log["sdk"] = new { path = sdkPath, expectedSha256 = CoreSha, hashMatches = hash };
            if (!hash) { status = "SDK_IDENTITY_MISMATCH"; return ExitFor(status); }
            // No HMContext construction: its constructor prewarms GameInputSvc. Check mode stays read-only.
            var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(sdkPath);
            if (asm.ManifestModule.ModuleVersionId.ToString() != CoreMvid) { status = "SDK_IDENTITY_MISMATCH"; return ExitFor(status); }
            if (prepare) {
                installMutex = new Mutex(false, @"Global\YeManCC.GamepadPrerequisites.Install");
                try { mutexHeld = installMutex.WaitOne(0); }
                catch (AbandonedMutexException) { mutexHeld = true; status = "PREVIOUS_INSTALL_INTERRUPTED"; return 11; }
                if (!mutexHeld) { status = "SETUP_ALREADY_RUNNING"; return 11; }
            }
            var expectedStage = PackagePreparation.DefaultRawStage();
            var metadata = PackagePreparation.ReadSdkMetadata(asm, expectedStage);
            log["sdkMetadata"] = metadata;
            log["sdkMethodsInvoked"] = Array.Empty<string>();
            log["usbipProbe"] = "not-invoked;UMDF-preparation-only;composite-target-availability-remains-backend-owned";
            void Evidence(string phase, object evidence) {
                log["preparationPhase"] = phase;
                if(!log.TryGetValue("preparationEvidence",out var trail)) {
                    trail=new List<object>();log["preparationEvidence"]=trail;
                }
                ((List<object>)trail!).Add(new {phase,at=DateTimeOffset.Now,evidence});Save();
            }
            PackagePreparation.Observation Observe(string phase) {
                var inventory=Pnp("/enum-drivers","/format","xml");
                string suffix=phase=="initial" ? ".drivers.xml" : phase=="after-official" ? ".after-drivers.xml" : ".before-drivers.xml";
                log["driverInventoryEvidence"]=PackagePreparation.CaptureInventory(report,inventory.Output,inventory.Exit,inventory.TimedOut,suffix);
                Save();
                if(inventory.Exit!=0 || inventory.TimedOut)throw new InvalidDataException("driver enumeration unproven: "+phase);
                var drivers=ParseDrivers(inventory.Output);
                var devices=Pnp("/enum-devices","/connected","/format","xml");
                if(devices.Exit!=0 || devices.TimedOut)throw new InvalidDataException("device enumeration unproven: "+phase);
                var deviceDoc=XDocument.Parse(devices.Output);
                if(deviceDoc.Root is null || !deviceDoc.Root.Elements().Any())throw new InvalidDataException("device schema unproven");
                bool own=devices.Output.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase);
                var clients=BlockingClients();
                bool admin=new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
                bool owner=StartupOwnerPid==0 || ValidStartupOwner();
                bool stillPinned=File.Exists(sdkPath) && new FileInfo(sdkPath).Length==PackagePreparation.CoreBytes && FileSha(sdkPath)==CoreSha;
                var facts=new PackagePreparation.MaintenanceFacts(admin,mutexHeld,owner,own,clients.Length!=0);
                var observation=new PackagePreparation.Observation(drivers,facts,RepoHasRemnants(),true,stillPinned);
                Evidence("observe-"+phase,new {facts,clients,observation.RepositoryRemnants,observation.SdkIdentity,
                    owned=drivers.Where(Owned).ToArray(),catalogScope="driver-raw-and-package-independent;not-binding-proof"});
                return observation;
            }
            var initial=Observe("initial");
            var runtime=new PackagePreparation.PreparationRuntime(Observe,contract=> {
                // Exactly one official terminal, the body of FullDeploy. Its enum
                // is preserved rather than lost by that public bool forwarding shim.
                if(FileSha(sdkPath)!=CoreSha || asm.ManifestModule.ModuleVersionId.ToString()!=CoreMvid)
                    throw new InvalidDataException("SDK changed before official preparation");
                log["installAttempted"]=true;
                log["sdkMethodsInvoked"]=new[] {"HIDMaestro.Internal.DriverDeploy.Run(null)"};Save();
                return PackagePreparation.InvokeOfficialPreparation(asm);
            },Evidence);
            var result=PackagePreparation.Execute(prepare,metadata,initial,runtime);
            log["packagePreparation"]=result;
            log["installAttempted"]=result.SdkCallAttempted;
            status=result.Status;log["decision"]=result.Decision;
            log["officialPolicyScope"]="original-2.0-Deploy-gate+owner-census;startup-only;no-Everything-cleanup;device-binding-not-inferred";
            Save();return ExitFor(status);
        }
        catch (PackagePreparation.InventorySchemaUnrecognizedException e) {
            log["error"] = e.ToString(); log["inventoryRoot"] = e.ObservedRoot;
            status = "DRIVER_INVENTORY_UNRECOGNIZED"; return PackagePreparation.InventoryUnrecognizedExit;
        }
        catch (Exception e) { log["error"] = e.ToString(); status = "ERROR_UNPROVEN"; return 11; }
        finally { if (mutexHeld) installMutex!.ReleaseMutex(); installMutex?.Dispose(); log["status"] = status; log["finishedAt"] = DateTimeOffset.Now; Save(); Console.WriteLine(status); Console.WriteLine(report); }
    }
    static int SelfTest()
    {
        int count = 0; void Check(bool ok, string name) { if (!ok) throw new Exception(name); count++; }
        var f = new Facts(true,true,false,true,0,false,false,false,false);
        Check(Decide(f,false)=="MISSING_DRIVER", "check cannot install");
        Check(Decide(f,true)=="INSTALL_FRESH", "fresh explicit prepare");
        Check(Decide(f with { Hash=false },true)=="SDK_IDENTITY_MISMATCH", "SDK gate");
        Check(Decide(f with { Admin=false },true)=="ADMIN_REQUIRED", "elevation gate");
        Check(Decide(f with { ClientsRunning=true },true)=="CLOSE_CONTROLLER_APPS_FIRST", "live app gate");
        Check(Decide(f with { EnumValid=false },true)=="ENUMERATION_UNPROVEN", "enumeration gate");
        foreach (var g in new[] {f with {OwnedPackages=1}, f with {RepositoryRemnants=true}, f with {OwnDevices=true}, f with {SdkReady=true}})
            Check(Decide(g,true)=="PARTIAL_OR_CONFLICTING_STATE", "do not repair partial installation");
        Check(Decide(f with {SdkReady=true,CompletePackages=true},true)=="DRIVER_STORE_READY", "already ready no install");
        Driver Row(string name, string version, string published) => new(published,name,"HIDMaestro",version);
        var current = new List<Driver> { Row("hidmaestro.inf","10/19/2026 2.0.0.2201","oem1.inf"),
            Row("hidmaestro_xusb.inf","10/19/2026 2.0.0.2201","oem2.inf") };
        var old = current.Select(d => d with { RawVersion="08/21/2026 1.4.7.12" }).ToList();
        Check(PackagePreparation.InspectInventory(current).CurrentComplete,"exact current pair");
        Check(PackagePreparation.HistoricalPairPresent(old),"old pair still has historical presence");
        Check(PackagePreparation.InspectInventory(old).Status=="DRIVER_PACKAGE_VERSION_MISMATCH","old pair is not current readiness");
        Check(!PackagePreparation.InspectInventory([current[0]]).CurrentComplete,"both current packages required");
        Check(!PackagePreparation.InspectInventory(current.Select(d=>d with {RawVersion=""}).ToList()).CurrentComplete,"missing versions unknown");
        Check(!PackagePreparation.InspectInventory(current.Select(d=>d with {Provider="OtherVendor"}).ToList()).CurrentComplete,"strict provider");
        Check(!PackagePreparation.InspectInventory(current.Select(d=>d with {Published="../oem1.inf"}).ToList()).CurrentComplete,"strict published name");
        Check(!PackagePreparation.InspectInventory(current.Select(d=>d with {RawVersion="08/21/2026 2.0.0.2201"}).ToList()).CurrentComplete,"date and version both exact");
        Check(ExitFor("DRIVER_PACKAGE_VERSION_MISMATCH")!=0,"old package version is not ready exit");
        Check(ExitFor("DRIVER_STORE_READY")==0,"current store version scope retains success exit");
        Check(ExitFor("DRIVER_INVENTORY_UNRECOGNIZED")==18,"unknown inventory has advisory exit without claiming ready");
        var xml="<Drivers><Driver DriverName='oem1.inf'><OriginalName>hidmaestro.inf</OriginalName><ProviderName>HIDMaestro</ProviderName><DriverVersion>10/19/2026 2.0.0.2201</DriverVersion></Driver></Drivers>";
        Check(ParseDrivers(xml).Single().RawVersion=="10/19/2026 2.0.0.2201","version kept from XML");
        foreach(var bad in new[]{"not xml","<OtherSchema />","<Drivers />","<Drivers><Other /></Drivers>"}) {
            bool threw=false;try{ParseDrivers(bad);}catch{threw=true;}Check(threw,"unknown is not absent");
        }
        Console.WriteLine($"GAMEPAD_PREREQUISITES_SELFTEST_PASS checks={count} deviceOperations=0 sdkMethodsInvoked=0");return 0;
    }
}
