[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[Parameter(Mandatory=$true)][string]$HostAssembly,[switch]$SchedulerPressure,[switch]$ExecutingTimeout,[switch]$ExecutingTimeoutOnly,[switch]$PublicPowerOnly,[ValidateSet('','queued-revocation','inflight-revocation','actual-timeout-borrow','same-sta-and-fresh-request')][string]$PublicPowerScenario='')
$ErrorActionPreference='Stop'
if($PublicPowerOnly -and ($SchedulerPressure -or $ExecutingTimeout -or $ExecutingTimeoutOnly)){throw 'Public power mode is exclusive'}
if($PublicPowerScenario -and -not $PublicPowerOnly){throw 'PublicPowerScenario requires PublicPowerOnly'}
if($ExecutingTimeout -and $ExecutingTimeoutOnly){throw 'Select either -ExecutingTimeout or -ExecutingTimeoutOnly'}
if(($ExecutingTimeout -or $ExecutingTimeoutOnly) -and -not $SchedulerPressure){throw '-ExecutingTimeout requires -SchedulerPressure; default51 already contains this original case'}
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$hostPath=(Resolve-Path -LiteralPath $HostAssembly).Path
$hostSha=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
$fixtureSource=Join-Path $PSScriptRoot 'FanStaAdmissionSelfTest\Program.cs'
$fixtureSha=(Get-FileHash -LiteralPath $fixtureSource -Algorithm SHA256).Hash.ToLowerInvariant()
$wrapperSha=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$selection=if($PublicPowerOnly){'public-power-only'}elseif($ExecutingTimeoutOnly){'scheduler-pressure+executing-timeout-only'}elseif($ExecutingTimeout){'scheduler-pressure+executing-timeout'}elseif($SchedulerPressure){'scheduler-pressure'}else{'default'}
$expectedPressureChecks=@(
 'scheduler pressure warmup uses real message-pumped STA'
 'scheduler pressure barrier owns pump without fabricated backend work'
 'scheduler pressure original request is pending in WPF queue'
 'scheduler pressure queued deadline returns unsubmitted busy'
 'scheduler pressure queued deadline keeps original15s budget'
 'scheduler pressure queued expiry preserves timeout/write/epoch state'
 'scheduler pressure expired queue ownership is retired'
 'scheduler pressure released barrier never executes expired action'
 'scheduler pressure fresh action succeeds after queue expiry'
 'scheduler pressure queued scenario releases all owned resources'
 'scheduler pressure sequential 100 executes each action once'
 'scheduler pressure sequential 100 preserves order and STA identity'
 'scheduler pressure sequential 100 preserves gates and retires owners'
 'scheduler pressure sequential 100 accepts following fresh action'
 'scheduler pressure sequential scenario releases all owned resources'
 'scheduler pressure loads no real hardware assembly'
)
$expectedPressureScenarios=@('queued-dispatcher-deadline','sequential-jitter-100')
$publicNames=[ordered]@{
 'queued-revocation'=@('HTTP owner returned while queued borrower retained import','revoked queued action never enters SDK port','queued return closes import without timeout')
 'inflight-revocation'=@('inflight call retains import after HTTP owner returns','cancel does not pretend entered SDK returned','post-call current check rejects revoked success','actual return releases exact import and STA owner')
 'actual-timeout-borrow'=@('inflight call retains import after HTTP owner returns','original timeout does not close actual inflight borrow','cancel does not pretend entered SDK returned','actual timeout remains an honest timeout','actual return releases exact import and STA owner')
 'same-sta-and-fresh-request'=@('same STA nested path checks revoked result','same STA return releases all scope borrowers')
}
$publicCommon=@('missing current scope never falls back after activation','resource return path remains callable after cancellation','new legitimate request reaches original STA','no cancellation or disposal synthesizes ACK','owned actual STA returns')
$publicScenarios=if($PublicPowerScenario){@($PublicPowerScenario)}else{@($publicNames.Keys)}
$publicChecks=@();foreach($kind in $publicScenarios){foreach($item in @($publicNames[$kind])+$publicCommon){$publicChecks+=($kind+': '+$item)}}
if($ExecutingTimeoutOnly){$expectedPressureChecks=@('scheduler pressure loads no real hardware assembly');$expectedPressureScenarios=@()}
if($ExecutingTimeout -or $ExecutingTimeoutOnly){
 $expectedPressureChecks+=@('real timeout fixture entered','genuine15s call still classified timeout','pending timed-out call cannot overlap cleanup','actual return clears exact pending ownership','next operation only after actual owner return')
 $expectedPressureScenarios+='executing-timeout-isolation'
}
$resultPath=Join-Path $root 'run\result.json'
if(Test-Path -LiteralPath $resultPath){throw "Refuse to overwrite original STA result: $resultPath"}
New-Item -ItemType Directory -Path $root -Force | Out-Null
$fixtureArgs=@($hostPath,(Join-Path $root 'run'))
if($SchedulerPressure){$fixtureArgs+='--scheduler-pressure'}
if($PublicPowerOnly){$fixtureArgs+='--public-power-only';if($PublicPowerScenario){$fixtureArgs+=$PublicPowerScenario}}
if($ExecutingTimeoutOnly){$fixtureArgs+='--executing-timeout-only'}elseif($ExecutingTimeout){$fixtureArgs+='--executing-timeout'}
Write-Output (ConvertTo-Json -Compress -InputObject ([ordered]@{selection=$selection;expectedChecks=$(if($PublicPowerOnly){$publicChecks.Count}elseif($SchedulerPressure){$expectedPressureChecks.Count}else{51});selectedScenarios=$(if($PublicPowerOnly){$publicScenarios}elseif($SchedulerPressure){$expectedPressureScenarios}else{@('default51')})}))
$elapsed=[Diagnostics.Stopwatch]::StartNew()
& dotnet run --project (Join-Path $PSScriptRoot 'FanStaAdmissionSelfTest\FanStaAdmissionSelfTest.csproj') -c Release --artifacts-path (Join-Path $root 'build') -- @fixtureArgs
$runExit=$LASTEXITCODE
if(-not (Test-Path -LiteralPath $resultPath)){throw "Fan STA admission produced no original result (exit=$runExit)"}
# Read the original FAIL too. Never rewrite it or rerun the default group.
$result=Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
Write-Output (ConvertTo-Json -Depth 10 -Compress -InputObject ([ordered]@{selection=$selection;exitCode=$runExit;actualChecks=$result.checks;actualPassed=$result.actualPassed;actualFailed=$result.actualFailed;producerElapsedMs=$result.elapsedMs;wrapperElapsedMs=$elapsed.Elapsed.TotalMilliseconds;hostAssemblySha256=$hostSha;fixtureSourceSha256=$fixtureSha;wrapperSourceSha256=$wrapperSha;scenarios=@($result.scenarios | Select-Object name,ok,elapsedMs,callMs,attempts,executedActions,expiredActions,freshActions);originalResult=$resultPath}))
if($result.selection -cne $selection -or $result.checks -ne @($result.results).Count){throw 'Fan STA selection/count mismatch'}
if($result.actualPassed -ne @($result.results | Where-Object {$_.ok -eq $true}).Count -or $result.actualFailed -ne @($result.results | Where-Object {$_.ok -ne $true}).Count){throw 'Fan STA actual pass/fail counts disagree with original rows'}
if($PublicPowerOnly){
 if($result.checks -ne $publicChecks.Count -or @($result.scenarios).Count -ne $publicScenarios.Count){throw 'Public power STA set incomplete'}
 foreach($name in $publicChecks){if(@($result.results | Where-Object {$_.name -ceq $name}).Count -ne 1){throw "Public power STA missing check: $name"}}
 foreach($row in $result.results){if($publicChecks -cnotcontains $row.name){throw "Unexpected public power STA check: $($row.name)"}}
 foreach($name in $publicScenarios){if(@($result.scenarios | Where-Object {$_.name -ceq $name}).Count -ne 1){throw "Public power STA missing scenario: $name"}}
}elseif($SchedulerPressure){
 if($result.checks -ne $expectedPressureChecks.Count){throw 'Fan STA pressure check set is incomplete'}
 foreach($name in $expectedPressureChecks){
  if(@($result.results | Where-Object {$_.name -ceq $name}).Count -ne 1){throw "Fan STA pressure check missing or duplicated: $name"}
 }
 foreach($row in $result.results){if($expectedPressureChecks -cnotcontains $row.name){throw "Unexpected Fan STA pressure check: $($row.name)"}}
 if(@($result.scenarios).Count -ne $expectedPressureScenarios.Count){throw 'Fan STA pressure scenario set is incomplete'}
 foreach($name in $expectedPressureScenarios){
  if(@($result.scenarios | Where-Object {$_.name -ceq $name}).Count -ne 1){throw "Fan STA pressure scenario missing or duplicated: $name"}
 }
 foreach($row in $result.scenarios){if($expectedPressureScenarios -cnotcontains $row.name){throw "Unexpected Fan STA pressure scenario: $($row.name)"}}
}elseif($result.checks -ne 51){throw 'Fan STA default51 gate is incomplete'}
if($result.hostAssemblySha256 -cne $hostSha -or $result.fixtureSourceSha256 -cne $fixtureSha -or $result.identityUnchanged -ne $true){throw 'Fan STA gate targets changed Host or fixture source'}
if((Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hostSha){throw 'Host changed during STA gate'}
if((Get-FileHash -LiteralPath $fixtureSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fixtureSha -or (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $wrapperSha){throw 'STA fixture or wrapper changed during gate'}
if($runExit -ne 0 -or $result.ok -ne $true -or @($result.results | Where-Object {$_.ok -ne $true}).Count -ne 0 -or @($result.scenarios | Where-Object {$_.ok -ne $true}).Count -ne 0){throw "Fan STA admission regression failed (selection=$selection exit=$runExit); original FAIL retained: $resultPath"}
if($PublicPowerOnly){Write-Output "fan STA public power: PASS ($($publicScenarios.Count) exact scenarios, $($publicChecks.Count) checks; default51 and pressure NOT_RUN; remote provider/physical OPEN)"}
elseif($SchedulerPressure){Write-Output "fan STA admission: PASS ($selection; $($expectedPressureScenarios.Count) exact scenarios, $($expectedPressureChecks.Count) checks; default51 NOT_RUN; no physical proof)"}
else{Write-Output 'fan STA admission: PASS (51 checks; real engine and STA, fixture session/owner actions, no physical proof)'}
