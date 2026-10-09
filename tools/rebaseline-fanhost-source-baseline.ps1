#Requires -Version 5.1
<#
  Rebaseline the FanLab real-host source index (FANHOST-SOURCE-BASELINE.json + fanhost-source-files.sha256).

  Why this tool exists
  --------------------
  tools/verify-fanhost-source-baseline.ps1 is a read-only, fail-closed verifier: it re-derives the
  FanLab/real-host tree index and throws on any difference from the recorded baseline. That is the
  wanted behaviour, but it also means every *approved* Fan batch that touches real-host sources makes
  the check fail until the baseline is deliberately re-recorded. Doing that by hand (copy/paste 64-hex
  values) is how the 2026-09-02 snapshot silently went stale.

  This tool re-derives every value from the tree, audits the delta against the previous index, refuses
  to hand-edit hashes, keeps the previous values inside the file, and writes the index + JSON back with
  a read-back verification. Use -DryRun first; it never writes.

  It does NOT decide whether a source change is allowed - it only records what is on disk, with the
  caller-supplied authority/reason text. Approved-by text must be given for an apply run.

  Usage
  -----
    pwsh -NoProfile -File tools/rebaseline-fanhost-source-baseline.ps1 -DryRun
    pwsh -NoProfile -File tools/rebaseline-fanhost-source-baseline.ps1 `
         -ApprovedBy '<decision document / section>' -Reason '<what changed and why>'
#>
[CmdletBinding()]
param(
  [string]$FanLabRoot = '',
  [string]$ApprovedBy = '',
  [string]$Reason = '',
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Get-Sha256Text {
  param([string]$Text)
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToUpperInvariant()
  } finally { $sha.Dispose() }
}

function Get-Sha256File {
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
$baselineText = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8
$baseline = $baselineText | ConvertFrom-Json
if ([int]$baseline.schemaVersion -ne 1) { throw 'Unsupported FanHost source baseline schema.' }

$indexName = [string]$baseline.sourceFilesIndex
if ([string]::IsNullOrWhiteSpace($indexName)) { throw 'Baseline has no sourceFilesIndex.' }
$indexPath = Join-Path $root $indexName
$source = Join-Path $root ([string]$baseline.sourceDirectory)
if (!(Test-Path -LiteralPath $source -PathType Container)) { throw "FanHost source directory missing: $source" }

# --- previous index (recorded) -------------------------------------------------
$previous = [ordered]@{}
if (Test-Path -LiteralPath $indexPath -PathType Leaf) {
  foreach ($line in @(Get-Content -LiteralPath $indexPath -Encoding UTF8)) {
    if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw "Invalid existing source index entry: $line" }
    $previous[$Matches[2]] = $Matches[1].ToUpperInvariant()
  }
}
$previousIndexSha = if (Test-Path -LiteralPath $indexPath -PathType Leaf) { Get-Sha256File $indexPath } else { '' }

# --- current tree (same exclusion + relative-path rules as the verifier) -------
$current = [ordered]@{}
$actualFiles = @{}
Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {
  $_.FullName -notmatch '\\(bin|obj|output|\.git)(\\|$)'
} | ForEach-Object {
  $relative = $_.FullName.Substring($source.Length + 1).Replace('\', '/')
  if ($actualFiles.ContainsKey($relative)) { throw "Duplicate source path in tree: $relative" }
  $actualFiles[$relative] = $_.FullName
}
foreach ($relative in @($actualFiles.Keys | Sort-Object -CaseSensitive)) {
  $current[$relative] = Get-Sha256File $actualFiles[$relative]
}
if ($current.Count -lt 1) { throw 'Refusing to rebaseline an empty source index.' }

$indexText = (($current.Keys | ForEach-Object { "$($current[$_])  $_" }) -join "`n") + "`n"
$indexSha = Get-Sha256Text $indexText

# --- audit --------------------------------------------------------------------
$added = @($current.Keys | Where-Object { -not $previous.Contains($_) })
$removed = @($previous.Keys | Where-Object { -not $current.Contains($_) })
$changed = @($current.Keys | Where-Object { $previous.Contains($_) -and $previous[$_] -ne $current[$_] })

$coreFiles = [ordered]@{}
foreach ($p in $baseline.coreFiles.PSObject.Properties) { $coreFiles[$p.Name] = [string]$p.Value }
$coreChanged = @()
foreach ($name in @($coreFiles.Keys)) {
  $path = Join-Path $source $name.Replace('/', '\')
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Baseline core file missing from tree: $name" }
  $now = Get-Sha256File $path
  if ($coreFiles[$name] -ne $now) { $coreChanged += $name }
  $coreFiles[$name] = $now
}

Write-Output "== fanhost-source-baseline rebaseline =="
Write-Output "root            : $root"
Write-Output "baselineId      : $($baseline.baselineId)"
Write-Output "index file      : $indexName"
Write-Output "files           : previous=$($previous.Count) current=$($current.Count)"
Write-Output "index sha256    : $previousIndexSha"
Write-Output "                  -> $indexSha"
foreach ($p in $baseline.coreFiles.PSObject.Properties) {
  Write-Output ("core {0,-22}: {1}`n                  -> {2}" -f $p.Name, [string]$p.Value, $coreFiles[$p.Name])
}
Write-Output "delta           : added=$($added.Count) removed=$($removed.Count) changed=$($changed.Count)"
foreach ($f in $added) { Write-Output "  + $f" }
foreach ($f in $removed) { Write-Output "  - $f" }
foreach ($f in $changed) { Write-Output "  ~ $f" }

if ($DryRun) {
  Write-Output 'DRY_RUN: no files written.'
  # `return` (not `exit`) so the exporters can call this in-process and keep running after a preview.
  return
}

if ([string]::IsNullOrWhiteSpace($ApprovedBy) -or [string]::IsNullOrWhiteSpace($Reason)) {
  throw 'An apply run requires -ApprovedBy and -Reason (they are recorded inside the baseline).'
}
if ($added.Count -gt 0 -or $removed.Count -gt 0) {
  Write-Output "NOTE: the tree gained/lost files relative to the previous index; the file list is re-derived, previous values stay recorded."
}

# --- stage the new JSON (previous values stay in the file) --------------------
$stamp = (Get-Date).ToString('yyyy-MM-dd')
$tag = (Get-Date).ToString('yyyyMMdd-HHmmss')
$backupPath = "$baselinePath.bak-$tag"

$previousCurrent = [ordered]@{
  sourceFileCount       = $previous.Count
  sourceFilesIndexSha256 = $previousIndexSha
  coreFiles             = [ordered]@{}
}
foreach ($p in $baseline.coreFiles.PSObject.Properties) { $previousCurrent.coreFiles[$p.Name] = [string]$p.Value }

$newRebaseline = [ordered]@{
  approvedBy  = $ApprovedBy
  date        = $stamp
  previousBaselineId = [string]$baseline.baselineId
  previousRebaseline = $baseline.rebaseline
  current     = [ordered]@{
    sourceFileCount        = $current.Count
    sourceFilesIndexSha256 = $indexSha
    coreFiles              = $coreFiles
  }
  auditMethod = 'per-file hash comparison of the current FanLab/real-host tree (bin/obj/output/.git excluded) against the previous index supplied by this tool; values are computed, never hand-copied'
  auditResult = [ordered]@{
    addedFiles       = $added
    changedCoreFiles = @($coreChanged)
    removedFiles     = $removed
    changedFiles     = $changed
  }
  reason      = $Reason
  previousRecorded = [ordered]@{
    sourceFilesIndexSha256 = $previousIndexSha
    programCsSha256        = [string]$baseline.coreFiles.'Program.cs'
  }
}

$newBaseline = [ordered]@{
  schemaVersion       = [int]$baseline.schemaVersion
  baselineId          = [string]$baseline.baselineId
  role                = [string]$baseline.role
  sourceDirectory     = [string]$baseline.sourceDirectory
  sourceFileCount     = $current.Count
  sourceFilesIndex    = $indexName
  sourceFilesIndexSha256 = $indexSha
  coreFiles           = $coreFiles
  omittedArtifacts    = $baseline.omittedArtifacts
  importedFrom        = [string]$baseline.importedFrom
  hcSourceBaseline    = [string]$baseline.hcSourceBaseline
  rebaseline          = $newRebaseline
  policy              = $baseline.policy
}
$newBaselineText = ($newBaseline | ConvertTo-Json -Depth 12) + "`n"

# --- apply: backup, write index, write json, read back ------------------------
Copy-Item -LiteralPath $baselinePath -Destination $backupPath -Force
[IO.File]::WriteAllText($indexPath, $indexText, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllText($baselinePath, $newBaselineText, (New-Object Text.UTF8Encoding($false)))

$readIndexSha = Get-Sha256File $indexPath
$readBaseline = (Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json)
if ($readIndexSha -ne $indexSha) { throw 'Read-back mismatch: source index SHA-256.' }
if ([string]$readBaseline.sourceFilesIndexSha256 -ne $indexSha) { throw 'Read-back mismatch: baseline index SHA-256 field.' }
if ([int]$readBaseline.sourceFileCount -ne $current.Count) { throw 'Read-back mismatch: sourceFileCount.' }
foreach ($name in @($coreFiles.Keys)) {
  if (([string]$readBaseline.coreFiles.$name) -ne $coreFiles[$name]) { throw "Read-back mismatch: core file $name." }
}

Write-Output "APPLIED: $backupPath (backup), $indexName, FANHOST-SOURCE-BASELINE.json"
Write-Output "next: pwsh -NoProfile -File tools/verify-fanhost-source-baseline.ps1  => expect FANHOST_SOURCE_BASELINE_OK"
