# =============================================================================
# tool_gate.ps1 - the ymcc_sim TOOL_GATE (204 §6 A.4/A.5). Runs the REQUIRED negatives, the normal controls
# and a fixed-seed rerun, and prints TOOL_GATE=PASS/FAIL. Its own exit code follows the same rule as the tool:
# a failed or missing required case can never produce exit 0.
#
# Required negatives (204 §6 A.4): failed check with a normal return, missing required summary, timeout,
# exception, hardware gate unknown/open, historical log hit, old host answer, PID reuse / foreign same-name app.
#
# Usage: powershell -File tools\ymcc_sim\tests\tool_gate.ps1 [-Seed 204] [-OutDir <dir>]
# =============================================================================
[CmdletBinding()]
param(
  [int]$Seed = 204,
  [string]$OutDir = '',
  [switch]$SkipSandboxControl
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'tool-gate' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
# the frozen pre-fix revision is discovered, not hardcoded: newest prewrite-*\domains\sleep\sleep_domain.ps1
$frozen = ''
$prewriteRoot = 'G:\YeManCC-Work\_scratch\Repair202GateA-20260922-075435\ymcc-sim-204'
if (Test-Path -LiteralPath $prewriteRoot) {
  $cand = Get-ChildItem -LiteralPath $prewriteRoot -Directory -Filter 'prewrite-*' | Sort-Object Name -Descending
  foreach ($c in $cand) {
    $f1 = Join-Path $c.FullName 'domains\sleep\sleep_domain.ps1'
    $f2 = Join-Path $c.FullName 'tools-ymcc_sim\domains\sleep\sleep_domain.ps1'
    if (Test-Path -LiteralPath $f1) { $frozen = $f1; break }
    if (Test-Path -LiteralPath $f2) { $frozen = $f2; break }
  }
}
$script:ToolRunSeq = 0

$script:Cases = New-Object System.Collections.ArrayList
function Add-Case {
  param([string]$Id, [string]$Name, [bool]$Ok, [string]$Expected, [string]$Actual, [string]$Kind = 'negative')
  $script:Cases.Add([ordered]@{ id = $Id; name = $Name; kind = $Kind; pass = [bool]$Ok; expected = $Expected; actual = $Actual }) | Out-Null
  $tag = if ($Ok) { 'PASS' } else { 'FAIL' }
  $color = if ($Ok) { 'Green' } else { 'Red' }
  Write-Host ("  [{0}] {1} :: {2} (expected {3} / actual {4})" -f $tag, $Id, $Name, $Expected, $Actual) -ForegroundColor $color
}

function New-SyntheticRun {
  param([string]$Tag)
  $r = New-SimRun -Domain 'toolgate' -Mode $Tag -ModeKind 'offline-injection' -ToolPath $PSCommandPath -Seed $Seed
  return $r
}

function Invoke-Tool {
  <#
    Runs the sleep domain and returns its REAL exit code.
    MEASURED 2026-09-22 (registered D-204-2): with Start-Process -RedirectStandardOutput/-RedirectStandardError,
    PS 5.1 leaves $p.ExitCode unreadable (prints empty, [int]$null = 0) even after WaitForExit() and Refresh() -
    which silently turned a tool exit 10 into "0" in the first two gate runs. The synchronous call operator +
    $LASTEXITCODE is reliable, so the gate uses that for every case that must produce a code. (The timeout case
    does not need a code: a timeout is a failure by definition.)
  #>
  param([string]$ScriptPath, [string[]]$ToolArgs)
  $script:ToolRunSeq = $script:ToolRunSeq + 1
  $seq = $script:ToolRunSeq
  $outFile = Join-Path $OutDir ("tool-$stamp-$seq.out")
  $exe = 'powershell.exe'
  $all = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ScriptPath) + $ToolArgs
  $merged = ''
  try {
    $merged = (& $exe @all 2>&1 | Out-String)
  } catch {
    return [ordered]@{ exitCode = $null; timeout = $false; launchFailed = $true; output = ''; error = $_.Exception.Message; seq = $seq }
  }
  $code = $LASTEXITCODE
  [IO.File]::WriteAllText($outFile, $merged, [Text.UTF8Encoding]::new($false))
  return [ordered]@{ exitCode = $code; timeout = $false; launchFailed = $false; output = $merged; error = ''; seq = $seq }
}

function Start-Stub {
  param([int]$Port, [string]$State)
  $log = Join-Path $OutDir "stub-$State-$stamp.jsonl"
  if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
  $p = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $testsDir 'stub_host.ps1'), '-Port', "$Port", '-State', $State, '-LogPath', $log) -PassThru -WindowStyle Hidden
  for ($i = 0; $i -lt 40; $i++) {
    if (Test-SimPort $Port) { break }
    Start-Sleep -Milliseconds 250
  }
  return [ordered]@{ proc = $p; log = $log; port = $Port }
}
function Stop-Stub($stub) {
  try { if (-not $stub.proc.HasExited) { $stub.proc.Kill() } } catch { }
  Start-Sleep -Milliseconds 300
}
function Get-StubPostCount([string]$log) {
  if (-not (Test-Path -LiteralPath $log)) { return 0 }
  $n = 0
  foreach ($l in @(Get-Content -LiteralPath $log -Encoding UTF8)) {
    if ($l -match '"ev":"request"' -and $l -match '"method":"POST"') { $n++ }
  }
  return $n
}

Write-Host '=== ymcc_sim TOOL_GATE（204-A）===' -ForegroundColor White
Write-Host ("seed={0} outDir={1}" -f $Seed, $OutDir)

# ---------------------------------------------------------------- T1: pre-fix negative (SIM-1)
if (Test-Path -LiteralPath $frozen) {
  $pre = Invoke-Tool -ScriptPath $frozen -ToolArgs @('-Mode', 'selfcheck', '-HostExe', 'C:\__missing__\YeManFanHost.exe', '-EvidenceDir', $OutDir)
  Add-Case 'T1' 'PRE-FIX negative: frozen selfcheck with a failing check returns 0' ($pre.exitCode -eq 0) 'exit 0 (the defect)' ("exit={0}" -f $pre.exitCode) 'prefix-negative'
  $post = Invoke-Tool -ScriptPath (Join-Path $simRoot 'domains\sleep\sleep_domain.ps1') -ToolArgs @('-Mode', 'selfcheck', '-HostExe', 'C:\__missing__\YeManFanHost.exe', '-EvidenceDir', $OutDir)
  Add-Case 'T1b' 'FIXED: same input must not exit 0' ($post.exitCode -ne 0 -and $null -ne $post.exitCode) 'non-zero (CHECK_FAIL=2 expected)' ("exit={0}" -f $post.exitCode) 'control'
} else {
  Add-Case 'T1' 'PRE-FIX negative available (frozen copy present)' $false 'frozen copy path exists' 'missing' 'prefix-negative'
}

# ---------------------------------------------------------------- T2..T6: aggregator negatives (SIM-1)
$r2 = New-SyntheticRun 'failed-check-normal-return'
Add-SimCheck -Run $r2 -Form 'G' -Name 'deliberately failed required check' -Ok $false -Detail 'negative fixture' -Quiet
$v2 = Get-SimVerdict $r2
Add-Case 'T2' 'failed check + normal return -> not exit 0' ($v2.exitCode -ne 0) 'non-zero' ("verdict={0} exit={1}" -f $v2.verdict, $v2.exitCode)

$r3 = New-SyntheticRun 'missing-required-summary'
Add-SimRequiredButMissing -Run $r3 -Name 'required summary: identity' -Detail 'negative fixture'
$v3 = Get-SimVerdict $r3
Add-Case 'T3' 'missing required summary -> non-zero (EVIDENCE_INCOMPLETE_STALE)' ($v3.exitCode -eq 9) 'exit 9' ("verdict={0} exit={1}" -f $v3.verdict, $v3.exitCode)

$r4 = New-SyntheticRun 'exception'
Add-SimEvent -Run $r4 -Kind 'exception' -Detail 'negative fixture'
$v4 = Get-SimVerdict $r4
Add-Case 'T4' 'exception event -> non-zero' ($v4.exitCode -ne 0) 'non-zero' ("verdict={0} exit={1}" -f $v4.verdict, $v4.exitCode)

$r5 = New-SyntheticRun 'timeout'
Add-SimEvent -Run $r5 -Kind 'timeout' -Detail 'negative fixture'
$v5 = Get-SimVerdict $r5
Add-Case 'T5' 'timeout event -> exit 8 (TIMEOUT)' ($v5.exitCode -eq 8) 'exit 8' ("verdict={0} exit={1}" -f $v5.verdict, $v5.exitCode)

# real child that never reports an exit code (killed by us, our own child only)
$child = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-Command', 'Start-Sleep -Seconds 120') -PassThru -WindowStyle Hidden
$childExited = $child.WaitForExit(1200)
$childHadNoExit = -not $childExited
try { if (-not $childExited) { $child.Kill() } } catch { }
$r6 = New-SyntheticRun 'child-without-exit-code'
if ($childHadNoExit) { Add-SimEvent -Run $r6 -Kind 'child-no-exit' -Detail 'our own child was killed before reporting an exit code' }
$v6 = Get-SimVerdict $r6
Add-Case 'T6' 'child process without an exit code -> exit 8' ($childHadNoExit -and $v6.exitCode -eq 8) 'child had no exit code; exit 8' ("childNoExit={0} verdict={1} exit={2}" -f $childHadNoExit, $v6.verdict, $v6.exitCode)

# ---------------------------------------------------------------- T7/T8: hardware-gate negatives (SIM-2)
foreach ($case in @(@{ id = 'T7'; state = 'gate-open'; label = 'hardware gate OPEN (hardwareWritesEnabled=true)' },
                    @{ id = 'T8'; state = 'gate-unknown'; label = 'hardware gate UNKNOWN (field absent)' })) {
  $port = 8871
  $stub = Start-Stub -Port $port -State $case.state
  $tokenFile = Join-Path $OutDir "stub-token-$stamp.session"
  [IO.File]::WriteAllText($tokenFile, ([guid]::NewGuid().ToString('N')), [Text.UTF8Encoding]::new($false))
  $res = Invoke-Tool -ScriptPath (Join-Path $simRoot 'domains\sleep\sleep_domain.ps1') -ToolArgs @('-Mode', 'forms', '-Attach', '-AcknowledgeAttachRestart', '-Port', "$port", '-SessionTokenPath', $tokenFile, '-EvidenceDir', (Join-Path $OutDir 'gate-evidence'))
  $posts = Get-StubPostCount $stub.log
  Stop-Stub $stub
  $detail = "$($res | ConvertTo-Json -Compress)"
  $tail = @(($res.output -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -Last 3) -join ' / '
  Add-Case $case.id ("$($case.label): 0 change calls AND non-zero exit") ($posts -eq 0 -and $res.exitCode -ne 0 -and $null -ne $res.exitCode) '0 POSTs, non-zero exit (10 expected)' ("POSTs={0} exit={1} tail=[{2}] detail={3}" -f $posts, $res.exitCode, $tail, $detail)
}

# gate-safe control: the same path must be ALLOWED when the read proves a masked gate
$portSafe = 8872
$stubSafe = Start-Stub -Port $portSafe -State 'gate-safe'
$tokenFile2 = Join-Path $OutDir "stub-token-safe-$stamp.session"
[IO.File]::WriteAllText($tokenFile2, ([guid]::NewGuid().ToString('N')), [Text.UTF8Encoding]::new($false))
$gateSafe = Test-SimActionGate -Base "http://127.0.0.1:$portSafe" -Token 'x' -Kind 'external'
Stop-Stub $stubSafe
Add-Case 'T9' 'gate-safe control: masked state read allows actions' ([bool]$gateSafe.allowed) 'allowed=true' ("allowed={0} reason={1}" -f $gateSafe.allowed, $gateSafe.reason) 'control'

# ---------------------------------------------------------------- T10: historical log hit (SIM-4)
$logPath = Join-Path $OutDir "hist-$stamp.log"
[IO.File]::WriteAllText($logPath, "old line generation=111 suspend accepted`n", [Text.UTF8Encoding]::new($false))
$snap = Get-SimLogSnapshot -Paths @($logPath)
$hist = Get-SimLogMatchesSince -Path $logPath -FromOffset $snap.files[$logPath].length -Pattern 'suspend' -ExpectedMarkers @('111') -RequireMarker
[IO.File]::AppendAllText($logPath, "fresh line generation=222 suspend accepted`n", [Text.UTF8Encoding]::new($false))
$fresh = Get-SimLogMatchesSince -Path $logPath -FromOffset $snap.files[$logPath].length -Pattern 'suspend' -ExpectedMarkers @('222') -RequireMarker
Add-Case 'T10' 'historical-only hit is rejected; a fresh append passes' ((-not $hist.matchesThisRun) -and $hist.historicalHitsPresent -and $fresh.matchesThisRun) 'historical: no match; fresh: match' ("historical={0} histPresent={1} fresh={2}" -f $hist.matchesThisRun, $hist.historicalHitsPresent, $fresh.matchesThisRun)

# ---------------------------------------------------------------- T11: own-process identity (SIM-4)
$cand = [ordered]@{ pid = 100; creationTime = '2026-09-22T10:00:00'; name = 'YeManCC' }
$ownOk = Test-SimIdentityOwned -Candidate $cand -Expected @([ordered]@{ pid = 100; creationTime = '2026-09-22T10:00:00' })
$ownReused = Test-SimIdentityOwned -Candidate ([ordered]@{ pid = 100; creationTime = '2026-09-22T11:11:11'; name = 'YeManCC' }) -Expected @([ordered]@{ pid = 100; creationTime = '2026-09-22T10:00:00' })
$ownForeign = Test-SimIdentityOwned -Candidate ([ordered]@{ pid = 999; creationTime = '2026-09-22T10:00:00'; name = 'YeManCC' }) -Expected @([ordered]@{ pid = 100; creationTime = '2026-09-22T10:00:00' })
Add-Case 'T11' 'PID reuse / foreign same-name app are refused; exact identity is owned' ($ownOk -and (-not $ownReused) -and (-not $ownForeign)) 'own=true reused=false foreign=false' ("own={0} reused={1} foreign={2}" -f $ownOk, $ownReused, $ownForeign)

# ---------------------------------------------------------------- T12: old host answer (SIM-3)
$before = @([ordered]@{ instanceId = '1234:2026-09-22T10:00:00' })
$afterSame = @([ordered]@{ instanceId = '1234:2026-09-22T10:00:00' })
$afterNew = @([ordered]@{ instanceId = '1234:2026-09-22T10:05:00' })
$rvSame = Get-SimRestartVerdict -Before $before -After $afterSame
$rvNew = Get-SimRestartVerdict -Before $before -After $afterNew
Add-Case 'T12' 'restart verdict: reuse is REUSE (cannot pass as a new instance), fresh is FRESH' ($rvSame.verdict -eq 'REUSE' -and $rvNew.verdict -eq 'FRESH') 'REUSE / FRESH' ("same={0} new={1}" -f $rvSame.verdict, $rvNew.verdict)

# ---------------------------------------------------------------- T13/T14: normal controls + fixed-seed rerun
$sleepScript = Join-Path $simRoot 'domains\sleep\sleep_domain.ps1'
$ctrl1 = Invoke-Tool -ScriptPath $sleepScript -ToolArgs @('-Mode', 'selfcheck', '-EvidenceDir', (Join-Path $OutDir 'control-evidence'))
Add-Case 'T13' 'normal control: selfcheck exits 0 (ALL_PASS)' ($ctrl1.exitCode -eq 0) 'exit 0' ("exit={0}" -f $ctrl1.exitCode) 'control'

if (-not $SkipSandboxControl) {
  $ctrl2 = Invoke-Tool -ScriptPath $sleepScript -ToolArgs @('-Mode', 'forms', '-Forms', 'C2', '-Port', '8765', '-EvidenceDir', (Join-Path $OutDir 'control-evidence'))
  Add-Case 'T14' 'normal control: sandbox forms C2 exits 0' ($ctrl2.exitCode -eq 0) 'exit 0' ("exit={0}" -f $ctrl2.exitCode) 'control'
} else {
  Add-Case 'T14' 'normal control: sandbox forms C2 (skipped by switch)' $true 'exit 0' 'skipped' 'control'
}

$rerun = Invoke-Tool -ScriptPath $sleepScript -ToolArgs @('-Mode', 'selfcheck', '-EvidenceDir', (Join-Path $OutDir 'control-evidence'))
Add-Case 'T15' 'fixed-seed rerun (seed 204) reproduces the same verdict' ($rerun.exitCode -eq $ctrl1.exitCode) ("exit {0}" -f $ctrl1.exitCode) ("exit={0}" -f $rerun.exitCode) 'rerun'

# ---------------------------------------------------------------- verdict
$failed = @($script:Cases | Where-Object { -not $_.pass })
$verdict = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }
$report = [ordered]@{
  gate = 'ymcc_sim TOOL_GATE'
  batch = 204
  phase = 'A'
  seed = $Seed
  at = (Get-Date).ToString('o')
  toolIdentity = [ordered]@{
    core = (Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'sim_core')
    sleepDomain = (Get-SimFileIdentity $sleepScript 'sleep_domain')
    gateScript = (Get-SimFileIdentity $PSCommandPath 'tool_gate')
  }
  frozenRevision = (Get-SimFileIdentity $frozen 'frozen-sleep-domain')
  cases = @($script:Cases)
  failed = $failed.Count
  TOOL_GATE = $verdict
}
$jsonPath = Join-Path $OutDir ("tool-gate-$stamp.json")
[IO.File]::WriteAllText($jsonPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Host ''
Write-Host ("cases={0} failed={1}" -f @($script:Cases).Count, $failed.Count) -ForegroundColor $(if ($failed.Count) { 'Red' } else { 'Green' })
Write-Host ("TOOL_GATE={0}" -f $verdict) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host ("report={0}" -f $jsonPath)
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }