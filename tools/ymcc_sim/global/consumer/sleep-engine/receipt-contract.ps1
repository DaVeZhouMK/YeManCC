# Pure receipt/source guards. No Host, engine, network or process is started.
function Test-SleepEngineChildReceipt {
  param([int]$ChildExitCode, [string]$ReportedStatus, [bool]$NaturalExit, [bool]$ConsumerCompleted)
  $code = if ($ChildExitCode -ne 0) { 'CHILD_NONZERO_EXIT' }
    elseif (-not $NaturalExit) { 'CHILD_NATURAL_EXIT_UNCONFIRMED' }
    elseif (-not $ConsumerCompleted) { 'CONSUMER_DID_NOT_COMPLETE' }
    elseif ($ReportedStatus -ne 'PASS') { 'CHILD_RECEIPT_NOT_PASS' } else { $null }
  [pscustomobject]@{ passed = $null -eq $code; code = $code; stage = 'feedback' }
}
function Get-SleepEngineSourceSnapshot {
  param([Parameter(Mandatory=$true)][string]$SourceRoot)
  $files = @((Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Filter '*.cs')) +
    @((Get-ChildItem -LiteralPath $SourceRoot -File -Filter '*.csproj')) +
    @((Get-Item -LiteralPath (Join-Path $SourceRoot 'Invoke-SleepEngine.ps1'))) +
    @((Get-Item -LiteralPath (Join-Path $SourceRoot 'receipt-contract.ps1')))
  @($files | Sort-Object FullName -Unique | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
  })
}
function Test-SleepEngineSourceSnapshots {
  param([object[]]$Before, [object[]]$After)
  $previous = @{}
  $current = @{}
  foreach ($item in $Before) { $previous[$item.path.ToLowerInvariant()] = $item.sha256.ToLowerInvariant() }
  foreach ($item in $After) { $current[$item.path.ToLowerInvariant()] = $item.sha256.ToLowerInvariant() }
  $differences = @()
  foreach ($key in @($previous.Keys + $current.Keys | Sort-Object -Unique)) {
    if (-not $previous.ContainsKey($key) -or -not $current.ContainsKey($key)) { $differences += [ordered]@{ path = $key; code = 'SOURCE_FILE_SET_CHANGED' } }
    elseif ($previous[$key] -ne $current[$key]) { $differences += [ordered]@{ path = $key; code = 'SOURCE_HASH_CHANGED' } }
  }
  [pscustomobject]@{ unchanged = $differences.Count -eq 0 -and $previous.Count -gt 0; beforeBuildChecked = $true; differences = @($differences); beforeCount = $previous.Count; afterCount = $current.Count }
}
