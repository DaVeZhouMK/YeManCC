using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using Microsoft.Win32;

internal static partial class Program
{
    const string HidHideSha = "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6";
    static readonly Version MinimumHidHideDriver = new(1,4,181,0);
    static readonly Version BundledHidHideProduct = new(1,5,230);
    const string HidHideVendor = "Nefarius Software Solutions e.U.";
    const string HidHideCli = @"C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe";
    const string HidHideRepairKey = @"SOFTWARE\YeManCC\HidHideMaintenance";
    const string HidHideUpgradeCode = "{8822CC70-E2A5-4CB7-8F14-E27101150A1D}";
    // Official 1.1.50.0 package observed after its MSI removed the root device.
    const string LegacyHidHideInfSha = "8BC2D15ADE0CF1F43363F85295F9416FA87ED50F94DF25FD48F83587B0F9E2DE";
    const string LegacyHidHideSysSha = "8698BB98850ADC15459BC70E08DB27122C206607B2FE93630E344B9397E76614";
    static readonly Version LegacyHidHideResidueVersion = new(1,1,50,0);
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
    [DllImport("advapi32.dll", EntryPoint = "EnumDependentServicesW", ExactSpelling = true, SetLastError = true)]
    static extern bool EnumDependentServices(IntPtr service, uint state, IntPtr entries, uint bytes, out uint needed, out uint count);
    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool DeleteService(IntPtr service);
    [DllImport("advapi32.dll")] static extern bool CloseServiceHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)]
    struct ServiceStatus { public uint Type, State, Accepted, Win32Exit, SpecificExit, Checkpoint, Wait; }
    static (bool Present, bool Running, bool Stopped) HidHideService()
    {
        var scm = OpenSCManager(null, null, 1);
        if (scm == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            var service = OpenService(scm, "HidHide", 4);
            if (service == IntPtr.Zero) {
                int error = Marshal.GetLastWin32Error();
                if (error == 1060) return (false, false, false);
                throw new Win32Exception(error);
            }
            try {
                if (!QueryServiceStatus(service, out var status)) throw new Win32Exception(Marshal.GetLastWin32Error());
                return (true, status.State == 4, status.State == 1 && status.Type == 1);
            } finally { CloseServiceHandle(service); }
        } finally { CloseServiceHandle(scm); }
    }
    [DllImport("msi.dll", CharSet = CharSet.Unicode, EntryPoint = "MsiEnumRelatedProductsW", ExactSpelling = true)]
    static extern uint MsiEnumRelatedProducts(string upgradeCode, uint reserved, uint index, StringBuilder product);
    sealed record HidHideProduct(string Code, string Name, string Publisher, string Version, bool MachineMsi);
    static bool IsHidHideProductName(string name) => name.Equals("HidHide", StringComparison.OrdinalIgnoreCase) ||
        name.Equals(HidHideVendor + " HidHide (x64)", StringComparison.OrdinalIgnoreCase);
    static HidHideProduct[] HidHideProducts()
    {
        var related=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for (uint i=0;;i++) {
            if (i==64) throw new InvalidDataException("HidHide MSI enumeration exceeded budget");
            var code=new StringBuilder(39);
            uint rc=MsiEnumRelatedProducts(HidHideUpgradeCode,0,i,code);
            if (rc==259) break; // ERROR_NO_MORE_ITEMS
            if (rc!=0) throw new Win32Exception((int)rc);
            related.Add(code.ToString());
        }
        var rows=new Dictionary<string,HidHideProduct>(StringComparer.OrdinalIgnoreCase);
        foreach (var view in new[] {RegistryView.Registry64, RegistryView.Registry32}) {
            using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var uninstall = hive.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
            if (uninstall is null) continue;
            foreach (string name in uninstall.GetSubKeyNames()) {
                using var key = uninstall.OpenSubKey(name);
                string display=key?.GetValue("DisplayName") as string ?? "";
                if (!related.Contains(name) && !IsHidHideProductName(display) &&
                    !display.StartsWith("HidHide ",StringComparison.OrdinalIgnoreCase)) continue;
                var row=new HidHideProduct(name,display,key?.GetValue("Publisher") as string ?? "",
                    key?.GetValue("DisplayVersion") as string ?? "", related.Contains(name) &&
                    key?.GetValue("WindowsInstaller") is int msi && msi==1 && Guid.TryParseExact(name,"B",out _));
                if (!rows.TryAdd(name,row) && rows[name]!=row) throw new InvalidDataException("Conflicting HidHide registrations");
            }
        }
        foreach (string code in related) rows.TryAdd(code,new(code,"","","",false));
        return rows.Values.ToArray();
    }
    static Version? VerifiedHidHideProductVersion(HidHideProduct row) => row.MachineMsi && IsHidHideProductName(row.Name) &&
        row.Publisher.Equals(HidHideVendor,StringComparison.OrdinalIgnoreCase) &&
        Version.TryParse(row.Version,out var v) && v.Build>=0 ? new Version(v.Major,v.Minor,v.Build) : null;
    static bool IsLegacyHidHideProduct(HidHideProduct row) => VerifiedHidHideProductVersion(row) is { Major: 1 } v && v<BundledHidHideProduct;
    static Version? HidHidePackageVersion(HidMaestroPackagePreparation.Driver row) =>
        row.Original.Equals("hidhide.inf",StringComparison.OrdinalIgnoreCase) &&
        row.Provider.Equals(HidHideVendor,StringComparison.OrdinalIgnoreCase) &&
        Version.TryParse(row.RawVersion.Split(' ',StringSplitOptions.RemoveEmptyEntries).LastOrDefault(),out var v) ? v : null;

    sealed record HidHideFacts(bool Present, bool Running, bool DriverFile, Version? DriverVersion, bool Cli,
        bool Repository, string[] Filters, HidMaestroPackagePreparation.Driver[] Packages, HidHideProduct[] Products)
    {
        public bool Stopped { get; init; }
        public bool Evidence => Present || DriverFile || Cli || Repository || Filters.Length!=0 || Packages.Length!=0 || Products.Length!=0;
        public bool Old => Present && DriverVersion is not null && DriverVersion<MinimumHidHideDriver;
        public bool OldPackage => Packages.Any(IsLegacyHidHidePackage);
        public bool Current => Present && DriverFile && Cli && DriverVersion>=MinimumHidHideDriver &&
            Packages.Length!=0 && Packages.All(x=>HidHidePackageVersion(x)>=MinimumHidHideDriver) &&
            Products.Length==1 && VerifiedHidHideProductVersion(Products[0])>=BundledHidHideProduct;
    }
    static HidHideFacts ReadHidHideFacts(IReadOnlyList<HidMaestroPackagePreparation.Driver> drivers)
    {
        var service=HidHideService();
        string system=Environment.GetFolderPath(Environment.SpecialFolder.System);
        string file=Path.Combine(system,"drivers","HidHide.sys");
        bool present=File.Exists(file);
        using var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64);
        using var serviceKey=machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide");
        using var classes=machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class") ?? throw new InvalidDataException("Device class inventory unavailable");
        var filters=new List<string>();
        foreach (string id in classes.GetSubKeyNames()) {
            using var key=classes.OpenSubKey(id);
            foreach (string prop in new[]{"UpperFilters","LowerFilters"}) {
                var value=key?.GetValue(prop);
                if ((value is string[] list && list.Contains("HidHide",StringComparer.OrdinalIgnoreCase)) ||
                    (value is string text && text.Equals("HidHide",StringComparison.OrdinalIgnoreCase))) filters.Add(id+":"+prop);
            }
        }
        return new(service.Present || serviceKey is not null,service.Running,present,present ? HidHideFileVersion(file) : null,
            File.Exists(HidHideCli),Directory.EnumerateDirectories(Path.Combine(system,"DriverStore","FileRepository"),"hidhide*").Any(),
            filters.ToArray(),drivers.Where(d=>d.Original.StartsWith("hidhide",StringComparison.OrdinalIgnoreCase)).ToArray(),HidHideProducts()) { Stopped=service.Stopped };
    }
    static bool HidHideExistingReady(bool present, bool running, bool package, bool driverFile) =>
        running || (present && package && driverFile);
    static Version? HidHideFileVersion(string path)
    {
        try {
            var info=FileVersionInfo.GetVersionInfo(path);
            var version=new Version(info.FileMajorPart,info.FileMinorPart,info.FileBuildPart,info.FilePrivatePart);
            return version==new Version(0,0,0,0) ? null : version;
        } catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }
    static string HidHideDecision(bool ready, bool present, bool evidence, bool prepare, bool admin, bool clients,
        bool legacyDriver = false, bool cliPresent = true)
    {
        if (legacyDriver) return "DRIVER_TOO_OLD_NO_REINSTALL";
        if (ready && cliPresent) return "READY";
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
    const string HidHideOrphanRebootPhase = "reboot-before-orphan-cleanup";
    sealed record HidHideRepair(string Phase, string Boot, int? ExitCode = null, bool LegacyDiskTakeover = false,
        string? OrphanPackage = null);
    static bool IsPublishedHidHideInf(string? published) => published is not null &&
        System.Text.RegularExpressions.Regex.IsMatch(published,@"\Aoem[0-9]+\.inf\z",System.Text.RegularExpressions.RegexOptions.IgnoreCase);
    static bool IsLegacyHidHidePackage(HidMaestroPackagePreparation.Driver row) =>
        HidHidePackageVersion(row) is { } v && v<MinimumHidHideDriver;
    static HidHideProduct? SelectHidHideUninstall(HidHideFacts facts) =>
        (facts.Old || (facts.DriverVersion is null && !facts.Cli && facts.OldPackage)) &&
        facts.Packages.All(IsLegacyHidHidePackage) &&
        facts.Products.Length==1 && IsLegacyHidHideProduct(facts.Products[0]) ? facts.Products[0] : null;
    static HidHideProduct? SelectInstallerHidHideUninstall(HidHideFacts facts) =>
        SelectHidHideUninstall(facts) is { } product && VerifiedHidHideProductVersion(product)==new Version(1,1,50) &&
        facts.DriverVersion==LegacyHidHideResidueVersion && facts.Packages.Length!=0 &&
        facts.Packages.All(p=>HidHidePackageVersion(p)==LegacyHidHideResidueVersion) ? product : null;
    static bool HidHideLegacyDiskCandidate(HidHideFacts facts) => facts.Present && facts.Stopped && !facts.Running &&
        facts.DriverFile && facts.DriverVersion==LegacyHidHideResidueVersion && !facts.Cli &&
        facts.Filters.Length==0 && facts.Products.Length==0;
    static string? SelectInactiveHidHidePackage(HidHideFacts facts) =>
        !facts.Cli && facts.Filters.Length==0 && facts.Products.Length==0 &&
        facts.Packages.Length==1 && IsLegacyHidHidePackage(facts.Packages[0]) &&
        ((!facts.Present && !facts.DriverFile) || (HidHideLegacyDiskCandidate(facts) &&
            HidHidePackageVersion(facts.Packages[0])==LegacyHidHideResidueVersion)) &&
        IsPublishedHidHideInf(facts.Packages[0].Published)
        ? facts.Packages[0].Published : null;
    static string? SelectManualUninstallResidue(HidHideFacts facts) =>
        facts.Repository && HidHideLegacyDiskCandidate(facts) ? SelectInactiveHidHidePackage(facts) : null;
    static bool MatchesManualUninstallResidue(HidHideRepair state, HidHideFacts facts) =>
        IsPublishedHidHideInf(state.OrphanPackage) &&
        string.Equals(state.OrphanPackage,SelectManualUninstallResidue(facts),StringComparison.OrdinalIgnoreCase);
    static bool ValidHidHideOrphanWitness(HidHideRepair state) => state.Phase==HidHideOrphanRebootPhase
        ? IsPublishedHidHideInf(state.OrphanPackage) && state.ExitCode is null && !state.LegacyDiskTakeover
        : state.OrphanPackage is null || (state.Phase=="install-after-cleanup" && IsPublishedHidHideInf(state.OrphanPackage) && state.ExitCode==0);
    static bool HidHideLegacyFileOnly(HidHideFacts facts) => !facts.Present && !facts.Running && facts.DriverFile &&
        facts.DriverVersion==LegacyHidHideResidueVersion && !facts.Cli && facts.Filters.Length==0 && facts.Products.Length==0 &&
        facts.Packages.Length==0 && !facts.Repository;
    static bool CanInstallAfterHidHideCleanup(HidHideFacts facts, bool allowLegacyDisk) => !facts.Evidence ||
        (allowLegacyDisk && HidHideLegacyFileOnly(facts));

    const string HidHideDeviceInventoryCommand = "/enum-devices /drivers /services /stack /deviceids /format xml";
    static string HidHideDeviceReferences(string xml, string? published)
    {
        if (xml.Length>8*1024*1024) return "device-inventory-oversize";
        var root=XDocument.Parse(xml.TrimStart('\ufeff')).Root;
        if (root?.Name!="PnpUtil" || (string?)root.Attribute("Command")!=HidHideDeviceInventoryCommand ||
            !root.HasElements || root.Elements().Any(d=>d.Name!="Device" ||
                string.IsNullOrWhiteSpace((string?)d.Attribute("InstanceId")) || string.IsNullOrWhiteSpace((string?)d.Element("Status"))))
            return "device-inventory-unrecognized";
        bool Reference(string? s) => s is not null && (s.Equals("HidHide",StringComparison.OrdinalIgnoreCase) ||
            s.Equals(@"ROOT\HIDHIDE",StringComparison.OrdinalIgnoreCase) ||
            s.Equals("hidhide.inf",StringComparison.OrdinalIgnoreCase) ||
            (published is not null && s.Equals(published,StringComparison.OrdinalIgnoreCase)));
        return root.Descendants().Any(x=>Reference(x.HasElements ? null : x.Value) || x.Attributes().Any(a=>Reference(a.Value)))
            ? "device-reference" : "clear";
    }

    static bool HidHideDeviceFiltersClear(RegistryKey machine)
    {
        using var devices=machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum") ?? throw new InvalidDataException("Device registry unavailable");
        foreach (string bus in devices.GetSubKeyNames()) {
            using var busKey=devices.OpenSubKey(bus) ?? throw new InvalidDataException("Device registry changed");
            foreach (string id in busKey.GetSubKeyNames()) {
                using var idKey=busKey.OpenSubKey(id) ?? throw new InvalidDataException("Device registry changed");
                foreach (string instance in idKey.GetSubKeyNames()) {
                    using var key=idKey.OpenSubKey(instance) ?? throw new InvalidDataException("Device registry changed");
                    foreach (string name in new[]{"UpperFilters","LowerFilters"})
                        if (key.GetValue(name) is { } value && (value is string[] names ?
                            names.Contains("HidHide",StringComparer.OrdinalIgnoreCase) :
                            value is not string text || text.Equals("HidHide",StringComparison.OrdinalIgnoreCase))) return false;
                }
            }
        }
        return true;
    }

    enum HidHideLegacyServiceAction { VerifyDemandStart, RetireDisabled, VerifyAbsent }
    static string VerifyLegacyHidHideDisk(RegistryKey machine, string system, string? published, HidHideLegacyServiceAction action)
    {
        try {
            bool ExactFile(string p, string sha) => (File.GetAttributes(p)&FileAttributes.ReparsePoint)==0 && FileSha(p)==sha;
            if (!ExactFile(Path.Combine(system,"drivers","HidHide.sys"),LegacyHidHideSysSha)) return "sys-identity";
            bool retiringService=action==HidHideLegacyServiceAction.RetireDisabled;
            if (action==HidHideLegacyServiceAction.VerifyDemandStart &&
                (published is null || !ExactFile(Path.Combine(Directory.GetParent(system)!.FullName,"INF",published),LegacyHidHideInfSha)))
                return "inf-identity";
            var inventory=Pnp(HidHideDeviceInventoryCommand.Split(' '));
            if (inventory.Exit!=0 || inventory.TimedOut) return "device-inventory-failed";
            string references=HidHideDeviceReferences(inventory.Output,published);
            if (references!="clear") return references;
            if (!HidHideDeviceFiltersClear(machine)) return "device-filter-reference";
            using (var key=machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide")) {
                if (action==HidHideLegacyServiceAction.VerifyAbsent)
                    return key is null && !HidHideService().Present ? "verified" : "service-still-present";
                if (key?.GetValue("Type") is not 1 || key.GetValue("Start") is not int start || start!=(retiringService ? 4 : 3) ||
                    !string.Equals(key.GetValue("ImagePath",null,RegistryValueOptions.DoNotExpandEnvironmentNames) as string,
                        @"\SystemRoot\System32\drivers\HidHide.sys",StringComparison.OrdinalIgnoreCase)) return "service-identity";
            }
            IntPtr scm=OpenSCManager(null,null,1);
            if (scm==IntPtr.Zero) return "service-query-failed";
            try {
                IntPtr service=OpenService(scm,"HidHide",retiringService ? 0x1000Cu : 0xCu); // QUERY_STATUS | ENUMERATE_DEPENDENTS | optional DELETE
                if (service==IntPtr.Zero) return "service-query-failed";
                try {
                    if (!QueryServiceStatus(service,out var status) || status.State!=1 || status.Type!=1) return "service-not-stopped";
                    if (!EnumDependentServices(service,3,IntPtr.Zero,0,out _,out uint count) || count!=0) return "service-dependents-unproven";
                    if (!retiringService) return "verified";
                    // The PnP package is already gone. Retire only the stopped, disabled legacy service via SCM.
                    return DeleteService(service) ? "retired" : "service-delete-win32:"+Marshal.GetLastWin32Error();
                } finally { CloseServiceHandle(service); }
            } finally { CloseServiceHandle(scm); }
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException or System.Xml.XmlException or InvalidOperationException or Win32Exception) {
            return "inventory-error:"+e.GetType().Name;
        }
    }
    static string HidHideRepairDecision(HidHideRepair state, string? boot, HidHideFacts facts)
    {
        if (boot is null || state.Boot==boot)
            return state.Phase.StartsWith("reboot-",StringComparison.Ordinal) ? "REBOOT_REQUIRED" : state.Phase switch {
                "uninstall-failed" => "UNINSTALL_FAILED", "install-failed" => "UPGRADE_INSTALL_FAILED",
                "residue" => "UNINSTALL_RESIDUE", _ => "UPGRADE_INTERRUPTED"
            };
        if (facts.Current && facts.Running) return "CLEAR_REPAIR";
        if (state.Phase==HidHideOrphanRebootPhase)
            return !ValidHidHideOrphanWitness(state) ? "UPGRADE_INTERRUPTED" : !facts.Evidence ? "INSTALL_AFTER_UNINSTALL" :
                MatchesManualUninstallResidue(state,facts) ? "RETIRE_INACTIVE_PACKAGE" : "UNINSTALL_RESIDUE";
        if (state.Phase=="reboot-after-uninstall" && state.ExitCode is int code && InstallerSuccess(code))
            return !facts.Evidence ? "INSTALL_AFTER_UNINSTALL" : SelectInactiveHidHidePackage(facts) is not null ? "RETIRE_INACTIVE_PACKAGE" : "UNINSTALL_RESIDUE";
        if (state.Phase=="install-after-cleanup" && state.ExitCode is int cleanupCode && InstallerSuccess(cleanupCode))
            return CanInstallAfterHidHideCleanup(facts,state.LegacyDiskTakeover) ? "INSTALL_AFTER_UNINSTALL" : "UNINSTALL_RESIDUE";
        return state.Phase switch {
            "reboot-after-install" or "install-failed" => "UPGRADE_INSTALL_FAILED",
            "uninstall-failed" => "UNINSTALL_FAILED", "residue" => "UNINSTALL_RESIDUE", _ => "UPGRADE_INTERRUPTED"
        };
    }
    static string[] HidHideUninstallArguments(string code, string log) => ["/x",code,"/qn","/norestart","/L*v",log];
    static string[] HidHideBlockingClients() => BlockingClients(includeHidHide: true);

    static bool CanOfferHidHideInventory(HidHideFacts facts, string decision, bool hasTransaction) =>
        !hasTransaction && (decision=="READY" ||
            (decision=="PRESENT_NOT_READY_NO_REINSTALL" && !facts.Cli && facts.Present && facts.Running &&
             facts.DriverFile && facts.DriverVersion is { } version && version>=MinimumHidHideDriver && !facts.OldPackage));

    // The protected record outlives the setup process and the per-user exported report.
    // All mutations are serialized by the existing module/setup leases in Main.
    static int RunInstallerHidHideUninstall(string root, string report)
    {
        if (File.Exists(report)) return 2;
        var evidence=new Dictionary<string,object?> { ["mode"]="uninstall-legacy-hidhide", ["status"]="CHECKING" };
        void Save() {
            string temp=report+"."+Guid.NewGuid().ToString("N")+".tmp";
            try { File.WriteAllText(temp,JsonSerializer.Serialize(evidence,Json)); File.Move(temp,report,true); }
            finally { File.Delete(temp); }
        }
        string status="ERROR_UNPROVEN";
        int exit=11;
        try {
            if (!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)) {
                status="UPGRADE_ADMIN_REQUIRED"; return exit;
            }
            using var mutex=new Mutex(false,@"Global\YeManCC.GamepadPrerequisites.Install");
            bool held=false;
            try {
                try { held=mutex.WaitOne(0); }
                catch (AbandonedMutexException) { held=true; status="PREVIOUS_INSTALL_INTERRUPTED"; return exit; }
                if (!held) { status="SETUP_ALREADY_RUNNING"; return exit; }
                using var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64);
                using var state=machine.OpenSubKey(HidHideRepairKey);
                if (HasHidHideRepairState(state)) { status="MAINTENANCE_PENDING"; return exit; }
                var inventory=Pnp("/enum-drivers","/format","xml");
                evidence["driverInventoryEvidence"]=HidMaestroPackagePreparation.CaptureInventory(report,inventory.Output,inventory.Exit,inventory.TimedOut);
                if (inventory.Exit!=0 || inventory.TimedOut) { status="ENUMERATION_UNPROVEN"; return exit; }
                var facts=ReadHidHideFacts(ParseDrivers(inventory.Output));
                evidence["hidhideBefore"]=facts;
                var outcome=PrepareHidHide(facts,true,true,false,root,report,evidence,Save,out _,installerUninstallOnly:true);
                status=outcome.State; exit=outcome.Exit; return exit;
            }
            finally { if (held) mutex.ReleaseMutex(); }
        }
        catch (Exception e) { evidence["error"]=e.ToString(); return 11; }
        finally { evidence["status"]=status; evidence["finishedAt"]=DateTimeOffset.UtcNow; Save(); }
    }
    static bool HasHidHideRepairState(RegistryKey? key) => key?.GetValueNames().Contains("State",StringComparer.OrdinalIgnoreCase)==true;

    static (string State, int Exit) PrepareHidHide(HidHideFacts facts, bool prepare, bool admin,
        bool usbipRebootPending, string root, string report, Dictionary<string,object?> result, Action save,
        out bool canOfferInitialInventory, bool installerUninstallOnly=false)
    {
        canOfferInitialInventory=false;
        using var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64);
        using var existing=machine.OpenSubKey(HidHideRepairKey);
        if (installerUninstallOnly && HasHidHideRepairState(existing)) return ("MAINTENANCE_PENDING",0);
        string? raw=existing?.GetValue("State") as string;
        HidHideRepair? transaction=null;
        if (raw is not null) {
            try { transaction=raw.Length<=1024 ? JsonSerializer.Deserialize<HidHideRepair>(raw) : null; }
            catch (JsonException) { return ("UPGRADE_INTERRUPTED",0); }
            if (transaction is null || !YeManCC.UsbipMaintenanceState.ValidBootSession(transaction.Boot) ||
                transaction.Phase is not ("uninstalling" or "reboot-after-uninstall" or "uninstall-failed" or
                    "cleaning" or "install-after-cleanup" or "residue" or "installing" or "reboot-after-install" or "install-failed" or HidHideOrphanRebootPhase) ||
                !ValidHidHideOrphanWitness(transaction))
                return ("UPGRADE_INTERRUPTED",0);
        }
        else if (existing?.GetValueNames().Contains("State",StringComparer.OrdinalIgnoreCase)==true)
            return ("UPGRADE_INTERRUPTED",0);
        result["hidhideTransactionBefore"]=transaction;
        if (transaction?.ExitCode is int previousCode) result["hidhideMaintenanceExit"]=previousCode;
        var clients=HidHideBlockingClients();
        result["hidhideBlockingClients"]=clients;
        var candidate=installerUninstallOnly ? SelectInstallerHidHideUninstall(facts) : SelectHidHideUninstall(facts);
        if (installerUninstallOnly && candidate is null) return ("UNINSTALL_IDENTITY_UNVERIFIED",0);
        var orphan=transaction is null && prepare && candidate is null ? SelectManualUninstallResidue(facts) : null;
        string decision=HidHideDecision(HidHideExistingReady(facts.Present,facts.Running,facts.Packages.Length!=0,facts.DriverFile),
            facts.Present,facts.Evidence,prepare,admin,clients.Length!=0,facts.Old,facts.Cli);
        if (transaction is not null || (prepare && (candidate is not null || orphan is not null))) {
            string? boot=YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity;
            if (boot is null && prepare && admin && YeManCC.UsbipMaintenanceState.PrepareBootSessionIdentity())
                boot=YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity;
            result["hidhideBootSessionIdentity"]=boot;
            if (transaction is not null) decision=HidHideRepairDecision(transaction,boot,facts);
            else decision=boot is null ? "UPGRADE_BOOT_UNVERIFIED" : orphan is not null ? "VERIFY_ORPHAN_BEFORE_REBOOT" : "UNINSTALL_LEGACY";

            if (decision is "UNINSTALL_LEGACY" or "INSTALL_AFTER_UNINSTALL" or "RETIRE_INACTIVE_PACKAGE" or "CLEAR_REPAIR" or "VERIFY_ORPHAN_BEFORE_REBOOT") {
                if (!prepare) return ("UPGRADE_DEFERRED",0);
                if (usbipRebootPending) return ("UPGRADE_DEFERRED",0);
                if (!admin) return ("UPGRADE_ADMIN_REQUIRED",0);
                if (decision=="CLEAR_REPAIR") {
                    using var key=machine.OpenSubKey(HidHideRepairKey,writable:true)!;
                    key.DeleteValue("State"); key.Flush();
                    result["hidhideTransactionCompleted"]=true;
                    return ("READY",0);
                }
                if (clients.Length!=0) return ("UPGRADE_CLOSE_APPS",0);
            }
        } else if (prepare && (facts.Old || (!facts.Cli && facts.OldPackage))) {
            result["hidhideUninstallSkippedReason"]="official-legacy-msi-not-uniquely-verified";
            return ("UNINSTALL_IDENTITY_UNVERIFIED",0);
        }
        if (decision is not ("INSTALL_FRESH" or "UNINSTALL_LEGACY" or "INSTALL_AFTER_UNINSTALL" or "RETIRE_INACTIVE_PACKAGE" or "VERIFY_ORPHAN_BEFORE_REBOOT")) {
            // The CLI warning still applies; this inventory can only seed HIDMaestro's first read.
            canOfferInitialInventory=CanOfferHidHideInventory(facts,decision,transaction is not null);
            return (decision,0);
        }

        string installer=Path.Combine(root,"PowerControl","redist","HidHide_1.5.230_x64.exe");
        result["hidhideInstallerPath"]=installer;
        result["hidhideInstallerExpectedSha256"]=HidHideSha;
        using var installerFile=File.Exists(installer) ? new FileStream(installer,FileMode.Open,FileAccess.Read,FileShare.Read) : null;
        string installerHash=installerFile is not null ? Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(installerFile)) : "";
        result["hidhideInstallerActualSha256"]=installerHash;
        var identity=HidHideInstallerIdentity(installerHash.Length!=0,installerHash==HidHideSha);
        result["hidhideInstallerIdentity"]=identity.Status;
        if (identity.ExitCode!=0) return (decision,identity.ExitCode);

        void SaveRepair(HidHideRepair row) {
            using var key=machine.CreateSubKey(HidHideRepairKey,writable:true);
            key.SetValue("State",JsonSerializer.Serialize(row),RegistryValueKind.String); key.Flush();
            result["hidhideTransactionAfter"]=row;
            if (row.ExitCode is not null) result["hidhideMaintenanceExit"]=row.ExitCode;
            save();
        }
        void Mark(string phase, int? code=null) => SaveRepair(new(phase,
            YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity ?? throw new InvalidOperationException("Windows boot session unavailable"),code));
        bool TryBeginRepair(string phase, string? package=null) {
            try {
                SaveRepair(new(phase,YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity ??
                    throw new InvalidOperationException("Windows boot session unavailable"),OrphanPackage:package));
                return true;
            }
            catch (IOException e) when ((e.HResult & 0xffff)==1021) { // ERROR_CHILD_MUST_BE_VOLATILE
                result["hidhideJournalBlockedByVolatileParent"]=true;
                return false;
            }
        }
        HidHideFacts Inspect(string suffix) {
            var now=Pnp("/enum-drivers","/format","xml");
            result["hidhideInventory"+suffix]=HidMaestroPackagePreparation.CaptureInventory(report,now.Output,now.Exit,now.TimedOut,".hidhide-"+suffix+".xml");
            if(now.Exit!=0 || now.TimedOut) throw new InvalidOperationException("HidHide inventory not verified: "+suffix);
            var state=ReadHidHideFacts(ParseDrivers(now.Output));
            result["hidhide"+suffix]=state;
            return state;
        }
        int Run(string executable, string[] args, string phase) {
            var start=new ProcessStartInfo(executable){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Path.GetDirectoryName(executable)!};
            foreach (string arg in args) start.ArgumentList.Add(arg);
            result[phase=="Installer" ? "hidhideInstallAttempted" : "hidhide"+phase+"Attempted"]=true; save();
            using var child=Process.Start(start) ?? throw new InvalidOperationException("HidHide "+phase+" did not start");
            // Windows Installer may still own the transaction. Observe the same child; never kill or retry it.
            if (!child.WaitForExit(120000)) { result[phase=="Installer" ? "installerSlow" : "hidhide"+phase+"Slow"]=true; save(); child.WaitForExit(); }
            result["hidhide"+phase+"Exit"]=child.ExitCode; save();
            return child.ExitCode;
        }
        bool CanMutate() => HidHideBlockingClients().Length==0;
        string system=Environment.GetFolderPath(Environment.SpecialFolder.System);
        var before=Inspect("Preflight");
        if (!CanMutate()) return (decision=="INSTALL_FRESH" ? "CLOSE_CONTROLLER_APPS_FIRST" : "UPGRADE_CLOSE_APPS",0);
        if (decision=="VERIFY_ORPHAN_BEFORE_REBOOT") {
            if (orphan is null || orphan!=SelectManualUninstallResidue(before)) return ("UNINSTALL_IDENTITY_UNVERIFIED",0);
            string proof=VerifyLegacyHidHideDisk(machine,system,orphan,HidHideLegacyServiceAction.VerifyDemandStart);
            result["hidhideOrphanPreRebootProof"]=proof;
            if (proof!="verified") return ("UNINSTALL_IDENTITY_UNVERIFIED",0);
            if (!CanMutate()) return ("UPGRADE_CLOSE_APPS",0);
            // Record the observed orphan, not an MSI uninstall result. No driver writes until a later boot.
            TryBeginRepair(HidHideOrphanRebootPhase,orphan);
            return ("REBOOT_REQUIRED",0);
        }
        if (decision=="UNINSTALL_LEGACY") {
            if (candidate is null || (installerUninstallOnly ? SelectInstallerHidHideUninstall(before) : SelectHidHideUninstall(before))!=candidate) return ("UNINSTALL_IDENTITY_UNVERIFIED",0);
            if (!CanMutate()) return ("UPGRADE_CLOSE_APPS",0);
            if (!TryBeginRepair("uninstalling")) return (installerUninstallOnly ? "UPGRADE_JOURNAL_UNAVAILABLE" : "REBOOT_REQUIRED",0);
            int code=Run(Path.Combine(system,"msiexec.exe"),HidHideUninstallArguments(candidate.Code,report+".hidhide-uninstall.log"),"Uninstall");
            // 1618 means no new MSI transaction ran. Leave startup free to retry once Windows Installer is idle.
            if (installerUninstallOnly && code==1618) {
                using var key=machine.OpenSubKey(HidHideRepairKey,writable:true)!;
                key.DeleteValue("State"); key.Flush();
                result["hidhideTransactionAfter"]=null;
                return ("MAINTENANCE_BUSY",0);
            }
            Mark(InstallerSuccess(code) ? "reboot-after-uninstall" : "uninstall-failed",code);
            // Keep the exit and the immediate state separate; neither proves post-reboot clearance.
            try { Inspect("AfterUninstall"); } catch (Exception e) { result["hidhideAfterUninstallError"]=e.Message; }
            return (InstallerSuccess(code) ? "REBOOT_REQUIRED" : "UNINSTALL_FAILED",0);
        }
        bool migration=decision!="INSTALL_FRESH";
        bool allowLegacyDisk=transaction?.Phase=="install-after-cleanup" && transaction.LegacyDiskTakeover;
        string LegacyDiskProof(string suffix, string? published, HidHideLegacyServiceAction action) {
            string proof=VerifyLegacyHidHideDisk(machine,system,published,action);
            result["hidhideLegacyResidue"+suffix]=proof;
            return proof;
        }
        if (decision=="RETIRE_INACTIVE_PACKAGE") {
            string? leftover=SelectInactiveHidHidePackage(before);
            if (leftover is null || leftover!=SelectInactiveHidHidePackage(facts)) return ("UNINSTALL_RESIDUE",0);
            bool orphanRecovery=transaction!.Phase==HidHideOrphanRebootPhase;
            if (orphanRecovery && !MatchesManualUninstallResidue(transaction,before)) return ("UNINSTALL_RESIDUE",0);
            allowLegacyDisk=HidHideLegacyDiskCandidate(before);
            if (allowLegacyDisk && LegacyDiskProof("BeforeCleanup",leftover,HidHideLegacyServiceAction.VerifyDemandStart)!="verified") return ("UNINSTALL_RESIDUE",0);
            if (!CanMutate()) return ("UPGRADE_CLOSE_APPS",0);
            Mark("cleaning");
            // Only an inactive, uniquely verified old package; PnPUtil must refuse in-use packages.
            int removed=Run(Path.Combine(system,"pnputil.exe"),["/delete-driver",leftover],"InactivePackageCleanup");
            if (removed!=0) { Mark("residue",removed); return ("UNINSTALL_RESIDUE",0); }
            before=Inspect("AfterCleanup");
            if (allowLegacyDisk && HidHideLegacyDiskCandidate(before) && before.Packages.Length==0 && !before.Repository) {
                if (LegacyDiskProof("ServiceCleanup",leftover,HidHideLegacyServiceAction.RetireDisabled)!="retired") {
                    Mark("residue"); return ("UNINSTALL_RESIDUE",0);
                }
                before=Inspect("AfterServiceCleanup");
            }
            if (!CanInstallAfterHidHideCleanup(before,allowLegacyDisk) ||
                (before.Evidence && LegacyDiskProof("AfterCleanup",leftover,HidHideLegacyServiceAction.VerifyAbsent)!="verified")) {
                Mark("residue"); return ("UNINSTALL_RESIDUE",0);
            }
            // Cleanup is complete even if a consumer blocks installation now. Never replay deletion.
            SaveRepair(transaction with { Phase="install-after-cleanup", LegacyDiskTakeover=allowLegacyDisk && before.Evidence,
                ExitCode=orphanRecovery ? removed : transaction.ExitCode });
        }
        if (!CanInstallAfterHidHideCleanup(before,allowLegacyDisk) ||
            (decision!="RETIRE_INACTIVE_PACKAGE" && before.Evidence &&
                LegacyDiskProof("BeforeInstall",null,HidHideLegacyServiceAction.VerifyAbsent)!="verified")) {
            return (migration ? "UNINSTALL_RESIDUE" : "STATE_CHANGED_BEFORE_INSTALL",0);
        }
        if (!CanMutate()) return (migration ? "UPGRADE_CLOSE_APPS" : "CLOSE_CONTROLLER_APPS_FIRST",0);
        if (migration) Mark("installing");
        int installCode=Run(installer,HidHideArguments(report+".hidhide.log"),"Installer");
        if (migration) {
            Mark(InstallerSuccess(installCode) ? "reboot-after-install" : "install-failed",installCode);
            return (InstallerSuccess(installCode) ? "REBOOT_REQUIRED" : "UPGRADE_INSTALL_FAILED",0);
        }
        var after=HidHideService();
        return (InstallerSuccess(installCode) && after.Present
            ? (NeedsReboot(installCode) || !after.Running ? "REBOOT_REQUIRED" : File.Exists(HidHideCli) ? "READY" : "PRESENT_NOT_READY_NO_REINSTALL")
            : "INSTALL_POSTCONDITION_UNPROVEN",0);
    }
    static (string Status, int ExitCode) SetupResult(string hidHide, int hidMaestroExit, bool prepare, bool usbipRebootPending = false)
    {
        // The reboot requirement survives a second dependency's failure. Details remain in the report.
        if (usbipRebootPending || hidHide == "REBOOT_REQUIRED") return ("REBOOT_REQUIRED", 3010);
        if (hidHide == "READY" && hidMaestroExit == 0) return ("READY", 0);
        if (hidHide == "READY" && hidMaestroExit == HidMaestroPackagePreparation.InventoryUnrecognizedExit)
            return ("DRIVER_INVENTORY_UNRECOGNIZED", HidMaestroPackagePreparation.InventoryUnrecognizedExit);
        if (prepare && hidHide == "READY" && hidMaestroExit == HidMaestroPackagePreparation.MaintenanceDeferredExit)
            return ("DRIVER_MAINTENANCE_DEFERRED", HidMaestroPackagePreparation.MaintenanceDeferredExit);
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
        YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();
        if (args.Length!=0 && args[0]=="--uninstall-legacy-hidhide") {
            if (args.Length!=5 || args[1]!="--install-root" || args[3]!="--report") return 2;
            return RunInstallerHidHideUninstall(Path.GetFullPath(args[2]),Path.GetFullPath(args[4]));
        }
        if (args.Length == 1 && args[0] == "--selftest-usbip-upgrade") return UsbipUpgradeSelfTest.Run();
        if (args.Length == 1 && args[0] == "--selftest-usbip-owned-wait-child") { Console.WriteLine("READY"); Console.ReadLine(); return 0; }
        if (args.Length == 1 && args[0] == "--selftest-usbip-owned-wait") return UsbipUpgradeSelfTest.WaitBudget();
        if (args.Length == 1 && args[0] == "--selftest-usbip-boot-id") {
            var boot = YeManCC.UsbipMaintenanceState.CurrentBootIdentifier;
            Console.WriteLine(JsonSerializer.Serialize(new { status = boot is null ? "UNKNOWN" : "PASS", bootIdentifier = boot?.ToString("D"), bootSequence = YeManCC.UsbipMaintenanceState.CurrentBootSequence, osReadOnly = true, driverIo = false }));
            return boot is null ? 1 : 0;
        }
        if (args.Length == 2 && args[0] == "--selftest-usbip-sdk-contract") {
            var c = UsbipUpgrade.WindowsRuntime.ReadSdkContract(args[1]);
            Console.WriteLine(JsonSerializer.Serialize(new { status = "PASS", targetVersion = c.Target.ToString(),
                driverHash = c.DriverHash, installerResource = c.Resource, hardwareIo = false, sdkMethodsInvoked = 0 }));
            return 0;
        }
        if (args.Length == 7 && args[0] == "--usbip-observe" && args[1] == "--install-root" &&
            args[3] == "--owner-pid" && int.TryParse(args[4], out int observerOwner) && observerOwner > 0 &&
            args[5] == "--setup-session" && System.Text.RegularExpressions.Regex.IsMatch(args[6], @"\A[0-9]+:[0-9]+\z")) {
            // Deliberately before all setup gates/bootstrap, cleanup and report IO.
            // Reuse this helper process, without installers, dialogs or another EXE.
            StartupOwnerPid = observerOwner;
            StartupRoot = Path.GetFullPath(args[2]);
            if (!ValidStartupOwner()) return 11;
            var observer = new UsbipUpgrade.WindowsRuntime(StartupRoot, "", args[6], observerOwner, false, ValidStartupOwner);
            var observed = UsbipUpgrade.Observe(observer);
            if (!ValidStartupOwner()) return 11;
            Console.WriteLine(JsonSerializer.Serialize(new { batch = "GP-USBIP-OBSERVE-1", ownerPid = observerOwner,
                installRoot = StartupRoot, session = args[6], usbipUpgrade = observed }));
            return 0;
        }
        if (args.Contains("--selftest") || args.Contains("--selftest-sdk-contract")) {
            if (args.Length == 1 && args[0] == "--selftest") { int first=SelfTest(); return first == 0 ? AutoSelfTest() : first; }
            return HidMaestroMain(args);
        }
        bool prepare = false, modeSet = false;
        string setupSession = Environment.ProcessId + ":" + Environment.TickCount64;
        string moduleCommitState = "";
        string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", ".."));
        string report = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC", "gamepad-driver-setup.json");
        for (int i=0;i<args.Length;i++) {
            if (args[i] is "--check" or "--prepare") {
                if (modeSet) return 2;
                modeSet=true; prepare=args[i]=="--prepare";
            } else if (args[i]=="--wait-update-commit" && i+1<args.Length) moduleCommitState=Path.GetFullPath(args[++i]);
            else if (args[i]=="--install-root" && i+1<args.Length) root=Path.GetFullPath(args[++i]);
            else if (args[i]=="--report" && i+1<args.Length) report=Path.GetFullPath(args[++i]);
            else if (args[i]=="--owner-pid" && i+1<args.Length && int.TryParse(args[++i],out int pid) && pid>0) StartupOwnerPid=pid;
            else if (args[i]=="--setup-session" && i+1<args.Length && System.Text.RegularExpressions.Regex.IsMatch(args[i+1], @"\A[0-9]+:[0-9]+\z")) setupSession=args[++i];
            else return 2;
        }
        StartupRoot=root;
        var result = new Dictionary<string,object?> { ["batch"]="GP-PREREQ-2", ["startedAt"]=DateTimeOffset.Now,
            ["prepare"]=prepare,["ownerPid"]=StartupOwnerPid,["installRoot"]=root,["hardwareVerified"]=false };
        int exit=11; string status="UNPROVEN"; bool held=false;
        YeManCC.ModuleMaintenanceGate? moduleLease = null;
        using var mutex=new Mutex(false,@"Global\YeManCC.GamepadPrerequisites.Install");
        void Save() { Directory.CreateDirectory(Path.GetDirectoryName(report)!); File.WriteAllText(report,JsonSerializer.Serialize(result,Json),new UTF8Encoding(false)); }
        try {
            // Check-only does not take the install mutex, clean up files, or run SDK constructors.
            if (prepare) {
                if (StartupOwnerPid!=0 && !ValidStartupOwner()) { status="INVALID_STARTUP_OWNER"; return exit; }
                ModulePayloadBootstrap.Apply(root, moduleCommitState, StartupOwnerPid, () => StartupOwnerPid == 0 || ValidStartupOwner());
                result["modulePayloadReady"]=true;
                moduleLease=YeManCC.ModuleMaintenanceGate.TryEnter();
                // The outgoing helper releases its lease just after its commit.
                // Only this authenticated post-update startup gets a short wait;
                // ordinary/manual concurrent maintenance still returns promptly.
                if (moduleLease is null && moduleCommitState.Length != 0) {
                    var moduleWait=Stopwatch.StartNew();
                    while (moduleLease is null && moduleWait.Elapsed < TimeSpan.FromSeconds(15) && (StartupOwnerPid == 0 || ValidStartupOwner())) {
                        Thread.Sleep(100); moduleLease=YeManCC.ModuleMaintenanceGate.TryEnter();
                    }
                }
                if (moduleLease is null) { status="DRIVER_MAINTENANCE_DEFERRED"; result["hidmaestro"]=status; result["guidance"]="YMCC 或模块正在更新，请等待完成后重试。"; exit=19; return exit; }
                try { held=mutex.WaitOne(0); } catch (AbandonedMutexException) { held=true; status="PREVIOUS_INSTALL_INTERRUPTED"; return exit; }
                if (!held) { status="SETUP_ALREADY_RUNNING"; return exit; }
                if (StartupOwnerPid!=0 && !ValidStartupOwner()) { status="INVALID_STARTUP_OWNER"; return exit; }
                try { result["legacyCleanup"]=CleanupLegacy(root); } catch(Exception e) { result["legacyCleanup"]="PRESERVED: "+e.Message; }
            }
            // Preserve the previous summary as a bounded diagnostic, not as an installation trigger.
            if (File.Exists(report)) File.Copy(report, report + ".previous", overwrite:true);
            Save();
            bool admin=new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
            // Isolated one-shot version upgrade, before controller admission.
            // Same/newer/unknown installs never enter the uninstall terminal.
            var usbipRuntime = new UsbipUpgrade.WindowsRuntime(root, report, setupSession, StartupOwnerPid, admin,
                () => StartupOwnerPid == 0 || ValidStartupOwner());
            usbipRuntime.UninstallEvidenceChanged = evidence => {
                result["usbipUninstallEvidence"] = evidence;
                Save();
            };
            using (var usbipGate = new Mutex(false, UsbipUpgrade.GateName)) {
                bool gateHeld = false, abandoned = false;
                try {
                    try { gateHeld = usbipGate.WaitOne(0); }
                    catch (AbandonedMutexException) {
                        gateHeld = true; abandoned = true;
                        usbipRuntime.Publish(new(1, "unverified", "", "", "上次 USB/IP 操作中断，已保留现场。请重启 Windows 后检查。", 1));
                    }
                    if (gateHeld && !abandoned) {
                        result["usbipMaintenanceEvidenceBefore"] = YeManCC.UsbipMaintenanceState.DiagnosticEvidence();
                        Save();
                        usbipRuntime.RetireConfirmedPreviousBootFence(prepare);
                        result["usbipUpgrade"] = UsbipUpgrade.Execute(usbipRuntime, prepare);
                        result["usbipUninstallEvidence"] = usbipRuntime.UninstallEvidence;
                        result["usbipMaintenanceEvidenceAfter"] = YeManCC.UsbipMaintenanceState.DiagnosticEvidence();
                        Save();
                    }
                    else if (!gateHeld) usbipRuntime.Publish(new(1, "busy", "", "", "USB/IP 正在由其他会话处理，请等待后重新打开 YMCC。", 1));
                }
                finally { if (gateHeld) usbipGate.ReleaseMutex(); }
            }
            // Preserve the finite stage history in the ordinary exported report;
            // fast SDK callbacks may finish before native reads the latest file.
            result["usbipLifecycle"] = usbipRuntime.LifecycleEvidence;
            Save();
            var inventoryStarted=Stopwatch.GetTimestamp();
            var inventory=Pnp("/enum-drivers","/format","xml");
            result["driverInventoryEvidence"]=HidMaestroPackagePreparation.CaptureInventory(report,inventory.Output,inventory.Exit,inventory.TimedOut);
            Save();
            if (inventory.Exit!=0 || inventory.TimedOut) { status="ENUMERATION_UNPROVEN"; return exit; }
            var drivers=ParseDrivers(inventory.Output);
            var hidHide=ReadHidHideFacts(drivers);
            result["hidhideBefore"]=new {hidHide.Present,hidHide.Running,package=hidHide.Packages.Length!=0,driverFile=hidHide.DriverFile,
                evidence=hidHide.Evidence,driverFileVersion=hidHide.DriverVersion?.ToString(),minimumDriverFileVersion=MinimumHidHideDriver.ToString(),
                expectedCliPath=HidHideCli,expectedCliPresent=hidHide.Cli,hidHide.Filters,hidHide.Repository,hidHide.Products,hidHide.Packages};
            var hidHideResult=PrepareHidHide(hidHide,prepare,admin,usbipRuntime.RebootPending,root,report,result,Save,
                out bool canOfferInitialInventory);
            string hh=hidHideResult.State;
            result["hidhide"]=hh; Save();
            if (hidHideResult.Exit!=0) {
                exit=hidHideResult.Exit;
                status=exit==ExitHidHideInstallerMissing ? "HIDHIDE_INSTALLER_MISSING" : "HIDHIDE_INSTALLER_HASH_MISMATCH";
                return exit;
            }
            result["hidhideRebootRequired"]=hh=="REBOOT_REQUIRED";
            if (hh=="REBOOT_REQUIRED") { status="REBOOT_REQUIRED"; exit=3010; return exit; }
            if (hh.StartsWith("UNINSTALL_",StringComparison.Ordinal) || hh.StartsWith("UPGRADE_",StringComparison.Ordinal)) {
                status=hh;
                return exit;
            }
            string hmReport=report+".hidmaestro.json";
            if (File.Exists(hmReport)) File.Move(hmReport,hmReport+".previous",true);
            bool ordinaryStartup=prepare && moduleCommitState.Length==0 && StartupOwnerPid>0 && ValidStartupOwner();
            var initialInventory=ordinaryStartup && canOfferInitialInventory && !usbipRuntime.RebootPending
                ? new StartupInventory(inventory,drivers,inventoryStarted) : null;
            int hm=HidMaestroMain([prepare ? "--prepare" : "--check","--install-root",root,"--report",hmReport],
                ordinaryStartup,initialInventory);
            result["hidmaestroExit"]=hm;
            using(var detail=JsonDocument.Parse(File.ReadAllText(hmReport))) result["hidmaestro"]=detail.RootElement.GetProperty("status").GetString();
            var final=SetupResult(hh,hm,prepare,usbipRuntime.RebootPending);
            status=final.Status; exit=final.ExitCode;
            // The USB/IP uninstaller independently requires reboot; do not
            // publish overall READY while its machine-wide fence is active.
            if (result.TryGetValue("usbipUpgrade", out var usbipResult) && usbipResult is UsbipUpgrade.Snapshot usbip && usbip.phase == "reboot-required") {
                status="REBOOT_REQUIRED";exit=3010;
            }
            return exit;
        } catch(HidMaestroPackagePreparation.InventorySchemaUnrecognizedException e) {
            result["error"]=e.ToString(); result["inventoryRoot"]=e.ObservedRoot;
            result["automaticMaintenanceAttempted"]=false;
            status="DRIVER_INVENTORY_UNRECOGNIZED"; exit=HidMaestroPackagePreparation.InventoryUnrecognizedExit; return exit;
        } catch(Exception e) { result["error"]=e.ToString(); if (exit!=3010) status="ERROR_UNPROVEN"; return exit; }
        finally {
            result["status"]=status;result["exitCode"]=exit;result["finishedAt"]=DateTimeOffset.Now;
            try { Save(); } finally { try { if(held) mutex.ReleaseMutex(); } finally { moduleLease?.Dispose(); } }
            Console.WriteLine(status);Console.WriteLine(report);
        }
    }
    static int AutoSelfTest()
    {
        int n=0;void Check(bool ok,string label){if(!ok)throw new Exception(label);n++;}
        Check(HidHideDecision(true,true,true,true,true,true)=="READY","healthy has no mutation even with clients");
        Check(HidHideDecision(true,true,true,true,true,false,legacyDriver:true)=="DRIVER_TOO_OLD_NO_REINSTALL","installed old version is not ready and is not reinstalled");
        Check(HidHideDecision(true,true,true,true,true,false,cliPresent:false)=="PRESENT_NOT_READY_NO_REINSTALL","missing CLI cannot be ready or treated as a clean install");
        Check(HidHideDecision(false,false,false,true,true,false,cliPresent:false)=="INSTALL_FRESH","clean install still permitted when no CLI exists");
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
        const string oldBoot="windows-volatile-v2|{35B74384-719A-46C2-8359-4EA8A5C2C27E}";
        const string nextBoot="windows-volatile-v2|{17255D57-AB0A-4135-B484-28A2D40B6890}";
        var oldPackage=new HidMaestroPackagePreparation.Driver("oem78.inf","hidhide.inf",HidHideVendor,"08/05/2021 1.1.50.0");
        var oldProduct=new HidHideProduct("{AC3049BB-6FD8-4FE6-B153-842AAEDCC720}",HidHideVendor+" HidHide (x64)",HidHideVendor,"1.1.50",true);
        var oldFacts=new HidHideFacts(true,false,true,new Version(1,1,50,0),false,true,["class:UpperFilters"],[oldPackage],[oldProduct]);
        var clean=new HidHideFacts(false,false,false,null,false,false,[],[],[]);
        var current=new HidHideFacts(true,true,true,MinimumHidHideDriver,true,true,["class:UpperFilters"],
            [oldPackage with {RawVersion="11/18/2024 1.4.181.0"}],[oldProduct with { Name="HidHide",Version="1.5.230" }]);
        Check(SelectHidHideUninstall(oldFacts)==oldProduct,"old service + old official MSI + missing modern CLI enters uninstall");
        Check(SelectInstallerHidHideUninstall(oldFacts)==oldProduct,"setup selects exactly the legacy MSI with matching SYS and INF");
        Check(SelectInstallerHidHideUninstall(oldFacts with {Products=[oldProduct with {Version="1.1.50.0"}]}) is not null,"MSI/display version accepts the zero revision");
        Check(SelectInstallerHidHideUninstall(oldFacts with {Products=[oldProduct with {Version="1.1.51"}]}) is null,"setup does not remove other legacy MSI versions");
        Check(SelectInstallerHidHideUninstall(oldFacts with {DriverVersion=null}) is null,"setup does not remove an incomplete or unproved driver");
        Check(SelectInstallerHidHideUninstall(oldFacts with {Packages=[]}) is null,"setup requires a matching driver store package");
        Check(SelectInstallerHidHideUninstall(oldFacts with {Packages=[oldPackage,oldPackage with {RawVersion="11/18/2024 1.4.181.0"}]}) is null,"setup preserves mixed driver versions");
        Check(SelectInstallerHidHideUninstall(oldFacts with {Products=[oldProduct with {MachineMsi=false}]}) is null,"setup never runs an unverified uninstall entry");
        Check(SelectHidHideUninstall(oldFacts with {DriverVersion=null})==oldProduct,"matching legacy store and MSI allow detection without a disk version");
        Check(SelectHidHideUninstall(oldFacts with {Products=[]}) is null,"field remnants without a registered uninstaller cannot execute");
        Check(SelectHidHideUninstall(oldFacts with {Products=[oldProduct with {MachineMsi=false}]}) is null,"unverified uninstall command cannot execute");
        Check(SelectHidHideUninstall(oldFacts with {Products=[oldProduct,oldProduct with {Version="1.5.230"}]}) is null,"multiple products never auto-uninstall");
        Check(SelectHidHideUninstall(oldFacts with {Packages=[oldPackage with {Provider="other"}]}) is null,"foreign INF is not an old official INF");
        Check(SelectHidHideUninstall(oldFacts with {Products=[oldProduct with {Version="unknown"}]}) is null,"unknown product version is preserved");
        Check(SelectHidHideUninstall(oldFacts with {DriverVersion=MinimumHidHideDriver}) is null && SelectHidHideUninstall(current) is null,"same and newer drivers remain untouched");
        Check(HidHideUninstallArguments(oldProduct.Code,@"C:\user data\uninstall.log").SequenceEqual(new[]{"/x",oldProduct.Code,"/qn","/norestart","/L*v",@"C:\user data\uninstall.log"}),"MSI removal uses the product code only, without reboot or arbitrary commands");
        foreach(int code in new[]{0,3010,1641}) {
            var state=new HidHideRepair("reboot-after-uninstall",oldBoot,code);
            Check(HidHideRepairDecision(state,oldBoot,clean)=="REBOOT_REQUIRED","successful MSI exit never installs on the same boot");
            Check(HidHideRepairDecision(state,null,clean)=="REBOOT_REQUIRED","unknown boot never proves reboot");
            Check(HidHideRepairDecision(state,nextBoot,oldFacts)=="UNINSTALL_RESIDUE","successful MSI exit with service still present does not install");
            Check(HidHideRepairDecision(state,nextBoot,clean)=="INSTALL_AFTER_UNINSTALL","verified clean state on next boot installs");
            var residue=clean with {Packages=[oldPackage],Repository=true};
            Check(HidHideRepairDecision(state,nextBoot,residue)=="RETIRE_INACTIVE_PACKAGE","only inactive verified staged legacy INF is eligible for non-forced removal");
            Check(HidHideRepairDecision(state,nextBoot,residue with {Filters=["class:UpperFilters"]})=="UNINSTALL_RESIDUE","class filter residue blocks automatic package retirement");
            Check(HidHideRepairDecision(state,nextBoot,residue with {Packages=[oldPackage with {Published="not-oem.inf"}]})=="UNINSTALL_RESIDUE","only the observed oem leaf can reach PnPUtil");
        }
        var stopped=oldFacts with { Stopped=true, Products=[], Filters=[] };
        var uninstall=new HidHideRepair("reboot-after-uninstall",oldBoot,0);
        Check(HidHideRepairDecision(uninstall,nextBoot,stopped)=="RETIRE_INACTIVE_PACKAGE","exact stopped legacy pair requires additional identity and binding preflight");
        Check(HidHideRepairDecision(uninstall,nextBoot,stopped with { Stopped=false })=="UNINSTALL_RESIDUE","pending or unknown service state cannot retire package");
        Check(HidHideRepairDecision(uninstall,nextBoot,stopped with { Running=true })=="UNINSTALL_RESIDUE","running service cannot retire package");
        Check(HidHideRepairDecision(uninstall with { ExitCode=1603 },nextBoot,stopped)=="UPGRADE_INTERRUPTED","no retirement on failed MSI receipt");
        Check(HidHideRepairDecision(uninstall,nextBoot,stopped with { DriverVersion=new Version(1,1,51,0) })=="UNINSTALL_RESIDUE","unknown residual SYS version does not retire");
        var orphanWitness=new HidHideRepair(HidHideOrphanRebootPhase,oldBoot,OrphanPackage:oldPackage.Published);
        Check(SelectManualUninstallResidue(stopped)==oldPackage.Published && SelectHidHideUninstall(stopped) is null,
            "observed manual-uninstall residue is not an MSI uninstall candidate");
        Check(ValidHidHideOrphanWitness(orphanWitness) && !ValidHidHideOrphanWitness(orphanWitness with { ExitCode=0 }) &&
            !ValidHidHideOrphanWitness(orphanWitness with { LegacyDiskTakeover=true }) &&
            !ValidHidHideOrphanWitness(orphanWitness with { OrphanPackage="../oem78.inf" }),"orphan witness cannot carry an invented MSI receipt or unsafe path");
        Check(HidHideRepairDecision(orphanWitness,oldBoot,stopped)=="REBOOT_REQUIRED" &&
            HidHideRepairDecision(orphanWitness,null,stopped)=="REBOOT_REQUIRED","manual orphan never retires in the initial or unknown boot");
        Check(HidHideRepairDecision(orphanWitness,nextBoot,stopped)=="RETIRE_INACTIVE_PACKAGE" &&
            HidHideRepairDecision(orphanWitness,nextBoot,clean)=="INSTALL_AFTER_UNINSTALL","new boot revalidates the same orphan or genuine absence");
        Check(HidHideRepairDecision(orphanWitness,nextBoot,current)=="CLEAR_REPAIR","external completion keeps a healthy new HidHide");
        foreach(var rejected in new[] {
            stopped with { Packages=[oldPackage with { Published="oem88.inf" }] },
            stopped with { Products=[oldProduct] }, stopped with { Running=true }, stopped with { Stopped=false },
            stopped with { Cli=true }, stopped with { Repository=false }, stopped with { Filters=["class:UpperFilters"] },
            stopped with { Packages=[oldPackage,oldPackage with { Published="oem88.inf" }] },
            stopped with { Packages=[oldPackage with { RawVersion="08/05/2021 1.1.49.0" }] },
            stopped with { DriverVersion=new Version(1,1,51,0) }
        }) Check(HidHideRepairDecision(orphanWitness,nextBoot,rejected)=="UNINSTALL_RESIDUE","changed or still owned residual identity never resumes");
        var cleanup=uninstall with { Phase="install-after-cleanup", LegacyDiskTakeover=true };
        var disk=stopped with { Present=false, Stopped=false, Packages=[], Repository=false };
        var orphanCleanup=orphanWitness with { Phase="install-after-cleanup", ExitCode=0, LegacyDiskTakeover=true };
        Check(ValidHidHideOrphanWitness(orphanCleanup) && HidHideRepairDecision(orphanCleanup,nextBoot,disk)=="INSTALL_AFTER_UNINSTALL",
            "successful PnP and service cleanup can resume installation without reinstalling or replaying cleanup");
        Check(SelectManualUninstallResidue(disk) is null && SelectManualUninstallResidue(clean) is null &&
            SelectManualUninstallResidue(current) is null,"orphan recovery does not accept file-only, fresh or current installations");
        Check(HidHideRepairDecision(uninstall,nextBoot,disk)=="UNINSTALL_RESIDUE","disk pair alone does not authorize installation");
        Check(HidHideRepairDecision(cleanup,nextBoot,disk)=="INSTALL_AFTER_UNINSTALL","committed cleanup resumes without re-deletion after a blocked install");
        Check(HidHideRepairDecision(cleanup with { LegacyDiskTakeover=false },nextBoot,disk)=="UNINSTALL_RESIDUE","INF-only cleanup cannot authorize takeover of a service");
        Check(HidHideRepairDecision(cleanup,nextBoot,disk with { Repository=true })=="UNINSTALL_RESIDUE","remaining repository blocks installation");
        Check(HidHideRepairDecision(cleanup,nextBoot,disk with { Present=true, Stopped=true })=="UNINSTALL_RESIDUE","legacy service must be retired before the installer can take over the disk file");
        Check(HidHideRepairDecision(new("reboot-after-install",oldBoot,0),nextBoot,current with { Running=false })=="UPGRADE_INSTALL_FAILED","no success with stopped replacement service");
        string DeviceXml(string values) => $"<PnpUtil Command=\"{HidHideDeviceInventoryCommand}\"><Device InstanceId=\"ROOT\\OTHER\\0\"><Status>Disconnected</Status>{values}</Device></PnpUtil>";
        Check(HidHideDeviceReferences(DeviceXml("<DriverName>oem18.inf</DriverName>"),"oem78.inf")=="clear","valid inventory without a HidHide reference");
        Check(HidHideDeviceReferences(DeviceXml("<DriverName>OEM78.INF</DriverName>"),"oem78.inf")=="device-reference","disconnected device still binds observed INF");
        Check(HidHideDeviceReferences(DeviceXml("<Stack><UpperFilter>HidHide</UpperFilter></Stack>"),null)=="device-reference","device stack filter blocks takeover");
        Check(HidHideDeviceReferences(DeviceXml(@"<HardwareIds><HardwareId>ROOT\HIDHIDE</HardwareId></HardwareIds>"),null)=="device-reference","root device blocks takeover");
        Check(HidHideDeviceReferences("<PnpUtil/>",null)=="device-inventory-unrecognized","missing device inventory fails closed");
        Check(HidHideRepairDecision(new("uninstall-failed",oldBoot,1603),nextBoot,clean)=="UNINSTALL_FAILED","failed uninstall never retries or installs");
        Check(HidHideRepairDecision(new("uninstalling",oldBoot),nextBoot,clean)=="UPGRADE_INTERRUPTED","interrupted operation never replays");
        Check(HidHideRepairDecision(new("reboot-after-install",oldBoot,0),oldBoot,current)=="REBOOT_REQUIRED","same-boot file readiness does not prove a loaded new filter");
        Check(HidHideRepairDecision(new("reboot-after-install",oldBoot,0),nextBoot,current)=="CLEAR_REPAIR","next boot confirms complete current install");
        Check(HidHideRepairDecision(new("reboot-after-install",oldBoot,0),nextBoot,current with {Cli=false})=="UPGRADE_INSTALL_FAILED","new version without modern CLI cannot clear repair");
        Check(HidHideRepairDecision(new("reboot-after-install",oldBoot,0),nextBoot,current with {Packages=[oldPackage with {RawVersion="unknown"}]})=="UPGRADE_INSTALL_FAILED","unknown INF never clears repair");
        Check(SetupResult("REBOOT_REQUIRED",0,true)==("REBOOT_REQUIRED",3010),"reboot after successful prepare");
        foreach(int hm in new[]{10,11,13,15})
            Check(SetupResult("REBOOT_REQUIRED",hm,true)==("REBOOT_REQUIRED",3010),"HidMaestro error does not mask required reboot");
        Check(SetupResult("READY",11,true)==("DEPENDENCY_NOT_READY",11),"no reboot inferred from an unrelated failure");
        Check(SetupResult("READY",0,true)==("READY",0),"both dependencies ready admit target");
        foreach (bool prepare in new[] { false, true }) {
            foreach (int hm in new[] { 0, 11, 18, 19 })
                Check(SetupResult("READY",hm,prepare,true)==("REBOOT_REQUIRED",3010),"active USB/IP install/uninstall fence survives every SDK failure/status and cannot publish Ready");
            Check(SetupResult("READY",0,prepare,false)==("READY",0),"absent or failed USB/IP without a mutation fence does not invalidate independent UMDF preparation");
        }
        Check(SetupResult("READY",18,true)==("DRIVER_INVENTORY_UNRECOGNIZED",18),"unrecognized inventory stays advisory and is not Ready");
        Check(SetupResult("REBOOT_REQUIRED",18,true)==("REBOOT_REQUIRED",3010),"reboot requirement still overrides advisory inspection");
        Check(SetupResult("PRESENT_NOT_READY_NO_REINSTALL",18,true)==("DEPENDENCY_NOT_READY",11),"known dependency fault is not hidden by advisory inspection");
        Check(SetupResult("MISSING",0,false)==("MISSING_DEPENDENCY",10),"check-only remains non-installing");
        var runningWithoutCli=clean with {Present=true,Running=true,DriverFile=true,DriverVersion=MinimumHidHideDriver};
        const string partial="PRESENT_NOT_READY_NO_REINSTALL";
        Check(CanOfferHidHideInventory(current,"READY",false) &&
            !CanOfferHidHideInventory(current,"READY",true),"healthy reuse still requires no repair transaction");
        Check(CanOfferHidHideInventory(runningWithoutCli,partial,false) &&
            SetupResult(partial,0,true)==("DEPENDENCY_NOT_READY",11),"running modern driver without CLI can seed inventory but stays unready");
        Check(!CanOfferHidHideInventory(runningWithoutCli,partial,true),"active repair blocks partial inventory reuse");
        Check(!CanOfferHidHideInventory(runningWithoutCli with {Running=false},partial,false) &&
            !CanOfferHidHideInventory(runningWithoutCli with {DriverFile=false},partial,false) &&
            !CanOfferHidHideInventory(runningWithoutCli with {DriverVersion=null},partial,false),"unproved running driver cannot seed partial inventory");
        Check(!CanOfferHidHideInventory(runningWithoutCli with {DriverVersion=new Version(1,1,50)},partial,false) &&
            !CanOfferHidHideInventory(runningWithoutCli with {Packages=[oldPackage]},partial,false),"old file or package cannot seed partial inventory");
        Check(!CanOfferHidHideInventory(runningWithoutCli with {Cli=true},partial,false) &&
            !CanOfferHidHideInventory(runningWithoutCli,"UPGRADE_INTERRUPTED",false),"other unready paths take a fresh read");
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
