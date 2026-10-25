using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;

// Version maintenance is a startup one-shot. It never repairs an unknown or
// same-version driver, constructs a controller, or downloads a different SDK.
internal static class UsbipUpgrade
{
    internal const string GateName = @"Global\YeManCC.UsbipVersionMaintenance";
    internal const string AppKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1";
    internal sealed record Snapshot(int schemaVersion, string phase, string installedVersion,
        string targetVersion, string detail, int sequence);
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
    // Live status is a separate read-only operation. It cannot acknowledge or
    // repeat a previous maintenance attempt, and never invokes the SDK backend.
    internal static Snapshot Observe(Runtime runtime)
    {
        Inventory? inventory = null;
        Snapshot State(string phase, string detail) => new(1, phase,
            inventory?.Installed?.ToString() ?? "", inventory?.Target.ToString() ?? "", detail, 0);
        try {
            inventory = runtime.Inspect();
            if (runtime.RebootPending) return State("reboot-required", "USB/IP 维护仍要求重启 Windows，当前读取未解除维护限制。");
            if (!inventory.Verified) return State("unverified", inventory.Reason);
            if (!inventory.Present) return State("not-installed", "当前未发现完整 USB/IP 安装；本次只读检查未安装驱动。");
            if (inventory.Installed is null) return State("unverified", "当前安装版本尚未确认，已保留原安装。");
            if (inventory.Installed.CompareTo(inventory.Target) > 0) return State("newer-preserved", "已重新核对，保留当前较新版本，不降级。");
            if (inventory.DriverCurrent) return State("current", "已重新核对当前配套版本；未重试上次升级，也不代表虚拟手柄已经启动。");
            if (inventory.Installed == inventory.Target) return State("unverified", "产品版本与配套相同，但驱动文件尚未核验一致；未自动修复。");
            return State("update-found", "当前版本低于配套版本，已保留现有安装；未自动重试升级。");
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
                inventory?.Target.ToString() ?? "", detail, ++sequence);
            runtime.Publish(value); return value;
        }
        Snapshot InstallAndVerify(bool requireRestart)
        {
            inventory = inventory! with { Installed = null };
            Send("installing", "正在由 HIDM 安装随包 USB/IP，请等待安装结束。");
            runtime.Install(_ => { });
            if (!runtime.OwnerAlive) return Send("failed", "HIDM 安装入口已返回，但 YMCC 已退出。下次启动会重新核对实际版本。");
            Send("verifying", "安装入口已返回，正在核对实际驱动版本与后端。");
            if (!runtime.Verify(inventory)) return Send("reboot-required", "尚未确认新驱动可用。请重启 Windows 后重新检查。");
            inventory = inventory with { Installed = inventory.Target, DriverCurrent = true };
            return requireRestart
                ? Send("reboot-required", "USB/IP 配套版本已安装，原卸载流程要求重启，请先重启 Windows。")
                : Send("complete", "USB/IP 配套升级完成；如虚拟手柄尚未开启，请重新启用所需模式。");
        }
        try
        {
            Send("checking", "正在核对当前 HIDM 配套的 USB/IP 版本。");
            inventory = runtime.Inspect();
            if (runtime.RebootPending) return Send("reboot-required", "上次 USB/IP 版本维护要求重启。请先重启 Windows，再启用虚拟手柄。");
            if (!inventory.Verified) return Send("unverified", inventory.Reason);
            if (!inventory.Present) {
                if (!runtime.ShouldResumeInstall(inventory)) return Send("not-installed", "尚未安装 USB/IP；需要时由 HIDM 安装随包版本。");
                if (!prepare) return Send("update-found", "已识别重启前的配套升级，检查模式未安装驱动。");
                if (!runtime.Administrator) return Send("admin-required", "请以管理员身份重新打开 YMCC，接续 USB/IP 配套安装。");
                if (!runtime.OwnerAlive) return Send("failed", "YMCC 已退出，未开始接续安装。");
                if (!runtime.SafeToChange(false, out var installReason)) return Send("busy", installReason);
                if (runtime.AlreadyAttempted(inventory)) return Send("failed", "本次开机的配套安装未完成，已停止重复安装。请重启 Windows 后检查或导出日志。");
                if (!runtime.Removed()) return Send("unverified", "重启后仍发现旧版 USB/IP 驱动包或后端残留，未开始安装。请检查系统应用中的 USB/IP 或导出日志。");
                runtime.RecordAttempt(inventory);
                return InstallAndVerify(false);
            }
            if (inventory.Installed is null) return Send("unverified", "无法确认已装版本，已保留原安装。请导出日志。");
            if (inventory.Installed > inventory.Target)
                return Send("newer-preserved", "已装版本高于 HIDM 配套版本，保留现有版本，不降级。");
            if (inventory.DriverCurrent || inventory.Installed == inventory.Target) {
                if (prepare && inventory.DriverCurrent) runtime.ConfirmCurrent(inventory);
                return Send("current", "USB/IP 无需版本升级；这不代表虚拟手柄已经启动。");
            }
            Send("update-found", "发现较旧的 USB/IP，准备升级到当前 HIDM 的配套版本。");
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
                "USB/IP 升级已停止：" + ex.GetBaseException().Message[..Math.Min(ex.GetBaseException().Message.Length, 240)] + "。请导出日志。", ++sequence);
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
        private string coreHash = "", uninstallHash = "";
        private readonly string journal;
        private Inventory? plannedInventory;
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
            if (hash.Equals(targetDriverHash, StringComparison.OrdinalIgnoreCase)) return new(target, installed, true, true, true);
            if (installed == target) return new(target, installed, true, true, false);
            if (!Legacy.TryGetValue(hash, out var known) || Version.Parse(known) != installed)
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
            uninstallHash = Sha(uninstaller);
            return new(target, installed, true, true, false);
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
        public bool ShouldResumeInstall(Inventory inventory)
        {
            string previous = PendingPath + ".previous-boot";
            if (!File.Exists(previous)) return false;
            try {
                if (new FileInfo(previous).Length > 4096) return false;
                using var doc = JsonDocument.Parse(File.ReadAllText(previous));
                return ContinuationMatches(doc.RootElement, coreHash, inventory.Target, YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
            } catch { return false; }
        }
        public void ConfirmCurrent(Inventory inventory)
        {
            string previous = PendingPath + ".previous-boot";
            if (!File.Exists(previous)) return;
            try {
                using var doc = JsonDocument.Parse(File.ReadAllText(previous));
                if (ContinuationMatches(doc.RootElement, coreHash, inventory.Target, YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity))
                    File.Move(previous, previous + ".completed", true);
            } catch { /* Diagnostic cleanup cannot change the installed driver verdict. */ }
        }
        internal static bool ContinuationMatches(JsonElement record, string core, Version target, Guid? boot, uint? sequence=null, string? bootSession=null)
        {
            try {
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
            if (!File.Exists(journal)) return false;
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(journal));
                return AttemptStillBlocks(doc.RootElement, coreHash, inventory.Installed, inventory.Target, YeManCC.UsbipMaintenanceState.CurrentBootIdentifier, YeManCC.UsbipMaintenanceState.CurrentBootSequence, YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity);
            }
            catch { return true; } // A corrupt interrupted-attempt record does not authorize a repeat.
        }
        internal static bool AttemptStillBlocks(JsonElement record, string core, Version? installed, Version target, Guid? boot, uint? sequence=null, string? bootSession=null)
        {
            try {
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
                using (var key = Registry.LocalMachine.OpenSubKey(AppKey)) {
                    if (!Version.TryParse(key?.GetValue("DisplayVersion") as string, out var now) || !Legacy.TryGetValue(Sha(installedImage), out var expected) || now != Version.Parse(expected))
                        throw new InvalidDataException("USB/IP 版本或驱动身份已改变，未卸载");
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
            if (verified && File.Exists(PendingPath)) {
                using var pending = JsonDocument.Parse(File.ReadAllText(PendingPath));
                if (pending.RootElement.GetProperty("operation").GetString() == "usbip-install" &&
                    pending.RootElement.GetProperty("session").GetString() == session) File.Delete(PendingPath);
            }
            if (verified) ConfirmCurrent(inventory);
            return verified;
        }
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern uint RegisterWindowMessageW(string name);
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
        public void Publish(Snapshot snapshot) {
            AtomicWrite(report + ".usbip.json", new { batch = "GP-USBIP-UPGRADE-1", installRoot = root, ownerPid, session, usbipUpgrade = snapshot });
            // Finite stage notifications only; no worker, timer, polling or UI command.
            uint message = RegisterWindowMessageW("YeManCC.UsbipUpgrade.Changed");
            if (message != 0 && ownerPid > 0) PostMessageW(new IntPtr(0xffff), message, (UIntPtr)(uint)ownerPid, IntPtr.Zero);
        }
        private static void AtomicWrite(string file, object value, bool omitNull=false)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(file)!);
            string temp = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try {
                using (var stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    JsonSerializer.Serialize(stream, value, omitNull ? new JsonSerializerOptions { DefaultIgnoreCondition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull } : null);
                File.Move(temp, file, true);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
        }
    }
}
