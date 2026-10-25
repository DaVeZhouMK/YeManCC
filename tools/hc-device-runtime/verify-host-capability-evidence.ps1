# Read-only capability evidence gate. This never starts a process or grants installation authority.
function Assert-YmccHostCapabilityEvidence {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)]$Plan)
 if ($Plan.kind -ceq 'host-only-v3') {
  $catalogVerifier = Join-Path $PSScriptRoot 'verify-host-catalog-evidence.ps1'
  if (-not (Test-Path -LiteralPath $catalogVerifier -PathType Leaf)) { throw 'HOST_CATALOG_VERIFIER_MISSING' }
  . $catalogVerifier
  return (Assert-YmccHostCatalogEvidence -Plan $Plan)
 }
 $ErrorActionPreference='Stop'
 function Need([bool]$Condition,[string]$Code){if(-not $Condition){throw ('HOST_CAPABILITY_'+$Code)}}
 function Number($Value){return $null -ne $Value -and ($Value -is [byte] -or $Value -is [int16] -or $Value -is [int32] -or $Value -is [int64] -or $Value -is [uint16] -or $Value -is [uint32] -or $Value -is [uint64] -or $Value -is [single] -or $Value -is [double] -or $Value -is [decimal])}
 function HashFile([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
 function ReadProof($Record){
  Need ($null -ne $Record -and $Record.path -is [string] -and $Record.sha256 -is [string] -and $Record.sha256 -cmatch '^[a-f0-9]{64}$') 'PROOF_DESCRIPTOR'
  $p=[IO.Path]::GetFullPath($Record.path)
  Need ([IO.Path]::GetExtension($p) -ieq '.json' -and (Test-Path -LiteralPath $p -PathType Leaf)) 'PROOF_FILE'
  Need (((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'PROOF_REPARSE'
  $bytes=[IO.File]::ReadAllBytes($p);Need ($bytes.Length -gt 0 -and $bytes.Length -le 16777216) 'PROOF_SIZE'
  $algorithm=[Security.Cryptography.SHA256]::Create()
  try{$hash=([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$algorithm.Dispose()}
  Need ($hash -ceq $Record.sha256) 'PROOF_HASH'
  return [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)|ConvertFrom-Json
 }
 function TimeValue($Value){if($Value -is [DateTimeOffset]){return $Value};if($Value -is [DateTime]){return [DateTimeOffset]::new($Value)};return [DateTimeOffset]::Parse([string]$Value,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)}
 function RepoFile([string]$Relative){
  Need ($Relative -and -not [IO.Path]::IsPathRooted($Relative) -and -not $Relative.Contains(':') -and $Relative.Replace('\','/') -notmatch '(^|/)\.\.?(/|$)') 'SOURCE_PATH'
  $root=[IO.Path]::GetFullPath($Plan.repoRoot).TrimEnd('\');$file=[IO.Path]::GetFullPath((Join-Path $root $Relative))
  Need ($file.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) 'SOURCE_PATH'
  return $file
 }
 function ExactRows($Rows,[string[]]$Ids,[string]$Status,[string]$Code){
  Need ($null -ne $Rows -and @($Rows).Count -eq $Ids.Count) ($Code+'_COUNT')
  Need (@($Rows|Group-Object id|Where-Object Count -ne 1).Count -eq 0) ($Code+'_DUPLICATE')
  foreach($id in $Ids){Need (@($Rows|Where-Object {$_.id -ceq $id -and $_.status -ceq $Status}).Count -eq 1) ($Code+'_CASE')}
 }
 $cap=$Plan.capabilityEvidence
 Need ($Plan.kind -ceq 'host-only-v2' -and $cap.schemaVersion -eq 1 -and $cap.contract -ceq 'onex-strict-fan-write-v1') 'CONTRACT'
 Need ($Plan.newHostDllSha256 -cmatch '^[a-f0-9]{64}$' -and $Plan.newBackendDllSha256 -cmatch '^[a-f0-9]{64}$') 'CANDIDATE_PIN'
 $suite=ReadProof $cap.suite;$execution=ReadProof $cap.execution
 foreach($flag in @($suite.hardwareIo,$suite.hostMainStarted,$suite.installedApplicationMutated,$suite.candidate.builtFromCurrentSources,$suite.candidate.installed,$suite.candidateChanged,$execution.hostMainStarted,$execution.hardwareIo,$execution.installedApplicationMutated)){Need ($flag -is [bool]) 'BOOLEAN_SCHEMA'}
 Need ($suite.kind -ceq 'onex-fan-lighting-coexistence' -and $suite.status -ceq 'PASS' -and $suite.hardwareIo -eq $false -and $suite.hostMainStarted -eq $false -and $suite.installedApplicationMutated -eq $false) 'SUITE_SCOPE'
 Need ($suite.candidate.sha256 -ceq $Plan.newHostDllSha256 -and $suite.candidate.builtFromCurrentSources -eq $true -and $suite.candidate.installed -eq $false -and $suite.candidate.cases -eq 69 -and $suite.candidate.checks -eq 580) 'SUITE_CANDIDATE'
 Need (@($suite.sourceChanges).Count -eq 0 -and @($suite.externalInputChanges).Count -eq 0 -and $suite.candidateChanged -eq $false) 'SUITE_DRIFT'
 Need (@($suite.sourceHashes).Count -ge 66 -and @($suite.sourceHashes|Group-Object path|Where-Object Count -ne 1).Count -eq 0) 'SOURCE_INVENTORY'
 # These two sources are explicit Compile Includes outside the project directories.
 # Their SHA256 values were read from the portable PDB paired by PE CodeView GUID/stamp
 # with the approved 211dce9f Backend. Keep historical suite/plan inventories unchanged.
 $approvedLinkedSources=[ordered]@{
  'capabilities/FanCapabilityCatalog.cs'='c29f450a28dc6cb51a234f66e22f2442928327468bed7e1b737c6bcfd93f47aa'
  'capabilities/AsusFanCapabilitySession.cs'='bd247a4a525fe9dfb4b37108f2fe6d14e8c33265ef32fc9f78f5629761effee5'
 }
 foreach($relative in $approvedLinkedSources.Keys){
  $sourceFile=RepoFile $relative
  Need (Test-Path -LiteralPath $sourceFile -PathType Leaf) 'LINKED_SOURCE_MISSING'
  Need ((HashFile $sourceFile) -ceq $approvedLinkedSources[$relative]) 'LINKED_SOURCE_CHANGED'
 }
 foreach($row in $suite.sourceHashes){Need ($row.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile (RepoFile $row.path)) -ceq $row.sha256) 'SOURCE_CHANGED'}
 foreach($relative in @('FanLab/real-host/Program.cs','FanLab/real-host/OneXFanDeviceLifecycle.cs','FanLab/real-host/MsiFanDeviceLifecycle.cs','tools/ymcc_sim/global/onex-fan-coexistence/Harness.cs','tools/ymcc_sim/global/onex-fan-coexistence/StrictWriteCases.cs','tools/ymcc_sim/global/onex-fan-coexistence/OldWriteProbe.cs','tools/ymcc_sim/global/onex-fan-coexistence/fixtures/host-a678-write-failure/YeManFanHost.dll','tools/ymcc_sim/global/onex-fan-coexistence/fixtures/host-a678-write-failure/YeManFanBackend.dll')){
  Need (@($suite.sourceHashes|Where-Object path -ceq $relative).Count -eq 1) 'SOURCE_REQUIRED'
 }
 $compiled=@($Plan.compiledInputs);Need ($compiled.Count -ge 27 -and @($compiled|Group-Object path|Where-Object Count -ne 1).Count -eq 0) 'COMPILED_INVENTORY'
 foreach($row in $compiled){
  if($approvedLinkedSources.Contains($row.path)){Need ($row.sha256 -ceq $approvedLinkedSources[$row.path]) 'COMPILED_BINDING'}
  else{Need (@($suite.sourceHashes|Where-Object {$_.path -ceq $row.path -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'COMPILED_BINDING'}
 }
 $boundCompiled=@($compiled)
 foreach($relative in $approvedLinkedSources.Keys){
  if(@($compiled|Where-Object path -ceq $relative).Count -eq 0){$boundCompiled+=[pscustomobject]@{path=$relative;sha256=$approvedLinkedSources[$relative]}}
 }
 $expectedCompiled=@('FanLab/real-host/YeManFanHost.csproj','FanLab/fan-backend/YeManFanBackend.csproj','native/ProcessEfficiency.cs','FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs')
 foreach($relativeRoot in @('FanLab/real-host','FanLab/fan-backend')){
  foreach($file in Get-ChildItem -LiteralPath (RepoFile $relativeRoot) -Recurse -File -Filter '*.cs'|Where-Object {$_.FullName -notmatch '[\\/](bin|obj)[\\/]'} ){
   $expectedCompiled+=$file.FullName.Substring(([IO.Path]::GetFullPath($Plan.repoRoot).TrimEnd('\')).Length+1).Replace('\','/')
  }
 }
 foreach($projectRelative in @('FanLab/real-host/YeManFanHost.csproj','FanLab/fan-backend/YeManFanBackend.csproj')){
  $projectPath=RepoFile $projectRelative
  [xml]$projectXml=Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
  foreach($include in $projectXml.SelectNodes('//*[local-name()="Compile" and @Include]')){
   $relativeInclude=[string]$include.Include
   Need ($relativeInclude -notmatch '[*?$]' -and -not [IO.Path]::IsPathRooted($relativeInclude)) 'COMPILED_INCLUDE_UNSUPPORTED'
   $includedPath=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $projectPath) $relativeInclude))
   $repoPrefix=[IO.Path]::GetFullPath($Plan.repoRoot).TrimEnd('\')+'\'
   Need ($includedPath.StartsWith($repoPrefix,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($includedPath) -ieq '.cs') 'COMPILED_INCLUDE_UNSAFE'
   $expectedCompiled+=$includedPath.Substring($repoPrefix.Length).Replace('\','/')
  }
 }
 Need ((Compare-Object @($expectedCompiled|Sort-Object -Unique) @($boundCompiled.path|Sort-Object)) -eq $null) 'COMPILED_CLOSURE'

 foreach($row in @($suite.externalInputs)+@($suite.hostPayloadFiles)){
  Need ($row.path -and $row.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile $row.path) -ceq $row.sha256) 'INPUT_CHANGED'
 }
 $payload=[IO.Path]::GetFullPath($suite.hostPayloadRoot).TrimEnd('\')
 Need ((HashFile (Join-Path $payload 'YeManFanHost.dll')) -ceq $Plan.newHostDllSha256 -and (HashFile (Join-Path $payload 'YeManFanBackend.dll')) -ceq $Plan.newBackendDllSha256) 'BACKEND_OR_HOST'
 Need ($execution.schemaVersion -eq 1 -and $execution.status -ceq 'PASS' -and $execution.kind -ceq 'host-capability-execution-v1' -and $execution.hostMainStarted -eq $false -and $execution.hardwareIo -eq $false -and $execution.installedApplicationMutated -eq $false) 'EXECUTION_SCOPE'
 Need ($execution.candidateHostSha256 -ceq $Plan.newHostDllSha256 -and $execution.candidateBackendSha256 -ceq $Plan.newBackendDllSha256) 'EXECUTION_CANDIDATE'
 Need (@($execution.inputChanges).Count -eq 0 -and @($execution.inputs).Count -ge 10) 'EXECUTION_INPUTS'
 foreach($row in $execution.inputs){Need ($row.path -and $row.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile $row.path) -ceq $row.sha256) 'EXECUTION_INPUT_CHANGED'}
 foreach($harness in @($cap.oneXHarness,$cap.msiHarness)){
  Need ($harness.path -and $harness.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile $harness.path) -ceq $harness.sha256) 'HARNESS_IDENTITY'
  Need (@($execution.inputs|Where-Object {$_.path -ceq $harness.path -and $_.sha256 -ceq $harness.sha256}).Count -eq 1) 'HARNESS_NOT_EXECUTED'
 }
 $steps=@($execution.steps);Need ($steps.Count -eq 4 -and @($steps|Group-Object id|Where-Object Count -ne 1).Count -eq 0) 'EXECUTION_STEPS'
 $results=@{}
 $fixed='tools/ymcc_sim/global/onex-fan-coexistence/fixtures/'
 $hcPin='08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'
 $helperPin='e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf'
 $oldHostPin='7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f'
 $oldWritePin='a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539'
 $oldBackendPin='6d2bcbd48498b7826db64d77b6d5d9d3f45bb91917d816b97c7a830a1eaa84db'
 $oldMsiPin='5e94170105c65bb0503751460082db2a0827061b075c55afe92f7ef9b3b91993'
 $oldHost=RepoFile ($fixed+'host7f-light-e546/YeManFanHost.dll');$oldWrite=RepoFile ($fixed+'host-a678-write-failure/YeManFanHost.dll');$helper=RepoFile ($fixed+'host7f-light-e546/YeManLightSetter.dll')
 Need ((HashFile $oldHost) -ceq $oldHostPin -and (HashFile $oldWrite) -ceq $oldWritePin -and (HashFile (RepoFile ($fixed+'host-a678-write-failure/YeManFanBackend.dll'))) -ceq $oldBackendPin -and (HashFile $helper) -ceq $helperPin) 'FIXED_NEGATIVE_IDENTITY'
 foreach($id in @('old-coexistence','old-write','candidate','msi')){
  $selected=@($steps|Where-Object id -ceq $id);Need ($selected.Count -eq 1) 'STEP_REQUIRED';$step=$selected[0]
  Need ($step.exitCode -eq 0 -and $step.naturalExit -eq $true -and $step.timedOut -eq $false -and $step.pid -gt 0 -and $step.resultFresh -eq $true) 'PROCESS_EXIT'
  foreach($flag in @($step.naturalExit,$step.timedOut,$step.resultFresh)){Need ($flag -is [bool]) 'PROCESS_BOOLEAN_SCHEMA'}
  Need ((Number $step.exitCode) -and (Number $step.pid)) 'PROCESS_NUMBER_SCHEMA'
  $start=TimeValue $step.startedUtc;$finish=TimeValue $step.finishedUtc
  Need ($finish -ge $start -and ($finish-$start).TotalSeconds -le 90 -and $finish -le [DateTimeOffset]::UtcNow.AddSeconds(5)) 'PROCESS_TIME'
  $mtime=(Get-Item -LiteralPath $step.output.path).LastWriteTimeUtc
  Need ($mtime -ge $start.UtcDateTime.AddSeconds(-1) -and $mtime -le $finish.UtcDateTime.AddSeconds(1)) 'RESULT_STALE'
  Need ((HashFile $step.executable.path) -ceq $step.executable.sha256 -and [IO.Path]::GetFileName($step.executable.path) -ieq 'dotnet.exe') 'PROCESS_IMAGE'
  $args=@($step.arguments);$expectedHarness=if($id -eq 'msi'){$cap.msiHarness.path}else{$cap.oneXHarness.path}
  Need ($args.Count -ge 5 -and $args[0] -ceq $expectedHarness -and $args[-1] -ceq $step.output.path -and @($args|Where-Object {$_ -match '^--'}).Count -eq 0) 'PROCESS_ARGUMENTS'
  if($id -eq 'old-coexistence'){Need ($args.Count -eq 6 -and $args[1] -ceq 'old' -and $args[2] -ceq $oldHost -and $args[3] -ceq $helper -and (HashFile $args[4]) -ceq $hcPin) 'OLD_NOT_EXECUTED'}
  elseif($id -eq 'old-write'){Need ($args.Count -eq 5 -and $args[1] -ceq 'old-write' -and $args[2] -ceq $oldWrite -and (HashFile $args[3]) -ceq $hcPin) 'OLD_WRITE_NOT_EXECUTED'}
  elseif($id -eq 'candidate'){Need ($args.Count -eq 5 -and (HashFile $args[1]) -ceq $Plan.newHostDllSha256 -and $args[2] -ceq $helper -and (HashFile $args[3]) -ceq $hcPin) 'WRONG_EXECUTED_CANDIDATE'}
  else{Need ($args.Count -eq 5 -and (HashFile $args[1]) -ceq $oldMsiPin -and (HashFile $args[2]) -ceq $Plan.newHostDllSha256 -and (HashFile $args[3]) -ceq $hcPin) 'MSI_EXECUTED_CANDIDATE'}
  $results[$id]=ReadProof $step.output
 }
 $candidate=$results['candidate'];$coexist=$results['old-coexistence'];$old=$results['old-write'];$msi=$results['msi']
 Need ($candidate.status -ceq 'PASS' -and $candidate.checks -eq 580 -and $candidate.candidateHostSha256 -ceq $Plan.newHostDllSha256 -and $candidate.helperSha256 -ceq $helperPin -and $candidate.hcSha256 -ceq $hcPin -and $candidate.nativeAttempts -eq 0 -and $candidate.hardwareIo -eq $false -and $candidate.hostMainStarted -eq $false) 'CANDIDATE_RESULT'
 $ids=@('ols-false-clean-retry','ols-throw-never-general-close','ols-failure-and-dispose-failure-retains-both','ols-false-dispose-failure-retry','sleep-before','sleep-during-init','close-during-init','timeout-during-init','events-new-sleep-rejects-then-resume','fan-release-failure-retains-handle','void-fan-return-swallowing-bool-is-real','exit-config-throw-preserves-return-debt','close-lost-open-marker','close-lost-use-openlib','dispose-failure-does-not-repeat-fan-return','foreign-marker','foreign-transport','foreign-serial','foreign-hid','foreign-arrives-during-initialize','unknown:UnknownX1','unknown:UnknownOverride','invalid-ec-contract','two-backends-do-not-adopt-shared-static','reentrant-second-backend-claim','parallel-backends-static-claim-is-serialized','unrelated-device-keeps-virtual-open','actual-host-release-proof-remains-bounded','strict-legacy-cache-cannot-bypass-result','strict-duty-sleep-before','strict-profile-sleep-before','strict-duty-sleep-during','strict-duty-exit-throw','strict-profile-exit-throw','strict-invalid-duty-and-lost-preconditions','temperature-false-detaches-no-target-or-success-cache','temperature-wrong-source-and-retired-owner','temperature-failure-feeds-existing-bounded-engine','existing-engine-sixty-second-window-remains-terminal','old-bound-temperature-failure-does-not-fault-new-engine-owner','write-failure-and-failed-return-retain-debt')
 foreach($model in @('OneXPlayerX1AMD','OneXPlayerX1Intel','OneXPlayerX1Pro','OneXPlayerApex')){
  $ids+=('fan-then-light:'+$model);$ids+=('light-then-fan-reopen:'+$model);$ids+=('strict-duty-goldens:'+$model)
  foreach($operation in @('cold-duty','primed-duty','software-profile','hardware-profile')){$ids+=('strict-false:'+ $model+':'+$operation)}
 }
 ExactRows $candidate.cases $ids 'PASS' 'CANDIDATE'
 foreach($row in $candidate.cases){Need (Number $row.checks) 'CHECK_NUMBER_SCHEMA'}
 foreach($flag in @($candidate.hardwareIo,$candidate.hostMainStarted,$coexist.hardwareIo,$old.hardwareIo,$msi.hostMainStarted)){Need ($flag -is [bool]) 'RESULT_BOOLEAN_SCHEMA'}
 foreach($row in @($coexist.checks)+@($old.checks)){Need ($row.passed -is [bool]) 'CHECK_BOOLEAN_SCHEMA'}
 Need ((($candidate.cases|Measure-Object -Property checks -Sum).Sum -eq 580) -and @($candidate.cases|Where-Object {$_.checks -le 0}).Count -eq 0) 'CHECK_TOTAL'
 Need ($coexist.status -ceq 'DEFECT_REPRODUCED_IN_MEMORY' -and $coexist.host.sha256 -ceq $oldHostPin -and $coexist.helper.sha256 -ceq $helperPin -and $coexist.hc.sha256 -ceq $hcPin -and $coexist.nativeAttempts -eq 0 -and $coexist.hardwareIo -eq $false -and @($coexist.cases).Count -eq 8 -and @($coexist.checks).Count -eq 63 -and @($coexist.checks|Where-Object passed -ne $true).Count -eq 0) 'OLD_COEXISTENCE'
 foreach($name in @('OneXPlayerX1AMD','OneXPlayerX1Intel','OneXPlayerX1Pro','OneXPlayerApex')){Need (@($coexist.cases|Where-Object {$_.id -ceq ('fan-then-lighting:'+$name) -and $_.status -ceq 'DEFECT_REPRODUCED_IN_MEMORY' -and $_.error -ceq 'SERIAL_PORT_BUSY:COM3'}).Count -eq 1) 'OLD_BUSY_MISSING'}
 Need (@($coexist.cases|Where-Object {$_.id -ceq 'failed-ols-cached-retry' -and $_.status -ceq 'DEFECT_REPRODUCED_IN_MEMORY' -and $_.deviceOpen -eq $true -and $_.libraryInitialized -eq $false}).Count -eq 1) 'OLD_OLS_MISSING'
 Need ($old.status -ceq 'DEFECT_REPRODUCED_IN_MEMORY' -and $old.host.sha256 -ceq $oldWritePin -and $old.hc.sha256 -ceq $hcPin -and $old.nativeAttempts -eq 0 -and $old.hardwareIo -eq $false -and @($old.checks).Count -eq 35 -and @($old.checks|Where-Object passed -ne $true).Count -eq 0) 'OLD_WRITE'
 ExactRows $old.cases @('duty-cold','duty-cached','manual-profile','automatic-profile','temperature-target-false-progress','same-temperature-recovery-never-retries') 'SOFTWARE_FALSE_SWALLOWED_REPRODUCED' 'OLD_WRITE'
 $percent=@(0.0,0.25,0.5,1.0,25.0,50.0,75.0,99.0,100.0);$raw=@(0,0,1,2,46,92,138,182,184)
 Need (@($old.goldenWrites).Count -eq 9) 'GOLDEN_COUNT'
 for($i=0;$i -lt 9;$i++){Need ((Number $old.goldenWrites[$i].percent) -and (Number $old.goldenWrites[$i].raw)) 'GOLDEN_NUMBER_SCHEMA';Need ($old.goldenWrites[$i].percent -eq $percent[$i] -and $old.goldenWrites[$i].raw -eq $raw[$i]) 'GOLDEN_CHANGED'}
 Need ($msi.status -ceq 'PASS' -and $msi.candidateHostSha256 -ceq $Plan.newHostDllSha256 -and $msi.legacyHostSha256 -ceq $oldMsiPin -and $msi.hcReferenceSha256 -ceq $hcPin -and $msi.nativeAttempts -eq 0 -and $msi.hardwareCalls -eq 0 -and $msi.hostMainStarted -eq $false -and @($msi.cases).Count -eq 25 -and @($msi.cases|Where-Object status -cne 'PASS').Count -eq 0 -and @($msi.cases|Group-Object id|Where-Object Count -ne 1).Count -eq 0) 'MSI_RESULT'
 foreach($id in @('candidate.open-repeat-close-reopen.ClawA1M','candidate.open-repeat-close-reopen.ClawA2VM','candidate.open-repeat-close-reopen.ClawBZ2EM','candidate.open-repeat-close-reopen.ClawCG3EM','legacy.shift-read-failure-still-writes','candidate.shift-read-failure-never-enters-shift','candidate.openlib-owned-cleanup-retry')){Need (@($msi.cases|Where-Object id -ceq $id).Count -eq 1) 'MSI_CASE_MISSING'}
 return [pscustomobject]@{status='PASS';contract=$cap.contract;hostDllSha256=$Plan.newHostDllSha256;backendDllSha256=$Plan.newBackendDllSha256;candidateCases=69;candidateChecks=580;legacyCoexistenceCases=8;legacyWriteCases=6;goldens=9;exactMsiCases=25;executedCommands=4;naturalExitsVerified=$true;sourceInputsVerified=@($suite.sourceHashes).Count;plannedCompiledInputsVerified=$compiled.Count;approvedLinkedInputsVerified=$approvedLinkedSources.Count;totalCompiledInputsVerified=$boundCompiled.Count;installedApplicationMutated=$false;hardwareIo=$false}
}
