param(
  [string[]]$Paths = @(),
  [string]$WorkspaceRoot = '',
  [string]$ProjectRoot = '',
  [string]$InstallRoot = 'C:\SOFT\YeMan',
  [string]$BuildNativeExe = '',
  [string]$EvidencePath = ''
)

# 920 8.0-E: comparing only YeManCC.exe is not a delivery gate. The apphost EXE
# can be byte-identical while the business Host (YeManFanHost.dll) differs, which
# is exactly how an old fan-host stayed hidden behind a fresh main program.
# Every delivery location must therefore also expose a Host DLL + payload
# manifest + HC runtime assembly identity, be self-consistent with its own
# manifest, and agree with the other locations. Missing business identity is
# reported as UNVERIFIABLE, never as PASS.
#
# 2026-09-20 fix: the payload directory is NOT under the EXE's own folder. The
# real layout (package-release.ps1 / the installed tree) is
#
#   <installRoot>\YeManCC\YeManCC.exe
#   <installRoot>\PowerControl\fan-host-v2\YeManFanHost.dll
#   <installRoot>\PowerControl\handheldcompanion-runtime\<runtimeId>\HandheldCompanion.dll
#
# so the payload is a SIBLING of the YeManCC directory. The previous version
# looked in <exeDir>\PowerControl\fan-host-v2, found nothing at any of the four
# locations and reported every one of them as missing business identity, which
# made the gate structurally unable to PASS.
#
# Keep this file pure ASCII: PS 5.1 misdecodes non-ASCII scripts without a BOM.

if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
if (-not $WorkspaceRoot) { $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..')) }
if (-not $EvidencePath) { $EvidencePath = Join-Path $WorkspaceRoot 'Build\Validation\YeManCC-export-verification.json' }
# The build artifact is NOT a delivery location: the build output ships no payload
# of its own (the payload is staged into Release/PowerControl at package time), so
# demanding a payload next to it would either be a permanent UNVERIFIABLE or force
# a silent substitution of somebody else's payload - the exact "quiet PASS" that
# 8.0-F forbids. It is recorded separately and used only to answer "does the
# package carry the build that was just made?".
if (-not $BuildNativeExe) { $BuildNativeExe = Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe' }
if ($Paths.Count -eq 0) {
  $Paths = @(
    (Join-Path $WorkspaceRoot 'Build\Package\UpdateRoot\YeManCC\YeManCC.exe'),
    (Join-Path $WorkspaceRoot 'Release\YeManCC\YeManCC.exe'),
    (Join-Path $InstallRoot 'YeManCC\YeManCC.exe')
  )
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))).Replace('-', ''))
  } finally { $sha.Dispose() }
}
function Get-FullPath([string]$Path) {
  if (-not $Path) { return '' }
  try { return [IO.Path]::GetFullPath($Path).TrimEnd('\') } catch { return $Path }
}
function Resolve-PayloadDir([string]$ExePath) {
  # Canonical sibling only. Nested/legacy payload fallbacks can hide a split export.
  $exeDir = Split-Path -Parent $ExePath
  $dir = Join-Path (Split-Path -Parent $exeDir) 'PowerControl\fan-host-v2'
  return @{ dir = $dir; source = if (Test-Path -LiteralPath (Join-Path $dir 'YeManFanHost.dll') -PathType Leaf) { 'exe-parent' } else { 'none' } }
}

# The locked OpenXInput proxy (HC runtime copy that carries the OpenXInput* exports).
$openXInputProxyLockedSha256 = 'C89A11166ACD59AA27DADA45B9C64494221C04ACB0B0886EA393C5D36F9071EC'
$records = @()
foreach ($path in $Paths) {
  $record = [ordered]@{
    path = $path
    exists = $false
    sha256 = $null
    length = $null
    lastWriteTimeUtc = $null
    payloadDir = $null
    payloadDirSource = 'none'
    hostDllPresent = $false
    hostDllSha256 = $null
    hostPayloadManifestPresent = $false
    hostPayloadManifestSha256 = $null
    hostDllDeclaredInManifest = $null
    hostDllMatchesManifest = 'unknown'
    hcRuntimeId = $null
    hcAssemblyPresent = $false
    hcAssemblySha256 = $null
    # OpenXInput proxy: native resolves the OpenXInput* exports from xinput1_4.dll
    # loaded out of the executable's own directory, so a delivery location without
    # that file cannot perform OpenXInput slot assignment. 2026-09-23: the proxy was
    # MATCH on 09-22 and MISSING after a pure-extract install, while every gate
    # still said PASS.
    openXInputProxyPresent = $false
    openXInputProxySha256 = $null
    openXInputProxyMatchesLock = 'unknown'
  }
  if (Test-Path -LiteralPath $path -PathType Leaf) {
    $file = Get-Item -LiteralPath $path
    $record.exists = $true
    $record.sha256 = Get-Sha256 $path
    $record.length = [int64]$file.Length
    $record.lastWriteTimeUtc = $file.LastWriteTimeUtc.ToString('o')

    $resolved = Resolve-PayloadDir $path
    $fanHostDir = $resolved.dir
    $record.payloadDir = $fanHostDir
    $record.payloadDirSource = $resolved.source
    $dllPath = Join-Path $fanHostDir 'YeManFanHost.dll'
    $manifestPath = Join-Path $fanHostDir 'YeManFanHost.payload.json'
    $runtimeId = $null
    if (Test-Path -LiteralPath $dllPath -PathType Leaf) {
      $record.hostDllPresent = $true
      $record.hostDllSha256 = Get-Sha256 $dllPath
    }
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
      $record.hostPayloadManifestPresent = $true
      $record.hostPayloadManifestSha256 = Get-Sha256 $manifestPath
      try {
        $parsed = (Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
        $runtimeId = [string]$parsed.runtimeId
        $entry = @($parsed.files | Where-Object { ([string]$_.path) -eq 'YeManFanHost.dll' })
        if ($entry.Count -eq 1) {
          $record.hostDllDeclaredInManifest = ([string]$entry[0].sha256).ToUpperInvariant()
          if ($record.hostDllPresent) {
            $record.hostDllMatchesManifest = if ($record.hostDllSha256 -eq $record.hostDllDeclaredInManifest) { 'true' } else { 'false' }
          }
        } else {
          $record.hostDllDeclaredInManifest = 'not-declared'
        }
      } catch {
        $record.hostDllDeclaredInManifest = 'parse-error'
      }
    }
    # HC runtime assembly: the payload manifest names the locked runtime id, and
    # the runtime lives beside the payload under PowerControl.
    $proxyPath = Join-Path (Split-Path -Parent $path) 'Xinput1_4.dll'
    if (Test-Path -LiteralPath $proxyPath -PathType Leaf) {
      $record.openXInputProxyPresent = $true
      $record.openXInputProxySha256 = Get-Sha256 $proxyPath
      $record.openXInputProxyMatchesLock = if ($record.openXInputProxySha256 -eq $openXInputProxyLockedSha256) { 'true' } else { 'false' }
    }
    if ($runtimeId) {
      $record.hcRuntimeId = $runtimeId
      $hcDir = Join-Path (Split-Path -Parent $fanHostDir) 'handheldcompanion-runtime'
      $hcDll = Join-Path (Join-Path $hcDir $runtimeId) 'HandheldCompanion.dll'
      if (Test-Path -LiteralPath $hcDll -PathType Leaf) {
        $record.hcAssemblyPresent = $true
        $record.hcAssemblySha256 = Get-Sha256 $hcDll
      }
    }
  }
  $records += $record
}

$existing = @($records | Where-Object { $_.exists })
$hashes = @($existing | ForEach-Object { $_.sha256 } | Select-Object -Unique)
$lengths = @($existing | ForEach-Object { $_.length } | Select-Object -Unique)
$apphostIdentical = $existing.Count -eq $Paths.Count -and $hashes.Count -eq 1 -and $lengths.Count -eq 1

$missingBusiness = @($existing | Where-Object { -not $_.hostDllPresent -or -not $_.hostPayloadManifestPresent })
$businessHashes = @($existing | Where-Object { $_.hostDllPresent } | ForEach-Object { $_.hostDllSha256 } | Select-Object -Unique)
$manifestHashes = @($existing | Where-Object { $_.hostPayloadManifestPresent } | ForEach-Object { $_.hostPayloadManifestSha256 } | Select-Object -Unique)
$inconsistent = @($existing | Where-Object { $_.hostDllMatchesManifest -ne 'true' })
$missingHcRuntime = @($existing | Where-Object { -not $_.hcAssemblyPresent })
$hcHashes = @($existing | Where-Object { $_.hcAssemblyPresent } | ForEach-Object { $_.hcAssemblySha256 } | Select-Object -Unique)

# Package-location consistency, recorded separately: the update root and the
# release tree must carry the SAME native + Host DLL + payload manifest + HC
# runtime. A stale local install is only refreshed by a controlled deployment,
# so it must not be able to hide whether the package itself is coherent.
$missingOpenXInputProxy = @($existing | Where-Object { -not $_.openXInputProxyPresent })
$openXInputProxyIdentical = ($missingOpenXInputProxy.Count -eq 0) -and ($existing.Count -gt 0) -and
  (@($existing | ForEach-Object { $_.openXInputProxySha256 } | Select-Object -Unique).Count -eq 1) -and
  (@($existing | Where-Object { $_.openXInputProxyMatchesLock -ne 'true' }).Count -eq 0)

$packagePaths = @(
  (Get-FullPath (Join-Path $WorkspaceRoot 'Build\Package\UpdateRoot\YeManCC\YeManCC.exe')),
  (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC\YeManCC.exe'))
)
$packageRecords = @($existing | Where-Object { $packagePaths -contains (Get-FullPath $_.path) })
$packageConsistent = $false
if ($packageRecords.Count -eq $packagePaths.Count) {
  $packageConsistent = (@($packageRecords | ForEach-Object { $_.sha256 } | Select-Object -Unique).Count -eq 1) -and
    (@($packageRecords | ForEach-Object { $_.hostDllSha256 } | Select-Object -Unique).Count -eq 1) -and
    (@($packageRecords | ForEach-Object { $_.hostPayloadManifestSha256 } | Select-Object -Unique).Count -eq 1) -and
    (@($packageRecords | ForEach-Object { $_.hcAssemblySha256 } | Select-Object -Unique).Count -eq 1) -and
    (@($packageRecords | Where-Object { $_.hostDllMatchesManifest -ne 'true' }).Count -eq 0)
}

$status = if (-not $apphostIdentical) { 'BLOCKED' }
  elseif ($missingBusiness.Count -gt 0 -or $missingHcRuntime.Count -gt 0) { 'UNVERIFIABLE' }
  elseif ($businessHashes.Count -ne 1 -or $manifestHashes.Count -ne 1 -or $inconsistent.Count -gt 0 -or $hcHashes.Count -ne 1) { 'BLOCKED' }
  else { 'PASS' }

# Does the package carry the build that was just made? Recorded as its own fact so
# the answer never depends on substituting a payload from somewhere else.
$buildNativeSha256 = ''
if (Test-Path -LiteralPath $BuildNativeExe -PathType Leaf) { $buildNativeSha256 = Get-Sha256 $BuildNativeExe }
$packageMatchesBuild = $false
if ($packageRecords.Count -eq $packagePaths.Count -and $buildNativeSha256) {
  $packageMatchesBuild = (@($packageRecords | ForEach-Object { $_.sha256 } | Select-Object -Unique) -join ',') -eq $buildNativeSha256
}

# ---------------------------------------------------------------------------
# 920-v1.2 section 14.4 W3: staged admission verdicts.
# The all-locations-identical `status` above stays as a DIAGNOSTIC and must not
# be treated as a universal admission gate. Each stage is judged separately, and
# each stage explicitly does NOT require what the contract lists under
# "not required" - otherwise "candidate package is correct but the device is not
# deployed" would become a circular dependency.
# ---------------------------------------------------------------------------
function Short-Hash([string]$Value) {
  if (-not $Value) { return '(none)' }
  if ($Value.Length -le 8) { return $Value.ToLower() }
  return $Value.Substring(0, 8).ToLower()
}

function Get-ZipEntryHashes([string]$ZipPath) {
  $out = @{}
  if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) { return $out }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
  try {
    foreach ($e in $archive.Entries) {
      $name = $e.FullName.Replace('\', '/').TrimStart('/')
      if (-not $name -or $name.EndsWith('/')) { continue }
      $sha = [Security.Cryptography.SHA256]::Create()
      try {
        $s = $e.Open()
        try { $out[$name] = ([BitConverter]::ToString($sha.ComputeHash($s))).Replace('-', '') } finally { $s.Dispose() }
      } finally { $sha.Dispose() }
    }
  } finally { $archive.Dispose() }
  return $out
}

# CandidateReady: the approved source/config/rules are bound to the actual build,
# and the tests point at the same candidate. Does NOT require the local install to
# be updated.
$credentialPath = Join-Path $WorkspaceRoot 'Build\Validation\opt-gate\BUILD__build.json'
$credentialOk = $false
$credentialDetail = 'build credential missing'
if (Test-Path -LiteralPath $credentialPath -PathType Leaf) {
  try {
    $cred = (Get-Content -LiteralPath $credentialPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
    $credentialOk = ([string]$cred.validationStatus -eq 'PASS') -and [bool]$cred.rulesSha256 -and [bool]$cred.sourceTreeSha256
    $credentialDetail = 'validationStatus=' + [string]$cred.validationStatus + ' rulesSha256=' + (Short-Hash ([string]$cred.rulesSha256))
  } catch { $credentialDetail = 'build credential unreadable' }
}
$candidateReady = $credentialOk -and ($buildNativeSha256 -ne '')
$candidateReason = if ($candidateReady) { 'approved source/rules bound to a PASS build credential and the build artifact exists' } else { 'build credential not a PASS credential, or build artifact missing (' + $credentialDetail + ')' }

# FullTestPackageReady: the ACTUAL zip content (not the possibly-stale staging
# folder) must carry the approved native + Host DLL + payload manifest + HC
# runtime. 2026-09-23: the single complete package IS the updater package, so
# this now describes the only package there is.
$zipPath = Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip'
$zipEntries = Get-ZipEntryHashes $zipPath
$approvedDll = ''
$approvedManifest = ''
$approvedHc = ''
if ($packageRecords.Count -gt 0) {
  $approvedDll = [string]$packageRecords[0].hostDllSha256
  $approvedManifest = [string]$packageRecords[0].hostPayloadManifestSha256
  $approvedHc = [string]$packageRecords[0].hcAssemblySha256
}
$zipDll = [string]$zipEntries['PowerControl/fan-host-v2/YeManFanHost.dll']
$zipManifest = [string]$zipEntries['PowerControl/fan-host-v2/YeManFanHost.payload.json']
$zipNative = [string]$zipEntries['YeManCC/YeManCC.exe']
$zipHcKeys = @($zipEntries.Keys | Where-Object { $_ -match '^PowerControl/handheldcompanion-runtime/.+/HandheldCompanion\.dll$' })
$zipHcCount = $zipHcKeys.Count
# Section 14.4 requires detecting a MISSING *or WRONG* HC runtime, so the zip's HC
# hash must match the approved runtime - presence alone is not enough.
$zipHcMatches = ($zipHcCount -ge 1) -and ($approvedHc -ne '') -and
  (@($zipHcKeys | ForEach-Object { $zipEntries[$_] }) -contains $approvedHc)
$fullPackageReady = ($zipEntries.Count -gt 0) -and ($zipDll -ne '') -and ($zipManifest -ne '') -and
  ($zipDll -eq $approvedDll) -and ($zipManifest -eq $approvedManifest) -and $zipHcMatches -and
  ($buildNativeSha256 -ne '') -and ($zipNative -eq $buildNativeSha256)
$fullPackageReason = if ($fullPackageReady) { 'zip carries the approved native + Host DLL + manifest + HC runtime' } else {
  'zip=' + (Split-Path $zipPath -Leaf) + ' entries=' + $zipEntries.Count + ' dll=' + (Short-Hash $zipDll) + ' manifest=' + (Short-Hash $zipManifest) + ' native=' + (Short-Hash $zipNative) + ' hcEntries=' + $zipHcCount + ' hcMatches=' + $zipHcMatches
}

# Public publishing and real-device verification are separate from local archive readiness.
# The one canonical YeManCC.zip now preserves rules.fanHost and replaces rules.fanHostV2.
# Local production-script migration results are recorded by the Release task; this read-only
# export identity audit does not authenticate a remote channel or simulate device readiness.
$updateChannelReady = $false
$updateChannelReason = 'Local V2 package: legacy fan-host preserved, fan-host-v2 delivered. Remote channel publication and real-device upgrade remain separate unauthorized/unverified gates; see Release migration evidence.'

# DeviceIdentityVerified: the target device's ACTUAL session identity must match
# the tested candidate. Same path on the dev machine does NOT count.
$deviceIdentityVerified = $false
$deviceIdentityReason = 'WAITING_DEVICE: no handheld session identity is available from this machine; the dev machine install path must never be treated as the same instance.'

# OpenXInputProxyReady (2026-09-23): every delivery location that exists must expose the locked
# proxy next to its executable. Does NOT require the local install to be refreshed, only that a
# location that is present is complete - the same rule the other stages follow.
$openXInputProxyReady = $openXInputProxyIdentical -and ($missingOpenXInputProxy.Count -eq 0)
$openXInputProxyReason = if ($openXInputProxyReady) {
  'locked proxy present and identical at all ' + $existing.Count + ' location(s) (' + (Short-Hash $openXInputProxyLockedSha256) + ')'
} else {
  'missing at ' + $missingOpenXInputProxy.Count + ' of ' + $existing.Count + ' location(s): ' +
    (@($missingOpenXInputProxy | ForEach-Object { $_.path }) -join ', ')
}

$stagedVerdicts = [ordered]@{
  CandidateReady = [ordered]@{ ready = $candidateReady; reason = $candidateReason }
  FullTestPackageReady = [ordered]@{ ready = $fullPackageReady; reason = $fullPackageReason }
  UpdateChannelReady = [ordered]@{ ready = $updateChannelReady; reason = $updateChannelReason }
  DeviceIdentityVerified = [ordered]@{ ready = $deviceIdentityVerified; reason = $deviceIdentityReason }
  OpenXInputProxyReady = [ordered]@{ ready = $openXInputProxyReady; reason = $openXInputProxyReason }
}

$evidence = [ordered]@{
  evidenceId = 'MAINLINE-EXPORT-VERIFICATION-20260903'
  status = $status
  artifact = 'YeManCC.exe'
  identityScope = 'apphost-exe + host-business-dll + host-payload-manifest + hc-runtime-assembly'
  apphostIdentical = $apphostIdentical
  hostDllDistinctHashes = $businessHashes.Count
  hostPayloadManifestDistinctHashes = $manifestHashes.Count
  hcAssemblyDistinctHashes = $hcHashes.Count
  locationsMissingBusinessIdentity = @($missingBusiness | ForEach-Object { $_.path })
  locationsWithManifestMismatch = @($inconsistent | ForEach-Object { $_.path })
  locationsMissingHcRuntime = @($missingHcRuntime | ForEach-Object { $_.path })
  locationsMissingOpenXInputProxy = @($missingOpenXInputProxy | ForEach-Object { $_.path })
  openXInputProxyLockedSha256 = $openXInputProxyLockedSha256
  packageLocationsConsistent = $packageConsistent
  buildArtifactExe = $BuildNativeExe
  buildArtifactSha256 = $buildNativeSha256
  packageMatchesBuild = $packageMatchesBuild
  stagedVerdicts = $stagedVerdicts
  records = $records
  identity = if ($status -eq 'PASS') {
    [ordered]@{
      apphostSha256 = $hashes[0]
      length = $lengths[0]
      hostDllSha256 = $businessHashes[0]
      hostPayloadManifestSha256 = $manifestHashes[0]
      hcAssemblySha256 = $hcHashes[0]
    }
  } else { $null }
  mutation = 'read-only'
}

$parent = Split-Path -Parent $EvidencePath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $EvidencePath -Encoding UTF8
Write-Output "YeManCC EXPORT VERIFICATION: $status"
Write-Output "Evidence: $EvidencePath"
# Staged admission verdicts (920-v1.2 section 14.4 W3). Printed regardless of the
# location-consistency diagnostic above, because that diagnostic is NOT the
# universal admission gate.
foreach ($k in $stagedVerdicts.Keys) {
  $v = $stagedVerdicts[$k]
  Write-Output ("STAGE " + $k + "=" + $(if ($v.ready) { 'READY' } else { 'NOT_READY' }) + " :: " + $v.reason)
}
if ($status -ne 'PASS') {
  Write-Output ("apphostIdentical=" + $apphostIdentical +
    " hostDllHashes=" + $businessHashes.Count +
    " manifestHashes=" + $manifestHashes.Count +
    " hcHashes=" + $hcHashes.Count +
    " missingBusiness=" + $missingBusiness.Count +
    " missingHcRuntime=" + $missingHcRuntime.Count +
    " manifestMismatch=" + $inconsistent.Count)
  exit 1
}
