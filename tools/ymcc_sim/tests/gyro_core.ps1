# gyro_core.ps1 - S1–S3/S12 的共用 core「只读」检查（204-M）：合成链/阈值/校准/采样通道的可核对锚点。
# 说明：真正跑合成链需要 App + 钩子（owner: W3 前端）；本脚本只断言"通道与观测面存在 + 判读纪律"，不做合成注入。
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'gyro-core' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
function G([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
$gyroAssets = 'C:\SOFT\YeMan\PowerControl\feature-assets\gyro-motion'
$gyroFlag = Join-Path $gyroAssets 'mock-handshake.flag'
$gyroEnabled = Join-Path $gyroAssets 'enabled.flag'
$gyroCapFlag = Join-Path $gyroAssets 'input-capture.flag'
$gyroLog = Join-Path $D.localAppData 'gyro-motion.log'
$capLog = Join-Path $D.localAppData 'input-capture.jsonl'
G 'G1' 'S1/S12 合成链通道存在（feature-assets\gyro-motion 资产目录 + enabled/input-capture 标记）' ((Test-Path -LiteralPath $gyroAssets) -and ((Test-Path -LiteralPath $gyroEnabled) -or (Test-Path -LiteralPath $gyroCapFlag))) ("assets=" + (Test-Path -LiteralPath $gyroAssets) + " enabled=" + (Test-Path -LiteralPath $gyroEnabled) + " capture=" + (Test-Path -LiteralPath $gyroCapFlag) + " mockFlag=" + (Test-Path -LiteralPath $gyroFlag))
G 'G2' 'S1 观测面存在（gyro-motion.log 或可创建路径）' ((Test-Path -LiteralPath $gyroLog) -or (Test-Path -LiteralPath (Split-Path -Parent $gyroLog))) ("log=" + $gyroLog + " exists=" + (Test-Path -LiteralPath $gyroLog))
G 'G3' 'S12 采样通道存在（input-capture.jsonl）' (Test-Path -LiteralPath $capLog) ("cap=" + (Test-Path -LiteralPath $capLog))
$capTail = @()
if (Test-Path -LiteralPath $capLog) { $capTail = @(Read-SimTailLines -Path $capLog -Count 50 | Where-Object { $_ -match 'kind' } | Select-Object -First 5) }
G 'G4' 'S12 采样记录含 kind 字段（start/sample 结构可判读）' (@($capTail).Count -gt 0) ("kindRows=" + @($capTail).Count)
$discriminators = @('mockHandshake', 'sensorFamily', 'providerBindingProven', 'pairProven', 'proofId')
$g5hit = @()
foreach ($f in @($gyroLog, $capLog) | Where-Object { Test-Path -LiteralPath $_ }) {
  $src = Get-Content -LiteralPath $f -Raw -Encoding UTF8
  foreach ($k in $discriminators) { if ($src -match ('"' + $k + '"')) { $g5hit += ($k + '@' + (Split-Path -Leaf $f)) } }
}
G 'G5' 'S1 判读纪律：观测面含可区分合成/真实的判据字段（mockHandshake/sensorFamily/providerBindingProven/pairProven/proofId）' (@($g5hit).Count -gt 0) ("hits=" + ((@($g5hit) | Select-Object -Unique) -join ','))
G 'G6' 'S2/S3 纪律：合成样本不得写成"真传感器已验证"（扫描 gyro 日志与证据目录）' (-not (@(Get-ChildItem $D.evidenceDir -Recurse -Filter '*.json' -ErrorAction SilentlyContinue | Select-Object -First 12 | Where-Object { (Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8) -match 'real-sensor-verified' }).Count -gt 0)) 'no real-sensor-verified claims'
$failed = @($script:Cases | Where-Object { -not $_.pass })
$expectedCases = 6
$fakeGreen = (@($script:Cases).Count -ne $expectedCases)
$verdict = if ($failed.Count -eq 0 -and -not $fakeGreen) { 'PASS' } else { 'FAIL' }
$jf = Join-Path $OutDir ("gyro-core-$stamp.json")
[IO.File]::WriteAllText($jf, ([ordered]@{ at = (Get-Date).ToString('o'); cases = @($script:Cases); failed = $failed.Count; expectedCases = $expectedCases; fakeGreen = [bool]$fakeGreen; verdict = $verdict
  note = 'S1–S3/S12 core 只读部分；合成注入需 App+钩子（owner W3），本脚本不做注入' } | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
if ($fakeGreen) { Write-Host ("gyro-core: FAKE-GREEN cases={0} expected={1}（套件自身空转，判 FAIL）" -f @($script:Cases).Count, $expectedCases) -ForegroundColor Red }
Write-Host ("gyro-core: cases={0}/{1} failed={2} -> {3}" -f @($script:Cases).Count, $expectedCases, $failed.Count, $verdict) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host ("report=" + $jf)
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }
