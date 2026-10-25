<#
.SYNOPSIS
  Deploy one matching YeManCC native/WebView2 runtime to the installed folder.

.DESCRIPTION
  The native host and WebView resources form one protocol version.  This script
  backs up replaced resources, removes stale hashed web assets, copies the
  complete current build, then verifies the index entry point and executable.
  The installed PowerControl tree is closed as a full mainline product export:
  the complete ordinary PowerControl main assets (formal release template
  policy) and the hash-locked third-party assets are deployed first, then the
  virtual-gamepad bundle (feature-assets + InputHost + HIDMaestro + locked HC
  runtime + HidHide installer/CLI) is merged in.  This makes a clean, empty
  C:\SOFT\YeMan export fully runnable instead of a partial overlay.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [string]$InstallRoot = 'C:\SOFT\YeMan\YeManCC',
  # 203 (2026-09-22 Gate C decision): verify the deployment on an explicit ISOLATED copy.
  [switch]$IsolatedRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ymcc-shared-runtime\shared-runtime.ps1')
. (Join-Path $PSScriptRoot 'hc-device-runtime\verify-runtime.ps1')
. (Join-Path $PSScriptRoot 'lighting-build-contract.ps1')
. (Join-Path $PSScriptRoot 'settings-baseline-contract.ps1')

# Called only after the existing complete Assert-ExportBuildIdentity gate.
# Native is deployed later in this script: validate its planned source here.
function Copy-InstalledLightingPayload {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
    [Parameter(Mandatory=$true)][string]$SourcePowerControl,
    [Parameter(Mandatory=$true)][string]$InstalledPowerControl,
    [Parameter(Mandatory=$true)][string]$BackupRoot,
    [Parameter(Mandatory=$true)][string]$NativeExe,
    [Parameter(Mandatory=$true)]$ExportBuildIdentity
  )
  $ErrorActionPreference = 'Stop'
  $source = Resolve-YmccLightingChild $SourcePowerControl 'light-setter'
  function Assert-LightingTuple([string]$PayloadRoot) {
    if ($ExportBuildIdentity.schema -ne 1 -or $ExportBuildIdentity.buildId -notmatch '^[a-f0-9]{64}$' -or -not $ExportBuildIdentity.payload) { throw 'LIGHTING_INSTALLED_BUILD_IDENTITY_REQUIRED' }
    foreach ($relative in @('YeManCC/YeManCC.exe')+@(Get-YmccLightingRuntimeNames | ForEach-Object {'PowerControl/light-setter/'+$_})) {
      $rows = @($ExportBuildIdentity.payload | Where-Object path -CEQ $relative)
      if ($rows.Count -ne 1 -or $rows[0].sha256 -cnotmatch '^[a-f0-9]{64}$' -or $rows[0].bytes -le 0) { throw ('LIGHTING_INSTALLED_BUILD_TUPLE_MISSING: '+$relative) }
      $file = if ($relative -ceq 'YeManCC/YeManCC.exe') { Resolve-YmccLightingChild (Split-Path -Parent $NativeExe) (Split-Path -Leaf $NativeExe) } else { Resolve-YmccLightingChild $PayloadRoot ($relative.Substring('PowerControl/light-setter/'.Length)) }
      if ((Get-Item -LiteralPath $file).Length -ne $rows[0].bytes -or (Get-YmccLightingFileHash $file) -cne $rows[0].sha256) { throw ('LIGHTING_INSTALLED_BUILD_TUPLE_CHANGED: '+$relative) }
    }
    Assert-YmccLightingMainlinePayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -PayloadRoot $PayloadRoot
  }
  $admitted = Assert-LightingTuple $source
  $target = Resolve-YmccLightingChild $InstalledPowerControl 'light-setter'
  $backup = Resolve-YmccLightingChild $BackupRoot 'PowerControl/light-setter'
  $existingAcl = [Collections.Generic.List[object]]::new()
  if (Test-Path -LiteralPath $target -PathType Container) {
    foreach ($name in @('')+@(Get-YmccLightingRuntimeNames)) {
      $path = if ($name) { Join-Path $target $name } else { $target }
      if (Test-Path -LiteralPath $path) {
        $acl = Get-Acl -LiteralPath $path
        $existingAcl.Add(@{name=$name;acl=$acl;sddl=$acl.Sddl})
      }
    }
  }
  function Assert-LightingAcl([string]$PayloadRoot) {
    foreach ($entry in $existingAcl) {
      $path = if ($entry.name) { Join-Path $PayloadRoot $entry.name } else { $PayloadRoot }
      if ((Get-Acl -LiteralPath $path).Sddl -cne $entry.sddl) { throw ('LIGHTING_INSTALLED_ACL_CHANGED: '+$entry.name) }
    }
  }
  $paths = @($source,$target,$backup)
  for ($i=0; $i -lt $paths.Count; $i++) {
    for ($j=$i+1; $j -lt $paths.Count; $j++) {
      if ($paths[$i] -ieq $paths[$j] -or $paths[$i].StartsWith($paths[$j]+'\',[StringComparison]::OrdinalIgnoreCase) -or $paths[$j].StartsWith($paths[$i]+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'LIGHTING_INSTALLED_OVERLAPPING_PATHS' }
    }
  }
  if ([IO.Path]::GetPathRoot($target) -ine [IO.Path]::GetPathRoot($backup)) { throw 'LIGHTING_INSTALLED_STAGE_VOLUME_MISMATCH' }
  if (Test-Path -LiteralPath $backup) { throw 'LIGHTING_INSTALLED_BACKUP_ALREADY_EXISTS' }
  $stage = Resolve-YmccLightingChild $BackupRoot ('lighting-stage-'+[Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  foreach ($name in Get-YmccLightingRuntimeNames) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $stage $name) }
  foreach ($entry in $existingAcl) {
    $path = if ($entry.name) { Join-Path $stage $entry.name } else { $stage }
    Set-Acl -LiteralPath $path -AclObject $entry.acl
  }
  Assert-LightingAcl $stage
  $staged = Assert-LightingTuple $stage
  if ($staged.receiptSha256 -cne $admitted.receiptSha256) { throw 'LIGHTING_INSTALLED_CANDIDATE_CHANGED' }
  $oldMoved = $false
  $newMoved = $false
  try {
    if (Test-Path -LiteralPath $target) {
      New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
      [IO.Directory]::Move($target,$backup)
      $oldMoved = $true
    }
    [IO.Directory]::Move($stage,$target)
    $newMoved = $true
    $installed = Assert-LightingTuple $target
    Assert-LightingAcl $target
    if ($installed.receiptSha256 -cne $admitted.receiptSha256) { throw 'LIGHTING_INSTALLED_CANDIDATE_CHANGED' }
  } catch {
    $failure = $_
    if ($newMoved) { [IO.Directory]::Move($target,$stage) }
    if ($oldMoved) { [IO.Directory]::Move($backup,$target) }
    throw $failure
  }
  return $installed
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

function Add-IsolatedNote([string]$Text) {
  if (-not $script:IsolatedNotesPath) { return }
  Add-Content -LiteralPath $script:IsolatedNotesPath -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $Text) -Encoding UTF8
  Write-Output ('ISOLATED-ROOT skip: ' + $Text)
}

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

# Formal-install PowerControl main-asset policy. It mirrors the formal release
# template policy (package-release.ps1 Test-IsExcludedPowerControlPath) and
# additionally reserves the four private roots that are deployed by dedicated
# lanes below: fan-host (separate authenticated payload), feature-assets
# (virtual-gamepad bundle), handheldcompanion-runtime (locked HC manifest),
# and redist (HidHide installer/CLI verified by hash at deploy time).
function Test-InstalledPowerControlExcluded([string]$Relative, [bool]$IsDirectory) {
  $r = $Relative.Replace('/', '\')
  # Battery settings/journals are per-user state; ship only the built immutable helper payload.
  if ($r -match '^battery-host(?:\\|$)') {
    if ($IsDirectory) { return $r -ne 'battery-host' }
    $batteryFiles=(Get-Content -LiteralPath (Join-Path $ProjectRoot 'native\BatteryHost\payload-files.json') -Raw | ConvertFrom-Json)
    return $r.Substring('battery-host\'.Length) -notin $batteryFiles
  }
  # CE-only GameValueScan runtime: bind the adapter, Lua bridge and manifest.
  # Runtime state, unknown JSON and other added payloads never enter this lane.
  if ($r -match '^MemoryScan(?:\\|$)') {
    if ($IsDirectory) { return $r -ine 'MemoryScan' }
    if ($r -notin @('MemoryScan\MemoryScanHost.cs', 'MemoryScan\MemoryScanHost.exe', 'MemoryScan\MemoryScanHost.payload.json', 'MemoryScan\CeScanEngine.lua')) { return $true }
    $memoryScanRoot = Join-Path $ProjectRoot 'PowerControl\MemoryScan'
    $memoryScanManifest = Get-Content -LiteralPath (Join-Path $memoryScanRoot 'MemoryScanHost.payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($memoryScanManifest.schema -ne 2 -or $memoryScanManifest.engine -cne 'cheat-engine' -or $memoryScanManifest.source -cne 'MemoryScanHost.cs' -or
        $memoryScanManifest.binary -cne 'MemoryScanHost.exe' -or $memoryScanManifest.runtime -cne 'Windows .NET Framework 4.8 x64') {
      throw 'Invalid immutable MemoryScan payload manifest'
    }
    if ((Get-Sha256 (Join-Path $memoryScanRoot 'MemoryScanHost.cs')) -ine $memoryScanManifest.sourceSha256 -or
        (Get-Sha256 (Join-Path $memoryScanRoot 'MemoryScanHost.exe')) -ine $memoryScanManifest.binarySha256 -or
        (Get-Sha256 (Join-Path $memoryScanRoot 'CeScanEngine.lua')) -ine $memoryScanManifest.luaSha256) {
      throw 'Unbound immutable MemoryScan payload source or binary'
    }
    return $false
  }
  # Decky is an exact immutable allowlist, before generic dist/JSON exclusions.
  # Runtime settings, foreign plugins and backend files are never ordinary assets.
  if ($r -match '^decky(?:\\|$)') {
    if ($IsDirectory) {
      return $r -notin @('decky','decky\plugins','decky\plugins\ymcc-sidebar','decky\plugins\ymcc-sidebar\dist')
    }
    return $r -notin @(
      'decky\PluginLoader_noconsole.exe','decky\LICENSE.decky-loader',
      'decky\plugins\ymcc-sidebar\dist\index.js',
      'decky\plugins\ymcc-sidebar\package.json','decky\plugins\ymcc-sidebar\plugin.json',
      'decky\plugins\ymcc-sidebar\LICENSE.decky-api','decky\plugins\ymcc-sidebar\LICENSE.vue'
    )
  }
  if ($r -match '^(fan-host(?:-v2)?|light-setter|feature-assets|handheldcompanion-runtime|ymcc-shared-runtime|redist)(?:-quarantine|\\|$)') { return $true }
  if ($r -match '(^|\\)(\.git|build|dist|__pycache__|KX\.bak_removed|product-old-files-[^\\]+)(\\|$)') { return $true }
  if ($r -match '^(TPD|intel|ryzenadj|tools|pawnio|OpenSpeedy|RTSS-Overlays)(\\|$)') { return $true }
  if ($IsDirectory) { return $false }
  if ($r -match '\.bak(?:_|$)' -or $r -match '\.(obj|pdb|ilk|log|pid|hb|tmp)$') { return $true }
  if ($r -match '\.(md|py|spec)$') { return $true }
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
  # Immutable resources from the current Steam appearance pack. Never include
  # a user cache/state file merely because it has the same directory prefix.
  if ($r -match '^steam-appearance\\' -and $r -match '\.json$') {
    $pack = Join-Path $ProjectRoot 'PowerControl\steam-appearance'
    $manifest = Get-Content -LiteralPath (Join-Path $pack 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.id -ne 'ymcc-steam-appearance' -or $manifest.version -ne 1) { throw 'Invalid bundled Steam appearance manifest' }
    if ($r -ieq 'steam-appearance\manifest.json') { return $false }
    $prefix = 'steam-appearance\themes\'
    if (-not $r.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Unclassified Steam appearance JSON: $r" }
    $relative = $r.Substring($prefix.Length).Replace('\', '/')
    $matches = @($manifest.files | Where-Object { $_.path -ieq $relative })
    $source = Join-Path (Join-Path $pack 'themes') $relative
    if ($matches.Count -ne 1 -or (Get-Sha256 $source) -ine $matches[0].sha256) { throw "Unbound bundled Steam appearance resource: $r" }
    return $false
  }
  # Keep the immutable shared title policy aligned with package-release.ps1.
  if (-not $script:installedSettingsBaseline) { $script:installedSettingsBaseline = Read-YmccSettingsBaselineContract $ProjectRoot }
  if (Test-YmccMutableSettingsPath $r $script:installedSettingsBaseline) { return $true }
  if ($r -ieq 'settings-baseline.json' -or $r -ieq $script:installedSettingsBaseline.defaultsFile) { return $false }
  if ($r -ieq 'GameTitlePolicy.json') { return $false }
  if ($r -match '\.json$') {
    throw "Unclassified PowerControl JSON must be added to the install policy: $r"
  }
  return $false
}

function Copy-InstalledPowerControlMainAssets([string]$SourceRoot, [string]$DestinationRoot) {
  New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $SourceRoot -Recurse -Force) {
    $relative = Get-RelativePath $SourceRoot $item.FullName
    if (Test-InstalledPowerControlExcluded $relative $item.PSIsContainer) { continue }
    $target = Join-Path $DestinationRoot $relative
    if ($item.PSIsContainer) {
      New-Item -ItemType Directory -Force -Path $target | Out-Null
    } else {
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
      Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
  }
}

# Copies the hash-locked third-party assets (MG-AUTO, OpenSpeedy, pawnio,
# RTSS-Overlays, physpanel) with SHA-256 verification. Source is the workspace
# Assets overlay when complete, otherwise the verified source fallback, exactly
# as the formal packager resolves them.
function Copy-InstalledLockedPowerControlAssets([string]$ProjectRoot, [string]$WorkspaceRoot, [string]$DestinationPowerControl, [string]$BackupRoot) {
  $lockPath = Join-Path $ProjectRoot 'tools\release-assets.lock.json'
  $assetLock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $assetsRoot = Join-Path $WorkspaceRoot 'Assets'
  $workspaceAssetsComplete = (Test-Path -LiteralPath $assetsRoot -PathType Container) -and
    (@($assetLock.files | Where-Object {
      -not (Test-Path -LiteralPath (Join-Path $assetsRoot ([string]$_.assetPath).Replace('/', '\')) -PathType Leaf)
    }).Count -eq 0)
  foreach ($entry in $assetLock.files) {
    $source = if ($workspaceAssetsComplete) {
      Join-Path $assetsRoot ([string]$entry.assetPath).Replace('/', '\')
    } else {
      Join-Path $ProjectRoot ([string]$entry.fallbackPath).Replace('/', '\')
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Locked PowerControl asset is missing: $source" }
    if ((Get-Sha256 $source) -ne [string]$entry.sha256) { throw "Locked PowerControl asset hash mismatch: $source" }
    $destination = Join-Path $DestinationPowerControl ([string]$entry.releasePath).Replace('/', '\')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    # 203 (2026-09-22, W3-17 finding F2): a pre-existing file here was overwritten WITHOUT a backup, so a
    # rollback could not restore it. Measured case: PowerControl\MG-AUTO\memreduct.ini (user settings for
    # Memory Reduct) was replaced by the locked asset and the backup held no previous copy.
    # Route through Copy-WithBackup: it mirrors the destination's path under the backup root and then copies
    # the verified locked source over the destination.
    Copy-WithBackup $source $destination $BackupRoot
  }
  return $workspaceAssetsComplete
}

function Copy-WithBackup([string]$Source, [string]$Destination, [string]$BackupRoot) {
  if (Test-Path -LiteralPath $Destination -PathType Leaf) {
    # 203/Fan: keep the path relative to the install parent so same-named files from different
    # directories cannot collide in the backup (leaf-only naming made per-file restore ambiguous).
    $base = Split-Path -Parent (Split-Path -Parent $BackupRoot)
    $destFull = Get-FullPath $Destination
    $rel = $null
    if ($base) {
      $baseFull = Get-FullPath $base
      if ($destFull.StartsWith($baseFull + '\')) { $rel = $destFull.Substring($baseFull.Length + 1) }
    }
    if (-not $rel) { $rel = Split-Path -Leaf $Destination }   # out-of-base fallback: previous behaviour
    $backupPath = Join-Path $BackupRoot $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
    Copy-Item -LiteralPath $Destination -Destination $backupPath -Force
  }
  Copy-Item -LiteralPath $Source -Destination $Destination -Force
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

# Export freshness guard.  A deployed/exported artifact must never be older than
# the source it claims to represent.  Regression history: the QPC phase-stepping
# change lived in native/main.cpp but a 17:00 build (pre-change) was deployed at
# 17:0x, so runtime logs kept the old 60 Hz/16.7 ms behavior while the source
# already contained the fix.  This probe compares the newest source timestamp
# (native, InputHost, web/src, version/config) against the matching build
# artifact timestamp and aborts the export (fail-closed) when the artifact is
# older, so an un-rebuilt source can never be pushed again.
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
    throw 'Export freshness guard: Build artifacts are missing; run build-workspace.ps1 before exporting.'
  }
  $nativeProduct = (Get-Item -LiteralPath $nativeExePath -Force).LastWriteTimeUtc
  $webProduct = (Get-Item -LiteralPath $webProductPath -Force).LastWriteTimeUtc

  $stale = @()
  if ($nativeLatest -gt $nativeProduct.Add($tolerance)) {
    $stale += "native 源码 ($($nativeLatest.ToString('o')) UTC) 晚于 YeManCC.exe 产物 ($($nativeProduct.ToString('o')) UTC) —— 产物未包含最新源码"
  }
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
    throw "Export freshness guard REJECTED (禁止导出旧版构建):`n  " + ($stale -join "`n  ") + "`n  请先运行 build-workspace.ps1 重建后再导出。"
  }
  return $true
}

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'monitor-hud-export.ps1')
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$WebBuild = Join-Path $WorkspaceRoot 'Build\App\Web'
$DeckyBuild = Join-Path $WorkspaceRoot 'Build\App\Decky\plugins\ymcc-sidebar'
$NativeExe = Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe'
$RecoveryService = Join-Path $WorkspaceRoot 'Build\App\Native\YeManRecoveryService.exe'
$ControllerVisibilityProbe = Join-Path $WorkspaceRoot 'Build\App\Native\YeManControllerVisibilityProbe.exe'
$RtssProfileHelper = Join-Path $WorkspaceRoot 'Build\App\Native\YMCCRtssProfileHelper.exe'
$RtssOverlayBridge = Join-Path $WorkspaceRoot 'Build\App\Native\YMCCOverlayBridge.dll'
$NativeInputHost = Join-Path $WorkspaceRoot 'Build\App\Native\InputHost'
$sourcePowerControl = Join-Path $ProjectRoot 'PowerControl'
# Validate the complete appearance payload before any installed files are moved or copied.
. (Join-Path $PSScriptRoot 'steam-appearance-export.ps1')
Get-SteamAppearanceExportFiles $sourcePowerControl | Out-Null
$FeatureAssetsSource = Join-Path $ProjectRoot 'PowerControl\feature-assets'
$HcRuntimeSource = Join-Path $ProjectRoot 'PowerControl\handheldcompanion-runtime'
$HcRuntimeFolderName = 'YMCC-DEVICECORE-20261018-v1'
$CustomSteamLibrarySource = Join-Path $ProjectRoot 'CustomSteamLibrary'
$hidHideInstallerName = 'HidHide_1.5.230_x64.exe'
$hidHideInstallerExpectedSha256 = 'F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6'
$hidHideCliName = 'HidHideCLI.exe'
$hidHideCliExpectedSha256 = '9DD283FEDFBD301E1A574A3D4B8663F6274CCB5C896F468ED55A6239F9ADE270'
$CustomSteamLibraryFiles = @(
  'package-manifest.json',
  'CustomSteamLibrary.exe',
  'SteamArtworkLab.exe',
  'run-workspace.bat',
  'assets\custom-steam-library.ico',
  'workspace-ui\index.html',
  'workspace-ui\app.js',
  'workspace-ui\styles.css',
  'CUSTOM-STEAM-LIBRARY-INTEGRATION-CONTRACT.md',
  'SEPARATION-TASK-CUSTOM-STEAM-LIBRARY.md',
  'CUSTOM-STEAM-LIBRARY-UPGRADE-CONTRACT.md'
)
$InstallRoot = Get-FullPath $InstallRoot
$ExpectedInstall = 'C:\SOFT\YeMan\YeManCC'
if ($IsolatedRoot) {
  # Refuse anything that could reach the real product area, and refuse aliases.
  if ($InstallRoot -eq $ExpectedInstall) { throw "Isolated-root mode refuses the real install path: $InstallRoot" }
  if ($InstallRoot -like 'C:\SOFT\YeMan*') { throw "Isolated-root mode refuses the C:\SOFT\YeMan area: $InstallRoot" }
  $probe = $InstallRoot
  while ($probe -and $probe.Length -gt 3) {
    if (Test-Path -LiteralPath $probe) {
      $item = Get-Item -LiteralPath $probe -Force
      if ($item.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw "Isolated-root mode refuses a reparse-point target: $probe" }
    }
    $probe = Split-Path -Parent $probe
  }
  Write-Output "ISOLATED-ROOT MODE: target $InstallRoot (real install untouched; host side effects skipped and recorded)"
} elseif ($InstallRoot -ne $ExpectedInstall) {
  throw "Unexpected install target: $InstallRoot. Pass -IsolatedRoot to target an isolated copy."
}
foreach ($required in @($WebBuild, (Join-Path $WebBuild 'index.html'), (Join-Path $WebBuild 'hud-output.html'), (Join-Path $WebBuild 'assets'), $NativeExe, $RecoveryService, $ControllerVisibilityProbe, $RtssProfileHelper, $RtssOverlayBridge)) {
  if (-not (Test-Path -LiteralPath $required)) { throw "Build output is incomplete: $required" }
}
# Freshness gate: never export an artifact older than its source (see
# Assert-ExportIsFresh for the QPC/0910-17 regression this prevents).
Assert-ExportIsFresh $ProjectRoot $WorkspaceRoot | Out-Null
$exportBuildIdentity = Assert-ExportBuildIdentity -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot
# Ordinary local test exports deliberately have no formal fixture receipts.
# Admit only the declaration already bound to the complete verified archive.
$testExportRoot = Join-Path $WorkspaceRoot 'Build\Package\CompleteRoot'
$testExportMarker = Join-Path $testExportRoot 'YeManCC\test-export.json'
if (Test-Path -LiteralPath $testExportMarker -PathType Leaf) {
  Assert-ExportArchiveIdentity -Zip (Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip') -Root $testExportRoot -BuildIdentity $exportBuildIdentity | Out-Null
  $testDeclaration = Get-Content -LiteralPath $testExportMarker -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($testDeclaration.schemaVersion -ne 1 -or $testDeclaration.kind -cne 'mainline-local-test-export' -or
      $testDeclaration.formalReleaseAccepted -isnot [bool] -or $testDeclaration.formalReleaseAccepted -ne $false -or
      $testDeclaration.fixtureValidation -cne 'NOT_RUN' -or $testDeclaration.gitPublished -isnot [bool] -or $testDeclaration.gitPublished -ne $false) {
    throw 'TEST_EXPORT_DEPLOY_DECLARATION_INVALID'
  }
  $env:YEMAN_TEST_EXPORT = '1'
  Write-Output 'TEST_EXPORT_DEPLOY: formal Decky Build/Package fixture receipts NOT_RUN; source, complete archive and payload identity verified.'
} else {
  $env:YEMAN_TEST_EXPORT = '0'
  & (Join-Path $PSScriptRoot 'decky_update_release_gate.ps1') -Stage Verify -WorkspaceRoot $WorkspaceRoot
}
Assert-MonitorHudWebExport $WebBuild | Out-Null
Assert-MonitorHudDeckyExport $DeckyBuild | Out-Null
& node (Join-Path $PSScriptRoot 'build-decky-sidebar-plugin.mjs') --out $DeckyBuild --check
if ($LASTEXITCODE -ne 0) { throw "Decky sidebar build identity rejected: exit=$LASTEXITCODE" }
# Fail before any target file or process is touched; never silently preserve stale V2 bytes.
& (Join-Path $PSScriptRoot 'fan_host_path_gate.ps1') -ProjectRoot $ProjectRoot -SourceOnly | Out-Null
. (Join-Path $PSScriptRoot 'fan-host-layout.ps1')
Assert-FanHostExistingExport (Join-Path $sourcePowerControl 'fan-host') (Join-Path (Split-Path -Parent $InstallRoot) 'PowerControl\fan-host-v2') | Out-Null

# ================================================================
# Adopted-optimization inheritance gate - pre-copy (release ruling, 2026-09-20).
# Re-hashes the Build stage against the per-build capture and re-probes every
# adopted optimization (binary marker + source anchor) BEFORE anything under the
# install root is touched. A missing capture, a missing optimization or a
# tampered Build output aborts the deployment here. No silent skip switch.
# ================================================================
$OptGateScript = Join-Path $ProjectRoot 'tools\opt-inheritance-gate.ps1'
$OptBuildManifest = Join-Path $WorkspaceRoot 'Build\Validation\opt-gate\BUILD__build.json'
if (-not (Test-Path -LiteralPath $OptBuildManifest -PathType Leaf)) {
  throw "Adopted-optimization inheritance gate: missing build capture $OptBuildManifest. Run build-workspace.ps1 (which writes it) before deploying."
}
& $OptGateScript -Mode Verify -Manifest $OptBuildManifest -WorkspaceRoot $WorkspaceRoot
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED (pre-copy, exit=$LASTEXITCODE): refusing to replace installed files."
}
# Explicit nine-file production export. Two hash-identical utilities resolve
# from PowerControl/ymcc-shared-runtime via RuntimeSharedDependencies. Build
# output remains unthinned; only candidate/package/deploy staging is deduplicated.
$inputHostFiles = @(
  'YeManInputHost.exe',
  'YeManInputHost.dll',
  'YeManInputHost.deps.json',
  'YeManInputHost.runtimeconfig.json',
  'HIDMaestro.Core.dll',
  'Nefarius.Drivers.HidHide.dll',
  'Nefarius.Vicius.Abstractions.dll',
  'Microsoft.Extensions.DependencyInjection.Abstractions.dll',
  'Microsoft.Extensions.Logging.Abstractions.dll'
)
$sharedRuntimeFallbackHashes = @{
  'Nefarius.Utilities.DeviceManagement.dll' = 'B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C'
  'Newtonsoft.Json.dll' = 'A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B'
}
Assert-YmccSharedRuntimeSource $ProjectRoot
$sharedRuntimeSource = Join-Path $ProjectRoot 'deps\ymcc-shared-runtime\bin'
$inputHostMissing = @()
if (Test-Path -LiteralPath $NativeInputHost -PathType Container) {
  $inputHostMissing = @($inputHostFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $NativeInputHost $_) -PathType Leaf) })
} else {
  $inputHostMissing = $inputHostFiles
}
$nativeInputHostReady = $inputHostMissing.Count -eq 0
# Virtual-gamepad bundle is part of the normal export (HIDMaestro/InputHost,
# HidHide installer+CLI, locked HC runtime and both markers merged in).
# The bundle is only meaningful when the canonical XInput source is intact, so
# the InputHost files are a hard requirement of every export.
if (-not $nativeInputHostReady) {
  throw "Normal virtual-gamepad export is missing InputHost files: $($inputHostMissing -join ', ')"
}
Assert-YmccInputHostBuild $ProjectRoot $NativeInputHost
$hcRuntimeSource = Join-Path $HcRuntimeSource $HcRuntimeFolderName
$hcRuntimeManifest = Join-Path $hcRuntimeSource 'HandheldCompanion.runtime.json'
Assert-YmccDeviceCoreRuntime -ProjectRoot $ProjectRoot -RuntimeRoot $hcRuntimeSource | Out-Null
if (-not (Test-Path -LiteralPath $hcRuntimeSource -PathType Container) -or
    -not (Test-Path -LiteralPath $hcRuntimeManifest -PathType Leaf)) {
  throw "Normal export is missing locked HC runtime $HcRuntimeFolderName"
}
# Filename-specific pins agree with the actual production resolver.
foreach ($name in $sharedRuntimeFallbackHashes.Keys) {
  $sharedPath = Get-YmccSharedRuntimeSourceFile $ProjectRoot $name
  if (-not (Test-Path -LiteralPath $sharedPath -PathType Leaf) -or
      (Get-Sha256 $sharedPath) -ne $sharedRuntimeFallbackHashes[$name]) {
    throw "Pinned InputHost shared fallback is missing or changed: $name"
  }
}


$hidHideInstallerCandidates = @(
  (Join-Path $WorkspaceRoot "Release\PowerControl\redist\$hidHideInstallerName"),
  (Join-Path $ProjectRoot "PowerControl\redist\$hidHideInstallerName"),
  (Join-Path (Split-Path -Parent $WorkspaceRoot) "Archives\Migration-Backup\20260831-152113\YMCC-Workspace\FanLab\hc-upstream\redist\$hidHideInstallerName"),
  'C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe'
) | Select-Object -Unique
$hidHideInstallerFile = $hidHideInstallerCandidates |
  Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
  Select-Object -First 1
if (-not $hidHideInstallerFile) {
  throw "Normal export is missing locked $hidHideInstallerName"
}
if ((Get-Sha256 $hidHideInstallerFile) -ne $hidHideInstallerExpectedSha256) {
  throw "Normal export has an unexpected $hidHideInstallerName hash"
}

$hidHideCliFile = @(
  'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe',
  (Join-Path $ProjectRoot "PowerControl\redist\$hidHideCliName"),
  'C:\SOFT\YeMan\PowerControl\redist\HidHideCLI.exe'
) | Select-Object -Unique |
  Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
  Select-Object -First 1
if (-not $hidHideCliFile) {
  throw "Normal export is missing locked $hidHideCliName"
}
if ((Get-Sha256 $hidHideCliFile) -ne $hidHideCliExpectedSha256) {
  throw "Normal export has an unexpected $hidHideCliName hash"
}
foreach ($feature in @('gyro-motion', 'virtual-gamepad')) {
  $featurePath = Join-Path $FeatureAssetsSource $feature
  if (-not (Test-Path -LiteralPath $featurePath -PathType Container)) {
    throw "Feature asset directory is missing: $featurePath"
  }
}
foreach ($relative in $CustomSteamLibraryFiles) {
  $source = Join-Path $CustomSteamLibrarySource $relative
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "CustomSteamLibrary source file is missing: $source"
  }
}
$customManifest = (Get-Content -LiteralPath (Join-Path $CustomSteamLibrarySource 'package-manifest.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
if ([string]$customManifest.packageId -ne 'custom-steam-library' -or [string]$customManifest.packageType -ne 'green-child') {
  throw 'CustomSteamLibrary package identity is invalid'
}
if ([string]$customManifest.packageVersion -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
  throw "CustomSteamLibrary package version is invalid: $($customManifest.packageVersion)"
}
if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
  # A clean export is an allowed deployment starting point. The caller owns
  # the exact fixed target above; recreate only that directory after its
  # existence has been validated, never a broad or computed parent.
  New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
}
if ($IsolatedRoot) {
  Write-Output 'ISOLATED-ROOT: running-product check skipped (not executed)'
} elseif (Get-Process -Name 'YeManCC','CustomSteamLibrary','SteamArtworkLab','YeManRecoveryService','YeManControllerVisibilityProbe' -ErrorAction SilentlyContinue) {
  throw 'YeManCC.exe, CustomSteamLibrary.exe, SteamArtworkLab.exe, YeManRecoveryService.exe or YeManControllerVisibilityProbe.exe is running. Exit them before deployment.'
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$BackupRoot = Join-Path $InstallRoot ".deployment-backup-$timestamp"
New-Item -ItemType Directory -Path $BackupRoot | Out-Null
if ($IsolatedRoot) {
  $script:IsolatedNotesPath = Join-Path $BackupRoot 'ISOLATED-ROOT-NOTES.txt'
  Set-Content -LiteralPath $script:IsolatedNotesPath -Encoding UTF8 -Value @(
    'isolated-root verification run',
    'host side effects were NOT executed; each skip is listed below')
}

# The hashed asset folder must be an exact copy. Keeping old chunks allows an
# old index.html to silently bind to a new native IPC protocol.
foreach ($folder in @('assets', 'icons')) {
  $destination = Join-Path $InstallRoot $folder
  if (Test-Path -LiteralPath $destination) {
$assetsBackupParent = Join-Path $BackupRoot 'YeManCC'
New-Item -ItemType Directory -Force -Path $assetsBackupParent | Out-Null
Move-Item -LiteralPath $destination -Destination (Join-Path $BackupRoot (Join-Path 'YeManCC' $folder))
  }
}

$installedPowerControl = Join-Path (Split-Path -Parent $InstallRoot) 'PowerControl'
$installedFeatureAssets = Join-Path $installedPowerControl 'feature-assets'
$featureAssetsBackup = Join-Path $BackupRoot 'PowerControl\feature-assets'
if (Test-Path -LiteralPath $installedFeatureAssets) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $featureAssetsBackup) | Out-Null
  Move-Item -LiteralPath $installedFeatureAssets -Destination $featureAssetsBackup
}
New-Item -ItemType Directory -Force -Path $installedPowerControl | Out-Null
# Full mainline PowerControl closure. This is what makes an empty
# C:\SOFT\YeMan export runnable (T0: HWiNFO/TDP/PawnIO/OpenSpeedy/RTSS must be
# present, not just feature-assets): first the ordinary main assets via the
# formal release template policy, then the hash-locked third-party assets.
# Preserve the ACTUAL previous installed plugin before ordinary assets can overwrite it.
Move-MonitorHudDeckyBackup $installedPowerControl $BackupRoot
Copy-InstalledPowerControlMainAssets $sourcePowerControl $installedPowerControl
. (Join-Path $PSScriptRoot 'steam-appearance-export.ps1')
Assert-SteamAppearanceExport $sourcePowerControl $installedPowerControl | Out-Null
# Source-tree dist is not authoritative: install the verified matching task build.
Copy-MonitorHudDeckyExport $DeckyBuild $installedPowerControl
# Record the exact identity also consumed by the normal whole-package updater.
. (Join-Path $ProjectRoot 'native\decky_package_maintenance.ps1')
$deckyManaged = New-YmccDeckyManifest (Join-Path $installedPowerControl 'decky') (Join-Path $ProjectRoot 'decky-plugin\vendor\decky-loader.lock.json')
$deckyReceipt = Join-Path $installedPowerControl 'decky\.ymcc-managed.json'
if (Test-Path -LiteralPath $deckyReceipt -PathType Leaf) {
  $deckyReceiptBackup = Join-Path $BackupRoot 'PowerControl\decky\.ymcc-managed.json'
  New-Item -ItemType Directory -Path (Split-Path -Parent $deckyReceiptBackup) -Force | Out-Null
  Copy-Item -LiteralPath $deckyReceipt -Destination $deckyReceiptBackup -Force
}
$deckyManaged | ConvertTo-Json -Depth 8 -Compress | Set-Content -LiteralPath $deckyReceipt -Encoding UTF8
Copy-InstalledLockedPowerControlAssets $ProjectRoot $WorkspaceRoot $installedPowerControl $BackupRoot | Out-Null
$installedSharedRuntime = Join-Path $installedPowerControl 'ymcc-shared-runtime'
Copy-YmccSharedRuntime $ProjectRoot $installedSharedRuntime (Join-Path $BackupRoot 'PowerControl\ymcc-shared-runtime')
Assert-YmccBatteryHostCopies $ProjectRoot (Join-Path $installedPowerControl 'battery-host')
# fan-host is a separate, ACL-authenticated payload lane. On an existing
# install it must be preserved untouched; on a clean export the source payload
# is staged so the dedicated install-fan-host-payload.ps1 remains runnable.
$sourceFanHost = Join-Path $sourcePowerControl 'fan-host'
$installedFanHost = Join-Path $installedPowerControl 'fan-host-v2'
$fanHostDeployMode = if (Test-Path -LiteralPath $installedFanHost -PathType Container) {
  'preserved'
} else {
  Copy-Item -LiteralPath $sourceFanHost -Destination $installedFanHost -Recurse -Force
  'staged'
}
# Use the validated normal-build tuple; the planned native is copied later.
# Stage and verify lighting before replacing its old directory, with rollback.
Copy-InstalledLightingPayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -SourcePowerControl $sourcePowerControl -InstalledPowerControl $installedPowerControl -BackupRoot $BackupRoot -NativeExe $NativeExe -ExportBuildIdentity $exportBuildIdentity | Out-Null
Copy-Item -LiteralPath $FeatureAssetsSource -Destination $installedFeatureAssets -Recurse -Force
$virtualGamepadAssets = Join-Path $installedFeatureAssets 'virtual-gamepad'
# P0-3（2026-09-13 用户裁决，HC 严格）：删除 test-xinput-suppress.flag 写入——
# SUSPEND/journal/recovery 机制已整体退役（HC 无 journal、无 recovery 子进程，
# InstallNullDriver 仅 HC FixVirtualSlot 插槽自愈内联使用，ControllerManager.cs:2001、
# App.xaml.cs:648-716）。test-auto-install.flag 保留（HIDMaestro 驱动自动安装门）。
New-Item -ItemType Directory -Force -Path $virtualGamepadAssets | Out-Null
Set-Content -LiteralPath (Join-Path $virtualGamepadAssets 'test-auto-install.flag') `
  -Value 'HIDMAESTRO-TEST-AUTO-INSTALL-v1' -Encoding Ascii

$installedRedist = Join-Path $installedPowerControl 'redist'
$redistBackup = Join-Path $BackupRoot 'PowerControl\redist'
New-Item -ItemType Directory -Force -Path $installedRedist | Out-Null
# GP-PREREQ-2: same public first-install assets as normal complete export.
& (Join-Path $PSScriptRoot 'build-gamepad-prerequisites.ps1') -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw 'First-install setup build failed.' }
$setupRedistFiles = @('HIDMaestroSetup.exe','HIDMaestroSetup.dll','HIDMaestroSetup.deps.json','HIDMaestroSetup.runtimeconfig.json','HIDMaestroSetup.manifest.json') | ForEach-Object { Join-Path $ProjectRoot ("PowerControl\redist\" + $_) }
foreach ($redistFile in (@($hidHideInstallerFile, $hidHideCliFile) + $setupRedistFiles)) {
  if (-not $redistFile) { continue }
  $destination = Join-Path $installedRedist (Split-Path -Leaf $redistFile)
  if ([IO.Path]::GetFullPath($redistFile).TrimEnd('\') -eq
      [IO.Path]::GetFullPath($destination).TrimEnd('\')) {
    continue
  }
  if (Test-Path -LiteralPath $destination -PathType Leaf) {
    $backup = Join-Path $redistBackup (Split-Path -Leaf $destination)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backup -Force
  }
  Copy-Item -LiteralPath $redistFile -Destination $destination -Force
}

$installedHcRuntimeRoot = Join-Path $installedPowerControl 'handheldcompanion-runtime'
$installedHcRuntime = Join-Path $installedHcRuntimeRoot $HcRuntimeFolderName
# 203 (2026-09-22, W3-17 finding F1): the backup must MIRROR the install layout. The previous form moved the
# whole handheldcompanion-runtime root INTO a destination leaf that is itself named $HcRuntimeFolderName, so the
# backup ended up as ...\handheldcompanion-runtime\HC-X\HC-X\<files>; a naive 1:1 mirror restore then put the
# runtime back one level too deep (measured: 3 files unrestored). Move the root's CONTENTS instead, and assert
# the mirrored layout so this class of defect fails closed.
$hcRuntimeBackupRoot = Join-Path $BackupRoot 'PowerControl\handheldcompanion-runtime'
if (Test-Path -LiteralPath $installedHcRuntimeRoot -PathType Container) {
  New-Item -ItemType Directory -Force -Path $hcRuntimeBackupRoot | Out-Null
  foreach ($hcRuntimeEntry in Get-ChildItem -LiteralPath $installedHcRuntimeRoot -Force) {
    Move-Item -LiteralPath $hcRuntimeEntry.FullName -Destination $hcRuntimeBackupRoot
  }
  $hcBackupInner = Join-Path $hcRuntimeBackupRoot $HcRuntimeFolderName
  if (-not (Test-Path -LiteralPath $hcBackupInner -PathType Container)) {
    throw "HC runtime backup layout is not a 1:1 mirror (missing $hcBackupInner); refusing to continue"
  }
  if (Test-Path -LiteralPath (Join-Path $hcBackupInner $HcRuntimeFolderName)) {
    throw "HC runtime backup is nested one level too deep (would break a mirror restore); refusing to continue"
  }
}
New-Item -ItemType Directory -Force -Path $installedHcRuntimeRoot | Out-Null
Copy-Item -LiteralPath $hcRuntimeSource -Destination $installedHcRuntime -Recurse -Force

$installedCustomSteamLibrary = Join-Path $InstallRoot 'CustomSteamLibrary'
$customSteamLibraryBackup = Join-Path $BackupRoot 'YeManCC\CustomSteamLibrary'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $customSteamLibraryBackup) | Out-Null
foreach ($relative in $CustomSteamLibraryFiles) {
  $source = Join-Path $CustomSteamLibrarySource $relative
  $destination = Join-Path $installedCustomSteamLibrary $relative
  if (Test-Path -LiteralPath $destination -PathType Container) {
    throw "CustomSteamLibrary managed path is a directory but source is a file: $relative"
  }
  if (Test-Path -LiteralPath $destination -PathType Leaf) {
    $backup = Join-Path $customSteamLibraryBackup $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backup -Force
  }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath $source -Destination $destination -Force
}

foreach ($item in Get-ChildItem -LiteralPath $WebBuild -Force) {
  $destination = Join-Path $InstallRoot $item.Name
  if ($item.PSIsContainer) {
    Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse -Force
  } else {
    Copy-WithBackup $item.FullName $destination $BackupRoot
  }
}
Copy-WithBackup $NativeExe (Join-Path $InstallRoot 'YeManCC.exe') $BackupRoot
Copy-WithBackup $RecoveryService (Join-Path $InstallRoot 'YeManRecoveryService.exe') $BackupRoot
Copy-WithBackup $ControllerVisibilityProbe (Join-Path $InstallRoot 'YeManControllerVisibilityProbe.exe') $BackupRoot
Copy-WithBackup $RtssProfileHelper (Join-Path $InstallRoot 'YMCCRtssProfileHelper.exe') $BackupRoot
Copy-WithBackup $RtssOverlayBridge (Join-Path $InstallRoot 'YMCCOverlayBridge.dll') $BackupRoot
# S1 墙 A（虚拟手柄常驻-槽位钉死任务书 2026-09-15）：部署 OpenXInput 代理。
# HC 锁 runtime 的 Xinput1_4.dll（176,640B）携带 OpenXInputGetUserIndex/
# SetUserIndex 扩展导出；放 YeManCC.exe 同目录（DLL 搜索序应用目录优先）后
# native 的 ymccResolveOpenXInput 即 probe 成功，槽位守护生效。
$openXInputProxySource = Join-Path $hcRuntimeSource 'Xinput1_4.dll'
if (Test-Path -LiteralPath $openXInputProxySource -PathType Leaf) {
    Copy-WithBackup $openXInputProxySource (Join-Path $InstallRoot 'Xinput1_4.dll') $BackupRoot
} else {
    throw "OpenXInput proxy is missing from HC runtime lock: $openXInputProxySource"
}
# The virtual-gamepad feature directory is the canonical home of the Host and
# the HumanDNA I/O adapter for installed builds. The build intermediate stays in
# Build\App\Native\InputHost; this deploy re-homes those files into the feature
# asset and retires the legacy YeManCC\InputHost folder into the backup.
$legacyInputHost = Join-Path $InstallRoot 'InputHost'
if ($nativeInputHostReady) {
  foreach ($name in $inputHostFiles) {
    $source = if ($name -in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) {
      Join-Path $NativeInputHost $name
    } else { Get-YmccSharedRuntimeSourceFile $ProjectRoot $name }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing InputHost export source: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $virtualGamepadAssets $name) -Force
    if ((Get-Sha256 $source) -ne (Get-Sha256 (Join-Path $virtualGamepadAssets $name))) { throw "InputHost copy drift: $name" }
  }
  # Remove only copied legacy template duplicates; never delete the HC lock.
  foreach ($name in $sharedRuntimeFallbackHashes.Keys) {
    $duplicate = Join-Path $virtualGamepadAssets $name
    if (Test-Path -LiteralPath $duplicate -PathType Leaf) { [IO.File]::Delete($duplicate) }
  }
}
if (Test-Path -LiteralPath $legacyInputHost -PathType Container) {
New-Item -ItemType Directory -Force -Path (Join-Path $BackupRoot 'YeManCC') | Out-Null
Move-Item -LiteralPath $legacyInputHost -Destination (Join-Path $BackupRoot 'YeManCC\InputHost') -Force
}
foreach ($name in @('version.json', 'YeMan-Support.html')) {
  $source = Join-Path $ProjectRoot $name
  if (Test-Path -LiteralPath $source -PathType Leaf) {
    Copy-WithBackup $source (Join-Path $InstallRoot $name) $BackupRoot
  }
}

# Runtime repair consumes package-generated manifests, not the Web build tree.
# Bind them and the generated prerequisite payload to the same verified ZIP;
# stale installed manifests would otherwise restore an older Decky frontend.
$completePackageRoot = Join-Path $WorkspaceRoot 'Build\Package\CompleteRoot'
Assert-ExportArchiveIdentity -Zip (Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip') -Root $completePackageRoot -BuildIdentity $exportBuildIdentity | Out-Null
foreach ($relative in @('YeManCC/decky-package.json','YeManCC/module-package.json','YeManCC/update-manifest.json','PowerControl/redist/HIDMaestroSetup.dll','PowerControl/redist/HIDMaestroSetup.manifest.json')) {
  Copy-WithBackup (Join-Path $completePackageRoot $relative) (Join-Path (Split-Path -Parent $InstallRoot) $relative) $BackupRoot
}
Assert-ExportStagedBuild (Split-Path -Parent $InstallRoot) $exportBuildIdentity | Out-Null
Assert-MonitorHudWebExport $InstallRoot $WebBuild | Out-Null
Assert-MonitorHudDeckyExport (Join-Path $installedPowerControl 'decky\plugins\ymcc-sidebar') $DeckyBuild | Out-Null
$installedIndex = Get-Content -LiteralPath (Join-Path $InstallRoot 'index.html') -Raw -Encoding UTF8
if ($installedIndex -notmatch 'src="\./(assets/[^\"]+\.js)"') { throw 'Installed index.html has no hashed JavaScript entry point' }
$entry = $Matches[1]
$sourceEntry = Join-Path $WebBuild $entry
$installedEntry = Join-Path $InstallRoot $entry
if (-not (Test-Path -LiteralPath $installedEntry -PathType Leaf)) { throw "Installed entry point is missing: $entry" }
if ((Get-Sha256 $sourceEntry) -ne (Get-Sha256 $installedEntry)) { throw "Installed entry point differs from build: $entry" }
if ((Get-Sha256 $NativeExe) -ne (Get-Sha256 (Join-Path $InstallRoot 'YeManCC.exe'))) { throw 'Installed executable differs from build' }
if ((Get-Sha256 $RecoveryService) -ne (Get-Sha256 (Join-Path $InstallRoot 'YeManRecoveryService.exe'))) { throw 'Installed recovery service differs from build' }
if ((Get-Sha256 $ControllerVisibilityProbe) -ne (Get-Sha256 (Join-Path $InstallRoot 'YeManControllerVisibilityProbe.exe'))) { throw 'Installed controller visibility probe differs from build' }
if ((Get-Sha256 $RtssProfileHelper) -ne (Get-Sha256 (Join-Path $InstallRoot 'YMCCRtssProfileHelper.exe'))) { throw 'Installed RTSS helper differs from build' }
if ((Get-Sha256 $RtssOverlayBridge) -ne (Get-Sha256 (Join-Path $InstallRoot 'YMCCOverlayBridge.dll'))) { throw 'Installed RTSS bridge differs from build' }

$sourceAssets = @(Get-ChildItem -LiteralPath (Join-Path $WebBuild 'assets') -Recurse -File | ForEach-Object { $_.FullName.Substring((Join-Path $WebBuild 'assets').Length).TrimStart('\') } | Sort-Object)
$installedAssets = @(Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'assets') -Recurse -File | ForEach-Object { $_.FullName.Substring((Join-Path $InstallRoot 'assets').Length).TrimStart('\') } | Sort-Object)
if (Compare-Object $sourceAssets $installedAssets) { throw 'Installed web asset set differs from the build' }
$sourceFeatureAssets = @(Get-ChildItem -LiteralPath $FeatureAssetsSource -Recurse -File | ForEach-Object { $_.FullName.Substring($FeatureAssetsSource.Length).TrimStart('\') } | Sort-Object)
$installedFeatureAssetsFiles = @(Get-ChildItem -LiteralPath $installedFeatureAssets -Recurse -File | ForEach-Object { $_.FullName.Substring($installedFeatureAssets.Length).TrimStart('\') } | Sort-Object)
$sourceFeatureAssets = @($sourceFeatureAssets | Where-Object { $_ -notin @('virtual-gamepad\Nefarius.Utilities.DeviceManagement.dll','virtual-gamepad\Newtonsoft.Json.dll') })
$expectedFeatureAssets = @($sourceFeatureAssets +
  'virtual-gamepad\test-auto-install.flag' +
  @($inputHostFiles | ForEach-Object { "virtual-gamepad\$_" }) | Sort-Object -Unique)
if (Compare-Object $expectedFeatureAssets $installedFeatureAssetsFiles) { throw 'Installed feature asset set differs from source/bundle' }
Assert-YmccSharedRuntimeStaged $ProjectRoot $installedSharedRuntime
# Resolve every deps.json runtime declaration at its approved on-disk location.
$hostDeps = Get-Content -LiteralPath (Join-Path $virtualGamepadAssets 'YeManInputHost.deps.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($target in @($hostDeps.targets.PSObject.Properties)) {
  foreach ($pkg in @($target.Value.PSObject.Properties)) {
    if ($null -eq $pkg.Value.runtime) { continue }
    foreach ($name in @($pkg.Value.runtime.PSObject.Properties.Name)) {
      $runtimePath = if ($sharedRuntimeFallbackHashes.ContainsKey($name)) { Join-Path $installedSharedRuntime $name } else { Join-Path $virtualGamepadAssets $name }
      if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) { throw "Installed InputHost runtime closure missing: $name" }
      if ($sharedRuntimeFallbackHashes.ContainsKey($name)) {
        if (Test-Path -LiteralPath (Join-Path $virtualGamepadAssets $name)) { throw "Unexpected local shared fallback duplicate: $name" }
        if ((Get-Sha256 $runtimePath) -ne $sharedRuntimeFallbackHashes[$name]) { throw "Installed shared fallback hash mismatch: $name" }
      }
    }
  }
}

# X4（YS-9122）：虚拟手柄包逐哈希——防「宿主 exe/dll 新 + 依赖 dll 缺」式新旧
# 混装（912-2 部署路径产物）。Compare-Object 只比文件集，挡不住混装；9 件
# 逐一断言安装件等于各自的 Build 或 HC 锁定源件。三个旗标（enabled.flag / input-capture.flag /
# .test-sidecar.json）与输入捕获旗标运行期可变，保持文件集对比即可。
foreach ($inputHostFile in $inputHostFiles) {
  $installed = Join-Path $virtualGamepadAssets $inputHostFile
  $source = if ($inputHostFile -in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) {
    Join-Path $NativeInputHost $inputHostFile
  } else { Get-YmccSharedRuntimeSourceFile $ProjectRoot $inputHostFile }
  if (-not (Test-Path -LiteralPath $installed -PathType Leaf) -or
      (Get-Sha256 $installed) -ne (Get-Sha256 $source)) {
    throw "Installed InputHost file differs from build: $inputHostFile"
  }
}
$installedRedist = Join-Path $installedPowerControl 'redist'
$installedInstaller = Join-Path $installedRedist $hidHideInstallerName
if (-not (Test-Path -LiteralPath $installedInstaller -PathType Leaf) -or
    (Get-Sha256 $installedInstaller) -ne $hidHideInstallerExpectedSha256) {
  throw "Installed export is missing locked $hidHideInstallerName"
}
if ($hidHideCliFile) {
  $installedCli = Join-Path $installedRedist $hidHideCliName
  if (-not (Test-Path -LiteralPath $installedCli -PathType Leaf) -or
      (Get-Sha256 $installedCli) -ne $hidHideCliExpectedSha256) {
    throw "Installed export is missing locked $hidHideCliName"
  }
}

$runtimeManifest = Get-Content -LiteralPath (Join-Path $installedHcRuntime 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ([string]$runtimeManifest.runtimeId -ne $HcRuntimeFolderName) {
  throw "Installed HC runtime identity differs: $($runtimeManifest.runtimeId)"
}
$sourceRuntimeFiles = @(
  Get-ChildItem -LiteralPath $hcRuntimeSource -Recurse -File |
    ForEach-Object { $_.FullName.Substring($hcRuntimeSource.Length).TrimStart('\') } |
    Sort-Object
)
$installedRuntimeFiles = @(
  Get-ChildItem -LiteralPath $installedHcRuntime -Recurse -File |
    ForEach-Object { $_.FullName.Substring($installedHcRuntime.Length).TrimStart('\') } |
    Sort-Object
)
if (Compare-Object $sourceRuntimeFiles $installedRuntimeFiles) {
  throw 'Installed HC runtime file set differs from source'
}
foreach ($runtimeEntry in @($runtimeManifest.files)) {
  $relative = ([string]$runtimeEntry.path).Replace('/', '\')
  $source = Join-Path $hcRuntimeSource $relative
  $installed = Join-Path $installedHcRuntime $relative
  if (-not (Test-Path -LiteralPath $installed -PathType Leaf) -or
      (Get-Sha256 $source) -ne ([string]$runtimeEntry.sha256).ToUpperInvariant() -or
      (Get-Sha256 $installed) -ne ([string]$runtimeEntry.sha256).ToUpperInvariant()) {
    throw "Installed HC runtime file differs from locked manifest: $relative"
  }
}

# ================================================================
# fan-host 部署后自检（R-publisher，2026-09-11）：全绿才允许交付。
# 1) payload 8 件逐哈希 + HC runtime 122 件（上段已逐哈希）+ 文件集闭包
# 2) handshake smoke：运行时存在则 POST /api/handshake 断言 ok 且 reason 为空
#    （不再 conflict-locked）；未运行时输出 HANDSHAKE_SKIPPED 提示目标机复验
# 3) native --machine-identity-selftest、InputHost 三 selftest 全 PASS
# ================================================================
$fanHostManifest = Get-Content -LiteralPath (Join-Path $installedFanHost 'YeManFanHost.payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ([string]$fanHostManifest.runtimeId -ne $HcRuntimeFolderName) {
  throw "Installed Fan Host manifest runtimeId differs: $($fanHostManifest.runtimeId)"
}
$fanHostExpectedFiles = @($fanHostManifest.files | ForEach-Object { [string]$_.path } | Sort-Object -Unique)
$fanHostPhysicalFiles = @(Get-ChildItem -LiteralPath $installedFanHost -File -Recurse -Force |
  ForEach-Object { $_.FullName.Substring($installedFanHost.Length).TrimStart('\').Replace('\', '/') } |
  Where-Object { $_ -ne 'YeManFanHost.payload.json' } | Sort-Object -Unique)
if (Compare-Object $fanHostExpectedFiles $fanHostPhysicalFiles) {
  throw 'Installed Fan Host payload files do not exactly match the manifest'
}
foreach ($fanEntry in @($fanHostManifest.files)) {
  $relative = ([string]$fanEntry.path).Replace('/', '\')
  $installed = Join-Path $installedFanHost $relative
  if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { throw "Installed Fan Host payload file missing: $relative" }
  if ((Get-Sha256 $installed) -ne ([string]$fanEntry.sha256).ToUpperInvariant()) {
    throw "Installed Fan Host payload hash mismatch: $relative"
  }
}
# Handshake smoke：只需读出 HTTP 端点 ok/reason，不做任何硬件写入。
$fanHostSmoke = 'HANDSHAKE_NOT_ATTEMPTED'
if ($IsolatedRoot) {
  $fanHostSmoke = 'SKIPPED-ISOLATED-ROOT'
  Add-IsolatedNote 'Fan Host handshake probe (live 127.0.0.1:8765) - not executed'
} else {
$fanHostProcess = Get-Process -Name 'YeManFanHost' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($fanHostProcess) {
  try {
    $probe = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/api/handshake' -Method POST -UseBasicParsing -TimeoutSec 10
    $body = $probe.Content
    $ok = $body -match '"ok"\s*:\s*true'
    $conflictLocked = $body -match 'conflict-locked'
    if (-not $ok -or $conflictLocked) {
      throw "Fan Host handshake smoke failed (ok=$ok conflictLocked=$conflictLocked): $body"
    }
    $fanHostSmoke = 'HANDSHAKE_OK'
  } catch {
    if ($_.Exception.Message -like '*conflict-locked*') { throw "Fan Host handshake smoke conflict-locked: $($_.Exception.Message)" }
    Write-Output "HANDSHAKE_SKIPPED: Fan Host 运行中但端点探测未完成 ($($_.Exception.Message))"
  }
} else {
  Write-Output 'HANDSHAKE_SKIPPED: Fan Host 未运行，目标机上由主程序启动后复验 /api/handshake（要求 ok=true 且 reason 为空）'
}
}
# native machine-identity selftest（exit 0 = PASS，无需硬件）。YeManCC.exe
# 的 app.manifest 是 requireAdministrator——非提权部署上下文无法启动它（UAC
# 即时报"requires elevation"）。因此：提权上下文为硬门；非提权输出 SKIPPED
# 并记录（目标机以管理员复跑下方命令即得最终 PASS），不阻断 DEPLOY_OK。
$nativeSelftestResult = 'SKIPPED-NOT-ELEVATED'
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($IsolatedRoot) {
  $nativeSelftestResult = 'SKIPPED-ISOLATED-ROOT'
  Add-IsolatedNote 'native --machine-identity-selftest launch - not executed'
} elseif ($currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  # GUI-subsystem exe（/SUBSYSTEM:WINDOWS）+ `& exe 2>&1` 时 PowerShell 5.1 不等待，
  # $LASTEXITCODE 保持未设置（2026-09-13 P0 执行实测：native selftest 门误拒 exit=）。
  # 用 Start-Process -Wait -PassThru 确定性获取退出码。
  $machineIdentityProcess = Start-Process -FilePath (Join-Path $InstallRoot 'YeManCC.exe') `
    -ArgumentList '--machine-identity-selftest' -Wait -PassThru -WindowStyle Hidden
  if ($machineIdentityProcess.ExitCode -ne 0) { throw "native --machine-identity-selftest failed: exit=$($machineIdentityProcess.ExitCode)" }
  $nativeSelftestResult = 'PASS'
}
# InputHost 当前输出 selftest（5 项；exit 0 = PASS）：准入、Elite、Deck、Edge 线缆。
$inputHostSelftests = @('--selftest-protocol', '--selftest-virtual-output-admission', '--selftest-elite-state', '--selftest-steamdeck-state', '--selftest-edge-wire')
if ($IsolatedRoot) {
  Add-IsolatedNote 'InputHost selftests (5 launches) - not executed'
} else {
foreach ($ihSelf in $inputHostSelftests) {
  $out = & (Join-Path $virtualGamepadAssets 'YeManInputHost.exe') $ihSelf 2>&1
  if ($LASTEXITCODE -ne 0) { throw "InputHost $ihSelf failed: exit=$LASTEXITCODE" }
}
}
foreach ($relative in $CustomSteamLibraryFiles) {
  $source = Join-Path $CustomSteamLibrarySource $relative
  $installed = Join-Path $installedCustomSteamLibrary $relative
  if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { throw "Installed CustomSteamLibrary file is missing: $relative" }
  if ((Get-Sha256 $source) -ne (Get-Sha256 $installed)) { throw "Installed CustomSteamLibrary file differs from source: $relative" }
}

# T0 closure gate: a full installed export must carry the complete ordinary
# PowerControl main assets, not only the virtual-gamepad bundle. These are the
# native lookup targets that previously failed on a clean (empty) export.
$criticalPowerControlFiles = @(
  # HWiNFO configuration/recovery is compiled into YMCC; external entries are optional compatibility assets.
  'TDP\Performance.vbs',
  # R-pawnio-manifest（2026-09-11）：pawnio 根 5 件 + _internal 16 件原子集，
  # 缺 _internal\base_library.zip 时 YeManTdpCtl 弹 "Failed to start embedded
  # python interpreter!"。清单以 release-assets.lock.json PawnIO 集为准，
  # critical 检查只列关键活体，完整 21 件 + 逐哈希在下方 pawnio 自检段。
  'pawnio\YeManTdpCtl.exe', 'pawnio\PawnIO_setup.exe', 'pawnio\_internal\python313.dll',
  'pawnio\_internal\base_library.zip',
  'OpenSpeedy\bridge32.exe', 'OpenSpeedy\bridge64.exe',
  'RTSS-Overlays\YeManOBS-W-1.ovl', 'RTSS-Overlays\YeManOBS-L-1.ovl', 'RTSS-Overlays\YeManOBS-JJ-1.ovl', 'RTSS-Overlays\Empty.ovl',
  'MG-AUTO\memreduct.exe',
  'physpanel.exe'
)
foreach ($relative in $criticalPowerControlFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $installedPowerControl $relative) -PathType Leaf)) {
    throw "Installed PowerControl critical file is missing: $relative"
  }
}
# R-pawnio-manifest（2026-09-11）：改名集对比升级为逐哈希比对（原子运行时，
# 名字对上但内容损坏同样会让 python 引擎起不来；base_library.zip 1409329 为
# 关键锚点）。文件集 + 逐哈希双门，任一名/哈希不一致即拒部署。
$pawnioEntries = @(
  Get-Content -LiteralPath (Join-Path $ProjectRoot 'tools\release-assets.lock.json') -Raw -Encoding UTF8 |
    ConvertFrom-Json |
    ForEach-Object { $_.files } |
    Where-Object component -eq 'PawnIO'
)
$pawnioExpected = @($pawnioEntries | ForEach-Object { ([string]$_.releasePath).Substring('pawnio/'.Length).Replace('/', '\') } | Sort-Object)
$pawnioInstalled = @(
  Get-ChildItem -LiteralPath (Join-Path $installedPowerControl 'pawnio') -Recurse -File -ErrorAction SilentlyContinue |
    ForEach-Object { $_.FullName.Substring((Join-Path $installedPowerControl 'pawnio').Length).TrimStart('\') } |
    Sort-Object
)
if (Compare-Object $pawnioExpected $pawnioInstalled) {
  throw 'Installed PawnIO runtime does not exactly match the locked atomic file set'
}
foreach ($pawnioEntry in $pawnioEntries) {
  $relative = ([string]$pawnioEntry.releasePath).Substring('pawnio/'.Length).Replace('/', '\')
  # PS 5.1 的 Join-Path 不支持 3 位置参数（PS6+ 才有），必须两级拼接。
  # 注意：此处用 $pawnioEntry 而非 $entry，避免覆盖上方 index.html 入口变量
  # $entry（下方 DEPLOY_OK "Entry:" 输出依赖它）。
  $installed = Join-Path (Join-Path $installedPowerControl 'pawnio') $relative
  if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { throw "Installed PawnIO file missing: $relative" }
  if ((Get-Sha256 $installed) -ne ([string]$pawnioEntry.sha256).ToUpperInvariant()) {
    throw "Installed PawnIO hash mismatch: $relative ($(Get-Sha256 $installed) != $([string]$pawnioEntry.sha256))"
  }
}
# YeManTdpCtl.exe info exit==0 自检：Python 引擎活体 smoke（非静默启动，仅
# 无副作用 info 查询；errExit 与 ICMD 冲突不在本站点判断）。
$tdpCtl = Join-Path $installedPowerControl 'pawnio\YeManTdpCtl.exe'
# 报告诚实性（203 / W3-B5-F1，2026-09-22）：隔离根模式跳过该 smoke（上面不上进程），
# 因此横幅必须报实际状态，不能无条件打印 PASS（未执行≠通过）。
$tdpSmoke = 'NOT-ATTEMPTED (tool missing)'
if ($IsolatedRoot) {
  $tdpSmoke = 'SKIPPED-ISOLATED-ROOT'
  Add-IsolatedNote 'YeManTdpCtl.exe info smoke (product process launch) - not executed'
} elseif (Test-Path -LiteralPath $tdpCtl -PathType Leaf) {
  $tdpOut = & $tdpCtl info 2>&1
  if ($LASTEXITCODE -ne 0) { throw "YeManTdpCtl.exe info smoke failed: exit=$LASTEXITCODE" }
  $tdpSmoke = 'PASS'
}

# ================================================================
# Adopted-optimization inheritance gate - post-copy (release ruling, 2026-09-20).
# The installed tree must reproduce the per-build artifact hashes for every
# must-match class (native exe / Host apphost / managed DLL / SDK DLL) and carry
# every adopted optimization; the two packaging-generated files must be
# byte-identical to their source-root originals. This blocks a mixed-generation
# or partially-replaced install from ever being reported as DEPLOY_OK.
# ================================================================
$gateArgs = @{ Mode = 'Gate'; Stage = 'install'; BuildManifest = $OptBuildManifest; WorkspaceRoot = $WorkspaceRoot }
if ($IsolatedRoot) {
  # mirror the install lane layout (app/web = the install root, pc = its sibling feature-assets)
  $gateArgs.AppDirOverride = $InstallRoot
  $gateArgs.WebDirOverride = $InstallRoot
  $gateArgs.PcDirOverride = Join-Path (Split-Path -Parent $InstallRoot) 'PowerControl\feature-assets\virtual-gamepad'
  # 203 (2026-09-22, W3-18 finding O1): without an explicit -Out the gate writes gate__install.json into the
  # SHARED Mainline\Build\Validation\opt-gate, so an isolated rehearsal silently overwrote the last REAL
  # install-gate record (measured: 14:45, 14:53 and 15:04 clobbers). Redirect the isolated run's report into the
  # isolated container and say so in the accounting file.
  $gateArgs.Out = Join-Path (Split-Path -Parent $InstallRoot) '.isolated-validation\gate__install.isolated.json'
  Add-IsolatedNote ('install-stage gate report redirected to ' + $gateArgs.Out + ' (the shared gate__install.json is NOT written in isolated mode)')
}
& $OptGateScript @gateArgs
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED (install stage, exit=$LASTEXITCODE): installed tree does not inherit the adopted optimization set."
}

$installedIdentity = Write-ExportInstalledIdentityFromArchive (Split-Path -Parent $InstallRoot) (Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip') $exportBuildIdentity $BackupRoot
Write-Output ('INSTALLED_EXPORT_BUILD_ID=' + $installedIdentity.buildId)
Write-Output 'DEPLOY_OK'
Write-Output "Installed: $InstallRoot"
Write-Output "Backup:    $BackupRoot"
Write-Output "Entry:     $entry"
Write-Output "PowerControl closure: full (main assets + locked third-party assets)"
Write-Output "Feature assets: $installedFeatureAssets"
Write-Output "Virtual gamepad bundle: True (merged into normal export)"
Write-Output "HidHide installer deployed: $([bool]$hidHideInstallerFile)"
Write-Output "HidHide CLI deployed: $([bool]$hidHideCliFile)"
Write-Output "InputHost deployed: $nativeInputHostReady (feature-assets\virtual-gamepad)"
Write-Output "Legacy exe InputHost folder: $(-not (Test-Path -LiteralPath $legacyInputHost)) (retired)"
Write-Output "Shared third-party runtime: $installedSharedRuntime"
Write-Output "HC runtime deployed: $([bool]$hcRuntimeSource)"
if ($hcRuntimeSource) {
  Write-Output "HC runtime: $installedHcRuntime"
}
Write-Output "Fan host lane: $fanHostDeployMode (separate authenticated payload; run install-fan-host-payload.ps1 when staged)"
Write-Output "Fan host payload self-check: PASS (files=$(@($fanHostExpectedFiles).Count) per-hash verified)"
Write-Output "Fan host handshake smoke: $fanHostSmoke"
Write-Output "Fan host HC runtime: $($fanHostManifest.runtimeId)"
Write-Output "native machine-identity selftest: $nativeSelftestResult"
if ($IsolatedRoot) {
  Write-Output 'InputHost selftests: SKIPPED-ISOLATED-ROOT (not executed)'
  Write-Output 'Host side effects: SKIPPED (isolated-root mode; see ISOLATED-ROOT-NOTES.txt in the backup directory)'
} else {
Write-Output "InputHost selftests: PASS ($(($inputHostSelftests | ForEach-Object { $_ -replace '^--selftest-', '' }) -join '/'))"
}
Write-Output "PawnIO runtime: PASS (files=$($pawnioExpected.Count) per-hash verified)"
Write-Output "YeManTdpCtl info smoke: $tdpSmoke"
Write-Output "Critical PowerControl files: verified ($($criticalPowerControlFiles.Count))"
Write-Output "CustomSteamLibrary: $installedCustomSteamLibrary"
Write-Output "CustomSteamLibrary version: $($customManifest.packageVersion)"
