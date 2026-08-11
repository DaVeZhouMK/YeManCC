<#
.SYNOPSIS
  Publish script: sync built dist/ into the FIXED install location.

  Design: dev happens in YeManCC3/, the finished app lives at C:\SOFT\YeMan\
  (program area). The Task Scheduler is just a fixed pointer to this location
  (C:\SOFT\YeMan\YeManCC\YeManCC.exe --minimized).

  Prereqs (run on this machine first):
    1) pnpm run build         (vite build -> dist/)
    2) compile native shell   (native/YeManCC.exe -> dist/YeManCC.exe)
  This script only publishes dist/ to the install dir; it does NOT compile.

.USAGE
  pnpm run publish
#>
$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$srcDist     = Join-Path $ProjectRoot 'dist'
$instRoot    = 'C:\SOFT\YeMan\YeManCC'
$instExe     = Join-Path $instRoot 'YeManCC.exe'

if (-not (Test-Path $srcDist)) {
    Write-Error "dist/ not found. Run: pnpm run build"
    exit 1
}
if (-not (Test-Path (Join-Path $srcDist 'YeManCC.exe'))) {
    Write-Error "dist/YeManCC.exe missing. Compile native shell first (native/YeManCC.exe -> dist/YeManCC.exe)"
    exit 1
}

if (-not (Test-Path $instRoot)) { New-Item -ItemType Directory -Path $instRoot -Force | Out-Null }

# Copy the web shell beside the finished executable without creating a nested dist/.
# The merge copy keeps files added by players.
robocopy $srcDist $instRoot /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /XF YeManCC.exe
if ($LASTEXITCODE -ge 8) { Write-Error "web asset copy failed, exit=$LASTEXITCODE"; exit $LASTEXITCODE }

Copy-Item (Join-Path $srcDist 'YeManCC.exe') $instExe -Force

$powerControlSrc = Join-Path $ProjectRoot 'PowerControl'
$powerControlDst = 'C:\SOFT\YeMan\PowerControl'

# Release source must contain runtime payloads only. Never silently publish
# build output or retired dependencies from a nested staging directory.
$forbiddenNestedArtifacts = Get-ChildItem -LiteralPath $powerControlSrc -Recurse -Force -ErrorAction SilentlyContinue |
  Where-Object {
    $relative = $_.FullName.Substring($powerControlSrc.Length).TrimStart('\\')
    $relative -match '(^|\\)(build|dist|__pycache__|KX\.bak_removed|product-old-files-[^\\]+)(\\|$)' -or
      $_.Name -match '\.bak(?:_|$)'
  }
if ($forbiddenNestedArtifacts) {
  $names = ($forbiddenNestedArtifacts | Select-Object -First 20 -ExpandProperty FullName) -join "`n"
  Write-Error "Forbidden build/backup artifacts are present in PowerControl source:`n$names"
  exit 1
}

# These directories are retired and must never enter a finished release.
$forbiddenPowerControlDirs = @('TPD', 'intel', 'ryzenadj', 'tools')
foreach ($forbiddenDir in $forbiddenPowerControlDirs) {
  $forbiddenPath = Join-Path $powerControlSrc $forbiddenDir
  if (Test-Path -LiteralPath $forbiddenPath) {
    Write-Error "Forbidden PowerControl directory exists: $forbiddenPath"
    exit 1
  }
}

# TDP is the only valid performance-script directory. Never publish the old
# typo directory, even if it is accidentally reintroduced into the source tree.
$wrongTdpSrc = Join-Path $powerControlSrc 'TPD'
$wrongTdpDst = Join-Path $powerControlDst 'TPD'
if (Test-Path -LiteralPath $wrongTdpSrc) {
  Write-Error "Invalid source directory exists: $wrongTdpSrc. Use PowerControl\TDP."
  exit 1
}
if (Test-Path -LiteralPath $wrongTdpDst) {
  Write-Error "Invalid install directory exists: $wrongTdpDst. Remove it before publishing."
  exit 1
}

$tdpSrc = Join-Path $powerControlSrc 'TDP'
if (-not (Test-Path -LiteralPath $tdpSrc -PathType Container)) {
  Write-Error "Required TDP directory is missing: $tdpSrc"
  exit 1
}

# YeManTdpCtl is an atomic runtime. In particular, LpcIO.bin must exist at the
# top level beside YeManTdpCtl.exe; the fallback copy under _internal is not a
# substitute for a complete release payload.
$requiredPawnioRuntime = @(
  'YeManTdpCtl.exe',
  'PawnIO_setup.exe',
  'AMDFamily17.bin',
  'IntelMSR.bin',
  'LpcIO.bin',
  'RyzenSMU.bin',
  '_internal\python313.dll'
)
foreach ($relative in $requiredPawnioRuntime) {
  $requiredPath = Join-Path $powerControlSrc (Join-Path 'pawnio' $relative)
  if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
    Write-Error "Required PawnIO runtime is missing: $requiredPath"
    exit 1
  }
}
foreach ($bat in Get-ChildItem -LiteralPath $tdpSrc -Filter '*.bat' -File) {
  $isPlan = $bat.BaseName -like 'Plan-*'
  $hasTdpCommand = [bool](Select-String -LiteralPath $bat.FullName -Pattern 'YeManTdpCtl\.exe.*\bset\b|%TDPCTL%\s+set\s+' -Quiet)
  if ($isPlan -and -not $hasTdpCommand) {
    Write-Error "TDP Plan script has no TDP command: $($bat.FullName)"
    exit 1
  }
  if (-not $isPlan -and $hasTdpCommand) {
    Write-Error "Non-Plan TDP script must not change watts: $($bat.FullName)"
    exit 1
  }
}

# User settings live only in the unified repository. These retired files are
# migration inputs in source history, never release payloads, and must not
# overwrite a player's settings during a later update.
$legacyConfigFiles = @(
  'ui-settings.json', 'summon.json', 'music_player.json',
  'performance-schedule.json', 'game-custom.json', 'control-config.json',
  'autofloat.json', 'tdp-auto-apply.json', 'cpu_profiles.json',
  'cpu_autostart.json', 'cpu_auto_enable.json', 'cpu_lock.json',
  'launch_apps.json', 'boot_config.json', 'yeman-power-scheme.json',
  'tray_resident.json', 'autoclose.json', 'Power.txt',
  'ui-background\background.json', 'ui-background\dynamic-online.json',
  'ui-background\dynamic-cache.json'
)
$preservedUserConfigFiles = @('yeman-settings.json', 'yeman-settings.json.bak')

# These files are generated while the finished app is running. They are not
# release assets and must never be copied back into the clean install folder.
$runtimeGeneratedFiles = @(
  '.gitignore',
  'float-active',
  'fps-monitor.hb', 'fps-monitor.pid', 'fps-monitor.log',
  'hwinfo-ok', 'hwinfo-recovery.ts',
  'speedhack.log', 'startup_trace.txt', 'topmon.json',
  'FPS-*.txt', 'tdp-*.txt', 'yeman-gcm-search-result*.json'
)

if (-not (Test-Path -LiteralPath $powerControlDst)) {
  New-Item -ItemType Directory -Path $powerControlDst -Force | Out-Null
}
# Keep source-only files out of the finished PowerControl directory.
# pawnio runtime files remain published: YeManTdpCtl.exe, PawnIO_setup.exe and *.bin.
$pawnioSourceOnly = @('YeManTdpCtl.py', 'YeManTdpCtl.py.bak_pre_daemon_sync', 'YeManTdpCtl.spec')
$copyExcludes = @($pawnioSourceOnly + $legacyConfigFiles + $preservedUserConfigFiles + $runtimeGeneratedFiles)
robocopy $powerControlSrc $powerControlDst /E /COPY:DAT /R:1 /W:1 /XJ /NFL /NDL /NJH /NJS /NP /XF $copyExcludes
if ($LASTEXITCODE -ge 8) { Write-Error "PowerControl copy failed, exit=$LASTEXITCODE"; exit $LASTEXITCODE }

# Remove source-only files left by older publications.
foreach ($sourceOnly in $pawnioSourceOnly) {
  $finishedPath = Join-Path $powerControlDst (Join-Path 'pawnio' $sourceOnly)
  if (Test-Path -LiteralPath $finishedPath) {
    Remove-Item -LiteralPath $finishedPath -Force
  }
}

# Remove retired configuration files left by older releases. Do not touch
# runtime markers, logs, task XML, scripts, TDP, pawnio, or media assets.
foreach ($legacyConfigFile in $legacyConfigFiles) {
  $finishedPath = Join-Path $powerControlDst $legacyConfigFile
  if (Test-Path -LiteralPath $finishedPath) {
    Remove-Item -LiteralPath $finishedPath -Force
  }
}

# Remove generated files that may have been left by an older release. They
# are recreated by the running app when needed and do not belong in a clean
# finished directory.
foreach ($runtimeGeneratedFile in $runtimeGeneratedFiles) {
  $finishedPattern = Join-Path $powerControlDst $runtimeGeneratedFile
  foreach ($finishedPath in (Get-ChildItem -Path $finishedPattern -File -Force -ErrorAction SilentlyContinue)) {
    Remove-Item -LiteralPath $finishedPath.FullName -Force
  }
}

# A merge publish must not leave old content-hashed web chunks beside the
# current build. Remove only stale files whose names look like Vite assets and
# are absent from the current dist/assets; player-owned icons are untouched.
$currentAssetNames = @(Get-ChildItem -LiteralPath (Join-Path $srcDist 'assets') -File -Force | ForEach-Object Name)
$staleAssetPattern = '^(CpuView|Dropdown|index|PerformanceScheduleView|PowerView|QuickAppView|RtssView|SegButton|SettingsView|SleepGuardView|Slider|StateCard|SteamView|TdpView|Toggle)-.+\.(js|css)$'
foreach ($finishedAsset in Get-ChildItem -LiteralPath (Join-Path $instRoot 'assets') -File -Force -ErrorAction SilentlyContinue) {
  if ($finishedAsset.Name -match $staleAssetPattern -and $finishedAsset.Name -notin $currentAssetNames) {
    Remove-Item -LiteralPath $finishedAsset.FullName -Force
  }
}

# These runtime payloads are published beside YeManTdpCtl.exe. Remove the old
# copies from _internal after syncing, including copies left by older builds.
$duplicatePawnioFiles = @('PawnIO_setup.exe', 'AMDFamily17.bin', 'IntelMSR.bin', 'RyzenSMU.bin')
foreach ($duplicatePawnioFile in $duplicatePawnioFiles) {
  $duplicatePath = Join-Path $powerControlDst (Join-Path 'pawnio\_internal' $duplicatePawnioFile)
  if (Test-Path -LiteralPath $duplicatePath) {
    Remove-Item -LiteralPath $duplicatePath -Force
  }
}

$supportSrc = Join-Path $ProjectRoot 'YeMan-Support.html'
if (-not (Test-Path -LiteralPath $supportSrc)) {
  Write-Error "YeMan-Support.html missing"
  exit 1
}
Copy-Item -LiteralPath $supportSrc -Destination (Join-Path $instRoot 'YeMan-Support.html') -Force

$hwinfoBatSrc = Join-Path $ProjectRoot 'PowerControl\YeManHWiNFO.bat'
$hwinfoPs1Src = Join-Path $ProjectRoot 'PowerControl\YeManHWiNFO.ps1'
foreach ($hwinfoSrc in @($hwinfoBatSrc, $hwinfoPs1Src)) {
  if (Test-Path -LiteralPath $hwinfoSrc) {
    Copy-Item -LiteralPath $hwinfoSrc -Destination (Join-Path $powerControlDst (Split-Path $hwinfoSrc -Leaf)) -Force
  }
}

Write-Output "== publish done =="
Write-Output ("  exe : " + $instExe + " (" + (Get-Item $instExe).Length + " B)")
Write-Output "  web : embedded in the finished executable"
Write-Output "Task Scheduler points to: $instExe --minimized"
Write-Output "To refresh the registered task copy after editing the XML, toggle the setting in-app once, or re-run schtasks /Create /XML (see docs)."
