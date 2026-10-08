# OEM key (special/back button) coverage audit (2026-09-16)
#
# Cross-checks HandheldCompanion's OEMChords against YMCC's catalog + chord profile
# tables, so the 専用按键 domain is verified mechanically instead of by manual census.
#
# HC side  : every `public class` in Devices\**\*.cs with OEMChords.Add(new KeyboardChord(...))
#            -> visible (non silenced) chords, their down-key sets, mouse-ness.
# YMCC side: - keyId -> bit table (nativeKeyboardShortcutMaskBit)
#            - chord profile rules kP_* (kvk mask -> key names + bit + backIndex + mouseChord)
#            - catalog routing (oemCatalogForFamily) and profile routing (oemChordProfileIdFor)
#              parsed from their `case YmccFamily::X:` blocks (ordered `has("T")` tests)
#            - k*Keys catalog arrays -> keyIds
#            - MI_RUN pairs (family, deviceClass) = the SKUs YMCC resolves
#
# Checks:
#   A) per MI_RUN machine: YMCC catalog key count == HC visible-chord count
#   B) every catalogued keyId has a bit in nativeKeyboardShortcutMaskBit
#      (i.e. it can actually be collected/consumed)
#   C) every profile rule bit maps back to a catalogued keyId of that profile's machine
#   D) every HC visible chord's key set exists among YMCC's chord-profile rule key sets
#      (global coverage; mouse chords must appear as a mouseChord rule)
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\oem_key_coverage_audit.ps1
# Exit code 0 = no gaps.

param(
    [string]$HcDevicesRoot = 'g:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902\HandheldCompanion\Devices',
    [string]$MainCpp       = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp',
    [string]$OutputRoot    = ''
)
$ErrorActionPreference = 'Stop'
foreach ($p in @($HcDevicesRoot, $MainCpp)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" } }

# ---------- HC: class -> visible chords ----------
$hcChords = @{}          # class -> list of @{ name; keys(list of normalized); mouse(bool) }
$hcBases = @{}
foreach ($f in (Get-ChildItem -Path $HcDevicesRoot -Recurse -Filter *.cs)) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    # strip comments so commented-out chords (e.g. OneXFly M1/M2) are not counted
    $text = [regex]::Replace($text, '/\*[\s\S]*?\*/', '')
    $text = [regex]::Replace($text, '//[^\r\n]*', '')
    $cm = [regex]::Matches($text, 'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_\.]+)')
    for ($i = 0; $i -lt $cm.Count; $i++) {
        $cls = $cm[$i].Groups[1].Value
        $hcBases[$cls] = $cm[$i].Groups[2].Value.Split('.')[-1]
        $start = $cm[$i].Index
        $end = if ($i + 1 -lt $cm.Count) { $cm[$i + 1].Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        $list = New-Object System.Collections.Generic.List[object]
        foreach ($m in [regex]::Matches($body, 'OEMChords\.Add\(new\s+\w*Chord\(([\s\S]*?)\)\)\s*;')) {
            $args = $m.Groups[1].Value
            $nameM = [regex]::Match($args, '"([^"]*)"')
            $name = if ($nameM.Success) { $nameM.Groups[1].Value } else { '?' }
            # silenced flag: 4th positional arg, followed by either ButtonFlags or a named arg
            # (LegionGoTablet uses "true, flushInterval: 10")
            $silM = [regex]::Match($args, ',\s*(true|false)\s*,\s*(?:ButtonFlags|flushInterval)')
            $silenced = $silM.Success -and $silM.Groups[1].Value -eq 'true'
            $flagM = [regex]::Match($args, 'ButtonFlags\.([A-Za-z0-9]+)')
            $flag = if ($flagM.Success) { $flagM.Groups[1].Value } else { 'None' }
            $keys = New-Object System.Collections.Generic.List[string]
            $mouse = $false
            foreach ($k in [regex]::Matches($args, 'KeyCode\.([A-Za-z0-9]+)')) {
                $kname = $k.Groups[1].Value
                if ($kname -in @('LButton', 'XButton2', 'OemClear')) { $mouse = $true; continue }
                if (-not $keys.Contains($kname)) { $keys.Add($kname) }
            }
            $list.Add(@{ name = $name; keys = @($keys); mouse = $mouse; silenced = $silenced; flag = $flag }) | Out-Null
        }
        if ($list.Count -gt 0) { $hcChords[$cls] = $list }
    }
}
# inheritance: a class without own chords inherits its base's (e.g. AYANEO2 : AYANEODeviceCEc)
function Get-HcVisibleChords([string]$cls) {
    $c = $cls; $seen = 0
    while ($c -and $seen -lt 12) {
        if ($hcChords.ContainsKey($c)) {
            $out = @()
            foreach ($ch in $hcChords[$c]) { if (-not $ch.silenced) { $out += $ch } }
            return @($out)
        }
        if (-not $hcBases.ContainsKey($c)) { break }
        $c = $hcBases[$c]; $seen++
    }
    return @()
}

# ---------- YMCC: keyId -> bit ----------
$cpp = Get-Content -LiteralPath $MainCpp -Raw
$bitIdx = $cpp.IndexOf('nativeKeyboardShortcutMaskBit(const std::string& code, uint64_t& bit)')
$bitEnd = $cpp.IndexOf('};', $bitIdx)
$bitBlock = $cpp.Substring($bitIdx, $bitEnd - $bitIdx)
$keyIdBit = @{}
foreach ($m in [regex]::Matches($bitBlock, '\{"([A-Za-z0-9]+)",\s*(?:1ull\s*<<\s*(\d+)|XINPUT_GAMEPAD_[A-Z_]+)\}')) {
    if ($m.Groups[2].Success) { $keyIdBit[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
}
# kOemBit* constant values
$consts = @{}
foreach ($m in [regex]::Matches($cpp, '\bkOemBit([A-Za-z0-9]+)\s*=\s*1ull\s*<<\s*(\d+)')) {
    $consts[$m.Groups[1].Value] = [int]$m.Groups[2].Value
}

# ---------- YMCC: chord profile rules ----------
# kvk enum values
$kvk = @{}
foreach ($m in [regex]::Matches($cpp, '(kVk[A-Za-z0-9]+)\s*=\s*1u\s*<<\s*(\d+)')) { $kvk[$m.Groups[1].Value] = 1 -shl [int]$m.Groups[2].Value }
# rule arrays: static const OemChordRule kP_X[] = { { <mask>, <bit>, <back>, <group>, <mouse> }, ... };
$profiles = @{}   # kP_X -> list of @{ mask; bitName; back; group; mouse }
foreach ($m in [regex]::Matches($cpp, 'static const OemChordRule (kP_[A-Za-z0-9]+)\[\]\s*=\s*\{([\s\S]*?)\};')) {
    $rules = New-Object System.Collections.Generic.List[object]
    foreach ($r in [regex]::Matches($m.Groups[2].Value, '\{([^{}]*)\}')) {
        $parts = $r.Groups[1].Value.Split(',') | ForEach-Object { $_.Trim() }
        if ($parts.Count -lt 5) { continue }
        $bitName = ($parts[1] -replace 'kOemBit', '')
        $rules.Add(@{ mask = $parts[0]; bit = $bitName; back = $parts[2]; group = $parts[3]; mouse = ($parts[4] -eq 'true') }) | Out-Null
    }
    $profiles[$m.Groups[1].Value] = $rules
}
function Get-RuleKeys([string]$maskExpr) {
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($m in [regex]::Matches($maskExpr, 'kVk([A-Za-z0-9]+)')) {
        $n = $m.Groups[1].Value
        $canon = switch -Regex ($n) {
            '^(Ctrl)$' { 'Ctrl' }
            '^(Alt)$' { 'Alt' }
            '^(Shift)$' { 'Shift' }
            '^(Win)$' { 'Win' }
            default { $n }
        }
        if (-not $names.Contains($canon)) { $names.Add($canon) }
    }
    return @($names | Sort-Object)
}

# ---------- YMCC: routing tables ----------
function Parse-Route([string]$marker, [string]$returnPattern) {
    # returns hashtable family -> list of @{ tokens = @(...); value = 'NAME' } in source order
    $idx = $cpp.IndexOf($marker)
    if ($idx -lt 0) { throw "marker not found: $marker" }
    $end = $cpp.IndexOf("`n}", $idx)
    $block = $cpp.Substring($idx, $end - $idx)
    $cuts = [regex]::Matches($block, 'case\s+YmccFamily::([A-Za-z0-9_]+):')
    $routes = @{}
    $bodies = @()
    for ($i = 0; $i -lt $cuts.Count; $i++) {
        $fam = $cuts[$i].Groups[1].Value
        $start = $cuts[$i].Index + $cuts[$i].Length
        $stop = if ($i + 1 -lt $cuts.Count) { $cuts[$i + 1].Index } else { $block.IndexOf('default:', $start) }
        if ($stop -lt 0) { $stop = $block.Length }
        $bodies += @{ fam = $fam; body = $block.Substring($start, $stop - $start) }
    }
    # grouped case labels ("case A: case B: ... body") -> propagate the next non-empty body (walk backwards)
for ($i = $bodies.Count - 1; $i -ge 0; $i--) {
        if ($bodies[$i].body.Trim().Length -eq 0 -and $i + 1 -lt $bodies.Count) {
            $bodies[$i].body = $bodies[$i + 1].body
        }
    }
    foreach ($b in $bodies) {
        $entries = New-Object System.Collections.Generic.List[object]
        $remaining = $b.body
        foreach ($im in [regex]::Matches($b.body, 'if\s*\(([\s\S]*?)\)\s*return\s*' + $returnPattern)) {
            $tokens = @()
            foreach ($t in [regex]::Matches($im.Groups[1].Value, 'has\("([^"]+)"\)')) { $tokens += $t.Groups[1].Value }
            $entries.Add(@{ tokens = $tokens; value = $im.Groups[2].Value }) | Out-Null
        }
        # the default is the return that is not part of any if-return: drop if-return spans first
        $stripped = $b.body
        foreach ($im in [regex]::Matches($b.body, 'if\s*\([\s\S]*?\)\s*return\s*' + $returnPattern)) {
            $stripped = $stripped.Replace($im.Value, '')
        }
        # ROG's real profile route uses a guarded ternary, not an if-return.
        $tm = [regex]::Match($stripped, 'return\s+has\("([^"]+)"\)\s*\?\s*' + $returnPattern + '\s*:\s*' + $returnPattern)
        if ($tm.Success) {
            $entries.Add(@{ tokens = @($tm.Groups[1].Value); value = $tm.Groups[2].Value }) | Out-Null
            $entries.Add(@{ tokens = @(); value = $tm.Groups[3].Value }) | Out-Null
            $stripped = $stripped.Replace($tm.Value, '')
        }
        $dm = [regex]::Match($stripped, 'return\s*' + $returnPattern)
        if ($dm.Success) { $entries.Add(@{ tokens = @(); value = $dm.Groups[1].Value }) | Out-Null }
        if (-not $routes.ContainsKey($b.fam)) { $routes[$b.fam] = $entries }
    }
    return $routes
}
$catalogRoutes = Parse-Route 'static OemCatalogRef oemCatalogForFamily' 'oemCatalogOf\((\w+)\)'
$profileRoutes = Parse-Route 'static int oemChordProfileIdFor' 'id\(OemChordProfileId::(\w+)\)'
# profile enum order -> array index
$enumM = [regex]::Match($cpp, 'enum class OemChordProfileId\s*:\s*int\s*\{([\s\S]*?)\};')
$enumNames = @()
foreach ($m in [regex]::Matches($enumM.Groups[1].Value, '([A-Za-z_][A-Za-z0-9_]*)')) { $enumNames += $m.Groups[1].Value }
# profile array order
$profArrM = [regex]::Match($cpp, 'static const OemChordProfile kOemProfiles\[\]\s*=\s*\{([\s\S]*?)\};')
$profArrNames = @()
foreach ($m in [regex]::Matches($profArrM.Groups[1].Value, '\{\s*(kP_[A-Za-z0-9]+|nullptr)')) { $profArrNames += ($m.Groups[1].Value) }
# Routes return enum IDs, while rule tables are keyed by array name. Never
# silently skip per-machine checks by looking up "GpdLegacy" as "kP_GpdLegacy".
if ($enumNames.Count -ne $profArrNames.Count) { throw 'Profile enum/array cardinality mismatch' }
$profileArrays = @{}
for ($i = 0; $i -lt $enumNames.Count; $i++) { $profileArrays[$enumNames[$i]] = $profArrNames[$i] }
function Resolve-ProfileArray([string]$profileId) {
    if (-not $profileId) { return $null }
    if (-not $profileArrays.ContainsKey($profileId)) { throw "Unknown routed profile ID: $profileId" }
    $name = $profileArrays[$profileId]
    if ($name -eq 'nullptr') { return $null }
    if (-not $profiles.ContainsKey($name)) { throw "Routed profile array has no parsed rules: $profileId / $name" }
    return $name
}
function Route-Lookup($routes, [string]$fam, [string]$cls) {
    if (-not $routes.ContainsKey($fam)) { return $null }
    # routing conditions use OR only (has("A") || has("B") ...): any token may match
    foreach ($e in $routes[$fam]) {
        if ($e.tokens.Count -eq 0) { continue }
        foreach ($t in $e.tokens) {
            if ($cls -like "*$t*") { return $e.value }
        }
    }
    foreach ($e in $routes[$fam]) { if ($e.tokens.Count -eq 0) { return $e.value } }
    return $null
}
# catalog arrays: name -> keyIds
$catalogs = @{}
foreach ($m in [regex]::Matches($cpp, 'static const OemKeyDef (k[A-Za-z0-9]+Keys)\[\]\s*=\s*\{([\s\S]*?)\};')) {
    $ids = New-Object System.Collections.Generic.List[string]
    foreach ($k in [regex]::Matches($m.Groups[2].Value, '\{\s*"([A-Za-z0-9]+)"\s*,')) { $ids.Add($k.Groups[1].Value) | Out-Null }
    $catalogs[$m.Groups[1].Value] = $ids
}
# MI_RUN pairs
$miRuns = New-Object System.Collections.Generic.List[object]
foreach ($m in [regex]::Matches($cpp, 'MI_RUN\("([^"]+)",\s*\w+,\s*YmccFamily::([A-Za-z0-9_]+),\s*"HandheldCompanion\.Devices\.([A-Za-z0-9_]+)"')) {
    $miRuns.Add(@{ name = $m.Groups[1].Value; family = $m.Groups[2].Value; cls = $m.Groups[3].Value }) | Out-Null
}

# ---------- checks ----------
$failures = New-Object System.Collections.Generic.List[string]
# --- reviewed exceptions (documented, not drift) ---
# SteamDeck: HC has no OEMChords; its Special/Special2 keys come from DeviceHotkeys
# (SteamDeck.cs:89-90), which YMCC catalogues as embedded keys.
$acceptedExtraKeys = @{ 'SteamDeck' = @('special', 'special2') }
# OneXPlayer X1 / G1 family: extra keys come from the vendor HID monitor
# (MapVendorButton 0x21/0x22/0x23/0x24, OneXPlayerX1.cs:513-523), not OEMChords.
$acceptedExtraKeys['OneXPlayerX1'] = @('special', 'l4', 'r4')
$acceptedExtraKeys['OneXPlayerX1AMD'] = @('special', 'l4', 'r4')
$acceptedExtraKeys['OneXPlayerX1Intel'] = @('special', 'l4', 'r4')
$acceptedExtraKeys['OneXPlayerX1Mini'] = @('special', 'l4', 'r4')
$acceptedExtraKeys['OneXPlayerX1Pro'] = @('special', 'l4', 'r4')

foreach ($run in $miRuns) {
    $hc = Get-HcVisibleChords $run.cls
    # HC key count = distinct ButtonFlags among visible chords (dedupes re-encodings
    # like ClawA1M's three OEM2 chords)
    $hcFlags = New-Object System.Collections.Generic.HashSet[string]
    foreach ($ch in $hc) { if ($ch.flag -ne 'None' -and $ch.flag -ne '') { $hcFlags.Add($ch.flag) | Out-Null } }
    $catName = Route-Lookup $catalogRoutes $run.family $run.cls
    $profId = Route-Lookup $profileRoutes $run.family $run.cls
    $profName = Resolve-ProfileArray $profId
    $cat = if ($catName -and $catalogs.ContainsKey($catName)) { $catalogs[$catName] } else { @() }
    $extra = @()
    if ($acceptedExtraKeys.ContainsKey($run.cls)) { $extra = $acceptedExtraKeys[$run.cls] }
    if ($cat.Count -lt $hcFlags.Count) {
        $failures.Add("count: $($run.name) [$($run.cls)] hc-keys=$($hcFlags.Count) ymcc-catalog=$($cat.Count) (cat=$catName)")
    }
    # B: every catalogued keyId must have a consumable bit
    foreach ($id in $cat) {
        if (-not $keyIdBit.ContainsKey($id)) { $failures.Add("no-bit: $($run.name) catalogued keyId '$id' has no nativeKeyboardShortcutMaskBit entry") }
    }
    # C: profile rule bits must belong to this machine's catalog
    if ($profName -and $profiles.ContainsKey($profName)) {
        foreach ($rule in $profiles[$profName]) {
            if (-not $consts.ContainsKey($rule.bit)) { $failures.Add("unknown-rule-bit: $($run.name) $profName $($rule.bit)"); continue }
            $bit = $consts[$rule.bit]
            $matched = $false
            foreach ($id in $cat) { if ($keyIdBit.ContainsKey($id) -and $keyIdBit[$id] -eq $bit) { $matched = $true; break } }
            if (-not $matched) {
                $failures.Add("orphan-rule: $($run.name) profile=$profName rule bit=$bit not in catalog ($($cat -join ','))")
            }
        }
    }
}
# D: global chord coverage (HC chord key sets must exist among profile rules)
$ymccRuleSets = New-Object System.Collections.Generic.HashSet[string]
$ymccMouse = $false
foreach ($pname in $profiles.Keys) {
    foreach ($rule in $profiles[$pname]) {
        if ($rule.mouse) { $ymccMouse = $true; continue }
        $names = Get-RuleKeys $rule.mask
        $ymccRuleSets.Add(($names -join '+')) | Out-Null
    }
}
# HC key name -> canonical token (modifier sides collapse)
function Canon-HcKey([string]$k) {
    switch -Regex ($k) {
        '^(LControl|RControl|LControlKey|RControlKey)$' { return 'Ctrl' }
        '^(LShift|RShift|LShiftKey|RShiftKey)$' { return 'Shift' }
        '^(LMenu|RMenu|RAlt|LMenuKey|RMenuKey)$' { return 'Alt' }
        '^(LWin|RWin|LWinKey|RWinKey)$' { return 'Win' }
        '^Escape$' { return 'Esc' }
        '^Delete$' { return 'Del' }
        '^Snapshot$' { return 'Snap' }
        default { return $k }
    }
}
$hcSets = New-Object System.Collections.Generic.HashSet[string]
foreach ($cls in $hcChords.Keys) {
    foreach ($ch in $hcChords[$cls]) {
        if ($ch.silenced) { continue }
        if ($ch.mouse) { continue }
        if ($ch.keys.Count -eq 0) { continue }
        $canon = @()
        foreach ($k in $ch.keys) { $canon += (Canon-HcKey $k) }
        $hcSets.Add(((@($canon | Sort-Object)) -join '+')) | Out-Null
    }
}
# Two source-proven GPD Win5 compatibility encodings are represented by the
# base-key matcher, not duplicate array rows. Require both the actual GPD row
# and the production identity/prefix gate; never grant a generic subset match.
$gpdCompat = @{}
if ($cpp.Contains('((kbDown & rule.keys) == rule.keys)') -and $cpp.Contains('rule.backIndex != 0 && !gpdIfaceReady') -and $cpp.Contains('g_gpdBackIfaceReady') -and
    $profiles.ContainsKey('kP_GpdWin5')) {
    foreach ($r in $profiles['kP_GpdWin5']) {
        if ($r.mask -eq 'kVkF14' -and $r.bit -eq 'L4') { $gpdCompat['Ctrl+F14+Shift'] = $true }
        if ($r.mask -eq 'kVkF15' -and $r.bit -eq 'R4') { $gpdCompat['F15+F3'] = $true }
    }
}
foreach ($s in ($hcSets | Sort-Object)) {
    if ($gpdCompat.ContainsKey($s)) { Write-Output "GPD_COMPAT_ENCODING_CHECKED: $s -> identity-qualified F14/F15 production gate"; continue }
    if (-not $ymccRuleSets.Contains($s)) { $failures.Add("chord-missing: HC chord key set [$s] not present in any YMCC chord profile") }
}
if ($hcChords.Values | Where-Object { $_ | Where-Object { $_.mouse -and -not $_.silenced } }) {
    if (-not $ymccMouse) { $failures.Add("mouse-chord-missing: HC has a visible mouse chord but no YMCC mouseChord rule") }
}

if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $matrix = foreach ($run in $miRuns) {
        $catName = Route-Lookup $catalogRoutes $run.family $run.cls
        $profId = Route-Lookup $profileRoutes $run.family $run.cls
    $profName = Resolve-ProfileArray $profId
        $ids = if ($catName -and $catalogs.ContainsKey($catName)) { @($catalogs[$catName]) } else { @() }
        $rules = if ($profName -and $profiles.ContainsKey($profName)) { @($profiles[$profName] | ForEach-Object { $_ }) } else { @() }
        [pscustomobject]@{ identityCase=$run.name;family=$run.family;deviceClass=$run.cls;catalog=$catName;keys=$ids;profileId=$profId;profile=$profName;keyboardRows=@($rules | Where-Object {-not $_.mouse}).Count;mouseRows=@($rules | Where-Object {$_.mouse}).Count;deviceVerified=$false;vendorDefaultSuppression='UNVERIFIED';coverage='declared catalog / profile source binding; not device proof' }
    }
    [ordered]@{ source=$MainCpp;sourceSha256=(Get-FileHash -LiteralPath $MainCpp).Hash;hcSource=$HcDevicesRoot;modelCases=$miRuns.Count;profileCount=$profiles.Count;hcChordSets=$hcSets.Count;gpdCompatibilityEncodings=@($gpdCompat.Keys);failures=$failures.ToArray();models=@($matrix) } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'oem-model-matrix.json') -Encoding UTF8
}
Write-Output "oem-key-audit: hc-classes-with-chords=$($hcChords.Count) mi-run-machines=$($miRuns.Count) catalogs=$($catalogs.Count) profiles=$($profiles.Count) hc-chord-sets=$($hcSets.Count)"
if ($failures.Count -eq 0) {
    Write-Output "OEM_KEY_COVERAGE=PASS"
    exit 0
}
$failures | Select-Object -First 40 | ForEach-Object { Write-Output $_ }
Write-Output "OEM_KEY_COVERAGE=FAIL ($($failures.Count))"
exit 1