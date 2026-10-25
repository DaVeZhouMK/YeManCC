[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$ProcessFixture)
$ErrorActionPreference='Stop';$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$ev=[IO.Path]::GetFullPath($EvidenceDir)
[IO.Directory]::CreateDirectory($ev)|Out-Null
. (Join-Path $repo 'native/module_package_maintenance.ps1')
. (Join-Path $repo 'native/updater_hook_tools.ps1')
$checks=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[scriptblock]$Test){try{&$Test;$checks.Add(@{name=$Name;status='PASS'})}catch{$checks.Add(@{name=$Name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace})}}
function Need([bool]$Pass,[string]$Why){if(!$Pass){throw $Why}}
function Launch([string]$Folder,[string]$Name,[string]$Mode){$dir=Join-Path $ev $Folder;[IO.Directory]::CreateDirectory($dir)|Out-Null;$exe=Join-Path $dir $Name;if(Test-Path -LiteralPath $exe){if((Get-FileHash -LiteralPath $exe).Hash -cne (Get-FileHash -LiteralPath $ProcessFixture).Hash){throw 'FIXTURE_IMAGE_CHANGED'}}else{Copy-Item -LiteralPath $ProcessFixture -Destination $exe -Force};$p=Start-Process -FilePath $exe -ArgumentList $Mode -PassThru -WindowStyle Hidden;Start-Sleep -Milliseconds 150;$owned.Add($p);return @{exe=$exe;process=$p}}
try{
 Check 'absent recognized tools return without changing other programs' {Stop-YmccUpdateHookTools @((Join-Path $ev 'absent/OptiscalerClient.exe'),(Join-Path $ev 'absent/LosslessScaling.exe'))}
 Check 'arbitrary image path rejected' {$rejected=$false;try{Stop-YmccUpdateHookTools @((Join-Path $ev 'foreign.exe'))}catch{$rejected=$_.Exception.Message -match 'PATH_INVALID'};Need $rejected 'foreign image admitted'}
 Check 'normal OptiscalerClient exits gracefully' {$a=Launch 'normal' 'OptiscalerClient.exe' 'normal';Stop-YmccUpdateHookTools @($a.exe) 1000 2000;Need $a.process.HasExited 'OptiscalerClient remained alive'}
 Check 'hung LosslessScaling exits within bounded force budget' {$a=Launch 'hung' 'LosslessScaling.exe' 'hung';$clock=[Diagnostics.Stopwatch]::StartNew();Stop-YmccUpdateHookTools @($a.exe) 100 2000;Need $a.process.HasExited 'LosslessScaling remained alive';Need ($clock.ElapsedMilliseconds -lt 5000) 'unbounded tool shutdown'}
 Check 'same name at an unrelated path stays alive' {$foreign=Launch 'unrelated' 'LosslessScaling.exe' 'hung';$a=Launch 'owned' 'LosslessScaling.exe' 'hung';Stop-YmccUpdateHookTools @($a.exe) 0 2000;Need $a.process.HasExited 'owned process stayed alive';Need (!$foreign.process.HasExited) 'unrelated process was closed'}
 Check 'changed creation identity is rejected without closing' {$a=Launch 'identity' 'OptiscalerClient.exe' 'hung';$rejected=$false;try{Stop-YmccUpdaterParent $a.process.Id $a.exe '0' 0 1000}catch{$rejected=$_.Exception.Message -match 'IDENTITY_CHANGED'};Need $rejected 'wrong birth admitted';Need (!$a.process.HasExited) 'wrong identity killed'}
 Check 'all authenticated instances at the same path exit' {$a=Launch 'multi' 'OptiscalerClient.exe' 'normal';$b=Launch 'multi' 'OptiscalerClient.exe' 'hung';Stop-YmccUpdateHookTools @($a.exe) 100 2000;Need ($a.process.HasExited -and $b.process.HasExited) 'same-path instance survived'}
 Check 'watchdog relaunch aborts installation without repeated closing' {
  $a=Launch 'watchdog' 'LosslessScaling.exe' 'hung'
  $originalHookParentStop=(Get-Command Stop-YmccUpdaterParent -CommandType Function).ScriptBlock
  $script:hookWatchdogStopCalls=0;$script:hookWatchdogReplacement=$null
  # Exercise the real close, then inject only the external restart timing.
  # The final production inventory must observe the real replacement process.
  function Stop-YmccUpdaterParent([int]$ProcessId,[string]$ExpectedPath,[string]$CreationFileTime,[int]$GraceMilliseconds=20000,[int]$ForceMilliseconds=15000) {
   $script:hookWatchdogStopCalls++
   $mode=& $originalHookParentStop $ProcessId $ExpectedPath $CreationFileTime $GraceMilliseconds $ForceMilliseconds
   $script:hookWatchdogReplacement=Launch 'watchdog' 'LosslessScaling.exe' 'hung'
   return $mode
  }
  $rejected=$false
  try{Stop-YmccUpdateHookTools @($a.exe) 0 2000}catch{$rejected=$_.Exception.Message -match 'UPDATE_HOOK_TOOL_RESTARTED'}
  Need $rejected 'watchdog relaunch did not return an explicit retryable failure'
  Need $a.process.HasExited 'original tool did not exit'
  Need ($script:hookWatchdogStopCalls -eq 1) 'watchdog triggered repeated shutdown attempts'
  Need ($null -ne $script:hookWatchdogReplacement -and !$script:hookWatchdogReplacement.process.HasExited) 'replacement was closed by a retry loop'
 }
 Check 'empty allowlist performs no process discovery' {Stop-YmccUpdateHookTools @()}
}finally{foreach($p in $owned){try{if(!$p.HasExited){$p.Kill();$null=$p.WaitForExit(2000)}}finally{$p.Dispose()}}}
$failed=@($checks|Where-Object status -eq 'FAIL');$result=@{status=$(if($failed.Count){'FAIL'}else{'PASS'});checks=@($checks);actualExternalToolsControlled=$false;processes='inert Win32 fixtures with exact owned paths';sources=@('native/module_package_maintenance.ps1','native/updater_hook_tools.ps1')|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash}}}
$result.powerShellVersion=$PSVersionTable.PSVersion.ToString()
$result.powerShellEdition=[string]$PSVersionTable.PSEdition
$result.harness=@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash}
$result.fixture=@{path=[IO.Path]::GetFullPath($ProcessFixture);sha256=(Get-FileHash -LiteralPath $ProcessFixture).Hash}
$result.watchdogScenario=@{restartTimingInjected=$true;actualProductionStopExecuted=$true;replacementWasRealOwnedProcess=$true;productionFinalInventoryExecuted=$true;externalWatchdogExecuted=$false}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $ev 'result.json') -Encoding UTF8
$checks|Format-Table name,status
if($failed.Count){exit 1}
