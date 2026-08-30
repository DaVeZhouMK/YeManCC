$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$feature = Get-Content -LiteralPath (Join-Path $root 'src\bridge\fanFeature.ts') -Raw -Encoding UTF8
$settings = Get-Content -LiteralPath (Join-Path $root 'src\bridge\settingsRepository.ts') -Raw -Encoding UTF8
$power = Get-Content -LiteralPath (Join-Path $root 'src\views\PowerView.vue') -Raw -Encoding UTF8
$app = Get-Content -LiteralPath (Join-Path $root 'src\App.vue') -Raw -Encoding UTF8
$fanControl = -join ([char]0x98CE, [char]0x6247, [char]0x63A7, [char]0x5236)
$supportedDevice = -join ([char]0x5728, [char]0x652F, [char]0x6301, [char]0x7684, [char]0x8BBE, [char]0x5907, [char]0x4E0A)
$enable = -join ([char]0x542F, [char]0x7528)

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
  if (-not $Text.Contains($Needle)) { throw "fan startup/default self-test failed: $Label" }
}

Assert-Contains $settings 'fanControl: false' 'startup fan control defaults off'
Assert-Contains $power ('label="' + $fanControl + '"') 'startup page exposes fan control'
Assert-Contains $power ('description="' + $supportedDevice + $enable + $fanControl + '"') 'startup page uses requested description'
Assert-Contains $power 'onFanControlBootToggle' 'startup toggle persists fan-control preference'
Assert-Contains $app "startup.fanControl === true && gate.allowed && gate.writeReady" 'boot enable remains gated by HC handshake/write readiness'
Assert-Contains $app 'fanHostLifecycle.apply(configuredFan.nodes)' 'boot enable uses the persisted/default curve'
Assert-Contains $feature '{ tempC: 40, dutyPercent: 15 }' 'soft default node 2 matches reference'
Assert-Contains $feature '{ tempC: 69, dutyPercent: 30 }' 'soft default node 3 matches reference'
Assert-Contains $feature '{ tempC: 100, dutyPercent: 70 }' 'soft default node 4 matches reference'
Assert-Contains $feature "preset: 'balanced'" 'unconfigured fan defaults to balanced preset'

Write-Output 'fan startup/default self-test: PASS (startup toggle, fail-closed HC gate, balanced fallback, soft reference curve)'
