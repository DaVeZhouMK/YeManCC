[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [Parameter(Mandatory=$true)][string]$NativeExe,
  [string]$Zip = '',
  [string]$ReceiptPath = ''
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
if (-not $WorkspaceRoot) { $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..')) }
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
$build = Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
$receipt = if ($Zip) { Assert-SteamSleepArchiveExport $ProjectRoot $Zip $NativeExe }
           else { Assert-SteamSleepNativeExport $ProjectRoot $NativeExe }
if ($receipt.status -ne 'passed') { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: select the current mainline project' }
$nativeRecord = @($build.payload | Where-Object { $_.path -eq 'YeManCC/YeManCC.exe' })
if ($nativeRecord.Count -ne 1 -or $receipt.nativeSha256 -ne $nativeRecord[0].sha256) { throw 'EXPORT_STEAM_SLEEP_CANDIDATE_NOT_CURRENT_BUILD' }
Assert-SteamSleepExportReceipt $build.steamSleepRecovery $receipt
$receipt['buildId'] = $build.buildId
$receipt['sourceTreeSha256'] = $build.sourceTreeSha256
$receipt['verification'] = 'source-build-and-artifact-identity'
$json = $receipt | ConvertTo-Json -Depth 6
if ($ReceiptPath) {
  $parent = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ReceiptPath))
  New-Item -ItemType Directory -Force -Path $parent | Out-Null
  [IO.File]::WriteAllText($ReceiptPath, $json, [Text.UTF8Encoding]::new($false))
}
Write-Output $json
