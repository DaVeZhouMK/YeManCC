<#
.SYNOPSIS
  Installs the YeMan Fan Host payload manifest closure.

.DESCRIPTION
  Validates every payload manifest entry against its on-disk file
  (path 防逃逸 + 存在性 + SHA-256) and the shared HC runtime manifest.
  方案 A（2026-09-12 用户裁决）：不再施加 ACL 不可变 / quarantine /
  交互确认；部署边界 = 防逃逸 + 存在 + 哈希两层门。

  取得坑史（2026-09-11 R-publisher 同批 + 2026-09-12 方案 A 用户裁决）：
  ① 禁止以 IndexOfAny('\/') 整体拒绝子路径——HC runtime.json 合法含
     XInputPlus/Loader/...、Resources/... 等子目录条目，所有 manifest 段
     （payload 与 runtime）一律"归一化 + IsPathRooted + 逐段 .."判据，
     （911-23 风扇必挂已实证）。
  ② 禁止在字符串字面量里写 '\'（R3 语法炸）：R3-redo 后统一用
     TrimEnd([char]0x5C)，且本文件保持 UTF-8 BOM（PS 5.1 按 ANSI 误读
     无 BOM 非 ASCII 脚本会吞掉下一行语句）。
  ③ 修改本文件后必须重算 ps1 → payload files[] → manifest → main.cpp
     $expectedFanHostV2ManifestSha256 全链哈希，禁止手填哈希值。build
     前门（build-workspace.ps1 Assert-PublishPreGate）会自动对拍，不一致即 halt。
  ④ 方案 A（2026-09-12 用户裁决）：部署阀门=防逃逸 + 存在 + 哈希两层，
     ACL 不可变 / quarantine 扫描 / 交互 ShouldProcess 已删除，禁止加回；
     实例运行中仅警告放行（不再阻断）。
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$PayloadDirectory = '',
  [string]$StateDirectory = (Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host'),
  [switch]$SkipStateDirectory
)

$ErrorActionPreference = 'Stop'
$PayloadDirectory = if ([string]::IsNullOrWhiteSpace($PayloadDirectory)) { $PSScriptRoot } else { $PayloadDirectory }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw '请以管理员身份运行此脚本；未执行任何部署。'
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd([char]0x5C)  # R3-redo: 0x5C='\'，UTF-8 BOM 安全
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

$payloadFull = Get-FullPath $PayloadDirectory
if (-not (Test-Path -LiteralPath $payloadFull -PathType Container)) {
  throw "Fan Host 目录不存在: $payloadFull"
}

# 方案 A（2026-09-12 用户裁决）：原"实例运行中即拒改"改为警告放行——升级
# 期间运行的 Host 属已知暂态，部署边界交给防逃逸+存在+哈希两层门；不再用
# 进程占用作为阻断（launcher/updater 已各自有预检护栏）。
$payloadHostPath = Get-FullPath (Join-Path $payloadFull 'YeManFanHost.exe')
try {
  $resident = @(Get-CimInstance Win32_Process -Filter "Name='YeManFanHost.exe'" -ErrorAction Stop)
  foreach ($process in $resident) {
    $image = [string]$process.ExecutablePath
    if (-not [string]::IsNullOrWhiteSpace($image) -and
        (Get-FullPath $image).Equals($payloadHostPath, [StringComparison]::OrdinalIgnoreCase)) {
      Write-Warning "检测到 Fan Host 实例仍在运行 (pid=$($process.ProcessId))，按方案 A 放行；建议先退出主程序再升级。"
    }
  }
} catch {
  Write-Warning "无法确认 Fan Host 是否仍在运行；按方案 A 放行（诊断：$($_.Exception.Message)）"
}
$manifestPath = Join-Path $payloadFull 'YeManFanHost.payload.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
  throw '缺少 YeManFanHost.payload.json；无法校验载荷。'
}
if (((Get-Item -LiteralPath $manifestPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
  throw 'Fan Host manifest 不能是重解析点。'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 2 -or $null -eq $manifest.files -or $manifest.files.Count -eq 0) {
  throw 'Fan Host payload manifest 无效或为空。'
}
$runtimeManifestRelative = [string]$manifest.runtimeManifest
$manifestRuntimeId = [string]$manifest.runtimeId
if ([string]::IsNullOrWhiteSpace($runtimeManifestRelative) -or
    $manifestRuntimeId -notmatch '^HC-CANDIDATE-0\.32\.4\.0-[0-9a-f]{8}-[0-9]{8}$' -or
    [IO.Path]::IsPathRooted($runtimeManifestRelative) -or
    $runtimeManifestRelative -notmatch '(?i)^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$' -or
    $runtimeManifestRelative.Substring(3) -match '(^|[\\/])\.\.([\\/]|$)') {
  throw 'Fan Host payload runtimeManifest 路径无效。'
}
$runtimeManifestPath = Get-FullPath (Join-Path (Split-Path -Parent $payloadFull) $runtimeManifestRelative.Substring(3))
$runtimeFull = Get-FullPath (Split-Path -Parent $runtimeManifestPath)

$expectedFiles = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  $expectedHash = [string]$entry.sha256
  # 方案 A（2026-09-12 用户裁决）：与 runtime 段同构的防逃逸——归一化
  # （/ → \）后只防 IsPathRooted 与逐段 .. 逃逸，允许合法子路径。hex64 校验、
  # 去重、存在性、sha256 对拍全部保留（不再 IndexOfAny('\/') 整体拒绝）。
  $normalized = $relative.Replace('/', '\')
  if ([IO.Path]::IsPathRooted($normalized) -or
      $normalized -match '(^|[\\/])\.\.([\\/]|$)' -or
      $relative -eq '') {
    throw "Fan Host manifest 包含不安全的条目: $relative"
  }
  if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
    throw "Fan Host manifest 包含不安全的哈希: $relative"
  }
  if ($expectedFiles.ContainsKey($relative.ToLowerInvariant())) {
    throw "Fan Host manifest 有重复条目: $relative"
  }
  $path = Join-Path $payloadFull $normalized
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Fan Host manifest 文件缺失: $relative"
  }
  if (((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Fan Host manifest 文件不能是重解析点: $relative"
  }
  if ((Get-Sha256 $path) -ne $expectedHash.ToLowerInvariant()) {
    throw "Fan Host manifest 哈希不匹配: $relative"
  }
  $expectedFiles[$relative.ToLowerInvariant()] = $relative
}

$fanHostConfigEntry = @($manifest.files | Where-Object {
  ([string]$_.path).Equals('YeManFanHost.json', [StringComparison]::OrdinalIgnoreCase)
})
if ($fanHostConfigEntry.Count -ne 1) {
  throw 'Fan Host manifest 必须包含唯一 YeManFanHost.json。'
}
$fanHostConfigPath = Join-Path $payloadFull 'YeManFanHost.json'
$fanHostConfig = Get-Content -LiteralPath $fanHostConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$expectedFanLogPath = '%LOCALAPPDATA%\YeManCC\fan-host\logs\yeman-fan-host.log'
$configuredFanLogPaths = @($fanHostConfig.Serilog.WriteTo | ForEach-Object {
  if ($null -ne $_.Args) { [string]$_.Args.path }
})
if ($configuredFanLogPaths -notcontains $expectedFanLogPath) {
  throw "YeManFanHost.json 必须把 Serilog 日志写入: $expectedFanLogPath"
}

foreach ($required in @(
  'YeManFanHost.exe', 'YeManFanHost.dll', 'YeManFanHost.deps.json',
  'YeManFanHost.runtimeconfig.json', 'YeManFanHost.authorization.md',
  'install-fan-host-payload.ps1'
)) {
  if (-not $expectedFiles.ContainsKey($required.ToLowerInvariant())) {
    throw "Fan Host manifest 缺少必要文件: $required"
  }
}

if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) { throw "缺少 HC 公共 runtime manifest: $runtimeManifestPath" }
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($runtimeManifest.schemaVersion -ne 1 -or $runtimeManifest.runtimeId -ne $manifestRuntimeId -or $null -eq $runtimeManifest.files -or $runtimeManifest.files.Count -eq 0) { throw 'HC 公共 runtime manifest 无效或与 FanHost manifest 不一致。' }
foreach ($entry in $runtimeManifest.files) {
  $relative = [string]$entry.path
  # 2026-09-11：去掉 IndexOfAny('\/') 整体拒绝——manifest 允许子路径
  # （XInputPlus/Loader/...），整体拒绝会让任何含子路径条目必失败。改为
  # 归一化（/ → \）后只防根路径与 .. 逃逸；存在性与 sha256 校验逻辑不变。
  $normalizedRelative = $relative.Replace('/', '\')
  $path = Join-Path $runtimeFull $normalizedRelative
  if ([IO.Path]::IsPathRooted($normalizedRelative) -or
      $normalizedRelative -match '(^|[\\/])\.\.([\\/]|$)' -or
      -not (Test-Path -LiteralPath $path -PathType Leaf) -or
      (Get-Sha256 $path) -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "HC 公共 runtime 文件缺失或哈希不匹配: $relative" }
  }

# 方案 A（2026-09-12 用户裁决）：阀门=防逃逸 + 存在 + 哈希两层。ACL 不可变、
# quarantine 扫描、交互 ShouldProcess 已整段删除（run-emergency-fan-restore.ps1
# 不再被当异物处理），禁止加回。

# FAN-927 §2.1（2026-09-27 用户批准）：**完整文件校验归部署边界**。上面这套逐条
# 校验成功后，向**可变状态目录**原子写出一份小"部署记录"，把这次验证结果绑定到
# 规范安装根 + 两个小清单摘要 + 包代 + 校验规则版本。运行期（前端 launcher 与 Host）
# 只读这份记录与两个小清单摘要，不再遍历依赖目录、不再逐文件 certutil、不再重复全扫。
# 记录**不**放进 payload 自包含清单，也不新增任何人工填写的 pin。
if (-not $SkipStateDirectory) {
  $recordDirectory = Get-FullPath $StateDirectory
  New-Item -ItemType Directory -Force -Path $recordDirectory | Out-Null
  $recordPath = Join-Path $recordDirectory 'YeManFanHost.deployment.json'
  $recordTemp = $recordPath + '.tmp'
  $record = [ordered]@{
    schemaVersion = 1
    validationRulesVersion = 1
    payloadRoot = $payloadFull.ToLowerInvariant()
    runtimeRoot = $runtimeFull.ToLowerInvariant()
    payloadManifestSha256 = (Get-Sha256 $manifestPath)
    runtimeManifestSha256 = (Get-Sha256 $runtimeManifestPath)
    runtimeId = $manifestRuntimeId
    payloadFileCount = $manifest.files.Count
    runtimeFileCount = $runtimeManifest.files.Count
    result = 'ok'
    deployedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
  }
  [IO.File]::WriteAllText($recordTemp, ($record | ConvertTo-Json -Depth 4),
    (New-Object Text.UTF8Encoding($false)))
  if (Test-Path -LiteralPath $recordPath) { Remove-Item -LiteralPath $recordPath -Force }
  [IO.File]::Move($recordTemp, $recordPath)
  # 注意：不得新增第二个 FAN_HOST_*_OK 成功常量——前门（build-workspace.ps1
  # Assert-PublishPreGate §3）要求 ps1 的每个 FAN_HOST_*_OK 都被 fanHost.ts 的
  # 成功判定正则接受；部署记录的"已写出"不是 launcher 的成功判据（它另行读取记录核对），
  # 因此这里用不带 _OK 的标记，保持单一成功常量契约。
  Write-Output "FAN_HOST_DEPLOYMENT_RECORD_WRITTEN: $recordPath"
}

Write-Output "FAN_HOST_PAYLOAD_OK: $payloadFull"
