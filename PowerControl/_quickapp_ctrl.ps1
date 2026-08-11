Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class NtApi {
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
  [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@
param([int]$RootPid)
$ErrorActionPreference = 'Stop'
$procs = Get-CimInstance Win32_Process
$map = @{}
foreach ($p in $procs) {
  $pp = [int]$p.ParentProcessId
  if (-not $map.ContainsKey($pp)) { $map[$pp] = New-Object System.Collections.Generic.List[int] }
  $map[$pp].Add([int]$p.ProcessId)
}
$set = New-Object System.Collections.Generic.HashSet[int]
$q = New-Object System.Collections.Queue
$q.Enqueue($RootPid)
while ($q.Count -gt 0) {
  $cur = $q.Dequeue()
  if ($set.Contains($cur)) { continue }
  $set.Add($cur) | Out-Null
  if ($map.ContainsKey($cur)) {
    foreach ($c in $map[$cur]) { if (-not $set.Contains($c)) { $q.Enqueue($c) } }
  }
}
$ok = 0
$fail = 0
foreach ($p in $set) {
  $h = [NtApi]::OpenProcess(0x800, $false, [int]$p)
  if ($h -eq [IntPtr]::Zero) { $fail++; continue }
  $r = [NtApi]::NtSuspendProcess($h)
  [NtApi]::CloseHandle($h)
  if ($r -eq 0) { $ok++ } else { $fail++ }
}
$pidsOut = ($set | ForEach-Object { $_ }) -join ','
Write-Output ("SUSPENDED " + $ok + " " + $fail)
Write-Output ("PIDS " + $pidsOut)
