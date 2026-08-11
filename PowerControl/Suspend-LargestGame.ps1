
# Suspend-LargestGame.ps1
# Pure PowerShell - suspends the LARGEST-memory process (>500MB),
# excluding system processes, Steam* and Playnite* families.
# NO marker file: the resume counterpart scans and resumes all >500MB
# processes directly, so no state tracking is needed.

$ErrorActionPreference = 'Stop'

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

# ONE WMI snapshot - collect candidates
$cands = @()
foreach ($p in (Get-CimInstance Win32_Process)) {
    if ($null -eq $p.WorkingSetSize) { continue }
    if ($p.WorkingSetSize -lt $MIN_BYTES) { continue }
    $nm = $p.Name.ToLower()
    if ($SYS_BLACKLIST -contains $nm) { continue }
    if ($nm.StartsWith('steam'))    { continue }
    if ($nm.StartsWith('playnite')) { continue }
    $cands += [PSCustomObject]@{ Pid = $p.ProcessId; Name = $p.Name; WS = $p.WorkingSetSize }
}

if ($cands.Count -eq 0) {
    Write-Host 'No suspendable target found (>500MB, non-excluded, accessible).' -ForegroundColor Yellow
    exit 0
}

# largest first
$cands = $cands | Sort-Object WS -Descending

$target = $null
foreach ($c in $cands) {
    $h = [NtApi]::OpenProcess(0x800, $false, [int]$c.Pid)
    if ($h -eq [IntPtr]::Zero) { continue }          # no access -> skip
    $r = [NtApi]::NtSuspendProcess($h)
    [NtApi]::CloseHandle($h)
    if ($r -eq 0) { $target = $c; break }
}

if ($null -eq $target) {
    Write-Host 'All candidates have no access (system/other-user). Run elevated.' -ForegroundColor Red
    exit 0
}

Write-Host ("Suspended: {0} (PID {1})" -f $target.Name, $target.Pid) -ForegroundColor Green
Write-Host ("WorkingSet ~ {0} MB" -f [math]::Round($target.WS / 1048576)) -ForegroundColor Cyan
Write-Host ("Processes below {0} MB were ignored." -f $MIN_MB)
