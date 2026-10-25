# Scope-specific continuity for the existing nine-suite producer; no process starts.
function Assert-YmccHostCatalogNineSource {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)]$Plan,[Parameter(Mandatory=$true)][string]$GatePath)
 $ErrorActionPreference='Stop'
 function NeedNine([bool]$Value,[string]$Code){if(-not $Value){throw ('HOST_NINE_SOURCE_'+$Code)}}
 function HashNine([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
 function ReadNine($Record){
  NeedNine ($Record -and $Record.path -is [string] -and $Record.sha256 -cmatch '^[a-f0-9]{64}$' -and [IO.Path]::GetExtension($Record.path) -ieq '.json' -and (Test-Path -LiteralPath $Record.path -PathType Leaf)) 'DESCRIPTOR'
  NeedNine ((HashNine $Record.path) -ceq $Record.sha256) 'PROOF_CHANGED'
  Get-Content -LiteralPath $Record.path -Raw -Encoding UTF8|ConvertFrom-Json
 }
 $qualification=ReadNine $Plan.nineSuiteSourceEvidence
 $outer=ReadNine $qualification.originalExecution
 NeedNine ($qualification.status -ceq 'PASS_SCOPED_SOURCE_REVALIDATION' -and $qualification.originalOuterStatusPreserved -ceq 'FAIL' -and $qualification.doesNotRewriteOriginalNineReceipt -is [bool] -and $qualification.doesNotRewriteOriginalNineReceipt -eq $true) 'QUALIFICATION'
 NeedNine ($outer.status -ceq 'FAIL' -and $outer.exitCode -eq 0 -and $outer.verdict -ceq 'ALL_PASS' -and $outer.candidate.hostSha256 -ceq $Plan.newHostDllSha256 -and $outer.candidate.backendSha256 -ceq $Plan.newBackendDllSha256 -and $outer.candidate.manifestSha256 -ceq $Plan.newHostManifestSha256) 'EXECUTION_BINDING'
 NeedNine ([IO.Path]::GetFullPath($outer.fullGateDirectory).TrimEnd('\') -ceq [IO.Path]::GetFullPath($Plan.fullGateDirectory).TrimEnd('\') -and $outer.report.path -ceq $GatePath -and (HashNine $GatePath) -ceq $outer.report.sha256) 'REPORT_BINDING'
 NeedNine (@($outer.suiteResults).Count -eq 9 -and @($outer.suiteResults|Where-Object exitCode -NE 0).Count -eq 0 -and @($outer.inputChanges).Count -eq 13 -and @($qualification.originalGlobalSourceDrift).Count -eq 13) 'ORIGINAL_SCOPE'
 $expectedChangedModules=@('candidate-options.mjs','candidate-options.test.mjs','cli.mjs','extra-adapters.mjs','capability-catalog/adapter.mjs','motion-consumer-signals/adapter.mjs','motion-consumer-signals/inputs.mjs','motion-consumer-signals/report.test.mjs','motion-consumer-signals/test-fixtures.mjs','motion-consumer-signals/validation.mjs','revalidation-plan/index.mjs','revalidation-plan/plan.test.mjs')
 $repo=[IO.Path]::GetFullPath($Plan.repoRoot).TrimEnd('\');$prefix=Join-Path $repo 'tools/ymcc_sim/global/'
 $changedPs=Join-Path $repo 'tools/ymcc_sim/domains/global/global_domain.ps1'
 $seen=@{}
 foreach($changed in @($outer.inputChanges)){
  $path=[IO.Path]::GetFullPath($changed.path)
  NeedNine (-not $seen.ContainsKey($path)) 'DRIFT_DUPLICATE';$seen[$path]=$true
  NeedNine (@($qualification.originalGlobalSourceDrift|Where-Object {$_.path -ceq $changed.path -and $_.beforeSha256 -ceq $changed.beforeSha256 -and $_.afterSha256 -ceq $changed.afterSha256}).Count -eq 1) 'DRIFT_BINDING'
  if($path -ine $changedPs){NeedNine ($path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -and $path.Substring($prefix.Length).Replace('\','/') -cin $expectedChangedModules) 'UNREVIEWED_DRIFT'}
 }
 NeedNine ($seen.Count -eq 13 -and $seen.ContainsKey($changedPs)) 'CHANGED_SOURCE_SCOPE'
 $requiredDirect=@('tests/run_all.ps1','tests/tool_gate.ps1','tests/invariants.ps1','tests/extremes.ps1','tests/regressions.ps1','tests/gyro_core.ps1','tests/signals.ps1','tests/api_contract.ps1','tests/cycle.ps1','lib/sim_core.ps1','domains/fan/fan_domain.ps1')
 NeedNine (@($qualification.reviewedNineDirectSourceFiles).Count -eq 11 -and @($qualification.globalModuleInvocationReferences).Count -eq 0) 'DIRECT_SOURCE_SET'
 foreach($relative in $requiredDirect){
  $path=Join-Path $repo ('tools/ymcc_sim/'+$relative)
  $row=@($qualification.reviewedNineDirectSourceFiles|Where-Object path -CEQ $path)
  NeedNine ($row.Count -eq 1 -and (HashNine $path) -ceq $row[0].sha256 -and @($outer.inputs|Where-Object {$_.path -ceq $path -and $_.sha256 -ceq $row[0].sha256}).Count -eq 1) 'DIRECT_SOURCE_CHANGED'
  $text=Get-Content -LiteralPath $path -Raw -Encoding UTF8
  NeedNine ($text -notmatch '\.mjs|global_domain\.ps1') 'GLOBAL_MODULE_REFERENCE_CHANGED'
 }
 $run=$qualification.regressionRerun;$regression=ReadNine $run.report
 NeedNine ($run.exitCode -eq 0 -and $run.pid -gt 0 -and $run.naturalExit -is [bool] -and $run.naturalExit -eq $true -and $qualification.recursiveInputDrift -eq 0) 'REGRESSION_EXECUTION'
 # The actual regression schema uses cases/pass/fail; its raw producer status remains untouched.
 NeedNine (@($regression.cases).Count -eq 10 -and @($regression.cases|Where-Object {$_.pass -isnot [bool] -or $_.pass -ne $true}).Count -eq 0) 'REGRESSION_CASES'
 foreach($log in @($run.stdout,$run.stderr)){NeedNine ((HashNine $log.path) -ceq $log.sha256) 'REGRESSION_LOG_CHANGED'}

 $rows=@($qualification.recursiveInputIdentities)
 if($qualification.PSObject.Properties['recursiveEncodingRefresh'] -and $qualification.recursiveEncodingRefresh){
  $encoding=ReadNine $qualification.recursiveEncodingRefresh
  NeedNine ($encoding.status -ceq 'PASS' -and $encoding.hostStarted -is [bool] -and $encoding.hostStarted -eq $false -and $encoding.processStarted -is [bool] -and $encoding.processStarted -eq $false -and @($encoding.inputChanges).Count -eq 0 -and @($encoding.checks).Count -eq 1 -and $encoding.checks[0].id -ceq 'R8' -and $encoding.checks[0].pass -is [bool] -and $encoding.checks[0].pass -eq $true) 'ENCODING_SCOPE'
  $regressionPath=Join-Path $repo 'tools/ymcc_sim/tests/regressions.ps1'
  NeedNine ($encoding.source.path -ceq $regressionPath -and (HashNine $regressionPath) -ceq $encoding.source.sha256) 'ENCODING_SOURCE'
  $source=Get-Content -LiteralPath $regressionPath -Raw -Encoding UTF8
  $begin=$source.IndexOf('function Test-CyclePsEncoding(');$end=$source.IndexOf('# R9 ',$begin)
  NeedNine ($begin -ge 0 -and $end -gt $begin -and $source.Substring($begin,$end-$begin) -ceq $encoding.executedRegion) 'ENCODING_SOURCE'
  $rows=@($encoding.sourceInputs)
 }

 NeedNine ($rows.Count -ge 1 -and $rows.Count -le 4096 -and @($rows|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'RECURSIVE_INPUT_SET'
 $physical=@(Get-ChildItem -LiteralPath (Join-Path $repo 'tools/ymcc_sim') -File -Recurse -Force|Where-Object {$_.Extension -iin @('.ps1','.json')}|ForEach-Object FullName|Sort-Object)
 NeedNine ((Compare-Object $physical @($rows.path|Sort-Object)) -eq $null) 'RECURSIVE_INPUT_SET'
 foreach($row in $rows){NeedNine ((HashNine $row.path) -ceq $row.sha256) 'RECURSIVE_INPUT_CHANGED'}
 foreach($row in @($outer.inputs)){
  if($seen.ContainsKey([IO.Path]::GetFullPath($row.path))){continue}
  # Global modules are not invoked by this direct nine-suite path. PS/JSON metadata there is covered by the exact R8 scan above.
  if([IO.Path]::GetFullPath($row.path).StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){continue}
  NeedNine ((HashNine $row.path) -ceq $row.sha256) 'UNCHANGED_INPUT_DRIFT'
 }
 [pscustomobject]@{status='PASS';originalBusinessSuites=9;originalOuterStatus='FAIL-preserved';scopedRevalidation=('actual10-case regression plus exact current R8 scan on'+$rows.Count+' recursivePS/JSON inputs');globalModulesClaimedTested=$false;runtimeStarted=$false;hardwareAcceptance=$false}
}
