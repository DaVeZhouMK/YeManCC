$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$feature = Get-Content -LiteralPath (Join-Path $root 'src\bridge\fanFeature.ts') -Raw -Encoding UTF8
$settings = Get-Content -LiteralPath (Join-Path $root 'src\bridge\settingsRepository.ts') -Raw -Encoding UTF8
$power = Get-Content -LiteralPath (Join-Path $root 'src\views\PowerView.vue') -Raw -Encoding UTF8
$app = Get-Content -LiteralPath (Join-Path $root 'src\App.vue') -Raw -Encoding UTF8
$fanView = Get-Content -LiteralPath (Join-Path $root 'src\views\FanView.vue') -Raw -Encoding UTF8

# FAN-932: the boot/wake fan preference is now a real, operable, persisted switch
# that also actually auto-starts control at boot and wake. ASCII-only file: every
# CJK literal is assembled from code points so no BOM/encoding dependency exists.
$bootLabel = -join ([char]0x5F00, [char]0x673A, [char]0x002F, [char]0x4F11, [char]0x7720,
  [char]0x5524, [char]0x9192, [char]0x542F, [char]0x52A8, [char]0x98CE, [char]0x6247)
$bootHint = -join ([char]0x6839, [char]0x636E, [char]0x9002, [char]0x914D, [char]0x673A,
  [char]0x578B, [char]0x81EA, [char]0x52A8, [char]0x542F, [char]0x52A8, [char]0x98CE, [char]0x6247)
# Superseded informational hint; must not come back.
$oldHint = -join ([char]0x98CE, [char]0x6247, [char]0x63A7, [char]0x5236, [char]0x8BF7, [char]0x5728,
  [char]0x98CE, [char]0x6247, [char]0x9875, [char]0x624B, [char]0x52A8, [char]0x5F00, [char]0x542F)

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
  if (-not $Text.Contains($Needle)) { throw "fan startup/default self-test failed: $Label" }
}
function Assert-Absent([string]$Text, [string]$Needle, [string]$Label) {
  if ($Text.Contains($Needle)) { throw "fan startup/default self-test failed: $Label" }
}

Assert-Contains $settings 'fanControl: false' 'startup fan control still defaults off'

# PowerView: fixed copy, real bound switch, never hard-disabled.
Assert-Contains $power ('label="' + $bootLabel + '"') 'startup page renders the boot/wake fan label'
Assert-Contains $power ('description="' + $bootHint + '"') 'startup page shows the auto-start-by-device hint'
Assert-Absent $power $oldHint 'the superseded manual-enable hint must be gone'
$rowStart = $power.IndexOf('label="' + $bootLabel + '"')
if ($rowStart -lt 0) { throw 'fan startup/default self-test failed: boot/wake fan row not found' }
$rowStart = [Math]::Max(0, $power.LastIndexOf('<Toggle', $rowStart))
$rowEnd = $power.IndexOf('/>', $rowStart)
if ($rowEnd -le $rowStart) { throw 'fan startup/default self-test failed: boot/wake fan row boundary missing' }
$rowBody = $power.Substring($rowStart, $rowEnd - $rowStart)
Assert-Contains $rowBody 'v-model="fanControlOnBoot"' 'the boot/wake fan row must bind an operable switch'
Assert-Absent $rowBody ':disabled="true"' 'the boot/wake fan row must not be hard-disabled'

# FanView mirror: same preference, writes the setting only; never starts control.
Assert-Contains $fanView ('label="' + $bootLabel + '"') 'the fan page must mirror the boot/wake fan switch'
Assert-Contains $fanView ('description="' + $bootHint + '"') 'the fan page mirror must use the same hint'
Assert-Contains $fanView "saveSettingsSection('startupDesired', { fanControl: v })" 'the fan page mirror must persist the shared preference'
Assert-Contains $fanView "readSettingsSection<{ fanControl?: boolean }>('startupDesired')" 'the fan page mirror must read the shared preference'
$mirrorStart = $fanView.IndexOf('async function onFanBootAutoStartToggle(')
$mirrorEnd = $fanView.IndexOf('const enableStageText = computed(', $mirrorStart)
if ($mirrorStart -lt 0 -or $mirrorEnd -le $mirrorStart) { throw 'fan startup/default self-test failed: fan page mirror boundary missing' }
$mirrorBody = $fanView.Substring($mirrorStart, $mirrorEnd - $mirrorStart)
Assert-Absent $mirrorBody 'fanHostLifecycle.start' 'the preference mirror must never start the Fan Host'
Assert-Absent $mirrorBody 'fanHostLifecycle.apply' 'the preference mirror must never apply a curve'

# App: one shared, generation-deduped, serialized auto-start orchestration entry.
$autoStart = $app.IndexOf('function ensureFanAutoStart(')
if ($autoStart -lt 0) { throw 'fan startup/default self-test failed: auto-start orchestration entry missing' }
$autoStartEnd = $app.IndexOf('const router = useRouter();', $autoStart)
if ($autoStartEnd -le $autoStart) { throw 'fan startup/default self-test failed: auto-start orchestration boundary missing' }
$autoStartBody = $app.Substring($autoStart, $autoStartEnd - $autoStart)
Assert-Contains $autoStartBody 'readSettingsSection' 'the auto-start entry must read the persisted preference'
Assert-Contains $autoStartBody 'startup.fanControl !== true' 'the auto-start entry must require the preference to be strictly on'
Assert-Contains $autoStartBody 'fanHostLifecycle.hasControlIntent' 'the auto-start entry must defer to an existing control intent'
Assert-Contains $autoStartBody 'fanHostLifecycle.start()' 'the auto-start entry must be the preference start path'
Assert-Contains $autoStartBody 'gate.allowed' 'the auto-start entry must stay behind the device admission gate'
Assert-Contains $autoStartBody 'gate.writeReady' 'the auto-start entry must refuse unverified write routes'
Assert-Contains $autoStartBody 'getFanPresetCurve(' 'the auto-start entry must use the saved preset curve'
Assert-Contains $app "ensureFanAutoStart('boot')" 'boot must drive the shared auto-start entry'
Assert-Contains $app "ensureFanAutoStart('resume', resumeGeneration)" 'wake must drive the shared auto-start entry'
$startAt = $autoStartBody.IndexOf('fanHostLifecycle.start()')
$gateAt = $autoStartBody.IndexOf('gate.writeReady')
$applyAt = $autoStartBody.IndexOf('fanHostLifecycle.apply(')
if (-not ($startAt -ge 0 -and $gateAt -gt $startAt -and $applyAt -gt $gateAt)) {
  throw 'fan startup/default self-test failed: auto-start must gate allowed+writeReady before apply'
}

# The shell admission block itself stays inert (auto-start is a later step).
$bootStart = $app.IndexOf('app-startup-ready')
$bootEnd = $app.IndexOf('void syncMouseModeAtStartup();')
if ($bootStart -lt 0 -or $bootEnd -le $bootStart) { throw 'fan startup/default self-test failed: boot region boundary missing' }
$boot = $app.Substring($bootStart, $bootEnd - $bootStart)
Assert-Absent $boot 'fanHostLifecycle.start(' 'shell admission must not start the Fan Host'
Assert-Absent $boot 'fanHostLifecycle.apply(' 'shell admission must not apply a fan curve'
Assert-Absent $boot 'recordFanHandshake(' 'shell admission must not handshake'

# Page entry is still not a start path and still ignores the preference.
Assert-Contains $fanView 'resolveFanEntryAction(fanHostLifecycle.phase)' 'page entry uses the shared entry policy'
$entryStart = $fanView.IndexOf('async function adoptOnDemandEntryState()')
$entryEnd = $fanView.IndexOf('onUnmounted(() => {', $entryStart)
if ($entryStart -lt 0 -or $entryEnd -le $entryStart) { throw 'fan startup/default self-test failed: entry boundary missing' }
$entryBody = $fanView.Substring($entryStart, $entryEnd - $entryStart)
Assert-Absent $entryBody 'startupDesired' 'page entry must not read the persisted boot preference'
Assert-Absent $entryBody 'fanHostLifecycle.start' 'page entry must not start the Fan Host'

Assert-Contains $feature '{ tempC: 40, dutyPercent: 15 }' 'soft default node 2 matches reference'
Assert-Contains $feature '{ tempC: 69, dutyPercent: 30 }' 'soft default node 3 matches reference'
Assert-Contains $feature '{ tempC: 100, dutyPercent: 70 }' 'soft default node 4 matches reference'
Assert-Contains $feature "preset: 'balanced'" 'unconfigured fan defaults to balanced preset'

Write-Output 'fan startup/default self-test: PASS (boot/wake fan preference is an operable persisted switch with one shared auto-start entry gated by allowed+writeReady; shell admission and page entry stay non-start paths; balanced fallback; soft reference curve)'
