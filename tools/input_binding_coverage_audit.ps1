# Input binding coverage audit (2026-09-16)
#
# Virtual-gamepad domain check: every SKU YMCC resolves must bind a physical HID
# source (inputHidKey), and where YMCC embeds vendor/product IDs they must match
# HandheldCompanion's per-device declarations (vendorId / productIds).
#
# YMCC inputHidKey forms:
#   "rog-xbox:0x1ABE"            -> product id
#   "msi-claw:0x1901|0x1902"     -> product ids
#   "zotac-gamingzone:0x1EE9:0x1590" -> vendorId:productId
#   "<family>:hid" / ":hid-*"    -> generic discovery (no ids to compare)
#
# Checks:
#   A) every family present in the resolver assigns a non-empty inputHidKey
#   B) ids embedded in inputHidKey must exist in HC's declarations for that family
#   C) HC's declared productIds must be covered by YMCC (or listed as accepted extra)
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\input_binding_coverage_audit.ps1
# Exit code 0 = no gaps.

param(
    [string]$HcDevicesRoot = 'g:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902\HandheldCompanion\Devices',
    [string]$MainCpp       = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp'
)
$ErrorActionPreference = 'Stop'
foreach ($p in @($HcDevicesRoot, $MainCpp)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" } }

# ---------- YMCC: family -> inputHidKey values (nearest preceding id.family) ----------
$cpp = Get-Content -LiteralPath $MainCpp -Raw
$familyKeys = @{}
$curFam = $null
foreach ($line in ($cpp -split "`n")) {
    $fm = [regex]::Match($line, 'id\.family\s*=\s*YmccFamily::([A-Za-z0-9_]+)')
    if ($fm.Success) { $curFam = $fm.Groups[1].Value }
    $km = [regex]::Match($line, 'id\.inputHidKey\s*=\s*"([^"]+)"')
    if ($km.Success -and $curFam) {
        if (-not $familyKeys.ContainsKey($curFam)) { $familyKeys[$curFam] = New-Object System.Collections.Generic.HashSet[string] }
        $familyKeys[$curFam].Add($km.Groups[1].Value) | Out-Null
    }
}
# every family carrying a deviceClass assignment must appear in the map
$familiesInResolver = New-Object System.Collections.Generic.HashSet[string]
foreach ($m in [regex]::Matches($cpp, 'id\.family\s*=\s*YmccFamily::([A-Za-z0-9_]+)')) { $familiesInResolver.Add($m.Groups[1].Value) | Out-Null }

# ---------- HC: class -> vendorId / productIds (numeric + named constants) ----------
$hcVendor = @{}; $hcPids = @{}
foreach ($f in (Get-ChildItem -Path $HcDevicesRoot -Recurse -Filter *.cs)) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $text = [regex]::Replace($text, '/\*[\s\S]*?\*/', '')
    $text = [regex]::Replace($text, '//[^\r\n]*', '')
    # named pid constants per file
    $consts = @{}
    foreach ($m in [regex]::Matches($text, 'const\s+int\s+(PID_[A-Za-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)')) {
        $consts[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    $cm = [regex]::Matches($text, 'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_\.]+)')
    for ($i = 0; $i -lt $cm.Count; $i++) {
        $cls = $cm[$i].Groups[1].Value
        $start = $cm[$i].Index
        $end = if ($i + 1 -lt $cm.Count) { $cm[$i + 1].Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        $vm = [regex]::Match($body, 'vendorId\s*=\s*(0x[0-9A-Fa-f]+)')
        if ($vm.Success) { $hcVendor[$cls] = $vm.Groups[1].Value }
        $pm = [regex]::Match($body, 'productIds\s*=\s*\[([^\]]*)\]')
        if ($pm.Success) {
            $pids = New-Object System.Collections.Generic.List[string]
            foreach ($e in $pm.Groups[1].Value.Split(',')) {
                $t = $e.Trim()
                if ($t -match '^(0x[0-9A-Fa-f]+)$') { $pids.Add($t) | Out-Null }
                elseif ($consts.ContainsKey($t)) { $pids.Add($consts[$t]) | Out-Null }
            }
            if ($pids.Count -gt 0) { $hcPids[$cls] = $pids }
        }
    }
}
# family -> representative HC class carrying the ids
$familyClass = @{
    'AsusRogAllyX' = 'XboxROGAllyX'; 'AsusRogAlly' = 'XboxROGAlly'; 'AsusRogAllyClassic' = 'ROGAlly'
    'AsusRogAllyXClassic' = 'ROGAllyX'; 'MsiClaw' = 'ClawA1M'; 'ZotacGamingZone' = 'GamingZone'
    'LenovoLegionGo' = 'LegionGo'; 'Gpd' = 'GPDWin5'; 'ValveSteamDeck' = 'SteamDeck'
    'Ayanero' = 'AYANEODeviceCEc'; 'OneXPlayer' = 'OneXPlayerX1'; 'Ayn' = 'AynLoki'
    'Aokzoe' = 'AOKZOEA1'; 'Minisforum' = 'MinisforumV3'; 'SuiPlay' = 'SuiPlay0X1'
}
# accepted extras: HC-declared ids YMCC intentionally does not bind (diagnostics path)
$acceptedPidExtras = @{ 'MsiClaw' = @('0x1903') }   # PID_TESTING（ClawA1M.cs:135）

# HC declarations inherit: XboxROGAlly/XboxROGAllyX take vendorId/productIds from ROGAlly
$hcBases = @{}
foreach ($f in (Get-ChildItem -Path $HcDevicesRoot -Recurse -Filter *.cs)) {
    $t = Get-Content -LiteralPath $f.FullName -Raw
    foreach ($m in [regex]::Matches($t, 'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_\.]+)')) {
        $hcBases[$m.Groups[1].Value] = $m.Groups[2].Value.Split('.')[-1]
    }
}
function Get-HcPids([string]$cls) {
    $c = $cls; $seen = 0
    while ($c -and $seen -lt 12) {
        if ($hcPids.ContainsKey($c)) { return @($hcPids[$c]) }
        if (-not $hcBases.ContainsKey($c)) { break }
        $c = $hcBases[$c]; $seen++
    }
    return @()
}
function Get-HcVendor([string]$cls) {
    $c = $cls; $seen = 0
    while ($c -and $seen -lt 12) {
        if ($hcVendor.ContainsKey($c)) { return $hcVendor[$c] }
        if (-not $hcBases.ContainsKey($c)) { break }
        $c = $hcBases[$c]; $seen++
    }
    return $null
}

$failures = New-Object System.Collections.Generic.List[string]
# ids YMCC binds anywhere (HC declares device-family-wide productIds that YMCC may
# legitimately split across sibling SKUs, e.g. ROGAlly base declares 0x1ABE+0x1B4C)
$ymccAllIds = New-Object System.Collections.Generic.HashSet[string]
foreach ($k in ($familyKeys.Values | ForEach-Object { $_ })) {
    foreach ($h in [regex]::Matches($k, '0x[0-9A-Fa-f]{4}')) { $ymccAllIds.Add($h.Value.ToUpper()) | Out-Null }
}
foreach ($fam in ($familiesInResolver | Sort-Object)) {
    if ($fam -eq 'Unknown') { continue }   # fail-safe branch: unsupported SKU binds nothing
    $keys = if ($familyKeys.ContainsKey($fam)) { @($familyKeys[$fam]) } else { @() }
    $nonEmpty = @($keys | Where-Object { $_.Trim().Length -gt 0 })
    if ($nonEmpty.Count -eq 0) { $failures.Add("hidkey-missing: family $fam resolves devices but assigns no inputHidKey") ; continue }
    # collect ids used by YMCC for this family
    $ymccIds = New-Object System.Collections.Generic.HashSet[string]
    foreach ($k in $nonEmpty) {
        foreach ($h in [regex]::Matches($k, '0x[0-9A-Fa-f]{4}')) { $ymccIds.Add($h.Value.ToUpper()) | Out-Null }
    }
    if ($ymccIds.Count -eq 0) { continue }   # generic ":hid" discovery - nothing to compare
    $cls = $familyClass[$fam]
    if (-not $cls) { $failures.Add("hidkey-unknown-family: $fam has ids but no HC class mapping") ; continue }
    $hcPidSet = New-Object System.Collections.Generic.HashSet[string]
    foreach ($p in (Get-HcPids $cls)) { $hcPidSet.Add($p.ToUpper()) | Out-Null }
    $hcVidRaw = Get-HcVendor $cls
    $hcVid = if ($hcVidRaw) { $hcVidRaw.ToUpper() } else { $null }
    # B) YMCC ids must exist in HC declarations (product ids; a vendor id may also appear)
    foreach ($id in $ymccIds) {
        if ($id -eq $hcVid) { continue }
        if (-not $hcPidSet.Contains($id)) {
            $failures.Add("hidkey-extra: $fam inputHidKey id $id not declared by HC class $cls (productIds=[$($hcPidSet -join ',')] vendorId=$hcVid)")
        }
    }
    # C) HC product ids must be bound by YMCC somewhere (minus accepted extras)
    $extra = if ($acceptedPidExtras.ContainsKey($fam)) { $acceptedPidExtras[$fam] | ForEach-Object { $_.ToUpper() } } else { @() }
    foreach ($pidVal in ($hcPidSet | Sort-Object)) {
        if (-not $ymccAllIds.Contains($pidVal) -and -not ($extra -contains $pidVal)) {
            $failures.Add("hidkey-missing-pid: $fam HC class $cls declares $pidVal but no YMCC family binds it")
        }
    }
}

Write-Output "input-binding-audit: families=$($familiesInResolver.Count) families-with-ids=$(($familyKeys.Keys | Where-Object { (@($familyKeys[$_]) | Where-Object { $_ -match '0x' }).Count -gt 0 }).Count) hc-classes-with-pids=$($hcPids.Count)"
if ($failures.Count -eq 0) {
    Write-Output "INPUT_BINDING_COVERAGE=PASS"
    exit 0
}
$failures | ForEach-Object { Write-Output $_ }
Write-Output "INPUT_BINDING_COVERAGE=FAIL ($($failures.Count))"
exit 1