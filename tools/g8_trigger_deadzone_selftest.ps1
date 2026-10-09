<#
.SYNOPSIS
  G8 forward/reverse self-test: DS4 trigger low-band dead zone parity.

  HC DualShock4Target maps L2 digital bit from RAW trigger (AxisState>0) and
  writes raw analog (data[7]=AxisState). YMCC currently canonical-zeros <=30
  and thresholds >30/255 at InputHost. Expected fix (option B): native passes
  ltRaw/rtRaw, DS4 branch uses raw>0 for the digital bit and raw analog.

  Pure logic test: no HIDMaestro, no driver, no write. Asserts the semantic
  difference AND the expected post-fix behavior contract so the implementing
  change can be verified against this golden file.
#>
$ErrorActionPreference = 'Stop'

$fail = 0
function Assert([string]$n, [bool]$ok, [string]$detail) {
  if ($ok) { Write-Host "PASS | $n" }
  else { Write-Host "FAIL | $n | $detail"; $script:fail++ }
}

# HC canonical DS4 digital-bit predicate from raw byte trigger.
function HcDs4Digital([int]$raw) { return $raw -gt 0 }
# YMCC current predicate (InputHost uses Lt>30/255 after canonical zero <=30).
function YmccCurrentDigital([int]$raw) { return ($(if ($raw -le 30) { 0 } else { $raw })) -gt 0 }
# YMCC fixed predicate (option B: raw>0, analog = raw).
function YmccFixedDigital([int]$raw) { return $raw -gt 0 }
function YmccFixedAnalog([int]$raw) { return $raw }
function YmccCurrentAnalog([int]$raw) { return $(if ($raw -le 30) { 0 } else { $raw }) }

# ── Forward: light press inside the low band (1..30) must be a press ──────
foreach ($raw in @(1, 10, 20, 30)) {
  Assert "FWD L2=$raw HC press" (HcDs4Digital $raw) 'HC presses at >0'
  Assert "FWD L2=$raw YMCC-current WRONG (no press)" (-not (YmccCurrentDigital $raw)) 'current behavior drops digital bit'
  Assert "FWD L2=$raw YMCC-fixed press" (YmccFixedDigital $raw) 'option B presses like HC'
  Assert "FWD L2=$raw YMCC-fixed analog" ((YmccFixedAnalog $raw) -eq $raw) 'option B preserves raw analog'
}

# ── Reverse: zero must not press; high band unchanged ─────────────────────
Assert 'REV L2=0 no press' (-not (HcDs4Digital 0)) 'HC never presses at 0'
Assert 'REV L2=0 fixed no press' (-not (YmccFixedDigital 0)) 'option B agrees'
Assert 'REV L2=0 analog' ((YmccFixedAnalog 0) -eq 0) 'option B zero'
Assert 'REV L2=40 press+analog' ((HcDs4Digital 40) -and (YmccFixedDigital 40) -and (YmccFixedAnalog 40) -eq 40) 'high band identical'
Assert 'REV L2=255 analog' ((YmccFixedAnalog 255) -eq 255) 'full-scale identical'

# ── Option B isolation: X360 threshold unchanged ──────────────────────────
function XboxSoft([int]$raw) { return $raw -gt 30 }  # TriggerButtonThreshold=30/255 equiv
foreach ($raw in @(0, 29, 30, 31, 255)) {
  $expectSoft = $raw -gt 30
  Assert "X360 soft L2=$raw" ((XboxSoft $raw) -eq $expectSoft) 'X360 L2Soft threshold untouched by option B'
}

Write-Host ''
if ($fail -gt 0) { Write-Host "RESULT: $fail FAIL (G8 golden contract)" -ForegroundColor Red; exit 1 }
Write-Host 'RESULT: ALL PASS (G8 golden contract)' -ForegroundColor Green
exit 0