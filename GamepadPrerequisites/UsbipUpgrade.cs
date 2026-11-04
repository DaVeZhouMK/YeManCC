using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;

// Installation/version maintenance is a startup one-shot under GateName.
// Verified absence -> safety/absence -> journal -> stock SDK install -> verify.
// Legacy product -> official uninstall -> confirmed Windows reboot -> the same
// install endpoint. Status reads never mutate; unknown/same/newer installs are
// preserved. The attempt/pending records prevent same-boot mutation loops.
internal static class UsbipUpgrade
{
    internal const string GateName = @"Global\YeManCC.UsbipVersionMaintenance";
    internal const string AppKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1";
    internal sealed record Snapshot(int schemaVersion, string phase, string installedVersion,
        string targetVersion, string detail, int sequence, bool udeDiskMatchConfirmed = false);
    internal sealed record Inventory(Version Target, Version? Installed, bool Present, bool Verified,
        bool DriverCurrent, string Reason = "");
    internal interface Runtime
    {
        Inventory Inspect();
        bool Administrator { get; }
        bool OwnerAlive { get; }
        bool RestartRequired { get; }
        bool RebootPending { get; }
        bool SafeToChange(bool existingTransport, out string reason);
        bool ShouldResumeInstall(Inventory inventory);
        void ConfirmCurrent(Inventory inventory);
        bool AlreadyAttempted(Inventory inventory);
        void RecordAttempt(Inventory inventory);
        int Uninstall();
        bool Removed();
        void Install(Action<string> progress);
        bool Verify(Inventory inventory);
        void Publish(Snapshot snapshot);
    }
    // Current refers to the service-selected UDE file on disk, not the product
    // label, filter, loaded kernel image or actual controller consumption.
    private static string CurrentDetail(Inventory inventory) => inventory.Installed == inventory.Target
        ? "USB/IP 产品版本与 UDE 驱动文件均匹配 HIDM 配套；这不代表内核已加载该文件或虚拟手柄已经可用。"
        : $"USB/IP 的 UDE 驱动文件已匹配 HIDM 配套 {inventory.Target}，系统应用仍登记 {inventory.Installed}。已保留旧程序，未执行卸载；这不证明内核已加载该文件或虚拟手柄可用。";
    // Live status is a separate read-only operation. It cannot acknowledge or
    // repeat a previous maintenance attempt, and never invokes the SDK backend.
    internal static Snapshot Observe(Runtime runtime)
    {
        Inventory? inventory = null;
        Snapshot State(string phase, string detail) => new(1, phase,
            inventory?.Installed?.ToString() ?? "", inventory?.Target.ToString() ?? "", detail, 0, inventory?.DriverCurrent == true);
        try {
            inventory = runtime.Inspect();
            if (runtime.RebootPending) return State("reboot-required", "USB/IP 维护仍要求重启 Windows，当前读取未解除维护限制。");
            if (!inventory.Verified) return State("unverified", inventory.Reason);
            if (!inventory.Present) return State("not-installed", "当前未发现完整 USB/IP 安装；本次只读检查未安装驱动。");
            if (inventory.Installed is null) return State("unverified", "当前安装版本尚未确认，已保留原安装。");
            if (inventory.Installed.CompareTo(inventory.Target) > 0) return State("newer-preserved", "已重新核对，保留当前较新版本，不降级。");
            if (inventory.DriverCurrent && inventory.Installed == inventory.Target) return State("current", CurrentDetail(inventory));
            if (inventory.Installed == inventory.Target) return State("unverified", "产品版本与配套相同，但驱动文件尚未核验一致；未自动修复。");
            return State("update-found", inventory.DriverCurrent
                ? "UDE 驱动文件已匹配配套，但 USB/IP 产品仍为旧版，需要先卸载旧程序；本次只读检查未启动维护。"
                : "当前版本低于配套版本，已保留现有安装；未自动重试升级。");
        } catch (Exception ex) {
            var message = ex.GetBaseException().Message;
            return State("unverified", "当前 USB/IP 只读检查未完成：" + message[..Math.Min(message.Length, 240)]);
        }
    }
    internal static Snapshot Execute(Runtime runtime, bool prepare)
    {
        int sequence = 0;
        Inventory? inventory = null;
        Snapshot Send(string phase, string detail)
        {
            var value = new Snapshot(1, phase, inventory?.Installed?.ToString() ?? "",
                inventory?.Target.ToString() ?? "", detail, ++sequence, inventory?.DriverCurrent == true);
            runtime.Publish(value); return value;
        }
        Snapshot InstallAndVerify(bool requireRestart)
        {
            if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始 USB/IP 配套安装。");
            inventory = inventory! with { Installed = null, DriverCurrent = false };
            Send("installing", "正在由 HIDM 安装随包 USB/IP，请等待安装结束。");
            if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未调用 USB/IP 安装入口。");
            // Forward bounded SDK progress without allowing a diagnostic sink
            // failure to interrupt a system installer already in progress.
            object progressGate = new();
            int progressCount = 0;
            string lastProgress = "";
            bool acceptingProgress = true;
            try {
                runtime.Install(message => {
                    if (string.IsNullOrWhiteSpace(message)) return;
                    string detail = message.Trim();
                    detail = detail[..Math.Min(detail.Length, 240)];
                    lock (progressGate) {
                        if (!acceptingProgress || progressCount >= 16 || detail == lastProgress) return;
                        lastProgress = detail; ++progressCount;
                        try { Send("installing", "HIDM 安装进度：" + detail); } catch { }
                    }
                });
            } finally { lock (progressGate) acceptingProgress = false; }
            if (!runtime.OwnerAlive) return Send("failed", "HIDM 安装入口已返回，但 YMCC 已退出。下次启动会重新核对实际版本。");
            Send("verifying", "安装入口已返回，正在核对实际驱动版本与后端。");
            if (!runtime.Verify(inventory)) return Send("reboot-required", "尚未确认新驱动可用。请重启 Windows 后重新检查。");
            var target = inventory.Target;
            // Backend availability does not prove that Inno product registration
            // completed. Keep the actual inventory; never synthesize its version.
            inventory = runtime.Inspect();
            if (!inventory.Present || !inventory.Verified || inventory.Target != target ||
                inventory.Installed != target || !inventory.DriverCurrent)
                return Send(runtime.RebootPending ? "reboot-required" : "unverified",
                    "USB/IP 后端已通过检查，但产品登记或驱动文件尚未确认配套版本。未宣告安装完成，请重启 Windows 后检查或导出日志。");
            if (!runtime.OwnerAlive) return Send("failed", "安装检查已返回，但 YMCC 已退出。已保留维护记录，下次启动会重新核对。");
            runtime.ConfirmCurrent(inventory);
            return requireRestart
                ? Send("reboot-required", "USB/IP 配套版本已安装，原卸载流程要求重启，请先重启 Windows。")
                : Send("complete", "USB/IP 配套安装已通过驱动文件与后端检查；如虚拟手柄尚未开启，请重新启用所需模式。");
        }
        try
        {
            Send("checking", "正在核对当前 HIDM 配套的 USB/IP 版本。");
            inventory = runtime.Inspect();
            if (runtime.RebootPending) return Send("reboot-required", "上次 USB/IP 版本维护要求重启。请先重启 Windows，再启用虚拟手柄。");
            if (!inventory.Verified) return Send("unverified", inventory.Reason);
            if (!inventory.Present) {
                bool resume = runtime.ShouldResumeInstall(inventory);
                if (!prepare) return resume
                    ? Send("update-found", "已识别重启前的配套升级，检查模式未安装驱动。")
                    : Send("not-installed", "尚未安装 USB/IP；检查模式未安装驱动，启动准备时由 HIDM 安装随包版本。");
                if (!runtime.Administrator) return Send("admin-required", "请以管理员身份重新打开 YMCC，完成 USB/IP 配套安装。");
                if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始 USB/IP 配套安装。");
                if (!runtime.SafeToChange(false, out var installReason)) return Send("busy", installReason);
                if (runtime.AlreadyAttempted(inventory)) return Send("failed", "本次开机的配套安装未完成，已停止重复安装。请重启 Windows 后检查或导出日志。");
                if (!runtime.Removed()) return Send("unverified", "仍发现 USB/IP 驱动包或后端残留，未开始安装。请重启 Windows 后检查或导出日志。");
                if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始 USB/IP 配套安装。");
                runtime.RecordAttempt(inventory);
                return InstallAndVerify(false);
            }
            if (inventory.Installed is null) return Send("unverified", "无法确认已装版本，已保留原安装。请导出日志。");
            if (inventory.Installed > inventory.Target)
                return Send("newer-preserved", "已装版本高于 HIDM 配套版本，保留现有版本，不降级。");
            if (inventory.DriverCurrent && inventory.Installed == inventory.Target) {
                if (prepare) runtime.ConfirmCurrent(inventory);
                return Send("current", CurrentDetail(inventory));
            }
            // Registration alone cannot confirm a current driver. Preserve it,
            // consistently with the read-only observation; do not start repair.
            if (inventory.Installed == inventory.Target)
                return Send("unverified", "产品版本与配套相同，但驱动文件尚未核验一致；未自动修复。");
            Send("update-found", inventory.DriverCurrent
                ? "UDE 驱动文件已匹配配套，但 USB/IP 产品仍为旧版，准备核验并卸载旧程序。"
                : "发现较旧的 USB/IP，准备升级到当前 HIDM 的配套版本。");
            if (!prepare) return Send("update-found", "检查模式未修改设备；下次启动准备时处理配套升级。");
            if (!runtime.Administrator) return Send("admin-required", "请以管理员身份重新打开 YMCC，完成 USB/IP 配套升级。");
            if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始卸载。");
            if (!runtime.SafeToChange(true, out var reason)) return Send("busy", reason);
            if (runtime.AlreadyAttempted(inventory))
                return Send("failed", "上次配套升级未完成，已停止重复卸载。请重启 Windows 后检查 USB/IP 或导出日志。");
            // Persist before the first mutation: an interrupted operation must
            // not become an uninstall loop on the next application startup.
            runtime.RecordAttempt(inventory);
            if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始卸载。");
            Send("removing", "正在移除旧版 USB/IP，请稍候；可以继续使用 YMCC 的其他页面。");
            int code = runtime.Uninstall();
            // USBIP uses Inno uninstall semantics, not MSI exit semantics:
            // every nonzero exit is unconfirmed, including 3010 and 1641.
            if (code != 0) return Send("failed", "旧版卸载未确认成功，已停止安装。请重启 Windows 后重新检查，或导出日志。");
            if (runtime.RestartRequired)
                return Send("reboot-required", "旧版 USB/IP 已执行卸载，必须先重启 Windows；重启后会重新检查实际版本，再由 HIDM 安装需要的配套版本。");
            if (!runtime.Removed())
                return Send("reboot-required", "旧版驱动仍有登记或卸载残留。请重启 Windows 后重试，未强制删除驱动。");
            if (!runtime.OwnerAlive) return Send("failed", "旧版已移除，YMCC 已退出；下次启用时由 HIDM 安装配套版本。");
            return InstallAndVerify(false);
        }
        catch (Exception ex)
        {
            var value = new Snapshot(1, "failed", inventory?.Installed?.ToString() ?? "", inventory?.Target.ToString() ?? "",
                "USB/IP 安装或升级已停止：" + ex.GetBaseException().Message[..Math.Min(ex.GetBaseException().Message.Length, 240)] + "。请导出日志。", ++sequence);
            try { runtime.Publish(value); } catch { /* A broken status sink must not trap the maintenance owner. */ }
            return value;
        }
    }

    internal sealed class WindowsRuntime : Runtime
    {
        private readonly string root, report, session;
        private readonly int ownerPid;
        private readonly Func<bool> ownerAlive;
        private readonly bool admin;
        private Assembly? sdk;
        private string uninstaller = "", installedImage = "", targetDriverHash = "";
        private string coreHash = "", uninstallHash = "", installedClient = "", clientHash = "";
        private readonly string journal;
        private Inventory? plannedInventory;
        private readonly object lifecycleGate = new();
        private readonly List<object> lifecycle = new();
        public object[] LifecycleEvidence { get { lock (lifecycleGate) return lifecycle.ToArray(); } }
        private string PendingPath => YeManCC.UsbipMaintenanceState.PendingPath;
        internal WindowsRuntime(string root, string report, string session, int ownerPid, bool admin, Func<bool> ownerAlive)
        {
            this.root = root; this.report = report; this.session = session; this.ownerPid = ownerPid;
            this.admin = admin; this.ownerAlive = ownerAlive; journal = Path.Combine(YeManCC.UsbipMaintenanceState.DirectoryPath, "attempt.json");
        }
        public bool Administrator => admin;
        public bool OwnerAlive => ownerAlive();
        // The approved official Inno script's UninstallNeedRestart is true.
        public bool RestartRequired => true;
        public object? UninstallEvidence { get; private set; }
        public Action<object>? UninstallEvidenceChanged { get; set; }
        private void NoteUninstall(object evidence)
        {
            UninstallEvidence=evidence;
            // Evidence failures cannot cancel a driver transaction already
            // launched. Existing progress/state persistence remains separate.
            try { UninstallEvidenceChanged?.Invoke(evidence); } catch { }
        }
        internal void RetireConfirmedPreviousBootFence(bool prepare) => YeManCC.UsbipMaintenanceState.RetireConfirmedPreviousBoot(prepare);
        public bool RebootPending => YeManCC.UsbipMaintenanceState.Pending;
        private static string Sha(string file) { using var input = File.OpenRead(file); return Hash(input); }
        private static string Hash(Stream input) => Convert.ToHexString(SHA256.HashData(input));
        // Legacy images are the exact signed official bytes accepted by the
        // locked HIDM 2.0 source, not a guess from Windows DriverVer numbers.
        private static readonly Dictionary<string, string> Legacy = new(StringComparer.OrdinalIgnoreCase)
        {
            ["51db440065393e588a6b2585508c50eb3e1510b7b06d9afa6c5bde583751ea7d"] = "0.9.7.7",
            ["db9d6a97a043deab8e34122dcf429eaabb4af0d10279c8fea1248976a368c1d4"] = "0.9.7.5",
            ["2a969ba59b3ffe33227ca1de17759a189d085d51c4f4e0f34df0b244ee558ab8"] = "0.9.7.5",
        };
        // A target UDE may have been installed by HIDM's raw-driver migration
        // while the old Inno product/filter remains. That is still an old
        // product to remove, provided its product and executable identity pass.
        internal static bool LegacyProductMatchesDriver(Version installed, Version target, string driverHash, string targetHash)
        {
            if (installed >= target || !Legacy.Values.Any(v => Version.Parse(v) == installed)) return false;
            return driverHash.Equals(targetHash, StringComparison.OrdinalIgnoreCase) ||
                (Legacy.TryGetValue(driverHash, out var known) && Version.Parse(known) == installed);
        }
        public Inventory Inspect()
        {
            string file = Path.Combine(root, "PowerControl", "feature-assets", "virtual-gamepad", "HIDMaestro.Core.dll");
            // Resolve only the existing feature/shared runtime files. Metadata
            // reads need no dependency; later static SDK calls use this resolver.
            AssemblyLoadContext.Default.Resolving += (_, name) => {
                if (name.Name is null || name.Name.IndexOfAny([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar]) >= 0) return null;
                foreach (string directory in new[] { Path.GetDirectoryName(file)!, Path.Combine(root, "PowerControl", "ymcc-shared-runtime") }) {
                    string candidate = Path.Combine(directory, name.Name + ".dll");
                    if (File.Exists(candidate)) return AssemblyLoadContext.Default.LoadFromAssemblyPath(candidate);
                }
                return null;
            };
            var contract = ReadSdkContract(file);
            coreHash = HidMaestroPackagePreparation.CoreSha;
            sdk = contract.Assembly;
            var target = contract.Target;
            targetDriverHash = contract.DriverHash;
            using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var entry = hive.OpenSubKey(AppKey);
            using var service = hive.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\usbip2_ude");
            if (entry is null && service is null) return new(target, null, false, true, false);
            if (entry is null || service is null) return new(target, null, true, false, false, "USB/IP 安装登记不完整，未自动卸载。请检查系统应用或导出日志。");
            if (!Version.TryParse(entry.GetValue("DisplayVersion") as string, out var installed))
                return new(target, null, true, false, false, "无法确认 USB/IP 产品版本，保留原安装。");
            if (installed > target) return new(target, installed, true, true, false);
            string image = service.GetValue("ImagePath") as string ?? "";
            installedImage = ResolveImage(image);
            if (!File.Exists(installedImage)) return new(target, installed, true, false, false, "无法核验当前驱动文件，未自动卸载。");
            string hash = Sha(installedImage);
            bool driverCurrent = hash.Equals(targetDriverHash, StringComparison.OrdinalIgnoreCase);
            if (installed == target) return new(target, installed, true, true, driverCurrent);
            if (!LegacyProductMatchesDriver(installed, target, hash, targetDriverHash))
                return new(target, installed, true, false, false, "当前驱动不属于已核验的 HIDM 旧版，保留原安装。请导出日志。");
            string command = entry.GetValue("UninstallString") as string ?? "";
            uninstaller = UninstallerPath(command);
            string location = entry.GetValue("InstallLocation") as string ?? "";
            if (!TrustedUninstaller(uninstaller) || !Path.IsPathFullyQualified(location) ||
                !Path.GetFullPath(Path.GetDirectoryName(uninstaller)!).Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(location)), StringComparison.OrdinalIgnoreCase))
                return new(target, installed, true, false, false, "无法核验 USB/IP 官方卸载程序，未执行卸载。");
            string client = Path.Combine(location, "usbip.exe");
            if (!File.Exists(client) || !Version.TryParse(FileVersionInfo.GetVersionInfo(client).FileVersion, out var clientVersion) || clientVersion != installed)
                return new(target, installed, true, false, false, "USB/IP 产品登记与程序版本不一致，保留原安装。");
            installedClient = client;
            clientHash = Sha(client);
            uninstallHash = Sha(uninstaller);
            return new(target, installed, true, true, driverCurrent);
        }
        internal sealed record SdkContract(Assembly Assembly, Version Target, string DriverHash, string Resource);
        internal static SdkContract ReadSdkContract(string file)
        {
            if (!File.Exists(file) || new FileInfo(file).Length != HidMaestroPackagePreparation.CoreBytes ||
                !Sha(file).Equals(HidMaestroPackagePreparation.CoreSha, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("HIDM 源文件身份不匹配");
            var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.GetFullPath(file));
            if (assembly.ManifestModule.ModuleVersionId.ToString() != HidMaestroPackagePreparation.CoreMvid)
                throw new InvalidDataException("HIDM MVID 不匹配");
            // Literal metadata and resource bytes only: no SDK method or type
            // initializer is executed while discovering the bundled version.
            var type = assembly.GetType("HIDMaestro.Internal.Usbip.UsbipDriverInstaller", true)!;
            string targetText = (string)type.GetField("Version", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)!.GetRawConstantValue()!;
            var target = Version.Parse(targetText);
            string arch = System.Runtime.InteropServices.RuntimeInformation.OSArchitecture == System.Runtime.InteropServices.Architecture.Arm64 ? "arm64" : "x64";
            string suffix = arch == "arm64" ? "-arm64-release-attestation.exe" : "-x64.exe";
            string resource = "HIDMaestro.Resources.USBip-" + targetText + suffix;
            if (!assembly.GetManifestResourceNames().Contains(resource)) throw new InvalidDataException("缺少随包 USB/IP 安装器");
            using var stream = assembly.GetManifestResourceStream("HIDMaestro.Usbip." + arch + ".usbip2_ude.sys") ?? throw new InvalidDataException("缺少随包 USB/IP 驱动资源");
            if (type.GetMethod("HostControllerUsedSinceBoot", BindingFlags.Static | BindingFlags.NonPublic) is null ||
                assembly.GetType("HIDMaestro.Internal.Usbip.VhciClient", true)!.GetMethod("Probe", BindingFlags.Static | BindingFlags.NonPublic) is null ||
                assembly.GetType("HIDMaestro.Internal.AdminNamespace", true)!.GetMethod("Name", BindingFlags.Static | BindingFlags.NonPublic) is null)
                throw new InvalidDataException("HIDM 版本维护接口不匹配");
            return new(assembly, target, Hash(stream), resource);
        }
        internal static string ResolveImage(string image)
        {
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            if (image.StartsWith(@"\SystemRoot\", StringComparison.OrdinalIgnoreCase)) return Path.Combine(windows, image[12..]);
            if (image.StartsWith(@"\??\", StringComparison.Ordinal)) return image[4..];
            if (image.StartsWith(@"System32\", StringComparison.OrdinalIgnoreCase)) return Path.Combine(windows, image);
            return image;
        }
        internal static string UninstallerPath(string command)
        {
            command = command.Trim();
            if (command.Length < 3 || command[0] != '"') return "";
            int end = command.IndexOf('"', 1);
            return end > 1 && command[(end + 1)..].Trim().Length == 0 ? command[1..end] : "";
        }
        private static bool TrustedUninstaller(string file)
        {
            if (!Path.IsPathFullyQualified(file) || !File.Exists(file) || !System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(file), @"\Aunins[0-9]{3}\.exe\z", System.Text.RegularExpressions.RegexOptions.IgnoreCase)) return false;
            string full = Path.GetFullPath(file);
            var folders = new[] { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) };
            if (!folders.Any(p => p.Length > 0 && full.StartsWith(Path.TrimEndingDirectorySeparator(p) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) return false;
            for (var p = new DirectoryInfo(Path.GetDirectoryName(full)!); p is not null; p = p.Parent)
                if ((p.Attributes & FileAttributes.ReparsePoint) != 0) return false;
            return (File.GetAttributes(full) & FileAttributes.ReparsePoint) == 0;
        }
        public bool SafeToChange(bool existingTransport, out string reason)
        {
            reason = "请关闭虚拟手柄及其他使用 USB/IP 的程序，再重新打开 YMCC 完成升级。";
            if (RebootPending) { reason = "上次 USB/IP 维护后需要重启，请先重启 Windows。"; return false; }
            if (YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity is null) { reason = "无法核验本次 Windows 启动身份，未开始卸载 USB/IP。请导出日志。"; return false; }
            if (Environment.GetEnvironmentVariable("HIDMAESTRO_KEEP_TRANSPORT") == "1") { reason = "环境要求保留现有 USB/IP，已跳过升级。"; return false; }
            string[] names = ["YeManInputHost", "HandheldCompanion", "HIDMaestro", "DS4Windows", "viiper-windows-amd64", "viiper-windows-arm64", "usbip"];
            foreach (string name in names)
            foreach (var process in Process.GetProcessesByName(name)) { using (process) { if (!process.HasExited) return false; } }
            if (!existingTransport) return true; // Inspect has proved the product and service are absent.
            // Preserve the exact approved SDK's conservative since-boot gate.
            // A successful empty best-effort GetImportedDevices is insufficient:
            // that convenience method swallows IOCTL failures.
            var installer = sdk!.GetType("HIDMaestro.Internal.Usbip.UsbipDriverInstaller", true)!;
            object?[] why = [null];
            var used = installer.GetMethod("HostControllerUsedSinceBoot", BindingFlags.Static | BindingFlags.NonPublic) ?? throw new MissingMethodException("HostControllerUsedSinceBoot");
            if (used.Invoke(null, why) is not false) {
                reason = "本次开机已使用过 USB/IP 或状态无法确认。请先重启 Windows，保持虚拟手柄关闭，再打开 YMCC 升级。";
                return false;
            }
            // Execute the original throwing probe against a scoped read-only
            // VHCI handle. Zero rows must be a successful exact four-byte result.
            var type = sdk.GetType("HIDMaestro.Internal.Usbip.VhciClient", true)!;
            object handle = type.GetMethod("Open", BindingFlags.Static | BindingFlags.NonPublic)!.Invoke(null, null)!;
            using ((IDisposable)handle) {
                var value = (IntPtr)handle.GetType().GetProperty("Handle")!.GetValue(handle)!;
                object?[] probe = [value, null, (uint)0];
                type.GetMethod("Probe", BindingFlags.Static | BindingFlags.NonPublic)!.Invoke(null, probe);
                if (probe[1] is not byte[] bytes || probe[2] is not uint count || count != 4 || bytes.Length < count) return false;
            }
            return true;
        }
        public bool ShouldResumeInstall(Inventory inventory) => PreviousBootMatches(PendingPath + ".previous-boot", coreHash, inventory.Target,
            YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
        internal static bool PreviousBootMatches(string previous, string core, Version target, Guid? boot, uint? sequence, string? bootSession)
        {
            var saved = YeManCC.UsbipMaintenanceState.ReadRecord(previous, pending: true);
            if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Missing) return false;
            if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Unavailable) throw new IOException("USB/IP 前次维护记录不可读，未开始安装");
            if (saved.Kind != YeManCC.UsbipMaintenanceState.RecordKind.Ready) throw new InvalidDataException("USB/IP 前次维护记录未知或不支持，未开始安装");
            return ContinuationMatches(saved.Value, core, target, boot, sequence, bootSession);
        }
        public void ConfirmCurrent(Inventory inventory)
        {
            // Only a complete, freshly inspected product/UDE pair may retire
            // this session's install fence. A backend-only result is insufficient.
            if (!inventory.Present || !inventory.Verified || inventory.Installed != inventory.Target || !inventory.DriverCurrent)
                throw new InvalidDataException("USB/IP 完整安装身份尚未确认");
            CompleteOwnedInstallRecord(PendingPath, coreHash, inventory.Target, session,
                YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
            CompletePreviousBootRecord(PendingPath, coreHash, inventory.Target,
                YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
        }
        // File-only completion seam: a coincident session string cannot clear
        // another SDK/version/boot transaction, nor can it imply completion.
        internal static bool CompleteOwnedInstallRecord(string pendingPath, string core, Version target, string ownerSession, string? bootSession)
        {
            var saved = YeManCC.UsbipMaintenanceState.ReadRecord(pendingPath, pending: true);
            if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Missing) return false;
            if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Unavailable) throw new IOException("USB/IP 安装维护记录不可读，已保留");
            if (saved.Kind != YeManCC.UsbipMaintenanceState.RecordKind.Ready) throw new InvalidDataException("USB/IP 安装维护记录无法核验，已保留");
            using var pending = JsonDocument.Parse(saved.Value.GetRawText());
            var row = pending.RootElement;
            if (row.GetProperty("schemaVersion").GetInt32() != 3 ||
                row.GetProperty("operation").GetString() != "usbip-install" ||
                row.GetProperty("session").GetString() != ownerSession ||
                !string.Equals(row.GetProperty("coreSha256").GetString(), core, StringComparison.OrdinalIgnoreCase) ||
                row.GetProperty("targetVersion").GetString() != target.ToString() ||
                !YeManCC.UsbipMaintenanceState.ValidBootSession(bootSession) ||
                !string.Equals(row.GetProperty("bootSessionIdentity").GetString(), bootSession, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("USB/IP 维护记录不属于本次完整安装，已保留，请重新检查或导出日志");
            YeManCC.UsbipMaintenanceState.PreserveRecord(pendingPath, pendingPath + ".owned-completed-" + Convert.ToHexString(SHA256.HashData(saved.Raw)).ToLowerInvariant() + ".json", saved, removeSource: true);
            return true;
        }
        // Actual terminal preflight. The SDK may be reached only while the
        // observed absence and owner/consumer admission are still current.
        internal static bool RecheckInstall(Inventory planned, Func<Inventory> inspect,
            Func<bool> ownerAlive, Func<(bool Safe, string Reason)> safety)
        {
            if (!ownerAlive()) throw new InvalidOperationException("YMCC 已退出，未开始 USB/IP 安装");
            var now = inspect();
            if (now.Target != planned.Target || !now.Verified)
                throw new InvalidDataException("USB/IP 安装前状态已改变，未调用安装器");
            if (now.Present && now.Installed == now.Target && now.DriverCurrent) return false;
            if (now.Present || now.Installed is not null)
                throw new InvalidDataException("USB/IP 安装前出现其他产品或不完整登记，未调用安装器");
            var allowed = safety();
            if (!allowed.Safe) throw new InvalidOperationException(allowed.Reason);
            if (!ownerAlive()) throw new InvalidOperationException("YMCC 已退出，未开始 USB/IP 安装");
            return true;
        }
        // Internal file-only seam: callers supply the existing protected path
        // and boot evidence. This does not query Windows or retire pending.json.
        internal static bool CompletePreviousBootRecord(string pendingPath, string core, Version target, Guid? boot, uint? sequence, string? bootSession)
        {
            string previous = pendingPath + ".previous-boot";
            if (!File.Exists(previous)) return false;
            try {
                var saved = YeManCC.UsbipMaintenanceState.ReadRecord(previous, pending: true);
                if (saved.Kind != YeManCC.UsbipMaintenanceState.RecordKind.Ready) return false;
                using var doc = JsonDocument.Parse(saved.Value.GetRawText());
                if (!ContinuationMatches(doc.RootElement, core, target, boot, sequence, bootSession)) return false;
                YeManCC.UsbipMaintenanceState.PreserveRecord(previous, previous + ".completed", saved, removeSource: true);
                return true;
            } catch { return false; /* Diagnostic cleanup cannot change the installed driver verdict. */ }
        }
        internal static bool ContinuationMatches(JsonElement record, string core, Version target, Guid? boot, uint? sequence=null, string? bootSession=null)
        {
            try {
                if (!YeManCC.UsbipMaintenanceState.RecordStructure(record)) return false;
                string? operation = record.GetProperty("operation").GetString();
                return (operation == "usbip-uninstall" || operation == "usbip-install") &&
                    record.GetProperty("coreSha256").GetString() == core &&
                    record.GetProperty("targetVersion").GetString() == target.ToString() &&
                    !YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,boot,sequence,Environment.TickCount64,bootSession);
            } catch { return false; }
        }
        private void WritePending(string operation)
        {
            YeManCC.UsbipMaintenanceState.PrepareProtectedDirectory();
            AtomicWrite(PendingPath, new { schemaVersion = 3, operation, session, coreSha256 = coreHash,
                targetVersion = plannedInventory!.Target.ToString(), uptimeMs = Environment.TickCount64,
                bootIdentifier = YeManCC.UsbipMaintenanceState.CurrentBootIdentifier?.ToString("D"), bootSequence = YeManCC.UsbipMaintenanceState.CurrentBootSequence, bootSessionIdentity = YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity ?? throw new InvalidOperationException("Windows boot session is unavailable") }, omitNull:true);
        }
        public bool AlreadyAttempted(Inventory inventory)
        {
            var saved = YeManCC.UsbipMaintenanceState.ReadRecord(journal);
            if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Missing) return false;
            if (saved.Kind != YeManCC.UsbipMaintenanceState.RecordKind.Ready) return true;
            try
            {
                using var doc = JsonDocument.Parse(saved.Value.GetRawText());
                return AttemptStillBlocks(doc.RootElement, coreHash, inventory.Installed, inventory.Target, YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
            }
            catch { return true; } // A corrupt interrupted-attempt record does not authorize a repeat.
        }
        internal static bool AttemptStillBlocks(JsonElement record, string core, Version? installed, Version target, Guid? boot, uint? sequence=null, string? bootSession=null)
        {
            try {
                if (!YeManCC.UsbipMaintenanceState.RecordStructure(record)) return true;
                if (record.GetProperty("coreSha256").GetString() != core ||
                    record.GetProperty("installedVersion").GetString() != (installed?.ToString() ?? "absent") ||
                    record.GetProperty("targetVersion").GetString() != target.ToString()) return false;
                if (record.TryGetProperty("mutationStarted", out var started) && started.ValueKind == JsonValueKind.False) return false;
                if (record.TryGetProperty("bootSessionIdentity", out _))
                    return YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,boot,sequence,Environment.TickCount64,bootSession);
                // Retry a failed version pair only on a positively different
                // Windows boot; closing/reopening YMCC never renews the budget.
                if (record.TryGetProperty("bootIdentifier", out var value) &&
                    value.ValueKind == JsonValueKind.String && Guid.TryParse(value.GetString(), out var previous))
                    return YeManCC.UsbipMaintenanceState.RecordRequiresReboot(record,boot,sequence,Environment.TickCount64);
                return true;
            } catch { return true; }
        }
        public void RecordAttempt(Inventory inventory) {
            plannedInventory = inventory;
            YeManCC.UsbipMaintenanceState.PrepareProtectedDirectory();
            WriteAttempt(false);
        }
        private void WriteAttempt(bool mutationStarted) {
            var inventory = plannedInventory ?? throw new InvalidOperationException("Missing upgrade transaction");
            AtomicWrite(journal, new { coreSha256 = coreHash, installedVersion = inventory.Installed?.ToString() ?? "absent", targetVersion = inventory.Target.ToString(), session, mutationStarted, bootSessionIdentity = YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity ?? throw new InvalidOperationException("Windows boot session is unavailable"), bootIdentifier = YeManCC.UsbipMaintenanceState.CurrentBootIdentifier?.ToString("D"), bootSequence = YeManCC.UsbipMaintenanceState.CurrentBootSequence, startedAt = DateTimeOffset.UtcNow }, omitNull:true);
        }
        public int Uninstall()
        {
            // Never invoke a registry command line through a shell. Only the
            // reviewed executable receives fixed official Inno arguments.
            var name = sdk!.GetType("HIDMaestro.Internal.AdminNamespace", true)!.GetMethod("Name", BindingFlags.Static | BindingFlags.NonPublic)!;
            string transportName = (string)name.Invoke(null, ["Transport"])!;
            using var transport = new Mutex(false, transportName);
            bool held = false;
            try {
                try { held = transport.WaitOne(0); }
                catch (AbandonedMutexException) { held = true; throw new InvalidOperationException("前次传输操作中断，未卸载"); }
                if (!held) throw new InvalidOperationException("HIDM 正在处理 USB/IP，请稍后重新打开 YMCC");
                if (!OwnerAlive || !SafeToChange(true, out var reason)) throw new InvalidOperationException("USB/IP 占用状态发生变化，未卸载");
                using (var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (var key = hive.OpenSubKey(AppKey))
                using (var service = hive.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\usbip2_ude")) {
                    if (plannedInventory is null || !Version.TryParse(key?.GetValue("DisplayVersion") as string, out var now) ||
                        now != plannedInventory.Installed ||
                        !ResolveImage(service?.GetValue("ImagePath") as string ?? "").Equals(installedImage, StringComparison.OrdinalIgnoreCase) ||
                        !LegacyProductMatchesDriver(now, plannedInventory.Target, Sha(installedImage), targetDriverHash) ||
                        !UninstallerPath(key?.GetValue("UninstallString") as string ?? "").Equals(uninstaller, StringComparison.OrdinalIgnoreCase) ||
                        !Path.GetFullPath(key?.GetValue("InstallLocation") as string ?? "").TrimEnd(Path.DirectorySeparatorChar).Equals(Path.GetDirectoryName(uninstaller), StringComparison.OrdinalIgnoreCase) ||
                        !File.Exists(installedClient) || !Sha(installedClient).Equals(clientHash, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("USB/IP 产品、程序或驱动身份已改变，未卸载");
                }
                if (!TrustedUninstaller(uninstaller) || !Sha(uninstaller).Equals(uninstallHash, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("卸载程序已改变");
                var si = new ProcessStartInfo(uninstaller) { UseShellExecute = false, CreateNoWindow = true };
            foreach (var arg in new[] { "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" }) si.ArgumentList.Add(arg);
            // Persist the fence before launching the mutating terminal, so a
            // crash between process creation and observation cannot leave a gap.
            WriteAttempt(true);
            WritePending("usbip-uninstall");
            using var process = Process.Start(si) ?? throw new IOException("USB/IP 卸载程序未启动");
            // A system driver transaction is not force-killed. The durable
            // same-boot fence protects later creates after a bounded timeout.

            var terminalStartedAt = DateTimeOffset.UtcNow;
            NoteUninstall(new { schemaVersion=1, phase="started", pid=process.Id,
                startedAt=terminalStartedAt, budgetMs=300_000, forcedTermination=false });
            try {
                WaitForUninstall(process, 300_000);
                NoteUninstall(new { schemaVersion=1, phase="exited", pid=process.Id,
                    startedAt=terminalStartedAt, finishedAt=DateTimeOffset.UtcNow,
                    budgetMs=300_000, exitCode=process.ExitCode, forcedTermination=false });
                // Exit0 does not prove driver disappearance. Retain the original
                // mandatory-reboot fence; this receipt distinguishes a terminal
                // that returned from one that is still unknown after the budget.
                return process.ExitCode;
            } catch (TimeoutException) {
                NoteUninstall(new { schemaVersion=1, phase="wait-budget-expired", pid=process.Id,
                    startedAt=terminalStartedAt, observedAt=DateTimeOffset.UtcNow,
                    budgetMs=300_000, exitObserved=false, forcedTermination=false });
                throw;
            }
            } finally { if (held) transport.ReleaseMutex(); }
        }
        internal static void WaitForUninstall(Process process, int budgetMs)
        {
            if (!process.WaitForExit(Math.Clamp(budgetMs, 1, 300_000)))
                throw new TimeoutException("USB/IP 卸载超过预算，系统卸载仍可能运行。未启动后续安装，请重启 Windows");
        }
        public bool Removed()
        {
            using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var entry = hive.OpenSubKey(AppKey);
            using var service = hive.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\usbip2_ude");
            if (entry is not null || service is not null) return false;
            // Uninstall can leave extension/filter packages registered even when
            // its root service vanished. Require an actual successful inventory.
            var si = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "pnputil.exe")) {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (var arg in new[] { "/enum-drivers", "/format", "xml" }) si.ArgumentList.Add(arg);
            using var process = Process.Start(si) ?? throw new IOException("无法核对驱动包卸载结果");
            var output = process.StandardOutput.ReadToEndAsync(); var error = process.StandardError.ReadToEndAsync();
            if (!process.WaitForExit(30000)) {
                try { process.Kill(true); } catch { }
                process.WaitForExit(2000);return false;
            }
            if (process.ExitCode != 0 || !Task.WaitAll([output, error], 2000)) return false;
            var packages = HidMaestroPackagePreparation.ParseDrivers(output.GetAwaiter().GetResult());
            if (packages.Any(p => p.Original.Equals("usbip2_ude.inf", StringComparison.OrdinalIgnoreCase) || p.Original.Equals("usbip2_filter.inf", StringComparison.OrdinalIgnoreCase))) return false;
            var type = sdk!.GetType("HIDMaestro.HMContext", true)!;
            return type.GetProperty("IsUsbipBackendAvailable", BindingFlags.Public | BindingFlags.Static)!.GetValue(null) is false;
        }
        public void Install(Action<string> progress)
        {
            var planned = plannedInventory ?? throw new InvalidOperationException("Missing USB/IP install transaction");
            if (!RecheckInstall(planned, Inspect, () => OwnerAlive,
                () => { bool safe = SafeToChange(false, out var reason); return (safe, reason); })) {
                progress("安装前已确认配套产品和驱动文件，未重复运行安装器。");
                return;
            }
            string file = sdk!.Location;
            if (!Sha(file).Equals(coreHash, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("HIDM 在升级途中改变");
            WriteAttempt(true);
            WritePending("usbip-install");
            sdk.GetType("HIDMaestro.HMContext", true)!.GetMethod("InstallUsbipBackend", BindingFlags.Public | BindingFlags.Static)!.Invoke(null, [progress]);
        }
        public bool Verify(Inventory inventory)
        {
            using var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\usbip2_ude");
            string file = ResolveImage(key?.GetValue("ImagePath") as string ?? "");
            bool verified = File.Exists(file) && Sha(file).Equals(targetDriverHash, StringComparison.OrdinalIgnoreCase) &&
                sdk!.GetType("HIDMaestro.HMContext", true)!.GetProperty("IsUsbipBackendAvailable", BindingFlags.Public | BindingFlags.Static)!.GetValue(null) is true;
            // Execute re-inspects product registration before ConfirmCurrent
            // retires the fence; backend readiness alone must not retire it.
            return verified;
        }
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern uint RegisterWindowMessageW(string name);
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
        public void Publish(Snapshot snapshot) {
            lock (lifecycleGate) {
                lifecycle.Add(new { observedAt = DateTimeOffset.UtcNow, state = snapshot });
                if (lifecycle.Count > 48) lifecycle.RemoveAt(0);
                AtomicWrite(report + ".usbip.json", new { batch = "GP-USBIP-UPGRADE-1", installRoot = root,
                    ownerPid, session, usbipUpgrade = snapshot, usbipLifecycle = lifecycle.ToArray() });
            }
            // Finite stage notifications only; no worker, timer, polling or UI command.
            uint message = RegisterWindowMessageW("YeManCC.UsbipUpgrade.Changed");
            if (message != 0 && ownerPid > 0) PostMessageW(new IntPtr(0xffff), message, (UIntPtr)(uint)ownerPid, IntPtr.Zero);
        }
        private static void AtomicWrite(string file, object value, bool omitNull=false)
        {
            YeManCC.UsbipMaintenanceState.WriteRecord(file, value, omitNull, maximum: file.EndsWith(".usbip.json", StringComparison.OrdinalIgnoreCase) ? 262144 : 4096);
        }
    }
}
