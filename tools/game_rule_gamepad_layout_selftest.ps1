$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$quick = Get-Content (Join-Path $root 'src\components\GameQuickActions.vue') -Raw
$rules = Get-Content (Join-Path $root 'src\components\GameRulePanel.vue') -Raw
$engine = Get-Content (Join-Path $root 'src\gamepad\engine.ts') -Raw

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
  if (-not $text.Contains($needle)) { throw $message }
}

foreach ($col in 0..3) {
  $needle = 'data-gp-row="0" data-gp-col="' + $col + '"'
  Assert-Contains $quick $needle "top control $col is missing a fixed focus coordinate"
}
Assert-Contains $quick 'data-gp-row="1" data-gp-col="0"' 'FSR row coordinate is missing'
Assert-Contains $quick 'data-gp-row="1" data-gp-col="1"' 'Lossless Scaling row coordinate is missing'
Assert-Contains $quick 'data-gp-row="2" data-gp-col="0"' 'trainer row coordinate is missing'
Assert-Contains $quick 'gp-row="2" gp-col="1"' 'speed dropdown coordinate is missing'
Assert-Contains $quick 'data-gp-game-control="switch-program"' 'switch program semantic target is missing'
Assert-Contains $quick 'data-gp-game-control="fsr-import"' 'FSR semantic source is missing'
if ($rules -notmatch 'data-gp-rule-focus="manual-input"[\s\S]*?data-gp-row="4"[\s\S]*?data-gp-col="0"') { throw 'manual input is not aligned with confirmation' }
Assert-Contains $rules 'data-gp-row="4" data-gp-col="1"' 'manual confirmation is not aligned with input'
Assert-Contains $engine "marker === 'manual-input' || marker === 'manual-confirm'" 'manual row down-stop rule is missing'
Write-Output 'game rule gamepad layout selftest: 10/10 passed'
