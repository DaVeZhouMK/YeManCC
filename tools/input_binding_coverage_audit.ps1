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
    [string]$HcDevicesRoot = '',
    [string]$MainCpp       = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $HcDevicesRoot) { $HcDevicesRoot = Join-Path $repoRoot 'deps\handheldcompanion-runtime\source\Devices' }
if (-not $MainCpp) { $MainCpp = Join-Path $repoRoot 'native\main.cpp' }
Write-Output ("HC_SOURCE_ROOT={0}" -f (Resolve-Path -LiteralPath $HcDevicesRoot).Path)
foreach ($p in @($HcDevicesRoot, $MainCpp)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" } }

# ---------- YMCC: lexical resolver scopes (unknown early returns are local) ----------
$cpp = Get-Content -LiteralPath $MainCpp -Raw
$resolverStart = $cpp.IndexOf('static void ymccResolveFamily(MachineIdentity& id)')
if ($resolverStart -lt 0) { throw 'Identity resolver missing' }
$resolverStart = $cpp.IndexOf('{', $resolverStart)
$familyKeys = @{}
$familiesInResolver = New-Object System.Collections.Generic.HashSet[string]
$scopes = New-Object System.Collections.Generic.List[string]
# Match strings/comments before braces; family and HID assignments are consumed whole.
$lex = '(?<comment>/\*[\s\S]*?\*/|//[^\r\n]*)|(?<family>id\.family\s*=\s*YmccFamily::(?<name>\w+))|(?<key>id\.inputHidKey\s*=\s*"(?<value>[^"\r\n]+)")|"(?:\\.|[^"\\])*"|(?<open>\{)|(?<close>\})'
foreach ($token in [regex]::Matches($cpp.Substring($resolverStart), $lex)) {
    if ($token.Groups['open'].Success) {
        $parent = if ($scopes.Count) { $scopes[$scopes.Count - 1] } else { 'Unknown' }
        $scopes.Add($parent)
    } elseif ($token.Groups['close'].Success) {
        if (!$scopes.Count) { throw 'Unbalanced identity resolver' }
        $scopes.RemoveAt($scopes.Count - 1)
        if (!$scopes.Count) { break }
    } elseif ($token.Groups['family'].Success) {
        $fam = $token.Groups['name'].Value
        $scopes[$scopes.Count - 1] = $fam
        [void]$familiesInResolver.Add($fam)
    } elseif ($token.Groups['key'].Success) {
        $fam = $scopes[$scopes.Count - 1]
        if (!$familyKeys.ContainsKey($fam)) { $familyKeys[$fam] = New-Object System.Collections.Generic.HashSet[string] }
        [void]$familyKeys[$fam].Add($token.Groups['value'].Value)
    }
}
if ($scopes.Count -or !$familyKeys.Count) { throw 'Identity resolver parsing incomplete' }

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