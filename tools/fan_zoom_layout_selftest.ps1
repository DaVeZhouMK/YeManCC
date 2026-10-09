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

# FAN-932 §3.2: the rows must stay continuous integers - the boot/wake mirror takes
# row 2, the curve nodes move to 3, and the node editors to 4/5. A weak "some row 2
# exists" check could be satisfied by the wrong control, so each stop is asserted.
Assert-Contains $fan ':gp-row="2" :gp-col="0"' 'the boot/wake fan mirror lost its explicit gamepad row 2'
Assert-Contains $fan ':data-gp-row="3"' 'fan curve nodes must use gamepad row 3'
Assert-Contains $fan ':gp-row="4"' 'fan node temperature editor must use gamepad row 4'
Assert-Contains $fan ':gp-row="5"' 'fan node duty editor must use gamepad row 5'
Write-Output 'fan zoom layout selftest: 8/8 passed'
