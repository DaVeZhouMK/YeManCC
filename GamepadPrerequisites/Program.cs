using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
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
    sealed record StartupInventory(Command Command, IReadOnlyList<Driver> Drivers, long Started);
    internal sealed record Facts(bool Hash, bool Admin, bool ClientsRunning,
        bool EnumValid, int OwnedPackages, bool RepositoryRemnants, bool? OwnDevices,
        bool SdkReady, bool CompletePackages);
    // Original clean first-install policy; established packages use the exact current-package review below.
    static string Decide(Facts f, bool prepare)
    {
        if (!f.Hash) return "SDK_IDENTITY_MISMATCH";
        if (!f.EnumValid) return "ENUMERATION_UNPROVEN";
        if (f.SdkReady && f.CompletePackages) return "DRIVER_STORE_READY";
        if (f.SdkReady || f.OwnedPackages != 0 || f.RepositoryRemnants || f.OwnDevices != false)
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
    static readonly string[] BlockingClientNames =
        ["YeManCC", "YeManInputHost", "HandheldCompanion", "XboxGamingBarHelper", "HIDMaestro", "HidHideClient"];

    [InlineArray(260)]
    struct ProcessImageName { ushort first; }

    [StructLayout(LayoutKind.Sequential)]
    struct ProcessEntry
    {
        public uint Size, Usage, Id;
        public nuint DefaultHeapId;
        public uint ModuleId, ThreadCount, ParentId;
        public int Priority;
        public uint Flags;
        public ProcessImageName Executable;
    }

    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    static extern SafeFileHandle CreateToolhelp32Snapshot(uint flags, uint processId);

    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool Process32FirstW(SafeFileHandle snapshot, ref ProcessEntry entry);

    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool Process32NextW(SafeFileHandle snapshot, ref ProcessEntry entry);

    static string[] BlockingClients(bool includeHidHide = false)
    {
        int commonNames = BlockingClientNames.Length - 1;
        int names = includeHidHide ? BlockingClientNames.Length : commonNames;
        var byName = new List<int>?[names];
        // Fresh process-only snapshot at each guard; no thread or per-process handle enumeration.
        const uint SnapshotProcesses = 0x00000002;
        const int NoMoreFiles = 18;
        using var snapshot = CreateToolhelp32Snapshot(SnapshotProcesses, 0);
        if (snapshot.IsInvalid) throw new Win32Exception(Marshal.GetLastPInvokeError());
        var entry = new ProcessEntry { Size = (uint)Marshal.SizeOf<ProcessEntry>() };
        if (!Process32FirstW(snapshot, ref entry)) throw new Win32Exception(Marshal.GetLastPInvokeError());
        do {
            var observed = MemoryMarshal.Cast<ushort, char>((ReadOnlySpan<ushort>)entry.Executable);
            int terminator = observed.IndexOf('\0');
            if (terminator >= 0) observed = observed[..terminator];
            if (observed.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) observed = observed[..^4];
            for (int i = 0; i < names; i++) {
                if (!observed.Equals(BlockingClientNames[i], StringComparison.OrdinalIgnoreCase)) continue;
                (byName[i] ??= []).Add(checked((int)entry.Id));
                break;
            }
        } while (Process32NextW(snapshot, ref entry));
        int error = Marshal.GetLastPInvokeError();
        if (error != NoMoreFiles) throw new Win32Exception(error);
        var blocked = new List<string>();
        void AppendMatches(int i) {
            if (byName[i] is not { } ids) return;
            string name = BlockingClientNames[i];
            foreach (int id in ids) {
                if (name == "YeManCC" && id == StartupOwnerPid && ValidStartupOwner()) continue;
                blocked.Add(name + ":" + id);
            }
        }
        for (int i = 0; i < commonNames; i++) AppendMatches(i);
        if (StartupOwnerPid != 0 && !ValidStartupOwner()) blocked.Add("startup-owner-exited-or-changed");
        if (includeHidHide) AppendMatches(commonNames);
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
    internal static Driver[]? ExactCurrentPair(IReadOnlyList<Driver> drivers)
    {
        var inventory=PackagePreparation.InspectInventory(drivers);
        return inventory.CurrentComplete && inventory.PreparationKnown && inventory.Packages.Count == 2
            ? inventory.Packages.Select(p=>p.Driver).ToArray() : null;
    }
    static bool CanDeferInitialDeviceCensus(bool normalStartup, string phase, IReadOnlyList<Driver> drivers) =>
        normalStartup && phase == "initial" && ExactCurrentPair(drivers) is not null;
    static bool CanOmitUnchangedPreflightCensus(Driver[]? initial, Driver[]? preflight) =>
        initial is not null && preflight is not null && preflight.All(initial.Contains);
    static bool CanOmitUnchangedPostCensus(bool normalStartup, string phase,
        PackagePreparation.OfficialOutcome? outcome, Driver[]? before, IReadOnlyList<Driver> drivers) =>
        normalStartup && phase == "after-official" && outcome == PackagePreparation.OfficialOutcome.AlreadyCurrent &&
        before is not null && ExactCurrentPair(drivers) is { } after && after.All(d=>before.Contains(d));

    static bool CanReuseStartupInventory(StartupInventory? snapshot, bool ordinaryStartup, string phase, TimeSpan age) =>
        ordinaryStartup && phase=="initial" && age>=TimeSpan.Zero && age<=TimeSpan.FromSeconds(2) &&
        snapshot is { Command: { Exit: 0, TimedOut: false } } && ExactCurrentPair(snapshot.Drivers) is not null;

    static int HidMaestroMain(string[] args, bool normalStartup = false, StartupInventory? initialInventory = null)
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
            // Keep the verified SDK read-locked through inspection and the official call.
            // Allow the assembly loader to read it, but prevent writes and replacement.
            using var sdkFile = File.Exists(sdkPath)
                ? new FileStream(sdkPath, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024, FileOptions.SequentialScan)
                : null;
            bool hash = sdkFile != null && sdkFile.Length == PackagePreparation.CoreBytes &&
                Convert.ToHexString(SHA256.HashData(sdkFile)) == CoreSha;
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
            Driver[]? initialPair=null, officialPreflightPair=null;
            PackagePreparation.OfficialOutcome? officialOutcome=null;
            bool firstObservationReused=false;
            PackagePreparation.Observation Observe(string phase) {
                var snapshot=initialInventory;
                initialInventory=null;
                var reused=snapshot is not null && CanReuseStartupInventory(snapshot,normalStartup && prepare,phase,
                    Stopwatch.GetElapsedTime(snapshot.Started)) ? snapshot : null;
                if (reused is not null) firstObservationReused=true;
                var inventory=reused?.Command ?? Pnp("/enum-drivers","/format","xml");
                string suffix=phase=="initial" ? ".drivers.xml" : phase=="after-official" ? ".after-drivers.xml" : ".before-drivers.xml";
                log["driverInventoryEvidence"]=PackagePreparation.CaptureInventory(report,inventory.Output,inventory.Exit,inventory.TimedOut,suffix);
                Save();
                if(inventory.Exit!=0 || inventory.TimedOut)throw new InvalidDataException("driver enumeration unproven: "+phase);
                var drivers=reused?.Drivers ?? ParseDrivers(inventory.Output);
                bool ordinaryPreparation=normalStartup && prepare;
                bool unchangedPreflight=false;
                if (ordinaryPreparation && phase=="initial") initialPair=ExactCurrentPair(drivers);
                if (ordinaryPreparation && phase=="before-official") {
                    officialPreflightPair=ExactCurrentPair(drivers);
                    unchangedPreflight=CanOmitUnchangedPreflightCensus(initialPair,officialPreflightPair);
                    initialPair=null; // A preflight retry must not reuse this comparison.
                }
                // An exact current pair does not use device presence for admission.
                // Keep it unknown; changed SDK outcomes still read devices afterward.
                string census=CanDeferInitialDeviceCensus(ordinaryPreparation,phase,drivers)
                    ? "deferred-on-initial-current-pair" : unchangedPreflight
                    ? "omitted-before-unchanged-current-pair" : CanOmitUnchangedPostCensus(ordinaryPreparation,
                        phase,officialOutcome,officialPreflightPair,drivers)
                    ? "omitted-after-unchanged-current-pair" : "fresh";
                bool? own=null;
                if (census=="fresh") {
                    var devices=Pnp("/enum-devices","/connected","/format","xml");
                    if(devices.Exit!=0 || devices.TimedOut)throw new InvalidDataException("device enumeration unproven: "+phase);
                    var deviceDoc=XDocument.Parse(devices.Output);
                    if(deviceDoc.Root is null || !deviceDoc.Root.Elements().Any())throw new InvalidDataException("device schema unproven");
                    own=devices.Output.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase);
                }
                var clients=BlockingClients();
                bool admin=new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
                bool owner=StartupOwnerPid==0 || ValidStartupOwner();
                var facts=new PackagePreparation.MaintenanceFacts(admin,mutexHeld,owner,own,clients.Length!=0);
                var observation=new PackagePreparation.Observation(drivers,facts,RepoHasRemnants(),true,true);
                Evidence("observe-"+phase,new {facts,clients,observation.RepositoryRemnants,observation.SdkIdentity,
                    owned=drivers.Where(Owned).ToArray(),deviceCensus=census,
                    inventorySource=reused is not null ? "same-startup-hidhide-read" : "fresh-pnputil",
                    catalogScope="driver-raw-and-package-independent;not-binding-proof"});
                return observation;
            }
            var initial=Observe("initial");
            var runtime=new PackagePreparation.PreparationRuntime(Observe,contract=> {
                // Exactly one official terminal, the body of FullDeploy. Its enum
                // is preserved rather than lost by that public bool forwarding shim.
                log["installAttempted"]=true;
                log["sdkMethodsInvoked"]=new[] {"HIDMaestro.Internal.DriverDeploy.Run(null)"};Save();
                var call=PackagePreparation.InvokeOfficialPreparation(asm);
                officialOutcome=call.Outcome;
                return call;
            },Evidence);
            var result=PackagePreparation.Execute(prepare,metadata,initial,runtime,firstObservationReused);
            log["packagePreparation"]=result;
            log["installAttempted"]=result.SdkCallAttempted;
            status=result.Status;log["decision"]=result.Decision;
            log["officialPolicyScope"]="original-2.0-Deploy-gate+owner-census;startup-only;no-Everything-cleanup;device-binding-not-inferred";
            return ExitFor(status);
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
        var snapshot=new StartupInventory(new(0,false,"synthetic inventory",""),current,0);
        var age=TimeSpan.FromMilliseconds(100);
        Check(CanReuseStartupInventory(snapshot,true,"initial",age),"verified ordinary initial observation reuses outer read");
        Check(!CanReuseStartupInventory(snapshot,false,"initial",age) &&
            !CanReuseStartupInventory(snapshot,true,"before-official",age) &&
            !CanReuseStartupInventory(snapshot,true,"after-official",age),"other entrypoints and official checks always read fresh");
        Check(!CanReuseStartupInventory(snapshot,true,"initial",TimeSpan.FromSeconds(3)) &&
            !CanReuseStartupInventory(snapshot,true,"initial",TimeSpan.FromMilliseconds(-1)),"expired or invalid monotonic age reads fresh");
        Check(!CanReuseStartupInventory(null,true,"initial",age) &&
            !CanReuseStartupInventory(snapshot with {Command=new(1,false,"","failed")},true,"initial",age) &&
            !CanReuseStartupInventory(snapshot with {Command=new(null,true,"","timeout")},true,"initial",age),"failed and absent snapshots never replace inventory");
        Check(!CanReuseStartupInventory(snapshot with {Drivers=old},true,"initial",age) &&
            !CanReuseStartupInventory(snapshot with {Drivers=[current[0]]},true,"initial",age) &&
            !CanReuseStartupInventory(snapshot with {Drivers=[..current,old[0] with {Published="oem3.inf"}]},true,"initial",age),
            "legacy, incomplete and mixed inventory retain original path");
        var sdk=new PackagePreparation.SdkContract(CoreSha.ToLowerInvariant(),CoreMvid,PackagePreparation.EmbeddedManifest,
            "","synthetic",[]);
        var admitted=new PackagePreparation.MaintenanceFacts(true,true,true,false,false);
        var initial=new PackagePreparation.Observation(current,admitted with {OwnDevices=null},false);
        var before=new PackagePreparation.Observation([current[1],current[0]],admitted,false);
        var after=new PackagePreparation.Observation(current,admitted with {OwnDevices=null},false);
        var blocked=before with {Facts=admitted with {BlockingClients=true}};
        var replaced=before with {Drivers=[current[0] with {Published="oem3.inf"},current[1]]};
        (PackagePreparation.PreparationResult Result,string Trace) RunReuse(PackagePreparation.Observation observation,
            bool reused,params (string Phase,PackagePreparation.Observation Observation)[] reads) {
            var queue=new Queue<(string Phase,PackagePreparation.Observation Observation)>(reads);
            var trace=new List<string>();
            var runtime=new PackagePreparation.PreparationRuntime(phase=> {
                trace.Add(phase);
                var next=queue.Dequeue();
                if(next.Phase!=phase) throw new InvalidOperationException("unexpected observation: "+phase);
                return next.Observation;
            },_=> {
                trace.Add("official");
                return new(PackagePreparation.OfficialOutcome.AlreadyCurrent,"recorded test call","recorded test call");
            },(phase,_)=> { if(phase=="reuse-initial-fallback")trace.Add("fallback"); });
            var result=PackagePreparation.Execute(true,sdk,observation,runtime,reused);
            Check(queue.Count==0,"all expected independent reads occurred");
            return (result,string.Join(",",trace));
        }
        var baseline=RunReuse(initial,false,("before-official",before),("after-official",after));
        var stable=RunReuse(initial,true,("before-official",before),("after-official",after));
        Check(stable.Trace=="before-official,official,after-official" && stable.Trace==baseline.Trace &&
            stable.Result.Status=="DRIVER_STORE_READY" && stable.Result.Status==baseline.Result.Status &&
            stable.Result.BindingEvidence==baseline.Result.BindingEvidence,"same pair retains the official lifecycle and binding scope");
        var drift=RunReuse(initial,true,("before-official",replaced),
            ("before-official",replaced),("after-official",replaced));
        Check(drift.Trace=="before-official,fallback,before-official,official,after-official" &&
            drift.Result.Before.Inventory.Packages.Any(p=>p.Driver.Published=="oem3.inf"),"changed verified pair avoids a repeated initial read and requires a new preflight");
        var lateBlock=RunReuse(initial,true,("before-official",replaced),("before-official",blocked));
        Check(lateBlock.Trace=="before-official,fallback,before-official" && !lateBlock.Result.SdkCallAttempted &&
            lateBlock.Result.Status=="DRIVER_MAINTENANCE_DEFERRED","replacement cannot authorize SDK work when the new preflight observes a blocking client");
        var secondReplacement=replaced with {Drivers=[current[0] with {Published="oem4.inf"},current[1]]};
        var driftTwice=RunReuse(initial,true,("before-official",replaced),
            ("before-official",secondReplacement),("after-official",secondReplacement));
        Check(driftTwice.Trace=="before-official,fallback,before-official,official,after-official" &&
            driftTwice.Result.Before.Inventory.Packages.Any(p=>p.Driver.Published=="oem4.inf"),"retry outcome is based on its own preflight even if the pair changes again");
        var denied=RunReuse(initial,true,("before-official",blocked),("initial",blocked));
        Check(denied.Trace=="before-official,fallback,initial" && !denied.Result.SdkCallAttempted &&
            denied.Result.Status=="DRIVER_MAINTENANCE_DEFERRED","changed client guard cannot call the SDK");
        var missing=before with {Drivers=[], Facts=admitted};
        var firstInstall=RunReuse(initial,true,("before-official",missing),("initial",missing),
            ("before-official",missing),("after-official",after));
        Check(firstInstall.Trace=="before-official,fallback,initial,before-official,official,after-official" &&
            firstInstall.Result.Decision=="INSTALL_FRESH","disappearance cannot install using the borrowed basis");
        var invalidOwner=initial with {Facts=admitted with {StartupOwnerValid=false}};
        var noOwner=RunReuse(invalidOwner,true,("initial",invalidOwner));
        Check(noOwner.Trace=="fallback,initial" && !noOwner.Result.SdkCallAttempted &&
            noOwner.Result.Status=="INVALID_STARTUP_OWNER","refusal from reused initial observation is recomputed fresh");
        Check(CanDeferInitialDeviceCensus(true,"initial",current),"ordinary startup may defer exact current pair census");
        Check(!CanDeferInitialDeviceCensus(false,"initial",current),"manual and update launches keep census");
        Check(!CanDeferInitialDeviceCensus(true,"before-official",current) && !CanDeferInitialDeviceCensus(true,"after-official",current),"preflight and postflight never use initial deferral");
        var pair=ExactCurrentPair(current);
        Check(CanOmitUnchangedPreflightCensus(pair,ExactCurrentPair([current[1],current[0]])),
            "two independent observations of the same current pair may omit preflight census");
        Check(!CanOmitUnchangedPreflightCensus(null,pair) && !CanOmitUnchangedPreflightCensus(pair,ExactCurrentPair(old)) &&
            !CanOmitUnchangedPreflightCensus(pair,ExactCurrentPair(replaced.Drivers)),
            "no initial proof, legacy and changed packages retain preflight census");
        var noPreflightCensus=before with {Facts=admitted with {OwnDevices=null}};
        var stableWithoutCensus=RunReuse(initial,true,("before-official",noPreflightCensus),("after-official",after));
        Check(stableWithoutCensus.Trace==stable.Trace && stableWithoutCensus.Result.SdkCallAttempted &&
            stableWithoutCensus.Result.Status==stable.Result.Status &&
            stableWithoutCensus.Result.BindingEvidence==stable.Result.BindingEvidence,
            "unchanged current preparation keeps the official call and binding status without preflight census");
        var changedOutcomeRuntime=new PackagePreparation.PreparationRuntime(
            phase=>phase=="before-official" ? noPreflightCensus : before,
            _=>new(PackagePreparation.OfficialOutcome.Upgraded,"test","test"),(_,_)=>{});
        var changedOutcomeResult=PackagePreparation.Execute(true,sdk,initial,changedOutcomeRuntime,true);
        Check(changedOutcomeResult.Status=="DRIVER_PACKAGES_PREPARED_BINDING_UNPROVEN",
            "a post-call negative cannot establish pre-call absence after an upgrade");
        var didNotChange=PackagePreparation.OfficialOutcome.AlreadyCurrent;
        Check(CanOmitUnchangedPostCensus(true,"after-official",didNotChange,pair,[current[1],current[0]]),"same proved pair may omit post census irrespective of enumeration order");
        Check(!CanOmitUnchangedPostCensus(false,"after-official",didNotChange,pair,current) &&
            !CanOmitUnchangedPostCensus(true,"before-official",didNotChange,pair,current),"manual, update and preflight retain census");
        foreach (var outcome in new PackagePreparation.OfficialOutcome?[]{null,PackagePreparation.OfficialOutcome.Installed,
            PackagePreparation.OfficialOutcome.Upgraded,PackagePreparation.OfficialOutcome.KeptNewer})
            Check(!CanOmitUnchangedPostCensus(true,"after-official",outcome,pair,current),"only verified AlreadyCurrent qualifies");
        Check(!CanOmitUnchangedPostCensus(true,"after-official",didNotChange,null,current) &&
            !CanOmitUnchangedPostCensus(true,"after-official",didNotChange,pair,old) &&
            !CanOmitUnchangedPostCensus(true,"after-official",didNotChange,pair,[current[0]]) &&
            !CanOmitUnchangedPostCensus(true,"after-official",didNotChange,pair,[current[0] with {Published="oem3.inf"},current[1]]) &&
            !CanOmitUnchangedPostCensus(true,"after-official",didNotChange,pair,[..current,old[0] with {Published="oem3.inf"}]),
            "absent, changed, mixed or unproven pair retains post census");
        Check(!CanDeferInitialDeviceCensus(true,"initial",old) && !CanDeferInitialDeviceCensus(true,"initial",[]) &&
            !CanDeferInitialDeviceCensus(true,"initial",[current[0]]) &&
            !CanDeferInitialDeviceCensus(true,"initial",[..current,old[0] with {Published="oem3.inf"}]),"unrecognized, missing, legacy, and mixed packages retain census");
        Check(Decide(f with {OwnDevices=null},true)=="PARTIAL_OR_CONFLICTING_STATE","deferred device evidence never admits first install");
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
