# Resume only processes recorded by the unified Sleep Guard markers.
# The marker is the native game valve's ownership lease. No process
# enumeration, current-game replacement, or "resume every large process"
# fallback is allowed.
$ErrorActionPreference = 'Stop'
$DIR = 'C:\SOFT\YeMan\PowerControl'
$MARKER_DIRS = @(
  (Join-Path $DIR 'Sleep\manual-suspended'),
  (Join-Path $DIR 'Sleep\suspended')
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YmGameValveResumeApi {
    [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@

$markers = @($MARKER_DIRS | ForEach-Object {
  if (Test-Path -LiteralPath $_) { Get-ChildItem -LiteralPath $_ -File -Filter '*.txt' -ErrorAction SilentlyContinue }
})
if ($markers.Count -eq 0) {
  Write-Host 'No unified Sleep Guard suspension markers found.' -ForegroundColor Yellow
  exit 0
}

$ok = 0; $stale = 0; $fail = 0
foreach ($marker in $markers) {
  try {
    $targetPid = [int]$marker.BaseName
    $text = Get-Content -LiteralPath $marker.FullName -Raw
    $match = [regex]::Match($text, 'created=(\d+)')
    $pidMatch = [regex]::Match($text, '(^|\|)pid=(\d+)($|\|)')
    $stateMatch = [regex]::Match($text, '(^|\|)state=suspended($|\|)')
    if ($targetPid -le 0 -or -not $match.Success -or -not $pidMatch.Success -or
        -not $stateMatch.Success -or [int]$pidMatch.Groups[2].Value -ne $targetPid) {
      Remove-Item -LiteralPath $marker.FullName -Force; $stale++; continue
    }
    $process = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
    if (-not $process -or [Int64]$process.StartTime.ToFileTimeUtc() -ne [Int64]$match.Groups[1].Value) {
      Remove-Item -LiteralPath $marker.FullName -Force
      $stale++
      continue
    }
    $h = [YmGameValveResumeApi]::OpenProcess(0x0800, $false, $targetPid)
    if ($h -eq [IntPtr]::Zero) { $fail++; continue }
    try { $status = [YmGameValveResumeApi]::NtResumeProcess($h) }
    finally { [YmGameValveResumeApi]::CloseHandle($h) | Out-Null }
    if ($status -eq 0) { Remove-Item -LiteralPath $marker.FullName -Force; $ok++ } else { $fail++ }
  } catch { $fail++ }
}
Write-Host ("Resumed: {0}; stale markers removed: {1}; failed: {2}" -f $ok, $stale, $fail) -ForegroundColor Green
