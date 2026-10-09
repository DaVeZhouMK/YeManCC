# Generated frontend recovery bytes always come from the single payload script source.
[CmdletBinding()]
param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$source = Join-Path $ProjectRoot 'PowerControl\fan-host\install-fan-host-payload.ps1'
$target = Join-Path $ProjectRoot 'src\bridge\fanHostInstallerAsset.json'
$bytes = [IO.File]::ReadAllBytes($source)
$sha = [Security.Cryptography.SHA256]::Create()
try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }
# UTF8.GetString intentionally retains the BOM: repaired .ps1 bytes must match the manifest.
$asset = [ordered]@{ schemaVersion = 1; sha256 = $hash; content = [Text.Encoding]::UTF8.GetString($bytes) }
$content = ($asset | ConvertTo-Json -Depth 3) + [Environment]::NewLine
if (!(Test-Path -LiteralPath $target) -or [IO.File]::ReadAllText($target) -ne $content) {
  [IO.File]::WriteAllText($target, $content, (New-Object Text.UTF8Encoding($false)))
}
Write-Output "FAN_INSTALLER_ASSET_OK: $hash"
