[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [string]$HcDllPath = '',
  [string]$ArtifactsPath = '',
  [string]$ShippingHcDllPath = '',
  [switch]$SourceOnly,
  [AllowEmptyCollection()][AllowEmptyString()][string[]]$CaseFilter
)
$ErrorActionPreference = 'Stop'
$directed = $PSBoundParameters.ContainsKey('CaseFilter')
$knownGroups = @('legacy','provider','pinned-source','pinned-host','pinned-uma','asus','hc-close','default-hc')
if (-not $SourceOnly) { $knownGroups += 'hc-binary' }
if ($directed) {
  if (@($CaseFilter).Count -eq 0) { throw 'CaseFilter must select at least one original group or exact case identity' }
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach ($selection in $CaseFilter) {
    $group = ([string]$selection -split '/',2)[0]
    if ([string]::IsNullOrWhiteSpace($selection) -or -not ($knownGroups -ccontains $group) -or
        $selection.EndsWith('/') -or -not $seen.Add($selection)) {
      throw "Unknown, empty or duplicate CaseFilter selection: '$selection'. Known groups: $($knownGroups -join ', ')"
    }
  }
  foreach ($selection in $CaseFilter) {
    if ($selection.Contains('/') -and $seen.Contains(($selection -split '/',2)[0])) {
      throw "Overlapping group and exact case selection: '$selection'"
    }
  }
}
# The original Runner alone declares exact identities. A typo within a known group
# is rejected there before any case runs, without a second permanent case table.
$runBinary = -not $SourceOnly -and (-not $directed -or @($CaseFilter | Where-Object { ($_ -split '/',2)[0] -ceq 'hc-binary' }).Count -gt 0)
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ([string]::IsNullOrWhiteSpace($HcDllPath)) {
  $HcDllPath = Join-Path $projectRoot 'deps\hc-runtime-upstream\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\HandheldCompanion.dll'
}
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
if ([string]::IsNullOrWhiteSpace($ArtifactsPath)) {
  $ArtifactsPath = Join-Path $evidenceRoot $(if ($directed) { 'build-selected' } elseif ($SourceOnly) { 'build-source-only' } else { 'build' })
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$result = Join-Path $evidenceRoot $(if ($directed) { 'selected-roundtrip-results.json' } elseif ($SourceOnly) { 'source-only-roundtrip-results.json' } else { 'actual-roundtrip-results.json' })
$captureGlobalEvidence = $env:YMCC_SIM_GLOBAL_RUN -eq '1'
$evidenceHelper = Join-Path $projectRoot 'tools/ymcc_sim/global/capability-catalog/output-evidence.mjs'
if (($SourceOnly -or $directed) -and $captureGlobalEvidence) { throw 'Selected/source-only diagnostics cannot satisfy the registered full HC binary roundtrip case' }
if ($captureGlobalEvidence) {
 if ([IO.Path]::GetFullPath($ArtifactsPath) -cne [IO.Path]::GetFullPath((Join-Path $evidenceRoot 'build'))) { throw 'Global roundtrip requires owned build output' }
 & node $evidenceHelper capture --case fan.unified-capability-roundtrip --evidence-dir $evidenceRoot
 if ($LASTEXITCODE -ne 0) { throw 'Roundtrip input capture failed' }
}
if ($runBinary -and [string]::IsNullOrWhiteSpace($ShippingHcDllPath)) {
 $shippingPayloadRoot=Join-Path $projectRoot 'PowerControl/fan-host'
 $shippingPayload=Get-Content -LiteralPath (Join-Path $shippingPayloadRoot 'YeManFanHost.payload.json') -Raw|ConvertFrom-Json
 $shippingRuntimeManifest=[IO.Path]::GetFullPath((Join-Path $shippingPayloadRoot ([string]$shippingPayload.runtimeManifest)))
 $ShippingHcDllPath=Join-Path (Split-Path -Parent $shippingRuntimeManifest) 'HandheldCompanion.dll'
}
$binaryAdapter = if ($runBinary) { 'true' } else { 'false' }
$runnerArguments = @($result,$HcDllPath,'normal',$ShippingHcDllPath)
if ($directed) { $runnerArguments += '--case-filter'; $runnerArguments += $CaseFilter }
$dotnetArguments = @('run','--project',(Join-Path $PSScriptRoot 'FanCapabilityRoundtrip.csproj'),'-c','Release',
  '--artifacts-path',$ArtifactsPath,"-p:HcBinaryAdapterEnabled=$binaryAdapter",'--') + $runnerArguments
$clock = [Diagnostics.Stopwatch]::StartNew()
& dotnet @dotnetArguments 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot $(if ($directed) { 'selected-roundtrip-output.log' } else { 'roundtrip-output.log' }))
$runExit = $LASTEXITCODE
$clock.Stop()
if ($runExit -ne 0) { throw "Fan capability roundtrip failed: $runExit; elapsed $($clock.Elapsed.TotalMilliseconds) ms; raw output retained" }
$record = Get-Content -LiteralPath $result -Raw -Encoding UTF8 | ConvertFrom-Json
$expectedBinaryStatus = if ($runBinary) { 'passed-actual-locked-dll' } elseif ($directed) { 'not-run-selected-scope' } else { 'not-run-source-only' }
if ($record.status -ne 'passed' -or $record.canonicalId -cne 'fan.unified-capability-roundtrip' -or
    $record.binaryStatus -cne $expectedBinaryStatus -or
    ($runBinary -and $record.shippingOneXStatus -cne 'passed-actual-shipping-one-x-contract')) {
  throw "Fan roundtrip evidence is incomplete for the selected scope: $expectedBinaryStatus"
}
function Assert-ExactNames($expected, $actual, [string]$label) {
  $expected = @($expected); $actual = @($actual)
  if ($expected.Count -eq 0 -or $actual.Count -ne $expected.Count) { throw "$label count mismatch" }
  $unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  for ($i=0; $i -lt $expected.Count; $i++) {
    if ([string]::IsNullOrWhiteSpace([string]$actual[$i]) -or
        -not [StringComparer]::Ordinal.Equals([string]$expected[$i],[string]$actual[$i]) -or
        -not $unique.Add([string]$actual[$i])) { throw "$label identity mismatch at $i" }
  }
}
if ($directed) {
  if ($record.selection -cne 'explicit') { throw 'Directed selection was not consumed by the original Runner' }
  Assert-ExactNames $CaseFilter $record.requestedSelection 'requested selection'
  $expectedGroups = [Collections.Generic.List[string]]::new()
  foreach ($selection in $CaseFilter) {
    $group = ($selection -split '/',2)[0]
    if (-not $expectedGroups.Contains($group)) { $expectedGroups.Add($group) }
    $matched = @($record.expectedCases | Where-Object { $_ -ceq $selection -or $_.StartsWith($selection + '/', [StringComparison]::Ordinal) })
    if ($matched.Count -eq 0) { throw "Selection did not declare any original cases: $selection" }
  }
  Assert-ExactNames $expectedGroups $record.selectedGroups 'selected groups'
  foreach ($identity in $record.expectedCases) {
    if (-not ($CaseFilter -ccontains $identity) -and -not ($CaseFilter -ccontains ($identity -split '/',2)[0])) {
      throw "Unselected case declared by Runner: $identity"
    }
  }
} elseif ($record.selection -cne 'default') { throw 'Default fixture scope changed unexpectedly' }
# The original Runner freezes this registry before execution. Compare actual names,
# order, groups and counts; a PASS label or minimum check count cannot stand in for it.
Assert-ExactNames $record.expectedCases $record.actualCases 'actual cases'
Assert-ExactNames $record.expectedCases @($record.caseResults | ForEach-Object { $_.identity }) 'case receipts'
if ($record.selectionVerified -ne $true) { throw 'Runner did not verify its exact declared selection' }
foreach ($case in $record.caseResults) {
  if ($case.status -cne 'passed' -or $case.checks -lt 1 -or
      -not (@($record.selectedGroups) -ccontains $case.group) -or
      $case.identity -cne ($case.group + '/' + $case.name)) { throw 'Unexpected or incomplete actual case receipt' }
}
if ($captureGlobalEvidence) {
 & node $evidenceHelper seal --case fan.unified-capability-roundtrip --evidence-dir $evidenceRoot
 if ($LASTEXITCODE -ne 0) { throw 'Roundtrip output/source evidence closure failed' }
}
if ($directed) {
  Write-Output "FAN_CAPABILITY_SELECTED_OK: $($record.actualCases.Count) exact cases; $($record.checks) checks; groups $($record.selectedGroups -join ','); HC binary $($record.binaryStatus); hardware NOT_RUN"
} elseif ($SourceOnly) {
  Write-Output "FAN_CAPABILITY_SOURCE_ONLY_OK: $($record.checks) checks; actual Host sources and in-memory terminals; HC binary NOT_RUN; hardware NOT_RUN"
} else {
  Write-Output "FAN_CAPABILITY_ROUNDTRIP_OK: $($record.checks) checks; actual locked HC DLL; no hardware transport"
}
