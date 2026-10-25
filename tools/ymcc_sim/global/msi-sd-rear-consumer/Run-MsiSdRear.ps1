<# Invoked as kind=powershell by the existing global runner. host-process.ps1
   assigns this wrapper and every descendant to its gated kill-on-close Job.
   This module neither creates another Job nor performs name-based cleanup. #>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [Parameter(Mandatory=$true)][string]$NodePath
)
$ErrorActionPreference='Stop'
$msiExit=2
$msiRunId=[Guid]::NewGuid().ToString('N')
$msiNodeExit=$null
$msiPass=$false
$msiError=$null
$msiResult=$null
$msiResultHash=$null
$msiStarted=[DateTime]::UtcNow.ToString('o')
$msiActualPowerShell=[Environment]::ProcessPath
$msiEnvironmentKeys=@('NODE_OPTIONS','NODE_PATH','PSModulePath')
$msiPreviousEnvironment=@{}
foreach($msiKey in $msiEnvironmentKeys){$msiPreviousEnvironment[$msiKey]=[Environment]::GetEnvironmentVariable($msiKey,'Process')}
try {
 if($PSVersionTable.PSVersion.Major -lt 7){throw 'MSI_SD_POWERSHELL_7_REQUIRED'}
 if(-not [IO.Path]::IsPathFullyQualified($EvidenceDir) -or (Test-Path -LiteralPath $EvidenceDir)){throw 'MSI_SD_FRESH_ABSOLUTE_EVIDENCE_REQUIRED'}
 $env:PSModulePath=Join-Path $PSHOME 'Modules'
 foreach($msiKey in @('NODE_OPTIONS','NODE_PATH')){[Environment]::SetEnvironmentVariable($msiKey,$null,'Process')}
 $msiBase=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../sleep-native-recovery-consumer/toolchain-inputs.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 if(-not [IO.Path]::GetFullPath($NodePath).Equals([IO.Path]::GetFullPath($msiBase.nodeExecutable),[StringComparison]::OrdinalIgnoreCase)){throw 'MSI_SD_NODE_EXECUTABLE_MISMATCH'}
 if(-not [IO.Path]::GetFullPath($msiActualPowerShell).Equals([IO.Path]::GetFullPath($msiBase.powershellExecutable),[StringComparison]::OrdinalIgnoreCase)){throw 'MSI_SD_POWERSHELL_EXECUTABLE_MISMATCH'}
 foreach($msiExecutable in @($NodePath,$msiActualPowerShell)){
  $msiRows=@($msiBase.files | Where-Object { $_.path.Equals($msiExecutable,[StringComparison]::OrdinalIgnoreCase) })
  if($msiRows.Count -ne 1 -or (Get-FileHash -LiteralPath $msiExecutable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $msiRows[0].sha256){throw 'MSI_SD_BOOTSTRAP_EXECUTABLE_HASH_MISMATCH'}
 }
 & $NodePath (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir --run-id $msiRunId --powershell-path $msiActualPowerShell --powershell-pid $PID
 $msiNodeExit=$LASTEXITCODE
 $msiResultPath=Join-Path $EvidenceDir 'result.json'
 $msiResult=Get-Content -LiteralPath $msiResultPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $msiResultHash=(Get-FileHash -LiteralPath $msiResultPath -Algorithm SHA256).Hash.ToLowerInvariant()
 $msiPass=$msiNodeExit -eq 0 -and $msiResult.schemaVersion -eq 1 -and $msiResult.id -eq 'controller.msi-sd-rear-consumer' -and $msiResult.runId -eq $msiRunId -and $msiResult.status -eq 'PASS' -and $msiResult.passed -eq $true -and $msiResult.powershell.pid -eq $PID -and $msiResult.inputsUnchanged -eq $true -and $msiResult.toolsUnchanged -eq $true -and $msiResult.positive.validated -eq $true -and $msiResult.faultSummary.validated -eq $true -and @($msiResult.faults).Count -eq 5
 if($msiPass){$msiExit=0}else{$msiExit=1}
} catch {
 $msiError=$_.Exception.ToString()
 [Console]::Error.WriteLine($msiError)
 $msiExit=2
} finally {
 try {
  # Only this new attempt may receive a terminal. An old directory is never overwritten.
  $msiAttemptPath=Join-Path $EvidenceDir 'attempt.json'
  if(Test-Path -LiteralPath $msiAttemptPath){
   $msiAttempt=Get-Content -LiteralPath $msiAttemptPath -Raw -Encoding UTF8 | ConvertFrom-Json
   if($msiAttempt.runId -eq $msiRunId -and $msiAttempt.powershellPid -eq $PID){
    $msiHashes=@(@{path=$msiAttemptPath;sha256=(Get-FileHash -LiteralPath $msiAttemptPath -Algorithm SHA256).Hash.ToLowerInvariant()})
    if($null -ne $msiResultHash){$msiHashes+=@{path=(Join-Path $EvidenceDir 'result.json');sha256=$msiResultHash}}
    $msiTerminal=[ordered]@{
     schemaVersion=1;id='controller.msi-sd-rear-consumer';status=$(if($msiPass){'PASS'}else{'FAIL'});passed=$msiPass
     runId=$msiRunId;evidenceRoot=[IO.Path]::GetFullPath($EvidenceDir);startedAt=$msiStarted;finishedAt=[DateTime]::UtcNow.ToString('o')
     powershellPid=$PID;powershellPath=$msiActualPowerShell;nodePid=$msiAttempt.nodePid;nodePath=$NodePath;nodeExitCode=$msiNodeExit;wrapperExitCode=$msiExit
     ownedJobExpectedFromGlobalPowershellDescriptor=$true;independentJobMembershipObserved=$false
     nodeExitObserved=($null -ne $msiNodeExit);nodeNaturalSuccess=($msiPass -and $msiNodeExit -eq 0)
     nodeExitEvidence='Synchronous PowerShell LASTEXITCODE and this-attempt Node result; no independent Node signal observation'
     inputsUnchanged=$msiResult.inputsUnchanged;toolsUnchanged=$msiResult.toolsUnchanged
     positive=$msiResult.positive;faultSummary=$msiResult.faultSummary;faults=@($msiResult.faults | ForEach-Object { @{id=$_.id;result=$_.result;compiled=$_.compiled;nativeExitCode=$_.nativeExitCode;nativeSignal=$_.nativeSignal;childExitCode=$_.childExitCode;childSignal=$_.childSignal;expectedError=$_.expectedError;validated=$_.validated} })
     producerTerminals=@($msiResult.processes);boundary=$msiResult.boundary;remainingGaps=$msiResult.remainingGaps
     error=$msiError;completionLine=$(if($msiPass){'MSI_SD_REAR_CONSUMER_V1_COMPLETED positive=78/67093 faults=5/5 DefaultRelease'}else{$null})
     sourceHashes=$msiHashes
    }
    $msiStream=[IO.File]::Open((Join-Path $EvidenceDir 'wrapper-terminal.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$msiBytes=[Text.UTF8Encoding]::new($false).GetBytes(($msiTerminal | ConvertTo-Json -Depth 16)+[Environment]::NewLine);$msiStream.Write($msiBytes,0,$msiBytes.Length)}finally{$msiStream.Dispose()}
   }else{throw 'MSI_SD_STALE_ATTEMPT_REFUSED'}
  }elseif($msiPass){throw 'MSI_SD_ATTEMPT_MISSING'}
 } catch {
  [Console]::Error.WriteLine($_.Exception.ToString())
  $msiExit=2;$msiPass=$false
 }
 foreach($msiKey in $msiEnvironmentKeys){[Environment]::SetEnvironmentVariable($msiKey,$msiPreviousEnvironment[$msiKey],'Process')}
}
if($msiPass -and $msiExit -eq 0){Write-Output 'MSI_SD_REAR_CONSUMER_V1_COMPLETED positive=78/67093 faults=5/5 DefaultRelease'}
exit $msiExit
