param(
  [int]$Cycles = 20,
  [string]$ExeName = 'YeManCC-wake-race.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $projectRoot 'native\testrun\wake_race_test'
$exe = Join-Path $testRoot $ExeName
Get-ChildItem -LiteralPath $testRoot -Filter 'cycle-*.json' -File -ErrorAction SilentlyContinue |
  Remove-Item -Force
Remove-Item -LiteralPath (Join-Path $testRoot 'started.json') -Force -ErrorAction SilentlyContinue

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WakeRaceWin32 {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, UIntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
}
'@

$dataDir = Join-Path $testRoot 'data-race'
$sandboxPower = Join-Path $testRoot 'sandbox-power'
New-Item -ItemType Directory -Path (Join-Path $sandboxPower 'Sleep') -Force | Out-Null
$env:YEMAN_WEBVIEW_DATA_DIR = $dataDir
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

$script:wakeRaceHwnd = [IntPtr]::Zero
$callback = [WakeRaceWin32+EnumWindowsProc]{
  param($h, $l)
  [uint32]$id = 0
  [WakeRaceWin32]::GetWindowThreadProcessId($h, [ref]$id) | Out-Null
  if ($id -eq [uint32]$proc.Id) {
    $script:wakeRaceHwnd = $h
    return $false
  }
  return $true
}
[WakeRaceWin32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
if ($script:wakeRaceHwnd -eq [IntPtr]::Zero) { throw "window not found pid=$($proc.Id)" }

function Send-PowerEvent([uint64]$code) {
  [UIntPtr]$ignored = [UIntPtr]::Zero
  $sent = [WakeRaceWin32]::SendMessageTimeout(
    $script:wakeRaceHwnd, 0x0218, [UIntPtr]::new($code), [IntPtr]::Zero,
    2, 5000, [ref]$ignored)
  if ($sent -eq [IntPtr]::Zero) { throw "power event send failed code=$code" }
}

for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
  Send-PowerEvent 4
  Send-PowerEvent 18
  Send-PowerEvent 7
  $cyclePath = Join-Path $testRoot "cycle-$cycle.json"
  $deadline = (Get-Date).AddSeconds(12)
  while (-not (Test-Path -LiteralPath $cyclePath) -and (Get-Date) -lt $deadline) {
    if ($proc.HasExited) { break }
    Start-Sleep -Milliseconds 50
  }
  if (-not (Test-Path -LiteralPath $cyclePath)) { throw "cycle $cycle timeout" }
  $result = Get-Content -LiteralPath $cyclePath -Raw | ConvertFrom-Json
  if (-not $result.ok) { throw "cycle $cycle failed: $($result | ConvertTo-Json -Compress -Depth 8)" }
}

if (-not $proc.HasExited) { Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue }
Write-Output "wake_race_cycles=$Cycles"
Write-Output "wake_race_pid=$($proc.Id)"
