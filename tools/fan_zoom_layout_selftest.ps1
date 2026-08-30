$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fan = Get-Content (Join-Path $root 'src\views\FanView.vue') -Raw
$app = Get-Content (Join-Path $root 'src\App.vue') -Raw

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
  if (-not $text.Contains($needle)) { throw $message }
}

Assert-Contains $app 'scrollbar-gutter: stable;' 'app-content does not reserve a stable scrollbar gutter'
Assert-Contains $fan 'grid-auto-rows:35px' 'fan control rows do not have a stable height'
Assert-Contains $fan 'aspect-ratio:650 / 370' 'fan chart does not have a stable aspect ratio'
Assert-Contains $fan 'grid-template-columns:repeat(4,minmax(0,1fr))' 'fan node grid is not fixed to four columns'
if ($fan -match '@media\(max-width:560px\)\{\.control-line\{grid-template-columns:1fr\}') {
  throw 'fan controls still reflow to one column at narrow/high-zoom sizes'
}

$nodeCount = ([regex]::Matches($fan, 'data-gp-row="2"')).Count
if ($nodeCount -lt 1) { throw 'fan curve nodes lost their explicit gamepad row' }
Write-Output 'fan zoom layout selftest: 5/5 passed'
