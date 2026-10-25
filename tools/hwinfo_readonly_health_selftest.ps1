[CmdletBinding()]
param(
 [string]$RecoveryScript='',
 [string]$BeforeScript='',
 [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
if(!$RecoveryScript){$RecoveryScript=Join-Path $PSScriptRoot '../PowerControl/YeManHWiNFO.ps1'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $PSScriptRoot '../../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-readonly-health-20261016'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$identity='YMCC_READONLY_HEALTH_'+[Guid]::NewGuid().ToString('N')
function Read-Functions([string]$Path){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($Path),[ref]$tokens,[ref]$errors)
 if($errors.Count){throw 'Production helper parse failure'}
 $result=@{};foreach($f in @($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$false))){$result[$f.Name]=$f.Extent.Text}
 return $result
}
function Isolate-Names([string]$Text){
 foreach($pair in @(@('Global\HWiNFO_SENS_SM2',($identity+'_0')),@('HWiNFO_SENS_SM2',($identity+'_1')),@('Global\HWiNFO_SENS_SM',($identity+'_2')),@('HWiNFO_SENS_SM',($identity+'_3')))){$Text=$Text.Replace($pair[0],$pair[1])}
 return $Text.Replace('Global\YeManCC_HWiNFO_Recovery',($identity+'_mutex'))
}
$functions=Read-Functions $RecoveryScript
$afterSource=Isolate-Names $functions['Test-HWiNFOSharedMemory']
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'AFTER-ISOLATED-PROBE.ps1'),$afterSource)
$beforeFunctions=if($BeforeScript){Read-Functions $BeforeScript}else{@{}}
$beforeSource=if($BeforeScript){Isolate-Names $beforeFunctions['Test-HWiNFOSharedMemory']}else{
 $s=$afterSource
 $s=[regex]::Replace($s,'\[System.IO.MemoryMappedFiles.MemoryMappedFile\]::OpenExisting\(\s*\$name\s*,\s*\[System.IO.MemoryMappedFiles.MemoryMappedFileRights\]::Read\s*\)','[System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($name)')
 $s=$s.Replace('$mmf.CreateViewAccessor(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)','$mmf.CreateViewAccessor(0, 0)')
 $s
}
# Run exact production probes; only named mapping identities are isolated.
$afterProbe=[scriptblock]::Create($afterSource.Replace('function Test-HWiNFOSharedMemory','function Test-AfterProbe'))
$beforeProbe=[scriptblock]::Create($beforeSource.Replace('function Test-HWiNFOSharedMemory','function Test-BeforeProbe'))
. $afterProbe
. $beforeProbe
$script:clock=1700000000000L
function Get-HWiNFOClockMs{return $script:clock}
$script:checks=New-Object Collections.ArrayList
$script:observations=New-Object Collections.ArrayList
function Check([string]$Name,[bool]$Value){if(!$Value){throw $Name};[void]$script:checks.Add($Name)}
function Proof([string]$Side,[int]$Period=0){$p=$null;$ok=if($Side -eq 'before'){Test-BeforeProbe -ExpectedIntervalMs $Period -ProducerProof ([ref]$p)}else{Test-AfterProbe -ExpectedIntervalMs $Period -ProducerProof ([ref]$p)};return @{healthy=[bool]$ok;proof=$p}}
$maps=New-Object Collections.ArrayList
$views=New-Object Collections.ArrayList
function Fixture([int]$Slot,[string]$Rights){
 $acl=New-Object IO.MemoryMappedFiles.MemoryMappedFileSecurity;$acl.SetAccessRuleProtection($true,$false)
 $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
 $allow=if($Rights -eq 'read'){[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read}elseif($Rights -eq 'write'){[IO.MemoryMappedFiles.MemoryMappedFileRights]::Write}else{[IO.MemoryMappedFiles.MemoryMappedFileRights]::ReadWrite}
 $acl.AddAccessRule((New-Object Security.AccessControl.AccessRule[IO.MemoryMappedFiles.MemoryMappedFileRights]($sid,$allow,[Security.AccessControl.AccessControlType]::Allow)))
 $map=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(($identity+'_'+$Slot),2048,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite,[IO.MemoryMappedFiles.MemoryMappedFileOptions]::None,$acl,[IO.HandleInheritability]::None)
 $view=$map.CreateViewAccessor();[void]$maps.Add($map);[void]$views.Add($view)
 $view.Write(0,[uint32]0x53695748);$view.Write(4,[uint32]2);$view.Write(8,[uint32]1);$view.Write(12,[int64]1700000000);$view.Write(20,[uint32]48);$view.Write(24,[uint32]392);$view.Write(28,[uint32]1);$view.Write(32,[uint32]440);$view.Write(36,[uint32]460);$view.Write(40,[uint32]1);$view.Write(44,[uint32]3000)
 return @{map=$map;view=$view}
}
function Dispose-Fixtures{
 foreach($v in $views){$v.Dispose()};$views.Clear();foreach($m in $maps){$m.Dispose()};$maps.Clear()
}
try{
 Check 'absent map stays unhealthy before and after' (!(Proof before).healthy -and !(Proof after).healthy)
 $fixture=Fixture 1 'read';$v=$fixture.view
 Check 'original actual probe rejects a valid map when only read access is granted' (!(Proof before).healthy)
 $fresh=Proof after
 Check 'current actual probe accepts the same read-only map' ($fresh.healthy -and $fresh.proof.PollTime -eq 1700000000)
 [void]$observations.Add(@{mapping='read-only';beforeHealthy=(Proof before).healthy;afterHealthy=$fresh.healthy;producerProof=$fresh.proof})
 Check 'read-only map proves an explicit 3000ms producer interval' ((Proof after 3000).healthy)
 Check 'interval mismatch is not mistaken for healthy read-only sharing' (!(Proof after 1000).healthy)
 $access=$null;$readView=$null;$writeDenied=$false
 try{$access=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting((($identity+'_1')),[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read);$readView=$access.CreateViewAccessor(0,0,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read);try{$readView.Write(44,[uint32]1000)}catch{$writeDenied=$true}}
 finally{if($readView){$readView.Dispose()};if($access){$access.Dispose()}}
 Check 'accepted health handle/view still cannot write producer bytes' ($writeDenied -and $v.ReadUInt32(44) -eq 3000)
 $polls=New-Object Collections.ArrayList
 for($i=0;$i -lt 6;$i++){$v.Write(12,[int64](1700000000+$i*3));$script:clock=1700000000000L+$i*3000L;$r=Proof after;Check ('real advancing read-only producer observation '+$i) $r.healthy;[void]$polls.Add($r.proof.PollTime)}
 Check 'producer progression is real and monotonic' ($polls[5] -gt $polls[0])
 $script:clock=1700000000000L;$v.Write(12,[int64]1700000000)
 foreach($period in @(1000,1500,2000,3000)){
  $v.Write(44,[uint32]$period)
  Check ('matching allowed interval '+$period+' works without write permission') ((Proof after $period).healthy)
  $script:clock=1700000006000L;Check ('stale producer '+$period+' is not rescued by access relaxation') (!(Proof after $period).healthy);$script:clock=1700000000000L
 }
 $v.Write(44,[uint32]3000)
 foreach($poll in @(0,-1,1700000010,9223372036854776)){$v.Write(12,[int64]$poll);Check ('invalid producer timestamp '+$poll+' remains rejected') (!(Proof after).healthy)};$v.Write(12,[int64]1700000000)
 $v.Write(0,[uint32]0);Check 'bad signature remains rejected despite readable ACL' (!(Proof after).healthy);$v.Write(0,[uint32]0x53695748)
 $v.Write(40,[uint32]0);Check 'zero readings remains rejected' (!(Proof after).healthy);$v.Write(40,[uint32]1)
 $v.Write(32,[uint32]($v.Capacity-8));Check 'out of bounds reading remains rejected' (!(Proof after).healthy);$v.Write(32,[uint32]440)
 # Test the ACTUAL healthy-start transaction: mock only process/configuration
 # side effects, use a unique mutex and the unchanged production health function.
 foreach($name in @('Invoke-HWiNFORecovery','Test-HWiNFOHealth','Invoke-HWiNFORecoveryEntry')){. ([scriptblock]::Create((Isolate-Names $functions[$name])))}
 $script:stops=0;$script:plans=0;$script:copies=0;$script:launches=0;$script:registryCalls=0;$script:resolves=0;$script:useAfter=$true
 $script:Mode='start';$script:Elevated=$false;$script:SensorIntervalMs=0;$script:ExecutablePath=''
 function Test-HWiNFOSharedMemory{param([int]$ExpectedIntervalMs=0,[ref]$ProducerProof)if($script:useAfter){return Test-AfterProbe -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof $ProducerProof}else{return Test-BeforeProbe -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof $ProducerProof}}
 function Get-Process{[CmdletBinding()]param([string]$Name,[int]$Id)return [pscustomobject]@{Id=123;ProcessorAffinity=[IntPtr]0}}
 function Resolve-HWiNFO{$script:resolves++;return 'C:\YMCC-Isolated-Never-Executed\HWiNFO64.exe'}
 function Get-HWiNFOConfigPlan{param($Destination,$RequestedIntervalMs)$script:plans++;return @{}}
 function Stop-HWiNFO{$script:stops++}
 function Copy-HWiNFOConfig{param($Destination)$script:copies++}
 function Invoke-HWiNFOSensorBudget{param($Executable,$Destination)$script:registryCalls++}
 function Start-Process{[CmdletBinding()]param($FilePath,$WorkingDirectory,$WindowStyle,[switch]$PassThru)$script:launches++;throw 'Inert launch boundary: no real process is launched'}
 for($i=0;$i -lt 8;$i++){Check ('healthy read-only start '+$i+' is a successful no-op') ((Invoke-HWiNFORecovery) -eq 0)}
 Check 'healthy read-only starts perform zero stop/config/registry/launch work' ($stops -eq 0 -and $plans -eq 0 -and $copies -eq 0 -and $launches -eq 0 -and $registryCalls -eq 0 -and $resolves -eq 0)
 $script:SensorIntervalMs=1500;Check 'healthy explicit period change is still deferred' ((Invoke-HWiNFORecovery) -eq 12);$script:SensorIntervalMs=0
 $script:useAfter=$false;try{[void](Invoke-HWiNFORecovery)}catch{}
 Check 'old access policy would enter the unnecessary recovery path' ($stops -eq 1 -and $plans -eq 1 -and $copies -eq 1 -and $registryCalls -eq 1)
 [void]$observations.Add(@{case='read-only healthy start';oldRecoveryWork=@{stops=$stops;plans=$plans;copies=$copies;registry=$registryCalls};newStarts=8;newRecoveryWork=0;realProcessOperations=0})
 Dispose-Fixtures
 foreach($slot in @(0,1,2,3)){$f=Fixture $slot 'read';Check ('read-only alternative mapping slot '+$slot+' works') ((Proof after).healthy);Dispose-Fixtures}
 $f=Fixture 1 'write';Check 'write-only object does not become valid input' (!(Proof after).healthy);Dispose-Fixtures
 $f=Fixture 0 'write';$f2=Fixture 1 'read';Check 'unreadable first name falls through to the existing readable alias' ((Proof after).healthy);Dispose-Fixtures
 $f=Fixture 1 'both';Check 'ordinary read-write producer remains accepted before and after' ((Proof before).healthy -and (Proof after).healthy)
 $result=@{ok=$true;checks=$checks.Count;cases=@($checks.ToArray());observations=@($observations.ToArray());actualProductionProbeAndHealthyStart=$true;beforeActualScript=[bool]$BeforeScript;realNamedMappings=$true;realHwinfoMap=$false;realProcessOperations=$false;registryWrites=$false;cpuPercentageOrTimingMeasured=$false;helperSha256=(Get-FileHash -LiteralPath $RecoveryScript -Algorithm SHA256).Hash}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'READONLY-HEALTH-RESULT.json'),($result|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
 Write-Output ('READONLY_HEALTH_PASS '+$checks.Count+' checks; actual production probe/healthy-start, isolated Windows mappings')
}finally{Dispose-Fixtures}
