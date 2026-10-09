param([string]$ExeName = 'YeManCC-wake-watchdog.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $projectRoot 'native\testrun\wake_watchdog_test'
$exe = Join-Path $testRoot $ExeName
foreach ($name in @('started.json', 'result.json')) {
  Remove-Item -LiteralPath (Join-Path $testRoot $name) -Force -ErrorAction SilentlyContinue
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WakeWatchdogWin32 {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, UIntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
}
'@

$sandboxPower = Join-Path $testRoot 'sandbox-power'
New-Item -ItemType Directory -Path (Join-Path $sandboxPower 'Sleep') -Force | Out-Null
$env:YEMAN_WEBVIEW_DATA_DIR = Join-Path $testRoot 'data'
$env:YEMAN_POWER_CONTROL_DIR = $sandboxPower
$env:YEMAN_TRACE = 'debug'
$proc = Start-Process -FilePath $exe -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru
$startedPath = Join-Path $testRoot 'started.json'
$deadline = (Get-Date).AddSeconds(20)
while (-not (Test-Path -LiteralPath $startedPath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { throw "early exit $($proc.ExitCode)" }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $startedPath)) { throw 'startup timeout' }

$script:wakeWatchdogHwnd = [IntPtr]::Zero
$callback = [WakeWatchdogWin32+EnumWindowsProc]{
  param($h, $l)
  [uint32]$id = 0
  [WakeWatchdogWin32]::GetWindowThreadProcessId($h, [ref]$id) | Out-Null
  if ($id -eq [uint32]$proc.Id) {
    $script:wakeWatchdogHwnd = $h
    return $false
  }
  return $true
}
[WakeWatchdogWin32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
if ($script:wakeWatchdogHwnd -eq [IntPtr]::Zero) { throw "window not found pid=$($proc.Id)" }

foreach ($code in 4, 18, 7) {
  [UIntPtr]$ignored = [UIntPtr]::Zero
  $sent = [WakeWatchdogWin32]::SendMessageTimeout(
    $script:wakeWatchdogHwnd, 0x0218, [UIntPtr]::new([uint64]$code),
    [IntPtr]::Zero, 2, 5000, [ref]$ignored)
  if ($sent -eq [IntPtr]::Zero) { throw "power event send failed code=$code" }
}

$resultPath = Join-Path $testRoot 'result.json'
$deadline = (Get-Date).AddSeconds(35)
while (-not (Test-Path -LiteralPath $resultPath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { break }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $resultPath)) { throw 'watchdog fail-open timeout' }
$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
if (-not $result.ok -or $result.state.phase -ne 'ready' -or -not $result.state.hardwareWritesAllowed) {
  throw "watchdog result invalid: $($result | ConvertTo-Json -Compress -Depth 8)"
}
Get-Content -LiteralPath $resultPath
if (-not $proc.HasExited) { Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue }
Write-Output "wake_watchdog_pid=$($proc.Id)"
