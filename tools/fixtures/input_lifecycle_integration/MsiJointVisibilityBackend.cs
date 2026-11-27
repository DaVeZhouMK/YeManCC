// Opt-in managed fixture terminal only. No HidHide, CM, driver or device API.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;
internal static partial class Program
{
    private static class MsiJointVisibilityBackend
    {
        private const int MaxBytes = 1024 * 1024, MaxCommands = 4096;
        private const string BeginPrefix = "managed:HIDHIDE_CYCLE_BEGIN ";
        private const string SignalPrefix = "fixture-signal:HIDHIDE_CYCLE ";
        private static readonly UTF8Encoding Utf8 = new(false, true);
        private static readonly object Gate = new();
        // Pins only; never a second visibility inventory. Lists exist only in the loaded document.
        private static string? PinnedPath, PinnedDeclarations, PinnedFile;
        internal readonly record struct Reply(bool Success, string Status, string Detail);
        private sealed record Cycle(string path, string runId, ulong epoch, ulong powerGeneration, int requestIndex);
        private sealed class State
        {
            internal string[] AllowedDevices = Array.Empty<string>(), AllowedApplications = Array.Empty<string>();
            internal List<string> Hidden = new(), Applications = new(), Commands = new();
            internal bool Cloaking;
            internal uint Cycles;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct FileInfo
        {
            internal uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh, WriteLow, WriteHigh;
            internal uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string name, uint access, FileShare share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(SafeFileHandle handle, out FileInfo info);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle, StringBuilder name, uint size, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint GetFileType(SafeFileHandle handle);
        private static void Require(bool ok, string reason) { if (!ok) throw new InvalidDataException(reason); }
        private static bool Ordinary(string s) => !string.IsNullOrEmpty(s) && s.IndexOfAny(new[] { '\r', '\n', '"', '\0' }) < 0;
        private static string FullPath(string value)
        {
            Require(!string.IsNullOrWhiteSpace(value) && Path.IsPathFullyQualified(value), "absolute-path-required");
            Require(!value.StartsWith(@"\\?\", StringComparison.Ordinal) && !value.StartsWith(@"\\.\", StringComparison.Ordinal), "device-path-rejected");
            var full = Path.GetFullPath(value);
            Require(full.IndexOf(':', Path.GetPathRoot(full)!.Length) < 0, "alternate-stream-rejected");
            return full;
        }
        private static string StatePath(string? receipt)
        {
            Require(receipt is not null, "original-receipt-not-configured");
            var originalReceipt = FullPath(receipt!);
            var value = Environment.GetEnvironmentVariable("YMCC_FIXTURE_MSI_VISIBILITY_STATE");
            Require(value is not null, "state-environment-missing");
            var full = FullPath(value!);
            var name = Path.GetFileName(full);
            Require(System.Text.RegularExpressions.Regex.IsMatch(name, @"\Amsi-visibility-[1-9][0-9]*\.json\z", System.Text.RegularExpressions.RegexOptions.CultureInvariant), "state-filename-invalid");
            Require(string.Equals(Path.GetDirectoryName(full), Path.GetDirectoryName(originalReceipt), StringComparison.OrdinalIgnoreCase), "state-not-original-receipt-sibling");
            Require(PinnedPath is null || string.Equals(PinnedPath, full, StringComparison.OrdinalIgnoreCase), "state-sequence-path-changed");
            for (var d = Path.GetDirectoryName(full); d is not null; d = Path.GetDirectoryName(d))
                Require((File.GetAttributes(d) & FileAttributes.ReparsePoint) == 0, "state-parent-reparse-rejected");
            return full;
        }
        private static FileStream Open(string full, out string identity)
        {
            // OPEN_EXISTING, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ and OPEN_REPARSE_POINT.
            // A second writer fails its open. There is no separate reader or replace/rename window.
            var handle = CreateFileW(full, 0xC0000000, FileShare.Read, IntPtr.Zero, 3, 0x00200080, IntPtr.Zero);
            try
            {
                Require(!handle.IsInvalid, "state-open-failed");
                var info = default(FileInfo);
                Require(GetFileType(handle) == 1 && GetFileInformationByHandle(handle, out info), "state-file-info-failed");
                Require((info.Attributes & (0x400u | 0x10u)) == 0 && info.Links == 1, "state-not-single-link-ordinary-file");
                var final = new StringBuilder(32768);
                var count = GetFinalPathNameByHandleW(handle, final, (uint)final.Capacity, 0);
                Require(count > 0 && count < final.Capacity, "state-final-path-unavailable");
                var actual = final.ToString();
                if (actual.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) actual = @"\\" + actual[8..];
                else if (actual.StartsWith(@"\\?\", StringComparison.Ordinal)) actual = actual[4..];
                Require(string.Equals(actual, full, StringComparison.OrdinalIgnoreCase), "state-final-path-mismatch");
                identity = info.Volume + ":" + info.IndexHigh + ":" + info.IndexLow;
                Require(PinnedFile is null || PinnedFile == identity, "state-file-identity-changed");
                return new FileStream(handle, FileAccess.ReadWrite);
            }
            catch { handle.Dispose(); throw; }
        }
        private static string[] Paths(JsonElement root, string key)
        {
            var values = root.GetProperty(key);
            Require(values.ValueKind == JsonValueKind.Array && values.GetArrayLength() <= 256, "state-path-array-invalid");
            var result = new List<string>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var item in values.EnumerateArray())
            {
                Require(item.ValueKind == JsonValueKind.String, "state-path-type-invalid");
                var value = item.GetString()!;
                Require(Ordinary(value) && seen.Add(value), "state-path-value-invalid-or-duplicate");
                result.Add(value);
            }
            return result.ToArray();
        }
        private static State Read(FileStream stream, out string declarations)
        {
            Require(stream.Length > 0 && stream.Length <= MaxBytes, "state-size-invalid");
            var raw = new byte[(int)stream.Length];
            stream.Position = 0;
            var read = 0;
            while (read < raw.Length) { var count = stream.Read(raw, read, raw.Length - read); Require(count > 0, "state-short-read"); read += count; }
            Require(stream.ReadByte() == -1, "state-size-changed");
            Require(!(raw.Length >= 3 && raw[0] == 239 && raw[1] == 187 && raw[2] == 191), "state-bom-rejected");
            using var document = JsonDocument.Parse(Utf8.GetString(raw), new JsonDocumentOptions { MaxDepth = 16 });
            var root = document.RootElement;
            Require(root.ValueKind == JsonValueKind.Object, "state-object-required");
            var names = new HashSet<string>(StringComparer.Ordinal) { "schema", "allowedDevices", "allowedApplications", "hidden", "applications", "cloaking", "commands", "cycles" };
            foreach (var property in root.EnumerateObject()) Require(names.Remove(property.Name), "state-unknown-or-duplicate-property");
            Require(names.Count == 0 && root.GetProperty("schema").ValueKind == JsonValueKind.Number && root.GetProperty("schema").TryGetUInt32(out var schema) && schema == 1, "state-schema-invalid");
            var state = new State { AllowedDevices = Paths(root, "allowedDevices"), AllowedApplications = Paths(root, "allowedApplications"), Hidden = Paths(root, "hidden").ToList(), Applications = Paths(root, "applications").ToList() };
            Require(state.AllowedDevices.Length > 0 && state.AllowedApplications.Length > 0, "state-declarations-empty");
            declarations = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new { state.AllowedDevices, state.AllowedApplications })));
            Require(PinnedDeclarations is null || declarations == PinnedDeclarations, "state-declarations-changed");
            Require(state.Hidden.All(x => state.AllowedDevices.Contains(x, StringComparer.OrdinalIgnoreCase)) && state.Applications.All(x => state.AllowedApplications.Contains(x, StringComparer.OrdinalIgnoreCase)), "state-list-outside-declarations");
            var cloak = root.GetProperty("cloaking");
            Require(cloak.ValueKind is JsonValueKind.True or JsonValueKind.False, "state-cloaking-invalid");
            state.Cloaking = cloak.GetBoolean();
            Require(root.GetProperty("cycles").ValueKind == JsonValueKind.Number && root.GetProperty("cycles").TryGetUInt32(out state.Cycles) && state.Cycles <= 4096, "state-cycles-invalid");
            var commands = root.GetProperty("commands");
            Require(commands.ValueKind == JsonValueKind.Array && commands.GetArrayLength() <= MaxCommands, "state-command-log-invalid");
            foreach (var item in commands.EnumerateArray()) { Require(item.ValueKind == JsonValueKind.String, "state-command-type-invalid"); state.Commands.Add(item.GetString()!); }
            return state;
        }
        private static string Target(string value, string[] allowed)
        {
            Require(Ordinary(value), "command-path-invalid");
            var canonical = allowed.FirstOrDefault(x => string.Equals(x, value, StringComparison.OrdinalIgnoreCase));
            Require(canonical is not null, "command-path-not-declared");
            return canonical!;
        }
        private static void Update(List<string> values, string value, bool add)
        {
            var index = values.FindIndex(x => string.Equals(x, value, StringComparison.OrdinalIgnoreCase));
            if (add && index < 0) values.Add(value); else if (!add && index >= 0) values.RemoveAt(index);
        }
        private static void CycleRoot(string value)
        {
            Require(string.Equals(value, @"USB\VID_0DB0&PID_1901\MSI_POLICY_RETURN_BUILTIN", StringComparison.OrdinalIgnoreCase) || string.Equals(value, @"USB\VID_0DB0&PID_1902\MSI_POLICY_RETURN_BUILTIN", StringComparison.OrdinalIgnoreCase), "cycle-root-not-builtin");
        }
        private static (Cycle? Binding, string State, string Detail) LatestCycle(State state)
        {
            Cycle? binding = null; var result = "idle"; var detail = "";
            for (var i = 0; i < state.Commands.Count; ++i)
            {
                var command = state.Commands[i];
                if (command.StartsWith(BeginPrefix, StringComparison.Ordinal))
                {
                    binding = JsonSerializer.Deserialize<Cycle>(command[BeginPrefix.Length..]);
                    Require(binding is not null && binding.requestIndex == i && Ordinary(binding.path), "cycle-begin-log-invalid");
                    CycleRoot(binding!.path); result = "in-flight"; detail = "fixture-signal-pending";
                }
                else if (command.StartsWith(SignalPrefix, StringComparison.Ordinal))
                {
                    using var signal = JsonDocument.Parse(command[SignalPrefix.Length..]);
                    var value = signal.RootElement;
                    Require(value.ValueKind == JsonValueKind.Object && value.EnumerateObject().Count() == 7, "cycle-signal-shape-invalid");
                    if (binding is null || value.GetProperty("requestIndex").GetInt32() != binding.requestIndex || value.GetProperty("path").GetString() != binding.path || value.GetProperty("runId").GetString() != binding.runId || value.GetProperty("epoch").GetUInt64() != binding.epoch || value.GetProperty("powerGeneration").GetUInt64() != binding.powerGeneration) continue;
                    var next = value.GetProperty("state").GetString();
                    Require(next is "done" or "failed" or "cancelled", "cycle-signal-state-invalid");
                    Require(result == "in-flight", "cycle-signal-duplicate-terminal");
                    result = next!; detail = value.GetProperty("detail").GetString() ?? "fixture-signal-only";
                }
            }
            return (binding, result, detail);
        }
        internal static Reply Execute(string command, string path, bool cloak, string? receipt, string runId, ulong epoch, ulong powerGeneration, string phase)
        {
            lock (Gate)
            {
                try
                {
                    Require(command is "HIDHIDE_HIDE" or "HIDHIDE_UNHIDE" or "HIDHIDE_APP_REG" or "HIDHIDE_APP_UNREG" or "HIDHIDE_CLOAK" or "HIDHIDE_CLOAK_OFF" or "HIDHIDE_QUERY" or "HIDHIDE_QUERY_STATE" or "HIDHIDE_CYCLE" or "HIDHIDE_CYCLE_BEGIN" or "HIDHIDE_CYCLE_POLL", "unsupported-hidhide-command");
                    var full = StatePath(receipt);
                    using var stream = Open(full, out var identity);
                    var state = Read(stream, out var declarations);
                    Require(state.Commands.Count < MaxCommands, "command-limit");
                    var log = "managed:" + command;
                    var status = ""; var detail = "true";
                    switch (command)
                    {
                        case "HIDHIDE_HIDE": case "HIDHIDE_UNHIDE":
                            path = Target(path, state.AllowedDevices); Update(state.Hidden, path, command == "HIDHIDE_HIDE"); status = command == "HIDHIDE_HIDE" ? "hidhide-hidden" : "hidhide-unhidden"; log += " " + JsonSerializer.Serialize(path); break;
                        case "HIDHIDE_APP_REG": case "HIDHIDE_APP_UNREG":
                            path = Target(path, state.AllowedApplications); Update(state.Applications, path, command == "HIDHIDE_APP_REG"); status = command == "HIDHIDE_APP_REG" ? "hidhide-app-registered" : "hidhide-app-unregistered"; log += " " + JsonSerializer.Serialize(path); break;
                        case "HIDHIDE_CLOAK": case "HIDHIDE_CLOAK_OFF":
                            Require(string.IsNullOrEmpty(path), "unexpected-command-path"); state.Cloaking = command == "HIDHIDE_CLOAK" && cloak; status = state.Cloaking ? "hidhide-cloak-on" : "hidhide-cloak-off"; log += state.Cloaking ? " true" : " false"; break;
                        case "HIDHIDE_QUERY":
                            Require(string.IsNullOrEmpty(path), "unexpected-command-path"); status = "hidhide-query"; detail = JsonSerializer.Serialize(new { ok = true, hidden = state.Hidden, applications = state.Applications, cloakOn = state.Cloaking, inverse = "off", queryAttempts = 1 }); break;
                        case "HIDHIDE_QUERY_STATE":
                            Require(string.IsNullOrEmpty(path), "unexpected-command-path"); status = "hidhide-query-state"; detail = JsonSerializer.Serialize(new { ok = true, hidden = state.Hidden, cloakOn = state.Cloaking }); break;
                        case "HIDHIDE_CYCLE":
                            path = Target(path, state.AllowedDevices); CycleRoot(path); Require(state.Cycles < 4096, "cycle-limit"); ++state.Cycles; status = "hidhide-cycle"; log += " " + JsonSerializer.Serialize(path); break;
                        case "HIDHIDE_CYCLE_BEGIN":
                        {
                            path = Target(path, state.AllowedDevices); CycleRoot(path); var prior = LatestCycle(state); status = "hidhide-cycle-begin";
                            if (prior.State == "in-flight") { detail = "{\"accepted\":false,\"reason\":\"in-flight\"}"; log = "managed:HIDHIDE_CYCLE_BEGIN_REJECTED in-flight"; }
                            else { Require(state.Cycles < 4096, "cycle-limit"); ++state.Cycles; var binding = new Cycle(path, runId, epoch, powerGeneration, state.Commands.Count); log = BeginPrefix + JsonSerializer.Serialize(binding); detail = JsonSerializer.Serialize(new { accepted = true, mode = "controlled-fixture-signal", runId, epoch, powerGeneration, physicalReturnProven = false, businessSettlementProven = false }); }
                            break;
                        }
                        case "HIDHIDE_CYCLE_POLL":
                        {
                            Require(string.IsNullOrEmpty(path), "unexpected-command-path"); var current = LatestCycle(state); var pending = current.State == "in-flight"; status = "hidhide-cycle-poll";
                            detail = JsonSerializer.Serialize(new { state = current.State, ok = current.State == "done", inFlight = pending, retained = pending, phase, resourceDisposition = pending ? "owned-pending" : "settled", detail = current.Detail, elapsedMs = 0, totalMs = 0, path = current.Binding?.path ?? "", runId = current.Binding?.runId ?? "", epoch = current.Binding?.epoch ?? 0UL, powerGeneration = current.Binding?.powerGeneration ?? 0UL, scope = "controlled-fixture-signal-only", physicalReturnProven = false, businessSettlementProven = false }); break;
                        }
                    }
                    state.Commands.Add(log);
                    var bytes = JsonSerializer.SerializeToUtf8Bytes(new { schema = 1, allowedDevices = state.AllowedDevices, allowedApplications = state.AllowedApplications, hidden = state.Hidden, applications = state.Applications, cloaking = state.Cloaking, commands = state.Commands, cycles = state.Cycles });
                    Require(bytes.Length <= MaxBytes, "result-size-limit");
                    stream.Position = 0; stream.Write(bytes, 0, bytes.Length); stream.SetLength(bytes.Length); stream.Flush(true);
                    PinnedPath ??= full; PinnedFile ??= identity; PinnedDeclarations ??= declarations;
                    return new Reply(true, status, detail);
                }
                catch (Exception error) { return new Reply(false, "fixture-hidhide-rejected", error is InvalidDataException ? error.Message : "state-io-or-json-failed:" + error.GetType().Name); }
            }
        }
    }
}
