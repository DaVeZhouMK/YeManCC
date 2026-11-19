# One approved checker successor, not a general script replacement facility.
# Product bytes and their source index always remain in the original fixed root.
function Get-YmccSteamGuardSuccessorPolicy {
  return [ordered]@{
    receipt='8d5bfbfd559a269deb166b45b7befe843e194e73576c7058d1051eaf36608684'
    oldGuard='a0de1c637f8ffa99170ef10241aadb01d2f5dddbf4cd81f236c0c15e0654c539'
    newGuard='10cb0f2f42b391beae833d9d5b2ff3e049af925758b2986b9314682cadecd6ab'
    failedJob='85b11c1b082bde14dab93363c2e150959de8e0646f4749d460a4b744f0f503fe'
    failedStderr='4534d00e559c7dc6e490fbd81b855c70b64e3790f0275453982dc7742346ed7e'
    originalLog='c085ae599bae7bd2f41e13a4af6ed45010af1036db9e64eccb525f51f8998d3f'
    runtime='0d20ce441a439b80c873fe36f31cf8a1d9fece4cd0cf499e7d3439cdce247848'
    native='847e6f2946f169b6cb2d78b9887bd5ae7f8846c7b38035c348621eda52986f25'
    recovered='2630016b1396921c4e88a05b0d5f3b159d9995a97a8c6984779a940f5f7c2948'
    before='4567f7f776e86cbc0eac1a5bc2b496e9a52f19c1f1e70303fbf14e5e3cb3cdfb'
    worker='c4de581f69d5fc1d129c79bbfa0ddd7771fa1b6a4ab545b35c041a66305379f3'
    capture='d9ba083d71fcb9771c12fa4eb3d88c85c56af0a032ff91b5b2a8a6a12991acbe'
    authority='7c2f4484610f53d8e4d9fddbf07e4557750ba50205debe6012e812d1ba2de59c'
    pending='f8f624ba6abd6c28eee78bacab2c4310915dd6d116ac6cc62589e7a31f5ea184'
    partialGraph='f25d9fc4410b4826ae7a28ec0e93108d078591e25ea0c4f92c7ca3415f9bc8cc'
    nonce='f704954ff97e4e0891e4d4adedc96b61'
  }
}
function Get-YmccGuardFullPath([string]$Path) {
  if(-not $Path -or -not [IO.Path]::IsPathRooted($Path)){throw 'EXPORT_GUARD_SUCCESSOR_ABSOLUTE_PATH_REQUIRED'}
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}
function Get-YmccGuardBytesHash([byte[]]$Bytes) {
  $sha=[Security.Cryptography.SHA256]::Create()
  try{return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function Get-YmccGuardPin([string]$Path) {
  $full=Get-YmccGuardFullPath $Path
  $cursor=$full
  while($cursor){
    if((Test-Path -LiteralPath $cursor) -and (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)){throw 'EXPORT_GUARD_SUCCESSOR_REPARSE_POINT'}
    $cursor=[IO.Path]::GetDirectoryName($cursor)
  }
  $bytes=[IO.File]::ReadAllBytes($full)
  return [ordered]@{path=$full;bytes=$bytes.LongLength;sha256=(Get-YmccGuardBytesHash $bytes)}
}
function Assert-YmccGuardPin($Pin,[string]$RequiredHash='',[string]$RequiredPath='') {
  if(-not $Pin -or [string]$Pin.sha256 -notmatch '^[a-f0-9]{64}$' -or $Pin.bytes -lt 0){throw 'EXPORT_GUARD_SUCCESSOR_PIN_INVALID'}
  if($RequiredPath -and (Get-YmccGuardFullPath $Pin.path) -ine (Get-YmccGuardFullPath $RequiredPath)){throw 'EXPORT_GUARD_SUCCESSOR_PIN_SCOPE'}
  if($RequiredHash -and $Pin.sha256 -cne $RequiredHash){throw 'EXPORT_GUARD_SUCCESSOR_NOT_APPROVED'}
  $actual=Get-YmccGuardPin $Pin.path
  if($actual.bytes -ne $Pin.bytes -or $actual.sha256 -cne $Pin.sha256){throw 'EXPORT_GUARD_SUCCESSOR_PIN_CHANGED'}
}
function Read-YmccGuardPinnedJson($Pin,[string]$RequiredHash='',[string]$RequiredPath='') {
  Assert-YmccGuardPin $Pin $RequiredHash $RequiredPath
  $bytes=[IO.File]::ReadAllBytes($Pin.path)
  if($bytes.LongLength -ne $Pin.bytes -or (Get-YmccGuardBytesHash $bytes) -cne $Pin.sha256){throw 'EXPORT_GUARD_SUCCESSOR_PIN_CHANGED'}
  return ([Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)|ConvertFrom-Json)
}
function New-YmccSteamGuardSuccessorContext([string]$ReceiptPath,[string]$ProductRoot,[string]$WorkspaceRoot) {
  $policy=Get-YmccSteamGuardSuccessorPolicy
  $pin=Get-YmccGuardPin $ReceiptPath
  $receipt=Read-YmccGuardPinnedJson $pin $policy.receipt
  $product=Get-YmccGuardFullPath $ProductRoot;$work=Get-YmccGuardFullPath $WorkspaceRoot
  if($receipt.schemaVersion -ne 1 -or $receipt.status -cne 'SOURCE_READY_SCOPED_EXPORT_GUARD_DIAGNOSTIC_ARGUMENT' -or
     $receipt.sourceReady -ne $true -or (Get-YmccGuardFullPath $receipt.fixedProductRoot) -ine $product -or
     (Get-YmccGuardFullPath (Join-Path $product '../..')) -ine $work){throw 'EXPORT_GUARD_SUCCESSOR_SCOPE_MISMATCH'}
  $change=@($receipt.changes|Where-Object relative -CEQ 'tools/sleep-steam-export-guard.ps1')
  if($change.Count -ne 1){throw 'EXPORT_GUARD_SUCCESSOR_CHANGE_INVALID'}
  $old=Get-YmccGuardPin (Join-Path $product 'tools/sleep-steam-export-guard.ps1')
  Assert-YmccGuardPin $old $policy.oldGuard
  Assert-YmccGuardPin $change[0].before $policy.oldGuard
  Assert-YmccGuardPin $change[0].successor $policy.newGuard
  $oldText=[IO.File]::ReadAllText($old.path);$guardBytes=[IO.File]::ReadAllBytes($change[0].successor.path)
  if((Get-YmccGuardBytesHash $guardBytes) -cne $policy.newGuard){throw 'EXPORT_GUARD_SUCCESSOR_PIN_CHANGED'}
  $newText=[Text.Encoding]::UTF8.GetString($guardBytes)
  $oldCall='\bconst\s+auto\s+fresh\s*=\s*nativeSteamGameTrackerReadFresh\s*\(\s*client\s*,\s*game\s*,\s*logScope\s*\)\s*;'
  $newCall='\bconst\s+auto\s+fresh\s*=\s*nativeSteamGameTrackerReadFresh\s*\(\s*client\s*,\s*game\s*,\s*logScope\s*(?:,\s*&\s*freshDiagnostic\s*)?\)\s*;'
  if($oldText.Split(@($oldCall),[StringSplitOptions]::None).Count -ne 2 -or $oldText.Replace($oldCall,$newCall) -cne $newText){throw 'EXPORT_GUARD_SUCCESSOR_DIFF_NOT_APPROVED'}
  $job=Read-YmccGuardPinnedJson $receipt.originalFailure.job $policy.failedJob
  Assert-YmccGuardPin $receipt.originalFailure.stderr $policy.failedStderr
  if($job.status -cne 'FAILED' -or $job.rootExitCode -ne 1 -or $job.activeProcessesFinal -ne 0 -or $job.zeroObservedBeforeDispose -ne $true){throw 'EXPORT_GUARD_SUCCESSOR_ORIGINAL_FAILURE_INVALID'}
  $runtime=Get-YmccGuardPin (Join-Path $product 'native/steam_game_link_runtime.h')
  Assert-YmccGuardPin $runtime $policy.runtime
  $tools=@(foreach($name in @('export-steam-guard-successor.ps1','release-export-identity.ps1','resume-export-build-tail.ps1','package-release.ps1','verify-steam-sleep-export.ps1','deploy-local-mainline.ps1','deploy-installed.ps1')){Get-YmccGuardPin (Join-Path $PSScriptRoot $name)})
  $binding=[ordered]@{schema=1;kind='YMCC_STEAM_DIAGNOSTIC_GUARD_SUCCESSOR_V1';productRoot=$product;workspaceRoot=$work;evidenceRoot='G:\YeManCC-Work\Testing\YMCC\Evidence';receipt=$pin;oldGuard=$old;guard=$change[0].successor;originalFailureJob=$receipt.originalFailure.job;originalFailureStderr=$receipt.originalFailure.stderr;runtime=$runtime;tools=$tools}
  $context=[ordered]@{binding=$binding;guardText=$newText}
  Assert-YmccSteamGuardSuccessorCurrent $context $product $work
  return $context
}
function Assert-YmccSteamGuardSuccessorCurrent($Context,[string]$ProductRoot,[string]$WorkspaceRoot) {
  $b=$Context.binding;$policy=Get-YmccSteamGuardSuccessorPolicy
  if(-not $b -or $b.kind -cne 'YMCC_STEAM_DIAGNOSTIC_GUARD_SUCCESSOR_V1' -or $b.productRoot -ine (Get-YmccGuardFullPath $ProductRoot) -or $b.workspaceRoot -ine (Get-YmccGuardFullPath $WorkspaceRoot)){throw 'EXPORT_GUARD_SUCCESSOR_SCOPE_MISMATCH'}
  Assert-YmccGuardPin $b.receipt $policy.receipt
  Assert-YmccGuardPin $b.oldGuard $policy.oldGuard (Join-Path $b.productRoot 'tools/sleep-steam-export-guard.ps1')
  Assert-YmccGuardPin $b.guard $policy.newGuard
  Assert-YmccGuardPin $b.originalFailureJob $policy.failedJob
  Assert-YmccGuardPin $b.originalFailureStderr $policy.failedStderr
  Assert-YmccGuardPin $b.runtime $policy.runtime (Join-Path $b.productRoot 'native/steam_game_link_runtime.h')
  if(@($b.tools).Count -ne 7){throw 'EXPORT_GUARD_SUCCESSOR_TOOLS_INVALID'}
  foreach($tool in @($b.tools)){Assert-YmccGuardPin $tool}
}
function Assert-YmccSteamGuardBuildBinding($Captured,$Context) {
  if(-not $Context){if($Captured){throw 'EXPORT_GUARD_SUCCESSOR_EXPLICIT_RECEIPT_REQUIRED'};return}
  if(-not $Captured -or ($Captured|ConvertTo-Json -Depth 9 -Compress) -cne ($Context.binding|ConvertTo-Json -Depth 9 -Compress)){throw 'EXPORT_GUARD_SUCCESSOR_BUILD_BINDING_MISMATCH'}
}
function Get-YmccGuardRecoveredProducer($Context,[string]$RecoveredInputsPath,[string]$PartialNativeGraphPath) {
  $b=$Context.binding;$policy=Get-YmccSteamGuardSuccessorPolicy
  Assert-YmccSteamGuardSuccessorCurrent $Context $b.productRoot $b.workspaceRoot
  $recoveredPin=Get-YmccGuardPin $RecoveredInputsPath
  $r=Read-YmccGuardPinnedJson $recoveredPin $policy.recovered
  if($r.status -cne 'CAPTURED_BEFORE_AND_SAME_PRODUCER_WORKER_STAGE_RECOVERED' -or $r.producerReexecuted -ne $false -or $r.productControlled -ne $false -or $r.newBuildIdentityWritten -ne $false -or $r.pendingControlled -ne $false){throw 'EXPORT_GUARD_RESUME_RECOVERY_INVALID'}
  $job=Read-YmccGuardPinnedJson $r.producerJob $policy.failedJob
  Assert-YmccGuardPin $r.originalLog $policy.originalLog
  $graphPin=Get-YmccGuardPin $PartialNativeGraphPath
  $graph=Read-YmccGuardPinnedJson $graphPin $policy.partialGraph
  if($graph.status -cne 'PARTIAL_COMPILED_NATIVE_GRAPH_PINNED_FULL_EXPORT_REJECTED' -or $graph.fullBuildAccepted -ne $false -or
     $graph.exportFailure -cne 'EXPORT_STEAM_ORIGINAL_PROCESS_LIFETIME_MISSING' -or @($graph.includes).Count -ne 1495 -or
     @($graph.libraries).Count -ne 54 -or @($graph.unclosedActualInputs).Count -ne 0 -or $graph.toolPinsClosingUnchanged -ne $true){throw 'EXPORT_GUARD_RESUME_PARTIAL_GRAPH_INVALID'}
  Assert-YmccGuardPin $graph.job $policy.failedJob
  Assert-YmccGuardPin $graph.log $policy.originalLog
  Assert-YmccGuardPin $graph.toolPins
  Assert-YmccGuardPin $graph.pdb
  foreach($pin in @($graph.products)+@($graph.generatedInputs)){Assert-YmccGuardPin $pin}
  $before=Read-YmccGuardPinnedJson $r.before $policy.before
  $capture=Read-YmccGuardPinnedJson $before.capture $policy.capture
  $authority=Read-YmccGuardPinnedJson $before.explicitAuthorities $policy.authority
  if((Get-YmccGuardFullPath $capture.snapshot) -ine $b.productRoot -or $capture.branch -cne 'main' -or @($authority.files).Count -ne 0 -or $before.currentValuesNotRelabeledAsCaptured -ne $true -or @($before.mismatches).Count -ne 0){throw 'EXPORT_GUARD_RESUME_SOURCE_PROVENANCE_INVALID'}
  $captured=@{};foreach($row in @($capture.payload)){if($captured.ContainsKey($row.path)){throw 'EXPORT_GUARD_RESUME_DUPLICATE_SOURCE'};$captured[$row.path]=$row}
  $seen=@{};foreach($row in @($before.sources)){
    $name=ConvertTo-ExportRelativePath $row.path
    if(-not $captured.ContainsKey($name)){throw 'EXPORT_GUARD_RESUME_SOURCE_NOT_CAPTURED'}
    $seen[$name]=$true;$original=$captured[$name]
    if($row.sha256 -cne $original.sha256 -or $row.bytes -ne $original.bytes){throw 'EXPORT_GUARD_RESUME_SOURCE_NOT_CAPTURED'}
  }
  # Preserve the original index's two duplicate declarations and full multiset.
  if(@($before.sources).Count -ne 1805 -or $seen.Count -ne 1803 -or (Get-ExportIndexHash @($before.sources)) -cne $before.sourceBeforeHash -or
     (Get-ExportIndexHash @(Get-ExportSourceIndex $b.productRoot)) -cne $before.sourceBeforeHash){throw 'EXPORT_GUARD_RESUME_SOURCE_CHANGED'}
  $pending=Read-YmccGuardPinnedJson $r.pending $policy.pending (Join-Path $b.workspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain/PENDING.json')
  $proofJson=Read-YmccGuardPinnedJson $r.workerProof $policy.worker
  if($pending.state -cne 'NEW_BUILD_NOT_YET_BOUND' -or $pending.nonce -cne $policy.nonce -or $proofJson.nonce -cne $pending.nonce -or $pending.project -ine $b.productRoot -or $pending.workspace -ine $b.workspaceRoot){throw 'EXPORT_GUARD_RESUME_PENDING_SCOPE'}
  $proof=[ordered]@{};foreach($property in $proofJson.PSObject.Properties){$proof[$property.Name]=$property.Value}
  Assert-CslWorkerStage $proof $b.productRoot $b.workspaceRoot -BeforeHostPublish
  $native=Get-YmccGuardPin (Join-Path $b.workspaceRoot 'Build/App/Native/YeManCC.exe');Assert-YmccGuardPin $native $policy.native
  return [ordered]@{sources=@($before.sources);proof=$proof;receipt=$recoveredPin;pending=$r.pending;before=$r.before;worker=$r.workerProof;native=$native;partialGraph=$graphPin;originalJob=$r.producerJob;originalLog=$r.originalLog;closingPins=@($recoveredPin,$r.before,$r.workerProof,$before.capture,$before.explicitAuthorities,$graphPin,$graph.toolPins,$graph.pdb)+@($graph.products)+@($graph.generatedInputs)}
}
function Assert-YmccGuardPrerequisiteEntries([object[]]$Payload,[string]$ProductRoot,[string]$CompleteRoot) {
  # Read the exact five-file tuple already produced and closed by Package.
  # This function neither starts an EXE nor invokes the prerequisite builder.
  $rows=@(foreach($name in @('HIDMaestroSetup.exe','HIDMaestroSetup.dll','HIDMaestroSetup.deps.json','HIDMaestroSetup.runtimeconfig.json','HIDMaestroSetup.manifest.json')){
    $relative='PowerControl/redist/'+$name
    $row=@($Payload|Where-Object path -CEQ $relative)
    if($row.Count -ne 1){throw 'EXPORT_GUARD_PREREQUISITE_TUPLE_INCOMPLETE'}
    $source=[ordered]@{path=(Join-Path $ProductRoot $relative);bytes=$row[0].bytes;sha256=$row[0].sha256}
    $complete=[ordered]@{path=(Join-Path $CompleteRoot $relative);bytes=$row[0].bytes;sha256=$row[0].sha256}
    Assert-YmccGuardPin $source;Assert-YmccGuardPin $complete
    [ordered]@{path=$relative;bytes=$row[0].bytes;sha256=$row[0].sha256}
  })
  return [ordered]@{status='COMPLETED_PACKAGE_PREREQUISITE_TUPLE_VERIFIED';files=$rows;producerReexecuted=$false}
}
function Assert-YmccGuardCompletedPrerequisites($Context,$BuildIdentity) {
  $b=$Context.binding
  Assert-YmccSteamGuardSuccessorCurrent $Context $b.productRoot $b.workspaceRoot
  Assert-YmccSteamGuardBuildBinding $BuildIdentity.verifierSuccessor $Context
  $complete=Join-Path $b.workspaceRoot 'Build/Package/CompleteRoot'
  $record=Get-Content -LiteralPath (Join-Path $complete 'YeManCC/export-identity.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if($record.buildId -cne $BuildIdentity.buildId -or $record.sourceTreeSha256 -cne $BuildIdentity.sourceTreeSha256){throw 'EXPORT_GUARD_PREREQUISITE_PACKAGE_BINDING'}
  Assert-YmccSteamGuardBuildBinding $record.verifierSuccessor $Context
  return Assert-YmccGuardPrerequisiteEntries @($record.payload) $b.productRoot $complete
}
