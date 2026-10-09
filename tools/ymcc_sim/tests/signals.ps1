# signals.ps1 - HC 信号读取与伪装筛选报告（204-K）。数据在 domains\fan\hc_signals.json；本脚本只读+核对+报告。
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'signals' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$sigPath = Join-Path $simRoot 'domains\fan\hc_signals.json'
if (-not (Test-Path -LiteralPath $sigPath)) { Write-Host "MISSING $sigPath" -ForegroundColor Red; exit 3 }
$data = Get-Content -LiteralPath $sigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$signals = @($data.signals)
$spoofable = @($signals | Where-Object { $_.spoofable -eq $true })
$realOnly = @($signals | Where-Object { $_.spoofable -ne $true })
$report = [ordered]@{
  at = (Get-Date).ToString('o')
  dataIdentity = Get-SimFileIdentity $sigPath 'hc_signals'
  tool = Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'sim_core'
  total = @($signals).Count; spoofable = @($spoofable).Count; realOnly = @($realOnly).Count
  byChain = @($signals | Group-Object chain | ForEach-Object { [ordered]@{ chain = $_.Name; count = $_.Count } })
  signals = $signals; policy = @($data.policy)
}
$jf = Join-Path $OutDir ("hc-signals-$stamp.json")
[IO.File]::WriteAllText($jf, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Host ("signals total={0} spoofable={1} realOnly={2}" -f $report.total, $report.spoofable, $report.realOnly) -ForegroundColor Green
foreach ($s in $signals) {
  $cls = if ($s.spoofable -eq $true) { 'SPOOFABLE' } else { 'REAL-ONLY' }
  Write-Host ("  [{0}] {1} ({2}) :: {3}" -f $cls, $s.id, $s.chain, $s.name) -ForegroundColor Gray
}
Write-Host ("report=" + $jf)
exit 0
