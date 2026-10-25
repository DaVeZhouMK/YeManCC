[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'verify-host-capability-evidence.ps1')
$plan=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
$root=[IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
if(Test-Path -LiteralPath $root){throw 'Fresh capability test evidence required'}
[IO.Directory]::CreateDirectory($root)|Out-Null
$checks=[Collections.Generic.List[object]]::new();$steps=[Collections.Generic.List[object]]::new()
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function WriteJson([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))}
function Descriptor([string]$Path){[pscustomobject]@{path=$Path;sha256=Hash $Path}}
function Check([bool]$Value,[string]$Name){$checks.Add([pscustomobject]@{name=$Name;passed=$Value});if(-not $Value){throw ('CAPABILITY_TEST_FAILED: '+$Name)}}
function Clone($Value){return $Value|ConvertTo-Json -Depth 100|ConvertFrom-Json}
$cap=$plan.capabilityEvidence
if(-not $cap){throw 'Plan requires explicit capability suite and harness inputs'}
$repo=[IO.Path]::GetFullPath($plan.repoRoot).TrimEnd('\')
$fixed=Join-Path $repo 'tools/ymcc_sim/global/onex-fan-coexistence/fixtures'
$msiFixed=Join-Path $repo 'tools/ymcc_sim/global/fan-open-signals/fixtures/legacy-host-5e941'
$suite=Get-Content -LiteralPath $cap.suite.path -Raw -Encoding UTF8|ConvertFrom-Json
$hostFile=Join-Path $suite.hostPayloadRoot 'YeManFanHost.dll'
$backendFile=Join-Path $suite.hostPayloadRoot 'YeManFanBackend.dll'
$hcFile=@($suite.inputEvidence.hc.path)[0]
$oneX=$cap.oneXHarness.path;$msi=$cap.msiHarness.path
$dotnet=(Get-Command dotnet.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$paths=@($dotnet,$oneX,$msi,$hostFile,$backendFile,$hcFile,$cap.suite.path)
foreach($dir in @((Split-Path -Parent $oneX),(Split-Path -Parent $msi))){$paths+=@(Get-ChildItem -LiteralPath $dir -File|Where-Object {$_.Extension -in @('.dll','.exe','.json')}|ForEach-Object FullName)}
foreach($source in $suite.sourceHashes){$paths+=Join-Path $repo $source.path}
foreach($relative in @('capabilities/FanCapabilityCatalog.cs','capabilities/AsusFanCapabilitySession.cs')){$paths+=Join-Path $repo $relative}
foreach($file in @('host7f-light-e546/YeManFanHost.dll','host7f-light-e546/YeManFanBackend.dll','host7f-light-e546/YeManLightSetter.dll','host7f-light-e546/provenance.json','host-a678-write-failure/YeManFanHost.dll','host-a678-write-failure/YeManFanBackend.dll','host-a678-write-failure/provenance.json')){$paths+=Join-Path $fixed $file}
$paths+=Join-Path $msiFixed 'YeManFanHost.dll';$paths+=Join-Path $msiFixed 'YeManFanBackend.dll'
$inputs=@($paths|Select-Object -Unique|ForEach-Object {Descriptor $_})
function Execute([string]$Id,[string[]]$Arguments,[string]$Output){
 if(Test-Path -LiteralPath $Output){throw 'Capability output must be newly produced'}
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$dotnet;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in $Arguments){$info.ArgumentList.Add($arg)}
 $info.WorkingDirectory=$root
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::new();$process.StartInfo=$info
 if(-not $process.Start()){throw 'Capability process start failed'}
 $processId=$process.Id;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$exited=$process.WaitForExit(60000);$timedOut=-not $exited
 if($timedOut){$process.Kill($true);$process.WaitForExit(5000)|Out-Null}
 $outText=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult();$code=$process.ExitCode;$finished=[DateTime]::UtcNow
 [IO.File]::WriteAllText((Join-Path $root ($Id+'.log')),($outText+"`n"+$errorText),[Text.UTF8Encoding]::new($false))
 $fresh=(Test-Path -LiteralPath $Output -PathType Leaf) -and (Get-Item -LiteralPath $Output).LastWriteTimeUtc -ge $started.AddSeconds(-1)
 $record=[pscustomobject]@{id=$Id;pid=$processId;executable=(Descriptor $dotnet);arguments=@($Arguments);startedUtc=$started.ToString('o');finishedUtc=$finished.ToString('o');exitCode=$code;naturalExit=$exited;timedOut=$timedOut;resultFresh=[bool]$fresh;output=$(if($fresh){Descriptor $Output}else{$null})}
 $steps.Add($record);$process.Dispose()
 Check ($code -eq 0 -and $exited -and $fresh) ($Id+' actual command naturally exits zero and writes fresh result')
}
$oldOut=Join-Path $root 'old-coexistence.json'
Execute 'old-coexistence' @($oneX,'old',(Join-Path $fixed 'host7f-light-e546/YeManFanHost.dll'),(Join-Path $fixed 'host7f-light-e546/YeManLightSetter.dll'),$hcFile,$oldOut) $oldOut
$writeOut=Join-Path $root 'old-write.json'
Execute 'old-write' @($oneX,'old-write',(Join-Path $fixed 'host-a678-write-failure/YeManFanHost.dll'),$hcFile,$writeOut) $writeOut
$candidateOut=Join-Path $root 'candidate.json'
Execute 'candidate' @($oneX,$hostFile,(Join-Path $fixed 'host7f-light-e546/YeManLightSetter.dll'),$hcFile,$candidateOut) $candidateOut
$msiOut=Join-Path $root 'msi.json'
Execute 'msi' @($msi,(Join-Path $msiFixed 'YeManFanHost.dll'),$hostFile,$hcFile,$msiOut) $msiOut
$inputChanges=@($inputs|Where-Object {(Hash $_.path) -cne $_.sha256})
$execution=[pscustomobject]@{schemaVersion=1;kind='host-capability-execution-v1';status='PASS';candidateHostSha256=$plan.newHostDllSha256;candidateBackendSha256=$plan.newBackendDllSha256;inputs=$inputs;inputChanges=$inputChanges;steps=@($steps);hostMainStarted=$false;hardwareIo=$false;installedApplicationMutated=$false}
$executionPath=Join-Path $root 'execution.json';WriteJson $executionPath $execution
$plan.capabilityEvidence|Add-Member -NotePropertyName execution -NotePropertyValue (Descriptor $executionPath) -Force
$proofPlan=Join-Path $root 'verified-capability-plan.json';WriteJson $proofPlan $plan
$positive=Assert-YmccHostCapabilityEvidence -Plan $plan
Check ($positive.status -eq 'PASS' -and $positive.candidateCases -eq 69 -and $positive.executedCommands -eq 4) 'all current source/artifact/failure/process evidence accepted'
function Negative([string]$Name,[string]$Reason,[scriptblock]$Mutation){
 $dir=Join-Path $root ('negative-'+$Name);[IO.Directory]::CreateDirectory($dir)|Out-Null
 $candidatePlan=Clone $plan;$candidateExecution=Clone $execution;$candidateSuite=Clone $suite
 # Mutable test copies never touch the authoritative command outputs or receipts.
 foreach($step in $candidateExecution.steps){
  $original=$step.output.path;$copy=Join-Path $dir ([IO.Path]::GetFileName($original));Copy-Item -LiteralPath $original -Destination $copy
  $step.output=Descriptor $copy;$step.arguments[-1]=$copy
 }
 & $Mutation $candidatePlan $candidateExecution $candidateSuite $dir
 $suiteFile=Join-Path $dir 'suite.json';WriteJson $suiteFile $candidateSuite;$candidatePlan.capabilityEvidence.suite=Descriptor $suiteFile
 $executionFile=Join-Path $dir 'execution.json';WriteJson $executionFile $candidateExecution;$candidatePlan.capabilityEvidence.execution=Descriptor $executionFile
 $reasonText=$null
 try{Assert-YmccHostCapabilityEvidence -Plan $candidatePlan|Out-Null}catch{$reasonText=$_.Exception.Message}
 Check ($null -ne $reasonText -and $reasonText.Contains($Reason)) ('reject '+$Name+' ['+$reasonText+']')
}
function ChangeOutput($Execution,[string]$Id,[scriptblock]$Mutation){
 $step=@($Execution.steps|Where-Object id -ceq $Id)[0];$object=Get-Content -LiteralPath $step.output.path -Raw -Encoding UTF8|ConvertFrom-Json
 & $Mutation $object
 WriteJson $step.output.path $object
 [IO.File]::SetLastWriteTimeUtc($step.output.path,([DateTimeOffset]$step.finishedUtc).UtcDateTime)
 $step.output.sha256=Hash $step.output.path
}
Negative 'summary-only-empty-cases' 'CANDIDATE_COUNT' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.cases=@()}}
Negative 'failed-case-with-green-total' 'CANDIDATE_CASE' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.cases[0].status='FAIL'}}
Negative 'duplicate-case' 'CANDIDATE_DUPLICATE' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.cases[1]=$d.cases[0]}}
Negative 'changed-check-total' 'CHECK_TOTAL' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.cases[0].checks=999}}
Negative 'old-candidate-result' 'CANDIDATE_RESULT' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.candidateHostSha256='a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539'}}
Negative 'wrong-backend-pin' 'BACKEND_OR_HOST' {param($p,$e,$s) $p.newBackendDllSha256='f8fdbd516d2a002fa65e1cad5bc02405f16ec7b04208d4f6a8c2d26f330dea09'}
Negative 'missing-original-six-failures' 'OLD_WRITE_COUNT' {param($p,$e,$s) ChangeOutput $e 'old-write' {param($d) $d.cases=@($d.cases|Select-Object -First 5)}}
Negative 'original-failure-marked-pass' 'OLD_WRITE_CASE' {param($p,$e,$s) ChangeOutput $e 'old-write' {param($d) $d.cases[0].status='PASS'}}
Negative 'golden-conversion-changed' 'GOLDEN_CHANGED' {param($p,$e,$s) ChangeOutput $e 'old-write' {param($d) $d.goldenWrites[2].raw=2}}
Negative 'missing-original-busy' 'OLD_BUSY_MISSING' {param($p,$e,$s) ChangeOutput $e 'old-coexistence' {param($d) $d.cases[1].error='unrelated failure'}}
Negative 'wrong-exact-msi-candidate' 'MSI_RESULT' {param($p,$e,$s) ChangeOutput $e 'msi' {param($d) $d.candidateHostSha256='a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539'}}
Negative 'partial-msi-cases' 'MSI_RESULT' {param($p,$e,$s) ChangeOutput $e 'msi' {param($d) $d.cases=@($d.cases|Select-Object -First 24)}}
Negative 'native-attempt' 'CANDIDATE_RESULT' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.nativeAttempts=1}}
Negative 'partial-command-set' 'EXECUTION_STEPS' {param($p,$e,$s) $e.steps=@($e.steps|Select-Object -First 3)}
Negative 'command-nonzero-exit' 'PROCESS_EXIT' {param($p,$e,$s) $e.steps[0].exitCode=2}
Negative 'command-timeout' 'PROCESS_EXIT' {param($p,$e,$s) $e.steps[0].timedOut=$true;$e.steps[0].naturalExit=$false}
Negative 'stale-child-result' 'RESULT_STALE' {param($p,$e,$s) [IO.File]::SetLastWriteTimeUtc($e.steps[0].output.path,[DateTime]::UtcNow.AddDays(-2))}
Negative 'command-hash-mismatch' 'PROOF_HASH' {param($p,$e,$s) $e.steps[0].output.sha256='0'*64}
Negative 'actual-old-failure-not-executed' 'OLD_NOT_EXECUTED' {param($p,$e,$s) $e.steps[0].arguments[1]='candidate'}
Negative 'wrong-actual-candidate-command' 'WRONG_EXECUTED_CANDIDATE' {param($p,$e,$s) $e.steps[2].arguments[1]=$e.steps[0].arguments[2]}
Negative 'missing-required-source' 'SOURCE_INVENTORY' {param($p,$e,$s) $s.sourceHashes=@($s.sourceHashes|Where-Object path -cne 'FanLab/real-host/OneXFanDeviceLifecycle.cs')}
function LinkedSourceFixture($p,[string]$Label,[string]$Relative,[bool]$Missing){
 $fixture=[IO.Path]::GetFullPath((Join-Path $root ('linked-'+$Label)))
 if(-not $fixture.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $fixture)){throw 'Fresh linked source fixture required'}
 [IO.Directory]::CreateDirectory((Join-Path $fixture 'capabilities'))|Out-Null
 foreach($source in @('capabilities/FanCapabilityCatalog.cs','capabilities/AsusFanCapabilitySession.cs')){Copy-Item -LiteralPath (Join-Path $repo $source) -Destination (Join-Path $fixture $source)}
 $victim=[IO.Path]::GetFullPath((Join-Path $fixture $Relative))
 if(-not $victim.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe linked source fixture mutation'}
 if($Missing){Remove-Item -LiteralPath $victim}else{[IO.File]::AppendAllText($victim,"`n// isolated linked source mutation`n",[Text.UTF8Encoding]::new($false))}
 $p.repoRoot=$fixture
}
Negative 'linked-catalog-missing' 'LINKED_SOURCE_MISSING' {param($p,$e,$s) LinkedSourceFixture $p 'catalog-missing' 'capabilities/FanCapabilityCatalog.cs' $true}
Negative 'linked-catalog-changed' 'LINKED_SOURCE_CHANGED' {param($p,$e,$s) LinkedSourceFixture $p 'catalog-changed' 'capabilities/FanCapabilityCatalog.cs' $false}
Negative 'linked-session-missing' 'LINKED_SOURCE_MISSING' {param($p,$e,$s) LinkedSourceFixture $p 'session-missing' 'capabilities/AsusFanCapabilitySession.cs' $true}
Negative 'linked-session-changed' 'LINKED_SOURCE_CHANGED' {param($p,$e,$s) LinkedSourceFixture $p 'session-changed' 'capabilities/AsusFanCapabilitySession.cs' $false}
Negative 'source-pin-changed' 'SOURCE_CHANGED' {param($p,$e,$s) $s.sourceHashes[0].sha256='0'*64}
Negative 'source-path-traversal' 'SOURCE_PATH' {param($p,$e,$s) $s.sourceHashes[0].path='../outside'}
Negative 'suite-artifact-drift' 'SUITE_DRIFT' {param($p,$e,$s) $s.candidateChanged=$true}
Negative 'v1-is-not-new-capability-contract' 'CONTRACT' {param($p,$e,$s) $p.kind='host-only-v1'}
Negative 'string-bool-is-not-scope-proof' 'BOOLEAN_SCHEMA' {param($p,$e,$s) $s.hardwareIo='false'}
Negative 'string-natural-exit-is-not-proof' 'PROCESS_BOOLEAN_SCHEMA' {param($p,$e,$s) $e.steps[0].naturalExit='true'}
Negative 'string-checks-are-not-executed-count' 'CHECK_NUMBER_SCHEMA' {param($p,$e,$s) ChangeOutput $e 'candidate' {param($d) $d.cases[0].checks=[string]$d.cases[0].checks}}
Negative 'string-golden-is-not-numeric-evidence' 'GOLDEN_NUMBER_SCHEMA' {param($p,$e,$s) ChangeOutput $e 'old-write' {param($d) $d.goldenWrites[0].raw='0'}}
# Exercise the actual connector's early rejection against copied ordinary files.
# Full success/rollback still requires a genuine current nine-suite gate.
foreach($failure in @('missing-capability-proof','wrong-candidate-summary')){
 $fixture=Join-Path $root ('connector-'+$failure);[IO.Directory]::CreateDirectory($fixture)|Out-Null
 $records=@($plan.changes|ForEach-Object {[pscustomobject]@{relative=$_.relative;sha256=$_.beforeSha256}})+@($plan.preserved)
 foreach($row in $records){
  if([IO.Path]::IsPathRooted($row.relative) -or $row.relative.Replace('\','/') -match '(^|/)\.\.?(/|$)'){throw 'Unsafe connector test fixture'}
  $source=Join-Path $plan.installRoot $row.relative;$target=[IO.Path]::GetFullPath((Join-Path $fixture $row.relative))
  if(-not $target.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Connector test path escaped fixture'}
  [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;Copy-Item -LiteralPath $source -Destination $target
 }
 $testPlan=Clone $plan;$testPlan.installRoot=$fixture;$testPlan.evidenceDir=Join-Path $root ($failure+'-connector-receipt')
 if($failure -eq 'missing-capability-proof'){$testPlan.capabilityEvidence=$null;$expected='HOST_CAPABILITY_CONTRACT'}
 else{$badSuite=Clone $suite;$badSuite.candidate.checks=999;$badFile=Join-Path $root 'wrong-suite-for-connector.json';WriteJson $badFile $badSuite;$testPlan.capabilityEvidence.suite=Descriptor $badFile;$expected='HOST_CAPABILITY_SUITE_CANDIDATE'}
 $testFile=Join-Path $root ($failure+'-connector-plan.json');WriteJson $testFile $testPlan
 $engine=Join-Path $PSHOME 'pwsh.exe';$connector=Join-Path $PSScriptRoot 'connect-installed-components.ps1'
 & $engine -NoProfile -File $connector -PlanPath $testFile *> (Join-Path $root ($failure+'-connector.log'));$code=$LASTEXITCODE
 $receipt=Get-Content -LiteralPath (Join-Path $testPlan.evidenceDir 'verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Check ($code -eq 2 -and $receipt.passed -eq $false -and $receipt.error.Contains($expected) -and @($receipt.changedFiles).Count -eq 0 -and -not $receipt.rollbackPerformed) ('actual connector rejects '+$failure+' before replacement')
 $drift=@($records|Where-Object {(Hash (Join-Path $fixture $_.relative)) -cne $_.sha256})
 Check ($drift.Count -eq 0) ('actual connector leaves every copied file intact for '+$failure)
}
$verified=Assert-YmccHostCapabilityEvidence -Plan $plan
Check ($verified.status -eq 'PASS') 'original current evidence still passes after negative copies'
$report=[ordered]@{schemaVersion=1;status='PASS';checks=@($checks);checksPassed=$checks.Count;capability=$verified;execution=$executionPath;executionSha256=Hash $executionPath;verifiedPlan=$proofPlan;installedApplicationMutated=$false;hardwareIo=$false;hostMainStarted=$false}
WriteJson (Join-Path $root 'result.json') $report
Write-Output ('HOST_CAPABILITY_EVIDENCE_TEST PASS '+$checks.Count)
