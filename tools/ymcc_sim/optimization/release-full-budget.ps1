# Full acceptance reuses the existing original gate inside the original owned-job guard.
function Get-YmccFullReleaseNode {
 return (Get-Command node.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
}
function New-YmccFullReleaseFixtureSession {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[ValidateRange(1,1800)][int]$BudgetSeconds=1800)
 $entry=Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-full-budget.mjs'
 $text=& (Get-YmccFullReleaseNode) $entry --action create --budget-ms ([string]($BudgetSeconds*1000))
 if($LASTEXITCODE -ne 0){throw 'FULL_RELEASE_SESSION_CREATE_FAILED'}
 $value=($text -join "`n")|ConvertFrom-Json
 if(-not $value.sessionFile -or -not(Test-Path -LiteralPath $value.sessionFile -PathType Leaf)){throw 'FULL_RELEASE_SESSION_CREATE_RESULT_INVALID'}
 return [string]$value.sessionFile
}
function Invoke-YmccFullReleaseFixtureStage {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][ValidateSet('Build','Package','Verify')][string]$Stage,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[string]$CandidatePackage='',[string]$LifecycleEvidenceRoot='')
 $entry=Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-full-budget.mjs'
 $arguments=@('--action','stage','--stage',$Stage,'--workspace-root',([IO.Path]::GetFullPath($WorkspaceRoot)))
 $sessionFile=$env:YMCC_RELEASE_FIXTURE_SESSION
 if($Stage -in @('Package','Verify')){
  $buildReceiptPath=Join-Path $WorkspaceRoot 'Build/Validation/DeckyUpdate/BUILD.json'
  if(-not(Test-Path -LiteralPath $buildReceiptPath -PathType Leaf)){throw 'FULL_RELEASE_BUILD_SESSION_REQUIRED'}
  $buildReceipt=Get-Content -LiteralPath $buildReceiptPath -Raw -Encoding UTF8|ConvertFrom-Json
  if($buildReceipt.passed -ne $true -or -not $buildReceipt.fixtureBudgetSession){throw 'FULL_RELEASE_BUILD_SESSION_REQUIRED'}
  if($sessionFile -and [IO.Path]::GetFullPath($sessionFile) -ine [IO.Path]::GetFullPath([string]$buildReceipt.fixtureBudgetSession)){throw 'FULL_RELEASE_BUILD_SESSION_CHANGED'}
  $sessionFile=[string]$buildReceipt.fixtureBudgetSession
 }
 if($sessionFile){$arguments+=@('--session',$sessionFile)}
 if($CandidatePackage){$arguments+=@('--candidate-package',([IO.Path]::GetFullPath($CandidatePackage)))}
 if($LifecycleEvidenceRoot){$arguments+=@('--lifecycle-evidence-root',([IO.Path]::GetFullPath($LifecycleEvidenceRoot)))}
 # Full acceptance already requires PowerShell 7 for its owned-job budget guard.
 # Pass that exact available runtime to the existing RawProgram consumers;
 # otherwise their source-bound parallel route would remain diagnostic-only.
 # Ordinary exports never enter this explicit fixture stage.
 $previousRawProgramLauncher=$env:YMCC_SIM_RAW_PROGRAM_PWSH
 try {
  if(-not $previousRawProgramLauncher){
   $rawLauncher=Get-Command pwsh.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1
   $env:YMCC_SIM_RAW_PROGRAM_PWSH=[IO.Path]::GetFullPath($rawLauncher.Source)
  }
  & (Get-YmccFullReleaseNode) $entry @arguments
 } finally { $env:YMCC_SIM_RAW_PROGRAM_PWSH=$previousRawProgramLauncher }
 # The calling original script preserves this native exit code, including TIMEOUT=124.
}
function Assert-YmccFullReleaseFixtureWorker {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$Stage,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[string]$CandidatePackage='',[string]$LifecycleEvidenceRoot='')
 . (Join-Path $ProjectRoot 'tools/ymcc_sim/global/release-host-environment.ps1')
 $core=Get-YmccSimulationCoreRoot
 $file=[IO.Path]::GetFullPath([string]$env:YMCC_RELEASE_FIXTURE_SESSION)
 if(-not $file.StartsWith($core.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'FULL_RELEASE_WORKER_SCOPE_INVALID'}
 $cursor=$file
 while($cursor){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'FULL_RELEASE_WORKER_REPARSE_POINT'}};$next=[IO.Path]::GetDirectoryName($cursor);if($next -eq $cursor){break};$cursor=$next}
 if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or ([IO.FileInfo]$file).Length -gt 1048576){throw 'FULL_RELEASE_WORKER_SESSION_MISSING'}
 $session=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json
 $attempt=$session.activeAttempt
 if($session.schemaVersion -ne 1 -or $session.kind -cne 'ymcc-full-release-fixture-budget' -or $session.repoRoot -ine $ProjectRoot -or $session.coreRoot -ine $core -or $session.status -cne 'RUNNING' -or -not $env:YMCC_RELEASE_FIXTURE_ATTEMPT -or $attempt.id -cne $env:YMCC_RELEASE_FIXTURE_ATTEMPT -or $attempt.stage -cne $Stage -or $attempt.workspaceRoot -ine [IO.Path]::GetFullPath($WorkspaceRoot) -or $attempt.status -cne 'RUNNING' -or -not(Test-Path -LiteralPath ($file+'.lock') -PathType Leaf)){throw 'FULL_RELEASE_WORKER_CONTEXT_INVALID'}
 $expectedCandidate=if($CandidatePackage){[IO.Path]::GetFullPath($CandidatePackage)}else{''}
 $expectedLifecycle=if($LifecycleEvidenceRoot){[IO.Path]::GetFullPath($LifecycleEvidenceRoot)}else{''}
 if($attempt.candidatePackage -ine $expectedCandidate -or $attempt.lifecycleEvidenceRoot -ine $expectedLifecycle -or (Get-FileHash -LiteralPath $attempt.gateEntry -Algorithm SHA256).Hash -ine $attempt.gateSha256){throw 'FULL_RELEASE_WORKER_INPUT_CHANGED'}
}

# Existing hosted full-acceptance regressions share the successful Build quota.
function Invoke-YmccFullReleaseRegression {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,
  [Parameter(Mandatory=$true)][ValidateSet('test:updater','test:updater-manager-resume','test:updater-resume','test:updater-retry','test:updater-rollback','test:fan-host-paths','test:fan-payload','test:updater-install-chain')][string]$Test)
 $buildReceiptPath=Join-Path $WorkspaceRoot 'Build/Validation/DeckyUpdate/BUILD.json'
 if(-not(Test-Path -LiteralPath $buildReceiptPath -PathType Leaf)){throw 'FULL_RELEASE_BUILD_SESSION_REQUIRED'}
 $buildReceipt=Get-Content -LiteralPath $buildReceiptPath -Raw -Encoding UTF8|ConvertFrom-Json
 if($buildReceipt.passed -ne $true -or -not $buildReceipt.fixtureBudgetSession){throw 'FULL_RELEASE_BUILD_SESSION_REQUIRED'}
 $sessionFile=[string]$buildReceipt.fixtureBudgetSession
 if($env:YMCC_RELEASE_FIXTURE_SESSION -and [IO.Path]::GetFullPath($env:YMCC_RELEASE_FIXTURE_SESSION) -ine [IO.Path]::GetFullPath($sessionFile)){throw 'FULL_RELEASE_BUILD_SESSION_CHANGED'}
 $pnpm=(Get-Command pnpm.cmd -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
 $entry=Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-full-budget.mjs'
 & (Get-YmccFullReleaseNode) $entry --action hosted-regression --test $Test --workspace-root ([IO.Path]::GetFullPath($WorkspaceRoot)) --session $sessionFile --pnpm-command $pnpm
 # Preserve the original native exit, including TIMEOUT=124, for the caller.
}


# The public Full entry delegates its complete original body once; ordinary/Fast
# exports never call this function. Stage accounting remains a separate inner ledger.
function Invoke-YmccFullReleasePublishInvocation {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[string]$WorkspaceRoot='',
  [switch]$DeployInstalled,[ValidateRange(1,1800)][int]$BudgetSeconds=1800,
  [Parameter(Mandatory=$true)][long]$StartedAtUnixMs)
 . (Join-Path $ProjectRoot 'tools/ymcc_sim/global/release-host-environment.ps1')
 $core=Get-YmccSimulationCoreRoot
 $parent=Join-Path $core 'Evidence/Build/full-publish-invocation'
 New-Item -ItemType Directory -Path $parent -Force|Out-Null
 Assert-YmccLocalFrozenPath $parent
 $requestRoot=Join-Path $parent ([Guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $requestRoot|Out-Null
 $process=[Diagnostics.Process]::GetCurrentProcess()
 try{$ownerTicks=$process.StartTime.ToUniversalTime().Ticks.ToString()}finally{$process.Dispose()}
 $request=[ordered]@{schemaVersion=1;kind='ymcc-full-publish-original-body-v1';repoRoot=[IO.Path]::GetFullPath($ProjectRoot);coreRoot=$core;workspaceRoot=if($WorkspaceRoot){[IO.Path]::GetFullPath($WorkspaceRoot)}else{''};deployInstalled=[bool]$DeployInstalled;budgetSeconds=$BudgetSeconds;startedAtUnixMs=$StartedAtUnixMs;ownerPid=$PID;ownerStartTicks=$ownerTicks}
 $file=Join-Path $requestRoot 'REQUEST.json'
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($request|ConvertTo-Json -Depth 6))
 $stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
 $entry=Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-full-budget.mjs'
 & (Get-YmccFullReleaseNode) $entry --action publish-invocation --request-file $file
 # Keep the original worker's nonzero and TIMEOUT=124 without a second attempt.
 $global:LASTEXITCODE=$LASTEXITCODE
}
