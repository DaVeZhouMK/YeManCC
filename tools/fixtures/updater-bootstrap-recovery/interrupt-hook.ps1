$script:FixtureRemoveFile=(Get-Command Remove-YmccBootstrapStageFile).ScriptBlock
$script:FixtureWriteStage=(Get-Command Write-YmccBootstrapStage).ScriptBlock
$script:FixtureSetAtomic=(Get-Command Set-YmccDeckyAtomic).ScriptBlock
function Write-YmccBootstrapStage([string]$Stage,[string]$Target,[string]$PackageVersion,[string]$PayloadSha256,[string]$Phase,[string[]]$ManagedPaths){
 & $script:FixtureWriteStage $Stage $Target $PackageVersion $PayloadSha256 $Phase $ManagedPaths
 if($script:FixturePhase -ceq 'source-read' -and $Phase -ceq 'preparing'){
  [IO.File]::WriteAllText($script:FixtureBarrier,$Stage)
  $wait=[Diagnostics.Stopwatch]::StartNew()
  while(![IO.File]::Exists($script:FixtureSourceRelease) -and $wait.ElapsedMilliseconds -lt 6000){[Threading.Thread]::Sleep(20)}
  if(![IO.File]::Exists($script:FixtureSourceRelease)){throw 'SOURCE_LEASE_FIXTURE_RELEASE_TIMEOUT'}
 }
 if($Phase -ceq $script:FixturePhase){[IO.File]::WriteAllText($script:FixtureBarrier,$Stage);[Threading.Thread]::Sleep([Threading.Timeout]::Infinite)}
}
function Set-YmccDeckyAtomic([string]$Root,[string]$Relative,[string]$Source,[byte[]]$Bytes,[string]$Expected){
 if($script:FixturePhase -eq 'receipt-before' -and $Root -ieq $script:FixtureTarget -and $Relative -ceq '.ymcc-input-managed.json'){
  [IO.File]::WriteAllText($script:FixtureBarrier,$Root);[Threading.Thread]::Sleep([Threading.Timeout]::Infinite)
 }
 & $script:FixtureSetAtomic $Root $Relative $Source $Bytes $Expected
 if($script:FixturePhase -eq 'applying-middle' -and $Root -ieq $script:FixtureTarget -and $Relative -ceq 'feature-assets/virtual-gamepad/YeManInputHost.dll'){
  [IO.File]::WriteAllText($script:FixtureBarrier,$Root);[Threading.Thread]::Sleep([Threading.Timeout]::Infinite)
 }
}

function Remove-YmccBootstrapStageFile([string]$Stage,[string]$File){
 & $script:FixtureRemoveFile $Stage $File
 if($script:FixturePhase -eq 'cleanup-middle' -and [IO.Path]::GetFileName($File) -cne '.ymcc-bootstrap-stage.json'){
  [IO.File]::WriteAllText($script:FixtureBarrier,$Stage);[Threading.Thread]::Sleep([Threading.Timeout]::Infinite)
 }
}
