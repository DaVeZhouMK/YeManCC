# regressions.ps1 - 永久回归负例 R1–R10（204-M）：把项目真踩过的坑变成"永远能 FAIL"的用例。
# 只读/注入式：沙箱宿主（自启自清）+ 桩件；不触发真实睡眠；产品零改动。
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'regressions' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
# 注意：函数名不得用 R（PS 内置别名 r -> Invoke-History，别名优先于函数，会静默空转）
function Check-R([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
function Stub([int]$Port, [string]$State) {
  $log = Join-Path $OutDir "stub-$State-$stamp.jsonl"
  if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
  $p = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $testsDir 'stub_host.ps1'), '-Port', "$Port", '-State', $State, '-LogPath', $log) -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 1500
  return [ordered]@{ proc = $p; log = $log; port = $Port; alive = (-not $p.HasExited) }
}
function Kill-Stub($s) { try { if (-not $s.proc.HasExited) { $s.proc.Kill() } } catch { }; Start-Sleep -Milliseconds 200 }

# R1 常驻宿主复用（非本轮自启）⇒ 沙箱门必须拒绝
$s = Stub 8901 'gate-safe'
$g = Test-SimActionGate -Base 'http://127.0.0.1:8901' -Token 'x' -Kind 'sandbox' -SandboxFacts @{ launchedByRun = $false; realBackendPassed = $false }
Check-R 'R1' '非本轮自启的宿主 ⇒ 门拒绝（防复用旧实例）' (-not $g.allowed) ("allowed=$($g.allowed) reason=$($g.reason)")
Kill-Stub $s

# R2/R3 桩件 silent/drop 不响应 ⇒ 就绪判据只看"进程存活+端口可连"，不得要求响应
$s2 = Stub 8902 'silent'
$alive = -not $s2.proc.HasExited
Check-R 'R2' 'silent 桩件：进程存活即视为就绪（不得因无响应判未就绪）' ($alive -and (Test-SimPort 8902)) ("alive=$alive port=" + (Test-SimPort 8902))
$h = Send-SimFanApi -Method GET -Path '/health' -Base 'http://127.0.0.1:8902' -Token 'x' -TimeoutMs 1500
Check-R 'R3' 'silent 桩件：/health 无响应属预期（不得当成就绪）' ($h.status -eq -1) ("healthStatus=" + $h.status)
Kill-Stub $s2

# R4 noop 参数名必须映射为 noop-success（ValidateSet）
$stubSrc = Get-Content -LiteralPath (Join-Path $testsDir 'stub_host.ps1') -Raw -Encoding UTF8
$hasNoopSuccess = $stubSrc -match "'noop-success'"
$cycleSrc = Get-Content -LiteralPath (Join-Path $testsDir 'cycle.ps1') -Raw -Encoding UTF8
$mapsNoop = $cycleSrc -match "c.fault -eq 'noop'"
Check-R 'R4' 'noop→noop-success 映射存在（ValidateSet 与调用侧一致）' ($hasNoopSuccess -and $mapsNoop) ("stubHas=$hasNoopSuccess cycleMaps=$mapsNoop")

# R5 代次语义（真宿主；与 invariants I9 同源，这里独立复验）：
#   同一物理边沿内重复 suspend ⇒ 必须走去重路径（200 + deduplicated=true，不得产生第二次真实转换）；
#   恢复后复用旧代次 ⇒ 必须被拒（跨边沿 stale，非 200）。
$port = 8903
$sandDir = Join-Path $OutDir 'sandbox'; New-Item -ItemType Directory -Force -Path $sandDir | Out-Null
$tokFile = Join-Path $sandDir 'YeManFanHost.session'; New-SimFanSessionToken $tokFile | Out-Null
$hp = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$port", '--protocol-version', '2', '--session-token-file', $tokFile, '--mock-handshake') -PassThru -WindowStyle Hidden
$ready = Wait-SimHostReady -Port $port -TokenPath $tokFile -TimeoutSec 60
$tok = Get-SimFanSessionToken $tokFile
$base = "http://127.0.0.1:$port"
$r5ok = $false; $r5d = 'host-not-ready'
if ($ready.ready -and $tok) {
  $null = Send-SimFanApi -Method POST -Path '/api/handshake' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000
  $a = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":7,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $b = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":7,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $c = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":7,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $e = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":7,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $r5ok = ($a.status -eq 200) -and ($b.status -eq 200) -and ($b.deduplicated -eq $true) -and ($e.status -ne 200)
  $r5d = ("first={0} dup={1}/dedup={2} resume={3} staleReuse={4}/{5}" -f $a.status, $b.status, $b.deduplicated, $c.status, $e.status, $e.errorCode)
}
Check-R 'R5' '代次语义：边沿内重复 suspend=去重(200+deduplicated)；恢复后复用旧代次=拒绝' $r5ok $r5d

# R6/R7 证据必须带 64 位 SHA256（禁 mtime 判据 / 禁截断哈希）
$ev = Get-ChildItem $D.evidenceDir -Filter 'sleep-sim-*.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$hashOk = $false; $lenOk = $false; $r67 = 'no-evidence'
if ($ev) {
  $js = Get-Content -LiteralPath $ev.FullName -Raw -Encoding UTF8
  $hashOk = $js -match '"sha256"\s*:\s*"[0-9A-Fa-f]{64}"'
  $lenOk = -not ($js -match '"sha256"\s*:\s*"[0-9A-Fa-f]{2,63}"')
  $r67 = ("file=" + $ev.Name + " has64=" + $hashOk + " noShort=" + $lenOk)
}
Check-R 'R6' '证据含 64 位 SHA256（不得以 mtime 为判据）' $hashOk $r67
Check-R 'R7' '证据中无截断哈希（位宽门）' $lenOk $r67

# R8 工具树编码纪律：.ps1 带 BOM、.json 无 BOM
$bomBad = @(); $jsonBad = @()
foreach ($f in Get-ChildItem $simRoot -Recurse -File -Include '*.ps1', '*.json') {
  $bytes = [IO.File]::ReadAllBytes($f.FullName)
  $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
  if ($f.Extension -eq '.ps1' -and -not $hasBom) { $bomBad += $f.Name }
  if ($f.Extension -eq '.json' -and $hasBom) { $jsonBad += $f.Name }
}
Check-R 'R8' '编码纪律：.ps1 带 BOM / .json 无 BOM' ((@($bomBad).Count + @($jsonBad).Count) -eq 0) ("ps1MissingBom=" + @($bomBad).Count + " jsonWithBom=" + @($jsonBad).Count)

# R9 未证不得升格：证据里出现 unproven 时不得同文件写"完全释放/已恢复"
$bad = 0; $scanned = 0
foreach ($f in Get-ChildItem $D.evidenceDir -Recurse -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 12) {
  $js = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8
  $scanned++
  if ($js -match 'unproven' -and ($js -match 'fully-released|已完全释放|fully-restored')) { $bad++ }
}
Check-R 'R9' '未证不升格（unproven 与"完全释放/已恢复"不得同件出现）' ($bad -eq 0) ("scanned=$scanned violations=$bad")

# R10 假绿防护：缺摘要 ⇒ exit 9（复用 provoke）
$po = & powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testsDir 'provoke-missing.ps1') -SimRoot $simRoot -EvidenceDir $OutDir 2>&1 | Out-String
$pc = $LASTEXITCODE
Check-R 'R10' '假绿防护：必需摘要缺失 ⇒ exit 9' ($pc -eq 9) ("exit=" + $pc)

if ($ready.ready -and $tok) { $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000 }
Start-Sleep -Seconds 2
$failed = @($script:Cases | Where-Object { -not $_.pass })
$expectedCases = 10
$fakeGreen = (@($script:Cases).Count -ne $expectedCases)
$verdict = if ($failed.Count -eq 0 -and -not $fakeGreen) { 'PASS' } else { 'FAIL' }
$report = [ordered]@{ at = (Get-Date).ToString('o'); cases = @($script:Cases); failed = $failed.Count; expectedCases = $expectedCases; fakeGreen = [bool]$fakeGreen; verdict = $verdict
  note = 'R1–R10 永久回归负例（每条都能 FAIL）；realSleepPerformed=false；沙箱宿主自启自清' }
$jf = Join-Path $OutDir ("regressions-$stamp.json")
[IO.File]::WriteAllText($jf, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
if ($fakeGreen) { Write-Host ("regressions: FAKE-GREEN cases={0} expected={1}（套件自身空转，判 FAIL）" -f @($script:Cases).Count, $expectedCases) -ForegroundColor Red }
Write-Host ("regressions: cases={0}/{1} failed={2} -> {3}" -f @($script:Cases).Count, $expectedCases, $failed.Count, $verdict) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host ("report=" + $jf)
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }
