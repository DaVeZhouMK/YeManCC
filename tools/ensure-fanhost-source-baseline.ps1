#Requires -Version 5.1
<#
  Keep the FanLab real-host source baseline in step with the tree - at export time.

  Why the exporter owns this
  --------------------------
  tools/verify-fanhost-source-baseline.ps1 is a read-only, fail-closed check, and
  `pnpm run test:fanhost-source-baseline` runs it from outside the export chain. So every approved Fan
  batch that touches FanLab/real-host sources left that check red until a human re-recorded the
  baseline - that is how the 2026-09-02 snapshot silently went stale (09-21 was its last re-record,
  while two adjudicated batches changed Program.cs / Section18..19 afterwards).

  This guard closes that loop with no hand-copied hashes:
    1) preview (read-only) with tools/rebaseline-fanhost-source-baseline.ps1 -DryRun;
    2) if the recorded baseline is already in step -> nothing is written and the read-only verifier
       is run as the cross-check;
    3) otherwise re-record from the tree (the tool recomputes every value, prints the added/removed/
       changed delta, keeps all previous values nested inside the baseline, and writes a .bak), then
       run the read-only verifier - a failure here aborts the caller (export chain).
  The delta is printed loudly on purpose: an automatic re-record must never hide a source change.

  Usage
  -----
    pwsh -NoProfile -File tools/ensure-fanhost-source-baseline.ps1            # used by the exporters
    pwsh -NoProfile -File tools/ensure-fanhost-source-baseline.ps1 -DryRun    # say what it would do
#>
[CmdletBinding()]
param(
  [string]$FanLabRoot = '',
  [string]$ToolRoot = '',
  [string]$ApprovedBy = 'package-release.ps1 / build-workspace.ps1 (export chain, batch export)',
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = $PSScriptRoot }
if ([string]::IsNullOrWhiteSpace($FanLabRoot)) { $FanLabRoot = Join-Path $ToolRoot '..\FanLab' }
if (!(Test-Path -LiteralPath $FanLabRoot -PathType Container)) { throw "FanLab root missing: $FanLabRoot" }

$rebaselineTool = Join-Path $ToolRoot 'rebaseline-fanhost-source-baseline.ps1'
$verifierTool = Join-Path $ToolRoot 'verify-fanhost-source-baseline.ps1'
foreach ($tool in @($rebaselineTool, $verifierTool)) {
  if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "FanHost source-baseline tool missing: $tool" }
}

# 1) read-only cross-check first: it is the authority on "is the recorded baseline in step with the
#    tree?". It also covers the case where the tree matches the index but the baseline JSON fields
#    (index sha / core hashes) drifted, which a delta-only preview would not see.
$verifierFailure = ''
$inStep = $false
try {
  & $verifierTool -FanLabRoot $FanLabRoot
  $inStep = $true
} catch {
  $verifierFailure = $_.Exception.Message
}

if ($inStep) {
  Write-Output 'FANLAB_SOURCE_BASELINE_UP_TO_DATE: recorded baseline already matches the tree (nothing written).'
  return
}

Write-Output "FANLAB_SOURCE_BASELINE_STALE: $verifierFailure"

# 2) read-only preview so the re-record is loud (added/removed/changed + old -> new values)
$preview = @(& $rebaselineTool -FanLabRoot $FanLabRoot -DryRun 2>&1 | ForEach-Object { [string]$_ })
$preview | Where-Object { $_ -match '^(index sha256|core |delta |  [-+~])' } | ForEach-Object { Write-Output "  $_" }
$deltaLine = @($preview | Where-Object { $_ -match '^delta\s+:' }) | Select-Object -Last 1
$deltaSummary = ''
$deltaCounts = @(0, 0, 0)
if (![string]::IsNullOrWhiteSpace($deltaLine)) {
  $deltaSummary = ($preview | Where-Object { $_ -match '^  [-+~] ' } | ForEach-Object { $_.Trim() }) -join ' '
  $deltaMatch = [regex]::Match($deltaLine, 'added=(\d+)\s+removed=(\d+)\s+changed=(\d+)')
  if ($deltaMatch.Success) { $deltaCounts = @([int]$deltaMatch.Groups[1].Value, [int]$deltaMatch.Groups[2].Value, [int]$deltaMatch.Groups[3].Value) }
}

if ($DryRun) {
  Write-Output 'DRY_RUN: would re-record the baseline from the tree.'
  return
}

$reason = ("export chain re-record at export time: the read-only verifier reported '$verifierFailure' " +
  "(tree delta added={0} removed={1} changed={2}). Recomputed from the tree by " +
  "tools/rebaseline-fanhost-source-baseline.ps1; previous values are kept nested inside the baseline. delta: {3}" -f `
  $deltaCounts[0], $deltaCounts[1], $deltaCounts[2], $deltaSummary)

& $rebaselineTool -FanLabRoot $FanLabRoot -ApprovedBy $ApprovedBy -Reason $reason

# 3) cross-check with the read-only verifier (throws on any mismatch)
& $verifierTool -FanLabRoot $FanLabRoot
Write-Output 'FANLAB_SOURCE_BASELINE_UPDATED: re-recorded from the tree and re-verified.'
