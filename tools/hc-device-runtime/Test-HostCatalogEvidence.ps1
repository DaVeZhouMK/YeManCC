[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$VerifierPath,[Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
. $VerifierPath
$plan=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
$sourceExecution=Get-Content -LiteralPath $plan.capabilityEvidence.execution.path -Raw -Encoding UTF8|ConvertFrom-Json
$root=Join-Path $EvidenceRoot ([guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($root)|Out-Null
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Clone($Value){$Value|ConvertTo-Json -Depth 30|ConvertFrom-Json}
function WriteJson([string]$Path,$Value){$Value|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $Path -Encoding UTF8}
function Descriptor([string]$Path){[pscustomobject]@{path=$Path;sha256=(Hash $Path)}}
$cases=[Collections.Generic.List[object]]::new()
$positive=Assert-YmccHostCapabilityEvidence -Plan $plan
$cases.Add([pscustomobject]@{id='positive-reviewed-exact-catalog-candidate';status='PASS';actual=$positive})
function ChangeStepReport($Execution,[string]$Id,[scriptblock]$Mutation){
 $step=@($Execution.steps|Where-Object id -CEQ $Id)[0];$original=[string]$step.output.path
 $report=Get-Content -LiteralPath $original -Raw -Encoding UTF8|ConvertFrom-Json
 & $Mutation $report|Out-Null
 $newFile=Join-Path $script:caseDir ($Id+'-changed.json');WriteJson $newFile $report
 [IO.File]::SetLastWriteTimeUtc($newFile,(Get-Item -LiteralPath $original).LastWriteTimeUtc)
 $step.output=Descriptor $newFile
 $step.arguments=@($step.arguments|ForEach-Object {if($_ -ceq $original){$newFile}else{$_}})
}
function ChangeProof($Candidate,[string]$Key,[scriptblock]$Mutation){
 $report=Get-Content -LiteralPath $Candidate.capabilityEvidence.$Key.path -Raw -Encoding UTF8|ConvertFrom-Json
 & $Mutation $report|Out-Null
 $file=Join-Path $script:caseDir ($Key+'-changed.json');WriteJson $file $report
 $Candidate.capabilityEvidence.$Key=Descriptor $file
}
function Negative([string]$Id,[string]$Expected,[scriptblock]$Mutation,[string]$Prefix='HOST_CATALOG_'){
 $script:caseDir=Join-Path $root $Id;[IO.Directory]::CreateDirectory($script:caseDir)|Out-Null
 $candidate=Clone $plan;$execution=Clone $sourceExecution
 & $Mutation $candidate $execution|Out-Null
 $file=Join-Path $script:caseDir 'execution.json';WriteJson $file $execution;$candidate.capabilityEvidence.execution=Descriptor $file
 $planFile=Join-Path $script:caseDir 'plan.json';WriteJson $planFile $candidate
 $reason=$null
 try{Assert-YmccHostCapabilityEvidence -Plan $candidate|Out-Null}catch{$reason=$_.Exception.Message}
 $passed=$reason -ceq ($Prefix+$Expected)
 $cases.Add([pscustomobject]@{id=$Id;status=$(if($passed){'PASS'}else{'FAIL'});expectedError=($Prefix+$Expected);actualError=$reason;plan=(Descriptor $planFile)})
 Write-Output ((@('FAIL','PASS')[[int]$passed])+' '+$Id+': '+$reason)
}
Negative 'old-kind-cannot-borrow-new-contract' 'CONTRACT' {param($p,$e)$p.kind='host-only-v2'} 'HOST_CAPABILITY_'
Negative 'old-contract-cannot-borrow-new-kind' 'CONTRACT' {param($p,$e)$p.capabilityEvidence.contract='onex-strict-fan-write-v1'}
Negative 'unknown-kind-refused' 'CONTRACT' {param($p,$e)$p.kind='host-only-v4'} 'HOST_CAPABILITY_'
Negative 'old-host-new-catalog-refused' 'FIXED_CANDIDATE_TUPLE' {param($p,$e)$p.newHostDllSha256='6b2f96972a47515bbe06e820e4196a22897782b7314adc03553f9b8e0aea9cf3'}
Negative 'old-backend-new-catalog-refused' 'FIXED_CANDIDATE_TUPLE' {param($p,$e)$p.newBackendDllSha256='211dce9f5594ff2a040ce3ac7824135f0e4df23987edcda563d038050d1dd04d'}
Negative 'unreviewed-runtime-refused' 'FIXED_CANDIDATE_TUPLE' {param($p,$e)$p.newRuntimeManifestSha256='0'*64}
Negative 'old-source-bundle-refused' 'SOURCE_BUNDLE_PIN' {param($p,$e)$p.capabilityEvidence.sourceBundle.sha256='6cc777b57a44536835a3e6bbe4913392589b7f06596789e873f400d46826ed1e'}
Negative 'unselected-source-root-refused' 'SOURCE_SELECTION' {param($p,$e)$p.componentSourceRoot=$p.repoRoot}
Negative 'missing-linked-compile-input' 'COMPILED_INVENTORY' {param($p,$e)$p.compiledInputs=@($p.compiledInputs|Where-Object path -CNE 'capabilities/FanCapabilityCatalog.cs')}
Negative 'changed-linked-compile-input' 'COMPILED_BINDING' {param($p,$e)@($p.compiledInputs|Where-Object path -CEQ 'capabilities/FanCapabilityCatalog.cs')[0].sha256='0'*64}
Negative 'missing-pdb-source-document' 'PDB_SOURCE_SET' {param($p,$e)ChangeProof $p 'pdbDocuments' {param($d)$d.pairs[0].documents=@($d.pairs[0].documents|Select-Object -Skip 1)}}
Negative 'wrong-portable-pdb-pair' 'PDB_PORTABLE_PAIR' {param($p,$e)ChangeProof $p 'portablePdb' {param($d)$d.pairs[0].pdbSha256='0'*64}}
Negative 'original-generated-apphost-variant-not-admitted' 'PAYLOAD_IDENTITY' {param($p,$e)$p.payloadRoot='D:\Codex\2026-10-18\kai-s\work\catalog-host-candidate-20261019\candidate-payload-msi-selftest-fix'}
Negative 'mixed-execution-candidate' 'EXECUTION_CANDIDATE' {param($p,$e)$e.candidate.backendSha256='0'*64}
Negative 'string-false-cannot-disable-hardware-flag' 'EXECUTION_SCOPE' {param($p,$e)$e.hardwareIo='false'}
Negative 'partial-execution-input-closure' 'EXECUTION_INPUT_INVENTORY' {param($p,$e)$e.inputs=@($e.inputs|Select-Object -Skip 1)}
Negative 'partial-consumer-command-set' 'EXECUTION_STEPS' {param($p,$e)$e.steps=@($e.steps|Where-Object id -CNE 'roundtrip')}
Negative 'duplicate-consumer-command' 'EXECUTION_STEPS' {param($p,$e)$e.steps[1]=$e.steps[0]}
Negative 'missing-actual-process-id' 'PROCESS_EXIT' {param($p,$e)$e.steps[0].pid=0}
Negative 'forced-process-exit-refused' 'PROCESS_EXIT' {param($p,$e)$e.steps[0].naturalExit=$false}
Negative 'string-process-boolean-refused' 'PROCESS_BOOLEAN_SCHEMA' {param($p,$e)$e.steps[0].timedOut='false'}
Negative 'nonfresh-output-refused' 'PROCESS_EXIT' {param($p,$e)$e.steps[0].resultFresh=$false}
Negative 'unreviewed-harness-refused' 'HARNESS_IDENTITY' {param($p,$e)$e.steps[0].harness.sha256='0'*64}
Negative 'actual-old-host-in-candidate-command' 'WRONG_EXECUTED_CANDIDATE' {param($p,$e)@($e.steps|Where-Object id -CEQ 'candidate')[0].arguments[1]=@($e.steps|Where-Object id -CEQ 'old-coexistence')[0].arguments[2]}
Negative 'summary-green-partial-candidate-cases' 'CANDIDATE_COUNT' {param($p,$e)ChangeStepReport $e 'candidate' {param($d)$d.cases=@($d.cases|Select-Object -Skip 1)}}
Negative 'candidate-failure-with-green-summary' 'CANDIDATE_CASE' {param($p,$e)ChangeStepReport $e 'candidate' {param($d)$d.cases[0].status='FAIL'}}
Negative 'candidate-check-total-mismatch' 'CHECK_TOTAL' {param($p,$e)ChangeStepReport $e 'candidate' {param($d)$d.cases[0].checks=999}}
Negative 'old-write-fault-omission' 'OLD_WRITE_COUNT' {param($p,$e)ChangeStepReport $e 'old-write' {param($d)$d.cases=@($d.cases|Select-Object -Skip 1)}}
Negative 'golden-duty-conversion-changed' 'GOLDEN_CHANGED' {param($p,$e)ChangeStepReport $e 'old-write' {param($d)$d.goldenWrites[2].raw=9}}
Negative 'msi-result-bound-to-old-host' 'MSI_RESULT' {param($p,$e)ChangeStepReport $e 'msi' {param($d)$d.candidateHostSha256='6b2f96972a47515bbe06e820e4196a22897782b7314adc03553f9b8e0aea9cf3'}}
Negative 'dispatch-one-route-missing' 'DISPATCH_RESULT' {param($p,$e)ChangeStepReport $e 'dispatch' {param($d)$d.registeredFanRoutes=69}}
Negative 'dispatch-row-failed-with-green-summary' 'DISPATCH_CASES' {param($p,$e)ChangeStepReport $e 'dispatch' {param($d)$d.rows[0].ok=$false}}
Negative 'string-catalog-counter-refused' 'CATALOG_NUMBER_SCHEMA' {param($p,$e)ChangeStepReport $e 'dispatch' {param($d)$d.checks='601'}}
Negative 'string-catalog-boolean-refused' 'CATALOG_BOOLEAN_SCHEMA' {param($p,$e)ChangeStepReport $e 'dispatch' {param($d)$d.hardwareIo='false'}}
Negative 'roundtrip-cannot-claim-loaded-host-dll' 'ROUNDTRIP_RESULT' {param($p,$e)ChangeStepReport $e 'roundtrip' {param($d)$d.hostConsumerEvidence='loaded149 Host DLL'}}
Negative 'public-forced-cleanup-refused' 'PUBLIC_SCOPE' {param($p,$e)ChangeProof $p 'publicConnection' {param($d)$d.forcedFixtureCleanup=$true}}
Negative 'missing-real-manager-constructor' 'PUBLIC_MANAGER_PROOF' {param($p,$e)ChangeProof $p 'publicConnection' {param($d)@($d.checks|Where-Object name -CEQ 'real 13 managers and real known-bad audio regression')[0].detail.approvedManagerCount=12}}
Negative 'missing-known-bad-audio-reproduction' 'PUBLIC_MANAGER_PROOF' {param($p,$e)ChangeProof $p 'publicConnection' {param($d)@($d.checks|Where-Object name -CEQ 'real 13 managers and real known-bad audio regression')[0].detail.knownBadReproduced=$false}}
Negative 'public-host-is-wrong-version' 'PUBLIC_HOST_BINDING' {param($p,$e)ChangeProof $p 'publicConnection' {param($d)@($d.checks|Where-Object name -CEQ 'real-manager preflight and Host arguments bind unchanged HC input')[0].detail.hostSha256='6b2f96972a47515bbe06e820e4196a22897782b7314adc03553f9b8e0aea9cf3'}}

Negative 'failed149-retired-tuple-cannot-be-installed' 'FIXED_CANDIDATE_TUPLE' {param($p,$e)$p.newHostDllSha256='149042f482b122adc85a19a905c8579c75cfbc881cd78d85c221aac1fe5cb424'}
Negative 'missing-full-host-selftest' 'PROOF_DESCRIPTOR' {param($p,$e)$p.capabilityEvidence.fullHostSelfTest=$null}
Negative 'failed-full-host-selftest-refused' 'FULL_HOST_CANDIDATE' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.status='FAIL'}}
Negative 'full-host-namespace-negative-missing' 'FULL_HOST_CHECKS' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.actualResult.checks=@($d.actualResult.checks|Where-Object {$_ -cne 'fan934-msi-unregistered-namespace-denied'})}}
Negative 'full-host-string-counter-refused' 'FULL_HOST_CHECKS' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.checksPassed='428'}}
Negative 'full-host-nonnatural-exit-refused' 'FULL_HOST_EXECUTION' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.naturalExit=$false}}
Negative 'full-host-real-backend-request-refused' 'FULL_HOST_EXECUTION' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.realBackendRequested=$true}}
Negative 'full-host-string-bool-refused' 'FULL_HOST_BOOLEAN_SCHEMA' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.timedOut='false'}}
Negative 'full-host-unbound-raw-output-refused' 'FULL_HOST_LOG_CHANGED' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.stdout.sha256='0'*64}}
Negative 'full-host-faked-green-json-vs-raw-log-refused' 'FULL_HOST_RAW_RESULT' {param($p,$e)ChangeProof $p 'fullHostSelfTest' {param($d)$d.actualResult.checks[0]='fake-green-assertion'}}
Negative 'roundtrip-host-source-omission-refused' 'ROUNDTRIP_HOST_SOURCE_BINDING' {param($p,$e)ChangeProof $p 'roundtripHostSources' {param($d)@($d.documents|Where-Object hostSource -EQ $true)[0].hostSource=$false}}
Negative 'roundtrip-source-proof-cannot-claim-loaded98' 'ROUNDTRIP_SOURCE_PROOF' {param($p,$e)ChangeProof $p 'roundtripHostSources' {param($d)$d.candidateHostDllLoadedByThisTest=$true}}
$failed=@($cases|Where-Object status -CNE 'PASS')
$result=[ordered]@{schemaVersion=1;status=$(if($failed.Count){'FAIL'}else{'PASS'});powerShellVersion=$PSVersionTable.PSVersion.ToString();cases=$cases.ToArray();checks=$cases.Count;failed=$failed.Count;verifier=(Descriptor $VerifierPath);plan=(Descriptor $PlanPath);hostMainStarted=$false;installedApplicationMutated=$false;sourceOrRuntimeChanged=$false}
$file=Join-Path $root 'verification.json';WriteJson $file $result
[pscustomobject]@{status=$result.status;checks=$cases.Count;failed=$failed.Count;output=$file}|ConvertTo-Json
if($failed.Count){exit 1}
