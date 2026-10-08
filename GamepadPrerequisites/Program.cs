using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using Microsoft.Win32;

// GP-PREREQ-2: locked HIDMaestro first-install engine; AutoSetup owns startup integration.
// Never constructs controllers, changes persona, or performs a global device sweep.
internal static partial class Program
{
    const string CoreSha = "BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E";
    const string CoreMvid = "c61e2715-47ae-4cf3-87f7-244c330ef80e";
    static readonly string[] Infs = ["hidmaestro.inf", "hidmaestro_xusb.inf"];
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    sealed record Driver(string Published, string Original, string Provider);
    sealed record Command(int? Exit, bool TimedOut, string Output, string Error);
    sealed record Facts(bool Hash, bool Admin, bool ClientsRunning,
        bool EnumValid, int OwnedPackages, bool RepositoryRemnants, bool OwnDevices,
        bool SdkReady, bool CompletePackages);
    // This predicate is also used by the selftest. No dependency mutates state until it says INSTALL_FRESH.
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
    static string Reg(string key, string name) => Convert.ToString(Registry.GetValue(key, name, "")) ?? "";
    static string FileSha(string p) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p)));
    static List<Driver> ParseDrivers(string text)
    {
        var doc = XDocument.Parse(text);
        if (doc.Root is null) throw new InvalidDataException("missing XML root");
        var all = doc.Root.Elements().Where(e => e.Name.LocalName == "Driver").ToList();
        // An empty/changed schema must not authorize installation by masquerading as an empty store.
        if (all.Count == 0) throw new InvalidDataException("No Driver records; enumeration cannot prove a fresh store.");
        return all.Select(e => new Driver(e.Attribute("DriverName")?.Value ?? "",
            e.Elements().FirstOrDefault(x => x.Name.LocalName == "OriginalName")?.Value ?? "",
            e.Elements().FirstOrDefault(x => x.Name.LocalName == "ProviderName")?.Value ?? "")).ToList();
    }
    static bool Owned(Driver d) => d.Provider.Equals("HIDMaestro", StringComparison.OrdinalIgnoreCase) &&
        Infs.Contains(d.Original, StringComparer.OrdinalIgnoreCase);
    static bool Complete(List<Driver> ds) => Infs.All(i => ds.Any(d => Owned(d) && d.Original.Equals(i, StringComparison.OrdinalIgnoreCase)));
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
    { "DRIVER_STORE_READY" or "DRIVER_PREPARED_DEVICE_UNVERIFIED" => 0, "MISSING_DRIVER" => 10,
      "SDK_IDENTITY_MISMATCH" => 14, "ADMIN_REQUIRED" => 15,
      "CLOSE_CONTROLLER_APPS_FIRST" => 13, _ => 11 };
    static int HidMaestroMain(string[] args)
    {
        if (args.Length == 1 && args[0] == "--selftest") return SelfTest();
        if (args.Length == 2 && args[0] == "--selftest-sdk-contract") {
            var sdk = Path.GetFullPath(args[1]);
            if (new FileInfo(sdk).Length != 40755712 || FileSha(sdk) != CoreSha) return 14;
            var a = AssemblyLoadContext.Default.LoadFromAssemblyPath(sdk);
            if (a.ManifestModule.ModuleVersionId.ToString() != CoreMvid) return 14;
            var t = a.GetType("HIDMaestro.Internal.DriverBuilder", true)!;
            if (Method(t, "IsDriverInstalled").ReturnType != typeof(bool) || Method(t, "IsDriverInstalled").GetParameters().Length != 0) return 3;
            var deploy = Method(t, "FullDeploy");
            if (deploy.ReturnType != typeof(bool) || deploy.GetParameters().Length != 1 || deploy.GetParameters()[0].ParameterType != typeof(bool)) return 3;
            Console.WriteLine("SDK_CONTRACT_PASS metadataOnly=true sdkMethodsInvoked=0"); return 0;
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
            bool hash = File.Exists(sdkPath) && new FileInfo(sdkPath).Length == 40755712 && FileSha(sdkPath) == CoreSha;
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
            var builder = asm.GetType("HIDMaestro.Internal.DriverBuilder", throwOnError: true)!;
            var probe = Method(builder, "IsDriverInstalled");
            bool sdkReady = (bool)Invoke(probe)!;
            try {
                var contextType = asm.GetType("HIDMaestro.HMContext", throwOnError: true)!;
                log["usbipAvailable"] = contextType.GetProperty("IsUsbipBackendAvailable", BindingFlags.Public | BindingFlags.Static)!.GetValue(null);
            } catch (Exception ex) { log["usbipProbeError"] = ex.GetType().Name + ": " + ex.Message; }
            var cmd = Pnp("/enum-drivers", "/format", "xml");
            File.WriteAllText(report + ".drivers.xml", cmd.Output, new UTF8Encoding(false));
            log["driverEnumeration"] = new { cmd.Exit, cmd.TimedOut, cmd.Error };
            if (cmd.Exit != 0 || cmd.TimedOut) { status = "ENUMERATION_UNPROVEN"; return ExitFor(status); }
            var drivers = ParseDrivers(cmd.Output);
            var owned = drivers.Where(Owned).ToList();
            var devices = Pnp("/enum-devices", "/connected", "/format", "xml");
            log["deviceEnumeration"] = new { devices.Exit, devices.TimedOut, devices.Error };
            if (devices.Exit != 0 || devices.TimedOut) { status = "ENUMERATION_UNPROVEN"; return ExitFor(status); }
            var deviceDoc = XDocument.Parse(devices.Output);
            if (deviceDoc.Root is null || !deviceDoc.Root.Elements().Any()) { status = "ENUMERATION_UNPROVEN"; return ExitFor(status); }
            bool ownDevices = devices.Output.Contains("HIDMAESTRO", StringComparison.OrdinalIgnoreCase);
            bool remnants = RepoHasRemnants(); bool admin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
            var clients = BlockingClients();
            var facts = new Facts(hash, admin, clients.Length != 0, true, owned.Count, remnants, ownDevices, sdkReady, Complete(drivers));
            log["before"] = new { sdkReady, owned, remnants, ownDevices, admin, clients, complete = facts.CompletePackages };
            status = Decide(facts, prepare); log["decision"] = status; Save();
            if (status != "INSTALL_FRESH") return ExitFor(status);
            // Explicit prepare + locked SDK + valid empty store + no own nodes/clients, on any machine.
            // This existing SDK pipeline installs its two embedded INFs and signing certificate.
            // It does not create controllers. No call to HMContext.InstallDriver's global device sweep.
            // Recheck cheap mutable facts immediately before the sole installation call.
            if (BlockingClients().Length != 0 || RepoHasRemnants()) { status = "STATE_CHANGED_BEFORE_INSTALL"; return ExitFor(status); }
            log["installAttempted"] = true; log["installStartedAt"] = DateTimeOffset.Now; Save();
            bool installed = (bool)Invoke(Method(builder, "FullDeploy"), false)!;
            var post = Pnp("/enum-drivers", "/format", "xml");
            File.WriteAllText(report + ".after-drivers.xml", post.Output, new UTF8Encoding(false));
            bool afterSdk = (bool)Invoke(probe)!;
            bool afterComplete = post.Exit == 0 && !post.TimedOut && Complete(ParseDrivers(post.Output));
            log["after"] = new { sdkReady = afterSdk, completePackages = afterComplete, installerReturned = installed, post.Exit, post.TimedOut, post.Error };
            status = installed && afterSdk && afterComplete ? "DRIVER_PREPARED_DEVICE_UNVERIFIED" : "INSTALL_POSTCONDITION_UNPROVEN";
            return ExitFor(status);
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
        var xml = "<Drivers><Driver DriverName='oem1.inf'><OriginalName>hidmaestro.inf</OriginalName><ProviderName>HIDMaestro</ProviderName></Driver><Driver DriverName='oem2.inf'><OriginalName>hidmaestro_xusb.inf</OriginalName><ProviderName>HIDMaestro</ProviderName></Driver></Drivers>";
        Check(Complete(ParseDrivers(xml)), "two exact packages");
        Check(!Complete(ParseDrivers(xml.Replace("HIDMaestro", "OtherVendor"))), "provider strict");
        Check(!Complete(ParseDrivers(xml.Replace("hidmaestro_xusb.inf", "unrelated.inf"))), "both packages required");
        foreach (var bad in new[] {"not xml", "<OtherSchema />"})
        { bool threw=false; try { ParseDrivers(bad); } catch { threw=true; } Check(threw,"unknown is not absent"); }
        Console.WriteLine($"GAMEPAD_PREREQUISITES_SELFTEST_PASS checks={count} deviceOperations=0"); return 0;
    }
}
