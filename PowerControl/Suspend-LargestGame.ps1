# Compatibility entry point for the native game recognition valve.
# The native valve selects exactly one PID; this script only revalidates and
# suspends that published PID. It must never perform a second process scan or
# rebuild a target from a name/working-set heuristic.
$ErrorActionPreference = 'Stop'
$DIR = 'C:\SOFT\YeMan\PowerControl'
$GATE = Join-Path $DIR 'game-target.json'
$MARKER_DIR = Join-Path $DIR 'Sleep\manual-suspended'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YmGameValveNtApi {
    [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
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

function Read-GameValveTarget {
  if (-not (Test-Path -LiteralPath $GATE)) { return $null }
  $target = Get-Content -LiteralPath $GATE -Raw | ConvertFrom-Json
  $targetPid = [int]$target.pid
  $created = [Int64]$target.processCreated
  $lastSeen = [Int64]$target.lastSeen
  $generation = [Int64]$target.generation
  $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - $lastSeen
  if (-not $target.valid -or $targetPid -le 0 -or $created -le 0 -or
      $generation -le 0 -or $age -lt 0 -or $age -gt 10000) { return $null }
  $process = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
  if (-not $process -or [Int64]$process.StartTime.ToFileTimeUtc() -ne $created) { return $null }
  return [PSCustomObject]@{ Pid = $targetPid; Created = $created; Name = $process.ProcessName; WS = $process.WorkingSet64 }
}

$target = Read-GameValveTarget
if ($null -eq $target) {
  Write-Host 'No current game admitted by the native game valve.' -ForegroundColor Yellow
  exit 0
}

$h = [YmGameValveNtApi]::OpenProcess((0x0800 -bor 0x1000), $false, [int]$target.Pid)
if ($h -eq [IntPtr]::Zero) {
  Write-Host 'The valve PID cannot be opened. Run elevated if required.' -ForegroundColor Red
  exit 0
}
$actualCreated = [YmGameValveNtApi]::ProcessCreated($h)
if ($actualCreated -ne $target.Created) {
  [YmGameValveNtApi]::CloseHandle($h) | Out-Null
  Write-Host 'The valve PID was reused before suspend; refusing to act.' -ForegroundColor Yellow
  exit 0
}
try { $status = [YmGameValveNtApi]::NtSuspendProcess($h) }
finally { [YmGameValveNtApi]::CloseHandle($h) | Out-Null }
if ($status -ne 0) {
  Write-Host ("Valve PID {0} suspend failed (NTSTATUS 0x{1:X8})." -f $target.Pid, $status) -ForegroundColor Red
  exit 0
}

New-Item -ItemType Directory -Force -Path $MARKER_DIR | Out-Null
$marker = Join-Path $MARKER_DIR ("{0}.txt" -f $target.Pid)
[IO.File]::WriteAllText($marker, "pid=$($target.Pid)|created=$($target.Created)|state=suspended")
Write-Host ("Suspended valve target: {0} (PID {1})" -f $target.Name, $target.Pid) -ForegroundColor Green
