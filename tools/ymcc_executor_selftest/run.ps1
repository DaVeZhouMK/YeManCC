[CmdletBinding()]
param(
 [string]$ToolsDirectory='G:\YeManCC-Work\Deliverables',
 [string]$FixtureDirectory='G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\Build\Validation\CPU-Executor-20261004\fixture',
 [string]$OutDir='G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\Build\Validation\CPU-Executor-20261004\integration'
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutDir){throw 'NEW_EMPTY_OUTPUT_DIRECTORY_REQUIRED'}
New-Item -ItemType Directory -Path $OutDir | Out-Null
$exe=Join-Path $FixtureDirectory 'YeManCC.exe'
# Mandatory product/fixture separation: a fixture marker assembly must be next to the EXE.
if(-not (Test-Path -LiteralPath (Join-Path $FixtureDirectory 'YeManCC.dll'))){throw 'FIXTURE_ASSEMBLY_REQUIRED'}
if(-not (Test-Path -LiteralPath (Join-Path $FixtureDirectory 'YeManInputHost.exe'))){throw 'FIXTURE_CHILD_REQUIRED'}
$hash=(Get-FileHash -LiteralPath $exe).Hash
$wrapper=Join-Path $ToolsDirectory 'ROG-YMCC-AI-EXECUTOR-20261004.ps1'
$life=Join-Path $ToolsDirectory 'ROG-YMCC-LIFECYCLE-CONTROL-20261004.ps1'
$ps5='C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
$ps7=(Get-Command pwsh.exe -ErrorAction Stop).Source
$checks=New-Object System.Collections.Generic.List[object]
$operations=New-Object System.Collections.Generic.List[object]
$fixtureState=Join-Path $OutDir 'fixture-state.jsonl'
$session=[guid]::NewGuid().ToString()
$fixtureArgs=@('--state-file',$fixtureState,'--proof-text','空格 引号 "quoted" 尾巴\')
$encoded=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject @($fixtureArgs) -Compress)))
function Check([bool]$Pass,[string]$Name){$checks.Add([pscustomobject]@{name=$Name;pass=$Pass});if(-not $Pass){throw "ASSERTION_FAILED: $Name"}}
function Quote([string]$v){
 if($null -eq $v -or $v.Length -eq 0){return '""'};if($v -notmatch '[\s"]'){return $v}
 $s='"';$n=0;foreach($ch in $v.ToCharArray()){if($ch -eq '\'){$n++;continue};if($ch -eq '"'){$s+=('\'*($n*2+1));$s+='"';$n=0;continue};if($n){$s+=('\'*$n);$n=0};$s+=$ch};if($n){$s+=('\'*($n*2))};return $s+'"'
}
function Run([string]$HostExe,[string]$Script,[string]$Name,[string[]]$Extra){
 $opDir=Join-Path $OutDir $Name
 $arg=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$Script,'-ExePath',$exe,'-ExpectedSha256',$hash,'-OutDir',$opDir,'-SessionId',$session)+$Extra
 $sw=[Diagnostics.Stopwatch]::StartNew()
 $process=Start-Process -FilePath $HostExe -ArgumentList (($arg|ForEach-Object{Quote $_}) -join ' ') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $OutDir "$Name.stdout.log") -RedirectStandardError (Join-Path $OutDir "$Name.stderr.log") -PassThru
 $processHandle=$process.Handle
 if(-not $process.WaitForExit(150000)){throw "TEST_WORKER_TIMEOUT: $Name; exact worker PID=$($process.Id)"}
 $sw.Stop();if($null -eq $process.ExitCode){throw "TEST_EXIT_CODE_UNAVAILABLE: $Name"}
 $op=[pscustomobject]@{name=$Name;hostExe=$HostExe;exitCode=$process.ExitCode;elapsedSeconds=$sw.Elapsed.TotalSeconds;outDir=$opDir}
 $operations.Add($op);return $op
}
function StateRows { if(Test-Path -LiteralPath $fixtureState){return @(Get-Content -Encoding UTF8 -LiteralPath $fixtureState|ForEach-Object{$_|ConvertFrom-Json})};return @() }
# Medium-token CIM intentionally cannot read the elevated EXE path. Do not make
# that privilege limitation a fake lifecycle failure. Exact path and creation
# identity are proven by elevated lifecycle logs; here only follow those PIDs.
function LiveFixture([int]$ProcessId){$p=Get-Process -Id $ProcessId -ErrorAction SilentlyContinue;return ($null -ne $p -and $p.ProcessName -eq 'YeManCC' -and -not $p.HasExited)}
$failure=$null;$completed=$false
try {
 foreach($tool in @($wrapper,$life,(Join-Path $ToolsDirectory 'ROG-CPU-TELEMETRY-CAPTURE-20261004.ps1'))){
  $errors=$null;$tokens=$null;[void][Management.Automation.Language.Parser]::ParseFile($tool,[ref]$tokens,[ref]$errors)
  Check (@($errors).Count -eq 0) ('parse-'+[IO.Path]::GetFileName($tool))
  $bytes=[IO.File]::ReadAllBytes($tool);Check ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) ('utf8-bom-'+[IO.Path]::GetFileName($tool))
 }
 $op=Run $ps5 $wrapper '01-no-auto-elevate' @('-Action','start','-NoAutoElevate')
 Check ($op.exitCode -eq 6) 'filtered-token-denied-without-mutating-target'
 Check (-not (Test-Path -LiteralPath $fixtureState)) 'no-fixture-launched-on-denial'
 $op=Run $ps5 $life '02-direct-elevated-start' @('-Action','start','-StartArgumentBase64',$encoded,'-StartWaitSeconds','10')
 Check ($op.exitCode -eq 0) 'PS5-lifecycle-auto-elevated-start-returned'
 $deadline=[DateTime]::UtcNow.AddSeconds(10);do{$ready=@(StateRows|Where-Object{$_.action -eq 'ready'});if($ready.Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $deadline)
 Check ($ready.Count -eq 1) 'fixture-window-and-child-ready'
 $old=$ready[0]
 Check (LiveFixture ([int]$old.pid)) 'elevated-worker-returned-while-descendant-still-alive'
 Check ([int]$old.childPid -gt 0) 'fixture-owned-child-observed'
 Check ((ConvertTo-Json -InputObject @($old.arguments) -Compress) -ceq (ConvertTo-Json -InputObject @($fixtureArgs) -Compress)) 'PS5-runAs-base64-arguments-roundtrip-unicode-quotes-trailing-slash'
 $op=Run $ps7 $wrapper '03-executor-restart' @('-Action','restart','-StartArgumentBase64',$encoded,'-GraceSeconds','10','-StartWaitSeconds','10')
 Check ($op.exitCode -eq 0) 'PS7-executor-auto-elevated-restart-returned'
 $control=@(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $op.outDir 'executor-control.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
 Check (@($control|Where-Object{$_.action -eq 'privilege.verified' -and $_.token.verifiedHigh}).Count -eq 1) 'actual-elevated-worker-token-proven'
 Check (@($control|Where-Object{$_.action -eq 'privilege.auto-elevate.result' -and $_.applied -and $_.elevatedTokenProven}).Count -eq 1) 'parent-completion-linked-to-elevated-worker'
 $lifecycle=@(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $op.outDir 'lifecycle\lifecycle-control.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
 $stop=@($lifecycle|Where-Object{$_.action -eq 'ymcc.stop' -and $_.rootStopped}|Select-Object -Last 1)
 $start=@($lifecycle|Where-Object{$_.action -eq 'ymcc.start' -and $_.applied}|Select-Object -Last 1)
 Check ($stop.Count -eq 1 -and $stop[0].cleanupComplete) 'old-root-and-owned-children-confirmed-exited'
 Check ($start.Count -eq 1 -and $start[0].targetPrivilegeVerified) 'new-root-high-integrity-token-proven'
 Check (-not (LiveFixture ([int]$old.pid))) 'old-root-absent-after-restart'
 Check (-not (Get-Process -Id ([int]$old.childPid) -ErrorAction SilentlyContinue)) 'old-owned-child-absent-after-restart'
 Check (LiveFixture ([int]$start[0].targetPid)) 'new-fixture-root-still-live-after-wrapper-return'
 Check ([string]$start[0].targetCreationTimeUtc -ne [string]$stop[0].targetCreationTimeUtc) 'restart-creation-time-changed'
 $op=Run $ps5 $wrapper '04-executor-status' @('-Action','status')
 Check ($op.exitCode -eq 0) 'PS5-executor-elevated-identity-status'
 $op=Run $ps5 $wrapper '05-scenario-required' @('-Action','capture','-WarmupSeconds','0','-Seconds','3')
 Check ($op.exitCode -eq 7) 'no-handshake-cannot-masquerade-as-verified-scenario'
 $s=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $op.outDir 'capture\summary.json') -Raw|ConvertFrom-Json
 Check (-not $s.measurementStarted -and -not $s.measurementValid -and -not $s.scenarioVerified) 'failed-scenario-preflight-not-reported-zero-CPU'
 $op=Run $ps7 $wrapper '06-observation-only' @('-Action','capture','-AllowUnverifiedScenario','-WarmupSeconds','1','-Seconds','5','-SampleMilliseconds','500','-InventoryMilliseconds','1000')
 Check ($op.exitCode -in @(0,5)) 'observation-completed-with-explicit-quality-exit'
 $s=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $op.outDir 'capture\summary.json') -Raw|ConvertFrom-Json
 Check ($s.measurementStarted -and -not $s.scenarioVerified) 'fixture-observation-never-claims-feature-on-off-verification'
 Check (($s.measurementValid -and $op.exitCode -eq 0) -or (-not $s.measurementValid -and $op.exitCode -eq 5)) 'measurement-quality-and-dispatch-exit-agree'
 Check ($s.ymccEntry.available -and [string]::Equals([string]$s.ymccEntry.path,$exe,[StringComparison]::OrdinalIgnoreCase)) 'elevated-capture-reads-exact-root-executable-identity'
 $op=Run $ps5 $wrapper '07-executor-stop' @('-Action','stop','-GraceSeconds','10')
 Check ($op.exitCode -eq 0) 'PS5-executor-graceful-stop'
 Check (-not (LiveFixture ([int]$start[0].targetPid))) 'no-fixture-root-after-stop'
 $op=Run $ps7 $wrapper '08-capture-stopped' @('-Action','capture','-AllowUnverifiedScenario','-WarmupSeconds','0','-Seconds','1')
 Check ($op.exitCode -eq 5) 'stopped-target-capture-fails-before-measurement'
 Check (-not (Test-Path -LiteralPath (Join-Path $op.outDir 'capture\summary.json'))) 'stopped-target-not-filled-with-zero-samples'
 $completed=$true
} catch { $failure=$_.Exception.Message }
finally {
 # Cleanup uses only the verified diagnostic EXE's exact path/hash, never names.
 try{$op=Run $ps5 $wrapper '99-cleanup-fixture' @('-Action','stop','-GraceSeconds','10');if($op.exitCode -ne 0 -and -not $failure){$failure='fixture-cleanup-failed'}}catch{if(-not $failure){$failure=$_.Exception.Message}}
 $result=[ordered]@{scope='diagnostic-fixture-not-product; no ROG capability acceptance and no product CPU optimization result';capturedAtUtc=[DateTime]::UtcNow.ToString('o');status=$(if($completed -and -not $failure){'PASS'}else{'FAIL'});checks=@($checks.ToArray());operations=@($operations.ToArray());failure=$failure;fixtureExe=$exe;fixtureSha256=$hash}
 [IO.File]::WriteAllText((Join-Path $OutDir 'executor-integration-results.json'),($result|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 Write-Output "status=$($result.status) checks=$($checks.Count) result=$OutDir\executor-integration-results.json"
}
if(-not $completed -or $failure){Write-Error $failure;exit 1};exit 0