$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$recognition = Get-Content (Join-Path $root 'src\components\GameRecognitionControl.vue') -Raw
$steam = Get-Content (Join-Path $root 'src\views\SteamView.vue') -Raw

if ($recognition -match '@media\s*\(max-width:\s*620px\)[\s\S]*\.game-rule-actions[\s\S]*grid-template-columns:\s*repeat\(2') {
  throw 'game recognition controller rows still reflow to two columns'
}
if ($recognition -notmatch '\.game-rule-actions\s*\{\s*display:\s*grid;\s*grid-template-columns:\s*repeat\(3') {
  throw 'game recognition action row lost its fixed three-column layout'
}
if ($recognition -notmatch '\.game-rule-tabs\s*\{\s*grid-template-columns:\s*repeat\(4') {
  throw 'game recognition tabs lost their fixed four-column layout'
}
if ($steam -match '@media\s*\(max-width:\s*560px\)[\s\S]*\.custom-library-summary[\s\S]*grid-template-columns') {
  Write-Output 'steam summary is responsive informational content; no gamepad target attached'
}
Write-Output 'gamepad zoom layout selftest: 3/3 passed'
