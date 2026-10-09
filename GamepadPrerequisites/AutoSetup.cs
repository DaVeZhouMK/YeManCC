using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

internal static partial class Program
{
    const string HidHideSha = "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6";
    static int StartupOwnerPid;
    static string StartupRoot = "";
    static bool ValidStartupOwner()
    {
        try {
            using var p = Process.GetProcessById(StartupOwnerPid);
            using var current = Process.GetCurrentProcess();
            return !p.HasExited && p.StartTime.ToUniversalTime() <= current.StartTime.ToUniversalTime() &&
                string.Equals(Path.GetFullPath(p.MainModule!.FileName), Path.Combine(StartupRoot, "YeManCC", "YeManCC.exe"), StringComparison.OrdinalIgnoreCase);
        } catch { return false; }
    }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr OpenSCManager(string? machine, string? database, uint access);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr OpenService(IntPtr manager, string name, uint access);
    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool QueryServiceStatus(IntPtr service, out ServiceStatus status);
    [DllImport("advapi32.dll")] static extern bool CloseServiceHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)]
    struct ServiceStatus { public uint Type, State, Accepted, Win32Exit, SpecificExit, Checkpoint, Wait; }
    static (bool Present, bool Running) HidHideService()
    {
        var scm = OpenSCManager(null, null, 1);
        if (scm == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            var service = OpenService(scm, "HidHide", 4);
            if (service == IntPtr.Zero) {
                int error = Marshal.GetLastWin32Error();
                if (error == 1060) return (false, false);
                throw new Win32Exception(error);
            }
            try {
                if (!QueryServiceStatus(service, out var status)) throw new Win32Exception(Marshal.GetLastWin32Error());
                return (true, status.State == 4);
            } finally { CloseServiceHandle(service); }
        } finally { CloseServiceHandle(scm); }
    }
    static bool HidHideRegistered()
    {
        foreach (var view in new[] {RegistryView.Registry64, RegistryView.Registry32}) {
            using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var uninstall = hive.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
            if (uninstall is null) continue;
            foreach (string name in uninstall.GetSubKeyNames()) {
                using var key = uninstall.OpenSubKey(name);
                if ((key?.GetValue("DisplayName") as string)?.StartsWith("HidHide", StringComparison.OrdinalIgnoreCase) == true) return true;
            }
        }
        return false;
    }
    static bool HidHideExistingReady(bool present, bool running, bool package, bool driverFile) =>
        running || (present && package && driverFile);
    static string HidHideDecision(bool ready, bool present, bool evidence, bool prepare, bool admin, bool clients)
    {
        if (ready) return "READY";
        if (present || evidence) return "PRESENT_NOT_READY_NO_REINSTALL";
        if (!prepare) return "MISSING";
        if (!admin) return "ADMIN_REQUIRED";
        if (clients) return "CLOSE_CONTROLLER_APPS_FIRST";
        return "INSTALL_FRESH";
    }
    static bool NeedsReboot(int exit) => exit is 3010 or 1641;
    static bool InstallerSuccess(int exit) => exit == 0 || NeedsReboot(exit);
    // Exit codes are consumed by native GamepadDriverSetup::ResultState and the virtual-gamepad UI.
    const int ExitHidHideInstallerMissing = 12;
    const int ExitHidHideInstallerHashMismatch = 13;
    static (string Status, int ExitCode) HidHideInstallerIdentity(bool exists, bool hashMatches)
    {
        if (!exists) return ("HIDHIDE_INSTALLER_MISSING", ExitHidHideInstallerMissing);
        if (!hashMatches) return ("HIDHIDE_INSTALLER_HASH_MISMATCH", ExitHidHideInstallerHashMismatch);
        return ("MATCH", 0);
    }
    static string[] HidHideArguments(string log) => ["/exenoui", "/qn", "/norestart", "/L*v", log];
    static (string Status, int ExitCode) SetupResult(string hidHide, int hidMaestroExit, bool prepare)
    {
        // The reboot requirement survives a second dependency's failure. Details remain in the report.
        if (hidHide == "REBOOT_REQUIRED") return ("REBOOT_REQUIRED", 3010);
        if (hidHide == "READY" && hidMaestroExit == 0) return ("READY", 0);
        if (!prepare && (hidHide == "MISSING" || hidMaestroExit == 10)) return ("MISSING_DEPENDENCY", 10);
        return ("DEPENDENCY_NOT_READY", 11);
    }

    // Retire only the former payload's indexed, byte-identical files. Unknown/modified files survive.
    static string CleanupLegacy(string root)
    {
        var dir = Path.GetFullPath(Path.Combine(root, "PowerControl", "gamepad-prerequisites"));
        if (!Directory.Exists(dir)) return "ABSENT";
        if ((File.GetAttributes(dir) & FileAttributes.ReparsePoint) != 0) return "PRESERVED_REPARSE_POINT";
        string manifest = Path.Combine(dir, "payload-manifest.json");
        if (!File.Exists(manifest)) return "PRESERVED_NO_OWNERSHIP_MANIFEST";
        using var doc = JsonDocument.Parse(File.ReadAllText(manifest));
        if (doc.RootElement.GetProperty("batch").GetString() != "GP-PREREQ-1") return "PRESERVED_UNKNOWN_BATCH";
        string[] known = ["YeManGamepadPrerequisites.exe", "YeManGamepadPrerequisites.dll", "YeManGamepadPrerequisites.deps.json", "YeManGamepadPrerequisites.runtimeconfig.json", "Run-GamepadPrerequisites.ps1", "01-Check.cmd", "02-首次准备.cmd", "README.txt"];
        var files = doc.RootElement.GetProperty("files").EnumerateArray().ToArray();
        if (files.Length != known.Length || files.Select(f=>f.GetProperty("name").GetString()).Distinct().Count() != known.Length) return "PRESERVED_INVALID_MANIFEST";
        foreach (var f in files) {
            string name = f.GetProperty("name").GetString()!;
            if (!known.Contains(name, StringComparer.Ordinal)) return "PRESERVED_UNKNOWN_ENTRY";
            string path = Path.Combine(dir, name);
            if (File.Exists(path) && ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0 ||
                new FileInfo(path).Length != f.GetProperty("bytes").GetInt64() ||
                !FileSha(path).Equals(f.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase))) return "PRESERVED_MODIFIED_FILE";
        }
        if (Directory.EnumerateFileSystemEntries(dir).Any(p => !known.Contains(Path.GetFileName(p), StringComparer.Ordinal) && Path.GetFileName(p) != "payload-manifest.json")) return "PRESERVED_UNKNOWN_FILE";
        foreach (var name in known) File.Delete(Path.Combine(dir, name));
        File.Delete(manifest);
        Directory.Delete(dir, recursive: false);
        return "REMOVED_OWNED_LEGACY_PAYLOAD";
    }
    static int Main(string[] args)
    {
        if (args.Contains("--selftest") || args.Contains("--selftest-sdk-contract")) {
            if (args.Length == 1 && args[0] == "--selftest") { int first=SelfTest(); return first == 0 ? AutoSelfTest() : first; }
            return HidMaestroMain(args);
        }
        bool prepare = false, modeSet = false;
        string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", ".."));
        string report = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC", "gamepad-driver-setup.json");
        for (int i=0;i<args.Length;i++) {
            if (args[i] is "--check" or "--prepare") {
                if (modeSet) return 2;
                modeSet=true; prepare=args[i]=="--prepare";
            } else if (args[i]=="--install-root" && i+1<args.Length) root=Path.GetFullPath(args[++i]);
            else if (args[i]=="--report" && i+1<args.Length) report=Path.GetFullPath(args[++i]);
            else if (args[i]=="--owner-pid" && i+1<args.Length && int.TryParse(args[++i],out int pid) && pid>0) StartupOwnerPid=pid;
            else return 2;
        }
        StartupRoot=root;
        var result = new Dictionary<string,object?> { ["batch"]="GP-PREREQ-2", ["startedAt"]=DateTimeOffset.Now,
            ["prepare"]=prepare,["ownerPid"]=StartupOwnerPid,["installRoot"]=root,["hardwareVerified"]=false };
        int exit=11; string status="UNPROVEN"; bool held=false;
        using var mutex=new Mutex(false,@"Global\YeManCC.GamepadPrerequisites.Install");
        void Save() { Directory.CreateDirectory(Path.GetDirectoryName(report)!); File.WriteAllText(report,JsonSerializer.Serialize(result,Json),new UTF8Encoding(false)); }
        try {
            // Check-only does not take the install mutex, clean up files, or run SDK constructors.
            if (prepare) {
                try { held=mutex.WaitOne(0); } catch (AbandonedMutexException) { held=true; status="PREVIOUS_INSTALL_INTERRUPTED"; return exit; }
                if (!held) { status="SETUP_ALREADY_RUNNING"; return exit; }
                if (StartupOwnerPid!=0 && !ValidStartupOwner()) { status="INVALID_STARTUP_OWNER"; return exit; }
                try { result["legacyCleanup"]=CleanupLegacy(root); } catch(Exception e) { result["legacyCleanup"]="PRESERVED: "+e.Message; }
            }
            // Preserve the previous summary as a bounded diagnostic, not as an installation trigger.
            if (File.Exists(report)) File.Copy(report, report + ".previous", overwrite:true);
            Save();
            bool admin=new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
            var inventory=Pnp("/enum-drivers","/format","xml");
            if (inventory.Exit!=0 || inventory.TimedOut) { status="ENUMERATION_UNPROVEN"; return exit; }
            var drivers=ParseDrivers(inventory.Output);
            var service=HidHideService();
            string repository=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"DriverStore","FileRepository");
            bool hhPackage=drivers.Any(d=>d.Original.StartsWith("hidhide",StringComparison.OrdinalIgnoreCase));
            bool hhFile=File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"drivers","HidHide.sys"));
            bool evidence=hhPackage || HidHideRegistered() || hhFile ||
                Directory.EnumerateDirectories(repository).Any(d=>Path.GetFileName(d).StartsWith("hidhide",StringComparison.OrdinalIgnoreCase));
            // Existing installed filters can be idle without attached hardware. Do not reinstall or
            // block that established path merely because SCM currently reports STOPPED.
            bool hhReady=HidHideExistingReady(service.Present,service.Running,hhPackage,hhFile);
            result["hidhideBefore"]=new {service.Present,service.Running,package=hhPackage,driverFile=hhFile,evidence};
            string hh=HidHideDecision(hhReady,service.Present,evidence,prepare,admin,BlockingClients().Length!=0);
            result["hidhide"]=hh; Save(); bool reboot=false;
            if (hh=="INSTALL_FRESH") {
                string installer=Path.Combine(root,"PowerControl","redist","HidHide_1.5.230_x64.exe");
                result["hidhideInstallerPath"]=installer;
                result["hidhideInstallerExpectedSha256"]=HidHideSha;
                bool installerExists=File.Exists(installer);
                string actualInstallerSha=installerExists ? FileSha(installer) : "";
                result["hidhideInstallerActualSha256"]=actualInstallerSha;
                var installerIdentity=HidHideInstallerIdentity(installerExists,actualInstallerSha==HidHideSha);
                result["hidhideInstallerIdentity"]=installerIdentity.Status;
                if (installerIdentity.ExitCode!=0) { status=installerIdentity.Status; exit=installerIdentity.ExitCode; return exit; }
                // Fresh state only. Do not start/repair/uninstall an existing driver's service.
                if (BlockingClients().Length!=0 || HidHideService().Present || HidHideRegistered()) { status="STATE_CHANGED_BEFORE_INSTALL"; return exit; }
                result["hidhideInstallAttempted"]=true; Save();
                var start=new ProcessStartInfo(installer) { UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Path.GetDirectoryName(installer)! };
                foreach(string a in HidHideArguments(report+".hidhide.log")) start.ArgumentList.Add(a);
                using var child=Process.Start(start) ?? throw new InvalidOperationException("HidHide installer did not start");
                // Never kill an MSI transaction. The app has a bounded UI wait and never retries this process.
                if (!child.WaitForExit(120000)) { result["installerSlow"]=true; Save(); child.WaitForExit(); }
                int code=child.ExitCode; result["hidhideInstallerExit"]=code;
                var after=HidHideService();
                reboot=NeedsReboot(code);
                hh=InstallerSuccess(code) && after.Present ? (reboot || !after.Running ? "REBOOT_REQUIRED" : "READY") : "INSTALL_POSTCONDITION_UNPROVEN";
                result["hidhide"]=hh; Save();
            }
            result["hidhideRebootRequired"]=hh=="REBOOT_REQUIRED";
            if (hh=="REBOOT_REQUIRED") { status="REBOOT_REQUIRED"; exit=3010; }
            string hmReport=report+".hidmaestro.json";
            if (File.Exists(hmReport)) File.Move(hmReport,hmReport+".previous",true);
            int hm=HidMaestroMain([prepare ? "--prepare" : "--check","--install-root",root,"--report",hmReport]);
            result["hidmaestroExit"]=hm;
            using(var detail=JsonDocument.Parse(File.ReadAllText(hmReport))) result["hidmaestro"]=detail.RootElement.GetProperty("status").GetString();
            var final=SetupResult(hh,hm,prepare);
            status=final.Status; exit=final.ExitCode;
            return exit;
        } catch(Exception e) { result["error"]=e.ToString(); if (exit!=3010) status="ERROR_UNPROVEN"; return exit; }
        finally {
            result["status"]=status;result["exitCode"]=exit;result["finishedAt"]=DateTimeOffset.Now;
            try { Save(); } finally { if(held) mutex.ReleaseMutex(); }
            Console.WriteLine(status);Console.WriteLine(report);
        }
    }
    static int AutoSelfTest()
    {
        int n=0;void Check(bool ok,string label){if(!ok)throw new Exception(label);n++;}
        Check(HidHideDecision(true,true,true,true,true,true)=="READY","healthy has no mutation even with clients");
        Check(HidHideExistingReady(true,false,true,true),"installed but idle driver remains usable");
        Check(!HidHideExistingReady(true,false,true,false),"missing installed binary is not ready");
        Check(!HidHideExistingReady(false,false,true,true),"orphaned package is not ready");
        Check(HidHideDecision(false,true,false,true,true,false)=="PRESENT_NOT_READY_NO_REINSTALL","stopped does not mean missing");
        Check(HidHideDecision(false,false,true,true,true,false)=="PRESENT_NOT_READY_NO_REINSTALL","partial preserved");
        Check(HidHideDecision(false,false,false,false,true,false)=="MISSING","check never installs");
        Check(HidHideDecision(false,false,false,true,false,false)=="ADMIN_REQUIRED","elevation required");
        Check(HidHideDecision(false,false,false,true,true,true)=="CLOSE_CONTROLLER_APPS_FIRST","live consumer protected");
        Check(HidHideDecision(false,false,false,true,true,false)=="INSTALL_FRESH","fresh permitted");
        foreach(int code in new[]{0,3010,1641})Check(InstallerSuccess(code),"installer success code");
        foreach(int code in new[]{1603,1618,5,-1})Check(!InstallerSuccess(code),"failure not success");
        Check(!NeedsReboot(0)&&NeedsReboot(3010)&&NeedsReboot(1641),"reboot recognized");
        Check(HidHideInstallerIdentity(false,false)==("HIDHIDE_INSTALLER_MISSING",ExitHidHideInstallerMissing),"missing HidHide installer has distinct result");
        Check(HidHideInstallerIdentity(true,false)==("HIDHIDE_INSTALLER_HASH_MISMATCH",ExitHidHideInstallerHashMismatch),"unexpected HidHide installer hash has distinct result");
        Check(HidHideInstallerIdentity(true,true)==("MATCH",0),"locked HidHide installer identity accepted");
        var args=HidHideArguments(@"C:\path with spaces\install.log");
        Check(args.SequenceEqual(new[]{"/exenoui","/qn","/norestart","/L*v",@"C:\path with spaces\install.log"}),"pinned installer silent switches and path argument");
        Check(SetupResult("REBOOT_REQUIRED",0,true)==("REBOOT_REQUIRED",3010),"reboot after successful prepare");
        foreach(int hm in new[]{10,11,13,15})
            Check(SetupResult("REBOOT_REQUIRED",hm,true)==("REBOOT_REQUIRED",3010),"HidMaestro error does not mask required reboot");
        Check(SetupResult("READY",11,true)==("DEPENDENCY_NOT_READY",11),"no reboot inferred from an unrelated failure");
        Check(SetupResult("READY",0,true)==("READY",0),"both dependencies ready admit target");
        Check(SetupResult("MISSING",0,false)==("MISSING_DEPENDENCY",10),"check-only remains non-installing");
        Check(StartupOwnerPid==0,"owner exception not globally enabled");
        // Exercise the real retirement implementation with an isolated, test-owned tree.
        string temp=Path.Combine(Path.GetTempPath(),"YMCC-setup-selftest-"+Guid.NewGuid().ToString("N"));
        string legacy=Path.Combine(temp,"PowerControl","gamepad-prerequisites");
        string[] oldNames=["YeManGamepadPrerequisites.exe","YeManGamepadPrerequisites.dll","YeManGamepadPrerequisites.deps.json","YeManGamepadPrerequisites.runtimeconfig.json","Run-GamepadPrerequisites.ps1","01-Check.cmd","02-首次准备.cmd","README.txt"];
        void Seed() {
            Directory.CreateDirectory(legacy);
            foreach(var name in oldNames)File.WriteAllText(Path.Combine(legacy,name),"test-owned-"+name);
            var files=oldNames.Select(name=>new {name,bytes=new FileInfo(Path.Combine(legacy,name)).Length,sha256=FileSha(Path.Combine(legacy,name))}).ToArray();
            File.WriteAllText(Path.Combine(legacy,"payload-manifest.json"),JsonSerializer.Serialize(new {batch="GP-PREREQ-1",files}));
        }
        try {
            Seed();Check(CleanupLegacy(temp)=="REMOVED_OWNED_LEGACY_PAYLOAD"&&!Directory.Exists(legacy),"exact old payload retired");
            Seed();File.AppendAllText(Path.Combine(legacy,oldNames[0]),"changed");
            Check(CleanupLegacy(temp)=="PRESERVED_MODIFIED_FILE"&&Directory.GetFiles(legacy).Length==9,"modified payload preserved completely");
            Seed();File.WriteAllText(Path.Combine(legacy,"user-note.txt"),"user");
            Check(CleanupLegacy(temp)=="PRESERVED_UNKNOWN_FILE"&&Directory.GetFiles(legacy).Length==10,"unknown content prevents cleanup");
            File.Delete(Path.Combine(legacy,"user-note.txt"));
            var manifest=Path.Combine(legacy,"payload-manifest.json");
            File.WriteAllText(manifest,File.ReadAllText(manifest).Replace(oldNames[0],"../outside.exe"));
            Check(CleanupLegacy(temp)=="PRESERVED_UNKNOWN_ENTRY","manifest cannot escape known leaves");
        } finally {
            // No recursive deletion; only the fixed files just created by this selftest.
            if(Directory.Exists(legacy)) {
                foreach(var name in oldNames.Concat(new[]{"payload-manifest.json","user-note.txt"}))File.Delete(Path.Combine(legacy,name));
                Directory.Delete(legacy,false);
            }
            if(Directory.Exists(Path.Combine(temp,"PowerControl")))Directory.Delete(Path.Combine(temp,"PowerControl"),false);
            if(Directory.Exists(temp))Directory.Delete(temp,false);
        }
        Console.WriteLine($"GAMEPAD_AUTO_SETUP_SELFTEST_PASS checks={n} deviceOperations=0");return 0;
    }
}
