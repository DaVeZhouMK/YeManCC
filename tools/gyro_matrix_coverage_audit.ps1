# Gyro/Accel matrix coverage audit (2026-09-16)
#
# Verifies YMCC's per-class IMU matrix recipes against HandheldCompanion's
# GyroMatrix/AcceleroMatrix declarations.
#
# Semantics (HC sources, locked candidate 0.32.4.0-06c0b954):
#   IDevice.AxisRemapIndices : AxisSwap['X'] -> output axis index of input X
#   IMUGyrometer.ReadingChanged / IMUAccelerometer : readingAxis[remap(input)] = raw_input;
#                                                    out[j] = readingAxis[j] * Axis[j]
#   => out[j] = Axis[j] * raw[swapInv(j)]
#
# YMCC stores recipes in two equivalent notations; both are evaluated here:
#   result form : "x-z-neg-y"          -> out = (+rawX, +rawZ, -rawY)
#   axis+swap   : "neg-x-neg-z-y-swap-yz" -> scale in place, then permute (permutation
#                                            moves the scaled value, so the sign travels)
#
# Checks:
#   A) global: every HC mapping (non-identity) must exist as some YMCC recipe
#   B) per class: the HC mapping for a class must appear among the recipes the
#      resolver assigns to that class (candidate set; branch/CPU variants allowed)
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\gyro_matrix_coverage_audit.ps1
# Exit code 0 = no gaps.

param(
    [string]$HcDevicesRoot = '',
    [string]$HcConfigsRoot = '',
    [string]$MainCpp       = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $HcDevicesRoot) { $HcDevicesRoot = Join-Path $repoRoot 'deps\handheldcompanion-runtime\source\Devices' }
if (-not $MainCpp) { $MainCpp = Join-Path $repoRoot 'native\main.cpp' }
Write-Output ("HC_SOURCE_ROOT={0}" -f (Resolve-Path -LiteralPath $HcDevicesRoot).Path)
foreach ($p in @($HcDevicesRoot, $MainCpp)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" } }

$bases = @{}; $ownGyro = @{}; $ownAccel = @{}

function Parse-Matrix([string]$text, [string]$kind) {
    # handles multi-line (Axis+AxisSwap), single-line (Axis only) and bare "new()"
    $m = [regex]::Match($text, "(?:this\.)?$kind\s*=\s*new\(\)\s*([^;]*);")
    if (-not $m.Success) { return $null }
    $init = $m.Groups[1].Value
    $axis = @(1.0, 1.0, 1.0)
    $am = [regex]::Match($init, 'Axis\s*=\s*new Vector3\(([^)]*)\)')
    if ($am.Success) { $axis = @($am.Groups[1].Value.Split(',') | ForEach-Object { [double]($_.Trim() -replace 'f$','') }) }
    $swap = @{ 'X' = 'X'; 'Y' = 'Y'; 'Z' = 'Z' }
    foreach ($p in [regex]::Matches($init, "'\s*([XYZ])\s*'\s*,\s*'\s*([XYZ])\s*'")) { $swap[$p.Groups[1].Value] = $p.Groups[2].Value }
    return @{ Axis = $axis; Swap = $swap }
}

foreach ($f in (Get-ChildItem -Path $HcDevicesRoot -Recurse -Filter *.cs)) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $cm = [regex]::Matches($text, 'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_\.]+)')
    for ($i = 0; $i -lt $cm.Count; $i++) {
        $cls = $cm[$i].Groups[1].Value
        $bases[$cls] = $cm[$i].Groups[2].Value.Split('.')[-1]
        $start = $cm[$i].Index
        $end = if ($i + 1 -lt $cm.Count) { $cm[$i + 1].Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        $g = Parse-Matrix $body 'GyroMatrix'; if ($g) { $ownGyro[$cls] = $g }
        $a = Parse-Matrix $body 'AcceleroMatrix'; if ($a) { $ownAccel[$cls] = $a }
    }
}


# HC 1.x moved matrices into Resources/Devices JSON. Model the actual filename
# lookup, not DeviceClass metadata: the loader does not use that metadata as a key.
$sourceDiagnostics = New-Object System.Collections.Generic.List[string]
$idSource = Get-Content -LiteralPath (Join-Path $HcDevicesRoot 'IDevice.cs') -Raw
if ($idSource -match 'DeviceConfigurationHelper\.LoadConfiguration') {
    if (-not $HcConfigsRoot) { $HcConfigsRoot = Join-Path (Split-Path -Parent $HcDevicesRoot) 'Resources\Devices' }
    if (-not (Test-Path -LiteralPath $HcConfigsRoot)) { throw "matrix-config-source-missing: $HcConfigsRoot" }
    $configs = @{}
    function Convert-JsonMatrix($value, [string]$label) {
        $axis = @(1.0, 1.0, 1.0)
        $swap = @{ X = 'X'; Y = 'Y'; Z = 'Z' }
        if ($null -ne $value) {
            $i = 0
            foreach ($key in @('X','Y','Z')) {
                if ($value.Axis -and $null -ne $value.Axis.$key) { $axis[$i] = [double]$value.Axis.$key }
                if ($value.AxisSwap -and $value.AxisSwap.$key) { $swap[$key] = [string]$value.AxisSwap.$key }
                $i++
            }
        }
        if (@($axis | Where-Object { $_ -notin @(-1.0,1.0) }).Count -gt 0 -or (@($swap.Values | Sort-Object -Unique) -join '') -cne 'XYZ') {
            $sourceDiagnostics.Add("matrix-config-invalid: $label; expected signed unit axes and an XYZ permutation")
            return @{ Invalid = $true }
        }
        return @{ Axis = $axis; Swap = $swap }
    }
    foreach ($file in Get-ChildItem -LiteralPath $HcConfigsRoot -Filter '*.json') {
        $config = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        $configs[$file.BaseName] = $config
        if ($config.DeviceClass -and $file.BaseName -cne $config.DeviceClass) {
            if ($bases.ContainsKey($file.BaseName)) {
                Write-Output "matrix-config-metadata-note: $($file.Name) declares $($config.DeviceClass); existing filename is the lookup key"
            } else {
                $sourceDiagnostics.Add("matrix-config-filename-mismatch: $($file.Name) declares $($config.DeviceClass); HC looks up the class name, then its base")
            }
        }
    }
    if ($configs.Count -eq 0) { throw 'matrix-config-empty: zero JSON configurations' }
    $ownGyro = @{}; $ownAccel = @{}
    foreach ($cls in $bases.Keys) {
        $cursor = $cls; $visited = @{}
        while ($cursor -and -not $configs.ContainsKey($cursor) -and $bases.ContainsKey($cursor)) {
            if ($visited.ContainsKey($cursor)) { throw "matrix-inheritance-cycle: $cls" }
            $visited[$cursor] = $true
            $cursor = $bases[$cursor]
        }
        $config = if ($cursor -and $configs.ContainsKey($cursor)) { $configs[$cursor] } else { $null }
        # A found config replaces the whole lookup; missing fields do not merge a parent.
        $ownGyro[$cls] = Convert-JsonMatrix $(if ($config) { $config.GyroMatrix } else { $null }) "$cls/$cursor/GyroMatrix"
        $ownAccel[$cls] = Convert-JsonMatrix $(if ($config) { $config.AcceleroMatrix } else { $null }) "$cls/$cursor/AcceleroMatrix"
    }
    Write-Output "HC_MATRIX_SOURCE=json configs=$($configs.Count)"
}
if ($ownGyro.Count -eq 0 -or $ownAccel.Count -eq 0) {
    throw "matrix-parse-empty: gyro=$($ownGyro.Count) accel=$($ownAccel.Count); source schema may have changed"
}

function Resolve-Matrix([string]$cls, [hashtable]$table) {
    $seen = 0
    while ($cls -and $seen -lt 12) {
        if ($table.ContainsKey($cls)) { return $table[$cls] }
        if (-not $bases.ContainsKey($cls)) { break }
        $cls = $bases[$cls]; $seen++
    }
    # IDevice constructor default: identity axis + identity swap
    return @{ Axis = @(1.0, 1.0, 1.0); Swap = @{ 'X' = 'X'; 'Y' = 'Y'; 'Z' = 'Z' } }
}

function HC-Canonical($matrix) {
    if ($matrix.Invalid) { return "(invalid-source-matrix)" }
    $s = @{}; foreach ($k in 'X','Y','Z') { $s[$k] = $matrix.Swap[$k] }
    $inv = @{}
    foreach ($k in $s.Keys) { $inv[$s[$k]] = $k }
    $ax = @{ 'X' = $matrix.Axis[0]; 'Y' = $matrix.Axis[1]; 'Z' = $matrix.Axis[2] }
    $out = @()
    foreach ($j in 'X','Y','Z') {
        $sign = 'p'
        if ($ax[$j] -lt 0) { $sign = 'n' }
        $out += ($sign + $inv[$j])
    }
    return ($out -join '-')
}

function YMCC-Canonical([string]$recipe) {
    $swap = ''; $body = $recipe
    if ($recipe -match '^(.*)-swap-([a-z]+)$') { $body = $Matches[1]; $swap = $Matches[2] }
    $parsed = @(); $neg = $false
    foreach ($t in $body.Split('-')) {
        if ($t -eq 'neg') { $neg = $true; continue }
        if ($t -match '^([xyz])$') { $parsed += @{ src = $Matches[1].ToUpper(); neg = $neg }; $neg = $false; continue }
        if ($t -match '^neg([xyz])$') { $parsed += @{ src = $Matches[1].ToUpper(); neg = $true }; $neg = $false; continue }
        return '(unparsed)'
    }
    if ($parsed.Count -ne 3) { return '(count)' }
    $map = @($parsed[0], $parsed[1], $parsed[2])
    if ($swap -eq 'yz') { $t = $map[1]; $map[1] = $map[2]; $map[2] = $t }
    elseif ($swap -eq 'xy') { $t = $map[0]; $map[0] = $map[1]; $map[1] = $t }
    elseif ($swap -eq 'yzx') { $map = @($map[1], $map[2], $map[0]) }
    elseif ($swap -eq 'zyx') { $map = @($map[2], $map[0], $map[1]) }
    elseif ($swap -ne '') { return "(swap:$swap)" }
    $out = @()
    for ($j = 0; $j -lt 3; $j++) {
        $sign = 'p'
        if ($map[$j].neg) { $sign = 'n' }
        $out += ($sign + $map[$j].src)
    }
    return ($out -join '-')
}

# resolver attribution: nearest preceding deviceClass assignment / condition wins
$cppText = Get-Content -LiteralPath $MainCpp -Raw
$yByClass = @{}
$cur = $null
foreach ($line in ($cppText -split "`n")) {
    $m1 = [regex]::Match($line, 'id\.deviceClass\s*=\s*(?:std::string\()?"HandheldCompanion\.Devices\.([A-Za-z0-9_]+)"')
    if ($m1.Success) { $cur = $m1.Groups[1].Value }
    $m2 = [regex]::Match($line, 'id\.deviceClass\s*==\s*"HandheldCompanion\.Devices\.([A-Za-z0-9_]+)"')
    if ($m2.Success) { $cur = $m2.Groups[1].Value }
    foreach ($mm in [regex]::Matches($line, 'id\.(gyro|accel)MatrixId\s*=\s*"[^"]*:(gyro|accel):([a-z0-9\-]+)"')) {
        if (-not $cur) { continue }
        $kind = $mm.Groups[1].Value
        $recipe = $mm.Groups[3].Value
        if (-not $yByClass.ContainsKey($cur)) { $yByClass[$cur] = @{} }
        if (-not $yByClass[$cur].ContainsKey($kind)) { $yByClass[$cur][$kind] = New-Object System.Collections.Generic.HashSet[string] }
        $yByClass[$cur][$kind].Add((YMCC-Canonical $recipe)) | Out-Null
    }
}
$global = New-Object System.Collections.Generic.HashSet[string]
foreach ($m in [regex]::Matches($cppText, ':(?:gyro|accel):([a-z0-9\-]+)"')) { $global.Add((YMCC-Canonical $m.Groups[1].Value)) | Out-Null }

$failures = New-Object System.Collections.Generic.List[string]
$identity = 'pX-pY-pZ'
foreach ($diagnostic in $sourceDiagnostics) { $failures.Add($diagnostic) }

# A) global coverage
foreach ($cls in ($bases.Keys | Sort-Object -Unique)) {
    $expG = HC-Canonical (Resolve-Matrix $cls $ownGyro)
    $expA = HC-Canonical (Resolve-Matrix $cls $ownAccel)
    if ($expG -ne $identity -and -not $global.Contains($expG)) { $failures.Add("matrix-missing-gyro: $cls -> $expG") }
    if ($expA -ne $identity -and -not $global.Contains($expA)) { $failures.Add("matrix-missing-accel: $cls -> $expA") }
}

# B) per-class candidate check
foreach ($cls in ($yByClass.Keys | Sort-Object -Unique)) {
    $expG = HC-Canonical (Resolve-Matrix $cls $ownGyro)
    $expA = HC-Canonical (Resolve-Matrix $cls $ownAccel)
    if ($expG -ne $identity -and $yByClass[$cls].ContainsKey('gyro') -and -not $yByClass[$cls]['gyro'].Contains($expG)) {
        $failures.Add("matrix-class-gyro: $cls hc=$expG ymcc=[$($yByClass[$cls]['gyro'] -join ',')]")
    }
    if ($expA -ne $identity -and $yByClass[$cls].ContainsKey('accel') -and -not $yByClass[$cls]['accel'].Contains($expA)) {
        $failures.Add("matrix-class-accel: $cls hc=$expA ymcc=[$($yByClass[$cls]['accel'] -join ',')]")
    }
}

Write-Output "gyro-matrix-audit: hc-classes=$($bases.Count) parsed-gyro=$($ownGyro.Count) parsed-accel=$($ownAccel.Count) ymcc-classes=$($yByClass.Count)"
if ($failures.Count -eq 0) {
    Write-Output "GYRO_MATRIX_COVERAGE=PASS"
    exit 0
}
$failures | ForEach-Object { Write-Output $_ }
Write-Output "GYRO_MATRIX_COVERAGE=FAIL ($($failures.Count))"
exit 1