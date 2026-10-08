# Lighting capability coverage audit (2026-09-16)
#
# Verifies YMCC's three deviceClass-level lighting tables against
# HandheldCompanion's per-class capability declarations:
#   - ymccDeviceClassLightingSet      vs DeviceCapabilities.DynamicLighting
#   - ymccDeviceClassSecondColorSet   vs DeviceCapabilities.DynamicLightingSecondLEDColor
#   - ymccDeviceClassLedEffects       vs DynamicLightingCapabilities |= LEDLevel.*
#
# HC assignment semantics honoured: "=" resets the flag set (OneXPlayerX1.cs:82),
# "+=" adds, "-=" removes (AYANEOFlipDS.cs:21-22); values inherit along the class chain.
# IDevice defaults: Capabilities = None, DynamicLightingCapabilities = None (IDevice.cs:160-161).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\lighting_capability_coverage_audit.ps1
# Exit code 0 = no gaps.

param(
    [string]$HcDevicesRoot = 'g:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902\HandheldCompanion\Devices',
    [string]$HcDeviceBase  = 'g:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902\HandheldCompanion\Devices\IDevice.cs',
    [string]$MainCpp       = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp'
)
$ErrorActionPreference = 'Stop'
foreach ($p in @($HcDevicesRoot, $HcDeviceBase, $MainCpp)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" } }

$ledBitNames = @{ 'SolidColor' = 'solid'; 'Breathing' = 'breathing'; 'Rainbow' = 'rainbow'; 'Wave' = 'wave'
                  'Wheel' = 'wheel'; 'Gradient' = 'gradient'; 'Ambilight' = 'ambilight'; 'LEDPreset' = 'preset' }

# per class: list of ops in source order -> ['flag', '+|-|=', capability] / ['led', '+|=', name]
$bases = @{}
$ops = @{}
foreach ($f in (Get-ChildItem -Path $HcDevicesRoot -Recurse -Filter *.cs)) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $cm = [regex]::Matches($text, 'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_\.]+)')
    for ($i = 0; $i -lt $cm.Count; $i++) {
        $cls = $cm[$i].Groups[1].Value
        $bases[$cls] = $cm[$i].Groups[2].Value.Split('.')[-1]
        $start = $cm[$i].Index
        $end = if ($i + 1 -lt $cm.Count) { $cm[$i + 1].Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        $list = New-Object System.Collections.Generic.List[object]
        foreach ($m in [regex]::Matches($body, 'Capabilities\s*(\||\+|-)?=\s*DeviceCapabilities\.(DynamicLighting[A-Za-z]*)')) {
            $sign = $m.Groups[1].Value
            $op = if ($sign -eq '-') { '-=' } elseif ($sign -eq '' ) { '=' } else { '+=' }
            $list.Add(@{ op = $op; flag = $m.Groups[2].Value })
        }
        foreach ($m in [regex]::Matches($body, 'DynamicLightingCapabilities\s*(\||\+|-)?=\s*([^;]+);')) {
            $sign = $m.Groups[1].Value
            $op = if ($sign -eq '-') { '-=' } elseif ($sign -eq '') { '=' } else { '+=' }
            $expr = $m.Groups[2].Value
            foreach ($lm in [regex]::Matches($expr, 'LEDLevel\.([A-Za-z]+)')) {
                $list.Add(@{ op = $op; led = $lm.Groups[1].Value })
            }
        }
        if ($list.Count -gt 0) { $ops[$cls] = $list }
    }
}
function Get-Effective([string]$cls, [string]$kind) {
    # walk chain from root to cls, applying ops in order; returns arrays of names
    $chain = New-Object System.Collections.Generic.List[string]
    $c = $cls; $seen = 0
    while ($c -and $seen -lt 12) { $chain.Insert(0, $c); if (-not $bases.ContainsKey($c)) { break }; $c = $bases[$c]; $seen++ }
    $flags = New-Object System.Collections.Generic.List[string]
    $leds = New-Object System.Collections.Generic.List[string]
    foreach ($node in $chain) {
        if (-not $ops.ContainsKey($node)) { continue }
        foreach ($entry in $ops[$node]) {
            if ($entry.ContainsKey('flag') -and $kind -eq 'flag') {
                $name = $entry['flag']
                $op = $entry['op']
                if ($op -eq '-=') { $flags.Remove($name) | Out-Null }
                else {
                    if ($op -eq '=') { $flags.Clear() }
                    if (-not $flags.Contains($name)) { $flags.Add($name) }
                }
            }
            elseif ($entry.ContainsKey('led') -and $kind -eq 'led') {
                $name = $entry['led']
                $op = $entry['op']
                if ($op -eq '-=') { $leds.Remove($name) | Out-Null }
                else {
                    if ($op -eq '=') { $leds.Clear() }
                    if (-not $leds.Contains($name)) { $leds.Add($name) }
                }
            }
        }
    }
    if ($kind -eq 'flag') { return @($flags) } else { return @($leds) }
}
function Get-Effects([string]$cls) {
    $leds = Get-Effective $cls 'led'
    $mapped = @()
    foreach ($l in $leds) { if ($l -eq 'None') { continue }; if ($ledBitNames.ContainsKey($l)) { $mapped += $ledBitNames[$l] } }
    return @($mapped | Sort-Object -Unique)
}

# YMCC tables
$cpp = Get-Content -LiteralPath $MainCpp -Raw
function Get-YmccSet([string]$marker) {
    $idx = $cpp.IndexOf($marker)
    if ($idx -lt 0) { throw "marker not found: $marker" }
    $end = $cpp.IndexOf('};', $idx)
    $block = $cpp.Substring($idx, $end - $idx)
    $set = New-Object System.Collections.Generic.HashSet[string]
    foreach ($m in [regex]::Matches($block, '"HandheldCompanion\.Devices\.([A-Za-z0-9_]+)"')) { $set.Add($m.Groups[1].Value) | Out-Null }
    return $set
}
$ymccLight = Get-YmccSet 'ymccDeviceClassLightingSet()'
$ymccSecond = Get-YmccSet 'ymccDeviceClassSecondColorSet()'
# effects: parse ymccDeviceClassLedEffects conditionals
$effIdx = $cpp.IndexOf('ymccDeviceClassLedEffects(const std::string& deviceClass)')
$effEnd = $cpp.IndexOf('static bool ymccFamilySupportsRumble', $effIdx)
$effBlock = $cpp.Substring($effIdx, $effEnd - $effIdx)
$ymccEffects = @{}
foreach ($m in [regex]::Matches($effBlock, 'if\s*\(([\s\S]*?)\)\s*\r?\n\s*return\s*\{([^}]*)\};')) {
    $cond = $m.Groups[1].Value
    $list = @()
    foreach ($e in [regex]::Matches($m.Groups[2].Value, '"([a-z]+)"')) { $list += $e.Groups[1].Value }
    $list = @($list | Sort-Object -Unique)
    foreach ($c in [regex]::Matches($cond, '"HandheldCompanion\.Devices\.([A-Za-z0-9_]+)"')) {
        $ymccEffects[$c.Groups[1].Value] = $list
    }
}

# instantiables (classes HC's GetCurrent can build) - only those matter in practice
$idText = Get-Content -LiteralPath $HcDeviceBase -Raw
$instantiated = New-Object System.Collections.Generic.HashSet[string]
foreach ($m in [regex]::Matches($idText, 'new\s+([A-Za-z0-9_]+)\s*\(\s*\)')) { $instantiated.Add($m.Groups[1].Value) | Out-Null }

$failures = New-Object System.Collections.Generic.List[string]
# --- reviewed exceptions (documented, not drift) ---
# 1) second-color on AYANEOFlipDS/Flip1SDS: HC removes DynamicLighting/Brightness
#    (AYANEOFlipDS.cs:21-22) but keeps DynamicLightingSecondLEDColor set. YMCC drops
#    the second-color flag too, because the LED pipeline is off for these classes and
#    the front-end would otherwise render a muted second-color field. No user-visible
#    capability difference.
$acceptedSecondColorExclusions = @('AYANEOFlipDS', 'AYANEOFlip1SDS')
# 2) 'wave' on ROG Ally family: REVOKED 2026-09-29. HC's SetLedColor maps
#    LEDLevel.Wave -> AuraMode.Wave (ROGAlly.cs:492-493) but HC's
#    DynamicLightingCapabilities (ROGAlly.cs:120-124) does not advertise Wave, so the
#    first-line flag gate (ROGAlly.cs:475) makes that case unreachable and HC's
#    DevicePage hides the Wave control (Views\Pages\DevicePage.xaml.cs:156). YMCC's
#    native ROG backend has no wave branch either and the front-end has no wave label,
#    so advertising it was a phantom capability of the same kind as the MSI invalid-mode
#    report. The exception is now empty; any extra effect is drift.
$acceptedExtraEffects = @()
foreach ($cls in ($bases.Keys | Sort-Object -Unique)) {
    if (-not $instantiated.Contains($cls)) { continue }
    $flags = Get-Effective $cls 'flag'
    $light = $flags -contains 'DynamicLighting'
    $second = $flags -contains 'DynamicLightingSecondLEDColor'
    if ($light -ne $ymccLight.Contains($cls)) { $failures.Add("lighting-set: $cls hc=$light ymcc=$($ymccLight.Contains($cls))") }
    if ($second -ne $ymccSecond.Contains($cls) -and -not $acceptedSecondColorExclusions.Contains($cls)) {
        $failures.Add("second-color: $cls hc=$second ymcc=$($ymccSecond.Contains($cls))")
    }
    if ($light) {
        $exp = Get-Effects $cls
        $act = if ($ymccEffects.ContainsKey($cls)) { $ymccEffects[$cls] } else { @() }
        $extra = @($act | Where-Object { -not ($exp -contains $_) })
        $missing = @($exp | Where-Object { -not ($act -contains $_) })
        $extraOk = ($missing.Count -eq 0) -and (@($extra | Where-Object { -not ($acceptedExtraEffects -contains $_) }).Count -eq 0)
        if (($exp -join ',') -ne ($act -join ',') -and -not $extraOk) {
            $failures.Add("led-effects: $cls hc=[$($exp -join ',')] ymcc=[$($act -join ',')]")
        }
    }
}
foreach ($cls in ($ymccLight | Sort-Object)) {
    if (-not $bases.ContainsKey($cls)) { $failures.Add("lighting-set-stale: $cls (not an HC class)") }
}
foreach ($cls in ($ymccSecond | Sort-Object)) {
    if (-not $bases.ContainsKey($cls)) { $failures.Add("second-color-stale: $cls (not an HC class)") }
}

Write-Output "lighting-audit: hc-classes=$($bases.Count) instantiables=$($instantiated.Count) ymcc-light=$($ymccLight.Count) ymcc-second=$($ymccSecond.Count) ymcc-effects=$($ymccEffects.Count)"
if ($failures.Count -eq 0) {
    Write-Output "LIGHTING_CAPABILITY_COVERAGE=PASS"
    exit 0
}
$failures | ForEach-Object { Write-Output $_ }
Write-Output "LIGHTING_CAPABILITY_COVERAGE=FAIL ($($failures.Count))"
exit 1