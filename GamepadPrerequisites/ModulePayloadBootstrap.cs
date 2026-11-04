using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;

// Compatibility for the public 035 helper, which intentionally skipped the
// input directories. Prepare file-only payload AFTER its YMCC commit, outside
// the UI thread and before any SDK preparation. All waits have finite budgets.
internal static class ModulePayloadBootstrap
{
    private const int JournalMaxBytes = 4_194_304;
    private static readonly string[] RequiredFiles = ["YeManInputHost.exe", "YeManInputHost.dll", "YeManInputHost.deps.json", "YeManInputHost.runtimeconfig.json", "HIDMaestro.Core.dll"];
    private static bool Exists(string path)
    {
        try { _ = File.GetAttributes(path); return true; }
        catch (FileNotFoundException) { return false; }
        catch (DirectoryNotFoundException) { return false; }
    }
    private static void RegularPath(string path)
    {
        for (string? cursor = Path.GetFullPath(path); cursor is not null; cursor = Path.GetDirectoryName(cursor))
            if ((File.GetAttributes(cursor) & FileAttributes.ReparsePoint) != 0) throw new IOException("Redirected module journal path");
    }
    private static JsonDocument ReadJournal(string path)
    {
        RegularPath(path);
        if ((File.GetAttributes(path) & FileAttributes.Directory) != 0) throw new IOException("Module journal is a directory");
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (input.Length == 0 || input.Length > JournalMaxBytes) throw new IOException("Module journal empty or too large");
        using var text = new StreamReader(input, new UTF8Encoding(false, true), true);
        var doc = JsonDocument.Parse(text.ReadToEnd());
        try {
            if (doc.RootElement.ValueKind != JsonValueKind.Object) throw new IOException("Module journal root is not an object");
            ValidateKeys(doc.RootElement);
            return doc;
        } catch { doc.Dispose(); throw; }
    }
    private static void ValidateKeys(JsonElement node)
    {
        if (node.ValueKind == JsonValueKind.Object) {
            var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var property in node.EnumerateObject()) {
                if (!keys.Add(property.Name)) throw new IOException("Duplicate module journal field");
                ValidateKeys(property.Value);
            }
        } else if (node.ValueKind == JsonValueKind.Array) foreach (var item in node.EnumerateArray()) ValidateKeys(item);
    }
    private static string Text(JsonElement node, string key)
    {
        var value = node.GetProperty(key);
        return value.ValueKind == JsonValueKind.String ? value.GetString()! : throw new IOException("Module journal field is not text: " + key);
    }
    private static long Integer(JsonElement node, string key)
    {
        var value = node.GetProperty(key);
        return value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out long number) ? number : throw new IOException("Module journal field is not an integer: " + key);
    }
    private static bool Hash(string value) => System.Text.RegularExpressions.Regex.IsMatch(value, "^[a-fA-F0-9]{64}$");
    private static bool OwnedPath(string value) => System.Text.RegularExpressions.Regex.IsMatch(value, @"^feature-assets/(virtual-gamepad|gyro-motion)/[A-Za-z0-9_-][A-Za-z0-9_./-]*$") &&
        !System.Text.RegularExpressions.Regex.IsMatch(value, @"(^|/)(\.\.?|logs?|cache|settings|user-data)(/|$)|\.(log|tmp|bak)$") && !value.Contains("//");
    private static JsonDocument ReadManifest(string root)
    {
        var doc = ReadJournal(Path.Combine(root, "YeManCC", "module-package.json"));
        try { ValidateManifest(doc.RootElement); return doc; } catch { doc.Dispose(); throw; }
    }
    private static void ValidateManifest(JsonElement manifest)
    {
        if (Integer(manifest, "schemaVersion") != 1 || Text(manifest, "owner") != "YMCC" || string.IsNullOrWhiteSpace(Text(manifest, "packageVersion"))) throw new IOException("Module manifest identity invalid");
        var files = manifest.GetProperty("files");
        if (files.ValueKind != JsonValueKind.Array || files.GetArrayLength() is < 6 or > 10000) throw new IOException("Module manifest file inventory invalid");
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase); var rows = new List<string>(); bool gyro = false;
        foreach (var entry in files.EnumerateArray()) {
            string path = Text(entry, "path"), hash = Text(entry, "sha256"); long bytes = Integer(entry, "bytes");
            if (!OwnedPath(path) || !seen.Add(path) || !Hash(hash) || bytes is < 0 or > 2147483648) throw new IOException("Module manifest file invalid");
            gyro |= path.StartsWith("feature-assets/gyro-motion/", StringComparison.Ordinal);
            rows.Add(path + "|" + bytes.ToString(System.Globalization.CultureInfo.InvariantCulture) + "|" + hash);
        }
        if (!gyro || RequiredFiles.Any(name => !seen.Contains("feature-assets/virtual-gamepad/" + name))) throw new IOException("Module manifest required file missing");
        if (manifest.TryGetProperty("compatibility", out var compatibility)) {
            if (compatibility.ValueKind != JsonValueKind.Object || Text(compatibility, "sourceKind") != "full-package-v1" || Text(compatibility, "payloadHashScope") != "manifest-files-v1" ||
                !Hash(Text(compatibility, "scriptSha256")) || compatibility.GetProperty("applyAfterYmccCommit").ValueKind != JsonValueKind.True) throw new IOException("Module compatibility identity invalid");
            rows.Sort(StringComparer.Ordinal);
            string expected = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(string.Join("\n", rows))));
            if (!Hash(Text(compatibility, "payloadSha256")) || !expected.Equals(Text(compatibility, "payloadSha256"), StringComparison.OrdinalIgnoreCase)) throw new IOException("Module payload identity mismatch");
        }
    }
    private static string? NullableHash(JsonElement record, string name)
    {
        var value = record.GetProperty(name);
        if (value.ValueKind == JsonValueKind.Null) return null;
        string text = Text(record, name);
        return Hash(text) ? text : throw new IOException("Module rollback hash invalid");
    }
    private static string? ExistingHash(string path)
    {
        if (!Exists(path)) return null;
        RegularPath(path);
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(SHA256.HashData(input));
    }
    private static bool InventoryEqual(JsonElement left, JsonElement right)
    {
        var files = left.GetProperty("files").EnumerateArray().ToDictionary(f => Text(f, "path"), f => f, StringComparer.OrdinalIgnoreCase);
        var incoming = right.GetProperty("files");
        return files.Count == incoming.GetArrayLength() && incoming.EnumerateArray().All(f => files.TryGetValue(Text(f, "path"), out var old) && Integer(old, "bytes") == Integer(f, "bytes") && Text(old, "sha256").Equals(Text(f, "sha256"), StringComparison.OrdinalIgnoreCase));
    }
    private static bool ManifestIdentityEqual(JsonElement left, JsonElement right)
    {
        if (Text(left, "packageVersion") != Text(right, "packageVersion") || !InventoryEqual(left, right)) return false;
        bool first = left.TryGetProperty("compatibility", out var a), second = right.TryGetProperty("compatibility", out var b);
        if (first != second) return false;
        return !first || new[] { "sourceKind", "payloadHashScope", "payloadSha256", "scriptSha256" }.All(k => Text(a, k).Equals(Text(b, k), StringComparison.OrdinalIgnoreCase));
    }
    private static void ValidateStageRecords(string root, JsonElement manifest)
    {
        string target = Path.GetFullPath(Path.Combine(root, "PowerControl")); int count = 0;
        foreach (string stage in Directory.EnumerateFileSystemEntries(root, "module-bootstrap-*", SearchOption.TopDirectoryOnly)) {
            if (++count > 64 || !System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(stage), "^module-bootstrap-[a-f0-9]{32}$")) throw new IOException("Module staging identity unconfirmed");
            RegularPath(stage);
            using var doc = ReadJournal(Path.Combine(stage, ".ymcc-bootstrap-stage.json")); var record = doc.RootElement;
            if (Integer(record, "schemaVersion") != 2 || Text(record, "owner") != "YMCC.InputPayloadBootstrap" ||
                !Path.TrimEndingDirectorySeparator(Path.GetFullPath(Text(record, "target"))).Equals(target, StringComparison.OrdinalIgnoreCase) ||
                !Hash(Text(record, "payloadSha256")) || string.IsNullOrWhiteSpace(Text(record, "packageVersion")) || Text(record, "phase") is not ("preparing" or "applying" or "committed")) throw new IOException("Module staging owner unconfirmed");
            var paths = record.GetProperty("managedPaths"); var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (paths.ValueKind != JsonValueKind.Array || paths.GetArrayLength() is < 1 or > 10000) throw new IOException("Module staging inventory invalid");
            foreach (var item in paths.EnumerateArray())
                if (item.ValueKind != JsonValueKind.String || !OwnedPath(item.GetString()!) || !seen.Add(item.GetString()!)) throw new IOException("Module staging path invalid");
            if (manifest.TryGetProperty("compatibility", out var compatibility) && Text(record, "packageVersion") == Text(manifest, "packageVersion") &&
                !Text(record, "payloadSha256").Equals(Text(compatibility, "payloadSha256"), StringComparison.OrdinalIgnoreCase)) throw new IOException("Module staging payload identity mismatch");
            if (record.TryGetProperty("postCommitCleanup", out var cleanup) &&
                (Text(record, "phase") != "committed" || Integer(cleanup, "schemaVersion") != 1 || !Hash(Text(cleanup, "moduleManifestSha256")) ||
                 (cleanup.TryGetProperty("settled", out var settled) && settled.ValueKind is not (JsonValueKind.True or JsonValueKind.False)))) throw new IOException("Module staging cleanup authority unconfirmed");
            if (record.TryGetProperty("stageId", out _) && Text(record, "stageId") != Path.GetFileName(stage)) throw new IOException("Module staging directory identity changed");
            bool cleanupConfirmed = false;
            if (record.TryGetProperty("cleanupProof", out var cleanupProof)) {
                if (Integer(cleanupProof, "schemaVersion") != 1 || !Hash(Text(cleanupProof, "moduleManifestSha256")) || !Hash(Text(cleanupProof, "managedReceiptSha256"))) throw new IOException("Module cleanup proof invalid");
                cleanupConfirmed = Text(cleanupProof, "moduleManifestSha256").Equals(ExistingHash(Path.Combine(root, "YeManCC", "module-package.json")), StringComparison.OrdinalIgnoreCase) &&
                    Text(cleanupProof, "managedReceiptSha256").Equals(ExistingHash(Path.Combine(target, ".ymcc-input-managed.json")), StringComparison.OrdinalIgnoreCase);
                if (!cleanupConfirmed) throw new IOException("Module cleanup proof identity changed");
            }
            if (record.TryGetProperty("rollbackLedger", out var ledger)) {
                var entries = ledger.GetProperty("entries"); var known = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                if (Integer(ledger, "schemaVersion") != 1 || entries.ValueKind != JsonValueKind.Array || entries.GetArrayLength() > 10001 || Text(record, "phase") == "preparing") throw new IOException("Module rollback ledger invalid");
                foreach (var entry in entries.EnumerateArray()) {
                    string relative = Text(entry, "path"); string? before = NullableHash(entry, "before"), after = NullableHash(entry, "after");
                    var remove = entry.GetProperty("remove");
                    if ((relative != ".ymcc-input-managed.json" && !seen.Contains(relative)) || !known.Add(relative) || remove.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
                        (remove.ValueKind == JsonValueKind.True ? after is not null || before is null : after is null)) throw new IOException("Module rollback entry invalid");
                    if (before is not null && !cleanupConfirmed) {
                        string backup = Path.Combine(stage, "rollback", relative.Replace('/', Path.DirectorySeparatorChar));
                        string? saved = ExistingHash(backup);
                        if (saved is not null ? !saved.Equals(before, StringComparison.OrdinalIgnoreCase) : !string.Equals(ExistingHash(Path.Combine(target, relative.Replace('/', Path.DirectorySeparatorChar))), before, StringComparison.OrdinalIgnoreCase)) throw new IOException("Module rollback original unconfirmed");
                    }
                }
            }
        }
    }
    private static void ValidateExistingReceipt(string root)
    {
        string path = Path.Combine(root, "PowerControl", ".ymcc-input-managed.json");
        if (!Exists(path)) return;
        using var receipt = ReadJournal(path); ValidateManifest(receipt.RootElement);
    }
    private static bool JournalFailure(Exception ex) => ex is IOException or UnauthorizedAccessException or JsonException or ArgumentException or InvalidOperationException or KeyNotFoundException or OverflowException;
    private static bool HistoricalRoot(string root) => !Exists(Path.Combine(root, "PowerControl", ".ymcc-input-managed.json")) && !HasInterruptedStages(root);
    internal static bool Current(string root)
    {
        try {
            if (!Exists(Path.Combine(root, "YeManCC", "module-package.json"))) return HistoricalRoot(root);
            using var doc = ReadManifest(root);
            ValidateStageRecords(root, doc.RootElement);
            foreach (var entry in doc.RootElement.GetProperty("files").EnumerateArray()) {
                string file = Path.Combine(root, "PowerControl", Text(entry, "path").Replace('/', Path.DirectorySeparatorChar));
                RegularPath(file);
                using var input = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read);
                if (input.Length != Integer(entry, "bytes") || !Convert.ToHexString(SHA256.HashData(input)).Equals(Text(entry, "sha256"), StringComparison.OrdinalIgnoreCase)) return false;
            }
            return true;
        } catch (Exception ex) when (JournalFailure(ex)) { return false; }
    }
    internal static bool ReceiptCurrent(string root)
    {
        try {
            if (!Exists(Path.Combine(root, "YeManCC", "module-package.json"))) return HistoricalRoot(root);
            if (!Current(root)) return false;
            using var manifest = ReadManifest(root);
            using var receipt = ReadJournal(Path.Combine(root, "PowerControl", ".ymcc-input-managed.json"));
            ValidateManifest(receipt.RootElement);
            return ManifestIdentityEqual(manifest.RootElement, receipt.RootElement);
        } catch (Exception ex) when (JournalFailure(ex)) { return false; }
    }
    internal static bool HasInterruptedStages(string root)
    {
        // Existence is pending debt, never ownership/deletion permission. A bad,
        // inaccessible or nameless marker must not look like an empty history.
        try { return Directory.EnumerateFileSystemEntries(root, "module-bootstrap-*", SearchOption.TopDirectoryOnly).Any(); }
        catch (Exception ex) when (JournalFailure(ex)) { return true; }
    }
    private static bool IsCommitConfirmed(string root, string path, int ownerPid, out bool committedMarker)
    {
        committedMarker = false;
        try {
            if (ownerPid <= 0) return false;
            using var state = ReadJournal(path); var record = state.RootElement;
            committedMarker = Text(record, "phase") == "committed";
            using var manifest = ReadManifest(root);
            if (record.TryGetProperty("schemaVersion", out _) && Integer(record, "schemaVersion") != 1) return false;
            if (record.TryGetProperty("schema", out _) && Integer(record, "schema") != 1) return false;
            if (Text(record, "phase") != "committed" || Integer(record, "pid") != ownerPid || Text(record, "version") != Text(manifest.RootElement, "packageVersion")) return false;
            foreach (string name in new[] { "installRoot", "root", "target" })
                if (record.TryGetProperty(name, out _) && !Path.TrimEndingDirectorySeparator(Path.GetFullPath(Text(record, name))).Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)), StringComparison.OrdinalIgnoreCase)) return false;
            if (record.TryGetProperty("payloadSha256", out _) && !Text(record, "payloadSha256").Equals(Text(manifest.RootElement.GetProperty("compatibility"), "payloadSha256"), StringComparison.OrdinalIgnoreCase)) return false;
            return true;
        } catch (Exception ex) when (JournalFailure(ex)) { return false; }
    }
    internal static async Task<string> ReadDiagnostics(StreamReader reader, CancellationToken cancellation)
    {
        var text=new StringBuilder();char[] buffer=new char[2048];
        try {
            while (true) {
                int count=await reader.ReadAsync(buffer.AsMemory(),cancellation).ConfigureAwait(false);
                if (count==0) break;
                int keep=Math.Min(count,Math.Max(0,16_384-text.Length));
                if (keep>0) text.Append(buffer,0,keep);
            }
        } catch (Exception ex) when (ex is IOException or OperationCanceledException or ObjectDisposedException) { }
        return text.ToString();
    }
    internal static string FinishDiagnosticRead(Task<string> read, int waitMilliseconds=2000)
    {
        var completed=Task.WhenAny(read,Task.Delay(Math.Clamp(waitMilliseconds,1,2000))).GetAwaiter().GetResult();
        return completed==read?read.GetAwaiter().GetResult():"diagnostic pipe remained open";
    }
    internal static void Apply(string root, string commitState, int ownerPid, Func<bool> ownerAlive)
    {
        if (!string.IsNullOrEmpty(commitState)) {
            var deadline = Stopwatch.StartNew();
            while (true) {
                if (!ownerAlive()) throw new IOException("YMCC exited before module payload commit");
                if (IsCommitConfirmed(root, commitState, ownerPid, out bool committedMarker)) break;
                if (committedMarker) throw new IOException("YMCC update commit identity unconfirmed; module payload left untouched");
                if (deadline.Elapsed > TimeSpan.FromSeconds(35)) throw new TimeoutException("YMCC update commit was not confirmed; input payload left untouched");
                Thread.Sleep(100);
            }
        }
        // A confirmed full update also retires superseded HC directories.
        if (string.IsNullOrEmpty(commitState) && Current(root) && ReceiptCurrent(root) && !HasInterruptedStages(root)) return;
        if (!ownerAlive()) throw new IOException("YMCC exited before module payload preparation");
        string script = Path.Combine(root, "PowerControl", "redist", "ymcc-input-payload-apply.ps1");
        using var manifest = ReadManifest(root);
        ValidateStageRecords(root, manifest.RootElement);
        ValidateExistingReceipt(root);
        using (var input = File.OpenRead(script)) {
            if (!Convert.ToHexString(SHA256.HashData(input)).Equals(manifest.RootElement.GetProperty("compatibility").GetProperty("scriptSha256").GetString(), StringComparison.OrdinalIgnoreCase)) throw new IOException("Module bootstrap script identity mismatch");
        }
        // Public035 retains package.zip beside the verified commit state.
        // Normal offline installation can reuse its existing complete package.
        string sourceArchive = string.IsNullOrEmpty(commitState)
            ? Path.Combine(root,"Packages","YeManCC.zip")
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(commitState))!,"package.zip");
        // Keep the sole recovery source readable until the bounded child exits.
        // The updater cache and successor download share this path. A handle
        // without write/delete sharing fences both before PowerShell opens it.
        // A healthy payload may repair only its receipt without an archive.
        using var sourceArchiveLease = File.Exists(sourceArchive)
            ? File.Open(sourceArchive, FileMode.Open, FileAccess.Read, FileShare.Read)
            : null;
        string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe");
        var start = new ProcessStartInfo(powershell) { UseShellExecute=false, CreateNoWindow=true, WorkingDirectory=root, RedirectStandardOutput=true, RedirectStandardError=true, StandardOutputEncoding=Encoding.UTF8, StandardErrorEncoding=Encoding.UTF8 };
        foreach (string arg in new[] { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script, "-InstallRoot", root, "-SourceArchive", sourceArchive }) start.ArgumentList.Add(arg);
        if (!string.IsNullOrEmpty(commitState)) {
            start.ArgumentList.Add("-UpdateCommitState"); start.ArgumentList.Add(commitState);
        }
        start.ArgumentList.Add("-UpdateOwnerPid"); start.ArgumentList.Add(ownerPid.ToString(System.Globalization.CultureInfo.InvariantCulture));
        using var process = Process.Start(start) ?? throw new IOException("Module bootstrap did not start");
        using var diagnosticsCancellation=new CancellationTokenSource();
        var output = ReadDiagnostics(process.StandardOutput,diagnosticsCancellation.Token);
        var errors = ReadDiagnostics(process.StandardError,diagnosticsCancellation.Token);
        if (!process.WaitForExit(60_000)) {
            // This helper performs only atomic file copies, never an MSI/driver transaction.
            if (!process.HasExited) process.Kill();
            process.WaitForExit(5000);
            diagnosticsCancellation.Cancel();
            throw new TimeoutException("Module file preparation exceeded budget; retry on next startup");
        }
        if (process.ExitCode != 0 || !Current(root) || !ReceiptCurrent(root)) {
            // A descendant can retain a redirected pipe after the helper exits.
            // Diagnostics must never outlive the install wait budget indefinitely.
            string detail=FinishDiagnosticRead(errors);
            diagnosticsCancellation.Cancel();
            throw new IOException("Module file preparation incomplete (exit="+process.ExitCode+"); originals/backups retained for retry: "+detail[..Math.Min(detail.Length,1200)]);
        }
        diagnosticsCancellation.Cancel();
    }
}
