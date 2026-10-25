param([string]$EvidenceRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $root))) 'Build\Validation\TaskView-SelfWorker-20261016\integration-contract'}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$checks=New-Object Collections.Generic.List[object]
function Check([string]$Name,[bool]$Condition){if(-not $Condition){throw $Name};$checks.Add(@{name=$Name;passed=$true})}
$main=[IO.File]::ReadAllText((Join-Path $root 'native\main.cpp'))
$entry=$main.IndexOf('int WINAPI wWinMain(')
$body=$main.Substring($entry)
$worker=$body.IndexOf('worker::tryRunCommandLine(')
Check 'internal worker dispatch exists before all ordinary startup' ($worker -gt 0 -and $worker -lt $body.IndexOf('--battery-runtime-identity') -and $worker -lt $body.IndexOf('g_hinst = hi;') -and $worker -lt $body.IndexOf('sgClearUserStandby(') -and $worker -lt $body.IndexOf('CreateMutexW('))
Check 'worker dispatch returns directly without main startup fallthrough' ($body -match 'if\s*\(ymcc::taskView::worker::tryRunCommandLine\(GetCommandLineW\(\),taskViewWorkerExit\)\)\s*return taskViewWorkerExit;')
Check 'producer uses own executable and internal mode' ($main.Contains('const auto helper=module;') -and $main.Contains('ymcc::taskView::worker::mode;'))
Check 'foreground grant is marshalled to the window owner' ($main.Contains('ops.foregroundOwner = g_hwnd;') -and $main.Contains('if (m == ymcc::taskView::foreground::message) return ymcc::taskView::foreground::deliver(h, w);'))
Check 'producer does not reference a separate helper file' (-not $main.Contains('YeManTaskViewHelper.exe'))
Check 'existing single-instance mutex remains in ordinary startup' ($body.Contains('CreateMutexW('))
$workerSource=[IO.File]::ReadAllText((Join-Path $root 'native\task_view_worker.h'))
Check 'worker has explicit event validation and initialized queue' ($workerSource.Contains('validEventPair') -and $workerSource.Contains('PeekMessageW') -and $workerSource.Contains('waitExecute'))
Check 'worker has no device or application initialization APIs' (-not ($workerSource -match '\b(?:SendInput|keybd_event|OpenProcess|CreateFileW|RegSetValueExW|GetAsyncKeyState|CreateWindowExW|CreateMutexW|CreateThread|std::thread|shell\.run|inputHostStart|g_hwnd)\b'))
$deploymentFiles=@('native\build_native.bat','tools\build-workspace.ps1','tools\package-release.ps1','tools\deploy-installed.ps1','tools\release-export-identity.ps1','tools\audit-source-export-coverage.ps1')
foreach($relative in $deploymentFiles){$text=[IO.File]::ReadAllText((Join-Path $root $relative));Check ('no standalone helper required: '+$relative) (-not ($text -match 'TaskViewHelper|build_task_view_helper|task_view_helper\.cpp'))}
Check 'obsolete separate helper source removed' (-not (Test-Path -LiteralPath (Join-Path $root 'native\task_view_helper.cpp')))
Check 'obsolete separate helper build recipe removed' (-not (Test-Path -LiteralPath (Join-Path $root 'native\build_task_view_helper.bat')))
foreach($relative in @('tools\build-workspace.ps1','tools\package-release.ps1','tools\deploy-installed.ps1','tools\release-export-identity.ps1','tools\audit-source-export-coverage.ps1','tools\release_export_identity_selftest.ps1')){
 $tokens=$null;$errors=$null
 [Management.Automation.Language.Parser]::ParseFile((Join-Path $root $relative),[ref]$tokens,[ref]$errors)|Out-Null
 Check ('PowerShell syntax: '+$relative) ($errors.Count -eq 0)
}
$report=@{passed=$true;checks=$checks.Count;cases=@($checks.ToArray());runsProduct=$false;touchesInstalledFiles=$false}
$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'results.json') -Encoding UTF8
Write-Output ($checks.Count.ToString()+' same-executable integration/package contract checks passed.')