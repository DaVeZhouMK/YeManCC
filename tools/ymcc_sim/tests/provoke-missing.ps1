# provoke-missing.ps1 - 构造"必需摘要缺失"的运行，验证 exit 9（EVIDENCE_INCOMPLETE_STALE）
param([string]$SimRoot, [string]$EvidenceDir)
$ErrorActionPreference = 'Continue'
. (Join-Path $SimRoot 'lib\sim_core.ps1')
$r = New-SimRun -Domain 'provoke' -Mode 'missing-summary' -ModeKind 'offline-injection' -ToolPath $PSCommandPath -Seed 1
Add-SimRequiredButMissing -Run $r -Name 'required summary: verdict' -Detail 'provoke missing-summary'
$v = Get-SimVerdict $r
Write-Output ("verdict=" + $v.verdict + " exitCode=" + $v.exitCode + " missing=" + $v.counts.missingRequired)
if ($EvidenceDir) { $null = Complete-SimRun -Run $r -EvidenceDir $EvidenceDir -Prefix 'provoke' }
exit [int]$v.exitCode
