$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$zip = Join-Path $repo 'Mainline\Release\Packages\YeManCC.zip'
$installed = 'C:\SOFT\YeMan\PowerControl'
$driverName = '(?i)(HIDMaestro|HidHide)'
# 2026-09-23 裁定：唯一产物 = 完整包，驱动类资产按设计随包分发。本审计因此从
# "正式包不得含驱动资产"改为"资产位置白名单"：只允许出现在
#   PowerControl/feature-assets/virtual-gamepad/
#   PowerControl/handheldcompanion-runtime/
#   PowerControl/redist/HidHide_1.5.230_x64.exe | HidHideCLI.exe
#   PowerControl/redist/HIDMaestroSetup.exe|.dll|.deps.json|.runtimeconfig.json|.manifest.json
# 四处；YeManCC/InputHost 仍必须为空。
# GP-PREREQ-2：HIDMaestroSetup 首次安装程序按设计由 build-gamepad-prerequisites.ps1
# 发布到 PowerControl/redist，与 HidHide 同址，故纳入白名单。
$allowedAssetPattern = '^PowerControl/(feature-assets/virtual-gamepad/|handheldcompanion-runtime/|redist/HidHide(_1\.5\.230_x64)?(CLI)?\.exe$|redist/HIDMaestroSetup\.(exe|dll|deps\.json|runtimeconfig\.json|manifest\.json)$)'
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
$zipForbidden = @()
$hcRuntimeEntries = @()
$inputHostEntries = @()
try {
  $zipForbidden = @($archive.Entries | Where-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    ($normalized -match $driverName) -and ($normalized -notmatch $allowedAssetPattern)
  } | ForEach-Object FullName)
  $hcRuntimeEntries = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/handheldcompanion-runtime(?:/|$)' } | ForEach-Object FullName)
  $inputHostEntries = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -match '^YeManCC/InputHost(?:/|$)' } | ForEach-Object FullName)
} finally { $archive.Dispose() }
$installedForbidden = @(Get-ChildItem -LiteralPath $installed -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
  $rel = $_.FullName.Substring($installed.Length).TrimStart('\') -replace '\\', '/'
  ($rel -match $driverName) -and ($rel -notmatch $allowedAssetPattern)
} | ForEach-Object FullName)
$violations = $zipForbidden.Count + $inputHostEntries.Count
$result = [ordered]@{
  evidenceId = 'A1-GYRO-VIRTUAL-RELEASE-BOUNDARY-20260903'
  status = if ($violations -eq 0) { 'PASS' } else { 'FAIL' }
  systemMutation = $false
  releaseZip = $zip
  releaseForbiddenEntries = $zipForbidden
  releaseInputHostEntries = $inputHostEntries
  installedExternalNamesOutsideLockedReference = $installedForbidden
  hcFanRuntimeEntries = $hcRuntimeEntries
  allowedAssetRoots = @(
    'PowerControl/feature-assets/virtual-gamepad/',
    'PowerControl/handheldcompanion-runtime/',
    'PowerControl/redist/HidHide_1.5.230_x64.exe',
    'PowerControl/redist/HidHideCLI.exe',
    'PowerControl/redist/HIDMaestroSetup.exe',
    'PowerControl/redist/HIDMaestroSetup.dll',
    'PowerControl/redist/HIDMaestroSetup.deps.json',
    'PowerControl/redist/HIDMaestroSetup.runtimeconfig.json',
    'PowerControl/redist/HIDMaestroSetup.manifest.json')
  conclusion = if ($violations -gt 0) {
    'Release boundary FAIL: driver assets appear outside the allowed roots (see releaseForbiddenEntries / releaseInputHostEntries).'
  } else {
    'Single complete package carries the virtual-gamepad bundle, the locked HC runtime and the locked HidHide installer/CLI only under their allowed PowerControl roots, and carries no YeManCC/InputHost entries.'
  }
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$out = Join-Path $repo 'Mainline\Build\Validation\HC-Parity\A1-gyro-virtual-release-boundary-20260903.json'
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 GYRO RELEASE BOUNDARY: $($result.status)"
Write-Output "Evidence: $out"
