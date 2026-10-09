$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ledgerPath = Join-Path $PSScriptRoot 'HCParityLedger.v1.json'
$ledger = Get-Content -Raw -LiteralPath $ledgerPath | ConvertFrom-Json
$out = Join-Path $repo 'Mainline\Build\Validation\HC-Parity\A1-hc-parity-ledger-audit-20260903.json'
$allowedDomains = @('controller','button','motion','route','persona','visibility','lifecycle')
$allowedClasses = @('exact','adapted','unknown')
$allowedStatuses = @('located','verified','blocked')
$rows = @()
$failures = @()
foreach ($entry in @($ledger.entries)) {
  $source = Join-Path $project ([string]$entry.sourcePath)
  $fixture = if ($entry.fixtureOrTrace) { Join-Path $project ([string]$entry.fixtureOrTrace) } else { $null }
  $sourceExists = Test-Path -LiteralPath $source -PathType Leaf
  $fixtureExists = if ($fixture) { Test-Path -LiteralPath $fixture -PathType Leaf } else { $false }
  $valid = $sourceExists -and $fixtureExists -and ($allowedDomains -contains [string]$entry.domain) -and ($allowedClasses -contains [string]$entry.parityClass) -and ($allowedStatuses -contains [string]$entry.status) -and @($entry.observedOrder).Count -gt 0
  if (-not $valid) { $failures += [string]$entry.parityId }
  $rows += [ordered]@{
    parityId = [string]$entry.parityId; domain = [string]$entry.domain; status = [string]$entry.status; parityClass = [string]$entry.parityClass
    sourcePath = $source; sourceExists = $sourceExists; fixtureOrTrace = $fixture; fixtureExists = $fixtureExists
    observedOrderCount = @($entry.observedOrder).Count; valid = $valid
  }
}
$result = [ordered]@{
  evidenceId = 'A1-HC-PARITY-LEDGER-AUDIT-20260903'
  status = if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' }
  systemMutation = $false
  ledgerPath = $ledgerPath
  ledgerRevision = $ledger.revision
  entryCount = @($ledger.entries).Count
  failures = @($failures)
  entries = $rows
  conclusion = if ($failures.Count -eq 0) { 'Every HC parity entry resolves to an existing source and fixture path with valid domain/class/status and a non-empty observed order; this is a referential audit only and does not prove behavioral parity.' } else { 'One or more HC parity entries are not referentially complete; keep affected routes blocked.' }
  generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 HC PARITY LEDGER AUDIT: $($result.status) entries=$($result.entryCount)"
Write-Output "Evidence: $out"
