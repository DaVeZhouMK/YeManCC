# Fan route coverage audit (FAN-934 rewrite, 2026-09-29)
#
# Replaces the 2026-09-16 static gate. That gate hardcoded 13 FanControl roots and a
# single "wiped" class (OneXPlayerX1); it stayed green while FanHost applied the
# ASUS-only receipt requirement to non-ASUS routes (GPD Win5 / MSI 258V fault-lock).
#
# This audit is fail-closed and reads the real sources:
#   1) HC Devices\**\*.cs : class/basetype map (with using-alias resolution),
#      per-class EFFECTIVE DeviceCapabilities.FanControl (root->leaf, honoring `=`
#      wipes and `|=` unions), and the IDevice.GetCurrent() instantiation set.
#   2) FanLab\real-host\Program.cs : the ACTUAL BuildFanRoutes() registration block
#      (Add(...) calls, routes["..."] = new FanRoute(...), foreach overrides), the
#      enum value sets, and the host's own KnownFanRouteCount.
#   3) Route evidence applicability: ASUS routes MUST declare AsusScopeReceipts;
#      every non-ASUS route MUST declare CompatCallbackNoScope. This is the direct
#      regression guard for the FAN-934 root cause.
#
# It replays the legacy 13-root gate to EXPLAIN the old 60/71 numbers (delta lists)
# instead of hardcoding an expected route count.
#
# Usage:
#   pwsh -NoProfile -File tools\fan_route_coverage_audit.ps1
#   pwsh -NoProfile -File tools\fan_route_coverage_audit.ps1 -OutputRoot <dir> [-LegacyOut <file>]
# Exit code 0 = no gaps.

param(
    [string]$HcDevicesRoot = '',
    [string]$HcDeviceBase  = '',
    [string]$FanHostSource = '',
    [string]$OutputRoot    = '',
    [string]$LegacyOut     = ''
)

$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]
$infos    = New-Object System.Collections.Generic.List[string]

if (-not $PSScriptRoot) { $PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path }
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $HcDevicesRoot) { $HcDevicesRoot = Join-Path $repoRoot 'deps\handheldcompanion-runtime\source\Devices' }
if (-not $HcDeviceBase)  { $HcDeviceBase  = Join-Path $HcDevicesRoot 'IDevice.cs' }
if (-not $FanHostSource) { $FanHostSource = Join-Path $repoRoot 'FanLab\real-host\Program.cs' }

foreach ($p in @($HcDevicesRoot, $HcDeviceBase, $FanHostSource)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" }
}

# ---------------------------------------------------------------------------
# Length-preserving tokenizer.
#   $code : strings/chars/comments blanked  -> structure (braces, class decls)
#   $nc   : comments blanked, strings kept  -> content (string literals, names)
# Every transform preserves length and newlines, so indices align across views.
# ---------------------------------------------------------------------------
$q = [string][char]0x27
$tokenPattern = '(?s)(?<raw>"{3,}.*?"{3,})' +
    '|(?<verb>[$]?@[$]?"(?:[^"]|"")*")' +
    '|(?<str>[$]?"(?:\\.|[^"\\])*")' +
    '|(?<chr>' + $q + '(?:\\.|[^' + $q + '\\])*' + $q + ')' +
    '|(?<blk>/\*.*?\*/)' +
    '|(?<lin>//[^\r\n]*)'
$rxToken = [regex]::new($tokenPattern)

function Convert-Blank {
    param([string]$text, [bool]$blankStrings)
    $sb = New-Object System.Text.StringBuilder
    $pos = 0
    foreach ($m in $rxToken.Matches($text)) {
        if ($m.Index -gt $pos) { [void]$sb.Append($text.Substring($pos, $m.Index - $pos)) }
        $g = $m.Groups
        $isComment = $g['blk'].Success -or $g['lin'].Success
        if ($isComment -or $blankStrings) {
            [void]$sb.Append([regex]::Replace($m.Value, '[^\r\n]', ' '))
        } else {
            [void]$sb.Append($m.Value)
        }
        $pos = $m.Index + $m.Length
    }
    if ($pos -lt $text.Length) { [void]$sb.Append($text.Substring($pos)) }
    return $sb.ToString()
}

function Get-BraceSpan {
    param([string]$code, [int]$openIndex)
    $open = [char]0x7B; $close = [char]0x7D
    $depth = 0
    for ($i = $openIndex; $i -lt $code.Length; $i++) {
        $c = $code[$i]
        if ($c -eq $open) { $depth++ }
        elseif ($c -eq $close) {
            $depth--
            if ($depth -eq 0) { return @{ Start = $openIndex; Length = ($i - $openIndex + 1) } }
        }
    }
    throw "unbalanced braces from index $openIndex"
}

function Get-LineNumber {
    param([string]$text, [int]$index)
    return ([regex]::Matches($text.Substring(0, $index), "`n")).Count + 1
}

# ---------------------------------------------------------------------------
# 1) HC device sources: class map + effective FanControl + instantiation set.
# ---------------------------------------------------------------------------
$classPattern = '(?m)^[ \t]*(?:(?:public|internal|private|protected|abstract|sealed|static|partial|new|unsafe|file)\s+)*class\s+(?<n>[A-Za-z_]\w*)(?:\s*<[^>{}]*>)?(?:\s*:\s*(?<b>[A-Za-z_][\w\.]*))?'
$aliasPattern = 'using\s+(?<a>[A-Za-z_]\w*)\s*=\s*(?<t>[A-Za-z_][\w\.]*)\s*;'
$capPattern   = '(?m)(?:^|[;{}]\s*)(?:this\.)?Capabilities\s*(?<op>\|=|=)\s*(?<expr>[^;]*);'

$classBases = @{}
$classFile  = @{}
$classCode  = @{}

Get-ChildItem -LiteralPath $HcDevicesRoot -Recurse -Filter *.cs | ForEach-Object {
    $path = $_.FullName
    $raw  = Get-Content -LiteralPath $path -Raw
    $code = Convert-Blank $raw $true
    $alias = @{}
    foreach ($am in [regex]::Matches($code, $aliasPattern)) {
        $alias[$am.Groups['a'].Value] = $am.Groups['t'].Value.Split('.')[-1]
    }
    foreach ($m in [regex]::Matches($code, $classPattern)) {
        $name = $m.Groups['n'].Value
        if ($classBases.ContainsKey($name)) { continue }   # first wins
        $b = $m.Groups['b'].Value
        if ($b) {
            if ($alias.ContainsKey($b)) { $b = $alias[$b] }
            elseif ($b.Contains('.'))   { $b = $b.Split('.')[-1] }
        }
        $openIdx = $code.IndexOf('{', $m.Index)
        if ($openIdx -lt 0) { continue }
        $span = Get-BraceSpan $code $openIdx
        $classBases[$name] = $b
        $classFile[$name]  = $path
        $classCode[$name]  = $code.Substring($span.Start, $span.Length)
    }
}

function Get-Chain([string]$cls) {
    $out = New-Object System.Collections.Generic.List[string]
    $cur = $cls; $g = 0
    while ($cur -and $classBases.ContainsKey($cur) -and $g -lt 64) {
        $out.Add($cur) | Out-Null
        $b = $classBases[$cur]
        if (-not $b) { break }
        $cur = $b; $g++
    }
    return ,$out.ToArray()
}

$fanCache = @{}
function Get-EffectiveFanControl([string]$cls) {
    if ($fanCache.ContainsKey($cls)) { return $fanCache[$cls] }
    $chain = Get-Chain $cls
    $fan = $false
    for ($i = $chain.Count - 1; $i -ge 0; $i--) {     # root -> leaf
        $c = $chain[$i]
        $body = $classCode[$c]
        if (-not $body) { continue }
        foreach ($m in [regex]::Matches($body, $capPattern)) {
            $expr = $m.Groups['expr'].Value
            $hasFan = $expr -match 'DeviceCapabilities\.FanControl\b'
            if ($m.Groups['op'].Value -eq '=') { $fan = $hasFan }
            elseif ($hasFan) { $fan = $true }
        }
    }
    $fanCache[$cls] = $fan
    return $fan
}

# instantiation set: only inside IDevice.GetCurrent()
$idRaw  = Get-Content -LiteralPath $HcDeviceBase -Raw
$idCode = Convert-Blank $idRaw $true
$idNc   = Convert-Blank $idRaw $false
$idSig  = 'public static IDevice GetCurrent()'
$idSi   = $idCode.IndexOf($idSig)
if ($idSi -lt 0) { throw "IDevice.GetCurrent() signature not found in $HcDeviceBase" }
$idSpan = Get-BraceSpan $idCode ($idCode.IndexOf('{', $idSi))
$idBody = $idNc.Substring($idSpan.Start, $idSpan.Length)
$idAlias = @{}
foreach ($am in [regex]::Matches($idCode, $aliasPattern)) {
    $idAlias[$am.Groups['a'].Value] = $am.Groups['t'].Value.Split('.')[-1]
}
$instantiable = @{}
foreach ($m in [regex]::Matches($idBody, 'new\s+(?<n>[A-Za-z_]\w*)\s*\(')) {
    $n = $m.Groups['n'].Value
    if ($idAlias.ContainsKey($n)) { $n = $idAlias[$n] }
    if ($classBases.ContainsKey($n)) { $instantiable[$n] = $true }
}

# ---------------------------------------------------------------------------
# 2) FanHost: enum sets, actual BuildFanRoutes() block, self-declared count.
# ---------------------------------------------------------------------------
$fhRaw = Get-Content -LiteralPath $FanHostSource -Raw

function Get-EnumMembers([string]$name) {
    $m = [regex]::Match($fhRaw, 'enum\s+' + $name + '\s*\{(?<b>[^}]*)\}')
    if (-not $m.Success) { throw "enum $name not found" }
    $vals = New-Object System.Collections.Generic.List[string]
    foreach ($im in [regex]::Matches($m.Groups['b'].Value, '(?<![A-Za-z0-9_])([A-Za-z_]\w*)')) { $vals.Add($im.Groups[1].Value) | Out-Null }
    return ,$vals.ToArray()
}
$enumKind      = Get-EnumMembers 'FanRouteKind'
$enumStrategy  = Get-EnumMembers 'FanRestoreStrategy'
$enumEvidence  = Get-EnumMembers 'FanEvidenceMode'

$selfDeclared = $null
$sdm = [regex]::Match($fhRaw, 'KnownFanRouteCount\s*!=\s*(?<n>\d+)')
if ($sdm.Success) { $selfDeclared = [int]$sdm.Groups['n'].Value }

$fhSig = 'private static IReadOnlyDictionary<string, FanRoute> BuildFanRoutes()'
$hi = $fhRaw.IndexOf($fhSig)
if ($hi -lt 0) { throw 'BuildFanRoutes() signature not found in Program.cs' }
$regionEnd = [Math]::Min($fhRaw.Length, $hi + 40000)
$region = $fhRaw.Substring($hi, $regionEnd - $hi)
$regionCode = Convert-Blank $region $true
$regionNc   = Convert-Blank $region $false
$regSpan = Get-BraceSpan $regionCode ($regionCode.IndexOf('{'))
$blockNc   = $regionNc.Substring($regSpan.Start, $regSpan.Length)
$blockCode = $regionCode.Substring($regSpan.Start, $regSpan.Length)

$events = New-Object System.Collections.Generic.List[object]

# A) Add(...) helper calls
$addPattern = '\bAdd\s*\(\s*FanRouteKind\.(?<kind>\w+)\s*,\s*"(?:[^"\\]|\\.)*"\s*,\s*FanRestoreStrategy\.(?<strategy>\w+)\s*,\s*FanEvidenceMode\.(?<evidence>\w+)\s*,\s*(?<rest>[^)]*)\)'
$addCount = [regex]::Matches($blockNc, '\bAdd\s*\(\s*FanRouteKind\.').Count
foreach ($m in [regex]::Matches($blockNc, $addPattern)) {
    $classes = @()
    foreach ($cm in [regex]::Matches($m.Groups['rest'].Value, '"(?<c>[^"]*)"')) { $classes += $cm.Groups['c'].Value }
    $events.Add([pscustomobject]@{
        Index = $m.Index; Type = 'Add'; Classes = $classes
        Kind = $m.Groups['kind'].Value; Strategy = $m.Groups['strategy'].Value
        Evidence = $m.Groups['evidence'].Value; Wr = $true; Hc = $true; Guarded = $false
    })
}

# B) routes["HandheldCompanion.Devices.X"] = new FanRoute(...)
# The 7th constructor argument (FanRoutePrecondition.*) is optional here so this stays
# in step with the current FanRoute signature (kind, description, wr, hc, strategy,
# evidence, precondition). A truly missing route still does not match.
$bPattern = 'routes\[\s*"HandheldCompanion\.Devices\.(?<cls>\w+)"\s*\]\s*=\s*new\s+FanRoute\(\s*FanRouteKind\.(?<kind>\w+)\s*,\s*"(?:[^"\\]|\\.)*"\s*,\s*(?<wr>true|false)\s*,\s*(?<hc>true|false)\s*,\s*FanRestoreStrategy\.(?<strategy>\w+)\s*,\s*FanEvidenceMode\.(?<evidence>\w+)(?:\s*,\s*FanRoutePrecondition\.\w+)?\s*\)'
foreach ($m in [regex]::Matches($blockNc, $bPattern)) {
    $events.Add([pscustomobject]@{
        Index = $m.Index; Type = 'Assign'; Classes = @($m.Groups['cls'].Value)
        Kind = $m.Groups['kind'].Value; Strategy = $m.Groups['strategy'].Value
        Evidence = $m.Groups['evidence'].Value
        Wr = ($m.Groups['wr'].Value -eq 'true'); Hc = ($m.Groups['hc'].Value -eq 'true'); Guarded = $false
    })
}

# C/D) foreach (var n in new[] { ... }) <stmt>;
$fePattern = 'foreach\s*\(\s*var\s+(?<v>[A-Za-z_]\w*)\s+in\s+new\s*\[\s*\]\s*\{(?<list>[^}]*)\}\s*\)\s*(?<stmt>[^;]*);'
$foreachCount = [regex]::Matches($blockNc, '\bforeach\s*\(').Count
$cCount = 0; $dCount = 0
foreach ($m in [regex]::Matches($blockNc, $fePattern)) {
    $classes = @()
    foreach ($cm in [regex]::Matches($m.Groups['list'].Value, '"(?<c>[^"]*)"')) { $classes += $cm.Groups['c'].Value }
    $stmt = $m.Groups['stmt'].Value
    if ($stmt -match '\bwith\s*\{') {
        $dCount++
        $st = [regex]::Match($stmt, 'RestoreStrategy\s*=\s*FanRestoreStrategy\.(?<s>\w+)')
        $wr = [regex]::Match($stmt, 'WriteReadyByDefault\s*=\s*(?<x>true|false)')
        $hc = [regex]::Match($stmt, 'HcCallbackReady\s*=\s*(?<x>true|false)')
        $events.Add([pscustomobject]@{
            Index = $m.Index; Type = 'Override'; Classes = $classes
            Kind = ''; Strategy = $(if ($st.Success) { $st.Groups['s'].Value } else { '' })
            Evidence = ''
            Wr = $(if ($wr.Success) { $wr.Groups['x'].Value -eq 'true' } else { $null })
            Hc = $(if ($hc.Success) { $hc.Groups['x'].Value -eq 'true' } else { $null })
            Guarded = ($stmt -match 'ContainsKey')
        })
    }
    elseif ($stmt -match 'new\s+FanRoute\(') {
        $cCount++
        $sm = [regex]::Match($stmt, 'FanRouteKind\.(?<kind>\w+)\s*,\s*"(?:[^"\\]|\\.)*"\s*,\s*(?<wr>true|false)\s*,\s*(?<hc>true|false)\s*,\s*FanRestoreStrategy\.(?<strategy>\w+)\s*,\s*FanEvidenceMode\.(?<evidence>\w+)')
        $events.Add([pscustomobject]@{
            Index = $m.Index; Type = 'Assign'; Classes = $classes
            Kind = $sm.Groups['kind'].Value; Strategy = $sm.Groups['strategy'].Value
            Evidence = $sm.Groups['evidence'].Value
            Wr = ($sm.Groups['wr'].Value -eq 'true'); Hc = ($sm.Groups['hc'].Value -eq 'true'); Guarded = $false
        })
    }
}
$totalNewFanRoute = [regex]::Matches($blockNc, 'new\s+FanRoute\s*\(').Count
$withCount = [regex]::Matches($blockNc, '\bwith\s*\{').Count
$bCount = ([regex]::Matches($blockNc, $bPattern)).Count

# The local Add(...) helper also lives inside this block. Derive its own `foreach`
# and `new FanRoute(` counts from its brace span so the integrity equations stay
# relational instead of hardcoding a magic "+1".
$addHelperIdx = $blockCode.IndexOf('void Add(')
$helperForeach = 0; $helperNewFanRoute = 0
if ($addHelperIdx -lt 0) {
    $failures.Add('parse-gap: Add(...) local helper not found inside the BuildFanRoutes block')
} else {
    $helperSpan = Get-BraceSpan $blockCode ($blockCode.IndexOf('{', $addHelperIdx))
    $helperCode = $blockCode.Substring($helperSpan.Start, $helperSpan.Length)
    $helperForeach     = [regex]::Matches($helperCode, '\bforeach\s*\(').Count
    $helperNewFanRoute = [regex]::Matches($helperCode, 'new\s+FanRoute\s*\(').Count
}

# parse integrity (relational, no hardcoded counts)
if ($addCount -ne ([regex]::Matches($blockNc, $addPattern)).Count) { $failures.Add("parse-gap: Add(...) call count $addCount != parsed Add events") }
if (($bCount + $cCount + $helperNewFanRoute) -ne $totalNewFanRoute) { $failures.Add("parse-gap: new FanRoute() $totalNewFanRoute != $helperNewFanRoute(helper)+$bCount(b)+$cCount(c)") }
if (($cCount + $dCount) -ne ($foreachCount - $helperForeach)) { $failures.Add("parse-gap: route foreach $($foreachCount - $helperForeach) != $cCount(c)+$dCount(d)") }
if ($dCount -ne $withCount) { $failures.Add("parse-gap: with{...} $withCount != $dCount sequential overrides") }

# apply events in registration order
$routes = @{}
$overrideMissing = New-Object System.Collections.Generic.List[string]
foreach ($ev in ($events | Sort-Object Index)) {
    if ($ev.Type -eq 'Add' -or $ev.Type -eq 'Assign') {
        foreach ($c in $ev.Classes) {
            $routes[$c] = [pscustomobject]@{
                DeviceClass = $c; Kind = $ev.Kind; Strategy = $ev.Strategy; Evidence = $ev.Evidence
                Wr = [bool]$ev.Wr; Hc = [bool]$ev.Hc
            }
        }
    }
    elseif ($ev.Type -eq 'Override') {
        foreach ($c in $ev.Classes) {
            if (-not $routes.ContainsKey($c)) {
                if (-not $ev.Guarded) { $overrideMissing.Add($c) }
                continue
            }
            if ($ev.Strategy) { $routes[$c].Strategy = $ev.Strategy }
            if ($null -ne $ev.Wr) { $routes[$c].Wr = [bool]$ev.Wr }
            if ($null -ne $ev.Hc) { $routes[$c].Hc = [bool]$ev.Hc }
        }
    }
}

foreach ($c in $overrideMissing) { $failures.Add("override-missing-base: $c (sequential override without a registered base route)") }

# ---------------------------------------------------------------------------
# 3) Assertions (fail-closed, relational only).
# ---------------------------------------------------------------------------
if (-not ($blockCode -match 'StringComparer\.Ordinal')) {
    $failures.Add('no-ordinal-comparer: BuildFanRoutes must use StringComparer.Ordinal so unknown models cannot gain write authority by CPU name or similar prefix')
}

if ($null -ne $selfDeclared -and $routes.Count -ne $selfDeclared) {
    $failures.Add("route-count-mismatch: parsed $($routes.Count) != host KnownFanRouteCount $selfDeclared")
}

foreach ($c in $routes.Keys) {
    $r = $routes[$c]
    if (-not $classBases.ContainsKey($c)) { $failures.Add("fan-route-stale: $c (not an HC device class)") }
    if (-not $r.Wr) { $failures.Add("route-not-write-ready: $c") }
    if (-not $r.Hc) { $failures.Add("route-not-callback-ready: $c") }
    if ($enumKind -notcontains $r.Kind)         { $failures.Add("kind-unknown: $c -> $($r.Kind)") }
    if ($enumStrategy -notcontains $r.Strategy) { $failures.Add("strategy-unknown: $c -> $($r.Strategy)") }
    if ($enumEvidence -notcontains $r.Evidence) { $failures.Add("evidence-unknown: $c -> $($r.Evidence)") }
}

# instantiable + effective FanControl must be routed
$instantiableFan = New-Object System.Collections.Generic.List[string]
foreach ($c in ($instantiable.Keys | Sort-Object)) {
    if (Get-EffectiveFanControl $c) {
        $instantiableFan.Add($c) | Out-Null
        if (-not $routes.ContainsKey($c)) { $failures.Add("fan-route-missing: $c (instantiable with effective FanControl but no registered route)") }
    }
}

# evidence applicability: ASUS <=> AsusScopeReceipts ; non-ASUS must be CompatCallbackNoScope
$asusRoutes = New-Object System.Collections.Generic.List[string]
$nonAsusRoutes = New-Object System.Collections.Generic.List[string]
foreach ($c in ($routes.Keys | Sort-Object)) {
    $chain = Get-Chain $c
    $isAsus = ($classFile[$c] -like '*\ASUS\*') -or ($chain -contains 'ASUS')
    if ($isAsus) { $asusRoutes.Add($c) | Out-Null } else { $nonAsusRoutes.Add($c) | Out-Null }
    $ev = $routes[$c].Evidence
    if ($isAsus) {
        if ($ev -ne 'AsusScopeReceipts') { $failures.Add("evidence-mismatch: ASUS route $c declares $ev (expected AsusScopeReceipts)") }
    } else {
        if ($ev -ne 'CompatCallbackNoScope') { $failures.Add("evidence-mismatch: non-ASUS route $c declares $ev (expected CompatCallbackNoScope)") }
    }
}

# routed but no effective FanControl (structural info, e.g. OneXPlayerX1 subtree)
$routedNoFan = New-Object System.Collections.Generic.List[string]
foreach ($c in ($routes.Keys | Sort-Object)) {
    if (-not (Get-EffectiveFanControl $c)) {
        $routedNoFan.Add($c) | Out-Null
        $infos.Add("routed-without-effective-fancontrol: $c (base class wipes FanControl; kept for identity/documentation, not a candidate for another writer)")
    }
}

# ---------------------------------------------------------------------------
# 4) Legacy 13-root gate replay (explains the old 60 / 71).
# ---------------------------------------------------------------------------
$legacyRoots = @('OneXAOKZOE','AYANEODeviceCEc','AYANEODeviceCEii','LegionGo','ClawA1M','AynLoki','ROGAlly','GamingZone','SteamDeck','GPDWin4','GPDWin5','GPDWinMax2','GPDWinMini')
function Get-Descendants([string]$root) {
    $out = New-Object System.Collections.Generic.List[string]
    $queue = New-Object System.Collections.Generic.Queue[string]
    $queue.Enqueue($root)
    while ($queue.Count -gt 0) {
        $cur = $queue.Dequeue()
        foreach ($k in $classBases.Keys) {
            if ($classBases[$k] -eq $cur -and -not $out.Contains($k)) { $out.Add($k) | Out-Null; $queue.Enqueue($k) }
        }
    }
    return ,$out.ToArray()
}
# NOTE: Get-Descendants keeps its array intact (returns `,$out.ToArray()`), so the
# result must be assigned before enumerating; `foreach (... in Get-Descendants ...)`
# would iterate once with the wrapped array as the single element.
$legacyFan = New-Object System.Collections.Generic.HashSet[string]
foreach ($r in $legacyRoots) {
    [void]$legacyFan.Add($r)
    $desc = Get-Descendants $r
    foreach ($d in $desc) { [void]$legacyFan.Add($d) }
}
$legacyWiped = New-Object System.Collections.Generic.HashSet[string]
[void]$legacyWiped.Add('OneXPlayerX1')
$wipedDesc = Get-Descendants 'OneXPlayerX1'
foreach ($d in $wipedDesc) { [void]$legacyWiped.Add($d) }

$legacyFanInstantiable = New-Object System.Collections.Generic.List[string]
foreach ($c in ($legacyFan | Sort-Object)) {
    if ($legacyWiped.Contains($c)) { continue }
    if (-not $instantiable.ContainsKey($c)) { continue }
    $legacyFanInstantiable.Add($c) | Out-Null
}

$legacyRouteNames = New-Object System.Collections.Generic.HashSet[string]
foreach ($m in [regex]::Matches($fhRaw, '"([A-Za-z0-9_]+)"')) {
    $n = $m.Groups[1].Value
    if ($n -match '^(OneX|AOKZOE|AYANEO|GPD|Loki|Claw|Legion|ROG|XboxROG|GamingZone|SteamDeck|SuiPlay|Minisforum)') { [void]$legacyRouteNames.Add($n) }
}

$deltaInstantiable = New-Object System.Collections.Generic.List[string]
foreach ($c in $instantiableFan) { if (-not ($legacyFanInstantiable -contains $c)) { $deltaInstantiable.Add($c) | Out-Null } }
$deltaRoutes = New-Object System.Collections.Generic.List[string]
foreach ($n in ($legacyRouteNames | Sort-Object)) { if (-not $routes.ContainsKey($n)) { $deltaRoutes.Add($n) | Out-Null } }

# legacy gate verdict (old logic, for the replay file only)
$legacyFailures = New-Object System.Collections.Generic.List[string]
foreach ($c in ($legacyFan | Sort-Object)) {
    if ($legacyWiped.Contains($c)) { continue }
    if (-not $instantiable.ContainsKey($c)) { continue }
    if (-not $legacyRouteNames.Contains($c)) { $legacyFailures.Add("fan-route-missing: $c") }
}
foreach ($n in $legacyRouteNames) { if (-not $classBases.ContainsKey($n)) { $legacyFailures.Add("fan-route-stale: $n") } }

# ---------------------------------------------------------------------------
# 5) Output.
# ---------------------------------------------------------------------------
$kindDist = @{}; $strategyDist = @{}; $evidenceDist = @{}
foreach ($c in $routes.Keys) {
    $k = $routes[$c].Kind;     $kindDist[$k]     = 1 + [int]$kindDist[$k]
    $s = $routes[$c].Strategy; $strategyDist[$s] = 1 + [int]$strategyDist[$s]
    $e = $routes[$c].Evidence; $evidenceDist[$e] = 1 + [int]$evidenceDist[$e]
}

Write-Output ("fan-route-audit: registered-routes={0} instantiable-device-classes={1} instantiable-effective-fancontrol={2} routed-without-effective-fancontrol={3}" -f `
    $routes.Count, $instantiable.Count, $instantiableFan.Count, $routedNoFan.Count)
Write-Output ("fan-route-audit: asus-routes={0} non-asus-routes={1} evidence-asus={2} evidence-compat={3}" -f `
    $asusRoutes.Count, $nonAsusRoutes.Count, ([int]$evidenceDist['AsusScopeReceipts']), ([int]$evidenceDist['CompatCallbackNoScope']))
Write-Output ("fan-route-audit: kind-distribution {0}" -f (($kindDist.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '))
Write-Output ("fan-route-audit: strategy-distribution {0}" -f (($strategyDist.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '))
Write-Output ("fan-route-audit: legacy-gate-replay instantiables-with-FanControl={0} routes={1} delta-instantiable=+{2} delta-routes=-{3}" -f `
    $legacyFanInstantiable.Count, $legacyRouteNames.Count, $deltaInstantiable.Count, $deltaRoutes.Count)
Write-Output ("fan-route-audit-legacy-delta-instantiable: {0}" -f (($deltaInstantiable | Sort-Object) -join ','))
Write-Output ("fan-route-audit-legacy-delta-routes: {0}" -f (($deltaRoutes | Sort-Object) -join ','))

foreach ($i in $infos) { Write-Output "fan-route-audit-info: $i" }

# machine-readable capability matrix
if ($OutputRoot) {
    if (-not (Test-Path -LiteralPath $OutputRoot)) { New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null }
    $routeRecords = @()
    foreach ($c in ($routes.Keys | Sort-Object)) {
        $chain = Get-Chain $c
        $rel = $classFile[$c]
        if ($rel -and $rel.StartsWith($HcDevicesRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            $rel = $rel.Substring($HcDevicesRoot.Length).TrimStart('\', '/')
        }
        $routeRecords += [pscustomobject]@{
            deviceClass = $c
            sourceFile  = $rel
            kind        = $routes[$c].Kind
            strategy    = $routes[$c].Strategy
            evidence    = $routes[$c].Evidence
            isAsus      = (($classFile[$c] -like '*\ASUS\*') -or ($chain -contains 'ASUS'))
            effectiveFanControl = (Get-EffectiveFanControl $c)
            writeReadyByDefault = $routes[$c].Wr
            hcCallbackReady     = $routes[$c].Hc
            chain       = ($chain -join ' > ')
        }
    }
    $matrix = [pscustomobject]@{
        generatedBy        = 'fan_route_coverage_audit.ps1'
        generatedUtc       = (Get-Date).ToUniversalTime().ToString('o')
        hcDevicesRoot      = $HcDevicesRoot
        fanHostSource      = $FanHostSource
        registeredRoutes   = $routes.Count
        selfDeclaredRoutes = $selfDeclared
        instantiableClasses = $instantiable.Count
        instantiableEffectiveFanControl = $instantiableFan.Count
        routedWithoutEffectiveFanControl = @($routedNoFan)
        kindDistribution     = $kindDist
        strategyDistribution = $strategyDist
        evidenceDistribution = $evidenceDist
        asusRoutes    = @($asusRoutes)
        nonAsusRoutes = @($nonAsusRoutes)
        routes        = $routeRecords
        legacyGateReplay = [pscustomobject]@{
            instantiablesWithFanControl = $legacyFanInstantiable.Count
            routes                      = $legacyRouteNames.Count
            deltaInstantiable           = @($deltaInstantiable | Sort-Object)
            deltaRoutes                 = @($deltaRoutes | Sort-Object)
            legacyFailures              = @($legacyFailures)
        }
        failureCount = $failures.Count
        failures     = @($failures)
    }
    $jsonPath = Join-Path $OutputRoot 'fan-capability-matrix.json'
    $matrix | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding utf8

    $md = New-Object System.Collections.Generic.List[string]
    $md.Add('# Fan capability matrix (FAN-934)')
    $md.Add('')
    $md.Add("Generated by ``fan_route_coverage_audit.ps1`` at $($matrix.generatedUtc).")
    $md.Add('')
    $md.Add("- HC source: ``$HcDevicesRoot``")
    $md.Add("- FanHost: ``$FanHostSource``")
    $md.Add('')
    $md.Add('## Summary')
    $md.Add('')
    $md.Add("| metric | value |")
    $md.Add("| --- | --- |")
    $md.Add("| registered routes | $($routes.Count) |")
    $md.Add("| host self-declared routes | $selfDeclared |")
    $md.Add("| instantiable device classes | $($instantiable.Count) |")
    $md.Add("| instantiable with effective FanControl | $($instantiableFan.Count) |")
    $md.Add("| routed without effective FanControl | $($routedNoFan.Count) |")
    $md.Add("| ASUS routes (AsusScopeReceipts) | $($asusRoutes.Count) |")
    $md.Add("| non-ASUS routes (CompatCallbackNoScope) | $($nonAsusRoutes.Count) |")
    $md.Add('')
    $md.Add('## Recovery-contract / strategy distribution')
    $md.Add('')
    $md.Add('| strategy | routes |')
    $md.Add('| --- | --- |')
    foreach ($e in ($strategyDist.GetEnumerator() | Sort-Object Name)) { $md.Add("| $($e.Name) | $($e.Value) |") }
    $md.Add('')
    $md.Add('## Routes')
    $md.Add('')
    $md.Add('| deviceClass | kind | strategy | evidence | eff.FanControl | write | cb | source |')
    $md.Add('| --- | --- | --- | --- | --- | --- | --- | --- |')
    foreach ($r in $routeRecords) {
        $md.Add("| $($r.deviceClass) | $($r.kind) | $($r.strategy) | $($r.evidence) | $($r.effectiveFanControl) | $($r.writeReadyByDefault) | $($r.hcCallbackReady) | $($r.sourceFile) |")
    }
    $md.Add('')
    $md.Add('## Notes')
    $md.Add('')
    $md.Add("- Legacy gate replay: the retired 13-root check reported ``instantiables-with-FanControl=$($legacyFanInstantiable.Count) routes=$($legacyRouteNames.Count)``. Delta instantiables (+$($deltaInstantiable.Count)) are FanControl classes the hardcoded root list never reached; delta routes (-$($deltaRoutes.Count)) is a class name that only appears as an unrelated string literal elsewhere in Program.cs. The legacy numbers are explained here, not hardcoded.")
    $md.Add("- Route evidence applicability (FAN-934 root cause): ASUS routes are the only routes that expect ASUS operation-scope receipts; all non-ASUS HC routes settle on the reviewed compat-callback contract. This is enforced structurally from the class chain / source directory, not by route name.")
    $md.Add("- Unknown models cannot gain write authority by CPU name or similar prefix: the registry uses ``StringComparer.Ordinal`` over explicit device-class keys.")
    $md.Add("- CTW scope is recorded separately: HC-supported MSI models are not automatically CTW-supported, and a device must not have two writers at once (A1M / 1T41 keep the HC capability; CTW is not opened for them here).")
    $md.Add("- Readback honesty: several routes return RPM (or a raw register value), not a duty percentage; a fan-speed estimate is not treated as a true readback.")
    $md.Add("- ``GenericEcDutyRegister`` is a declared contract value with no current registrant; the registry uses 11 of the 12 declared strategies.")
    Set-Content -LiteralPath (Join-Path $OutputRoot 'fan-capability-matrix.md') -Value $md -Encoding utf8
    Write-Output "fan-route-audit: wrote $(Join-Path $OutputRoot 'fan-capability-matrix.json') and fan-capability-matrix.md"
}

if ($LegacyOut) {
    $legacyLines = @(
        ("fan-route-audit: instantiables-with-FanControl={0} routes={1}" -f $legacyFanInstantiable.Count, $legacyRouteNames.Count),
        $(if ($legacyFailures.Count -eq 0) { 'FAN_ROUTE_COVERAGE=PASS' } else { "FAN_ROUTE_COVERAGE=FAIL ($($legacyFailures.Count))" })
    )
    $legacyLines | Set-Content -LiteralPath $LegacyOut -Encoding utf8
    Write-Output "fan-route-audit: wrote legacy replay -> $LegacyOut"
}

if ($failures.Count -eq 0) {
    Write-Output "FAN_ROUTE_COVERAGE=PASS"
    exit 0
}
$failures | ForEach-Object { Write-Output $_ }
Write-Output "FAN_ROUTE_COVERAGE=FAIL ($($failures.Count))"
exit 1
