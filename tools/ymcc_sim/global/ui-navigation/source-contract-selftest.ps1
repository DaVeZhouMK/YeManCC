[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'source-contract.ps1')
$caseRoot=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $caseRoot){throw 'FRESH_UI_SOURCE_GUARD_EVIDENCE_REQUIRED'}
$source=Join-Path $caseRoot 'source';$copy=Join-Path $caseRoot 'copy'
New-Item -ItemType Directory -Path $source,(Join-Path $source 'site'),$copy|Out-Null
$entry=Join-Path $source 'verify.mjs';$aux=Join-Path $source 'gyro-cases.mjs'
[IO.File]::WriteAllText($entry,'entry-v1');[IO.File]::WriteAllText($aux,'case-v1');[IO.File]::WriteAllText((Join-Path $source 'site/index.html'),'fixture')
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed){$checks.Add(@{name=$Name;passed=$Passed});if(-not $Passed){throw ('SOURCE_GUARD_CHECK_FAILED:'+ $Name)}}
$before=Get-SimUiTemplateSnapshot -Root $source
Check 'auxiliary and nested files retained' ($before.files.Count -eq 3)
Copy-SimUiTemplateSnapshot -Snapshot $before -Destination $copy
Check 'copied auxiliary bytes match captured version' ((Get-Content -LiteralPath (Join-Path $copy 'gyro-cases.mjs') -Raw) -eq 'case-v1')
Check 'unchanged source allowed' (Compare-SimUiTemplateSnapshot -Before $before -After (Get-SimUiTemplateSnapshot -Root $source)).unchanged
[IO.File]::WriteAllText($aux,'case-v2')
$changed=Compare-SimUiTemplateSnapshot -Before $before -After (Get-SimUiTemplateSnapshot -Root $source)
Check 'late auxiliary change invalidates source receipt' (-not $changed.unchanged -and @($changed.changes|Where-Object {$_.path -eq 'gyro-cases.mjs' -and $_.kind -eq 'changed'}).Count -eq 1)
$rejected=$false
try{Copy-SimUiTemplateSnapshot -Snapshot $before -Destination (Join-Path $caseRoot 'stale-copy')}catch{$rejected=$_.Exception.Message -eq 'UI_TEMPLATE_CHANGED_BEFORE_COPY'}
Check 'source changed before copy is rejected' $rejected
[IO.File]::WriteAllText($aux,'case-v1');$extra=Join-Path $source 'new-case.mjs';[IO.File]::WriteAllText($extra,'new fixture')
$added=Compare-SimUiTemplateSnapshot -Before $before -After (Get-SimUiTemplateSnapshot -Root $source)
Check 'added source set is visible' (@($added.changes|Where-Object kind -eq 'added').Count -eq 1)
Remove-Item -LiteralPath $extra
Remove-Item -LiteralPath $entry
$missing=Compare-SimUiTemplateSnapshot -Before $before -After (Get-SimUiTemplateSnapshot -Root $source)
Check 'missing source is not silently accepted' (@($missing.changes|Where-Object kind -eq 'missing').Count -eq 1)
$escape=[pscustomobject]@{files=@([pscustomobject]@{relative='../escape.txt';source=$aux;sha256=(Get-FileHash -LiteralPath $aux -Algorithm SHA256).Hash.ToLowerInvariant()})}
$rejected=$false
try{Copy-SimUiTemplateSnapshot -Snapshot $escape -Destination (Join-Path $caseRoot 'escape-copy')}catch{$rejected=$_.Exception.Message -eq 'UI_TEMPLATE_DESTINATION_ESCAPE'}
Check 'copy outside exact case directory rejected' $rejected
Check 'no escaped file written' (-not (Test-Path -LiteralPath (Join-Path $caseRoot 'escape.txt')))
[ordered]@{schemaVersion=1;status='PASS';checks=@($checks);scope='source snapshot/copy guard only';operatingSystemSleepPerformed=$false;browserStarted=$false;hardwareAccessed=$false}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $caseRoot 'result.json') -Encoding UTF8
Write-Output ('UI_SOURCE_GUARD_PASS '+$checks.Count)