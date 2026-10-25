# Generated frontend recovery bytes always come from the single payload script source.
[CmdletBinding()]
param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$source = Join-Path $ProjectRoot 'PowerControl\fan-host\install-fan-host-payload.ps1'
$target = Join-Path $ProjectRoot 'src\bridge\fanHostInstallerAsset.json'
# ConvertTo-Json differs in indentation and HTML escaping between PS 5.1 and 7.
# Use the already-required Node runtime to write canonical UTF-8/LF bytes directly;
# no stdout text transcoding, and preserve the payload's BOM inside content.
$generate = @"
const fs = require('node:fs');
const crypto = require('node:crypto');
const source = process.argv[1], target = process.argv[2];
const bytes = fs.readFileSync(source);
const hash = crypto.createHash('sha256').update(bytes).digest('hex');
const asset = { schemaVersion: 1, sha256: hash, content: bytes.toString('utf8') };
const content = JSON.stringify(asset, null, 2) + '\n';
if (!fs.existsSync(target) || fs.readFileSync(target, 'utf8') !== content) {
  fs.writeFileSync(target, content, 'utf8');
}
console.log('FAN_INSTALLER_ASSET_OK: ' + hash);
"@
& node -e $generate $source $target
if ($LASTEXITCODE -ne 0) { throw "Fan installer asset generation failed: exit=$LASTEXITCODE" }