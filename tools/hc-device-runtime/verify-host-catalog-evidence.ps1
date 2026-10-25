# Read-only capability evidence gate. This never starts a process or grants installation authority.
function Assert-YmccHostCatalogEvidence {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)]$Plan)
 $ErrorActionPreference='Stop'
 function Need([bool]$Condition,[string]$Code){if(-not $Condition){throw ('HOST_CATALOG_'+$Code)}}
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
 Need ($Plan.kind -ceq 'host-only-v3' -and $cap.schemaVersion -eq 1 -and $cap.contract -ceq 'fan-unified-catalog-v1') 'CONTRACT'
 $hostPin='98acebb4d81f40309b5180766a92c7ee915a7f05b7002185982d21342d7cf3ff'
 $backendPin='d8cb348ca9b081388c22247f4f44fca140c273260a78cbf6d7263ca6614cbc8a'
 $manifestPin='3afc1d0ccdcc331dd91921eecd086caf1134a4624b5226348df54a6689167bae'
 $runtimePin='6389f7a22b1e294ebaf6c75b05b28bb9839abc7459be0d8898985de27dfe47b8'
 $bundlePin='e5f9dbe64b987e41a43fbe0d4c0f5ee6ac9f3f80e3d04aa9cc1e8c52da8d8b6d'
 $hcPin='08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'
 $helperPin='e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf'
 $oldHostPin='7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f'
 $oldWritePin='a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539'
 $oldMsiPin='5e94170105c65bb0503751460082db2a0827061b075c55afe92f7ef9b3b91993'
 Need ($Plan.newHostDllSha256 -ceq $hostPin -and $Plan.newBackendDllSha256 -ceq $backendPin -and $Plan.newHostManifestSha256 -ceq $manifestPin -and $Plan.newRuntimeManifestSha256 -ceq $runtimePin) 'FIXED_CANDIDATE_TUPLE'
 Need ($cap.sourceBundle.sha256 -ceq $bundlePin) 'SOURCE_BUNDLE_PIN'
 $bundle=ReadProof $cap.sourceBundle
 $bundleRoot=Split-Path -Parent ([IO.Path]::GetFullPath($cap.sourceBundle.path))
 Need ([IO.Path]::GetFullPath($Plan.componentSourceRoot).TrimEnd('\') -ceq (Join-Path $bundleRoot 'source')) 'SOURCE_SELECTION'
 function BundleFile([string]$Relative){
  Need ($Relative -and $Relative -notmatch '[:\\]' -and -not [IO.Path]::IsPathRooted($Relative) -and $Relative -notmatch '(^|/)\.\.?(/|$)') 'SOURCE_MEMBER_PATH'
  $path=[IO.Path]::GetFullPath((Join-Path $bundleRoot $Relative))
  Need ($path.StartsWith($bundleRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'SOURCE_MEMBER_PATH'
  $cursor=$path
  while($cursor){if(Test-Path -LiteralPath $cursor){Need (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'SOURCE_MEMBER_REPARSE'};$cursor=Split-Path -Parent $cursor}
  return $path
 }
 Need ($bundle.kind -ceq 'ymcc-host-component-source-bundle' -and $bundle.componentId -ceq 'candidate98' -and $bundle.runtimeDeployment -eq $false -and @($bundle.sourceInputs).Count -eq 39) 'SOURCE_BUNDLE_SCHEMA'
 $listed=@{}
 foreach($row in @($bundle.files)){
  Need (-not $listed.ContainsKey([string]$row.path)) 'SOURCE_MEMBER_DUPLICATE'
  $file=BundleFile $row.path
  Need ((Test-Path -LiteralPath $file -PathType Leaf) -and (Get-Item -LiteralPath $file).Length -eq $row.bytes -and (HashFile $file) -ceq $row.sha256) 'SOURCE_MEMBER_CHANGED'
  $listed[[string]$row.path]=$true
 }
 $physical=@(Get-ChildItem -LiteralPath $bundleRoot -File -Recurse -Force|ForEach-Object {$_.FullName.Substring($bundleRoot.Length+1).Replace('\','/')})
 Need ($listed.Count -eq 53 -and (Compare-Object @($physical|Sort-Object) @(@($listed.Keys)+@('manifest.json')|Sort-Object)) -eq $null) 'SOURCE_PHYSICAL_SET'
 $actualCompiled=@($bundle.sourceInputs|Where-Object {'Compile' -cin @($_.roles) -or 'project' -cin @($_.roles)})
 Need (@($Plan.compiledInputs).Count -eq 31 -and @($Plan.compiledInputs|Group-Object path|Where-Object Count -ne 1).Count -eq 0) 'COMPILED_INVENTORY'
 foreach($row in @($actualCompiled)){Need (@($Plan.compiledInputs|Where-Object {$_.path -ceq $row.path -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'COMPILED_BINDING'}
 $pdb=ReadProof $cap.pdbDocuments;$portable=ReadProof $cap.portablePdb
 Need ($pdb.status -ceq 'PASS' -and $pdb.manualCompileInputCount -eq 29 -and $pdb.pdbDocumentsVerified -eq 34 -and @($pdb.pairs).Count -eq 2) 'PDB_DOCUMENT_PROOF'
 Need ($portable.status -ceq 'PASS' -and $portable.manifestSha256 -ceq $bundlePin -and $portable.runtimeAssemblyLoad -eq $false -and $portable.originalAbsoluteSourceFilesRequired -eq $false -and @($portable.pairs).Count -eq 2) 'PORTABLE_PDB_PROOF'
 foreach($module in @($bundle.modules)){
  $pairs=@($pdb.pairs|Where-Object name -CEQ $module.name);$pp=@($portable.pairs|Where-Object name -CEQ $module.name)
  Need ($pairs.Count -eq 1 -and $pp.Count -eq 1) 'PDB_MODULE_SET'
  $pair=$pairs[0];$portablePair=$pp[0]
  Need ($pair.dll.sha256 -ceq $module.dllSha256 -and $pair.pdb.sha256 -ceq $module.pdbSha256 -and $pair.pdb.guid -ceq $module.codeViewGuid -and $pair.pdb.stamp -eq $module.stamp -and $pair.debugPairVerified -eq $true) 'PDB_ARTIFACT_PAIR'
  Need ($portablePair.dllSha256 -ceq $module.dllSha256 -and $portablePair.pdbSha256 -ceq $module.pdbSha256 -and $portablePair.pairVerified -eq $true -and $portablePair.documents -eq @($module.documents).Count -and $portablePair.compilationRecords -eq 2) 'PDB_PORTABLE_PAIR'
  Need (@($pair.documents).Count -eq @($module.documents).Count) 'PDB_SOURCE_SET'
  foreach($doc in @($module.documents)){
   Need (@($pair.documents|Where-Object {$_.path -ceq $doc.originalPath -and $_.checksum -ceq $doc.sha256 -and $_.matchesBytes -eq $true}).Count -eq 1) 'PDB_SOURCE_BINDING'
   Need ((HashFile (BundleFile $doc.archivePath)) -ceq $doc.sha256) 'PDB_SOURCE_CHANGED'
  }
 }
 $payload=[IO.Path]::GetFullPath($Plan.payloadRoot).TrimEnd('\')
 Need ((HashFile (Join-Path $payload 'YeManFanHost.dll')) -ceq $hostPin -and (HashFile (Join-Path $payload 'YeManFanBackend.dll')) -ceq $backendPin -and (HashFile (Join-Path $payload 'YeManFanHost.payload.json')) -ceq $manifestPin -and (HashFile (Join-Path $payload 'YeManFanHost.exe')) -ceq '675b2a3f330852cc265acf449d27e3bf39cda23c628047560843dcebfa2c74ab') 'PAYLOAD_IDENTITY'
 $execution=ReadProof $cap.execution
 Need ($execution.kind -ceq 'fan-catalog-consumer-execution-v1' -and $execution.status -ceq 'PASS' -and $execution.candidate.hostSha256 -ceq $hostPin -and $execution.candidate.backendSha256 -ceq $backendPin -and $execution.candidate.manifestSha256 -ceq $manifestPin -and $execution.candidate.payloadRoot -ceq $payload) 'EXECUTION_CANDIDATE'
 foreach($flag in @($execution.hostMainStarted,$execution.hardwareIo,$execution.installedApplicationMutated,$execution.candidateRebuilt,$execution.releaseApproved)){Need ($flag -is [bool] -and $flag -eq $false) 'EXECUTION_SCOPE'}
 Need (@($execution.inputChanges).Count -eq 0 -and @($execution.inputs).Count -eq 275 -and @($execution.inputs|Group-Object path|Where-Object Count -ne 1).Count -eq 0) 'EXECUTION_INPUT_INVENTORY'
 foreach($input in @($execution.inputs)){Need ($input.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile $input.path) -ceq $input.sha256) 'EXECUTION_INPUT_CHANGED'}
 $steps=@($execution.steps);$ids=@('old-coexistence','old-write','candidate','msi','dispatch','roundtrip')
 Need ($steps.Count -eq 6 -and @($steps|Group-Object id|Where-Object Count -ne 1).Count -eq 0) 'EXECUTION_STEPS'
 $harnessPins=@{'old-coexistence'='99aaa0bb1c33ab28de4099ff05f103be6ea6bcd7ba477d97971fff803d00365a';'old-write'='99aaa0bb1c33ab28de4099ff05f103be6ea6bcd7ba477d97971fff803d00365a';candidate='99aaa0bb1c33ab28de4099ff05f103be6ea6bcd7ba477d97971fff803d00365a';msi='789cae7d7474409b86c0f7c5651f8f10c88f6ba36a5a69fd5626e4c687dacb8f';dispatch='f44c2ea5bc37cb7b084d97c226c63eabf68369b822c015cbb7c00b0b659777c6';roundtrip='d4193bdec1e7a7810a4ef5d3c6f9e3a171a7504b56bc31cbe471c361381d1329'}
 $results=@{}
 foreach($id in $ids){
  $matches=@($steps|Where-Object id -CEQ $id);Need ($matches.Count -eq 1) 'STEP_REQUIRED';$step=$matches[0]
  foreach($flag in @($step.naturalExit,$step.timedOut,$step.resultFresh)){Need ($flag -is [bool]) 'PROCESS_BOOLEAN_SCHEMA'}
  Need ((Number $step.exitCode) -and $step.exitCode -eq 0 -and (Number $step.pid) -and $step.pid -gt 0 -and $step.naturalExit -eq $true -and $step.timedOut -eq $false -and $step.resultFresh -eq $true) 'PROCESS_EXIT'
  $start=TimeValue $step.startedUtc;$finish=TimeValue $step.finishedUtc
  Need ($finish -ge $start -and ($finish-$start).TotalSeconds -le 90 -and $finish -le [DateTimeOffset]::UtcNow.AddSeconds(5)) 'PROCESS_TIME'
  $mtime=(Get-Item -LiteralPath $step.output.path).LastWriteTimeUtc
  Need ($mtime -ge $start.UtcDateTime.AddSeconds(-1) -and $mtime -le $finish.UtcDateTime.AddSeconds(1)) 'RESULT_STALE'
  Need ([IO.Path]::GetFileName($step.executable.path) -ieq 'dotnet.exe' -and $step.executable.sha256 -ceq 'ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135' -and (HashFile $step.executable.path) -ceq $step.executable.sha256) 'PROCESS_IMAGE'
  $args=@($step.arguments)
  Need ($step.harness.sha256 -ceq $harnessPins[$id] -and $args[0] -ceq $step.harness.path -and (HashFile $args[0]) -ceq $harnessPins[$id]) 'HARNESS_IDENTITY'
  foreach($path in @($step.executable.path,$step.harness.path)){Need (@($execution.inputs|Where-Object path -CEQ $path).Count -eq 1) 'EXECUTED_INPUT_MISSING'}
  Need (@($args|Where-Object {$_ -match '^--'}).Count -eq 0) 'PROCESS_ARGUMENTS'
  if($id -ceq 'old-coexistence'){Need ($args.Count -eq 6 -and $args[1] -ceq 'old' -and (HashFile $args[2]) -ceq $oldHostPin -and (HashFile $args[3]) -ceq $helperPin -and (HashFile $args[4]) -ceq $hcPin -and $args[5] -ceq $step.output.path) 'OLD_NOT_EXECUTED'}
  elseif($id -ceq 'old-write'){Need ($args.Count -eq 5 -and $args[1] -ceq 'old-write' -and (HashFile $args[2]) -ceq $oldWritePin -and (HashFile $args[3]) -ceq $hcPin -and $args[4] -ceq $step.output.path) 'OLD_WRITE_NOT_EXECUTED'}
  elseif($id -ceq 'candidate'){Need ($args.Count -eq 5 -and (HashFile $args[1]) -ceq $hostPin -and (HashFile $args[2]) -ceq $helperPin -and (HashFile $args[3]) -ceq $hcPin -and $args[4] -ceq $step.output.path) 'WRONG_EXECUTED_CANDIDATE'}
  elseif($id -ceq 'msi'){Need ($args.Count -eq 5 -and (HashFile $args[1]) -ceq $oldMsiPin -and (HashFile $args[2]) -ceq $hostPin -and (HashFile $args[3]) -ceq $hcPin -and $args[4] -ceq $step.output.path) 'MSI_EXECUTED_CANDIDATE'}
  elseif($id -ceq 'dispatch'){Need ($args.Count -eq 4 -and (HashFile $args[1]) -ceq $hostPin -and (HashFile $args[2]) -ceq 'be443c3c83c46c269660a389ecfaff3b74fb076111028f476f9bc74a6994e4fc' -and $args[3] -ceq $step.output.path) 'DISPATCH_EXECUTED_CANDIDATE'}
  else{Need ($args.Count -eq 3 -and $args[1] -ceq $step.output.path -and (HashFile $args[2]) -ceq '0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49') 'ROUNDTRIP_EXECUTED_CANDIDATE'}
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

 $dispatch=$results['dispatch'];$roundtrip=$results['roundtrip']
 Need ((HashFile $dispatch.backendLocation) -ceq $backendPin -and (HashFile $roundtrip.backendLocation) -ceq $backendPin) 'LOADED_BACKEND_IDENTITY'
 foreach($value in @($dispatch.checks,$dispatch.successfulDispatches,$dispatch.sourceClasses,$dispatch.factoryClasses,$dispatch.registeredFanRoutes,$dispatch.loadedBackendCount,$roundtrip.checks)){Need (Number $value) 'CATALOG_NUMBER_SCHEMA'}
 foreach($flag in @($dispatch.HCAssemblyLoaded,$dispatch.hardwareIo)){Need ($flag -is [bool]) 'CATALOG_BOOLEAN_SCHEMA'}
 Need ($dispatch.status -ceq 'PASS' -and $dispatch.checks -eq 601 -and $dispatch.successfulDispatches -eq 149 -and $dispatch.sourceClasses -eq 92 -and $dispatch.factoryClasses -eq 80 -and $dispatch.registeredFanRoutes -eq 70 -and $dispatch.hostSha256 -ceq $hostPin -and $dispatch.backendSha256 -ceq $backendPin -and $dispatch.loadedBackendCount -eq 1 -and $dispatch.HCAssemblyLoaded -eq $false -and $dispatch.hardwareIo -eq $false -and @($dispatch.failures).Count -eq 0) 'DISPATCH_RESULT'
 Need (@($dispatch.rows).Count -eq 601 -and @($dispatch.rows|Where-Object {$_.ok -isnot [bool] -or $_.ok -ne $true}).Count -eq 0) 'DISPATCH_CASES'
 Need ($roundtrip.status -ceq 'passed' -and $roundtrip.passed -is [bool] -and $roundtrip.passed -eq $true -and $roundtrip.checks -eq 62 -and @($roundtrip.checkNames).Count -eq 62 -and $roundtrip.backendSha256 -ceq $backendPin -and $roundtrip.hostConsumerAssemblySha256 -ceq $harnessPins['roundtrip'] -and $roundtrip.binaryStatus -ceq 'passed-actual-locked-dll' -and $roundtrip.hardwareAccess -ceq 'none; transport injected' -and $roundtrip.hostConsumerEvidence -ceq 'actual candidate Backend; Host consumer compiled from byte-identical immutable candidate sources') 'ROUNDTRIP_RESULT'


 $rtProof=ReadProof $cap.roundtripHostSources
 Need ($rtProof.status -ceq 'PASS' -and $rtProof.pairVerified -is [bool] -and $rtProof.pairVerified -eq $true -and $rtProof.sourceBundleManifestSha256 -ceq $bundlePin -and $rtProof.hostSourceDocuments -eq 21 -and $rtProof.fixtureSourceDocuments -eq 2 -and $rtProof.generatedDocuments -eq 3 -and $rtProof.allHostSourcesExactlyEqual -is [bool] -and $rtProof.allHostSourcesExactlyEqual -eq $true -and $rtProof.candidateHostDllLoadedByThisTest -is [bool] -and $rtProof.candidateHostDllLoadedByThisTest -eq $false -and $rtProof.backendDllSha256 -ceq $backendPin) 'ROUNDTRIP_SOURCE_PROOF'
 Need ($rtProof.dll.sha256 -ceq $harnessPins['roundtrip'] -and (HashFile $rtProof.dll.path) -ceq $harnessPins['roundtrip'] -and (HashFile $rtProof.pdb.path) -ceq $rtProof.pdb.sha256 -and @($rtProof.documents).Count -eq 26) 'ROUNDTRIP_SOURCE_ARTIFACT'
 $actualHostModule=@($bundle.modules|Where-Object name -CEQ 'YeManFanHost')[0]
 foreach($doc in @($actualHostModule.documents|Where-Object generated -EQ $false)){
  Need (@($rtProof.documents|Where-Object {$_.hostSource -is [bool] -and $_.hostSource -eq $true -and $_.relative -ceq $doc.sourceRelative -and $_.checksum -ceq $doc.sha256}).Count -eq 1) 'ROUNDTRIP_HOST_SOURCE_BINDING'
 }
 $full=ReadProof $cap.fullHostSelfTest
 Need ($full.status -ceq 'PASS' -and $full.actualHostSelfTest -is [bool] -and $full.actualHostSelfTest -eq $true -and $full.hostDllSha256 -ceq $hostPin -and $full.backendSha256 -ceq $backendPin -and $full.manifestSha256 -ceq $manifestPin -and $full.apphostSha256 -ceq '675b2a3f330852cc265acf449d27e3bf39cda23c628047560843dcebfa2c74ab') 'FULL_HOST_CANDIDATE'
 foreach($flag in @($full.naturalExit,$full.timedOut,$full.realBackendRequested,$full.hardwareWritesRequested,$full.realTokenFileRequested,$full.installedApplicationMutated)){Need ($flag -is [bool]) 'FULL_HOST_BOOLEAN_SCHEMA'}
 Need ((Number $full.processExitCode) -and $full.processExitCode -eq 0 -and (Number $full.pid) -and $full.pid -gt 0 -and $full.naturalExit -eq $true -and $full.timedOut -eq $false -and $full.realBackendRequested -eq $false -and $full.hardwareWritesRequested -eq $false -and $full.realTokenFileRequested -eq $false -and $full.installedApplicationMutated -eq $false -and $full.payloadInputDrift -eq 0) 'FULL_HOST_EXECUTION'
 Need ((Number $full.checksPassed) -and $full.checksPassed -eq 428 -and $full.actualResult.ok -is [bool] -and $full.actualResult.ok -eq $true -and @($full.actualResult.checks).Count -eq 428 -and @($full.actualResult.checks|Where-Object {$_ -ceq 'fan934-msi-unregistered-namespace-denied'}).Count -eq 1) 'FULL_HOST_CHECKS'
 $fullStart=TimeValue $full.startedUtc;$fullFinish=TimeValue $full.finishedUtc
 Need ($fullFinish -ge $fullStart -and ($fullFinish-$fullStart).TotalSeconds -le 180 -and $fullFinish -le [DateTimeOffset]::UtcNow.AddSeconds(5)) 'FULL_HOST_TIME'
 foreach($log in @($full.stdout,$full.stderr)){Need ($log.path -and $log.sha256 -cmatch '^[a-f0-9]{64}$' -and (HashFile $log.path) -ceq $log.sha256) 'FULL_HOST_LOG_CHANGED'}
 Need ((Get-Item -LiteralPath $full.stderr.path).Length -eq 0) 'FULL_HOST_STDERR'
 $jsonRows=@(Get-Content -LiteralPath $full.stdout.path -Encoding UTF8|Where-Object {$_.StartsWith('{"ok":') -and $_.Contains('"checks":') -and $_.Contains('"finalState":') -and $_.Contains('"openState":')})
 Need ($jsonRows.Count -eq 1) 'FULL_HOST_RAW_RESULT'
 $rawResult=$jsonRows[0]|ConvertFrom-Json
 Need ($rawResult.ok -is [bool] -and $rawResult.ok -eq $true -and @($rawResult.checks).Count -eq 428 -and @($rawResult.checks|Where-Object {$_ -ceq 'fan934-msi-unregistered-namespace-denied'}).Count -eq 1 -and ($rawResult.checks -join "`n") -ceq ($full.actualResult.checks -join "`n")) 'FULL_HOST_RAW_RESULT'
 $public=ReadProof $cap.publicConnection
 Need (Number $public.hostExitCode) 'PUBLIC_NUMBER_SCHEMA'
 Need ($public.status -ceq 'PASS' -and @($public.checks).Count -eq 30 -and @($public.checks|Where-Object status -CNE 'PASS').Count -eq 0 -and $public.hostPayloadRoot -ceq $payload -and $public.hostExitCode -eq 0) 'PUBLIC_CONNECTION'
 foreach($flag in @($public.realBackend,$public.hardwareAcceptance,$public.managerStartsCalled,$public.realDeviceOpenCalled,$public.forcedFixtureCleanup)){Need ($flag -is [bool] -and $flag -eq $false) 'PUBLIC_SCOPE'}
 Need ($public.realManagerPreflightRequired -is [bool] -and $public.realManagerPreflightRequired -eq $true -and $public.naturalShutdownRequired -is [bool] -and $public.naturalShutdownRequired -eq $true) 'PUBLIC_SCOPE'
 $manager=@($public.checks|Where-Object name -CEQ 'real 13 managers and real known-bad audio regression')
 Need ($manager.Count -eq 1 -and $manager[0].detail.approvedManagerCount -eq 13 -and $manager[0].detail.knownBadReproduced -eq $true -and $manager[0].detail.noHostOrDeviceOpen -eq $true -and $manager[0].detail.approvedClosure.manifestSha256 -ceq $runtimePin -and $manager[0].detail.approvedClosure.ordinaryDllsComparedToFrozenUpstream -eq 77) 'PUBLIC_MANAGER_PROOF'
 $hostProof=@($public.checks|Where-Object name -CEQ 'real-manager preflight and Host arguments bind unchanged HC input')
 Need ($hostProof.Count -eq 1 -and $hostProof[0].detail.hostSha256 -ceq $hostPin -and $hostProof[0].detail.hcSha256 -ceq $hcPin -and $hostProof[0].detail.realBackend -eq $false) 'PUBLIC_HOST_BINDING'
 . (RepoFile 'tools/hc-device-runtime/verify-runtime.ps1')
 $runtime=Assert-YmccDeviceCoreRuntime -ProjectRoot $Plan.repoRoot -RuntimeRoot $public.runtimeRoot
 Need ($runtime.manifestSha256 -ceq $runtimePin -and $runtime.ordinaryDllsComparedToFrozenUpstream -eq 77 -and (HashFile (Join-Path $public.runtimeRoot 'HandheldCompanion.dll')) -ceq $hcPin) 'RUNTIME_BINDING'
 return [pscustomobject]@{status='PASS';contract=$cap.contract;hostDllSha256=$hostPin;backendDllSha256=$backendPin;manifestSha256=$manifestPin;controlledSourceInputs=39;compiledSourceAndProjectInputs=31;pdbDocuments=34;candidateCases=69;candidateChecks=580;exactMsiCases=25;catalogDispatchChecks=601;roundtripChecks=62;roundtripHostEvidence='compiled-production-source';publicConnectionChecks=30;actualExecutedConsumers=6;actualInputIdentities=@($execution.inputs).Count;fullHostSelfTestChecks=428;softwareCapabilityEvidenceAccepted=$true;installationGranted=$false;hardwareAcceptance=$false}
}
