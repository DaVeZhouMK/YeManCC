using System.IO;
using System.Diagnostics;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Json;

namespace YeManFanHost;

// A short-lived Windows PowerShell launcher holds the exact predecessor
// handle; it starts no FanHost until that handle is signaled. No new DLL.
internal static class DeferredHostSuccessor
{
    internal static Process Start(string executable, IReadOnlyList<string> arguments, int waitMs)
    {
        var root = Path.Combine(Path.GetTempPath(), "YMCC-Fan-successor-" + Guid.NewGuid().ToString("N"));
        var directory = new DirectoryInfo(root);
        directory.Create();
        var security = new DirectorySecurity();
        security.SetAccessRuleProtection(true, false);
        foreach (var sid in new[] {
            WindowsIdentity.GetCurrent().User!,
            new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null),
            new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null) })
            security.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                PropagationFlags.None, AccessControlType.Allow));
        directory.SetAccessControl(security);
        var file = Path.Combine(root, "launch.json");
        using var predecessor = Process.GetCurrentProcess();
        File.WriteAllText(file, JsonSerializer.Serialize(new {
            predecessorPid = predecessor.Id,
            predecessorStartTicks = predecessor.StartTime.ToUniversalTime().Ticks.ToString(),
            executable, arguments = string.Join(" ", arguments.Select(QuoteWindowsArgument)),
            waitMs = Math.Max(1, waitMs),
            deadline = Environment.TickCount64 + Math.Max(1, waitMs),
        }), new UTF8Encoding(false));
        // The command line contains only the private request path. Tokens stay
        // in its ACL-protected JSON, never in an encoded command or diagnostics.
        var script = " $requestPath = '" + file.Replace("'", "''") + "'\n" + LauncherScript;
        var info = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
            "WindowsPowerShell", "v1.0", "powershell.exe")) { UseShellExecute = false, CreateNoWindow = true };
        foreach (var argument in new[] { "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand",
            Convert.ToBase64String(Encoding.Unicode.GetBytes(script)) }) info.ArgumentList.Add(argument);
        try { return Process.Start(info) ?? throw new InvalidOperationException("successor-launcher-not-created"); }
        catch
        {
            File.Delete(file);
            directory.Delete();
            throw;
        }
    }

    private static string QuoteWindowsArgument(string argument)
    {
        var result = new StringBuilder("\"");
        var slashes = 0;
        foreach (var ch in argument)
        {
            if (ch == '\\') { slashes++; continue; }
            if (ch == '"') { result.Append('\\', slashes * 2 + 1); result.Append(ch); }
            else { result.Append('\\', slashes); result.Append(ch); }
            slashes = 0;
        }
        result.Append('\\', slashes * 2);
        return result.Append('"').ToString();
    }

    private const string LauncherScript = """
$ErrorActionPreference = 'Stop'
$launchGate = $null
$prior = $null
$gateOwned = $false
try {
    $request = Get-Content -LiteralPath $requestPath -Raw | ConvertFrom-Json
    $exited = $false
    try { $prior = [Diagnostics.Process]::GetProcessById([int]$request.predecessorPid) }
    catch [ArgumentException] { $exited = $true }
    if ($null -ne $prior) {
        if ($prior.StartTime.ToUniversalTime().Ticks.ToString() -ne [string]$request.predecessorStartTicks) { exit 73 }
        # Keep this one handle across the wait. Never reopen a recycled PID.
        $null = $prior.Handle
        $exited = $prior.WaitForExit([int]$request.waitMs)
    }
    Add-Type -TypeDefinition 'public static class YMCCFanLauncherClock { [System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern ulong GetTickCount64(); }'
    if (!$exited -or [YMCCFanLauncherClock]::GetTickCount64() -ge [UInt64]$request.deadline) { exit 74 }
    $launchGate = [Threading.Mutex]::new($false, 'Global\YeManFanHost-launch-v1')
    try { $gateOwned = $launchGate.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $gateOwned = $true }
    if (!$gateOwned) { exit 75 }
    # Includes other paths and previous versions. The Host machine gate is a
    # second independent boundary; no duplicate is created as a waiting owner.
    $others = [Diagnostics.Process]::GetProcessesByName('YeManFanHost')
    try { if ($others.Count -ne 0) { exit 76 } }
    finally { foreach ($other in $others) { $other.Dispose() } }
    $info = [Diagnostics.ProcessStartInfo]::new([string]$request.executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.Arguments = [string]$request.arguments
    $child = [Diagnostics.Process]::Start($info)
    if ($null -eq $child) { exit 77 }
    $child.Dispose()
} finally {
    if ($gateOwned) { $launchGate.ReleaseMutex() }
    if ($null -ne $launchGate) { $launchGate.Dispose() }
    if ($null -ne $prior) { $prior.Dispose() }
    # Only this exact request and its now-empty private directory are removed.
    if (Test-Path -LiteralPath $requestPath -PathType Leaf) { Remove-Item -LiteralPath $requestPath -Force }
    $requestDirectory = [IO.Path]::GetFullPath((Split-Path -Parent $requestPath))
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($requestDirectory.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $requestDirectory) -match '^YMCC-Fan-successor-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $requestDirectory -Force
    }
}
""";
}
