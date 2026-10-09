[CmdletBinding()]
param([string]$ProjectRoot='', [string]$ReleaseZip='', [string]$EvidencePath='', [switch]$SourceOnly)
$ErrorActionPreference='Stop'
if (!$ProjectRoot) {$ProjectRoot=Split-Path -Parent $PSScriptRoot}
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
. (Join-Path $PSScriptRoot 'fan-host-layout.ps1')
$count=Assert-FanHostToolingPaths $ProjectRoot
& (Join-Path $PSScriptRoot 'verify-r5v9-fan-host-payload.ps1') -PayloadRoot (Join-Path $ProjectRoot 'PowerControl\fan-host') | Out-Null
$record=[ordered]@{status='PASS';sourceChecks=$count;sourceDirectory='fan-host';runtimeDirectory='fan-host-v2';stateDirectoryUnchanged='fan-host';scope='Read-only source and archive path/identity gate; not device or public-channel readiness';observedAt=[DateTimeOffset]::Now.ToString('o')}
if (!$SourceOnly) {
  if (!$ReleaseZip) {$ReleaseZip=Join-Path (Join-Path $ProjectRoot '..\..') 'Release\Packages\YeManCC.zip'}
  $record.archive=Assert-FanHostArchiveLane $ProjectRoot $ReleaseZip
}
if ($EvidencePath) { $parent=Split-Path -Parent ([IO.Path]::GetFullPath($EvidencePath));New-Item -ItemType Directory -Path $parent -Force|Out-Null;$record|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $EvidencePath -Encoding UTF8 }
Write-Output ('FAN_HOST_PATH_GATE_OK checks='+$count+' runtime=fan-host-v2 archiveChecked='+(!$SourceOnly))