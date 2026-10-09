param(
  [Parameter(Mandatory = $true)]
  [string]$GameName,
  [string]$ResultPath = '',
  [int]$DownloadTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$gcmExe = 'C:\SOFT\Game Cheats Manager\Game Cheats Manager.exe'
$trainerRoot = 'C:\Users\DaVe\AppData\Roaming\GCM Trainers'
$beforeSnapshot = @{}
$downloadStartedAt = [DateTime]::UtcNow

function Normalize-GameTitle([string]$value) {
  if (-not $value) { return '' }
  return ($value.ToLowerInvariant() -replace '[^\p{L}\p{N}]', '')
}

function Get-TrainerSnapshot {
  $snapshot = @{}
  if (Test-Path -LiteralPath $trainerRoot) {
    Get-ChildItem -LiteralPath $trainerRoot -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
      $snapshot[$_.FullName] = $_.LastWriteTimeUtc.Ticks
    }
  }
  return $snapshot
}

function Find-DownloadedTrainer([datetime]$startedAt, [hashtable]$before) {
  if (-not (Test-Path -LiteralPath $trainerRoot)) { return $null }
  $target = Normalize-GameTitle $GameName
  $dirs = Get-ChildItem -LiteralPath $trainerRoot -Directory -Force -ErrorAction SilentlyContinue | Where-Object {
    $oldTicks = if ($before.ContainsKey($_.FullName)) { [int64]$before[$_.FullName] } else { 0 }
    $_.LastWriteTimeUtc.Ticks -gt $oldTicks -and $_.LastWriteTimeUtc -ge $startedAt
  }
  foreach ($dir in $dirs) {
    $infoPath = Join-Path $dir.FullName 'gcm_info.json'
    if (-not (Test-Path -LiteralPath $infoPath)) { continue }
    try {
      $info = Get-Content -LiteralPath $infoPath -Raw -Encoding UTF8 | ConvertFrom-Json
      $actual = Normalize-GameTitle ([string]$info.game_name)
      if (-not $actual -or ($actual -ne $target -and -not ($actual.Contains($target) -or $target.Contains($actual)))) { continue }
      $file = Get-ChildItem -LiteralPath $dir.FullName -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -match '^\.(exe|ct|cetrainer)$' } |
        Sort-Object @{Expression={ if ($_.Extension -ieq '.exe') { 0 } else { 1 } }}, FullName |
        Select-Object -First 1
      if ($file) {
        return @{ folder = $dir.FullName; path = $file.FullName; gameName = [string]$info.game_name; origin = [string]$info.origin }
      }
    } catch { }
  }
  return $null
}

function Minimize-GcmWindow {
  try {
    $p = Get-GcmProcess
    if ($p -and $p.MainWindowHandle -ne 0) {
      [YeManGcmNative]::ShowWindow($p.MainWindowHandle, 6) | Out-Null # SW_MINIMIZE
    }
  } catch { }
}

function Get-GcmDownloadFailure {
  try {
    $p = Get-GcmProcess
    if (-not $p -or $p.MainWindowHandle -eq 0) { return '' }
    $r = [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
    $c = New-Object System.Windows.Automation.PropertyCondition(
      [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
      [System.Windows.Automation.ControlType]::ListItem
    )
    $all = $r.FindAll([System.Windows.Automation.TreeScope]::Descendants, $c)
    $bad = @($all | ForEach-Object { $_.Current.Name } | Where-Object {
      $_ -match '已存在|中止下载|下载失败|网络错误|连接失败|失败|Internet|error|failed'
    })
    return ($bad -join '; ')
  } catch { return '' }
}

function Write-Result($payload) {
  if ($ResultPath) {
    $payload | ConvertTo-Json -Compress | Set-Content -LiteralPath $ResultPath -Encoding UTF8
  }
  [Console]::WriteLine(($payload | ConvertTo-Json -Compress))
}

function Write-DownloadState([string]$state, [int]$elapsedSeconds, [string]$message = '') {
  Write-Result (@{
    ok = $false
    gameName = $GameName
    state = $state
    elapsedSeconds = $elapsedSeconds
    message = $message
  })
}

trap {
  Minimize-GcmWindow
  Write-Result (@{ ok = $false; gameName = $GameName; state = 'failed'; error = $_.Exception.Message; message = $_.Exception.Message })
  break
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

if (-not ('YeManGcmNative' -as [type])) {
  Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YeManGcmNative {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  public const uint LEFTDOWN = 0x0002;
  public const uint LEFTUP = 0x0004;
}
'@
}

$beforeSnapshot = Get-TrainerSnapshot
$downloadStartedAt = [DateTime]::UtcNow

function Get-GcmProcess {
  Get-Process -ErrorAction SilentlyContinue | Where-Object {
    try { $_.Path -eq $gcmExe } catch { $false }
  } | Select-Object -First 1
}

$process = Get-GcmProcess
if (-not $process) {
  Start-Process -FilePath $gcmExe -WorkingDirectory (Split-Path $gcmExe) -WindowStyle Minimized | Out-Null
  $deadline = [DateTime]::UtcNow.AddSeconds(15)
  do {
    Start-Sleep -Milliseconds 250
    $process = Get-GcmProcess
  } while (-not $process -and [DateTime]::UtcNow -lt $deadline)
}
if (-not $process) { throw '无法启动 Game Cheats Manager' }

$deadline = [DateTime]::UtcNow.AddSeconds(15)
while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline) {
  Start-Sleep -Milliseconds 250
  $process.Refresh()
}
if ($process.MainWindowHandle -eq 0) { throw 'Game Cheats Manager 窗口尚未就绪' }

# 临时恢复窗口以确保 Qt 输入控件可以接收焦点；搜索完成后马上最小化。
[YeManGcmNative]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
Start-Sleep -Milliseconds 150

$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$editCondition = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::Edit
)
$edits = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $editCondition)
if ($edits.Count -lt 2) { throw '未找到 GCM 在线搜索框' }
$searchEdit = $edits.Item(1)
$valuePattern = $searchEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
$valuePattern.SetValue($GameName)

$searchEdit.SetFocus()
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait("~")

$itemCondition = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::ListItem
)
$results = @()
$resultItems = @()
$readDeadline = [DateTime]::UtcNow.AddSeconds(8)
do {
  Start-Sleep -Milliseconds 350
  # Qt may replace the result list after the search request; reacquire the root
  # and list on every poll instead of keeping a stale AutomationElement.
  $process.Refresh()
  $root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$items = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $itemCondition)
  $resultItems = @(
    for ($i = 0; $i -lt $items.Count; $i++) {
      $item = $items.Item($i)
      if ($item.Current.Name -match '^\d+\.\s') { $item }
    }
  )
  $results = @($resultItems | ForEach-Object { $_.Current.Name })
} while ($results.Count -eq 0 -and [DateTime]::UtcNow -lt $readDeadline)

if ($results.Count -eq 0) { throw "GCM 未找到 '$GameName' 的在线修改器" }

# GCM's Qt list emits itemActivated on a real double-click. Prefer FLiNG,
# then fall back to the first result when no FLiNG entry is available.
$selectedIndex = 0
for ($i = 0; $i -lt $resultItems.Count; $i++) {
  if ($resultItems[$i].Current.Name -match '风灵|FLiNG') { $selectedIndex = $i; break }
}
$selectedName = $resultItems[$selectedIndex].Current.Name
$downloadStartedAt = [DateTime]::UtcNow
Write-DownloadState 'downloading' 0 "正在下载 $selectedName"
$bounds = $resultItems[$selectedIndex].Current.BoundingRectangle
if ($bounds.Width -le 0 -or $bounds.Height -le 0) { throw 'GCM 下载结果不可见' }
$x = [int]($bounds.X + $bounds.Width / 2)
$y = [int]($bounds.Y + $bounds.Height / 2)
[YeManGcmNative]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 80
[YeManGcmNative]::mouse_event([YeManGcmNative]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
[YeManGcmNative]::mouse_event([YeManGcmNative]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 80
[YeManGcmNative]::mouse_event([YeManGcmNative]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
[YeManGcmNative]::mouse_event([YeManGcmNative]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)

# Give Qt a short chance to enter its download worker, then minimize normally.
Start-Sleep -Milliseconds 500
Minimize-GcmWindow
$downloadDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(30, $DownloadTimeoutSeconds))
$downloaded = $null
$lastStateWrite = [DateTime]::UtcNow
do {
  Start-Sleep -Milliseconds 500
  $downloaded = Find-DownloadedTrainer $downloadStartedAt $beforeSnapshot
  $elapsed = [int](([DateTime]::UtcNow - $downloadStartedAt).TotalSeconds)
  if (([DateTime]::UtcNow - $lastStateWrite).TotalSeconds -ge 2) {
    Write-DownloadState 'downloading' $elapsed "正在下载 $selectedName，已等待 ${elapsed} 秒"
    $lastStateWrite = [DateTime]::UtcNow
  }
  $downloadFailure = Get-GcmDownloadFailure
  if ($downloadFailure) {
    # GCM may report "already exists" when its local list is stale. Accept it
    # only when the verified trainer is actually present on disk.
    $existing = Find-DownloadedTrainer ([DateTime]::MinValue) @{}
    if ($existing) {
      $downloaded = $existing
      break
    }
    throw $downloadFailure
  }
} while (-not $downloaded -and [DateTime]::UtcNow -lt $downloadDeadline)

if (-not $downloaded) {
  throw "GCM 已选择 $selectedName，但下载未完成或结果未通过元数据核验"
}

Minimize-GcmWindow

Write-Result (@{
  ok = $true
  state = 'completed'
  gameName = $GameName
  results = $results
  selected = $selectedName
  downloaded = $true
  trainerFolder = $downloaded.folder
  trainerPath = $downloaded.path
  trainerGameName = $downloaded.gameName
  origin = $downloaded.origin
})
