[CmdletBinding()]
param(
  [string]$Dll = 'deps/hidmaestro/HIDMaestro.Core.dll',
  [string]$Output = '',
  [switch]$LegacyA1
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$manifestPath = Join-Path $repo 'Environment\Manifests\hc-gyro-virtual-assets-20260903.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$sha256 = [Security.Cryptography.SHA256]::Create()
function Get-Sha256([string]$path) {
  $bytes = [IO.File]::ReadAllBytes($path)
  try { return ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-', '') } finally { }
}
$hid = $manifest.assets | Where-Object assetId -eq 'HidHide-1.5.230'
# The external manifest still owns HidHide and the explicit historical A1 replay.
$maestro = $null
if ($LegacyA1) {
  $maestro = $manifest.assets | Where-Object assetId -eq 'HIDMaestro-1.7.0'
  if (-not $PSBoundParameters.ContainsKey('Dll')) {
    $Dll = Join-Path $repo 'Archives\Migration-Backup\20260831-152113\YMCC-Workspace\Build\External\HIDMaestro\v1.7.0\HIDMaestro.Core.dll'
  }
}
if (-not $Output) {
  $Output = if ($LegacyA1) { '../../Build/Validation/HC-Parity/A1-external-asset-metadata-20260903.json' }
    else { '../../Build/Validation/HC-Parity/external-asset-metadata.json' }
}
$dllInput = if ([IO.Path]::IsPathRooted($Dll)) { $Dll } else { Join-Path $projectRoot $Dll }
$dllPath = (Resolve-Path -LiteralPath $dllInput).Path
$maestroRoot = Split-Path -Parent $dllPath
$assembly = [Reflection.AssemblyName]::GetAssemblyName($dllPath)
$bytes = (Get-Item -LiteralPath $dllPath).Length
$hash = Get-Sha256 $dllPath
$dependencyLock = $null
if ($dllPath -ieq [IO.Path]::GetFullPath((Join-Path $projectRoot 'deps/hidmaestro/HIDMaestro.Core.dll'))) {
  $dependencyLock = 'deps/hidmaestro/dependency-lock.json'
  $lock = Get-Content -Raw -LiteralPath (Join-Path $projectRoot $dependencyLock) | ConvertFrom-Json
  $core = @($lock.files | Where-Object { $_.path -eq 'deps/hidmaestro/HIDMaestro.Core.dll' })
  if ($core.Count -ne 1 -or $bytes -ne [long]$core[0].bytes -or $hash -ine $core[0].sha256) {
    throw 'Mainline HIDMaestro dependency lock mismatch'
  }
}
if ($LegacyA1) {
  if ($assembly.Version.ToString(3) -ne $maestro.version) { throw 'Legacy A1 DLL version differs from the historical asset manifest' }
} else {
  $maestro = [pscustomobject]@{ assetId = 'HIDMaestro-' + $assembly.Version.ToString(3); archiveEvidence = $null }
}
$out = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $projectRoot $Output }
$hidPath = $hid.archiveEvidence
$cert = $sig.SignerCertificate
$licenses = @(Get-ChildItem -LiteralPath $maestroRoot -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|NOTICE|THIRD-PARTY-NOTICES)(\..*)?$' } | ForEach-Object { $_.FullName })
$binaryFiles = if ($LegacyA1) {
  Get-ChildItem -LiteralPath $maestroRoot -Recurse -File | Where-Object { $_.Extension -in '.dll','.exe' }
} else { Get-Item -LiteralPath $dllPath }
$maestroBinaries = @($binaryFiles | ForEach-Object {
  $v = [Diagnostics.FileVersionInfo]::GetVersionInfo($_.FullName)
  [ordered]@{ path=$_.FullName; length=$_.Length; sha256=(Get-Sha256 $_.FullName); fileVersion=$v.FileVersion; productVersion=$v.ProductVersion }
})
$result = [ordered]@{
  evidenceId = $(if ($LegacyA1) { 'A1-EXTERNAL-ASSET-METADATA-20260903' } else { 'EXTERNAL-ASSET-METADATA' })
  status = 'PARTIAL'
  systemMutation = $false
  assetLockId = $manifest.assetLockId
  assetLockScope = 'Historical external manifest: HidHide, plus HIDMaestro archive only in LegacyA1 mode; current HIDMaestro uses dependencyLock.'
  hidHide = [ordered]@{
    assetId = $hid.assetId; path = $hidPath; lockedSha256 = $hid.sha256
    observedSha256 = (Get-Sha256 $hidPath)
    hashMatches = ((Get-Sha256 $hidPath) -eq $hid.sha256)
    authenticodeStatus = 'previously-observed-valid'
    signatureStatusMessage = 'Read-only signature result retained from A1-HIDHIDE-READONLY-20260903; this metadata script avoids PowerShell Security module side effects.'
    signerSubject = 'Nefarius Software Solutions e.U. (previously observed)'
    signerIssuer = 'DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1 (previously observed)'
    certificateNotAfter = '2025-02-17T07:59:59+08:00'
    certificateExpiredAtAudit = $true
  }
  hidMaestro = [ordered]@{
    assetId = $maestro.assetId; root = $maestroRoot; archiveEvidence = $maestro.archiveEvidence
    selectedDll = $dllPath; assembly = $assembly.FullName; dependencyLock = $dependencyLock
    dependencyLockMatches = $(if ($dependencyLock) { $true } else { $null })
    licenseFiles = $licenses; licenseFileCount = $licenses.Count
    binaries = $maestroBinaries
  }
  conclusion = 'Selected HIDMaestro metadata and the historical HidHide asset lock were checked read-only. HidHide signature/certificate observations remain historical; publication trust and external licensing approval remain open. No asset was installed, loaded, copied or packaged.'
  generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
}
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
$label = if ($LegacyA1) { 'A1 EXTERNAL ASSET METADATA' } else { 'EXTERNAL ASSET METADATA' }
Write-Output "${label}: $($result.status)"
Write-Output "Evidence: $out"
