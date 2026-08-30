<#
.SYNOPSIS
  Installs the immutable YeMan Fan Host payload and its private loopback state.

.DESCRIPTION
  This is an elevated, transactional deployment boundary. It validates every
  manifest entry before and after ACL changes, quarantines stale Host files
  instead of deleting them, and recursively removes untrusted write access.
  The executable payload and the mutable per-user API capability are kept in
  separate directories with different ACL contracts.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$PayloadDirectory = '',
  [string]$StateDirectory = (Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host'),
  [switch]$SkipStateDirectory,
  [switch]$KeepUnexpectedPayloadItems
)

$ErrorActionPreference = 'Stop'
$expectedV2ManifestFileCount = 102
$expectedR5HostExeSha256 = 'A3CE266B880F67045D3F895E2FC68B1FE81F641D5F1A08A01F0DE19D2458F733'
$expectedR5HostDllSha256 = '5D097826A864CD18BEC9AA8E2662DA82F80381340BAA447CF6E7FEED47F4CC74'
$expectedR5HcSha256 = '70E27FD4D73A5CA3E3E750DE2736B5E1C3B126D716DD9F4F5794C84DA88C6415'
$expectedV2LhmSha256 = 'F7ED30F07EA636C0DDCC5764C15C0A25AE8A0ACA02DED77E4AF24439955487CF'
$PayloadDirectory = if ([string]::IsNullOrWhiteSpace($PayloadDirectory)) { $PSScriptRoot } else { $PayloadDirectory }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw '请以管理员身份运行此脚本；未修改任何 ACL。'
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
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

function Invoke-Icacls([string[]]$Arguments) {
  $previousPreference = $ErrorActionPreference
  try {
    # icacls emits localized progress through stderr even on success. Keep it
    # out of PowerShell's native-error pipeline and use its documented exit
    # code as the sole transaction result.
    $ErrorActionPreference = 'Continue'
    & icacls.exe @Arguments *> $null
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  if ($exitCode -ne 0) { throw "icacls 失败 ($exitCode): $($Arguments -join ' ')" }
}

$untrustedSids = @('*S-1-1-0', '*S-1-5-11', '*S-1-5-32-545')
$systemSid = 'S-1-5-18'
$administratorsSid = 'S-1-5-32-544'
$callerSidValue = $identity.User.Value
$callerSid = "*$callerSidValue"
$payloadFull = Get-FullPath $PayloadDirectory
if (-not (Test-Path -LiteralPath $payloadFull -PathType Container)) {
  throw "Fan Host 目录不存在: $payloadFull"
}

# Never mutate/quarantine a payload while the exact Fan Host image is still
# resident. The Host may have HC/ACPI/HID handles open even when its listener
# is temporarily unavailable; updating beside it would create a split
# lifecycle in which the old process owns hardware but the new files are
# already considered installed. The launcher performs the same preflight,
# this guard also protects manual/updater invocations.
$payloadHostPath = Get-FullPath (Join-Path $payloadFull 'YeManFanHost.exe')
try {
  $resident = @(Get-CimInstance Win32_Process -Filter "Name='YeManFanHost.exe'" -ErrorAction Stop)
  foreach ($process in $resident) {
    $image = [string]$process.ExecutablePath
    if (-not [string]::IsNullOrWhiteSpace($image) -and
        (Get-FullPath $image).Equals($payloadHostPath, [StringComparison]::OrdinalIgnoreCase)) {
      throw "检测到同一 Fan Host 仍在运行 (pid=$($process.ProcessId))；必须先完成 OEM 恢复/关闭，未修改载荷。"
    }
  }
} catch {
  if ($_.Exception.Message -like '检测到同一 Fan Host*') { throw }
  throw "无法确认 Fan Host 是否仍在运行；拒绝修改载荷: $($_.Exception.Message)"
}
$manifestPath = Join-Path $payloadFull 'YeManFanHost.payload.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
  throw '缺少 YeManFanHost.payload.json；拒绝对不完整载荷设置 ACL。'
}
if (((Get-Item -LiteralPath $manifestPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
  throw 'Fan Host manifest 不能是重解析点。'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $null -eq $manifest.files -or $manifest.files.Count -ne $expectedV2ManifestFileCount) {
  throw 'Fan Host payload manifest 无效或为空。'
}

$expectedFiles = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  $expectedHash = [string]$entry.sha256
  if ([string]::IsNullOrWhiteSpace($relative) -or
      $relative.IndexOfAny([char[]]'\/') -ge 0 -or
      [IO.Path]::IsPathRooted($relative) -or
      $expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
    throw "Fan Host manifest 包含不安全的条目: $relative"
  }
  if ($expectedFiles.ContainsKey($relative.ToLowerInvariant())) {
    throw "Fan Host manifest 有重复条目: $relative"
  }
  $path = Join-Path $payloadFull $relative
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
  'YeManFanHost.runtimeconfig.json', 'HandheldCompanion.dll', 'GamepadMotion.dll',
  'LibreHardwareMonitorLib.dll', 'YeManFanHost.authorization.md',
  'install-fan-host-payload.ps1'
)) {
  if (-not $expectedFiles.ContainsKey($required.ToLowerInvariant())) {
    throw "Fan Host manifest 缺少必要文件: $required"
  }
}

foreach ($pinned in @(
  @{ Name = 'YeManFanHost.exe'; Hash = $expectedR5HostExeSha256 },
  @{ Name = 'YeManFanHost.dll'; Hash = $expectedR5HostDllSha256 },
  @{ Name = 'HandheldCompanion.dll'; Hash = $expectedR5HcSha256 },
  @{ Name = 'LibreHardwareMonitorLib.dll'; Hash = $expectedV2LhmSha256 }
)) {
  $pinnedPath = Join-Path $payloadFull $pinned.Name
  $pinnedObserved = Get-Sha256 $pinnedPath
  if ($pinnedObserved -ne $pinned.Hash.ToLowerInvariant()) {
    throw "Fan Host 不是冻结的 R5-v9 主线文件: $($pinned.Name) SHA-256=$pinnedObserved"
  }
}

$allowedTopLevel = @{}
$expectedFiles.Values | ForEach-Object { $allowedTopLevel[$_.ToLowerInvariant()] = $true }
$allowedTopLevel['yemanfanhost.payload.json'] = $true
$unexpected = @(Get-ChildItem -LiteralPath $payloadFull -Force | Where-Object {
  -not $allowedTopLevel.ContainsKey($_.Name.ToLowerInvariant())
})
if ($unexpected.Count -gt 0) {
  if ($KeepUnexpectedPayloadItems) {
    throw "Fan Host 载荷含有未清单化项: $($unexpected.Name -join ', ')"
  }
  $quarantineRoot = Join-Path (Split-Path -Parent $payloadFull) 'fan-host-quarantine'
  $quarantine = Join-Path $quarantineRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
  if ($PSCmdlet.ShouldProcess($payloadFull, 'quarantine stale Fan Host payload items')) {
    New-Item -ItemType Directory -Force -Path $quarantine | Out-Null
    foreach ($item in $unexpected) {
      Move-Item -LiteralPath $item.FullName -Destination (Join-Path $quarantine $item.Name) -ErrorAction Stop
    }
    Write-Output "FAN_HOST_PAYLOAD_QUARANTINED: $quarantine"
  }
}

function Assert-AllowedAcl([string]$Path, [string[]]$AllowedSids, [bool]$RejectRead) {
  $security = (Get-Item -LiteralPath $Path -Force).GetAccessControl()
  $writeRights = [Security.AccessControl.FileSystemRights]::WriteData -bor
    [Security.AccessControl.FileSystemRights]::AppendData -bor
    [Security.AccessControl.FileSystemRights]::WriteAttributes -bor
    [Security.AccessControl.FileSystemRights]::WriteExtendedAttributes -bor
    [Security.AccessControl.FileSystemRights]::Delete -bor
    [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
    [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
    [Security.AccessControl.FileSystemRights]::TakeOwnership
  $readRights = [Security.AccessControl.FileSystemRights]::ReadData -bor
    [Security.AccessControl.FileSystemRights]::ReadAttributes -bor
    [Security.AccessControl.FileSystemRights]::ReadExtendedAttributes -bor
    [Security.AccessControl.FileSystemRights]::ReadPermissions -bor
    [Security.AccessControl.FileSystemRights]::ExecuteFile
  foreach ($rule in $security.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
    if ($rule.AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow) { continue }
    $sid = $rule.IdentityReference.Value
    $hasWrite = ($rule.FileSystemRights -band $writeRights) -ne 0
    $hasRead = ($rule.FileSystemRights -band $readRights) -ne 0
    if ($hasWrite -and $sid -notin $AllowedSids) {
      throw "未获准主体仍可写入: $Path ($sid)"
    }
    if ($RejectRead -and ($hasRead -or $hasWrite) -and $sid -notin $AllowedSids) {
      throw "未获准主体仍可读取私有能力: $Path ($sid)"
    }
  }
}

function Clear-UnapprovedStateAclRules([string]$Path, [string[]]$AllowedSids) {
  $item = Get-Item -LiteralPath $Path -Force
  $security = $item.GetAccessControl()
  $remove = @(
    $security.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]) |
      Where-Object {
        $sid = $_.IdentityReference.Value
        $_.AccessControlType -eq [Security.AccessControl.AccessControlType]::Deny -or
          $sid -notin $AllowedSids
      }
  )
  foreach ($rule in $remove) {
    $security.RemoveAccessRuleSpecific($rule)
  }
  if ($remove.Count -gt 0) {
    $item.SetAccessControl($security)
  }
}

if ($PSCmdlet.ShouldProcess($payloadFull, 'apply immutable Fan Host payload ACL recursively')) {
  # SID notation is language-neutral. /T is mandatory: an inherited broad ACL
  # on a DLL is just as unsafe as one on the parent directory.
  # Preserve the installer process long enough to verify every result. Its
  # direct SID is removed as the final ACL operation below; a crash before
  # that removal is fail-closed because YeManFanHost rejects it on load.
  Invoke-Icacls @($payloadFull, '/grant:r', "$callerSid`:(F)", '/T', '/C', '/Q')
  Invoke-Icacls @($payloadFull, '/inheritance:r', '/T', '/C', '/Q')
  $payloadRemoveArguments = @($payloadFull, '/remove:g') + $untrustedSids + @('/T', '/C', '/Q')
  Invoke-Icacls $payloadRemoveArguments
  Invoke-Icacls @($payloadFull, '/grant:r', '*S-1-5-32-544:(F)', '*S-1-5-18:(F)', '*S-1-5-32-545:(RX)', '/T', '/C', '/Q')
  Invoke-Icacls @($payloadFull, '/setowner', '*S-1-5-32-544', '/T', '/C', '/Q')
}

$payloadVerificationSids = @($administratorsSid, $systemSid, $callerSidValue)
Assert-AllowedAcl $payloadFull $payloadVerificationSids $false
Assert-AllowedAcl $manifestPath $payloadVerificationSids $false
foreach ($relative in $expectedFiles.Values) {
  $path = Join-Path $payloadFull $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Fan Host payload 文件在 ACL 处理后不可用: $relative"
  }
  Assert-AllowedAcl $path $payloadVerificationSids $false
}
foreach ($entry in $manifest.files) {
  $path = Join-Path $payloadFull ([string]$entry.path)
  if ((Get-Sha256 $path) -ne ([string]$entry.sha256).ToLowerInvariant()) {
    throw "Fan Host manifest 哈希在 ACL 处理后不匹配: $($entry.path)"
  }
}
if ($PSCmdlet.ShouldProcess($payloadFull, 'remove temporary installer write grant')) {
  Invoke-Icacls @($payloadFull, '/remove:g', $callerSid, '/T', '/C', '/Q')
}

if (-not $SkipStateDirectory) {
  $expectedStateDirectory = Get-FullPath (Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host')
  $stateFull = Get-FullPath $StateDirectory
  if ($stateFull -ne $expectedStateDirectory) {
    throw "Fan Host 会话目录必须是: $expectedStateDirectory"
  }
  if ($PSCmdlet.ShouldProcess($stateFull, 'apply private Fan Host session ACL recursively')) {
    New-Item -ItemType Directory -Force -Path $stateFull | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stateFull 'logs') | Out-Null
    Invoke-Icacls @($stateFull, '/grant:r', "$callerSid`:(F)", '/T', '/C', '/Q')
    Invoke-Icacls @($stateFull, '/inheritance:r', '/T', '/C', '/Q')
    $stateRemoveArguments = @($stateFull, '/remove:g') + $untrustedSids + @('/T', '/C', '/Q')
    Invoke-Icacls $stateRemoveArguments
    Invoke-Icacls @($stateFull, '/grant:r', '*S-1-5-32-544:(F)', '*S-1-5-18:(F)', ("*$($identity.User.Value):(F)"), '/T', '/C', '/Q')
    # The root must also propagate the private capability ACL to logs and
    # future session files created by the resident Host.
    Invoke-Icacls @($stateFull, '/grant:r', '*S-1-5-32-544:(OI)(CI)(F)', '*S-1-5-18:(OI)(CI)(F)', ("*$($identity.User.Value):(OI)(CI)(F)"), '/C', '/Q')
    Invoke-Icacls @($stateFull, '/setowner', '*S-1-5-32-544', '/T', '/C', '/Q')
  }
  $stateAllowedSids = @($administratorsSid, $systemSid, $callerSidValue)
  # A previous redirected stream can retain a Logon SID (S-1-5-5-*) even
  # after the normal group ACL cleanup. State files are private capabilities:
  # normalize every explicit rule before accepting the directory again.
  Clear-UnapprovedStateAclRules $stateFull $stateAllowedSids
  foreach ($item in @(Get-ChildItem -LiteralPath $stateFull -Recurse -Force)) {
    Clear-UnapprovedStateAclRules $item.FullName $stateAllowedSids
  }
  Assert-AllowedAcl $stateFull $stateAllowedSids $true
  foreach ($item in @(Get-ChildItem -LiteralPath $stateFull -Recurse -Force)) {
    Assert-AllowedAcl $item.FullName $stateAllowedSids $true
  }
}

Write-Output "FAN_HOST_ACL_OK: $payloadFull"
if (-not $SkipStateDirectory) { Write-Output "FAN_HOST_STATE_ACL_OK: $expectedStateDirectory" }
