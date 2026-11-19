<# Continue only the approved failed export's consumers. No compiler is invoked. #>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ProductRoot,
  [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
  [Parameter(Mandatory=$true)][string]$SteamGuardSuccessorReceipt,
  [Parameter(Mandatory=$true)][string]$RecoveredProducerInputs,
  [Parameter(Mandatory=$true)][string]$PartialNativeGraph,
  [ValidateSet('ValidateOnly','CompleteBuildTail')][string]$Action='ValidateOnly',
  [string]$ReceiptPath=''
)
$ErrorActionPreference='Stop'
if($ReceiptPath -and (Test-Path -LiteralPath $ReceiptPath)){throw 'EXPORT_GUARD_RESUME_FRESH_RECEIPT_REQUIRED'}
if($ReceiptPath){
  $out=[IO.Path]::GetFullPath($ReceiptPath)
  foreach($base in @([IO.Path]::GetFullPath($ProductRoot).TrimEnd('\'),[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\'))){if($out.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'EXPORT_GUARD_RESUME_RECEIPT_OUTSIDE_PRODUCT_REQUIRED'}}
  if(-not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($out))){throw 'EXPORT_GUARD_RESUME_RECEIPT_PARENT_MISSING'}
}
. (Join-Path $PSScriptRoot 'release-export-identity.ps1') -SteamGuardSuccessorReceipt $SteamGuardSuccessorReceipt -GuardProductRoot $ProductRoot -GuardWorkspaceRoot $WorkspaceRoot
. (Join-Path $ProductRoot 'tools/custom_steam_library_build_guard.ps1')
$mutex=Enter-ExportWorkspaceLock $WorkspaceRoot
try {
  $identityPath=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json'
  if(Test-Path -LiteralPath $identityPath){throw 'EXPORT_GUARD_RESUME_BUILD_ALREADY_EXISTS'}
  $recovered=Get-YmccGuardRecoveredProducer $exportGuardSuccessorContext $RecoveredProducerInputs $PartialNativeGraph
  # Same original native/source contract and same five-tool Worker stage.
  $nativeCheck=Assert-SteamSleepNativeExport $ProductRoot $recovered.native.path
  if($nativeCheck.nativeSha256 -cne $recovered.native.sha256){throw 'EXPORT_GUARD_RESUME_NATIVE_CHANGED'}
  Assert-YmccGuardPin $recovered.pending
  $build=$null
  if($Action -ceq 'CompleteBuildTail'){
    # The original writer and original Complete/CAS retain their exact order.
    $build=Write-ExportBuildIdentity -ProjectRoot $ProductRoot -WorkspaceRoot $WorkspaceRoot -SourceBefore @($recovered.sources)
    Assert-YmccGuardPin $recovered.pending
    Complete-CslWorkerBeforeMain $recovered.proof $build $ProductRoot $WorkspaceRoot
    $null=Assert-ExportBuildIdentity -ProjectRoot $ProductRoot -WorkspaceRoot $WorkspaceRoot
    $null=Get-CslWorkerForPackage $build $ProductRoot $WorkspaceRoot
  }
  Assert-YmccSteamGuardSuccessorCurrent $exportGuardSuccessorContext $ProductRoot $WorkspaceRoot
  Assert-YmccGuardPin $recovered.native
  foreach($pin in $recovered.closingPins){Assert-YmccGuardPin $pin}
  if($Action -ceq 'ValidateOnly'){Assert-YmccGuardPin $recovered.pending}
  $result=[ordered]@{schema=1;status=$(if($Action -ceq 'ValidateOnly'){'GUARD_TAIL_RESUME_VALIDATED_ONLY'}else{'GUARD_TAIL_RESUMED_SAME_PRODUCER'});atUtc=[DateTime]::UtcNow.ToString('o');productRoot=$ProductRoot;workspaceRoot=$WorkspaceRoot;verifierSuccessor=$exportGuardSuccessorContext.binding;recoveredProducer=$recovered.receipt;sourceBefore=$recovered.before;workerProof=$recovered.worker;partialNativeGraph=$recovered.partialGraph;originalProducerJob=$recovered.originalJob;originalProducerLog=$recovered.originalLog;nonce=$recovered.proof.nonce;native=$recovered.native;buildId=$(if($build){$build.buildId}else{$null});sourceEntries=@($recovered.sources).Count;newBuildIdentityWritten=($Action -ceq 'CompleteBuildTail');productFixtureValidation='NOT_RUN';producerReexecuted=$false;originalFailureRetained=$true;packageExecuted=$false;deployed=$false}
  if($ReceiptPath){
    $out=Get-YmccGuardFullPath $ReceiptPath
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($result|ConvertTo-Json -Depth 10))
    $stream=[IO.File]::Open($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
  }
  $result|ConvertTo-Json -Depth 10
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
