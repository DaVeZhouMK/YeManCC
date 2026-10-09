[CmdletBinding()]
param(
  [string]$CollectorPath = (Join-Path $PSScriptRoot '..\Build\Validation\CPU-Full-App-20261004\ROG-CPU-TELEMETRY-CAPTURE-v9-candidate.ps1')
)
$ErrorActionPreference = 'Stop'
# Only AST-extracted definitions and the side-effect-free quality/metric block run.
# No dot-sourcing of the collector, CIM inventory, CPU capture or live YMCC access.
$CollectorPath = [IO.Path]::GetFullPath($CollectorPath)
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($CollectorPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count) { throw ('COLLECTOR_PARSE_ERROR: ' + (@($parseErrors | ForEach-Object { $_.Message }) -join '; ')) }
$functions = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false))
. ([scriptblock]::Create(($functions | ForEach-Object { $_.Extent.Text }) -join "`r`n"))
# Extract the real final validity and group assembly statements, not a test rewrite.
$statements = @($ast.EndBlock.Statements)
$qualityStart = -1; $qualityEnd = -1
for ($i=0; $i -lt $statements.Count; $i++) {
  $text = $statements[$i].Extent.Text
  if ($text -match '^\$reasons\s*=') { $qualityStart = $i }
  if ($text -match '^\$webviewBreakdown\s*=') { $qualityEnd = $i; break }
}
if ($qualityStart -lt 0 -or $qualityEnd -lt $qualityStart) { throw 'QUALITY_AST_BLOCK_NOT_FOUND' }
$qualityBlock = [scriptblock]::Create(($statements[$qualityStart..$qualityEnd] | ForEach-Object { $_.Extent.Text }) -join "`r`n")
$hasScopeRepair = $null -ne (Get-Command Get-ScopeExclusion -ErrorAction SilentlyContinue)
$cases = New-Object System.Collections.ArrayList
$target = 'C:\ScopeFixture\Install-A\YeManCC.exe'
$foreign = 'D:\ScopeFixture\Install-B\YeManCC.exe'
$t0 = '2026-10-04T00:00:00.0000000Z'
$t1 = '2026-10-04T00:00:01.0000000Z'
$t2 = '2026-10-04T00:00:02.0000000Z'
function New-Fixture([int]$Id, [int]$Parent, [string]$Name, [string]$Path, [string]$Cmd, [string]$Created) {
  [pscustomobject]@{pid=$Id;ppid=$Parent;name=$Name;path=$Path;commandLine=$Cmd;creationTimeUtc=$Created;cpuMs=0.0;cpuReadOk=$true}
}
function Reset-Fixture {
  $script:ExePath = $target
  $script:rootObserved = $false
  $script:unassociated = @{}
  $script:scopeExcluded = @{}
  $script:scopeRootIdentity = ''
  $script:unresolvedWebView2Count = 0
  foreach ($name in @('inventoryErrors','readErrors','pidReuse','rawSamples','systemRows')) {
    Set-Variable -Name $name -Scope Script -Value (New-Object System.Collections.ArrayList)
  }
  $script:churn = New-Object System.Collections.Generic.HashSet[string]
  $script:missedSamples = 0; $script:sampleCount = 2
  $script:SkipSystemCpu = $true; $script:scenarioVerified = $false
  $script:hostInfo = [pscustomobject]@{logicalProcessors=8}
  $script:sw = [pscustomobject]@{Elapsed=[timespan]::FromSeconds(1)}
}
function Get-FixtureResult([object[]]$Inventory) {
  $state = Get-Candidates $Inventory
  $processRows = @(foreach ($row in $state.rows) {
    $item = $row | Select-Object *
    Add-Member -InputObject $item -NotePropertyName cpuDeltaMs -NotePropertyValue 100.0
    Add-Member -InputObject $item -NotePropertyName firstCpuElapsedMs -NotePropertyValue 0.0
    Add-Member -InputObject $item -NotePropertyName lastCpuElapsedMs -NotePropertyValue 1000.0
    $item
  })
  . $qualityBlock
  $summary = [ordered]@{measurementValid=$valid;quality=[ordered]@{valid=$valid;invalidReasons=@($reasons)};groups=$groups}
  # Verify production summary still signs the exact $valid from this block.
  $summaryAst = @($statements | Where-Object { $_.Extent.Text -match '^\$summary\s*=' })
  if ($summaryAst.Count -ne 1 -or $summaryAst[0].Extent.Text -notmatch 'measurementStarted=\$true;measurementValid=\$valid;') { throw 'SUMMARY_VALIDITY_WIRING_CHANGED' }
  [pscustomobject]@{state=$state;summary=$summary;webviewBreakdown=$webviewBreakdown;
    excluded=@($scopeExcluded.Values | ForEach-Object { $_ });unassociated=@($unassociated.Values | ForEach-Object { $_ })}
}
function Assert-Fact([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Invoke-Case([string]$Name, [bool]$Boundary, [scriptblock]$Body) {
  Reset-Fixture
  $script:lastObservation = $null
  try { & $Body; $passed=$true; $errorText=$null } catch { $passed=$false; $errorText=$_.Exception.Message }
  [void]$cases.Add([pscustomobject]@{name=$Name;boundary=$Boundary;passed=$passed;error=$errorText;observed=$script:lastObservation})
}
function Observe([object]$Result) {
  $script:lastObservation = [ordered]@{includedPids=@($Result.state.rows | ForEach-Object { $_.pid });
    associations=@($Result.state.rows | ForEach-Object { $_.association });
    measurementValid=$Result.summary.measurementValid;invalidReasons=$Result.summary.quality.invalidReasons;
    webview2=$Result.summary.groups.webview2;fanHost=$Result.summary.groups.'fan-host';inputHost=$Result.summary.groups.'input-host';
    total=$Result.summary.groups.ymccTotalUnique;excluded=$Result.excluded;unassociated=$Result.unassociated}
}
function Assert-Rejected([object]$Result, [int]$Id, [string]$Role) {
  Observe $Result
  Assert-Fact (@($Result.state.rows | Where-Object { $_.pid -eq $Id }).Count -eq 0) ('contaminated root total: PID ' + $Id)
  Assert-Fact (-not $Result.summary.measurementValid) 'excluded foreign/unresolved candidate was signed measurementValid=true'
  Assert-Fact (-not $Result.summary.groups.ymccTotalUnique.valid) 'partial scoped total was marked valid'
  Assert-Fact ($null -eq $Result.summary.groups[$Role].cpuCores) ('excluded empty role was silently zero-filled: ' + $Role)
  Assert-Fact (-not $Result.summary.groups[$Role].verifiedAbsent) ('excluded role claimed verifiedAbsent: ' + $Role)
  Assert-Fact ($Result.excluded.Count -gt 0) 'missing explicit scope exclusion evidence'
}
$root = New-Fixture 100 0 'YeManCC.exe' $target '' $t0
$otherRoot = New-Fixture 200 0 'YeManCC.exe' $foreign '' $t0
Invoke-Case 'positive-exact-root-and-descendants' $false {
  $r=Get-FixtureResult @($root,(New-Fixture 110 100 'msedgewebview2.exe' 'C:\Runtime\msedgewebview2.exe' '--type=renderer' $t1),
    (New-Fixture 111 110 'msedgewebview2.exe' '' '--type=gpu-process' $t2),
    (New-Fixture 120 100 'YeManFanHost.exe' '' '' $t1),(New-Fixture 121 100 'YeManInputHost.exe' '' '' $t1),
    (New-Fixture 130 100 'worker.exe' '' '' $t1))
  Observe $r
  Assert-Fact ($r.summary.measurementValid -and $r.state.rows.Count -eq 6) 'proven descendants were lost'
  Assert-Fact ($r.summary.groups.ymccTotalUnique.cpuCores -eq 0.6) 'unique total accounting changed'
  Assert-Fact ($r.webviewBreakdown.groups.renderer.processCount -eq 1 -and $r.webviewBreakdown.groups.'gpu-process'.processCount -eq 1) 'webview type classification changed'
}
Invoke-Case 'positive-case-insensitive-exact-path-and-equal-time' $false {
  $r=Get-FixtureResult @((New-Fixture 100 0 'yemancc.EXE' $target.ToUpperInvariant() '' $t0),(New-Fixture 120 100 'YeManFanHost.exe' '' '' $t0))
  Observe $r; Assert-Fact ($r.summary.measurementValid -and $r.state.rows.Count -eq 2) 'case-insensitive exact root/equal timestamp rejected'
}
Invoke-Case 'positive-verified-absence-without-excluded-candidates' $false {
  $r=Get-FixtureResult @($root); Observe $r
  Assert-Fact ($r.summary.measurementValid -and $r.summary.groups.'fan-host'.cpuCores -eq 0 -and $r.summary.groups.'fan-host'.verifiedAbsent) 'clean verified absence changed'
}
Invoke-Case 'foreign-webview-text-yeman' $true {
  $r=Get-FixtureResult @($root,$otherRoot,(New-Fixture 210 200 'msedgewebview2.exe' 'C:\Runtime\msedgewebview2.exe' '--user-data-dir=D:\ScopeFixture\Install-B\YeManCC.WebView2' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'foreign-fan-exact-helper-name' $true {
  $r=Get-FixtureResult @($root,$otherRoot,(New-Fixture 220 200 'YeManFanHost.exe' 'D:\ScopeFixture\Install-B\YeManFanHost.exe' '' $t1))
  Assert-Rejected $r 220 'fan-host'
}
Invoke-Case 'foreign-input-exact-helper-name' $true {
  $r=Get-FixtureResult @($root,$otherRoot,(New-Fixture 221 200 'YeManInputHost.exe' 'D:\ScopeFixture\Install-B\YeManInputHost.exe' '' $t1))
  Assert-Rejected $r 221 'input-host'
}
Invoke-Case 'unresolved-webview-yeman-substring-only' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 210 999 'msedgewebview2.exe' '' '--title=YeMan' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'unresolved-webview-same-install-user-data-is-not-instance-proof' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 210 999 'msedgewebview2.exe' '' '--user-data-dir=C:\ScopeFixture\Install-A\YeManCC.WebView2' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'unresolved-webview-other-app-marker-is-not-proof' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 210 999 'msedgewebview2.exe' '' '--webview-exe-name=YeManCC.exe --webview-exe-version=1' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'unresolved-webview-known-other-heuristic-no-zero' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 210 999 'msedgewebview2.exe' '' '--webview-exe-name=GameViewer.exe' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'unresolved-fan-unverified-exact-helper' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 220 999 'YeManFanHost.exe' '' '' $t1))
  Assert-Rejected $r 220 'fan-host'
}
Invoke-Case 'unresolved-input-same-install-path-no-parent-evidence' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 221 999 'YeManInputHost.exe' 'C:\ScopeFixture\Install-A\YeManInputHost.exe' '' $t1))
  Assert-Rejected $r 221 'input-host'
}
foreach ($helper in @(@('YeManRecoveryService.exe','recovery-service'),@('YeManTdpCtl.exe','tdp-helper'),@('YeManLightSetter.exe','light-setter'))) {
  $helperName=$helper[0]; $helperRole=$helper[1]
  Invoke-Case ('unresolved-helper-' + $helperRole) $true {
    $r=Get-FixtureResult @($root,(New-Fixture 230 999 $helperName ('D:\Other\'+$helperName) '' $t1))
    Assert-Rejected $r 230 $helperRole
  }
}
Invoke-Case 'reused-root-pid-newer-than-webview-child' $true {
  $r=Get-FixtureResult @((New-Fixture 100 0 'YeManCC.exe' $target '' $t2),(New-Fixture 210 100 'msedgewebview2.exe' '' '--type=renderer YeMan' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'reused-intermediate-parent-pid' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 150 100 'worker.exe' '' '' $t2),(New-Fixture 220 150 'YeManFanHost.exe' '' '' $t1))
  Assert-Rejected $r 220 'fan-host'
}
Invoke-Case 'missing-intermediate-creation-time' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 150 100 'worker.exe' '' '' ''),(New-Fixture 220 150 'YeManFanHost.exe' '' '' $t1))
  Assert-Rejected $r 220 'fan-host'
}
Invoke-Case 'malformed-intermediate-creation-time' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 150 100 'worker.exe' '' '' 'not-a-time'),(New-Fixture 210 150 'msedgewebview2.exe' '' '' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'parent-cycle-unresolved' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 210 211 'msedgewebview2.exe' '' 'YeMan' $t1),(New-Fixture 211 210 'worker.exe' '' '' $t1))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'foreign-nested-ymcc-root-is-instance-barrier' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 200 100 'YeManCC.exe' $foreign '' $t1),(New-Fixture 221 200 'YeManInputHost.exe' '' '' $t2))
  Assert-Rejected $r 221 'input-host'
  Assert-Fact (@($r.state.rows | Where-Object { $_.pid -eq 200 }).Count -eq 0) 'foreign nested root was counted as child-other'
}
Invoke-Case 'concurrent-same-exe-path-root-ambiguity' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 200 0 'YeManCC.exe' $target '' $t0),(New-Fixture 210 200 'msedgewebview2.exe' '' '' $t1))
  Assert-Rejected $r 210 'webview2'
  Assert-Fact ($r.state.rows.Count -eq 0) 'ambiguous same-path instances were combined'
}
Invoke-Case 'root-instance-change-between-inventories' $true {
  $first=Get-FixtureResult @($root)
  Assert-Fact $first.summary.measurementValid 'first exact root invalid'
  $r=Get-FixtureResult @((New-Fixture 200 0 'YeManCC.exe' $target '' $t1),(New-Fixture 210 200 'msedgewebview2.exe' '' '' $t2))
  Assert-Rejected $r 210 'webview2'
  Assert-Fact ($r.state.rows.Count -eq 0) 'replacement root instance entered pinned scope'
}
Invoke-Case 'duplicate-pid-in-inventory-fails-closed' $true {
  $r=Get-FixtureResult @($root,(New-Fixture 150 100 'worker.exe' '' '' $t1),(New-Fixture 150 999 'worker.exe' '' '' $t1),(New-Fixture 210 150 'msedgewebview2.exe' '' 'YeMan' $t2))
  Assert-Rejected $r 210 'webview2'
}
Invoke-Case 'root-missing-creation-time' $true {
  $r=Get-FixtureResult @((New-Fixture 100 0 'YeManCC.exe' $target '' ''),(New-Fixture 210 100 'msedgewebview2.exe' '' 'YeMan' $t1))
  Assert-Rejected $r 210 'webview2'
  Assert-Fact ($r.state.rows.Count -eq 0) 'unidentifiable root was selected'
}
Invoke-Case 'repeat-excluded-inventory-retains-quality-evidence' $true {
  $fixture=@($root,(New-Fixture 220 999 'YeManFanHost.exe' '' '' $t1))
  $first=Get-FixtureResult $fixture
  $r=Get-FixtureResult $fixture
  Assert-Rejected $r 220 'fan-host'
  Assert-Fact ($r.excluded.Count -eq 1) 'scope exclusions duplicated each inventory'
  $r=Get-FixtureResult @($root); Observe $r
  Assert-Fact (-not $r.summary.measurementValid -and $null -eq $r.summary.groups.'fan-host'.cpuCores) 'earlier excluded candidate disappeared from window quality'
}
$failed=@($cases | Where-Object { -not $_.passed })
$result=[ordered]@{schema='rog-cpu-scope-selftest-v1';utc=(Get-Date).ToUniversalTime().ToString('o');
  collectorPath=$CollectorPath;collectorSha256=(Get-FileHash -LiteralPath $CollectorPath -Algorithm SHA256).Hash;
  runtime=[ordered]@{version=$PSVersionTable.PSVersion.ToString();edition=$PSVersionTable.PSEdition;executable=(Get-Process -Id $PID).Path};
  parsed=$true;functionCount=$functions.Count;hasScopeRepair=$hasScopeRepair;
  execution='AST-extracted collector functions + real validity/Metric assembly; synthetic inventories only';
  total=$cases.Count;passed=$cases.Count-$failed.Count;failed=$failed.Count;cases=@($cases | ForEach-Object { $_ })}
$result | ConvertTo-Json -Depth 20
if ($failed.Count) { exit 1 }
exit 0