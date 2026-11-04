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
        hostBackendAttempt = RecordEvidence(Path.Combine(DirectoryPath, "host-backend-attempt.json")),
        scope = "bounded-read-only-maintenance-records;not-driver-ready-or-controller-proof"
    };
    private static object RecordEvidence(string path)
    {
        try {
            var saved = ReadRecord(path);
            if (saved.Kind == RecordKind.Missing) return new { state = "absent" };
            if (saved.Kind != RecordKind.Ready) return new { state = "unknown-preserved", recordState = saved.Kind.ToString(), reason = saved.Error };
            using var doc = JsonDocument.Parse(saved.Value.GetRawText());
            var row = new Dictionary<string, object?> { ["state"] = "read", ["requiresReboot"] = RecordRequiresReboot(doc.RootElement) };
            foreach (var name in new[] { "schemaVersion", "operation", "session", "coreSha256", "targetVersion", "installedVersion", "mutationStarted", "uptimeMs", "bootIdentifier", "bootSequence", "bootSessionIdentity", "bootSessionEvidence", "hostPid", "reservation", "startedAt", "scope" })
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
    // Bounded record storage shared by setup and Host. Missing is a proved
    // ordinary directory absence, never File.Exists swallowing an IO failure.
    internal enum RecordKind { Missing, Ready, Malformed, Unsupported, Unavailable }
    internal sealed record RecordRead(RecordKind Kind, byte[] Raw, JsonElement Value, string? Error = null);
    internal static bool RecordStructure(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object) return false;
        var stack = new Stack<JsonElement>(); stack.Push(root); int nodes = 0;
        while (stack.Count > 0) {
            var value = stack.Pop(); if (++nodes > 2048) return false;
            if (value.ValueKind == JsonValueKind.Object) {
                var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var field in value.EnumerateObject()) { if (!keys.Add(field.Name)) return false; stack.Push(field.Value); }
            } else if (value.ValueKind == JsonValueKind.Array) foreach (var child in value.EnumerateArray()) stack.Push(child);
        }
        if (root.TryGetProperty("schemaVersion", out var schema) &&
            (schema.ValueKind != JsonValueKind.Number || !schema.TryGetInt32(out int version) || version is < 1 or > 3)) return false;
        foreach (string name in new[] { "operation", "session", "coreSha256", "targetVersion", "installedVersion", "startedAt", "reservation", "scope" })
            if (root.TryGetProperty(name, out var text) && (text.ValueKind != JsonValueKind.String || text.GetString() is not { Length: > 0 and <= 1024 })) return false;
        if (root.TryGetProperty("mutationStarted", out var started) && started.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return false;
        if (root.TryGetProperty("operation", out var operation) && operation.GetString() is not ("usbip-install" or "usbip-uninstall" or "usbip-host-backend-attempt")) return false;
        if (root.TryGetProperty("targetVersion", out var target) && !Version.TryParse(target.GetString(), out _)) return false;
        if (root.TryGetProperty("installedVersion", out var installed) && installed.GetString() != "absent" && !Version.TryParse(installed.GetString(), out _)) return false;
        foreach (string name in new[] { "uptimeMs", "bootSequence", "hostPid" }) if (root.TryGetProperty(name, out var number)) {
            if (number.ValueKind != JsonValueKind.Number || !number.TryGetInt64(out long integer) || integer < 0 ||
                name == "bootSequence" && integer > uint.MaxValue || name == "hostPid" && (integer == 0 || integer > int.MaxValue)) return false;
        }
        if (root.TryGetProperty("bootIdentifier", out var boot) && (boot.ValueKind != JsonValueKind.String || !Guid.TryParse(boot.GetString(), out var id) || id == Guid.Empty)) return false;
        if (root.TryGetProperty("bootSessionIdentity", out var session) && (session.ValueKind != JsonValueKind.String || !ValidBootSession(session.GetString()))) return false;
        return true;
    }
    private static void OrdinaryRecordParents(string path)
    {
        var parent = Path.GetDirectoryName(Path.GetFullPath(path)); int count = 0;
        while (parent is not null) {
            if (++count > 64) throw new IOException("USB/IP record ancestry is too deep");
            try { var flags = File.GetAttributes(parent); if ((flags & FileAttributes.Directory) == 0 || (flags & FileAttributes.ReparsePoint) != 0) throw new IOException("USB/IP record parent is not an ordinary directory"); }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            var next = Path.GetDirectoryName(parent); if (next == parent) break; parent = next;
        }
    }
    internal static RecordRead ReadRecord(string path, bool pending = false, int maximum = 4096)
    {
        byte[] bytes = [];
        try {
            if (!Path.IsPathFullyQualified(path)) return new(RecordKind.Unavailable, bytes, default, "non-absolute-path");
            if (maximum is < 1 or > 262144) return new(RecordKind.Unavailable, bytes, default, "invalid-budget");
            OrdinaryRecordParents(path);
            FileAttributes flags;
            try { flags = File.GetAttributes(path); }
            catch (FileNotFoundException) { return new(RecordKind.Missing, bytes, default); }
            catch (DirectoryNotFoundException) { return new(RecordKind.Missing, bytes, default); }
            if ((flags & (FileAttributes.Directory | FileAttributes.ReparsePoint)) != 0) return new(RecordKind.Unavailable, bytes, default, "not-ordinary-file");
            using (var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete)) {
                if (input.Length > maximum) return new(RecordKind.Malformed, bytes, default, "oversize");
                bytes = new byte[checked((int)input.Length)]; input.ReadExactly(bytes);
                if (input.ReadByte() != -1) return new(RecordKind.Unavailable, [], default, "length-changed");
            }
            string text;
            if (bytes.Length >= 2 && ((bytes[0] == 255 && bytes[1] == 254) || (bytes[0] == 254 && bytes[1] == 255)))
                text = new System.Text.UnicodeEncoding(bytes[0] == 254, true, true).GetString(bytes, 2, bytes.Length - 2);
            else { int start = bytes.AsSpan().StartsWith(new byte[] { 239, 187, 191 }) ? 3 : 0; text = new System.Text.UTF8Encoding(false, true).GetString(bytes, start, bytes.Length - start); }
            using var document = JsonDocument.Parse(text, new JsonDocumentOptions { MaxDepth = 16 }); var root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Object && root.TryGetProperty("schemaVersion", out var schema) && schema.ValueKind == JsonValueKind.Number && schema.TryGetInt64(out long future) && future > 3)
                return new(RecordKind.Unsupported, bytes, default, "future-schema");
            if (!RecordStructure(root)) return new(RecordKind.Malformed, bytes, default, "invalid-record");
            if (root.TryGetProperty("coreSha256", out var core) && (core.GetString() is not { Length: 64 } hash || !hash.All(Uri.IsHexDigit)))
                return new(RecordKind.Malformed, bytes, default, "invalid-core");
            if (pending && (!root.TryGetProperty("operation", out var operation) || operation.GetString() is not ("usbip-install" or "usbip-uninstall") ||
                !root.TryGetProperty("coreSha256", out _) || !root.TryGetProperty("targetVersion", out _))) return new(RecordKind.Malformed, bytes, default, "missing-pending-identity");
            return new(RecordKind.Ready, bytes, root.Clone());
        }
        catch (System.Text.DecoderFallbackException) { return new(RecordKind.Malformed, bytes, default, "invalid-encoding"); }
        catch (JsonException) { return new(RecordKind.Malformed, bytes, default, "invalid-json"); }
        catch (Exception error) { return new(RecordKind.Unavailable, bytes, default, error.GetType().Name); }
    }
    private static bool SameRecord(RecordRead a, RecordRead b) => a.Kind == b.Kind &&
        (a.Kind == RecordKind.Missing || a.Kind == RecordKind.Ready && a.Raw.AsSpan().SequenceEqual(b.Raw));
    // Only identified, fully decoded metadata may enter a file transition.
    // A conflict is not a missing file and never grants a new install budget.
    internal static void PreserveRecord(string source, string destination, RecordRead expected, bool removeSource, int maximum = 4096)
    {
        if (expected.Kind != RecordKind.Ready || !SameRecord(expected, ReadRecord(source, maximum: maximum))) throw new IOException("USB/IP source record changed");
        var prior = ReadRecord(destination, maximum: maximum);
        if (prior.Kind == RecordKind.Ready && !SameRecord(expected, prior) &&
            (destination.EndsWith(".previous-boot", StringComparison.Ordinal) || destination.EndsWith(".previous-boot.completed", StringComparison.Ordinal))) {
            // A healthy historical transaction is retained under its content
            // identity, not discarded or allowed to block every later upgrade.
            string digest = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(prior.Raw)).ToLowerInvariant();
            PreserveRecord(destination, destination + ".ymcc-history-" + digest + ".json", prior, removeSource: true, maximum: maximum);
            prior = ReadRecord(destination, maximum: maximum);
        }
        if (prior.Kind != RecordKind.Missing && !SameRecord(expected, prior)) throw new IOException("USB/IP destination contains different or unknown evidence");
        if (prior.Kind == RecordKind.Missing) {
            string stage = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try {
                using (var committed = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                    committed.Write(expected.Raw); committed.Flush(flushToDisk: true);
                }
                if (!SameRecord(expected, ReadRecord(stage, maximum: maximum)) || !SameRecord(expected, ReadRecord(source, maximum: maximum))) throw new IOException("USB/IP preservation changed before publication");
                File.Move(stage, destination, overwrite: false);
            } finally { if (File.Exists(stage)) File.Delete(stage); }
        }
        if (!SameRecord(expected, ReadRecord(destination, maximum: maximum)) || !SameRecord(expected, ReadRecord(source, maximum: maximum))) throw new IOException("USB/IP preserved record readback mismatch");
        if (removeSource) {
            File.Delete(source);
            if (ReadRecord(source, maximum: maximum).Kind != RecordKind.Missing || !SameRecord(expected, ReadRecord(destination, maximum: maximum))) throw new IOException("USB/IP source retirement not confirmed");
        }
    }
    internal static void WriteRecord(string path, object value, bool omitNull = false, int maximum = 4096)
    {
        OrdinaryRecordParents(path);
        var before = ReadRecord(path, maximum: maximum);
        if (before.Kind is not (RecordKind.Missing or RecordKind.Ready)) throw new InvalidDataException("USB/IP previous evidence is unreadable or unsupported");
        var options = omitNull ? new JsonSerializerOptions { DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull } : null;
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options);
        if (before.Kind == RecordKind.Ready) {
            var next = System.Text.Json.Nodes.JsonNode.Parse(payload) as System.Text.Json.Nodes.JsonObject;
            if (next is null) throw new InvalidDataException("USB/IP output is not an object");
            var owned = new HashSet<string>(new[] { "schemaVersion", "operation", "session", "coreSha256", "targetVersion", "installedVersion", "mutationStarted", "uptimeMs", "bootIdentifier", "bootSequence", "bootSessionIdentity", "bootSessionEvidence", "hostPid", "reservation", "startedAt", "scope", "batch", "installRoot", "ownerPid", "usbipUpgrade", "usbipLifecycle" }, StringComparer.OrdinalIgnoreCase);
            foreach (var field in before.Value.EnumerateObject()) if (!owned.Contains(field.Name) && !next.ContainsKey(field.Name)) next[field.Name] = System.Text.Json.Nodes.JsonNode.Parse(field.Value.GetRawText());
            payload = JsonSerializer.SerializeToUtf8Bytes(next);
        }
        if (payload.Length == 0 || payload.Length > maximum) throw new InvalidDataException("USB/IP record exceeds its storage budget");
        using var generated = JsonDocument.Parse(payload, new JsonDocumentOptions { MaxDepth = 16 });
        if (!RecordStructure(generated.RootElement)) throw new InvalidDataException("USB/IP record is invalid");
        if (before.Kind == RecordKind.Ready && before.Raw.AsSpan().SequenceEqual(payload)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            using (var output = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { output.Write(payload); output.Flush(flushToDisk: true); }
            var prepared = ReadRecord(temp, maximum: maximum);
            if (prepared.Kind != RecordKind.Ready || !prepared.Raw.AsSpan().SequenceEqual(payload)) throw new IOException("USB/IP temporary record was not confirmed");
            if (!SameRecord(before, ReadRecord(path, maximum: maximum))) throw new IOException("USB/IP record changed before publication");
            if (before.Kind == RecordKind.Ready) {
                string digest = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(before.Raw)).ToLowerInvariant();
                PreserveRecord(path, path + ".ymcc-before-" + digest + ".bak", before, removeSource: false, maximum: maximum);
            }
            if (!SameRecord(before, ReadRecord(path, maximum: maximum))) throw new IOException("USB/IP record changed after preservation");
            File.Move(temp, path, overwrite: before.Kind == RecordKind.Ready);
            var published = ReadRecord(path, maximum: maximum);
            if (published.Kind != RecordKind.Ready || !published.Raw.AsSpan().SequenceEqual(payload)) throw new IOException("USB/IP published record was not confirmed");
        } finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot) =>
        recordedBoot is null || currentBoot is null || recordedBoot == Guid.Empty || currentBoot == Guid.Empty || recordedBoot == currentBoot;
    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot, uint? recordedSequence, uint? currentSequence) =>
        recordedBoot is null || currentBoot is null || recordedBoot==Guid.Empty || currentBoot==Guid.Empty ||
        recordedSequence is null || currentSequence is null || (recordedBoot==currentBoot && recordedSequence==currentSequence);
    private static bool RecordRequiresReboot(JsonElement record) => RecordRequiresReboot(record,CurrentBootIdentifier,CurrentBootSequence,Environment.TickCount64,CurrentBootSessionIdentity);
    internal static bool RecordRequiresReboot(JsonElement record, Guid? currentBoot, uint? currentSequence, long currentUptime, string? currentBootSession=null)
    {
        try {
            if (!RecordStructure(record)) return true;
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
        } catch { return true; }
    }
    internal static bool Pending
    {
        get { var read = ReadRecord(PendingPath, pending: true); return read.Kind != RecordKind.Missing &&
            (read.Kind != RecordKind.Ready || RecordRequiresReboot(read.Value)); }
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
            var read = ReadRecord(PendingPath, pending: true);
            if (read.Kind != RecordKind.Missing) {
                if (read.Error == "oversize") { lastTransition = "pending-oversize;fence-retained"; return; }
                if (read.Kind != RecordKind.Ready) { lastTransition = "record-transition-failed;fence-retained"; return; }
                using var doc = JsonDocument.Parse(read.Value.GetRawText());
                if (!RecordRequiresReboot(doc.RootElement)) {
                    PrepareProtectedDirectory();
                    PreserveRecord(PendingPath, PendingPath + ".previous-boot", read, removeSource: true);
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
        var saved = ReadRecord(path);
        if (saved.Kind != RecordKind.Ready || CurrentBootSessionIdentity is null) return false;
        string original = saved.Value.GetRawText();
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
        if (!SameRecord(saved, ReadRecord(path))) return false;
        PreserveRecord(path, path + ".before-boot-session-v2", saved, removeSource: false);
        WriteRecord(path, node);
        var actual = ReadRecord(path);
        return actual.Kind == RecordKind.Ready && actual.Value.TryGetProperty("bootSessionIdentity", out var observed) && observed.GetString() == CurrentBootSessionIdentity;
    }
}
