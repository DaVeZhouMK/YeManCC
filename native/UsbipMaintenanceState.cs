using System.Security.AccessControl;
using System.Security.Principal;
using System.Text.Json;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace YeManCC;

// Machine-wide transaction evidence, shared by elevated setup and every Host.
// The directory admits SYSTEM/Administrators writes and Users read access only.
internal static class UsbipMaintenanceState
{
    internal static string DirectoryPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "YeManCC", "UsbipDriverMaintenance");
    internal static string PendingPath => Path.Combine(DirectoryPath, "pending.json");
    // Same provider/key/mutex as native/boot_mode_identity.h. Loader GUID/BootId
    // can repeat after a real restart. This is not an app/logon/Host identity.
    internal const string BootSessionKey = @"SOFTWARE\YeManCC\Runtime\BootSessionV2";
    private const string BootSessionMutex = @"Global\YeManCC-BootSessionV2";
    internal const string BootSessionPrefix = "windows-volatile-v2|";
    private static string? cachedBootSession;
    private static string lastTransition = "not-run";
    internal static bool ValidBootSession(string? value) => value is not null &&
        value.StartsWith(BootSessionPrefix, StringComparison.Ordinal) &&
        value.Length == BootSessionPrefix.Length + 38 &&
        Guid.TryParseExact(value[BootSessionPrefix.Length..], "B", out var id) && id != Guid.Empty;
    internal static string? CurrentBootSessionIdentity
    {
        get {
            var cached = System.Threading.Volatile.Read(ref cachedBootSession);
            if (cached is not null) return cached;
            try {
                using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
                using var key = machine.OpenSubKey(BootSessionKey, writable: false);
                if (key?.GetValueKind("Session") != RegistryValueKind.String) return null;
                var text = key.GetValue("Session", null, RegistryValueOptions.DoNotExpandEnvironmentNames) as string;
                var value = BootSessionPrefix + text;
                if (!ValidBootSession(value)) return null;
                value = BootSessionPrefix + Guid.Parse(text!).ToString("B").ToUpperInvariant();
                System.Threading.Interlocked.CompareExchange(ref cachedBootSession, value, null);
                return cachedBootSession;
            } catch { return null; }
        }
    }
    // Elevated setup only, inside its existing USB/IP gate. Host/status readers
    // never create the key or modify a maintenance record.
    private static bool PrepareBootSessionIdentity()
    {
        if (CurrentBootSessionIdentity is not null) return true;
        using var mutex = new Mutex(false, BootSessionMutex);
        bool held = false;
        try {
            try { held = mutex.WaitOne(5000); } catch (AbandonedMutexException) { held = true; }
            if (!held) return false;
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var key = machine.CreateSubKey(BootSessionKey, writable: true, RegistryOptions.Volatile);
            if (key is null) return false;
            // A malformed value is not overwritten and never proves a reboot.
            if (!key.GetValueNames().Contains("Session", StringComparer.OrdinalIgnoreCase))
                key.SetValue("Session", Guid.NewGuid().ToString("B").ToUpperInvariant(), RegistryValueKind.String);
            return CurrentBootSessionIdentity is not null;
        } catch { return false; }
        finally { if (held) mutex.ReleaseMutex(); }
    }
    internal static object DiagnosticEvidence() => new {
        schemaVersion = 1, policy = "VC-USBIP-UNINSTALL-BOOT-HANDOFF-20261024-R1",
        provider = "machine-volatile-hive-v2;shared-with-native-startup",
        currentBootSessionIdentity = CurrentBootSessionIdentity,
        loaderBootIdentifier = CurrentBootIdentifier?.ToString("D"),
        loaderBootSequence = CurrentBootSequence, currentUptimeMs = Environment.TickCount64,
        lastTransition, pending = RecordEvidence(PendingPath),
        previousBoot = RecordEvidence(PendingPath + ".previous-boot"),
        attempt = RecordEvidence(Path.Combine(DirectoryPath, "attempt.json")),
        scope = "bounded-read-only-maintenance-records;not-driver-ready-or-controller-proof"
    };
    private static object RecordEvidence(string path)
    {
        try {
            if (!File.Exists(path)) return new { state = "absent" };
            if (new FileInfo(path).Length > 4096) return new { state = "oversize-preserved" };
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var row = new Dictionary<string, object?> { ["state"] = "read", ["requiresReboot"] = RecordRequiresReboot(doc.RootElement) };
            foreach (var name in new[] { "schemaVersion", "operation", "session", "coreSha256", "targetVersion", "installedVersion", "mutationStarted", "uptimeMs", "bootIdentifier", "bootSequence", "bootSessionIdentity", "bootSessionEvidence" })
                if (doc.RootElement.TryGetProperty(name, out var value)) row[name] = value.Clone();
            return row;
        } catch (Exception e) { return new { state = "unreadable-preserved", errorType = e.GetType().Name, hresult = e.HResult }; }
    }
    private static readonly Lazy<Guid?> bootIdentifier = new(ReadBootIdentifier);
    internal static Guid? CurrentBootIdentifier => bootIdentifier.Value;
    private static readonly Lazy<uint?> bootSequence = new(ReadBootSequence);
    internal static uint? CurrentBootSequence => bootSequence.Value;
    internal static bool RequiresLoaderSequence(Architecture process,Architecture operatingSystem) => process==Architecture.X64 && operatingSystem==Architecture.X64;
    internal static bool LoaderSequenceRequired => RequiresLoaderSequence(RuntimeInformation.ProcessArchitecture,RuntimeInformation.OSArchitecture);
    [DllImport("kernel32.dll", SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, nuint bytes, out nuint read);
    // Same reviewed Windows x64 loader sequence as gpu_dedicated_memory_runtime.h.
    // Combining both observations also handles an unchanged environment GUID.
    private static uint? ReadBootSequence()
    {
        try {
            if (RuntimeInformation.ProcessArchitecture!=Architecture.X64) return null;
            byte[] shared=new byte[0x2c8];
            if (!ReadProcessMemory(new IntPtr(-1),new IntPtr(0x7ffe0000),shared,(nuint)shared.Length,out var read) || read!=(nuint)shared.Length ||
                BitConverter.ToUInt32(shared,0x26c)!=10 || BitConverter.ToUInt32(shared,0x270)!=0 || BitConverter.ToUInt16(shared,0x26a)!=9) return null;
            return BitConverter.ToUInt32(shared,0x2c4);
        } catch { return null; }
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct BootEnvironment { public Guid Identifier; public uint FirmwareType; public ulong Flags; }
    [DllImport("ntdll.dll")]
    private static extern int NtQuerySystemInformation(int informationClass, out BootEnvironment information, uint size, out uint returned);
    private static Guid? ReadBootIdentifier()
    {
        try {
            int result = NtQuerySystemInformation(90, out var info, (uint)Marshal.SizeOf<BootEnvironment>(), out uint returned);
            return result >= 0 && returned >= 16 && info.Identifier != Guid.Empty ? info.Identifier : null;
        } catch { return null; }
    }
    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot) =>
        recordedBoot is null || currentBoot is null || recordedBoot == Guid.Empty || currentBoot == Guid.Empty || recordedBoot == currentBoot;
    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot, uint? recordedSequence, uint? currentSequence) =>
        recordedBoot is null || currentBoot is null || recordedBoot==Guid.Empty || currentBoot==Guid.Empty ||
        recordedSequence is null || currentSequence is null || (recordedBoot==currentBoot && recordedSequence==currentSequence);
    private static bool RecordRequiresReboot(JsonElement record) => RecordRequiresReboot(record,CurrentBootIdentifier,CurrentBootSequence,Environment.TickCount64,CurrentBootSessionIdentity);
    internal static bool RecordRequiresReboot(JsonElement record, Guid? currentBoot, uint? currentSequence, long currentUptime, string? currentBootSession=null)
    {
        if (record.TryGetProperty("bootSessionIdentity", out var session)) {
            var previous = session.ValueKind == JsonValueKind.String ? session.GetString() : null;
            return !ValidBootSession(previous) || !ValidBootSession(currentBootSession) ||
                string.Equals(previous, currentBootSession, StringComparison.OrdinalIgnoreCase);
        }
        if (record.TryGetProperty("bootIdentifier", out var identity)) {
            Guid? previous = identity.ValueKind == JsonValueKind.String && Guid.TryParse(identity.GetString(), out var parsed) ? parsed : null;
            if (record.TryGetProperty("bootSequence",out var sequence))
                return sequence.ValueKind!=JsonValueKind.Number || !sequence.TryGetUInt32(out uint savedSequence) || RequiresReboot(previous,currentBoot,savedSequence,currentSequence);
            return RequiresReboot(previous, currentBoot);
        }
        // Read old unpublished records conservatively; all new transactions
        // require a stable boot identifier before their first mutation.
        long uptime = record.GetProperty("uptimeMs").GetInt64();
        return RequiresReboot(uptime, currentUptime);
    }
    internal static bool Pending
    {
        get {
            if (!File.Exists(PendingPath)) return false;
            try {
                if (new FileInfo(PendingPath).Length > 4096) return true;
                using var doc = JsonDocument.Parse(File.ReadAllText(PendingPath));
                return RecordRequiresReboot(doc.RootElement);
            } catch { return true; }
        }
    }
    internal static bool RequiresReboot(long recordedUptime, long currentUptime) => recordedUptime < 0 || currentUptime < 0 || currentUptime >= recordedUptime;
    internal static void PrepareProtectedDirectory()
    {
        var dir = new DirectoryInfo(DirectoryPath);
        Directory.CreateDirectory(dir.FullName);
        for (var part = dir; part is not null; part = part.Parent)
            if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("USB/IP maintenance path is redirected");
        var security = new DirectorySecurity();
        security.SetAccessRuleProtection(true, false);
        var admins = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
        security.SetOwner(admins);
        foreach (var sid in new[] { admins, new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null) })
            security.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
        security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinUsersSid, null), FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
        dir.SetAccessControl(security);
    }
    internal static void RetireConfirmedPreviousBoot(bool prepare)
    {
        if (!prepare) return;
        try {
            if (!PrepareBootSessionIdentity()) { lastTransition = "boot-session-unavailable;fence-retained"; return; }
            if (File.Exists(PendingPath)) {
                if (new FileInfo(PendingPath).Length > 4096) { lastTransition = "pending-oversize;fence-retained"; return; }
                using var doc = JsonDocument.Parse(File.ReadAllText(PendingPath));
                if (!RecordRequiresReboot(doc.RootElement)) {
                    PrepareProtectedDirectory();
                    File.Move(PendingPath, PendingPath + ".previous-boot", true);
                    lastTransition = "previous-boot-proven;normal-continuation";
                } else if (AnchorLegacyBootSession(PendingPath)) {
                    // There is no earlier V2 observation for this old record.
                    // Baseline only: SAME boot still blocked. Next real boot can
                    // resume normally even when the legacy loader tuple repeats.
                    lastTransition = "legacy-baselined;one-real-reboot-still-required";
                } else lastTransition = "same-or-unknown-boot;fence-retained";
            } else lastTransition = "no-pending-fence";
            AnchorLegacyBootSession(Path.Combine(DirectoryPath, "attempt.json"));
        } catch { lastTransition = "record-transition-failed;fence-retained"; }
    }
    private static bool AnchorLegacyBootSession(string path)
    {
        if (!File.Exists(path) || new FileInfo(path).Length > 4096 || CurrentBootSessionIdentity is null) return false;
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) return false;
        string original = File.ReadAllText(path);
        using var doc = JsonDocument.Parse(original); var record = doc.RootElement;
        if (record.TryGetProperty("bootSessionIdentity", out _) || !RecordRequiresReboot(record)) return false;
        // Migrate only this installer's bounded, identified transaction format.
        if (!record.TryGetProperty("coreSha256", out var core) || core.ValueKind != JsonValueKind.String ||
            core.GetString() is not { Length: 64 } hash || !hash.All(Uri.IsHexDigit) ||
            !record.TryGetProperty("targetVersion", out var target) || target.ValueKind != JsonValueKind.String || !Version.TryParse(target.GetString(), out _) ||
            !record.TryGetProperty("bootIdentifier", out var boot) || boot.ValueKind != JsonValueKind.String || !Guid.TryParse(boot.GetString(), out var id) || id == Guid.Empty) return false;
        PrepareProtectedDirectory();
        var node = System.Text.Json.Nodes.JsonNode.Parse(original) as System.Text.Json.Nodes.JsonObject;
        if (node is null) return false;
        node["bootSessionIdentity"] = CurrentBootSessionIdentity;
        node["bootSessionEvidence"] = "legacy-baseline-only;no-prior-reboot-inferred";
        var payload = JsonSerializer.SerializeToUtf8Bytes(node);
        if (payload.Length > 4096) return false;
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            using (var stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                stream.Write(payload); stream.Flush(flushToDisk: true);
            }
            if (File.ReadAllText(path) != original) return false;
            string backup = path + ".before-boot-session-v2";
            if (!File.Exists(backup)) File.Copy(path, backup, overwrite: false);
            File.Move(temp, path, overwrite: true); return true;
        } finally { if (File.Exists(temp)) File.Delete(temp); }
    }
}
