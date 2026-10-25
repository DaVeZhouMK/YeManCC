using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;

// Compatibility for the public 035 helper, which intentionally skipped the
// input directories. Prepare file-only payload AFTER its YMCC commit, outside
// the UI thread and before any SDK preparation. All waits have finite budgets.
internal static class ModulePayloadBootstrap
{
    internal static bool Current(string root)
    {
        string manifest = Path.Combine(root, "YeManCC", "module-package.json");
        if (!File.Exists(manifest)) return true; // source/dev or historical package
        if (new FileInfo(manifest).Length > 4_194_304) throw new IOException("Module manifest too large");
        using var doc = JsonDocument.Parse(File.ReadAllText(manifest));
        int count = 0;
        foreach (var entry in doc.RootElement.GetProperty("files").EnumerateArray()) {
            if (++count > 10000) throw new IOException("Module file count exceeds budget");
            string relative = entry.GetProperty("path").GetString() ?? "";
            if ((!relative.StartsWith("feature-assets/virtual-gamepad/", StringComparison.Ordinal) && !relative.StartsWith("feature-assets/gyro-motion/", StringComparison.Ordinal)) || relative.Contains("..") || relative.Contains('\\') || relative.Contains(':')) throw new IOException("Invalid module path");
            string file = Path.Combine(root, "PowerControl", relative.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(file) || new FileInfo(file).Length != entry.GetProperty("bytes").GetInt64()) return false;
            for (var part = new FileInfo(file) as FileSystemInfo; part is not null; part = part is FileInfo f ? f.Directory : ((DirectoryInfo)part).Parent)
                if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Redirected module path");
            using var input = File.OpenRead(file);
            if (!Convert.ToHexString(SHA256.HashData(input)).Equals(entry.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase)) return false;
        }
        return count >= 6;
    }
    internal static bool ReceiptCurrent(string root)
    {
        try {
            string expected=Path.Combine(root,"YeManCC","module-package.json");
            if (!File.Exists(expected)) return true;
            string receipt=Path.Combine(root,"PowerControl",".ymcc-input-managed.json");
            if (!File.Exists(receipt) || new FileInfo(receipt).Length>4_194_304 || (File.GetAttributes(receipt)&FileAttributes.ReparsePoint)!=0) return false;
            var manifest=System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(expected));
            var installed=System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(receipt));
            return manifest is not null && System.Text.Json.Nodes.JsonNode.DeepEquals(manifest,installed);
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or ArgumentException or InvalidOperationException) { return false; }
    }
    internal static bool HasInterruptedStages(string root)
    {
        string target = Path.GetFullPath(Path.Combine(root, "PowerControl"));
        try {
            foreach (string stage in Directory.EnumerateDirectories(root, "module-bootstrap-*", SearchOption.TopDirectoryOnly).Take(64)) {
                string name=Path.GetFileName(stage);
                if (!System.Text.RegularExpressions.Regex.IsMatch(name, "^module-bootstrap-[a-f0-9]{32}$") || (File.GetAttributes(stage)&FileAttributes.ReparsePoint)!=0) continue;
                string marker=Path.Combine(stage,".ymcc-bootstrap-stage.json");
                if (!File.Exists(marker) || new FileInfo(marker).Length>4_194_304 || (File.GetAttributes(marker)&FileAttributes.ReparsePoint)!=0) continue;
                try {
                    using var doc=JsonDocument.Parse(File.ReadAllText(marker));
                    var state=doc.RootElement;
                    if (state.GetProperty("schemaVersion").GetInt32()==2 && state.GetProperty("owner").GetString()=="YMCC.InputPayloadBootstrap" &&
                        string.Equals(state.GetProperty("target").GetString(),target,StringComparison.OrdinalIgnoreCase) &&
                        state.GetProperty("phase").GetString() is "preparing" or "applying" or "committed") return true;
                } catch (Exception ex) when (ex is IOException or JsonException or KeyNotFoundException or InvalidOperationException) { }
            }
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        return false;
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
                try {
                    using var state = JsonDocument.Parse(File.ReadAllText(commitState));
                    if (state.RootElement.GetProperty("phase").GetString() == "committed" && state.RootElement.GetProperty("pid").GetInt32() == ownerPid) break;
                } catch (Exception ex) when (ex is IOException or JsonException or KeyNotFoundException) { }
                if (deadline.Elapsed > TimeSpan.FromSeconds(35)) throw new TimeoutException("YMCC update commit was not confirmed; input payload left untouched");
                Thread.Sleep(100);
            }
        }
        // A confirmed full update also retires superseded HC directories.
        if (string.IsNullOrEmpty(commitState) && Current(root) && ReceiptCurrent(root) && !HasInterruptedStages(root)) return;
        if (!ownerAlive()) throw new IOException("YMCC exited before module payload preparation");
        string script = Path.Combine(root, "PowerControl", "redist", "ymcc-input-payload-apply.ps1");
        using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "YeManCC", "module-package.json")));
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
        if (process.ExitCode != 0 || !Current(root)) {
            // A descendant can retain a redirected pipe after the helper exits.
            // Diagnostics must never outlive the install wait budget indefinitely.
            string detail=FinishDiagnosticRead(errors);
            diagnosticsCancellation.Cancel();
            throw new IOException("Module file preparation incomplete (exit="+process.ExitCode+"); originals/backups retained for retry: "+detail[..Math.Min(detail.Length,1200)]);
        }
        diagnosticsCancellation.Cancel();
    }
}
