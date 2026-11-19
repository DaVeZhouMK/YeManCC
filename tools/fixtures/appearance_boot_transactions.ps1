# Dot-sourced only from the original robustness consumer's explicit BootTransactions branch.
$data=[IO.File]::ReadAllText($BootModeCases,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($data.schema -ne 1 -or @($data.cases).Count -ne 4 -or (@($data.cases|ForEach-Object mode) -join '|') -ne 'global|xbox|steam|xbox-steam'){throw 'Expected exact four actual resolved boot-mode cases'}
$Out=[IO.Path]::GetFullPath($Out);$worker=Join-Path $PSScriptRoot 'appearance_boot_transaction_worker.ps1'
$exe=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
# Library intentionally returns before this public dispatcher is declared.
# Load that one exact production definition; do not copy its implementation.
$parseTokens=$null;$parseErrors=$null;$sourceAst=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$parseTokens,[ref]$parseErrors)
if(@($parseErrors).Count){throw 'Actual appearance source parse failed'}
$dispatcher=@($sourceAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-AppearanceControl'},$false))
if($dispatcher.Count -ne 1){throw 'Expected exactly one actual public appearance dispatcher'}
Invoke-Expression $dispatcher[0].Extent.Text
$originalMutex=${function:New-AppearanceMutex};$mutexText=$originalMutex.ToString();$mutexAnchor="'Local\YMCC.CssLoader.'"
if($mutexText.Split(@($mutexAnchor),[StringSplitOptions]::None).Count -ne 2){throw 'Original appearance mutex declaration changed'}
$actualRecover=${function:Recover-PresetTransaction};$recoverText=$actualRecover.ToString();$restoreAnchor='Restore-PresetBackup $rollback'
if($recoverText.Split(@($restoreAnchor),[StringSplitOptions]::None).Count -ne 2){throw 'Original transaction recovery declaration changed'}
$rows=New-Object Collections.Generic.List[object];$controls=New-Object Collections.Generic.List[object]
$events=Join-Path $Out 'boot-transactions-events.jsonl';$stream=New-Object IO.FileStream($events,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);$writer=New-Object IO.StreamWriter($stream,(New-Object Text.UTF8Encoding($false)))
function Quote-Fixture([string]$Text){return "'"+$Text.Replace("'","''")+"'"}
function Record-Fixture($Value){$writer.WriteLine(($Value|ConvertTo-Json -Depth 9 -Compress));$writer.Flush();$stream.Flush($true)}
try{
 # All boot modes control the same real theme target; prepare it once, then
 # each mode's actual resolved values become the next transaction preimage.
 Profile -Bundle;Invoke-PresetPrepare|Out-Null
 $indices=if($BootTransactionSelection -eq 'Remaining'){@(1,2,3,4)}else{@(0,1,2,3,4)}
 foreach($index in $indices){
  $negative=$index -eq 4;$case=if($negative){$data.cases[0]}else{$data.cases[$index]};$script:FixtureMode='offline'
  $status=Get-AppearanceStatus;Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=$case.states})|Out-Null
  $statesMatched=$true;foreach($property in $case.states.PSObject.Properties){$row=@(Get-ThemeRows|Where-Object name -eq $property.Name);if($row.Count -ne 1 -or $row[0].configured -ne $property.Value){$statesMatched=$false}}
  if(!$statesMatched){throw 'Actual resolved mode states were not applied to owned PS profile'}
  $owner=Join-Path (Get-PresetStateDir) 'owner.json';$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$oldOwner=Get-ConfigHash $owner;$oldRound=Get-ConfigHash $round;$undo=New-PresetBackup @($round)
  $nonce=[guid]::NewGuid().ToString('N');$prefix='Local\YMCC.CssLoader.Fixture.'+$nonce+'.';$readyName='Local\YMCC.AppearanceTransactionFixture.'+$nonce+'.ready'
  Set-Item -LiteralPath 'Function:\New-AppearanceMutex' -Value ([scriptblock]::Create($mutexText.Replace($mutexAnchor,("'"+$prefix+"'"))))
  $ready=New-Object Threading.EventWaitHandle($false,[Threading.EventResetMode]::ManualReset,$readyName);$process=$null;$processSignaled=$false;$mutexHeld=$false;$marker=Join-Path (Get-PresetStateDir) 'transaction.json'
  try{
   $command='& '+(Quote-Fixture $worker)+' -Source '+(Quote-Fixture $source)+' -FixtureRoot '+(Quote-Fixture $Out)+' -ThemeRoot '+(Quote-Fixture $script:ThemeRoot)+' -StateBase '+(Quote-Fixture $script:PresetStateBase)+' -Backup '+(Quote-Fixture $undo.directory)+' -ReadyName '+(Quote-Fixture $readyName)+' -MutexPrefix '+(Quote-Fixture $prefix)+' -Mode '+(Quote-Fixture ([string]$case.mode))
   $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));$process=Start-Process -FilePath $exe -ArgumentList @('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
   $pidOwned=$process.Id;$birth=$process.StartTime.ToUniversalTime().Ticks.ToString();$created=Get-Date
   if(!$ready.WaitOne(2500)){throw 'Owned PS transaction worker did not reach partial-write barrier'}
   $probe=New-AppearanceMutex 'ThemeControl';$probeHeld=$false
   try{$probeHeld=$probe.WaitOne(0);$mutexHeld=!$probeHeld}finally{if($probeHeld){$probe.ReleaseMutex()};$probe.Dispose()}
   $markerPresent=Test-Path -LiteralPath $marker -PathType Leaf;$changed=(Get-ConfigHash $owner) -ne $oldOwner -and (Get-ConfigHash $round) -ne $oldRound
   Record-Fixture ([pscustomobject]@{mode=$case.mode;negative=$negative;stage='partial-journaled-writes';pid=$pidOwned;birthTicks=$birth;mutexHeld=$mutexHeld;mutexPrefix=$prefix;markerPresent=$markerPresent;changed=$changed;expectedOwnerHash=$oldOwner;expectedThemeHash=$oldRound})
   if(!$mutexHeld -or !$markerPresent -or !$changed){throw 'Partial transaction and exact mutex ownership not observed'}
   $process.Kill();$processSignaled=$process.WaitForExit(2000);if(!$processSignaled){throw 'Owned transaction worker did not exit after termination'}
   if($negative){Set-Item -LiteralPath 'Function:\Recover-PresetTransaction' -Value ([scriptblock]::Create($recoverText.Replace($restoreAnchor,'# Private current-source negative: rollback call removed.')))}
   $result=Invoke-AppearanceControl 'Status'
   $themeRestored=(Get-ConfigHash $round) -eq $oldRound;$ownerRestored=(Get-ConfigHash $owner) -eq $oldOwner;$markerRemoved=!(Test-Path -LiteralPath $marker)
   $probe=New-AppearanceMutex 'ThemeControl';$heldAfter=$false;try{$heldAfter=$probe.WaitOne(0)}catch [Threading.AbandonedMutexException]{$heldAfter=$true}finally{if($heldAfter){$probe.ReleaseMutex()};$probe.Dispose()}
   $row=[pscustomobject]@{mode=$case.mode;negative=$negative;pid=$pidOwned;birthTicks=$birth;processSignaled=$processSignaled;markerBeforeRecovery=$markerPresent;themeRestored=$themeRestored;ownerRestored=$ownerRestored;markerRemoved=$markerRemoved;mutexHeldBeforeKill=$mutexHeld;mutexReleasedAfterRecovery=$heldAfter;statesMatched=$statesMatched;statusReturned=[bool]$result.ok}
   if($negative){$detected=!$themeRestored -and $ownerRestored -and $markerRemoved;$row|Add-Member -NotePropertyName mutationDetected -NotePropertyValue $detected;$controls.Add($row);Record-Fixture $row;if(!$detected){throw 'Recovery-call removal negative was not detected'}}else{$rows.Add($row);Record-Fixture $row;if(!$themeRestored -or !$ownerRestored -or !$markerRemoved -or !$heldAfter -or !$result.ok){throw 'Actual next Status did not restore exact transaction preimage'}}
  }finally{
   Set-Item -LiteralPath 'Function:\Recover-PresetTransaction' -Value $actualRecover
   if($process){if(!$process.HasExited){$process.Kill();if(!$process.WaitForExit(2000)){throw 'Owned worker cleanup not confirmed'}};$process.Dispose()};$ready.Dispose()
   if($negative -and (Test-Path -LiteralPath $round -PathType Leaf) -and (Get-ConfigHash $round) -ne $oldRound){Restore-PresetBackup $undo}
  }
 }
 [pscustomobject]@{ok=$true;selection=$BootTransactionSelection;scope='Actual resolved four appearance settings on the same real target; PS transaction interruption after two completed atomic writes, owned control mutex, next real Status recovery. Native owner/Job cancellation and mid-atomic interruption NOT_RUN.';rows=$rows.ToArray();controls=$controls.ToArray();mutexAdaptation='Only literal prefix changed in original New-AppearanceMutex function; actual Mutex/WaitOne/Release/Dispose and control catch/finally retained.';negativeAdaptation='Actual Recover-PresetTransaction scriptblock, only Restore-PresetBackup call removed.'}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $Out 'boot-transactions-result.json') -Encoding UTF8
 Write-Output 'PASS: four actual PS transaction recoveries and one actual recovery-call negative'
}finally{Set-Item -LiteralPath 'Function:\New-AppearanceMutex' -Value $originalMutex;Set-Item -LiteralPath 'Function:\Recover-PresetTransaction' -Value $actualRecover;$writer.Dispose();$stream.Dispose()}
