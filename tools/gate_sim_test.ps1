param(
  [int]$ResumeCode = 18,
  [int]$DuplicateResumeCode = 7,
  [string]$DataName = 'data-sim',
  [string]$ExeName = 'YeManCC-gate-test.exe'
)
$ErrorActionPreference = 'Stop'
$gateRoot = 'C:\SOFT\YeMan\YeManCC4\YeManCC3\native\testrun\gate_test'
$dataDir = Join-Path $gateRoot $DataName
$exe = Join-Path $gateRoot $ExeName
foreach ($name in @('started.json', 'suspend-result.json', 'result.json', 'duplicate-result.json')) {
  Remove-Item -LiteralPath (Join-Path $gateRoot $name) -Force -ErrorAction SilentlyContinue
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GateWin32 {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, UIntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
}
'@

$env:YEMAN_WEBVIEW_DATA_DIR = $dataDir
$sandboxPower = Join-Path $gateRoot 'sandbox-power'
New-Item -ItemType Directory -Path (Join-Path $sandboxPower 'Sleep') -Force | Out-Null
$env:YEMAN_POWER_CONTROL_DIR = $sandboxPower
$env:YEMAN_TRACE = 'debug'
$proc = Start-Process -FilePath $exe -WorkingDirectory $gateRoot -WindowStyle Hidden -PassThru
$startedPath = Join-Path $gateRoot 'started.json'
$deadline = (Get-Date).AddSeconds(20)
while (-not (Test-Path -LiteralPath $startedPath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { throw "early exit $($proc.ExitCode)" }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $startedPath)) { throw 'startup timeout' }

$script:gateHwnd = [IntPtr]::Zero
$callback = [GateWin32+EnumWindowsProc]{
  param($h, $l)
  [uint32]$id = 0
  [GateWin32]::GetWindowThreadProcessId($h, [ref]$id) | Out-Null
  if ($id -eq [uint32]$proc.Id) {
    $script:gateHwnd = $h
    return $false
  }
  return $true
}
[GateWin32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
if ($script:gateHwnd -eq [IntPtr]::Zero) { throw "window not found pid=$($proc.Id)" }

[UIntPtr]$ignored = [UIntPtr]::Zero
[UIntPtr]$wakeCode = [UIntPtr]::new([uint64]4)
$sent = [GateWin32]::SendMessageTimeout($script:gateHwnd, 0x0218, $wakeCode, [IntPtr]::Zero, 2, 5000, [ref]$ignored)
if ($sent -eq [IntPtr]::Zero) { throw 'suspend send failed' }
$suspendPath = Join-Path $gateRoot 'suspend-result.json'
$deadline = (Get-Date).AddSeconds(25)
while (-not (Test-Path -LiteralPath $suspendPath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { throw "exit before suspend result $($proc.ExitCode)" }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $suspendPath)) { throw 'suspend result timeout' }

$wakeCode = [UIntPtr]::new([uint64]$ResumeCode)
$sent = [GateWin32]::SendMessageTimeout($script:gateHwnd, 0x0218, $wakeCode, [IntPtr]::Zero, 2, 5000, [ref]$ignored)
if ($sent -eq [IntPtr]::Zero) { throw 'resume send failed' }
$resultPath = Join-Path $gateRoot 'result.json'
$deadline = (Get-Date).AddSeconds(35)
while (-not (Test-Path -LiteralPath $resultPath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { break }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $resultPath)) { throw 'final result timeout' }
$duplicateWakeCode = [UIntPtr]::new([uint64]$DuplicateResumeCode)
$sent = [GateWin32]::SendMessageTimeout($script:gateHwnd, 0x0218, $duplicateWakeCode, [IntPtr]::Zero, 2, 5000, [ref]$ignored)
if ($sent -eq [IntPtr]::Zero) { throw 'duplicate resume send failed' }
$duplicatePath = Join-Path $gateRoot 'duplicate-result.json'
$deadline = (Get-Date).AddSeconds(10)
while (-not (Test-Path -LiteralPath $duplicatePath) -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { break }
  Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $duplicatePath)) { throw 'duplicate result timeout' }
$duplicate = Get-Content -LiteralPath $duplicatePath -Raw | ConvertFrom-Json
if (-not $duplicate.ok) { throw "duplicate resume reopened lifecycle: $($duplicate | ConvertTo-Json -Compress)" }
Get-Content -LiteralPath $suspendPath
Get-Content -LiteralPath $resultPath
Get-Content -LiteralPath $duplicatePath
if (-not $proc.HasExited) { Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue }
Write-Output ("gate_test_pid=" + $proc.Id)
