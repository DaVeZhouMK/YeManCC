# Compatibility helper for older callers. The native game recognition valve
# owns selection; this script may consume only its one published PID and must
# never rebuild a process tree or choose children by ParentProcessId.
param([int]$RootPid)
$ErrorActionPreference = 'Stop'
$GAME_TARGET = 'C:\SOFT\YeMan\PowerControl\game-target.json'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YmGameValveCompatApi {
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)]
  private static extern bool GetProcessTimes(IntPtr h, out System.Runtime.InteropServices.ComTypes.FILETIME creation, out System.Runtime.InteropServices.ComTypes.FILETIME exit, out System.Runtime.InteropServices.ComTypes.FILETIME kernel, out System.Runtime.InteropServices.ComTypes.FILETIME user);
  public static long ProcessCreated(IntPtr h) {
    System.Runtime.InteropServices.ComTypes.FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(h, out creation, out exit, out kernel, out user)) return 0;
    return ((long)(uint)creation.dwHighDateTime << 32) | (uint)creation.dwLowDateTime;
  }
}
'@

try {
  if (-not (Test-Path -LiteralPath $GAME_TARGET)) {
    Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'; exit 0
  }
  $target = Get-Content -LiteralPath $GAME_TARGET -Raw | ConvertFrom-Json
  $targetPid = [int]$target.pid
  $created = [Int64]$target.processCreated
  $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - [Int64]$target.lastSeen
  if (-not $target.valid -or $targetPid -le 0 -or $created -le 0 -or
      [Int64]$target.generation -le 0 -or $age -lt 0 -or $age -gt 10000 -or
      ($RootPid -gt 0 -and $RootPid -ne $targetPid)) {
    Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'; exit 0
  }
  $h = [YmGameValveCompatApi]::OpenProcess(0x0800 -bor 0x1000, $false, $targetPid)
  if ($h -eq [IntPtr]::Zero) {
    Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'; exit 0
  }
  try {
    if ([YmGameValveCompatApi]::ProcessCreated($h) -ne $created) {
      Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'; exit 0
    }
    $status = [YmGameValveCompatApi]::NtSuspendProcess($h)
  } finally { [YmGameValveCompatApi]::CloseHandle($h) | Out-Null }
  if ($status -eq 0) {
    Write-Output 'SUSPENDED 1 0'; Write-Output ("PIDS " + $targetPid)
  } else {
    Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'
  }
} catch {
  Write-Output 'SUSPENDED 0 1'; Write-Output 'PIDS'
}
