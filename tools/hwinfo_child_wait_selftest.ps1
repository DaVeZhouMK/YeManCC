[CmdletBinding()]
param([string]$RecoveryScript='',[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
if(!$RecoveryScript){$RecoveryScript=Join-Path $PSScriptRoot '../PowerControl/YeManHWiNFO.ps1'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $PSScriptRoot '../../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-elevated-entry-20261016'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);[void][IO.Directory]::CreateDirectory($OutputDirectory)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($RecoveryScript),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Production script parse failure'}
foreach($name in @('Fail','Wait-HWiNFORecoveryChild','Invoke-HWiNFORecoveryEntry')){$fn=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false);if(!$fn){throw ('Production function missing: '+$name)};. ([scriptblock]::Create($fn.Extent.Text))}
$checks=New-Object Collections.ArrayList
function Check([string]$Name,[bool]$Value){if(!$Value){throw $Name};[void]$checks.Add($Name)}
function Make-Child($Code=0,[bool]$Completes=$true,[bool]$HandleOK=$true,[bool]$ThrowWait=$false){
 $p=[pscustomobject]@{ExitCode=$Code;Completes=$Completes;HandleOK=$HandleOK;ThrowWait=$ThrowWait;Events=(New-Object Collections.ArrayList);Disposed=$false}
 $p|Add-Member ScriptProperty Handle {[void]$this.Events.Add('handle');if($this.HandleOK){return [IntPtr]1234};return [IntPtr]::Zero}
 $p|Add-Member ScriptMethod WaitForExit {param([int]$milliseconds)[void]$this.Events.Add('wait');if($milliseconds -ne 60000){throw 'wait is not bounded'};if($this.Events[0] -ne 'handle'){throw 'handle not captured first'};if($this.ThrowWait){throw 'inert process wait failure'};return $this.Completes}
 $p|Add-Member ScriptMethod Dispose {[void]$this.Events.Add('dispose');$this.Disposed=$true}
 return $p
}
function Failure-Code($p){try{[void](Wait-HWiNFORecoveryChild $p);return 0}catch{return $_.Exception.Data['HWiNFOExitCode']}}
foreach($code in @(0,1,4,5,7,11,12,13,14,15,-1)){$p=Make-Child $code;$actual=Wait-HWiNFORecoveryChild $p;Check ('exact completed child preserves exit code '+$code) ($actual -eq $code);Check ('child handle closes for code '+$code) ($p.Disposed -and ($p.Events -join ',') -eq 'handle,wait,dispose')}
$p=Make-Child 0 $false;Check 'timeout is explicit code 15 rather than false success' ((Failure-Code $p) -eq 15 -and $p.Disposed)
$p=Make-Child 0 $true $false;Check 'missing exact handle is code 15 and closes returned wrapper' ((Failure-Code $p) -eq 15 -and $p.Disposed -and !$p.Events.Contains('wait'))
$p=Make-Child $null;Check 'unknown exit code never becomes successful exit zero' ((Failure-Code $p) -eq 15 -and $p.Disposed)
$p=Make-Child '0';Check 'untyped string exit code remains unconfirmed' ((Failure-Code $p) -eq 15 -and $p.Disposed)
$p=Make-Child 0 $true $true $true;Check 'OS wait failure is code 15 and closes only wrapper' ((Failure-Code $p) -eq 15 -and $p.Disposed)
Check 'missing returned child stays unconfirmed' ((Failure-Code $null) -eq 15)
# Actual outer handoff; mock only access failure and process creation.
$script:Mode='restart';$script:Elevated=$false;$script:SensorIntervalMs=0;$script:ExecutablePath='';$script:nextChild=$null;$script:verb='';$script:treeWait=$null
function Invoke-HWiNFORecovery{Fail 4 'inert stop access denied' -NeedsElevation}
function Start-Process{[CmdletBinding()]param($FilePath,$Verb,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$Wait,[switch]$PassThru)$script:verb=$Verb;$script:treeWait=[bool]$Wait;return $script:nextChild}
$script:nextChild=Make-Child 0;Check 'actual outer UAC entry returns exact child success' ((Invoke-HWiNFORecoveryEntry) -eq 0 -and $verb -eq 'RunAs' -and !$treeWait)
$script:nextChild=Make-Child 14;Check 'actual outer UAC entry propagates exact nonzero result' ((Invoke-HWiNFORecoveryEntry) -eq 14)
$script:nextChild=Make-Child $null;Check 'actual outer entry exposes missing receipt as code 15' ((Invoke-HWiNFORecoveryEntry) -eq 15)
$script:nextChild=Make-Child 0 $false;Check 'actual outer entry exposes child timeout as code 15' ((Invoke-HWiNFORecoveryEntry) -eq 15)
# Real harmless Windows processes. The descendant outlives the recovery parent.
$caseDirectory=Join-Path $OutputDirectory ('inert child space '+[Guid]::NewGuid().ToString('N'));[void][IO.Directory]::CreateDirectory($caseDirectory)
$idPath=Join-Path $caseDirectory 'descendant.pid';$descScript=Join-Path $caseDirectory 'Idle Descendant.ps1';$parentScript=Join-Path $caseDirectory 'Recovery Parent.ps1'
[IO.File]::WriteAllText($descScript,'Start-Sleep -Seconds 20',[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures/hwinfo_inert_recovery_parent.ps1') -Destination $parentScript
$descendant=$null
try{
 $p=Microsoft.PowerShell.Management\Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$parentScript+'"'),'-ChildScript',('"'+$descScript+'"'),'-IdPath',('"'+$idPath+'"')) -WindowStyle Hidden -PassThru
 $result=Wait-HWiNFORecoveryChild $p
 Check 'actual parent returns its own exit code despite live descendant' ($result -eq 7 -and (Test-Path -LiteralPath $idPath))
 $childId=[int][IO.File]::ReadAllText($idPath);$descendant=Get-Process -Id $childId -ErrorAction Stop;$handle=$descendant.Handle
 Check 'bounded wait does not wait for or terminate intended descendant' (!$descendant.HasExited -and $descendant.ProcessName -eq 'powershell')
 $real=@{parentExitCode=$result;descendantId=$childId;descendantAliveWhenParentWaitReturned=(!$descendant.HasExited);noHwinfoOrUacInThisProcessFixture=$true}
}finally{if($descendant){if(!$descendant.HasExited){$descendant.Kill();[void]$descendant.WaitForExit(5000)};$descendant.Dispose()}}
$r=@{ok=$true;checks=$checks.Count;cases=@($checks.ToArray());realProcessFixture=$real;actualProductionWaitAndOuterEntry=$true;realHardwareOrHwinfoChanged=$false;cpuTimingOrPercentageMeasured=$false;helperSha256=(Get-FileHash -LiteralPath $RecoveryScript -Algorithm SHA256).Hash}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'CHILD-WAIT-RESULT.json'),($r|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
Write-Output ('HWINFO_CHILD_WAIT_PASS '+$checks.Count+' checks; actual production wait/outer entry and inert real process tree')
