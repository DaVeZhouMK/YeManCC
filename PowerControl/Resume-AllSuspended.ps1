
# Resume-AllSuspended.ps1
# SCAN-BASED: resumes ALL processes with WorkingSet > 500MB (non-system),
# WITHOUT any marker file. Performs ONE WMI snapshot, then resumes each
# eligible process. Designed to pair with Suspend-LargestGame.ps1.
# Resuming a process that is already running is a harmless no-op
# (NtResumeProcess returns success and does nothing).

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class NtApi {
    [DllImport("ntdll.dll")]    public static extern int  NtSuspendProcess(IntPtr h);
    [DllImport("ntdll.dll")]    public static extern int  NtResumeProcess(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@

$MIN_MB    = 500
$MIN_BYTES = [int64]$MIN_MB * 1024 * 1024

$SYS_BLACKLIST = @(
    'system','idle','smss.exe','csrss.exe','wininit.exe','winlogon.exe',
    'services.exe','lsass.exe','lsm.exe','svchost.exe','dwm.exe',
    'explorer.exe','conhost.exe','audiodg.exe','wmiprvse.exe',
    'powershell.exe','cmd.exe','wscript.exe','cscript.exe',
    'yemancc.exe','searchhost.exe','shellexperiencehost.exe',
    'runtimebroker.exe','applicationframehost.exe','ctfmon.exe',
    'textinputhost.exe','sihost.exe','fontdrvhost.exe','lsaiso.exe',
    'secure_system','registry','memory compression'
)

# ONE WMI snapshot - the only expensive call
$procs = Get-CimInstance Win32_Process

$targets = @()
foreach ($p in $procs) {
    if ($null -eq $p.WorkingSetSize) { continue }
    if ($p.WorkingSetSize -lt $MIN_BYTES) { continue }
    $nm = $p.Name.ToLower()
    if ($SYS_BLACKLIST -contains $nm) { continue }
    if ($nm.StartsWith('steam'))    { continue }
    if ($nm.StartsWith('playnite')) { continue }
    $targets += [PSCustomObject]@{ Pid = $p.ProcessId; Name = $p.Name }
}

if ($targets.Count -eq 0) {
    Write-Host 'No process >500MB to resume.' -ForegroundColor Yellow
    exit 0
}

$ok = 0; $fail = 0
foreach ($t in $targets) {
    $h = [NtApi]::OpenProcess(0x800, $false, [int]$t.Pid)
    if ($h -eq [IntPtr]::Zero) { $fail++; continue }
    $r = [NtApi]::NtResumeProcess($h)
    [NtApi]::CloseHandle($h)
    if ($r -eq 0) { $ok++ } else { $fail++ }
}

Write-Host ("Resumed: {0} process(es) (targeted {1}, skipped {2})" -f $ok, $targets.Count, $fail) -ForegroundColor Green
