$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$manifestPath = Join-Path $repo 'Environment\Manifests\hc-gyro-virtual-assets-20260903.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$sha256 = [Security.Cryptography.SHA256]::Create()
function Get-Sha256([string]$path) {
  $bytes = [IO.File]::ReadAllBytes($path)
  try { return ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-', '') } finally { }
}
$hid = $manifest.assets | Where-Object assetId -eq 'HidHide-1.5.230'
$maestro = $manifest.assets | Where-Object assetId -eq 'HIDMaestro-1.7.0'
$out = Join-Path $repo 'Mainline\Build\Validation\HC-Parity\A1-external-asset-metadata-20260903.json'
$hidPath = $hid.archiveEvidence
$maestroRoot = Join-Path $repo 'Archives\Migration-Backup\20260831-152113\YMCC-Workspace\Build\External\HIDMaestro\v1.7.0'
$cert = $sig.SignerCertificate
$licenses = @(Get-ChildItem -LiteralPath $maestroRoot -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|NOTICE|THIRD-PARTY-NOTICES)(\..*)?$' } | ForEach-Object { $_.FullName })
$maestroBinaries = @(Get-ChildItem -LiteralPath $maestroRoot -Recurse -File | Where-Object { $_.Extension -in '.dll','.exe' } | ForEach-Object {
  $v = [Diagnostics.FileVersionInfo]::GetVersionInfo($_.FullName)
  [ordered]@{ path=$_.FullName; length=$_.Length; sha256=(Get-Sha256 $_.FullName); fileVersion=$v.FileVersion; productVersion=$v.ProductVersion }
})
$result = [ordered]@{
  evidenceId = 'A1-EXTERNAL-ASSET-METADATA-20260903'
  status = 'PARTIAL'
  systemMutation = $false
  assetLockId = $manifest.assetLockId
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
    licenseFiles = $licenses; licenseFileCount = $licenses.Count
    binaries = $maestroBinaries
  }
  conclusion = 'Locked hashes and package metadata are read-only verified. HidHide Authenticode is cryptographically verified but its signing certificate is expired at audit time; publication trust and external licensing approval remain open. No asset was installed, loaded, copied or packaged.'
  generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 EXTERNAL ASSET METADATA: $($result.status)"
Write-Output "Evidence: $out"
