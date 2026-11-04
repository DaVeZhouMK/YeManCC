# Explicit release bug checks reuse the original optimizer and its shared budget.
function Invoke-YmccReleaseBugValidation {
 [CmdletBinding()]
 param(
  [Parameter(Mandatory=$true)][string]$ProjectRoot,
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [ValidateRange(1,1800)][int]$BudgetSeconds=1800
 )
 $ErrorActionPreference='Stop'
 $resolvedPlan=(Resolve-Path -LiteralPath $PlanPath -ErrorAction Stop).ProviderPath
 if(-not(Test-Path -LiteralPath $resolvedPlan -PathType Leaf)){throw 'RELEASE_BUG_PLAN_FILE_REQUIRED'}
 . (Join-Path $ProjectRoot 'tools/ymcc_sim/global/release-host-environment.ps1')
 $core=Get-YmccSimulationCoreRoot
 if(-not(Test-Path -LiteralPath $core -PathType Container)){throw 'EXPLICIT_RELEASE_BUG_CHECK_REQUIRES_PREPARED_SIMULATION_CORE'}
 $node=(Get-Command node.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
 $entry=Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/cli.mjs'
 $invocationRoot=Join-Path $core ('Evidence/Build/release-bugs/'+[Guid]::NewGuid().ToString('N'))
 $beforeHash=(Get-FileHash -LiteralPath $resolvedPlan -Algorithm SHA256).Hash
 & $node $entry --mode run --bug-plan $resolvedPlan --rerun --budget-ms ([string]($BudgetSeconds*1000)) --out-dir $invocationRoot
 $exitCode=$LASTEXITCODE
 if($exitCode -ne 0){throw "RELEASE_BUG_VALIDATION_FAILED: exit=$exitCode; evidence=$invocationRoot"}
 $receipts=@(Get-ChildItem -LiteralPath $invocationRoot -Directory|ForEach-Object{Join-Path $_.FullName 'execution-budget.json'}|Where-Object{Test-Path -LiteralPath $_ -PathType Leaf})
 if($receipts.Count -ne 1){throw 'RELEASE_BUG_CURRENT_BUDGET_RECEIPT_REQUIRED'}
 $budget=Get-Content -LiteralPath $receipts[0] -Raw -Encoding UTF8|ConvertFrom-Json
 if($budget.status -notin @('PASS_SELECTED_SCOPE','NOT_RUN_NO_RELEASE_BUGS')){throw "RELEASE_BUG_CURRENT_STATUS_NOT_ACCEPTED: $($budget.status)"}
 $afterHash=(Get-FileHash -LiteralPath $resolvedPlan -Algorithm SHA256).Hash
 if($beforeHash -cne $afterHash){throw 'RELEASE_BUG_PLAN_CHANGED_DURING_VALIDATION'}
 $receipt=[ordered]@{schemaVersion=1;status=$budget.status;scope='explicit-original-bug-regressions-only';plan=$resolvedPlan;planSha256=$beforeHash;budgetReceipt=$receipts[0];budgetReceiptSha256=(Get-FileHash -LiteralPath $receipts[0] -Algorithm SHA256).Hash;budgetSeconds=$BudgetSeconds;actualPassedThisRun=$budget.actualPassedThisRun;fullFixtureValidation='NOT_RUN';fullProductAcceptance=$false}
 $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $invocationRoot 'RELEASE-BUG-VALIDATION.json') -Encoding UTF8
 Write-Output ('RELEASE_BUG_VALIDATION='+$budget.status+'; evidence='+$invocationRoot)
}
