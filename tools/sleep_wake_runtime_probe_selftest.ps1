<#
.SYNOPSIS
  Self-test for sleep_wake_runtime_probe.ps1 decision logic using fabricated
  evidence files in a temp dir. Proves the probe FAILs when receipts are
  missing and PASSes when the expected receipts exist (forward/reverse).

  Never touches the real C:\SOFT\YeMan or %LOCALAPPDATA% paths.
#>
$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'sleep_wake_runtime_probe.ps1'
if (!(Test-Path -LiteralPath $tool)) { throw "Missing probe script: $tool" }

function Invoke-Probe([string]$probe, [hashtable]$files, [switch]$ExpectFail) {
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('sw-probe-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $tmp | Out-Null
  try {
    foreach ($name in $files.Keys) {
      $path = Join-Path $tmp $name
      [System.IO.File]::WriteAllText($path, $files[$name], [System.Text.Encoding]::UTF8)
    }
    $out = & $tool -Probe $probe -LocalAppData $tmp -SleepDir $tmp 6>&1 3>&1 2>&1
    $exit = $LASTEXITCODE
    $joined = ($out | Out-String)
    $failed = ($joined -match 'FAIL|RESULT:.*FAIL')
    if ($ExpectFail) {
      if (!$failed -and $exit -eq 0) { throw "Expected FAIL for probe '$probe' but got PASS" }
    } else {
      if ($failed -or $exit -ne 0) { throw "Expected PASS for probe '$probe' but got: $joined" }
    }
    return ([string]$joined)
  } finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
  }
}

$factsNoGrace = '{"event":"user-initiated-sleep","reason":1}' + "`n"
$factsGrace = '{"event":"user-initiated-sleep","reason":1}' + "`n" +
              '{"event":"long-sleep-resume-grace","generation":7,"delayMs":3000}' + "`n" +
              '{"event":"s0-wake-message","reason":7,"acceptedUserWake":0}' + "`n" +
              '{"event":"external-device-wake-evaluated","intentAgeMs":180000,"resleepEnabled":true}' + "`n"
$factsDelayed = '{"event":"long-sleep-resume-grace","generation":7,"delayMs":2500}' + "`n"
$factsShort = '{"event":"long-sleep-resume-grace","generation":7,"delayMs":3000}' + "`n" +
              '{"event":"s0-wake-message","reason":7,"acceptedUserWake":0,"entryFailure":true}' + "`n"

$captureChain = '{"kind":"sensor-suspend","t":100}' + "`n" +
                '{"kind":"sensor-resume","t":200}' + "`n" +
                '{"kind":"sample","t":210}' + "`n"
$lifecycle = 'power.resume-ready {"generation":7}' + "`n" + 'power.resumed {"generation":7}' + "`n"

# ── Reverse: short-sleep probe with a FORGED grace receipt must FAIL ──
$out = Invoke-Probe 'sleep-short' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsGrace; 'native-lifecycle.log' = $lifecycle } -ExpectFail
if ($out -notmatch 'G1 short-sleep no-grace') { throw 'sleep-short probe did not emit G1 reverse check' }
# And absent grace evidence on a real short sleep must PASS.
$out = Invoke-Probe 'sleep-short' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsNoGrace; 'native-lifecycle.log' = $lifecycle }
if ($out -notmatch 'G1 short-sleep no-grace') { throw 'sleep-short probe missing reverse check' }

# ── Forward: long-sleep probe with grace receipt -> PASS ──
$out = Invoke-Probe 'sleep-long' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsGrace; 'native-lifecycle.log' = $lifecycle }
if ($out -notmatch 'G1 long-sleep grace receipt') { throw 'sleep-long probe missing forward check' }

# ── Forward negative: grace with wrong delay -> must FAIL ──
Invoke-Probe 'sleep-long' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsDelayed; 'native-lifecycle.log' = $lifecycle } -ExpectFail | Out-Null

# ── controller-wake forward: reason=7 + evaluation -> PASS ──
$out = Invoke-Probe 'controller-wake' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsGrace; 'native-lifecycle.log' = $lifecycle }
if ($out -notmatch 'G3 reason=7 joystick.*reason recorded: 7') { throw 'controller-wake probe missing reason=7 PASS' }

# ── sensor-chain forward: suspend->resume->sample ordering -> PASS ──
$out = Invoke-Probe 'sensor-chain' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsNoGrace; 'native-lifecycle.log' = $lifecycle }
if ($out -notmatch 'samples after resume.*count=1') { throw 'sensor-chain probe missing samples-after-resume PASS' }

# ── entry-failure reason=7 must NOT be treated as a clean joystick wake: when
# the wake fact carries entryFailure=true, the joystick reason check FAILs ──
$out = Invoke-Probe 'controller-wake' @{ 'input-capture.jsonl' = $captureChain; 'sleep-facts.log' = $factsShort; 'native-lifecycle.log' = $lifecycle } -ExpectFail
if ($out -notmatch 'G3 reason=7 joystick') { throw 'entry-failure controller-wake probe missing reason check' }

Write-Host 'PASS sleep_wake_runtime_probe self-test'