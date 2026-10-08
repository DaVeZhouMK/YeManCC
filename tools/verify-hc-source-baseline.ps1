[CmdletBinding()]
param(
  [string]$RuntimeRoot = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
  $RuntimeRoot = Join-Path (Split-Path -Parent $PSCommandPath) '..\deps\handheldcompanion-runtime'
}

function Get-NormalizedRelativePath {
  param([string]$Root, [string]$Path)
  return $Path.Substring($Root.Length).TrimStart('\', '/').Replace('\', '/')
}

function Get-Sha256 {
  param([string]$Path)
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToUpperInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

$root = [IO.Path]::GetFullPath($RuntimeRoot)
$baselinePath = Join-Path $root 'HC-BASELINE.json'
$indexPath = Join-Path $root 'source-files.sha256'
if (!(Test-Path -LiteralPath $baselinePath -PathType Leaf)) { throw "HC baseline missing: $baselinePath" }
if (!(Test-Path -LiteralPath $indexPath -PathType Leaf)) { throw "HC source index missing: $indexPath" }

$baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$baseline.schemaVersion -ne 1) { throw 'Unsupported HC baseline schema.' }
$indexHash = Get-Sha256 $indexPath
if ($indexHash -ine [string]$baseline.sourceFilesIndexSha256) { throw 'HC source index SHA-256 mismatch.' }

$source = Join-Path $root ([string]$baseline.sourceDirectory)
if (!(Test-Path -LiteralPath $source -PathType Container)) { throw "HC source directory missing: $source" }

$expected = @{}
foreach ($line in @(Get-Content -LiteralPath $indexPath -Encoding UTF8)) {
  if ([string]::IsNullOrWhiteSpace($line)) { continue }
  if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw "Invalid HC source index entry: $line" }
  $relative = $Matches[2]
  if ($relative.StartsWith('/') -or $relative.Contains('..')) { throw "Unsafe HC source index path: $relative" }
  if ($expected.ContainsKey($relative)) { throw "Duplicate HC source index path: $relative" }
  $expected[$relative] = $Matches[1].ToUpperInvariant()
}

if ($expected.Count -ne [int]$baseline.sourceFileCount) { throw 'HC source index count mismatch.' }

$actual = @{}
Get-ChildItem -LiteralPath $source -Recurse -File | ForEach-Object {
  $relative = Get-NormalizedRelativePath $source $_.FullName
  if ($relative -match '(^|/)(bin|obj)(/|$)') { throw "Build cache is not allowed in HC source baseline: $relative" }
  $actual[$relative] = Get-Sha256 $_.FullName
}

if ($actual.Count -ne $expected.Count) { throw "HC source file count mismatch: expected $($expected.Count), actual $($actual.Count)." }
foreach ($relative in $expected.Keys) {
  if (!$actual.ContainsKey($relative)) { throw "HC source file missing: $relative" }
  if ($actual[$relative] -ne $expected[$relative]) { throw "HC source hash mismatch: $relative" }
}

$omitted = $baseline.omittedFromFrozenReference
if ($null -eq $omitted) { throw 'HC omitted-file record is missing.' }
$omittedIndexPath = Join-Path $root ([string]$omitted.index)
if (!(Test-Path -LiteralPath $omittedIndexPath -PathType Leaf)) { throw "HC omitted-file index missing: $omittedIndexPath" }
if ((Get-Sha256 $omittedIndexPath) -ine [string]$omitted.indexSha256) {
  throw 'HC omitted-file index SHA-256 mismatch.'
}
$omittedPaths = @{}
foreach ($line in @(Get-Content -LiteralPath $omittedIndexPath -Encoding UTF8)) {
  if ([string]::IsNullOrWhiteSpace($line)) { continue }
  if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw "Invalid HC omitted-file index entry: $line" }
  $relative = $Matches[2]
  if ($omittedPaths.ContainsKey($relative)) { throw "Duplicate HC omitted-file index path: $relative" }
  if ($actual.ContainsKey($relative)) { throw "HC file is both included and omitted: $relative" }
  $omittedPaths[$relative] = $true
}
if ($omittedPaths.Count -ne [int]$omitted.fileCount) { throw 'HC omitted-file index count mismatch.' }

$projectPath = Join-Path $source ([string]$baseline.projectFile)
if ((Get-Sha256 $projectPath) -ine [string]$baseline.projectFileSha256) {
  throw 'HC project file SHA-256 mismatch.'
}
$project = [xml](Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8)
$version = @($project.Project.PropertyGroup.Version | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })[0]
if ([string]$version -ne [string]$baseline.upstreamProjectVersion) { throw "HC project version mismatch: $version" }

$baseline.coreFiles.PSObject.Properties | ForEach-Object {
  $path = Join-Path $source $_.Name.Replace('/', '\\')
  if ((Get-Sha256 $path) -ine [string]$_.Value) { throw "HC core file SHA-256 mismatch: $($_.Name)" }
}

Write-Output "HC_SOURCE_BASELINE_OK: $($baseline.baselineId) files=$($actual.Count) version=$version"
