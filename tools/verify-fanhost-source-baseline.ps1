[CmdletBinding()]
param(
  [string]$FanLabRoot = ''
)

$ErrorActionPreference = 'Stop'

function Get-Sha256 {
  param([string]$Path)
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToUpperInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

if ([string]::IsNullOrWhiteSpace($FanLabRoot)) {
  $FanLabRoot = Join-Path (Split-Path -Parent $PSCommandPath) '..\FanLab'
}
$root = [IO.Path]::GetFullPath($FanLabRoot)
$baselinePath = Join-Path $root 'FANHOST-SOURCE-BASELINE.json'
if (!(Test-Path -LiteralPath $baselinePath -PathType Leaf)) { throw "FanHost source baseline missing: $baselinePath" }
$baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$baseline.schemaVersion -ne 1) { throw 'Unsupported FanHost source baseline schema.' }

$indexPath = Join-Path $root ([string]$baseline.sourceFilesIndex)
if (!(Test-Path -LiteralPath $indexPath -PathType Leaf)) { throw "FanHost source index missing: $indexPath" }
if ((Get-Sha256 $indexPath) -ine [string]$baseline.sourceFilesIndexSha256) {
  throw 'FanHost source index SHA-256 mismatch.'
}

$source = Join-Path $root ([string]$baseline.sourceDirectory)
$expected = @{}
foreach ($line in @(Get-Content -LiteralPath $indexPath -Encoding UTF8)) {
  if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw "Invalid FanHost source index entry: $line" }
  $relative = $Matches[2]
  if ($expected.ContainsKey($relative)) { throw "Duplicate FanHost source index path: $relative" }
  $expected[$relative] = $Matches[1].ToUpperInvariant()
}

$actual = @{}
Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {
  $_.FullName -notmatch '\\(bin|obj|output|\.git)(\\|$)'
} | ForEach-Object {
  $relative = $_.FullName.Substring($source.Length + 1).Replace('\', '/')
  $actual[$relative] = Get-Sha256 $_.FullName
}
if ($actual.Count -ne [int]$baseline.sourceFileCount) { throw "FanHost source file count mismatch: $($actual.Count)" }
foreach ($relative in $expected.Keys) {
  if (!$actual.ContainsKey($relative)) { throw "FanHost source file missing: $relative" }
  if ($actual[$relative] -ne $expected[$relative]) { throw "FanHost source hash mismatch: $relative" }
}

$baseline.coreFiles.PSObject.Properties | ForEach-Object {
  $path = Join-Path $source $_.Name.Replace('/', '\\')
  if ((Get-Sha256 $path) -ine [string]$_.Value) { throw "FanHost core source hash mismatch: $($_.Name)" }
}

$omitted = $baseline.omittedArtifacts
$omittedIndexPath = Join-Path $root ([string]$omitted.index)
if (!(Test-Path -LiteralPath $omittedIndexPath -PathType Leaf)) { throw "FanHost omitted-artifact index missing: $omittedIndexPath" }
if ((Get-Sha256 $omittedIndexPath) -ine [string]$omitted.indexSha256) {
  throw 'FanHost omitted-artifact index SHA-256 mismatch.'
}
$omittedCount = @(Get-Content -LiteralPath $omittedIndexPath -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count
if ($omittedCount -ne [int]$omitted.fileCount) { throw 'FanHost omitted-artifact index count mismatch.' }

Write-Output "FANHOST_SOURCE_BASELINE_OK: $($baseline.baselineId) files=$($actual.Count)"
