<#
.SYNOPSIS
  Rebuilds the framework-dependent Fan Host payload and its integrity manifest.

.DESCRIPTION
  This is a workspace-only build step. It copies the Host executable files
  into PowerControl\fan-host and retains only the explicit HC runtime
  dependency allowlist. The manifest covers all immutable runtime files
  recursively and is verified by YeManFanHost before it loads HC.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  # The HC binary closure is an explicit frozen input. Keep the historical
  # workspace-relative default for compatibility, but allow the BUS to point
  # at the audited migration backup instead of silently using an absent or
  # guessed runtime directory.
  [string]$HcRuntimeSourceRoot = ''
)

$ErrorActionPreference = 'Stop'
if ($env:YEMAN_ALLOW_R5V9_HOST_REBUILD -ne '1') {
  throw 'Fan Host rebuild is disabled: R5-v9 is the frozen mainline payload. Set YEMAN_ALLOW_R5V9_HOST_REBUILD=1 only for an explicitly authorized re-baseline.'
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

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  if (-not $pathFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside Fan Host payload: $pathFull"
  }
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

$projectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  # Keep Build/Release beside the checkout, matching build-workspace.ps1.
  $WorkspaceRoot = Get-FullPath (Join-Path $projectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}
$hostProject = Join-Path $projectRoot 'FanLab\real-host\YeManFanHost.csproj'
$hostOutput = Join-Path $projectRoot 'FanLab\real-host\bin\Release\net10.0-windows10.0.19041.0\win-x64'
$hcRuntimeSourceRoot = if ([string]::IsNullOrWhiteSpace($HcRuntimeSourceRoot)) {
  Join-Path $projectRoot 'FanLab\build\batch11-runtime-09'
} else {
  Get-FullPath $HcRuntimeSourceRoot
}
$payloadRoot = Join-Path $projectRoot 'PowerControl\fan-host'

if (-not (Test-Path -LiteralPath $hostProject -PathType Leaf)) { throw "Fan Host project missing: $hostProject" }
if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container)) { throw "Fan Host payload directory missing: $payloadRoot" }
if (-not (Test-Path -LiteralPath $hcRuntimeSourceRoot -PathType Container)) { throw "Pinned HC runtime source missing: $hcRuntimeSourceRoot" }

# The FanHost manifest is the child-side reference to the shared HC runtime.
# Derive both values from the actual selected runtime directory so a rebuilt
# Candidate cannot silently retain the previous runtime identity.
$hcRuntimeManifest = Join-Path $hcRuntimeSourceRoot 'HandheldCompanion.runtime.json'
if (-not (Test-Path -LiteralPath $hcRuntimeManifest -PathType Leaf)) { throw "Pinned HC runtime manifest missing: $hcRuntimeManifest" }
$hcRuntimeMetadata = Get-Content -LiteralPath $hcRuntimeManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$hcRuntimeId = [string]$hcRuntimeMetadata.runtimeId
if ($hcRuntimeMetadata.schemaVersion -ne 1 -or $hcRuntimeId -notmatch '^HC-CANDIDATE-0\.32\.4\.0-[0-9a-f]{8}-[0-9]{8}$') {
  throw "Pinned HC runtime manifest identity is invalid: $hcRuntimeManifest"
}
$runtimeManifestRelative = "..\handheldcompanion-runtime\$([IO.Path]::GetFileName($hcRuntimeSourceRoot))\HandheldCompanion.runtime.json"

& dotnet build $hostProject -c Release --no-restore
if ($LASTEXITCODE -ne 0) { throw "Fan Host build failed: exit=$LASTEXITCODE" }

# HC-SLIM-01 R3 (2026-10-06): the Host now carries an explicit project
# reference to FanLab\fan-backend (YeManFanBackend.dll), the ASUS fan
# curve/transport/receipt leaf component. The Host deps manifest lists it as a
# project dependency, so it must ship beside YeManFanHost.dll or the Host
# fails to load at runtime. Keep it in the explicit host set so the stale-file
# prune below cannot drop it.
$hostFiles = @(
  'YeManFanHost.exe',
  'YeManFanHost.dll',
  'YeManFanBackend.dll',
  'YeManFanHost.deps.json',
  'YeManFanHost.runtimeconfig.json',
  'YeManFanHost.json'
)

# HC is a full desktop application, but YeManFanHost uses only its device,
# OpenLib/ACPI, logging and temperature-monitor paths. Do not copy HC's
# WPF/UI, game-library, input-overlay or update components into the Host.
#
# IDevice.GetCurrent is one large switch over all device types. Its metadata
# and the ROG/GPD constructors require these input and HID assemblies before
# the selected fan route can be known. Keep this bootstrap profile explicit:
# a missing item must fail the build, rather than hiding the Fan page after a
# deployed Host reports an opaque FileNotFoundException.
$hcFactoryBootstrapFiles = @(
  'WindowsInput.dll',
  'GregsStack.InputSimulatorStandard.dll',
  'Gma.System.MouseKeyHook.dll',
  'HidLibrary.dll',
  'Nefarius.Utilities.DeviceManagement.dll',
  'Nefarius.Utilities.Bluetooth.dll',
  'Nefarius.Vicius.Abstractions.dll',
  'PInvoke.Kernel32.dll',
  'PInvoke.Windows.Core.dll',
  'SharpDX.dll',
  # HC initializes its GPU manager while ManagerFactory is created. The GPU
  # implementations bind Direct3D9 even when Fan Host does not use GPU data.
  'SharpDX.Direct3D9.dll',
  'SharpDX.DirectInput.dll',
  'SharpDX.XInput.dll',
  # OneXPlayerOxpHidMonitor uses the managed HIDAPI wrapper and this native
  # Windows implementation. Keep both in the same authenticated closure.
  'hidapi.net.dll',
  'hidapi.dll'
)
$hcFactoryBootstrapHashes = [ordered]@{
  'WindowsInput.dll' = '5567cea4661389a7fdcc51ef222e67b13c2176c9be46e61a88a100188a77c711'
  'GregsStack.InputSimulatorStandard.dll' = '453e8a4b4cf7241954e9aad060409c24f076ee0c9f742345fc36a1ea8dd8c6ee'
  'Gma.System.MouseKeyHook.dll' = 'fa9fec4dfc02c80d262e2e61abce31d9358ca84e36c9794ba5cb30f912940485'
  'HidLibrary.dll' = '00ad68889764a8bea6377a01d738a3ebc1dd286691d2ac5bcf7b1d2b16bcd9fa'
  'Nefarius.Utilities.DeviceManagement.dll' = 'b5eaf086634438f2774f6b65dd14254aaa078bf1ebfeb004f997314b61272b7c'
  'Nefarius.Utilities.Bluetooth.dll' = '010b46997f2bea44a9e95b063e106be3e662a93a7a7fab5e5d485644cc48b433'
  'Nefarius.Vicius.Abstractions.dll' = '51f380a12a82e925308e5d6255218df283b692f37b9e991c6ce5e63f3e11d8fa'
  'PInvoke.Kernel32.dll' = '3122b9c2ccd89b0ff915f4669d60f9ffa1a4d4a8608f61f5df1b29d6298c4c44'
  'PInvoke.Windows.Core.dll' = '28dc91c7027ba45b07be564a4564cf9e4606b96b01f4b431056e7d77ab25b81c'
  'SharpDX.dll' = '518d45a5aaec84cb37e83ee2cf58c503ab6a25febb8c48b53316340c967e84bd'
  'SharpDX.Direct3D9.dll' = '69701eda7433ac0010aba416b9d9c245cd78694770d4bb6b7541b83bace41d55'
  'SharpDX.DirectInput.dll' = '35d9ae6b98c5b68fdc1fcaf6e03c95c82f9305c7355dd911f8841880b42e945f'
  'SharpDX.XInput.dll' = '350195201205840b38aee094bcead4c78b1661f3570a7caa5c36b86ce6d03ff3'
  'hidapi.net.dll' = '5553f2487424b325f750c0fe83bd7961943cc97f2e0d1c24285506374b298f17'
  'hidapi.dll' = 'ebeb835e2b4530ed68843f19d6a2604c51772e3c26e7f542fde194075f82d9b4'
}
if (@($hcFactoryBootstrapFiles | Where-Object { -not $hcFactoryBootstrapHashes.Contains($_) }).Count -ne 0 -or
    $hcFactoryBootstrapFiles.Count -ne $hcFactoryBootstrapHashes.Count) {
  throw 'HC factory bootstrap file/hash manifest is inconsistent'
}

# The old hand-curated "small" set was insufficient: HC ManagerFactory creates
# GPU and library managers during its initialisation, and its dependencies are
# broader than IDevice.GetCurrent's IL alone. Derive the managed closure from
# HC's frozen deps manifest instead. A dependency missing from the source
# runtime fails this build, and the matching closure self-test runs below.
$hcDepsSource = Join-Path $hcRuntimeSourceRoot 'HandheldCompanion.deps.json'
if (-not (Test-Path -LiteralPath $hcDepsSource -PathType Leaf)) {
  throw "Pinned HC dependency manifest missing: $hcDepsSource"
}
$hcDeps = Get-Content -LiteralPath $hcDepsSource -Raw -Encoding UTF8 | ConvertFrom-Json
$hcTargetProperties = @($hcDeps.targets.PSObject.Properties)
if ($hcTargetProperties.Count -eq 0) { throw 'Pinned HC dependency manifest has no target graph' }
# .NET 10 publishes both the portable graph and a win-x64 RID graph.  Fan Host
# is a win-x64 process, so prefer that graph while retaining compatibility with
# older manifests that contain only one target.
$hcTargetProperty = $hcTargetProperties |
  Where-Object { $_.Name -match '(?i)/win-x64$' } |
  Select-Object -First 1
if ($null -eq $hcTargetProperty) { $hcTargetProperty = $hcTargetProperties | Select-Object -First 1 }
$hcTarget = $hcTargetProperty.Value
$hcRuntimeSources = @{
  'HandheldCompanion.deps.json' = $hcDepsSource
}
foreach ($library in $hcTarget.PSObject.Properties.Value) {
  foreach ($asset in @($library.runtime.PSObject.Properties.Name)) {
    $name = [IO.Path]::GetFileName([string]$asset)
    $source = Join-Path $hcRuntimeSourceRoot $name
    if ($name -match '\.dll$' -and (Test-Path -LiteralPath $source -PathType Leaf)) {
      $hcRuntimeSources[$name] = $source
    }
  }
  foreach ($asset in @($library.runtimeTargets.PSObject.Properties.Name)) {
    $relative = ([string]$asset).Replace('/', '\')
    $source = Join-Path $hcRuntimeSourceRoot $relative
    if ($relative -match '(?i)^runtimes\\win(?:-|\\)' -and (Test-Path -LiteralPath $source -PathType Leaf)) {
      $hcRuntimeSources[[IO.Path]::GetFileName($relative)] = $source
    }
  }
}

# These device libraries are copied by HC's build but are not expressed as
# managed deps assets. They are still reachable from mapped device routes.
$hcDeviceNativeFiles = @(
  'GamepadMotion.dll', 'hidapi.dll', 'IGCL_Wrapper.dll', 'JoyShockLibrary.dll',
  'libVIIPER.dll', 'SapientiaUsb.dll', 'SDL3.dll', 'UEFIVaribleDll.dll', 'Xinput1_4.dll'
)
$fanHostV2ExcludedFiles = @(
  'ColorPicker.dll', 'ColorPicker.Models.dll', 'Crc32.NET.dll', 'Fastenshtein.dll',
  'FluentResults.dll', 'GameFinder.Common.dll', 'GameFinder.Launcher.Heroic.dll',
  'GameFinder.RegistryUtils.dll', 'GameFinder.StoreHandlers.EADesktop.dll',
  'GameFinder.StoreHandlers.EGS.dll', 'GameFinder.StoreHandlers.GOG.dll',
  'GameFinder.StoreHandlers.Origin.dll', 'GameFinder.StoreHandlers.Steam.dll',
  'GameFinder.StoreHandlers.Xbox.dll', 'GameFinder.Wine.dll', 'GameLib.Core.dll',
  'GameLib.dll', 'GameLib.Plugin.BattleNet.dll', 'GameLib.Plugin.EA.dll',
  'GameLib.Plugin.Epic.dll', 'GameLib.Plugin.Gog.dll', 'GameLib.Plugin.Origin.dll',
  'GameLib.Plugin.RiotGames.dll', 'GameLib.Plugin.Rockstar.dll', 'GameLib.Plugin.Steam.dll',
  'GameLib.Plugin.Ubisoft.dll', 'GongSolutions.WPF.DragDrop.dll', 'HelixToolkit.Core.Wpf.dll',
  'IGCL_Wrapper.dll', 'iNKORE.UI.WPF.dll', 'iNKORE.UI.WPF.Emojis.dll',
  'Nefarius.Drivers.HidHide.dll',
  'iNKORE.UI.WPF.Modern.dll', 'JoyShockLibrary.dll', 'libVIIPER.dll', 'LiveCharts.dll',
  'LiveCharts.Wpf.dll', 'MathConverter.dll', 'Microsoft.Expression.Effects.dll',
  'Microsoft.Toolkit.Uwp.Notifications.dll', 'Microsoft.Xaml.Behaviors.dll',
  'NexusMods.Paths.dll', 'NJsonSchema.Annotations.dll', 'OneOf.dll',
  'Polly.Extensions.Http.dll', 'RTSSSharedMemoryNET.dll', 'SapientiaUsb.dll',
  'SDL3-CS.dll', 'SDL3.dll', 'SHA3.Net.dll', 'TransparentValueObjects.Abstractions.dll',
  'ValveKeyValue.dll', 'WindowsDisplayAPI.dll', 'WpfScreenHelper.dll', 'Xinput1_4.dll'
)
# Locked HC candidate runtimes may already be pre-pruned (the excluded UI/game
# assemblies are intentionally absent).  Treat absent exclusions as expected;
# any excluded files that do exist are still filtered from the payload below.
$missingV2Excluded = @($fanHostV2ExcludedFiles | Where-Object {
  -not (Test-Path -LiteralPath (Join-Path $hcRuntimeSourceRoot $_) -PathType Leaf)
})
if ($missingV2Excluded.Count -gt 0) {
  Write-Verbose "Pre-pruned HC source; absent exclusions: $($missingV2Excluded.Count)"
}
foreach ($name in $hcDeviceNativeFiles) {
  $source = Join-Path $hcRuntimeSourceRoot $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    Write-Verbose "Pinned HC candidate omits optional device native: $name"
    continue
  }
  $hcRuntimeSources[$name] = $source
}
foreach ($name in $hcFactoryBootstrapFiles) {
  $source = Join-Path $hcRuntimeSourceRoot $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Pinned HC factory bootstrap dependency missing: $name"
  }
  $hcRuntimeSources[$name] = $source
}

$hcRuntimeFiles = @($hcRuntimeSources.Keys | Where-Object {
  $_ -notin $fanHostV2ExcludedFiles
} | Sort-Object)

# HC's deps file contains both generic compile-time assemblies and Windows
# runtime targets for these three platform APIs. YeManFanHost loads HC through
# an AssemblyLoadContext resolver, so it cannot rely on the normal deps.json
# RID selection. Stage the Windows implementation at the flat payload root;
# using the generic lib/net10.0 file produces a PlatformNotSupportedException
# during MotherboardInfo initialization and makes the UI hide a real device.
$hcWindowsRuntimeOverrides = [ordered]@{
  'System.Management.dll' = @{
    SourceRelative = 'runtimes\win\lib\net10.0\System.Management.dll'
    Sha256 = '01f9360d110863f810431c4d29ada0fca89f267343d030e98aa823ea4c0c0ebb'
  }
  'System.IO.Ports.dll' = @{
    SourceRelative = 'runtimes\win\lib\net10.0\System.IO.Ports.dll'
    Sha256 = 'bf486068a47b18358313791b78aca74f4de61d1d9e2e08b58e3bfbf68bf15a2b'
  }
  'System.ServiceProcess.ServiceController.dll' = @{
    SourceRelative = 'runtimes\win\lib\net10.0\System.ServiceProcess.ServiceController.dll'
    Sha256 = '3274c2553c736435064e398f879404e8944f39790caee6632e6966046b3440e8'
  }
}
if (@($hcWindowsRuntimeOverrides.Keys | Where-Object { $_ -notin $hcRuntimeFiles }).Count -ne 0) {
  throw 'HC Windows runtime override is not part of the payload allowlist'
}

$supportFiles = @(
  'YeManFanHost.authorization.md',
  'install-fan-host-payload.ps1',
  'run-emergency-fan-restore.ps1'
)
foreach ($name in $hostFiles) {
  $source = Join-Path $hostOutput $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Host build output missing: $source" }
  Copy-Item -LiteralPath $source -Destination (Join-Path $payloadRoot $name) -Force
}

# 920 v1.15 section 26.3.2 (2026-09-21 ruling): the approved candidate runtime is now this
# production generator's direct supply. The pinned value is the candidate HandheldCompanion.dll
# (0c5132a9...); the old frozen value 886ac06b... and the earlier candidate pins (c7e202e6...,
# e8f0fbdb..., 96158883..., 5b39e42f..., 58aebb01..., 5f3eea17...) are superseded by this
# re-baseline and the FAN-936-R4 MSI fan-table wire-contract + real-Host-session-chain rebuild
# (the MSI family accepts the REAL 31-byte fan-table payload returned by the EC; Close settles on
# the latest release token; the fan handover avoids rewriting the MSI scenario; the read-only CLI
# uses the CoreReadsEffective completeness criterion - on top of the FAN-936 single closure PLUS the
# section-5.2 firmware-unknown fix in Devices/MSI/ClawA1M.cs; ASUS FAN-925B/S22-S23 retained).
# Passing the OLD HC through this gate and swapping the file afterwards is explicitly forbidden.
# Encoding contract: this file KEEPS its UTF-8 BOM (as in the section-25 baseline) and must stay
# ASCII-only for new text - PS 5.1 decodes a BOM-less script as ANSI and can drop the next
# statement, which is exactly how an added non-ASCII comment broke this gate mid-batch.
$pinnedHcAssembly = Join-Path $hcRuntimeSourceRoot 'HandheldCompanion.dll'
if ((Get-Sha256 $pinnedHcAssembly) -ne '0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49') {
  throw "Pinned HC runtime source has an unexpected HandheldCompanion.dll hash: $pinnedHcAssembly"
}
foreach ($name in $hcRuntimeFiles) {
  $source = [string]$hcRuntimeSources[$name]
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Pinned HC runtime dependency missing: $source"
  }
  $expectedHash = if ($hcFactoryBootstrapHashes.Contains($name)) {
    $hcFactoryBootstrapHashes[$name]
  } elseif ($hcWindowsRuntimeOverrides.Contains($name)) {
    $hcWindowsRuntimeOverrides[$name].Sha256
  } else { $null }
  if ($null -ne $expectedHash -and (Get-Sha256 $source) -ne $expectedHash) {
    throw "Pinned HC factory bootstrap dependency hash mismatch: $name"
  }
}

# The payload root is build output. Prune only top-level files which are not
# part of the explicit runtime/support set so a regular build cannot silently
# reintroduce the entire HC application beside the small Fan Host.
$allowedFiles = @($hostFiles + $supportFiles + 'YeManFanHost.payload.json', 'YeManFanHost.session')
foreach ($name in @($hcRuntimeFiles + $supportFiles)) {
  $path = if ($hcRuntimeFiles -contains $name) {
    Join-Path $hcRuntimeSourceRoot $name
  } else {
    Join-Path $payloadRoot $name
  }
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Fan Host dependency missing from selected source: $name"
  }
}
$staleFiles = @(Get-ChildItem -LiteralPath $payloadRoot -File | Where-Object { $_.Name -notin $allowedFiles })
foreach ($file in $staleFiles) {
  Remove-Item -LiteralPath $file.FullName -Force
  Write-Output "PRUNED_STALE_FAN_HOST_FILE $($file.Name)"
}

$manifestPath = Join-Path $payloadRoot 'YeManFanHost.payload.json'
$files = @(
  Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Where-Object {
    $_.Name -notin @('YeManFanHost.payload.json', 'YeManFanHost.session') -and
    $_.DirectoryName -notmatch '\\logs(?:\\|$)'
  } | Sort-Object FullName | ForEach-Object {
    [ordered]@{
      path = (Get-RelativePath $payloadRoot $_.FullName).Replace('\', '/')
      sha256 = Get-Sha256 $_.FullName
    }
  }
)
if ($files.Count -eq 0) { throw 'Fan Host payload manifest would be empty' }
[ordered]@{
  schemaVersion = 2
  runtimeManifest = $runtimeManifestRelative
  runtimeId = $hcRuntimeId
  files = $files
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$writtenManifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($writtenManifest.schemaVersion -ne 2 -or
    [string]$writtenManifest.runtimeId -ne $hcRuntimeId -or
    [string]$writtenManifest.runtimeManifest -ne $runtimeManifestRelative -or
    @($writtenManifest.files).Count -ne $files.Count) {
  throw 'Fan Host payload manifest generation did not produce the selected HC runtime identity'
}

$closureAudit = Join-Path $PSScriptRoot 'fan_hc_device_closure_selftest.ps1'
$metadataPowerShell = Get-Command pwsh.exe -ErrorAction SilentlyContinue
if ($null -eq $metadataPowerShell) {
  # 本机可能只有 Windows PowerShell 5.1（无 pwsh）。closure audit 只用
  # PE 元数据 API，5.1 完全兼容；回退 powershell.exe 保持重基可执行。
  $metadataPowerShell = Get-Command powershell.exe -ErrorAction Stop
}
& $metadataPowerShell.Source -NoLogo -NoProfile -ExecutionPolicy Bypass -File $closureAudit -HcRuntimeRoot $hcRuntimeSourceRoot -RuntimeRoot $hcRuntimeSourceRoot -PayloadRoot $payloadRoot -ExcludedRuntimeFiles ($fanHostV2ExcludedFiles -join ',')
if ($LASTEXITCODE -ne 0) { throw "Fan Host HC device closure audit failed: exit=$LASTEXITCODE" }

Write-Output "FAN_HOST_PAYLOAD_OK"
Write-Output "Payload:  $payloadRoot"
Write-Output "Manifest: $manifestPath"
Write-Output "Files:    $($files.Count)"
