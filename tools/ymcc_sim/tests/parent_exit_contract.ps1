[CmdletBinding()]
param(
 [string]$HostPayloadRoot='',
 [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $repo 'tools\ymcc_sim\lib\sim_core.ps1')
if(-not $HostPayloadRoot){$HostPayloadRoot=Split-Path -Parent (Get-SimDefaults).hostExe}
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $EvidenceDir){throw 'Fresh parent-exit contract evidence directory required'}
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
$hostPath=Join-Path $HostPayloadRoot 'YeManFanHost.dll'
$hostHash=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
$engine=Join-Path $PSHOME 'pwsh.exe'
if(-not(Test-Path -LiteralPath $engine)){$engine=(Get-Command pwsh.exe -ErrorAction Stop).Source}
$phases=@(
 @{id='actual-production-finalize-and-recovery';script=(Join-Path $repo 'tools\FanFinalizeContractSelfTest\Run-SelfTest.ps1');arguments=@('-HostPayloadRoot',$HostPayloadRoot,'-EvidenceRoot',(Join-Path $EvidenceDir 'engine'));receipt=(Join-Path $EvidenceDir 'engine\verification.json')},
 @{id='normal-parent-exit-natural-zero';script=(Join-Path $repo 'tools\fan_host_parent_watchdog_selftest.ps1');arguments=@('-HostRoot',$HostPayloadRoot,'-EvidenceDir',(Join-Path $EvidenceDir 'process'));receipt=(Join-Path $EvidenceDir 'process\verification.json')}
)
$results=@()
foreach($phase in $phases){
 $began=[DateTime]::UtcNow
 $phaseArguments=@($phase.arguments)
 & $engine -NoProfile -NonInteractive -File $phase.script @phaseArguments *> (Join-Path $EvidenceDir ($phase.id+'.log'))
 $code=$LASTEXITCODE
 $exists=Test-Path -LiteralPath $phase.receipt -PathType Leaf
 $value=if($exists){Get-Content -LiteralPath $phase.receipt -Raw -Encoding UTF8|ConvertFrom-Json}else{$null}
 $fresh=$exists -and (Get-Item -LiteralPath $phase.receipt).LastWriteTimeUtc -ge $began.AddSeconds(-1)
 $ok=$code -eq 0 -and $fresh -and $value.status -eq 'PASS' -and $value.passed -eq $true
 if($phase.id -eq 'normal-parent-exit-natural-zero'){$ok=$ok -and $value.hostDllSha256 -eq $hostHash -and $value.hostExitCode -eq 0 -and $value.forcedCleanup -eq $false}
 if($phase.id -eq 'actual-production-finalize-and-recovery'){
  $bindings=@($value.identities|Where-Object { [IO.Path]::GetFileName($_.file) -eq 'YeManFanHost.dll' })
  $ok=$ok -and $bindings.Count -eq 1 -and $bindings[0].sha256 -eq $hostHash -and @($value.nativeAttempts).Count -eq 0 -and $value.knownBadReadyTerminalRejected -eq $true
 }
 $results+=@{phase=$phase.id;passed=[bool]$ok;exitCode=$code;fresh=$fresh;receipt=$phase.receipt}
}
$unchanged=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hostHash
$passed=$results.Count -eq 2 -and @($results|Where-Object {$_.passed -ne $true}).Count -eq 0 -and $unchanged
[ordered]@{status=$(if($passed){'PASS'}else{'FAIL'});passed=[bool]$passed;phases=$results;hostPayloadRoot=[IO.Path]::GetFullPath($HostPayloadRoot);hostDllSha256=$hostHash;hostUnchanged=$unchanged;hardwareAcceptance=$false;realBackend=$false;normalExitAndHonestFailureTested=$true}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceDir 'verification.json') -Encoding UTF8
Write-Output ('PARENT_EXIT_CONTRACT '+$(if($passed){'PASS'}else{'FAIL'}))
if(-not $passed){exit 2}
exit 0
