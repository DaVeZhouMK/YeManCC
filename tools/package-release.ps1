<#
.SYNOPSIS
  Assemble Build outputs and verified runtime assets into Release.

.DESCRIPTION
  Writes only to the workspace Build, Release and Backup areas. It never
  deploys to C:\SOFT\YeMan.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [ValidateSet('full', 'legacy-bridge', 'legacy-bootstrap')]
  [string]$ReleaseEnvelope = '',
  [string]$CustomSteamLibrarySource = ''
)

$ErrorActionPreference = 'Stop'

# ---- module-path self-heal ----------------------------------------------------------
# Same measured remedy as tools/ymcc_sim/lib/sim_core.ps1 (2026-09-22) and
# tools/opt-inheritance-gate.ps1: when this Windows PowerShell 5.1 process is launched
# through node/pnpm (as `pnpm run package` does, and as CI does under shell: pwsh) it
# inherits PowerShell 7's PSModulePath. The leading PowerShell 7 entries shadow
# Microsoft.PowerShell.Utility, so autoloading of Get-FileHash / Get-Content fails with
# "CommandNotFoundException" here or in any script this one invokes with the call
# operator (for example tools/build-custom-steam-library-host.ps1). Put this host's own
# module dir first. Idempotent.
$pkgHomeModules = Join-Path $PSHOME 'Modules'
$pkgFirstEntry = ($env:PSModulePath -split ';')[0]
if (-not $pkgFirstEntry -or ($pkgFirstEntry.TrimEnd('\') -ine $pkgHomeModules.TrimEnd('\'))) {
  $env:PSModulePath = $pkgHomeModules + ';' + $env:PSModulePath
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-ChildPath([string]$Child, [string]$Parent, [string]$Label) {
  $childFull = Get-FullPath $Child
  $parentFull = Get-FullPath $Parent
  if (-not $childFull.StartsWith($parentFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Label is outside the workspace: $childFull"
  }
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Get-LatestFileTimeUtc([string[]]$Paths) {
  $latest = [DateTime]::MinValue
  foreach ($p in $Paths) {
    if (Test-Path -LiteralPath $p -PathType Leaf) {
      $t = (Get-Item -LiteralPath $p -Force).LastWriteTimeUtc
      if ($t -gt $latest) { $latest = $t }
    }
  }
  return $latest
}

function Get-LatestDirectoryFileTimeUtc([string]$Dir, [string[]]$ExcludeRelativePrefixes) {
  $latest = [DateTime]::MinValue
  if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return $latest }
  foreach ($f in Get-ChildItem -LiteralPath $Dir -Recurse -File -Force) {
    $rel = Get-RelativePath $Dir $f.FullName
    $skip = $false
    foreach ($prefix in $ExcludeRelativePrefixes) {
      if ($rel.StartsWith($prefix + '\', [StringComparison]::OrdinalIgnoreCase)) { $skip = $true; break }
    }
    if ($skip) { continue }
    if ($f.LastWriteTimeUtc -gt $latest) { $latest = $f.LastWriteTimeUtc }
  }
  return $latest
}

# Export freshness guard.  A packaged artifact must never be older than the
# source it claims to represent.  Regression history: the QPC phase-stepping
# change lived in native/main.cpp but a stale pre-change build was packaged and
# exported, so the shipped behavior kept the old 60 Hz/16.7 ms timings.  This
# probe aborts packaging (fail-closed) when a build artifact is older than its
# matching source, so an un-rebuilt source can never enter a release package.
function Assert-ExportIsFresh([string]$ProjectRoot, [string]$WorkspaceRoot) {
  $tolerance = [TimeSpan]::FromSeconds(120)
  $nativeLatest = Get-LatestDirectoryFileTimeUtc (Join-Path $ProjectRoot 'native') @()
  $inputHostLatest = Get-LatestDirectoryFileTimeUtc (Join-Path $ProjectRoot 'InputHost') @('obj', 'bin')
  $srcLatest = Get-LatestDirectoryFileTimeUtc (Join-Path $ProjectRoot 'src') @()
  $configLatest = Get-LatestFileTimeUtc @(
    (Join-Path $ProjectRoot 'app.config.json'),
    (Join-Path $ProjectRoot 'version.json'))
  $webSourceLatest = if ($srcLatest -gt $configLatest) { $srcLatest } else { $configLatest }

  $nativeExePath = Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe'
  $inputHostProduct = Get-LatestDirectoryFileTimeUtc (Join-Path $WorkspaceRoot 'Build\App\Native\InputHost') @()
  $webProductPath = Join-Path $WorkspaceRoot 'Build\App\Web\index.html'
  if (-not (Test-Path -LiteralPath $nativeExePath -PathType Leaf) -or
      -not (Test-Path -LiteralPath $webProductPath -PathType Leaf)) {
    throw 'Export freshness guard: Build artifacts are missing; run build-workspace.ps1 before packaging.'
  }
  $nativeProduct = (Get-Item -LiteralPath $nativeExePath -Force).LastWriteTimeUtc
  $webProduct = (Get-Item -LiteralPath $webProductPath -Force).LastWriteTimeUtc

  $stale = @()
  if ($nativeLatest -gt $nativeProduct.Add($tolerance)) {
    $stale += "native 源码 ($($nativeLatest.ToString('o')) UTC) 晚于 YeManCC.exe 产物 ($($nativeProduct.ToString('o')) UTC) —— 产物未包含最新源码"
  }
  # Auxiliary RTSS binaries are part of the same native source ABI. An old
  # bridge/helper must not be combined with a fresh YeManCC.exe.
  foreach ($name in @('YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')) {
    $binary = Join-Path $WorkspaceRoot ('Build\App\Native\' + $name)
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "RTSS build dependency missing: $binary" }
    if ($nativeLatest -gt (Get-Item -LiteralPath $binary).LastWriteTimeUtc.Add($tolerance)) {
      $stale += "RTSS build dependency is older than native sources: $binary"
    }
  }
  if ($inputHostLatest -gt $inputHostProduct.Add($tolerance)) {
    $stale += "InputHost 源码 ($($inputHostLatest.ToString('o')) UTC) 晚于 InputHost 产物 ($($inputHostProduct.ToString('o')) UTC) —— 产物未包含最新源码"
  }
  if ($webSourceLatest -gt $webProduct.Add($tolerance)) {
    $stale += "web/配置 源码 ($($webSourceLatest.ToString('o')) UTC) 晚于 index.html 产物 ($($webProduct.ToString('o')) UTC) —— 产物未包含最新源码"
  }
  if ($stale.Count -gt 0) {
    throw "Export freshness guard REJECTED (禁止导出旧版构建):`n  " + ($stale -join "`n  ") + "`n  请先运行 build-workspace.ps1 重建后再打包。"
  }
  return $true
}

function Copy-DirectoryContents([string]$Source, [string]$Destination, [string[]]$Exclude = @(), [string]$RelativeRoot = '') {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Copy source is missing: $Source"
  }
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  # 2026-09-21 (FAN-920 §28): the previous form
  #   Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Recurse -Force
  # copies a directory INTO an already existing same-named directory, so a second
  # copy of the same resource produced Resources\Resources\... and
  # XInputPlus\XInputPlus\... (17 undeclared files in the section-26 complete
  # package). Merge-copy explicitly instead: an empty target, a target that
  # already contains the same-named directory and a repeated copy all converge to
  # exactly the source set. Type conflicts and reparse points fail closed.
  foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
    # HC-SLIM-01 R3 §4.1: an optional relative-path exclusion set lets a caller drop
    # named leaves from a merge-copy (used to omit the two loose XInput wrapper DLLs
    # from the HC delivery tree). Entries are relative to the top-level $Source and
    # matched case-insensitively; the accumulated prefix is threaded through recursion.
    $relative = if ([string]::IsNullOrEmpty($RelativeRoot)) { $item.Name } else { Join-Path $RelativeRoot $item.Name }
    if (@($Exclude).Count -gt 0 -and @($Exclude | Where-Object { $_ -ieq $relative }).Count -gt 0) { continue }
    $target = Join-Path $Destination $item.Name
    if ($item.PSIsContainer) {
      if ($item.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "Copy source contains a reparse point: $($item.FullName)"
      }
      if (Test-Path -LiteralPath $target -PathType Leaf) {
        throw "Copy destination type conflict (file where a directory is expected): $target"
      }
      Copy-DirectoryContents $item.FullName $target $Exclude $relative
    } else {
      if (Test-Path -LiteralPath $target -PathType Container) {
        throw "Copy destination type conflict (directory where a file is expected): $target"
      }
      Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
  }
}

function Normalize-PackageRelativePath([string]$Path) {
  $normalized = ([string]$Path).Replace('/', '\')
  if ([string]::IsNullOrWhiteSpace($normalized) -or [IO.Path]::IsPathRooted($normalized) -or
      $normalized.Contains(':') -or @($normalized -split '\\' | Where-Object { $_ -eq '..' -or $_ -eq '' }).Count -gt 0) {
    throw "Package manifest path is unsafe: $Path"
  }
  return $normalized
}

function Assert-CustomSteamLibraryPackage([string]$Source) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Custom Steam Library package source is missing: $Source"
  }
  $manifestPath = Join-Path $Source 'package-manifest.json'
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Custom Steam Library package-manifest.json is missing'
  }
  $manifestText = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
  $manifest = $manifestText.TrimStart([char]0xFEFF) | ConvertFrom-Json
  if ([string]$manifest.packageId -ne 'custom-steam-library') { throw 'Custom Steam Library packageId is invalid' }
  if ([string]$manifest.packageType -ne 'green-child') { throw 'Custom Steam Library packageType is invalid' }
  if ([string]$manifest.entryPoint -ne 'CustomSteamLibrary.exe') { throw 'Custom Steam Library entryPoint is invalid' }
  if ([string]$manifest.worker -ne 'SteamArtworkLab.exe') { throw 'Custom Steam Library worker is invalid' }
  if ([string]$manifest.updater.unknownPaths -ne 'preserve') { throw 'Custom Steam Library unknown-path policy must be preserve' }
  if ([int]$manifest.updater.healthHandshake.protocol -ne 1 -or [bool]$manifest.updater.healthHandshake.requiredBeforeCommit -ne $true) { throw 'Custom Steam Library health handshake policy is missing' }
  $manifestFilesRaw = @($manifest.files | ForEach-Object { Normalize-PackageRelativePath ([string]$_) })
  $managedFilesRaw = @($manifest.managedPaths | ForEach-Object { Normalize-PackageRelativePath ([string]$_) })
  $manifestFiles = @($manifestFilesRaw | Sort-Object -Unique)
  $managedFiles = @($managedFilesRaw | Sort-Object -Unique)
  if ($manifestFiles.Count -ne $manifestFilesRaw.Count -or $managedFiles.Count -ne $managedFilesRaw.Count) { throw 'Custom Steam Library manifest contains duplicate paths' }
  if (Compare-Object $manifestFiles $managedFiles) { throw 'Custom Steam Library files and managedPaths are inconsistent' }
  $expectedFiles = @($manifestFiles + 'package-manifest.json' | Sort-Object -Unique)
  $actualFiles = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force | ForEach-Object {
    Get-RelativePath $Source $_.FullName
  } | Sort-Object -Unique)
  if (Compare-Object $expectedFiles $actualFiles) {
    throw "Custom Steam Library package contains files outside its manifest: $($actualFiles -join ', ')"
  }
  $indexEntries = @($manifest.fileIndex)
  if ($indexEntries.Count -ne $manifestFiles.Count) { throw 'Custom Steam Library fileIndex is incomplete' }
  $indexedPathsRaw = @($indexEntries | ForEach-Object { Normalize-PackageRelativePath ([string]$_.path) })
  $indexedPaths = @($indexedPathsRaw | Sort-Object -Unique)
  if ($indexedPaths.Count -ne $indexedPathsRaw.Count -or (Compare-Object $manifestFiles $indexedPaths)) { throw 'Custom Steam Library fileIndex does not exactly cover files' }
  foreach ($entry in $indexEntries) {
    $relative = Normalize-PackageRelativePath ([string]$entry.path)
    $file = Join-Path $Source $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Custom Steam Library manifest file is missing: $relative" }
    if ((Get-Item -LiteralPath $file).Length -ne [int64]$entry.bytes) { throw "Custom Steam Library file size mismatch: $relative" }
    if ((Get-Sha256 $file) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "Custom Steam Library file hash mismatch: $relative" }
  }
  return $manifest
}

function Get-ZipEntrySha256([System.IO.Compression.ZipArchiveEntry]$Entry) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = $Entry.Open()
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Sync-CustomSteamLibraryMainline([string]$Source, [string]$Version) {
  # Only the current in-repository child can enter a mainline release. Never
  # import archived SteamArtworkLab outputs or re-hash a drifted payload here.
  $canonical = Get-FullPath (Join-Path $ProjectRoot 'CustomSteamLibrary')
  if ((Get-FullPath $Source) -ine $canonical) { throw "Child source is not current mainline: $Source" }
  Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $ProjectRoot
  $manifest = Assert-CustomSteamLibraryPackage $Source
  if ([string]$manifest.packageVersion -ne $Version) { throw 'Child package version did not follow mainline' }
  Assert-CustomSteamLibrarySourceFreshness -ProjectRoot $ProjectRoot -PackageRoot $Source | Out-Null
  return $manifest
}

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

$installerName = 'HidHide_1.5.230_x64.exe'
$installerSha256 = 'F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6'

# HC-SLIM-01 size-order export policy (2026-10-07). These are retained
# authority files, not deletion candidates. The two shared utility DLLs are
# intentionally carried only by the HC runtime and resolved by the pinned
# InputHost resolver; any local duplicate is a packaging error.
$hcRuntimeName = 'HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
$hcSlimSharedFallbackHashes = [ordered]@{
  'Nefarius.Utilities.DeviceManagement.dll' = 'B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C'
  'Newtonsoft.Json.dll' = 'A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B'
}
$hcSlimSizeOrderRetainedHashes = [ordered]@{
  'Microsoft.Windows.SDK.NET.dll' = '0EC371D93798852E36461C8ADDDBEADCE0F963A04752F0B64E54FE19C1C834A7'
  'HandheldCompanion.dll' = '0C5132A9D13AEBFC5ADD2AA7C9AC54E8DAAA8EBC816A0C68D099BB9685DC2E49'
  'iNKORE.UI.WPF.Modern.dll' = '7650B744E0ADD04549593F680EBF1C8FE40125F36EB8C86445113B974EE25D6C'
  'libVIIPER.dll' = 'E2802542C7FB632914384AC3DBD4A09FA378A9BE91D04EBE822648B15E4960F7'
}

$hcSlimHidMaestroSha256 = 'BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E'

function Assert-HcSlimSourcePolicy([string]$PowerControlRoot) {
  $virtualGamepad = Join-Path $PowerControlRoot 'feature-assets\virtual-gamepad'
  $runtime = Join-Path $PowerControlRoot "handheldcompanion-runtime\$hcRuntimeName"
  foreach ($name in $hcSlimSharedFallbackHashes.Keys) {
    $local = Join-Path $virtualGamepad $name
    if (Test-Path -LiteralPath $local) {
      throw "HC-SLIM-01 source policy violation: local virtual-gamepad duplicate must not be exported: $local"
    }
    $authority = Join-Path $runtime $name
    if (-not (Test-Path -LiteralPath $authority -PathType Leaf) -or
        (Get-Sha256 $authority) -ne $hcSlimSharedFallbackHashes[$name]) {
      throw "HC-SLIM-01 shared authority missing or changed: $name"
    }
  }
  foreach ($name in $hcSlimSizeOrderRetainedHashes.Keys) {
    $authority = Join-Path $runtime $name
    if (-not (Test-Path -LiteralPath $authority -PathType Leaf) -or
        (Get-Sha256 $authority) -ne $hcSlimSizeOrderRetainedHashes[$name]) {
      throw "HC-SLIM-01 retained authority missing or changed: $name"
    }
  }
  $hid = Join-Path $virtualGamepad 'HIDMaestro.Core.dll'
  # The source template normally contains flags only. InputHost/HIDMaestro
  # are assembled from Build\App\Native\InputHost, not from this template.
  if ((Test-Path -LiteralPath $hid) -and
      (-not (Test-Path -LiteralPath $hid -PathType Leaf) -or
       (Get-Sha256 $hid) -ne $hcSlimHidMaestroSha256)) {
    throw 'HC-SLIM-01 source HIDMaestro.Core.dll identity mismatch'
  }
}

function Assert-HcSlimStagedPolicy([string]$PowerControlRoot) {
  Assert-HcSlimSourcePolicy $PowerControlRoot
  $hid = Join-Path $PowerControlRoot 'feature-assets\virtual-gamepad\HIDMaestro.Core.dll'
  if (-not (Test-Path -LiteralPath $hid -PathType Leaf) -or
      (Get-Sha256 $hid) -ne $hcSlimHidMaestroSha256) {
    throw 'HC-SLIM-01 HIDMaestro.Core.dll is required at its retained identity'
  }
}

function Test-IsExcludedPowerControlPath([string]$Relative, [bool]$IsDirectory) {
  $r = $Relative.Replace('/', '\')
  # YMCC Decky sidebar: exact immutable payload allowlist, before generic dist/JSON exclusions.
  # Never ship runtime settings/logs/temp, arbitrary plugins or added DLLs from this home.
  if ($r -match '^decky(?:\\|$)') {
    if ($IsDirectory) {
      return $r -notin @('decky', 'decky\plugins', 'decky\plugins\ymcc-sidebar', 'decky\plugins\ymcc-sidebar\dist')
    }
    return $r -notin @(
      'decky\PluginLoader_noconsole.exe',
      'decky\LICENSE.decky-loader',
      'decky\plugins\ymcc-sidebar\dist\index.js',
      'decky\plugins\ymcc-sidebar\package.json',
      'decky\plugins\ymcc-sidebar\plugin.json',
      'decky\plugins\ymcc-sidebar\LICENSE.decky-api'
    )
  }
  if ($r -match '^fan-host(?:-v2|-quarantine|\\|$)') { return $true }
  if ($r -match '^gamepad-prerequisites(?:\\|$)') { return $true }
  # HC-SLIM-01 R3 §4.1（2026-10-06）：HC 交付树内两个松散 XInput 包装 DLL 是
  # HandheldCompanion.dll 的编译期嵌入资源（Properties/Resources*.resx 的
  # ResXFileRef 在构建时嵌入），运行期没有任何松散文件消费者（唯一代码消费者
  # XInputPlus.cs 读的是嵌入资源）。仅从发布差集排除这两条松散件；保留 resx
  # 源输入、冻结 HC DLL、XInputPlus 目标件及原重建逻辑，不全目录清理。
  if ($r -match '^handheldcompanion-runtime\\[^\\]+\\Resources\\xinput1_x(64|86)\.dll$') { return $true }
  if ($r -match '^redist(?:\\|$)') {
    # Only the explicitly locked HidHide installer is a normal product/update asset.
    # Keep every other redist item on its existing separate packaging lane.
    if (-not $IsDirectory -and $r -ieq ("redist\" + $installerName)) { return $false }
    return $true
  }
  if ($r -match '(^|\\)(\.git|build|dist|__pycache__|KX\.bak_removed|product-old-files-[^\\]+)(\\|$)') { return $true }
  if ($r -match '^(TPD|intel|ryzenadj|tools|pawnio|OpenSpeedy|RTSS-Overlays)(\\|$)') { return $true }
  if ($IsDirectory) { return $false }
  if ($r -match '\.bak(?:_|$)' -or $r -match '\.(obj|pdb|ilk|log|pid|hb|tmp)$') { return $true }
  # light-setter（L1 方案 B，2026-09-16）：一等产品运行时资产。native 直调
  # PowerControl\light-setter\YeManLightSetter.exe（直驱 HC 程序集控制非 ROG
  # 机型灯光），缺失时 native fail-safe 为 light-setter-missing（功能降级）。
  # 与 feature-assets 私有域不同：随正式包一并交付；.pdb 由上方通用规则排除。
  if ($r -match '^light-setter(\\|$)') { return $false }
  if ($r -match '\.(md|py|spec)$' -and $r -ine 'fan-host\YeManFanHost.authorization.md') { return $true }
  if ($r -in @('.gitignore', 'AUTOFLOAT_SPEC.md', 'Test-AutoRotation-Repair.bat', 'Test-AutoRotation-Repair.vbs', 'physpanel.exe')) { return $true }
  if ($r -match '^MG-AUTO\\(memreduct\.exe|memreduct\.exe\.sig|memreduct\.ini|memreduct\.lng|memreduct\.sig|portable\.dat)$') { return $true }
  if ($r -in @(
    'yeman-settings.json', 'yeman-settings.json.bak',
    'ui-settings.json', 'summon.json', 'music_player.json',
    'performance-schedule.json', 'game-custom.json', 'control-config.json',
    'autofloat.json', 'tdp-auto-apply.json', 'cpu_profiles.json',
    'cpu_autostart.json', 'cpu_auto_enable.json', 'cpu_lock.json',
    'launch_apps.json', 'boot_config.json', 'yeman-power-scheme.json',
    'tray_resident.json', 'autoclose.json', 'Power.txt',
    'ui-background\background.json', 'ui-background\dynamic-online.json',
    'ui-background\dynamic-cache.json', 'ui-background\background.mp4',
    'Sleep\Enable.txt', 'Sleep\Escalation.txt', 'Sleep\sleepguard.json',
    'Sleep\target.txt', 'Sleep\睡眠击杀名单.txt',
    'Sleep\quickapp_suspended.json', 'Sleep\sleep-trigger-last.txt',
    'Sleep\resleep-last.txt'
  )) { return $true }
  if ($r -match '^Sleep\\controlled-sleep') { return $true }
  if ($r -match '^(float-active|fps-monitor\.(hb|pid|log)|hwinfo-ok|hwinfo-recovery\.ts|speedhack\.log|startup_trace\.txt|topmon\.json)$') { return $true }
  if ($r -match '^(FPS-|tdp-).+\.txt$' -or $r -match '^yeman-gcm-search-result.*\.json$') { return $true }
  if ($r -match '^handheldcompanion-runtime\\' -and ($r -match '\.json$' -or $r -match '\.md$')) { return $false }
  if ($r -like 'feature-assets\virtual-gamepad\.test-sidecar.json' -or $r -like 'feature-assets\gyro-motion\.test-sidecar.json') { return $false }
  if ($r -like 'feature-assets\virtual-gamepad\input-capture.flag' -or $r -like 'feature-assets\gyro-motion\input-capture.flag') { return $false }
  if ($r -like 'feature-assets\virtual-gamepad\YeManInputHost.deps.json' -or $r -like 'feature-assets\virtual-gamepad\YeManInputHost.runtimeconfig.json') { return $false }
  if ($r -like 'feature-assets\virtual-gamepad\test-auto-install.flag') { return $true }
  if ($r -match '\.json$' -and $r -notmatch '^(fan-host|handheldcompanion-runtime)\\') {
    throw "Unclassified PowerControl JSON must be added to the release policy: $r"
  }
  return $false
}

function Copy-PowerControlTemplates([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  foreach ($entry in Get-ChildItem -LiteralPath $Source -Recurse -Force) {
    $relative = Get-RelativePath $Source $entry.FullName
    if (Test-IsExcludedPowerControlPath $relative $entry.PSIsContainer) { continue }
    $target = Join-Path $Destination $relative
    if ($entry.PSIsContainer) {
      New-Item -ItemType Directory -Force -Path $target | Out-Null
    } else {
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
      Copy-Item -LiteralPath $entry.FullName -Destination $target -Force
    }
  }
}

function Move-ExistingReleaseItem([string]$Path, [string]$BackupRoot) {
  if (-not (Test-Path -LiteralPath $Path)) { return }
  $item = Get-Item -LiteralPath $Path -Force
  if ($item.PSIsContainer -and @(Get-ChildItem -LiteralPath $Path -Force).Count -eq 0) {
    Remove-Item -LiteralPath $Path -Force
    return
  }
  New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
  Move-Item -LiteralPath $Path -Destination (Join-Path $BackupRoot $item.Name)
}

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
# Read-only V2 path/payload preflight before staging/Release rotation.
& (Join-Path $PSScriptRoot 'fan_host_path_gate.ps1') -ProjectRoot $ProjectRoot -SourceOnly | Out-Null
& (Join-Path $PSScriptRoot 'verify-source-release-inputs.ps1') -ProjectRoot $ProjectRoot
$versionInfo = Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$packageInfo = Get-Content -LiteralPath (Join-Path $ProjectRoot 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$versionInfo.version
$packageVersion = [string]$packageInfo.version
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw "version.json has an invalid strict version: $version" }
$versionParts = @([int64]$Matches[1], [int64]$Matches[2], [int64]$Matches[3])
if (@($versionParts | Where-Object { $_ -gt [int]::MaxValue }).Count -gt 0) { throw "version.json version part is out of range: $version" }
if ($packageVersion -ne $version) { throw "Version mismatch: version.json=$version, package.json=$packageVersion" }
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$BuildRoot = Join-Path $WorkspaceRoot 'Build'
$ReleaseRoot = Join-Path $WorkspaceRoot 'Release'
$BackupReleaseRoot = Join-Path $WorkspaceRoot 'Backup\Release'
$AssetsRoot = Join-Path $WorkspaceRoot 'Assets'
$BuildWeb = Join-Path $BuildRoot 'App\Web'
$BuildNative = Join-Path $BuildRoot 'App\Native'
$PackageBuildRoot = Join-Path $BuildRoot 'Package'
$StagingRoot = Join-Path $PackageBuildRoot 'Staging'
$StagingYeManCC = Join-Path $StagingRoot 'YeManCC'
$StagingPowerControl = Join-Path $StagingRoot 'PowerControl'
$StagingCustomSteamLibrary = Join-Path $StagingYeManCC 'CustomSteamLibrary'
$UpdateRoot = Join-Path $PackageBuildRoot 'UpdateRoot'
$ReleaseYeManCC = Join-Path $ReleaseRoot 'YeManCC'
$ReleasePowerControl = Join-Path $ReleaseRoot 'PowerControl'
$ReleaseCustomSteamLibrary = Join-Path $ReleaseYeManCC 'CustomSteamLibrary'
$LegacyReleaseCustomSteamLibrary = Join-Path $ReleaseRoot 'CustomSteamLibrary'
$ReleasePackages = Join-Path $ReleaseRoot 'Packages'

$customSteamLibrarySourceFromEnv = [string]$env:YEMAN_CUSTOM_STEAM_LIBRARY_SOURCE
if (-not [string]::IsNullOrWhiteSpace($customSteamLibrarySourceFromEnv) -and [string]::IsNullOrWhiteSpace($CustomSteamLibrarySource)) {
  throw 'CustomSteamLibrary source override requires the explicit -CustomSteamLibrarySource parameter; refusing a stale environment override.'
}
$canonicalCustomSteamLibrarySource = Get-FullPath (Join-Path $ProjectRoot 'CustomSteamLibrary')
$CustomSteamLibrarySource = if ([string]::IsNullOrWhiteSpace($CustomSteamLibrarySource)) {
  $canonicalCustomSteamLibrarySource
} else {
  Get-FullPath $CustomSteamLibrarySource
}
if ($CustomSteamLibrarySource -ne $canonicalCustomSteamLibrarySource) {
  throw "CustomSteamLibrary source must be the formal mainline directory: $canonicalCustomSteamLibrarySource"
}

# Capture current compiler inputs in fresh directories, then validate without
# modifying the declared file/source fingerprints. No legacy lab synchronization.
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $ProjectRoot
& (Join-Path $PSScriptRoot 'build-custom-steam-library-host.ps1') `
  -OutputDirectory (Join-Path $WorkspaceRoot 'Build\CustomSteamLibrary') -Publish
& (Join-Path $PSScriptRoot 'build-custom-steam-library-worker.ps1') `
  -OutputDirectory (Join-Path $WorkspaceRoot 'Build\CustomSteamLibrary') -Publish
Sync-CustomSteamLibraryMainline $CustomSteamLibrarySource $version | Out-Null

# The ZIP envelope is manifest-driven. The legacy bridge keeps the old
# YeManCC + PowerControl envelope so a pre-manifest updater can install the
# new updater. The bootstrap variant also embeds the ready CustomSteamLibrary
# under YeManCC while omitting the manifest from the ZIP, so the broken
# pre-manifest PowerShell helper takes its safe no-manifest path.
$releaseEnvelopeFromEnv = [string]$env:YEMAN_RELEASE_ENVELOPE
if ([string]::IsNullOrWhiteSpace($ReleaseEnvelope)) {
  if (-not [string]::IsNullOrWhiteSpace($releaseEnvelopeFromEnv) -and $releaseEnvelopeFromEnv -ne 'full') {
    throw 'Legacy release envelope requires the explicit -ReleaseEnvelope parameter; refusing to overwrite the normal Release package.'
  }
  $releaseEnvelope = 'full'
} else {
  $releaseEnvelope = $ReleaseEnvelope
}
if ($releaseEnvelope -ne 'full') {
  throw "Legacy release envelopes are retired (2026-09-23 ruling: only the single complete package is produced). Unsupported envelope: $releaseEnvelope"
}
$isLegacyBootstrap = $false
$isLegacyBridge = $false
$updateLayoutRoots = @(
  [ordered]@{ source = 'YeManCC'; target = 'YeManCC'; mode = 'program' },
  [ordered]@{ source = 'PowerControl'; target = 'PowerControl'; mode = 'power-control' }
)
$embedCustomSteamLibrary = -not $isLegacyBridge -or $isLegacyBootstrap
$requiredUpdateRoots = @($updateLayoutRoots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
$includeFanHostInFullTestPackage = $true
$fanHostUpdatePolicy = 'preserve-existing' # public 0.0.28 owns the legacy fan-host lane
$fanHostV2UpdatePolicy = 'replace'

foreach ($path in @($BuildRoot, $ReleaseRoot, $PackageBuildRoot, $StagingRoot, $UpdateRoot, $ReleaseYeManCC, $ReleasePowerControl, $ReleaseCustomSteamLibrary, $ReleasePackages)) {
  Assert-ChildPath $path $WorkspaceRoot 'Task5 output'
}
if ((Get-FullPath $ReleaseYeManCC) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC'))) { throw 'Unexpected YeManCC release target' }
if ((Get-FullPath $ReleasePowerControl) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\PowerControl'))) { throw 'Unexpected PowerControl release target' }
if ((Get-FullPath $ReleaseCustomSteamLibrary) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC\CustomSteamLibrary'))) { throw 'Unexpected CustomSteamLibrary release target' }

$requiredBuild = @(
  (Join-Path $BuildWeb 'index.html'),
  (Join-Path $BuildWeb 'assets'),
  (Join-Path $BuildWeb 'app.config.json'),
  (Join-Path $BuildNative 'YeManCC.exe'),
  (Join-Path $BuildNative 'YeManRecoveryService.exe'),
  (Join-Path $BuildNative 'YMCCRtssProfileHelper.exe'),
  (Join-Path $BuildNative 'YMCCOverlayBridge.dll')
)
foreach ($path in $requiredBuild) {
  if (-not (Test-Path -LiteralPath $path)) { throw "Build output is incomplete: $path" }
}
# Freshness gate: never package an artifact older than its source (see
# Assert-ExportIsFresh for the QPC/0910-17 stale-build regression this prevents).
# Verify compiled passive JS/source/license identity and the locked Windows loader before export.
& node (Join-Path $ProjectRoot 'tools\build-decky-sidebar-plugin.mjs') --check
if ($LASTEXITCODE -ne 0) { throw "Decky sidebar payload freshness rejected: exit=$LASTEXITCODE" }
Assert-ExportIsFresh $ProjectRoot $WorkspaceRoot | Out-Null

# ================================================================
# FanLab source baseline follows the batch (2026-09-27).
# `pnpm run test:fanhost-source-baseline` is a read-only, fail-closed check that lives OUTSIDE this
# chain, so an approved Fan batch that touched FanLab/real-host sources left it red until someone
# re-recorded the baseline by hand (the 2026-09-02 snapshot went stale exactly that way: 09-21 was its
# last re-record while two adjudicated batches changed Program.cs / Section18..19 afterwards).
# The exporter now owns the re-record: the guard previews read-only, recomputes every value from the
# tree (never hand-copied), prints and records the added/removed/changed delta, keeps the previous
# values nested inside the baseline, and re-runs the read-only verifier as the cross-check. It is a
# no-op when the baseline is already in step, and any failure aborts the export (terminating error).
# ================================================================
$ensureSourceBaseline = Join-Path $ProjectRoot 'tools\ensure-fanhost-source-baseline.ps1'
if (!(Test-Path -LiteralPath $ensureSourceBaseline -PathType Leaf)) {
  throw "FanLab source-baseline guard missing: $ensureSourceBaseline"
}
& $ensureSourceBaseline

# ================================================================
# Adopted-optimization inheritance gate - pre-flight (release ruling, 2026-09-20).
# Re-hashes the Build stage against the per-build capture and re-probes every
# adopted optimization (binary marker + source anchor). A missing capture or a
# tampered Build output aborts here, before any staging/Release mutation.
# No silent skip switch exists by design.
# ================================================================
$OptGateScript = Join-Path $ProjectRoot 'tools\opt-inheritance-gate.ps1'
$OptBuildManifest = Join-Path $WorkspaceRoot 'Build\Validation\opt-gate\BUILD__build.json'
if (-not (Test-Path -LiteralPath $OptBuildManifest -PathType Leaf)) {
  throw "Adopted-optimization inheritance gate: missing build capture $OptBuildManifest. Run build-workspace.ps1 (which writes it) before packaging."
}
& $OptGateScript -Mode Verify -Manifest $OptBuildManifest -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED (pre-flight, exit=$LASTEXITCODE): Build output does not inherit the adopted optimization set."
}

if (Test-Path -LiteralPath $StagingRoot) { Remove-Item -LiteralPath $StagingRoot -Recurse -Force }
if (Test-Path -LiteralPath $UpdateRoot) { Remove-Item -LiteralPath $UpdateRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $StagingYeManCC, $StagingPowerControl, $UpdateRoot | Out-Null

Copy-DirectoryContents $BuildWeb $StagingYeManCC
Copy-Item -LiteralPath (Join-Path $BuildNative 'YeManCC.exe') -Destination (Join-Path $StagingYeManCC 'YeManCC.exe') -Force
Copy-Item -LiteralPath (Join-Path $BuildNative 'YeManRecoveryService.exe') -Destination (Join-Path $StagingYeManCC 'YeManRecoveryService.exe') -Force
Copy-Item -LiteralPath (Join-Path $BuildNative 'YMCCRtssProfileHelper.exe') -Destination (Join-Path $StagingYeManCC 'YMCCRtssProfileHelper.exe') -Force
Copy-Item -LiteralPath (Join-Path $BuildNative 'YMCCOverlayBridge.dll') -Destination (Join-Path $StagingYeManCC 'YMCCOverlayBridge.dll') -Force
# YeManCC/InputHost remains outside the mainline release lane.
# Operator instruction: the locked HidHide installer is a virtual-gamepad
# prerequisite and must ship at PowerControl/redist in Release and updater
# payloads. Other redist contents remain excluded here; the complete package
# separately assembles the locked CLI and HIDMaestro setup bundle.
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'version.json') -Destination (Join-Path $StagingYeManCC 'version.json') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'YeMan-Support.html') -Destination (Join-Path $StagingYeManCC 'YeMan-Support.html') -Force

function Assert-FanHostMainlinePayload([string]$payloadRoot) {
  # 2026-09-18: a full package must carry the MAINLINE fan-host payload. The
  # re-pinned verifier is the single source of truth for the expected hashes;
  # anything that cannot be proven to be the mainline export must fail closed
  # here instead of shipping an ambiguous package (drift prevention).
  $verifier = Join-Path $PSScriptRoot 'verify-r5v9-fan-host-payload.ps1'
  if (-not (Test-Path -LiteralPath $verifier -PathType Leaf)) {
    throw "Fan Host mainline payload verifier missing: $verifier"
  }
  & $verifier -PayloadRoot $payloadRoot
}
# GP-PREREQ-2: build redist setup and pure tests only; never install drivers during packaging.
& (Join-Path $PSScriptRoot 'build-gamepad-prerequisites.ps1') -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw 'Gamepad prerequisites payload build failed.' }
$sourcePowerControl = Join-Path $ProjectRoot 'PowerControl'
Assert-HcSlimSourcePolicy $sourcePowerControl
Copy-PowerControlTemplates $sourcePowerControl $StagingPowerControl
# Keep the release/updater tree self-contained even when the installer exists
# only in the operator-reserved default redist path. Never accept a drifted file.
$stagedHidHideInstaller = Join-Path $StagingPowerControl ("redist\" + $installerName)
if (-not (Test-Path -LiteralPath $stagedHidHideInstaller -PathType Leaf)) {
  $hidHideInstallerSource = @(
    (Join-Path $sourcePowerControl ("redist\" + $installerName)),
    (Join-Path $ReleaseRoot ("PowerControl\redist\" + $installerName)),
    ("C:\SOFT\YeMan\PowerControl\redist\" + $installerName)
  ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
  if (-not $hidHideInstallerSource) { throw "Missing locked HidHide installer from PowerControl/redist or C:\SOFT\YeMan\PowerControl\redist: $installerName" }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stagedHidHideInstaller) | Out-Null
  Copy-Item -LiteralPath $hidHideInstallerSource -Destination $stagedHidHideInstaller -Force
}
if ((Get-Sha256 $stagedHidHideInstaller) -ne $installerSha256) { throw "Release/updater has an unexpected $installerName hash" }
if ($includeFanHostInFullTestPackage) {
  # Fan Host is excluded by the generic PowerControl template policy because
  # normal updater releases preserve the installed, separately authenticated
  # Host. The full package is explicitly the opposite: it carries the MAINLINE
  # payload (sourcePowerControl\fan-host == ProjectRoot\PowerControl\fan-host,
  # i.e. the mainline export) so the tester receives one complete runnable
  # package. 2026-09-18: this payload is verified against the re-pinned hashes
  # before staging -- a payload that cannot be proven to be the mainline export
  # must fail closed instead of shipping an ambiguous package.
  Assert-FanHostMainlinePayload (Join-Path $sourcePowerControl 'fan-host')
  Copy-DirectoryContents (Join-Path $sourcePowerControl 'fan-host') (Join-Path $StagingPowerControl 'fan-host-v2')
}
Sync-CustomSteamLibraryMainline $CustomSteamLibrarySource $version | Out-Null
if ($embedCustomSteamLibrary) {
  Copy-DirectoryContents $CustomSteamLibrarySource $StagingCustomSteamLibrary
}

# 2026-09-23（W3 逐条检查批，operator 授权）：OpenXInput 代理进程序根。
# 裁定（2026-09-23）后唯一产物 = YeManCC.zip，且该包同时充当更新输入
# 与干净安装快照；旧链路里只有 tools\deploy-installed.ps1（L546-556）会把 HC 锁
# runtime 的 Xinput1_4.dll 拷到 YeManCC.exe 同目录，纯解包式安装/更新不会再走它。
# 实测：2026-09-22 身份核对 xinputProxy=MATCH，2026-09-23 08:32 MISSING（安装树与
# 包逐字节相等 ⇒ 包本身就没带该文件）⇒ native 的 ymccResolveOpenXInput 拿不到
# OpenXInput* 导出，槽位指派路径失效。把锁定代理放进程序根 staging 后，Release 与
# 完整包由既有拷贝链路自动带上，任何解包/更新都会落在 exe 同目录。
# 注意：下面的 HC 目录名必须与后文 $hcRuntimeName 保持一致。
$openXInputProxyName = 'Xinput1_4.dll'
$openXInputProxySha256 = 'C89A11166ACD59AA27DADA45B9C64494221C04ACB0B0886EA393C5D36F9071EC'
$openXInputProxySource = Join-Path $sourcePowerControl 'handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\Xinput1_4.dll'
if (-not (Test-Path -LiteralPath $openXInputProxySource -PathType Leaf)) { throw "OpenXInput proxy is missing from HC runtime lock: $openXInputProxySource" }
$openXInputProxySourceHash = Get-Sha256 $openXInputProxySource
if ($openXInputProxySourceHash -ne $openXInputProxySha256) { throw "OpenXInput proxy hash is not the locked one: $openXInputProxySourceHash" }
Copy-Item -LiteralPath $openXInputProxySource -Destination (Join-Path $StagingYeManCC $openXInputProxyName) -Force
$openXInputProxyStagedHash = Get-Sha256 (Join-Path $StagingYeManCC $openXInputProxyName)
if ($openXInputProxyStagedHash -ne $openXInputProxySha256) { throw "OpenXInput proxy drift while staging: $openXInputProxyStagedHash" }

# Bind future incoming Host/runtime to this authenticated full ZIP, never to the old client build.
$fanBindingManifestPath = Join-Path $sourcePowerControl 'fan-host\YeManFanHost.payload.json'
$fanBindingManifest = (Get-Content -LiteralPath $fanBindingManifestPath -Raw -Encoding UTF8) | ConvertFrom-Json
$fanRuntimeBindingDirectory = 'handheldcompanion-runtime\' + $hcRuntimeName
$fanRuntimeBindingManifestPath = Join-Path (Join-Path $sourcePowerControl $fanRuntimeBindingDirectory) 'HandheldCompanion.runtime.json'
if (-not (Test-Path -LiteralPath $fanRuntimeBindingManifestPath -PathType Leaf)) { throw 'Missing incoming fan runtime manifest for update binding' }
$updateLayoutManifest = [ordered]@{
  schemaVersion = 1
  packageId = 'yemancc-update'
  packageVersion = $version
  requiredRoots = $requiredUpdateRoots
  roots = $updateLayoutRoots
  rules = [ordered]@{
    unknownRoots = 'reject-unless-declared'
    unknownFiles = 'preserve-when-targeted'
    rollback = 'per-root'
    fanHost = $fanHostUpdatePolicy
    fanHostV2 = $fanHostV2UpdatePolicy
    gyroVirtualAssets = 'include'
    fanHostPayload = [ordered]@{
      directory = 'fan-host-v2'
      schemaVersion = 2
      manifestSha256 = Get-Sha256 $fanBindingManifestPath
      fileCount = @($fanBindingManifest.files).Count
      runtimeDirectory = $fanRuntimeBindingDirectory
      runtimeManifestSha256 = Get-Sha256 $fanRuntimeBindingManifestPath
    }
  }
}
$updateLayoutManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $StagingYeManCC 'update-manifest.json') -Encoding UTF8

$lockPath = Join-Path $ProjectRoot 'tools\release-assets.lock.json'
$assetLock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$workspaceAssetsComplete = (Test-Path -LiteralPath $AssetsRoot -PathType Container) -and
  (@($assetLock.files | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $AssetsRoot ([string]$_.assetPath).Replace('/', '\')) -PathType Leaf)
  }).Count -eq 0)
# A task checkout may contain an intentionally empty/incomplete Assets
# directory. Treat that as absent and use the verified PowerControl fallback;
# an empty directory must not shadow a complete task-local fallback set.
$useWorkspaceAssets = $workspaceAssetsComplete
$assetSources = @{}
foreach ($entry in $assetLock.files) {
  $source = if ($useWorkspaceAssets) {
    Join-Path $AssetsRoot ([string]$entry.assetPath).Replace('/', '\')
  } else {
    Join-Path $ProjectRoot ([string]$entry.fallbackPath).Replace('/', '\')
  }
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Release asset is missing: $source" }
  $hash = Get-Sha256 $source
  if ($hash -ne [string]$entry.sha256) { throw "Release asset hash mismatch: $source" }
  $destination = Join-Path $StagingPowerControl ([string]$entry.releasePath).Replace('/', '\')
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath $source -Destination $destination -Force
  $assetSources[[string]$entry.component] = if ($useWorkspaceAssets) { 'workspace-assets' } else { 'verified-source-fallback' }
}

$requiredProgram = @('YeManCC.exe', 'YeManRecoveryService.exe', 'YMCCRtssProfileHelper.exe', 'YMCCOverlayBridge.dll', 'index.html', 'app.config.json', 'version.json', 'YeMan-Support.html', 'update-manifest.json')
foreach ($name in $requiredProgram) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingYeManCC $name) -PathType Leaf)) { throw "Release program file is missing: $name" }
}
if (-not (Test-Path -LiteralPath (Join-Path $StagingYeManCC 'assets') -PathType Container)) { throw 'Release web assets directory is missing' }

$requiredPowerControl = @(
  'TDP',
  'OpenSpeedy\bridge32.exe', 'OpenSpeedy\bridge64.exe',
  'MG-AUTO\memreduct.exe', 'physpanel.exe',
  # R-pawnio-manifest（2026-09-11）：pawnio 是根 5 件 + _internal 16 件的
  # 原子运行时，任何一件缺失（尤其 _internal\base_library.zip）都会让
  # YeManTdpCtl 弹 "Failed to start embedded python interpreter!"。
  # 【栽坑史】历史发布清单只列根 5 件 + python313.dll，base_library.zip 等
  # _internal 件缺席；本清单以 release-assets.lock.json 的 PawnIO 原子集为准，
  # 与 staging 检查（下方 pawnioExpected/pawnioActual 全量对比）双保险。
  'pawnio\YeManTdpCtl.exe', 'pawnio\PawnIO_setup.exe',
  'pawnio\AMDFamily17.bin', 'pawnio\IntelMSR.bin',
  'pawnio\RyzenSMU.bin',
  'pawnio\_internal\python313.dll',
  'pawnio\_internal\base_library.zip',
  'pawnio\_internal\COPYING',
  'pawnio\_internal\libcrypto-3.dll', 'pawnio\_internal\libffi-8.dll',
  'pawnio\_internal\libssl-3.dll',
  'pawnio\_internal\select.pyd', 'pawnio\_internal\unicodedata.pyd',
  'pawnio\_internal\VCRUNTIME140.dll',
  'pawnio\_internal\_bz2.pyd', 'pawnio\_internal\_ctypes.pyd',
  'pawnio\_internal\_decimal.pyd', 'pawnio\_internal\_hashlib.pyd',
  'pawnio\_internal\_lzma.pyd', 'pawnio\_internal\_socket.pyd',
  'pawnio\_internal\_ssl.pyd',
  'RTSS-Overlays\YeManOBS-W-1.ovl',
  'RTSS-Overlays\YeManOBS-L-1.ovl',
  'RTSS-Overlays\YeManOBS-JJ-1.ovl',
  'RTSS-Overlays\Empty.ovl',
  # light-setter（2026-09-16）：native 固定查找该 EXE（非 ROG 灯光通路），
  # 正式包必须携带，否则更新后灯光功能静默降级为 light-setter-missing。
  'light-setter\YeManLightSetter.exe',
  'light-setter\YeManLightSetter.dll',
  'light-setter\YeManLightSetter.runtimeconfig.json'
)
foreach ($relative in $requiredPowerControl) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingPowerControl $relative))) { throw "Release PowerControl item is missing: $relative" }
}
foreach ($name in @('gyro-motion','virtual-gamepad')) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingPowerControl "feature-assets\$name\enabled.flag") -PathType Leaf)) { throw "Release PowerControl feature-assets\$name\enabled.flag is missing" }
}
$pawnioExpected = @($assetLock.files | Where-Object component -eq 'PawnIO' | ForEach-Object { ([string]$_.releasePath).Substring('pawnio/'.Length).Replace('/', '\') } | Sort-Object)
$pawnioActual = @(Get-ChildItem -LiteralPath (Join-Path $StagingPowerControl 'pawnio') -Recurse -File | ForEach-Object { Get-RelativePath (Join-Path $StagingPowerControl 'pawnio') $_.FullName } | Sort-Object)
if (Compare-Object $pawnioExpected $pawnioActual) { throw 'PawnIO runtime does not exactly match the locked atomic file set' }

$forbidden = @()
foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -Recurse -Force -File) {
  $relative = Get-RelativePath $StagingRoot $file.FullName
  $isFanHostManagedDocument = $relative -match '^PowerControl\\fan-host(?:-v2|-quarantine)?\\.+\.md$'
  $isCustomSteamLibraryManagedDocument = $relative -match '^(?:CustomSteamLibrary|YeManCC\\CustomSteamLibrary)\\(CUSTOM-STEAM-LIBRARY-INTEGRATION-CONTRACT|CUSTOM-STEAM-LIBRARY-UPGRADE-CONTRACT|SEPARATION-TASK-CUSTOM-STEAM-LIBRARY)\.md$'
  $isHcRuntimeDocument = $relative -match '^PowerControl\\handheldcompanion-runtime\\.+\.md$'
  # MAINLINE15 PACKAGE-RULES（2026-10-09）：decky 家目录在拷贝车道已由
  # Test-IsExcludedPowerControlPath 定为"精确白名单"，dist\index.js 是 Decky
  # Loader 的插件入口（必发资产，契约见 tools/decky_sidebar_packaging_selftest.mjs）。
  # 该白名单先于通用 dist 排除，此处终检复用同一函数同步豁免，
  # 避免同一条打包策略在拷贝与终检两处口径打架。
  $isDeckyShippedPayload = $false
  if ($relative -match '^PowerControl\\(decky\\.+)$') {
    $isDeckyShippedPayload = -not (Test-IsExcludedPowerControlPath $Matches[1] $false)
  }
  if (-not $isDeckyShippedPayload -and (
    $relative -match '(^|\\)(\.git|node_modules|build|dist|testrun|outputs|__pycache__|\.workbuddy)(\\|$)' -or
    $relative -match '\.(bak(?:_|$)|obj$|pdb$|ilk$|log$|pid$|hb$|py$|spec$|ts$|vue$|cpp$)' -or
    ($relative -match '\.md$' -and -not $isFanHostManagedDocument -and -not $isCustomSteamLibraryManagedDocument -and -not $isHcRuntimeDocument) -or
    $relative -match '(^|\\)(yeman-settings\.json(?:\.bak)?|startup_trace\.txt|hwinfo-ok|fps-monitor\.(hb|pid|log))$' -or
    $relative -match '^PowerControl\\(TPD|intel|ryzenadj|tools)(\\|$)' -or
    $relative -match '^PowerControl\\Sleep\\(Enable\.txt|Escalation\.txt|sleepguard\.json|target\.txt|睡眠击杀名单\.txt)$'
  )) { $forbidden += $relative }
}
if ($forbidden.Count -gt 0) { throw "Forbidden files entered Release staging: $($forbidden -join ', ')" }

$publishStarted = $false
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $BackupReleaseRoot "PreTask5-$stamp"
try {
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
$releaseItems = @($updateLayoutRoots | ForEach-Object { Join-Path $ReleaseRoot ([string]$_.source) }) + @(
  $LegacyReleaseCustomSteamLibrary,
  $ReleasePackages,
  (Join-Path $ReleaseRoot 'version.json'),
  (Join-Path $ReleaseRoot 'release-manifest.json'),
  (Join-Path $ReleaseRoot 'release-manifest.sha256'),
  (Join-Path $ReleaseRoot 'TESTING.md')
)
foreach ($path in $releaseItems) { Move-ExistingReleaseItem $path $backupRoot }

Move-Item -LiteralPath $StagingYeManCC -Destination $ReleaseYeManCC
Move-Item -LiteralPath $StagingPowerControl -Destination $ReleasePowerControl
if (Test-Path -LiteralPath $StagingRoot) { Remove-Item -LiteralPath $StagingRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $ReleasePackages | Out-Null

# Keep every declared product directory at the ZIP root. The updater and the
# archive validator both consume this same root list, so adding a future root
# no longer requires another hand-written Copy-Item branch here.
foreach ($root in $updateLayoutRoots) {
  $source = Join-Path $ReleaseRoot ([string]$root.source)
  $destination = Join-Path $UpdateRoot ([string]$root.source)
  Copy-DirectoryContents $source $destination
}
$UpdateYeManCC = Join-Path $UpdateRoot 'YeManCC'
if ($isLegacyBootstrap) {
  Remove-Item -LiteralPath (Join-Path $UpdateYeManCC 'update-manifest.json') -Force
}

$updateTopLevel = @(Get-ChildItem -LiteralPath $UpdateRoot -Force | Select-Object -ExpandProperty Name | Sort-Object)
$declaredUpdateRoots = @($updateLayoutRoots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
$missingUpdateRoots = @($requiredUpdateRoots | Where-Object { $_ -notin $updateTopLevel })
$unexpectedUpdateRoots = @($updateTopLevel | Where-Object { $_ -notin $declaredUpdateRoots })
if ($missingUpdateRoots.Count -gt 0 -or $unexpectedUpdateRoots.Count -gt 0) {
  throw "Update ZIP roots do not match update-manifest.json; missing: $($missingUpdateRoots -join ', '); unexpected: $($unexpectedUpdateRoots -join ', '); got: $($updateTopLevel -join ', ')"
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateYeManCC 'YeManCC.exe') -PathType Leaf)) {
  throw 'Update ZIP YeManCC directory is missing YeManCC.exe'
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'PowerControl\pawnio\YeManTdpCtl.exe') -PathType Leaf)) {
  throw 'Update ZIP PowerControl directory is missing PawnIO runtime'
}
$updateHidHideInstaller = Join-Path $UpdateRoot ("PowerControl\redist\" + $installerName)
if (-not (Test-Path -LiteralPath $updateHidHideInstaller -PathType Leaf) -or
    (Get-Sha256 $updateHidHideInstaller) -ne $installerSha256) {
  throw "Update ZIP is missing the locked HidHide installer at PowerControl/redist/$installerName"
}
if ($embedCustomSteamLibrary) {
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'YeManCC\CustomSteamLibrary\package-manifest.json') -PathType Leaf)) {
    throw 'Update ZIP CustomSteamLibrary directory is missing package-manifest.json'
  }
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe') -PathType Leaf)) {
    throw 'Update ZIP CustomSteamLibrary directory is missing CustomSteamLibrary.exe'
  }
}
if (-not $isLegacyBootstrap -and -not (Test-Path -LiteralPath (Join-Path $UpdateYeManCC 'update-manifest.json') -PathType Leaf)) {
  throw 'Update ZIP YeManCC directory is missing update-manifest.json'
}

# 2026-09-23 用户裁定：只产出唯一一个完整包 YeManCC.zip（同时承担更新包
# 与完整快照两个角色），包名沿用公开 v0.0.28 的 YeManCC.zip，但内容为完整包。UpdateRoot 保留为
# "包根 <-> update-manifest roots" 的目录级证明（见上），不参与压缩。
$packagePath = Join-Path $ReleasePackages 'YeManCC.zip'
# 2026-10-07 user ruling: exactly one full payload, retaining the v0.0.28 filename.
if (Test-Path -LiteralPath (Join-Path $ReleasePackages 'YeManCC-Complete.zip')) { throw 'Incorrect complete-package filename survived staging' }
# Complete runnable snapshot artifact: a data-closed copy of the formal Release
# that also carries the virtual-gamepad bundle (InputHost/HIDMaestro, locked
# HidHide installer/CLI and locked HC runtime) inside PowerControl. A clean
# machine can run this snapshot directly, and it is the only package the
# updater consumes.
$completeStaging = Join-Path $PackageBuildRoot 'CompleteRoot'
if (Test-Path -LiteralPath $completeStaging) { Remove-Item -LiteralPath $completeStaging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $completeStaging | Out-Null
Copy-DirectoryContents $ReleaseYeManCC (Join-Path $completeStaging 'YeManCC')
Copy-DirectoryContents $ReleasePowerControl (Join-Path $completeStaging 'PowerControl')
$cliName = 'HidHideCLI.exe'
$cliSha256 = '9DD283FEDFBD301E1A574A3D4B8663F6274CCB5C896F468ED55A6239F9ADE270'
# 批111（2026-09-13 执行单）：HC 资源单一真值重构——共享 6 DLL 唯一真值
# = HC 锁 handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902；
# full 装配一律从 HC 锁拷贝 + hash 对拍，virtual-gamepad 不再携带 HC 副本。
$hcRuntimeSource = Join-Path $sourcePowerControl "handheldcompanion-runtime\$hcRuntimeName"
$hcRuntimeComplete = Join-Path $completeStaging "PowerControl\handheldcompanion-runtime\$hcRuntimeName"
if (-not (Test-Path -LiteralPath $hcRuntimeSource -PathType Container)) { throw "Complete package is missing locked HC runtime $hcRuntimeName" }
# The four size-order targets are retained byte-for-byte. A missing or changed
# authority file aborts export; no volume target may override this rule.
foreach ($name in $hcSlimSizeOrderRetainedHashes.Keys) {
  $path = Join-Path $hcRuntimeSource $name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "HC-SLIM-01 retained authority is missing: $name" }
  if ((Get-Sha256 $path) -ne $hcSlimSizeOrderRetainedHashes[$name]) { throw "HC-SLIM-01 retained authority hash mismatch: $name" }
}
# HC-SLIM-01 R3 §4.1（2026-10-06）：此调用直接从**源** HC 交付树复制（绕过
# Test-IsExcludedPowerControlPath），所以必须显式传入同一条排除集，否则两个
# 松散包装 DLL 会被重新带回完整包。相对路径以 $hcRuntimeSource 为根。
$hcRuntimeLooseWrapperExclusions = @('Resources\xinput1_x64.dll', 'Resources\xinput1_x86.dll')
Copy-DirectoryContents $hcRuntimeSource $hcRuntimeComplete $hcRuntimeLooseWrapperExclusions
# YMCC 自有 5 件（来源 = 构建产物 Build\App\Native\InputHost，不变）。
$ymccHostFiles = @('YeManInputHost.exe', 'YeManInputHost.dll', 'YeManInputHost.deps.json', 'YeManInputHost.runtimeconfig.json', 'HIDMaestro.Core.dll')
# 共享 4 件（来源 = HC 锁，拷后 SHA256 与锁内源逐件对拍；漂移即 throw）。
# 两件内容完全相同的工具 DLL 由 RuntimeSharedDependencies 在生产
# virtual-gamepad 目录缺失时从 HC 锁目录只读校验并加载，避免包内重复。
# 不改 HC runtime 本体、不改 HIDMaestro/驱动；缺文件、路径、身份或 SHA
# 不符均 fail-closed，不能下载/修复/静默回落。
$sharedHostDlls = @(
  'Nefarius.Drivers.HidHide.dll',
  'Nefarius.Vicius.Abstractions.dll',
  'Microsoft.Extensions.DependencyInjection.Abstractions.dll',
  'Microsoft.Extensions.Logging.Abstractions.dll')
# Keep this explicit mapping for the existing AST-extracted production selftest.
# It is checked against the source policy, deploy/config pins and C# resolver.
$sharedRuntimeFallbackHashes = @{
  'Nefarius.Utilities.DeviceManagement.dll' = 'B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C'
  'Newtonsoft.Json.dll' = 'A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B'
}
$sharedRuntimeFallbackDlls = @($sharedRuntimeFallbackHashes.Keys)
$nativeInputHost = Join-Path $BuildNative 'InputHost'
$vgComplete = Join-Path $completeStaging 'PowerControl\feature-assets\virtual-gamepad'
New-Item -ItemType Directory -Force -Path $vgComplete | Out-Null
foreach ($name in $ymccHostFiles) {
  Copy-Item -LiteralPath (Join-Path $nativeInputHost $name) -Destination (Join-Path $vgComplete $name) -Force
}
foreach ($name in $sharedHostDlls) {
  $sharedSrc = Join-Path $hcRuntimeSource $name
  if (-not (Test-Path -LiteralPath $sharedSrc -PathType Leaf)) { throw "HC shared DLL missing from lock: $name" }
  Copy-Item -LiteralPath $sharedSrc -Destination (Join-Path $vgComplete $name) -Force
  if ((Get-Sha256 $sharedSrc) -ne (Get-Sha256 (Join-Path $vgComplete $name))) { throw "HC shared DLL drift: $name" }
}
# If the copied release template carried a legacy duplicate, remove only that
# staging copy. The authoritative HC runtime copy is never touched.
foreach ($name in $sharedRuntimeFallbackDlls) {
  $duplicate = Join-Path $vgComplete $name
  if (Test-Path -LiteralPath $duplicate -PathType Leaf) { [IO.File]::Delete($duplicate) }
}
$redistComplete = Join-Path $completeStaging 'PowerControl\redist'
New-Item -ItemType Directory -Force -Path $redistComplete | Out-Null
foreach ($candidate in @(
  (Join-Path $sourcePowerControl "redist\$installerName"),
  (Join-Path $ReleaseRoot "PowerControl\redist\$installerName"),
  "C:\SOFT\YeMan\PowerControl\redist\$installerName"
) | Select-Object -Unique) {
  if (Test-Path -LiteralPath $candidate -PathType Leaf) { $installerSource = $candidate; break }
}
if (-not $installerSource) { throw "Complete package is missing locked $installerName" }
if ((Get-Sha256 $installerSource) -ne $installerSha256) { throw "Complete package has an unexpected $installerName hash" }
Copy-Item -LiteralPath $installerSource -Destination (Join-Path $redistComplete $installerName) -Force
foreach ($candidate in @(
  (Join-Path $sourcePowerControl "redist\$cliName"),
  (Join-Path $ReleaseRoot "PowerControl\redist\$cliName"),
  'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe',
  "C:\SOFT\YeMan\PowerControl\redist\$cliName"
) | Select-Object -Unique) {
  if (Test-Path -LiteralPath $candidate -PathType Leaf) { $cliSource = $candidate; break }
}
if (-not $cliSource) { throw "Complete package is missing locked $cliName" }
if ((Get-Sha256 $cliSource) -ne $cliSha256) { throw "Complete package has an unexpected $cliName hash" }
Copy-Item -LiteralPath $cliSource -Destination (Join-Path $redistComplete $cliName) -Force
# GP-PREREQ-2: the public first-install executable belongs beside HidHide in redist.
$setupNames = @('HIDMaestroSetup.exe','HIDMaestroSetup.dll','HIDMaestroSetup.deps.json','HIDMaestroSetup.runtimeconfig.json','HIDMaestroSetup.manifest.json')
foreach ($name in $setupNames) {
  $source = Join-Path $sourcePowerControl ("redist\" + $name)
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing first-install asset: $name" }
  Copy-Item -LiteralPath $source -Destination (Join-Path $redistComplete $name) -Force
  if ((Get-Sha256 $source) -ne (Get-Sha256 (Join-Path $redistComplete $name))) { throw "First-install asset drift: $name" }
}
if (Test-Path -LiteralPath (Join-Path $completeStaging 'PowerControl\gamepad-prerequisites')) { throw 'Retired gamepad-prerequisites folder must not ship.' }

# HC-SLIM-01：完整性断言 = YMCC 5 件 + 共享 4 件共 9 件随 virtual-gamepad，
# 两件 fallback 只在 HC 锁目录存在；YeManInputHost.deps.json 声明的 runtime
# 目标文件按实际落点逐一存在（防缺失引用 DLL 致 FileNotFoundException）。
foreach ($name in $sharedRuntimeFallbackDlls) {
  if (Test-Path -LiteralPath (Join-Path $vgComplete $name)) { throw "Unexpected local shared fallback duplicate: $name" }
  $sharedPath = Join-Path $hcRuntimeComplete $name
  if (-not (Test-Path -LiteralPath $sharedPath -PathType Leaf) -or
      (Get-Sha256 $sharedPath) -ne $sharedRuntimeFallbackHashes[$name]) { throw "Pinned shared fallback missing or changed: $name" }
}
foreach ($name in @($ymccHostFiles + $sharedHostDlls)) {
  if (-not (Test-Path -LiteralPath (Join-Path $vgComplete $name) -PathType Leaf)) { throw "Complete package virtual-gamepad bundle is incomplete: $name" }
}
$hostDepsPath = Join-Path $vgComplete 'YeManInputHost.deps.json'
$hostDeps = (Get-Content -LiteralPath $hostDepsPath -Raw -Encoding UTF8) | ConvertFrom-Json
$declaredRuntime = @()
foreach ($target in @($hostDeps.targets.PSObject.Properties)) {
  foreach ($pkg in @($target.Value.PSObject.Properties)) {
    if ($null -ne $pkg.Value.runtime) { $declaredRuntime += @($pkg.Value.runtime.PSObject.Properties.Name) }
  }
}
$declaredRuntime = @($declaredRuntime | Sort-Object -Unique)
foreach ($runtimeFile in $declaredRuntime) {
  $runtimePath = if ($sharedRuntimeFallbackDlls -contains $runtimeFile) {
    Join-Path $hcRuntimeComplete $runtimeFile
  } else {
    Join-Path $vgComplete $runtimeFile
  }
  if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
    throw "Complete package YeManInputHost.deps.json runtime file is missing at its approved location: $runtimeFile ($runtimePath)"
  }
  if ($sharedRuntimeFallbackDlls -contains $runtimeFile -and
      (Get-Sha256 $runtimePath) -ne $sharedRuntimeFallbackHashes[$runtimeFile]) {
    throw "Complete package shared fallback hash mismatch: $runtimeFile"
  }
}
if (-not (Test-Path -LiteralPath (Join-Path $hcRuntimeComplete 'HandheldCompanion.runtime.json') -PathType Leaf)) { throw 'Complete package is missing locked HC runtime manifest' }
Assert-HcSlimStagedPolicy (Join-Path $completeStaging 'PowerControl')
Compress-Archive -Path (Join-Path $completeStaging '*') -DestinationPath $packagePath -CompressionLevel Optimal -Force
Write-Output "Complete package:    $packagePath"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipArchive = [IO.Compression.ZipFile]::OpenRead($packagePath)
try {
  # Final archive-level HC-SLIM-01 gate: staging checks are not enough if a
  # future archive assembly path changes. The two fallback DLLs must be absent
  # from virtual-gamepad, while the four size-order authority files must exist
  # at the locked HC runtime path with their retained hashes.
  foreach ($name in $sharedRuntimeFallbackDlls) {
    $forbidden = "PowerControl/feature-assets/virtual-gamepad/$name"
    $hits = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $forbidden })
    if ($hits.Count -ne 0) { throw "HC-SLIM-01 archive contains forbidden local duplicate: $forbidden" }
    $authority = "PowerControl/handheldcompanion-runtime/$hcRuntimeName/$name"
    $authorityHits = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $authority })
    if ($authorityHits.Count -ne 1 -or (Get-ZipEntrySha256 $authorityHits[0]) -ne $sharedRuntimeFallbackHashes[$name]) { throw "HC-SLIM-01 archive shared authority mismatch: $authority" }
  }
  foreach ($name in $hcSlimSizeOrderRetainedHashes.Keys) {
    $authority = "PowerControl/handheldcompanion-runtime/$hcRuntimeName/$name"
    $hits = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $authority })
    if ($hits.Count -ne 1 -or (Get-ZipEntrySha256 $hits[0]) -ne $hcSlimSizeOrderRetainedHashes[$name]) { throw "HC-SLIM-01 archive retained authority mismatch: $authority" }
  }
  foreach ($name in @('HIDMaestro.Core.dll')) {
    $retainedPath = "PowerControl/feature-assets/virtual-gamepad/$name"
    $hits = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $retainedPath })
    if ($hits.Count -ne 1 -or (Get-ZipEntrySha256 $hits[0]) -ne $hcSlimHidMaestroSha256) {
      throw "HC-SLIM-01 archive HIDMaestro retained identity mismatch: $retainedPath"
    }
  }
  $zipRoots = @($zipArchive.Entries | ForEach-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    if ($normalized) { ($normalized -split '/')[0] }
  } | Sort-Object -Unique)
  $missingZipRoots = @($requiredUpdateRoots | Where-Object { $_ -notin $zipRoots })
  $unexpectedZipRoots = @($zipRoots | Where-Object { $_ -notin $declaredUpdateRoots })
  if ($missingZipRoots.Count -gt 0 -or $unexpectedZipRoots.Count -gt 0) {
    throw "YeManCC.zip roots do not match update-manifest.json; missing: $($missingZipRoots -join ', '); unexpected: $($unexpectedZipRoots -join ', '); got: $($zipRoots -join ', ')"
  }
  $layoutManifestEntries = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq 'YeManCC/update-manifest.json' })
  if ($layoutManifestEntries.Count -ne 1) {
    throw 'YeManCC.zip must contain exactly one YeManCC/update-manifest.json'
  }
  $fanHostEntries = @($zipArchive.Entries | Where-Object {
    $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/fan-host-v2(?:/|$)'
  })
  if ($includeFanHostInFullTestPackage) {
    if ($fanHostEntries.Count -eq 0) { throw 'Full release package must contain PowerControl/fan-host-v2' }
  } elseif ($fanHostEntries.Count -gt 0) {
    throw "YeManCC.zip must not contain PowerControl/fan-host-v2 entries: $($fanHostEntries.FullName -join ', ')"
  }
  $legacyFanHostEntries = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -match '^PowerControl/fan-host(?:/|$)' })
  if ($legacyFanHostEntries.Count) { throw 'Legacy fan-host must not ship: public 0.0.28 would reject schema-2 payload' }
  $fanHostQuarantineEntries = @($zipArchive.Entries | Where-Object {
    $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/fan-host-quarantine(?:/|$)'
  })
  if ($fanHostQuarantineEntries.Count -gt 0) {
    throw "YeManCC.zip must not contain PowerControl/fan-host-quarantine entries: $($fanHostQuarantineEntries.FullName -join ', ')"
  }
  $gyroVirtualAssetEntries = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/feature-assets/(?:virtual-gamepad|gyro-motion)(?:/|$)' })
  if ($gyroVirtualAssetEntries.Count -eq 0) { throw 'YeManCC.zip generator must export PowerControl/feature-assets/virtual-gamepad and gyro-motion' }
  $inputHostEntries = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -match '^YeManCC/InputHost(?:/|$)' })
  if ($inputHostEntries.Count -gt 0) { throw "YeManCC.zip must not contain YeManCC/InputHost entries: $($inputHostEntries.FullName -join ', ')" }
  # 2026-09-23：唯一包 = 完整包，驱动类资产（HidHide 安装器/CLI、HIDMaestro、
  # HC 运行时）按设计随包分发。断言反转为"位置白名单"：只允许出现在
  # PowerControl/redist 的两个锁定名、feature-assets/virtual-gamepad 与
  # handheldcompanion-runtime 三处；出现在其他位置（尤其 YeManCC 根下）即 throw。
  $driverAssetEntries = @($zipArchive.Entries | Where-Object {
    $_.FullName.Replace('\', '/').TrimStart('/') -match '(?i)(^|/)(HidHide(?:_1\.5\.230_x64)?(?:CLI|Client|Watchdog)?\.exe|HIDMaestro\.Core\.dll)$'
  })
  foreach ($entry in $driverAssetEntries) {
    $rel = $entry.FullName.Replace('\', '/').TrimStart('/')
    $allowed = ($rel -in @('PowerControl/redist/HidHide_1.5.230_x64.exe', 'PowerControl/redist/HidHideCLI.exe')) -or
      ($rel -match '^PowerControl/feature-assets/virtual-gamepad/') -or
      ($rel -match '^PowerControl/handheldcompanion-runtime/')
    if (-not $allowed) { throw "Complete package carries a driver asset outside the allowed roots: $rel" }
  }
  foreach ($lockedAsset in @('PowerControl/redist/HidHide_1.5.230_x64.exe', 'PowerControl/redist/HidHideCLI.exe')) {
    $hit = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq $lockedAsset })
    if ($hit.Count -ne 1) { throw "Complete package is missing a locked driver asset: $lockedAsset" }
    $expectedAssetSha = if ($lockedAsset -like '*HidHideCLI*') { $cliSha256 } else { $installerSha256 }
    if ((Get-ZipEntrySha256 $hit[0]) -ne $expectedAssetSha) {
      throw "Complete package has an unexpected hash for: $lockedAsset"
    }
  }
  $flatEntries = @($zipArchive.Entries | Where-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    $normalized -match '^(YeManCC\.exe|YeMan-Support\.html|assets/|CustomSteamLibrary\.exe|SteamArtworkLab\.exe|workspace-ui/)'
  })
  if ($flatEntries.Count -gt 0) {
    throw "YeManCC.zip contains flattened YeManCC entries: $($flatEntries.FullName -join ', ')"
  }
  if ($embedCustomSteamLibrary) {
    $zipChildRoot = 'YeManCC/CustomSteamLibrary'
    $zipChildManifest = @($zipArchive.Entries | Where-Object {
      $_.FullName.Replace('\', '/').TrimStart('/') -eq "$zipChildRoot/package-manifest.json"
    })
    if ($zipChildManifest.Count -ne 1) {
      throw 'YeManCC.zip must contain exactly one nested YeManCC/CustomSteamLibrary/package-manifest.json'
    }
    $zipChildReader = New-Object IO.StreamReader($zipChildManifest[0].Open())
    try { $zipChild = $zipChildReader.ReadToEnd() | ConvertFrom-Json }
    finally { $zipChildReader.Dispose() }
    if ([string]$zipChild.packageVersion -ne $version) {
      throw "Nested CustomSteamLibrary version does not follow mainline: child=$($zipChild.packageVersion), main=$version"
    }
    foreach ($record in @($zipChild.fileIndex)) {
      $relative = Normalize-PackageRelativePath ([string]$record.path).Replace('\', '/')
      $entryPath = "$zipChildRoot/$($relative.Replace('\', '/'))"
      $entry = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq $entryPath })
      if ($entry.Count -ne 1) { throw "YeManCC.zip is missing nested CustomSteamLibrary file: $relative" }
      if ([int64]$entry[0].Length -ne [int64]$record.bytes) { throw "Nested CustomSteamLibrary byte mismatch: $relative" }
      if ((Get-ZipEntrySha256 $entry[0]) -ne ([string]$record.sha256).ToUpperInvariant()) {
        throw "Nested CustomSteamLibrary SHA-256 mismatch: $relative"
      }
    }
    $flatChildEntries = @($zipArchive.Entries | Where-Object {
      $_.FullName.Replace('\', '/').TrimStart('/') -match '^CustomSteamLibrary(?:/|$)'
    })
    if ($flatChildEntries.Count -gt 0) {
      throw 'YeManCC.zip must not contain a legacy top-level CustomSteamLibrary directory'
    }
  }
} finally {
  $zipArchive.Dispose()
}
$packageHash = Get-Sha256 $packagePath
  $fanHostTestingLine1 = if ($includeFanHostInFullTestPackage) { 'This full release includes PowerControl/fan-host-v2; the legacy fan-host lane is preserved.' } else { 'PowerControl/fan-host is excluded from this release; an existing installed' }
  $fanHostTestingLine2 = if ($includeFanHostInFullTestPackage) { 'Use the included Fan Host payload together with this package.' } else { 'Fan Host is preserved and is not overwritten by the updater.' }

$releaseVersion = [ordered]@{
  version = $version
  notes = [string]$versionInfo.notes
  sha256 = $packageHash
  publishedAt = (Get-Date -Format 'yyyy-MM-dd')
  package = 'Packages/YeManCC.zip'
}
$releaseVersion | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ReleaseRoot 'version.json') -Encoding UTF8

$testingLines = @(
  '# YeManCC Release Testing', '',
  "Version: $version", '',
  'Run the new executable explicitly:',
  (Join-Path $ReleaseYeManCC 'YeManCC.exe'), '',
  'The installed shortcut and scheduled task still run:',
  'C:\SOFT\YeMan\YeManCC\YeManCC.exe', '',
  $fanHostTestingLine1,
  $fanHostTestingLine2, '',
  'Mainline behavior includes updater shielding and sidebar feature hiding.',
  'The complete HidHide/HidHideCLI/HIDMaestro/HC-runtime bundle ships inside the',
  'single complete package (YeManCC.zip); that package is also the',
  'updater input, so an update installs the same complete product a clean',
  'machine runs.', '',
  "Complete package SHA-256: $packageHash"
)
$testingLines | Set-Content -LiteralPath (Join-Path $ReleaseRoot 'TESTING.md') -Encoding UTF8

# 2026-09-23：原先在此处的"完整包"装配块已前移（见上方 $packagePath 段），
# 因为唯一产物就是完整包本身。此处不再重复装配与压缩。


# ================================================================
# Adopted-optimization inheritance gate - post-package / pre-publish
# (release ruling, 2026-09-20). The staging package tree and the materialized
# Release tree must each reproduce the per-build artifact hashes and carry every
# adopted optimization; the two packaging-generated files (version.json,
# YeMan-Support.html) must be byte-identical to their source-root originals and
# their hashes are recorded in the gate report. Any mismatch (missing capture,
# missing optimization, mixed old native/Host/SDK, tampered generated file)
# aborts before PACKAGE_OK and before the transactional publish is committed.
# ================================================================
& $OptGateScript -Mode Gate -Stage package -BuildManifest $OptBuildManifest -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED (package stage, exit=$LASTEXITCODE): refusing to publish."
}
& $OptGateScript -Mode Gate -Stage release -BuildManifest $OptBuildManifest -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED (release stage, exit=$LASTEXITCODE): refusing to publish."
}

# ================================================================
# Final archive verification (release ruling T2, 2026-09-20). The directory gates
# above check trees; these calls check the ACTUAL archives produced by THIS run:
# every entry must be byte-identical to the staging directory it was built from,
# and the archive's native exe must equal this run's build-capture hash (a
# previously generated identity is never accepted as a new package's proof).
# The identity file is keyed by the package hash, so it can never be reused for a
# different package. Any failure throws before PACKAGE_OK and is rolled back by
# the existing transactional catch below. Archive set follows the envelope:
# legacy-bootstrap drops update-manifest.json from the update archive (the
# staging directory already reflects that, so the cross-check adapts for free).
# ================================================================
& $OptGateScript -Mode ReleasePackage -Zip $packagePath -ZipRoot $completeStaging `
  -IncludeRoots @('YeManCC','PowerControl') -BuildManifest $OptBuildManifest -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
  throw "Release package cross-check FAILED for the complete archive (exit=$LASTEXITCODE): refusing to publish."
}

# Archive lane/identity gate: no legacy path can receive PACKAGE_OK.
& (Join-Path $PSScriptRoot 'fan_host_path_gate.ps1') -ProjectRoot $ProjectRoot -ReleaseZip $packagePath | Out-Null
Write-Output 'PACKAGE_OK'
Write-Output "Release YeManCC:      $ReleaseYeManCC"
Write-Output "Release PowerControl: $ReleasePowerControl"
Write-Output "Release CustomSteamLibrary: $ReleaseCustomSteamLibrary"
Write-Output "Complete package:     $packagePath"
Write-Output "Package SHA256:       $packageHash"
Write-Output "Release envelope:     $releaseEnvelope"
Write-Output "Assets:               $($assetSources.Values -join ', ')"
$publishStarted = $true
} catch {
  # Release publication is transactional. If a move/copy/validation fails
  # after old output was backed up, restore every published item so the next
  # run never starts from a half-built Release directory.
  if (-not $publishStarted) {
    foreach ($path in $releaseItems) {
      if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
      }
      $backupPath = Join-Path $backupRoot ([IO.Path]::GetFileName($path))
      if (Test-Path -LiteralPath $backupPath) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Move-Item -LiteralPath $backupPath -Destination $path -Force -ErrorAction SilentlyContinue
      }
    }
  }
  throw
}
