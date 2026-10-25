# Separate real HC ManagerFactory regression, then unmodified FanHost EXE public mock API.
# Neither a forced exit nor watchdog 72 can pass. No ambient credentials are read.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RuntimeRoot,
 [Parameter(Mandatory=$true)][string]$SharedRuntimeRoot,
 [Parameter(Mandatory=$true)][string]$HostPayloadRoot,
 [Parameter(Mandatory=$true)][string]$OutDir
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$OutDir=[IO.Path]::GetFullPath($OutDir).TrimEnd('\')
$checks=[Collections.Generic.List[object]]::new()
$process=$null;$hostStopped=$false;$failure=$null;$forcedFixtureCleanup=$false;$hostExitCode=$null;$authFile=$null
if(Test-Path -LiteralPath (Join-Path $OutDir 'verification.json')){throw 'Evidence must be fresh; refusing to overwrite verification.json.'}
function Check([string]$Name,[bool]$Pass,$Detail=$null){
 $checks.Add([pscustomobject]@{name=$Name;status=$(if($Pass){'PASS'}else{'FAIL'});detail=$Detail})
 if(-not $Pass){throw ('CHECK_FAILED: '+$Name)}
}
try {
 if($OutDir.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence/build output must be outside product source.'}
 New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
 foreach($item in @(@($RuntimeRoot,'HandheldCompanion.dll'),@($SharedRuntimeRoot,'Newtonsoft.Json.dll'),@($HostPayloadRoot,'YeManFanHost.dll'))){Check ('input '+$item[1]) (Test-Path -LiteralPath (Join-Path $item[0] $item[1]))}
 . (Join-Path $repo 'tools\hc-device-runtime\verify-runtime.ps1')
 $runtimeReceipt=Assert-YmccDeviceCoreRuntime -ProjectRoot $repo -RuntimeRoot $RuntimeRoot
 Check 'exact approved runtime closure' ($runtimeReceipt.status -eq 'PASS') $runtimeReceipt
 # This established test includes an actual known-bad audio ABI negative control.
 & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File (Join-Path $repo 'tools\HcManagerInitializationSelfTest\Run-SelfTest.ps1') -RuntimeRoot $RuntimeRoot -SharedRuntimeRoot $SharedRuntimeRoot -EvidenceRoot (Join-Path $OutDir 'manager-regression') *> (Join-Path $OutDir 'manager-regression.log')
 $managerCode=$LASTEXITCODE
 $managerReport=Get-Content -LiteralPath (Join-Path $OutDir 'manager-regression\verification.json') -Raw | ConvertFrom-Json
 Check 'real 13 managers and real known-bad audio regression' ($managerCode -eq 0 -and $managerReport.status -eq 'PASS' -and $managerReport.approvedManagerCount -eq 13 -and $managerReport.knownBadReproduced) $managerReport

 $approved=Get-Content -LiteralPath (Join-Path $OutDir 'manager-regression\approved-runtime\result.json') -Raw|ConvertFrom-Json
 $hcHash=(Get-FileHash -LiteralPath (Join-Path $RuntimeRoot 'HandheldCompanion.dll')).Hash.ToLowerInvariant()
 $hostHash=(Get-FileHash -LiteralPath (Join-Path $HostPayloadRoot 'YeManFanHost.dll')).Hash.ToLowerInvariant()
 $audioBindings=@($approved.bindings|Where-Object {($_.assembly -split ',')[0] -in @('NAudio.Core','NAudio.Wasapi')})
 # Only Wasapi is necessarily resolved by these constructors; never claim an unexecuted Core path loaded.
 $audioMatches=@($audioBindings|Where-Object {$_.assembly -like 'NAudio.Wasapi,*'}).Count -eq 1
 foreach($binding in $audioBindings){
  $name=($binding.assembly -split ',')[0]+'.dll'
  $expected=(Get-FileHash -LiteralPath (Join-Path $RuntimeRoot $name)).Hash.ToLowerInvariant()
  if($binding.sha256 -cne $expected){$audioMatches=$false}
 }
 Check 'real constructor child bound to supplied runtime and actual repaired audio bytes' ($approved.ok -eq $true -and $approved.managerCount -eq 13 -and [IO.Path]::GetFullPath($approved.runtimeRoot) -eq [IO.Path]::GetFullPath($RuntimeRoot) -and $audioMatches) @{managerCount=$approved.managerCount;hcSha256=$hcHash;audioBindings=$audioBindings;separateRealHcProcess=$true}
 $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
 $auth=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N')
 $authDir=Join-Path $OutDir ('api-fixture\ai-fan-sessions\'+[guid]::NewGuid().ToString('D')+'\fan-host')
 New-Item -ItemType Directory -Path $authDir -Force|Out-Null
 $authFile=Join-Path $authDir 'YeManFanHost.session'
 # Fresh synthetic test value only. The harness never reads any token/session file.
 [IO.File]::WriteAllText($authFile,$auth,[Text.UTF8Encoding]::new($false))
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=Join-Path $HostPayloadRoot 'YeManFanHost.exe';$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.WorkingDirectory=$OutDir
 $info.Environment['YEMANCC_SELFTEST_LOG_ROOT']=Join-Path $OutDir 'host-logs'
 foreach($a in @('--port',[string]$port,'--parent-pid',[string]$PID,'--protocol-version','2','--hc-runtime-root',$RuntimeRoot,'--session-token-file',$authFile,'--mock-handshake','--mock-zero-hardware-evidence')){$info.ArgumentList.Add($a)}
 $process=[Diagnostics.Process]::Start($info)
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 . (Join-Path $repo 'tools\ymcc_sim\lib\sim_core.ps1')
 $base='http://127.0.0.1:'+$port
 $ready=$false
 for($i=0;$i -lt 90 -and -not $ready -and -not $process.HasExited;$i++){
  $health=Send-SimFanApi -Method GET -Path '/health' -Base $base -Token $auth -TimeoutMs 650
  $ready=$health.status -eq 200;if(-not $ready){Start-Sleep -Milliseconds 250}
 }
 Check 'actual shipped Host authenticated public health' $ready @{port=$port;pid=$process.Id}
 Check 'real-manager preflight and Host arguments bind unchanged HC input' ((Get-FileHash -LiteralPath (Join-Path $RuntimeRoot 'HandheldCompanion.dll')).Hash.ToLowerInvariant() -ceq $hcHash) @{hcSha256=$hcHash;hostSha256=$hostHash;hostPid=$process.Id;assemblyProof='separate real manager constructor child + exact approved runtime closure; mock Host does not itself load HC';realBackend=$false}
 function Api([string]$Path,[string]$Body='{}',[string]$Method='POST'){
  $reply=Send-SimFanApi -Method $Method -Path $Path -Base $base -Token $auth -Body $Body -TimeoutMs 5000
  Check ('public '+$Method+' '+$Path) ($reply.status -eq 200 -and $reply.json.ok -ne $false) @{httpStatus=$reply.status;errorCode=$reply.errorCode}
  return $reply.json
 }
 function State([bool]$Enabled){
  $s=(Api '/api/state' '{}' 'GET').state
  Check ('physical writes disabled; mock enabled='+$Enabled) ($s.hostMode -eq 'mock-handshake' -and $s.mockZeroHardwareEvidence -eq $true -and $s.hardwareWritesEnabled -eq $false -and $s.hardwareWritesObserved -eq $false -and $s.mockControlEnabled -eq $Enabled) @{state=$s.state;hostMode=$s.hostMode;mockControlEnabled=$s.mockControlEnabled;hardwareWritesEnabled=$s.hardwareWritesEnabled;hardwareWritesObserved=$s.hardwareWritesObserved}
 }
 $null=Api '/api/handshake';State $false
 $null=Api '/api/open';$null=Api '/api/open-events'
 $lease=(Api '/api/acquire-control').lease
 Check 'actual public control lease acquired' (-not [string]::IsNullOrWhiteSpace($lease.leaseId))
 $body=@{leaseId=$lease.leaseId;nodes=@(@{tempC=0;dutyPercent=0},@{tempC=55;dutyPercent=30},@{tempC=75;dutyPercent=60},@{tempC=90;dutyPercent=100})}|ConvertTo-Json -Compress -Depth 5
 $leaseBody=@{leaseId=$lease.leaseId}|ConvertTo-Json -Compress
 $null=Api '/api/enable' $body;State $true
 $null=Api '/api/heartbeat' $leaseBody
 $null=Api '/api/disable';State $false
 $null=Api '/api/restore' $leaseBody
 $null=Api '/api/release-control' $leaseBody
 $null=Api '/api/close';State $false
 $null=Api '/api/shutdown'

 $hostStopped=$process.WaitForExit(15000)
 if($hostStopped){$hostExitCode=$process.ExitCode}
 Check 'actual shipped Host naturally exits zero after public shutdown' ($hostStopped -and $hostExitCode -eq 0) @{hostPid=$process.Id;exitCode=$hostExitCode;watchdog72Accepted=$false;forcedCleanupAccepted=$false}
 Check 'runtime bytes remain unchanged across actual constructor and Host sequence' ((Get-FileHash -LiteralPath (Join-Path $RuntimeRoot 'HandheldCompanion.dll')).Hash.ToLowerInvariant() -ceq $hcHash)
} catch {
 $failure=$_.Exception.Message
 if(-not @($checks|Where-Object status -eq 'FAIL').Count){$checks.Add([pscustomobject]@{name='unhandled or missing evidence';status='FAIL';detail=$failure})}
} finally {
 if($process){
  if(-not $process.HasExited){try{$null=Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $auth -TimeoutMs 1500}catch{};if(-not $process.WaitForExit(3000)){$forcedFixtureCleanup=$true;$process.Kill($true);$process.WaitForExit(5000)|Out-Null}}
  if($stdout){$stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutDir 'fixture.stdout.log') -Encoding UTF8}
  if($stderr){$stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutDir 'fixture.stderr.log') -Encoding UTF8}
  $process.Dispose()
 }
 if($authFile -and (Test-Path -LiteralPath $authFile)){Remove-Item -LiteralPath $authFile -Force}
 $auth=$null
}
$status=if($checks.Count -gt 0 -and @($checks|Where-Object status -ne 'PASS').Count -eq 0 -and $hostStopped -and -not $forcedFixtureCleanup){'PASS'}else{'FAIL'}
[ordered]@{status=$status;checks=@($checks);hardwareAcceptance=$false;realBackend=$false;managerStartsCalled=$false;realDeviceOpenCalled=$false;sameProcessAssemblyAttachment=$false;realManagerPreflightRequired=$true;forcedFixtureCleanup=$forcedFixtureCleanup;hostExitCode=$hostExitCode;naturalShutdownRequired=$true;failure=$failure;runtimeRoot=$RuntimeRoot;sharedRuntimeRoot=$SharedRuntimeRoot;hostPayloadRoot=$HostPayloadRoot;timestampUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $OutDir 'verification.json') -Encoding UTF8
Write-Output ('FAN_RUNTIME_CONNECTION '+$status+' checks='+$checks.Count)
if($status -eq 'PASS'){exit 0}else{exit 1}




