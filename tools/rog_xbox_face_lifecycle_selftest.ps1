$ErrorActionPreference = 'Stop'
$native = Join-Path $PSScriptRoot '..\native\main.cpp'
$src = Get-Content -LiteralPath $native -Raw
$reasons = @()
if ($src -match 'rogSuppressForFrontend\("summon"\)') { $reasons += 'summon-suppress-still-armed' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("boot"\)') { $reasons += 'boot-enable-missing' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("exit"\)') { $reasons += 'exit-enable-missing' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("destroy"\)') { $reasons += 'destroy-enable-missing' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("window-hidden"\)') { $reasons += 'hide-enable-missing' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("suspend"\)') { $reasons += 'suspend-enable-missing' }
if ($src -notmatch 'rogEnsureXboxFaceEnabled\("resume"\)') { $reasons += 'resume-enable-missing' }
if ($src -notmatch 'rog-xbox-face-restore.flag') { $reasons += 'restore-flag-missing' }
if ($src -notmatch 'oemControl') { $reasons += 'control-input-split-missing' }
if ($src -match 'rogRestoreForGame\("hid-input-dead"\)') { $reasons += 'hid-dead-auto-restore-present' }
if ($src -match 'g_xinputPads\[i\]\.wButtons\s*\|=') { $reasons += 'slot-or-merge-present' }
if ($reasons.Count -gt 0) {
  Write-Error ("rog xbox face lifecycle audit failed: " + ($reasons -join ', '))
  exit 1
}
Write-Output 'rog xbox face lifecycle audit: PASS'
