<#
.SYNOPSIS
  T2 read-only deep audit of every HC fan route and YeMan Fan Host mapping.

  This script reads frozen HC source and Fan Host source only. It does not
  load HC, invoke WMI/ACPI/HID/EC, write hardware, or edit production code.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcDevicesRoot,
  [Parameter(Mandatory = $true)][string]$FactorySource,
  [Parameter(Mandatory = $true)][string]$HostSource,
  [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($HcDevicesRoot, $FactorySource, $HostSource)) {
  if (!(Test-Path -LiteralPath $path)) { throw "Required path is missing: $path" }
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$HcRoot = Split-Path -Parent $HcDevicesRoot
$factoryPath = $FactorySource
$hostPath = $HostSource

function Strip-CommentsPreserveLength([string]$text) {
  $value = [regex]::Replace($text, '(?s)/\*.*?\*/', {
      param($m)
      ($m.Value -replace '[^\r\n]', ' ')
    })
  return [regex]::Replace($value, '(?m)//[^\r\n]*', {
      param($m)
      ($m.Value -replace '[^\r\n]', ' ')
    })
}

function Get-LineNumber([string]$text, [int]$offset) {
  return 1 + ([regex]::Matches($text.Substring(0, [Math]::Max(0, $offset)), "`n").Count)
}

function Get-Sha256([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
  } finally { $sha.Dispose() }
}

function Get-Sha256([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::OpenRead($path)
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '') }
  finally { $stream.Dispose(); $sha.Dispose() }
}

function Get-Methods([string]$source) {
  $names = @()
  foreach ($name in @('SetFanControl', 'SetFanDuty', 'PowerProfileManager_Applied', 'Close')) {
    if ($source -match ("(?m)\b(?:public|internal|private|protected)?\s*(?:override\s+|virtual\s+)?(?:async\s+)?(?:void|bool|double|float|int|Task)\s+" + $name + '\s*\(')) {
      $names += $name
    }
  }
  return @($names | Sort-Object -Unique)
}

$classMap = @{}
$fanDeclarations = @{}
$typeAliases = @{}
$sourceRecords = @()
# Resolve inheritance from the whole frozen HC source tree.  AsusDevice and a
# few shared base types are outside Devices\, so limiting this map to the
# device folder creates false missing-owner findings.
foreach ($file in Get-ChildItem -LiteralPath $HcRoot -Recurse -Filter '*.cs' -File) {
  $raw = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  $source = Strip-CommentsPreserveLength $raw
  foreach ($aliasMatch in [regex]::Matches($source, '(?m)^\s*using\s+(?<alias>\w+)\s*=\s*(?:[\w\.]+\.)?(?<target>\w+)\s*;')) {
    $typeAliases[$aliasMatch.Groups['alias'].Value] = $aliasMatch.Groups['target'].Value
  }
  $matches = @([regex]::Matches($source, '(?m)\b(?:public|internal|private|protected)?\s*(?:abstract|partial)?\s*class\s+(?<name>\w+)(?:\s*:\s*(?<base>[\w\.]+))?'))
  for ($i = 0; $i -lt $matches.Count; $i++) {
    $match = $matches[$i]
    $name = $match.Groups['name'].Value
    $base = if ($match.Groups['base'].Success) { ($match.Groups['base'].Value -split '\.')[-1] } else { '' }
    if ($base -and $typeAliases.ContainsKey($base)) { $base = $typeAliases[$base] }
    $end = if ($i + 1 -lt $matches.Count) { $matches[$i + 1].Index } else { $source.Length }
    $body = $source.Substring($match.Index, $end - $match.Index)
    $record = [pscustomobject]@{
      Name = $name
      Base = $base
      File = $file.FullName
      Line = Get-LineNumber $raw $match.Index
      Source = $body
      Methods = @(Get-Methods $body)
      FanDeclared = [bool]($body -match 'Capabilities\s*(?:\|=|=)\s*DeviceCapabilities\.FanControl')
    }
    $classMap[$name] = $record
    if ($record.FanDeclared) { $fanDeclarations[$name] = $true }
    $sourceRecords += $record
  }
}

function Test-FanClass([string]$name) {
  $seen = @{}
  while ($name -and !$seen.ContainsKey($name)) {
    $seen[$name] = $true
    if ($fanDeclarations.ContainsKey($name)) { return $true }
    if (!$classMap.ContainsKey($name)) { break }
    $name = $classMap[$name].Base
  }
  return $false
}

function Resolve-Owner([string]$name, [string]$method) {
  $seen = @()
  while ($name -and ($seen -notcontains $name)) {
    $seen += $name
    if (!$classMap.ContainsKey($name)) { return "<missing:$name>" }
    if ($classMap[$name].Methods -contains $method) { return $name }
    $name = $classMap[$name].Base
  }
  return '<unresolved>'
}

function Get-Chain([string]$name) {
  $result = @()
  $seen = @{}
  while ($name -and !$seen.ContainsKey($name)) {
    $seen[$name] = $true
    $result += $name
    if (!$classMap.ContainsKey($name)) { $result += "<missing:$name>"; break }
    $name = $classMap[$name].Base
  }
  return @($result)
}

$factoryRaw = Get-Content -LiteralPath $factoryPath -Raw -Encoding UTF8
$factorySource = Strip-CommentsPreserveLength $factoryRaw
$factoryConstructors = @([regex]::Matches($factorySource, '\bnew\s+(?<name>[A-Za-z_]\w*)\s*\(') | ForEach-Object { $_.Groups['name'].Value } | Sort-Object -Unique)
$factoryFanClasses = @($factoryConstructors | Where-Object { Test-FanClass $_ })

$hostRaw = Get-Content -LiteralPath $hostPath -Raw -Encoding UTF8
$hostSource = Strip-CommentsPreserveLength $hostRaw
$routeStart = $hostSource.IndexOf('private static IReadOnlyDictionary<string, FanRoute> BuildFanRoutes()', [StringComparison]::Ordinal)
$routeEnd = $hostSource.IndexOf('private void LoadAssemblyAndFactory()', $routeStart, [StringComparison]::Ordinal)
if ($routeStart -lt 0 -or $routeEnd -le $routeStart) { throw 'FanRoutes source boundary is missing' }
$routeSource = $hostSource.Substring($routeStart, $routeEnd - $routeStart)

# Parse the route registry from its declarations, rather than using a broad
# character window around each class name.  The old window parser could attach
# a neighbouring route's kind/strategy and report write-ready entries as
# missing metadata.
$registryMetadata = @{}
function Set-RegistryMetadata([string]$name, [string]$kind, [string]$strategy, [bool]$ready) {
  if (!$name) { return }
  $registryMetadata[$name] = [pscustomobject]@{
    Kind = $kind
    Strategy = $strategy
    WriteReady = $ready
  }
}

$addPattern = '(?s)Add\(FanRouteKind\.(?<kind>\w+),.*?FanRestoreStrategy\.(?<strategy>\w+),\s*(?<classes>(?:"[A-Za-z_]\w*"\s*,?\s*)+)\);'
foreach ($m in [regex]::Matches($routeSource, $addPattern)) {
  $names = [regex]::Matches($m.Groups['classes'].Value, '"(?<name>[A-Za-z_]\w*)"')
  foreach ($n in $names) {
    Set-RegistryMetadata $n.Groups['name'].Value $m.Groups['kind'].Value $m.Groups['strategy'].Value $true
  }
}

$directPattern = '(?s)routes\["HandheldCompanion\.Devices\.(?<name>[A-Za-z_]\w*)"\]\s*=\s*new\s+FanRoute\(FanRouteKind\.(?<kind>\w+),.*?\btrue\s*,\s*true\s*,\s*FanRestoreStrategy\.(?<strategy>\w+)'
foreach ($m in [regex]::Matches($routeSource, $directPattern)) {
  Set-RegistryMetadata $m.Groups['name'].Value $m.Groups['kind'].Value $m.Groups['strategy'].Value $true
}

$foreachPattern = '(?s)foreach\s*\(var\s+name\s+in\s+new\[\]\s*\{(?<classes>[^}]*)\}\)\s*\{?\s*routes\[\$"HandheldCompanion\.Devices\.\{name\}"\]\s*=\s*new\s+FanRoute\(FanRouteKind\.(?<kind>\w+),.*?\btrue\s*,\s*true\s*,\s*FanRestoreStrategy\.(?<strategy>\w+)'
foreach ($m in [regex]::Matches($routeSource, $foreachPattern)) {
  $names = [regex]::Matches($m.Groups['classes'].Value, '"(?<name>[A-Za-z_]\w*)"')
  foreach ($n in $names) {
    Set-RegistryMetadata $n.Groups['name'].Value $m.Groups['kind'].Value $m.Groups['strategy'].Value $true
  }
}

# Preserve strategy-only overrides written with an interpolated foreach key,
# for example the Lenovo block that changes the initial MSI family strategy
# to LenovoHcDefaultTable without constructing a new FanRoute.
$foreachOverridePattern = '(?s)foreach\s*\(var\s+name\s+in\s+new\[\]\s*\{(?<classes>[^}]*)\}\)\s*\{?\s*routes\[\$"HandheldCompanion\.Devices\.\{name\}"\]\s*=\s*routes\[\$"HandheldCompanion\.Devices\.\{name\}"\]\s*with\s*\{\s*RestoreStrategy\s*=\s*FanRestoreStrategy\.(?<strategy>\w+)'
foreach ($m in [regex]::Matches($routeSource, $foreachOverridePattern)) {
  $names = [regex]::Matches($m.Groups['classes'].Value, '"(?<name>[A-Za-z_]\w*)"')
  foreach ($n in $names) {
    $name = $n.Groups['name'].Value
    if ($registryMetadata.ContainsKey($name)) {
      $old = $registryMetadata[$name]
      $registryMetadata[$name] = [pscustomobject]@{ Kind = $old.Kind; Strategy = $m.Groups['strategy'].Value; WriteReady = $true }
    }
  }
}

$overridePattern = 'routes\["HandheldCompanion\.Devices\.(?<name>[A-Za-z_]\w*)"\]\s*=\s*routes\["HandheldCompanion\.Devices\.[A-Za-z_]\w*"\]\s*with\s*\{\s*RestoreStrategy\s*=\s*FanRestoreStrategy\.(?<strategy>\w+)'
foreach ($m in [regex]::Matches($routeSource, $overridePattern)) {
  $name = $m.Groups['name'].Value
  if ($registryMetadata.ContainsKey($name)) {
    $old = $registryMetadata[$name]
    $registryMetadata[$name] = [pscustomobject]@{ Kind = $old.Kind; Strategy = $m.Groups['strategy'].Value; WriteReady = $old.WriteReady }
  }
}

$knownClassSet = @{}
foreach ($name in $factoryFanClasses) { $knownClassSet[$name] = $true }
$routeClasses = @([regex]::Matches($routeSource, '"(?:HandheldCompanion\.Devices\.)?(?<name>[A-Za-z_]\w*)"') |
  ForEach-Object { $_.Groups['name'].Value } | Where-Object { $knownClassSet.ContainsKey($_) } | Sort-Object -Unique)

function Get-NearbyMetadata([string]$name) {
  if ($registryMetadata.ContainsKey($name)) { return $registryMetadata[$name] }
  return [pscustomobject]@{ Kind = ''; Strategy = ''; WriteReady = $false }
}

$routeRows = @()
foreach ($name in $routeClasses) {
  $meta = Get-NearbyMetadata $name
  $chain = Get-Chain $name
  $close = Resolve-Owner $name 'Close'
  $control = Resolve-Owner $name 'SetFanControl'
  $duty = Resolve-Owner $name 'SetFanDuty'
  $profile = Resolve-Owner $name 'PowerProfileManager_Applied'
  $owners = @($close, $control, $duty, $profile)
  $unresolved = @($owners | Where-Object { $_ -match '^<' })
  $ownerRecords = @()
  foreach ($ownerName in $owners | Sort-Object -Unique) {
    if ($classMap.ContainsKey($ownerName)) {
      $ownerRecords += [pscustomobject]@{
        Owner = $ownerName
        File = $classMap[$ownerName].File
        Line = $classMap[$ownerName].Line
        Methods = $classMap[$ownerName].Methods
      }
    }
  }
  $routeRows += [pscustomobject]@{
    Route = $name
    BaseChain = $chain
    RegistryKind = $meta.Kind
    RegistryRestoreStrategy = $meta.Strategy
    RegistryWriteReady = $meta.WriteReady
    CloseOwner = $close
    SetFanControlOwner = $control
    SetFanDutyOwner = $duty
    ProfileCallbackOwner = $profile
    UnresolvedOwners = $unresolved
    OwnerEvidence = $ownerRecords
    HardwarePrimitives = @($ownerRecords | ForEach-Object {
      $classMap[$_.Owner].Source | Select-String -Pattern 'ECRamDirectWrite|EcWriteByte|DeviceIoControl|AsusACPI|WMI|WriteMemory|DlPortWrite' -AllMatches | ForEach-Object { $_.Matches.Value }
    } | Sort-Object -Unique)
  }
}

$missing = @($factoryFanClasses | Where-Object { $routeClasses -notcontains $_ })
$unexpected = @($routeClasses | Where-Object { $factoryFanClasses -notcontains $_ })
$unresolvedRows = @($routeRows | Where-Object { $_.UnresolvedOwners.Count -gt 0 })
$notReadyRows = @($routeRows | Where-Object { !$_.RegistryWriteReady })
$profileKindRows = @($routeRows | Where-Object { $_.RegistryKind -eq 'ProfileCurve' })
$customProfileRows = @($routeRows | Where-Object { $_.ProfileCallbackOwner -ne 'IDevice' -and $_.ProfileCallbackOwner -notmatch '^<' })

$fanProfilePath = Join-Path $HcRoot 'Misc\FanProfile.cs'
$fanProfileSource = if (Test-Path -LiteralPath $fanProfilePath) { Strip-CommentsPreserveLength (Get-Content -LiteralPath $fanProfilePath -Raw -Encoding UTF8) } else { '' }
$curveChecks = [ordered]@{
  hcFanProfile11PointCurve = [bool]($fanProfileSource -match 'fanSpeeds\s*=\s*\{[^}]*\}')
  hcFanProfileLinearInterpolation = [bool]($fanProfileSource -match 'Math\.Floor\(temp\s*/\s*10\.0\)' -and $fanProfileSource -match 'Math\.Clamp\(y,\s*0\.0,\s*100\.0\)')
  yeman11PointExpansion = [bool]($hostSource -match 'Enumerable\.Range\(0,\s*11\)\s*\.Select\(temp\s*=>\s*Interpolate\(nodes,\s*temp\s*\*\s*10\)\)')
  yemanHcSetFanSpeed = [bool]($hostSource -match 'Invoke\(fan,\s*"SetFanSpeed",\s*duties\)')
  yemanHcProfileCallback = [bool]($hostSource -match 'Invoke\("PowerProfileManager_Applied"')
  yemanFourNodeMonotonicValidation = [bool]($hostSource -match 'nodes\.Count\s*!=\s*4' -and $hostSource -match 'nodes\[0\]\.TempC\s*!=\s*0' -and $hostSource -match 'DutyPercent\s*<\s*nodes\[i\s*-\s*1\]\.DutyPercent' -and $hostSource -match 'nodes\[3\]\.DutyPercent\s*<\s*50')
}

$semanticMismatches = @($routeRows | Where-Object {
  ($_.RegistryKind -in @('GenericDuty', 'GenericRpmTarget') -and ($_.SetFanDutyOwner -match '^<' -or [string]::IsNullOrWhiteSpace($_.SetFanDutyOwner))) -or
  ($_.RegistryKind -eq 'ProfileCurve' -and ($_.ProfileCallbackOwner -match '^<' -or [string]::IsNullOrWhiteSpace($_.ProfileCallbackOwner)))
})
$expectedStrategies = @{
  AOKZOEA1 = 'OneXAcpiFanMode'; AYANEOAIRPlusAMD = 'AyanEOCEiiModeRegister'; AYANEOAIRPro = 'GenericEcControl';
  GPDWin4 = 'GpdWin4HcRelease'; GPDWin5 = 'GpdWin5HcAutoRelease'; GPDWinMini = 'None'; SteamDeck = 'None';
  LokiZero = 'AynLokiMode'; ClawA1M = 'MsiHcDefaultRelease'; LegionGoSZ1 = 'LenovoHcDefaultTable';
  LegionGoTablet2 = 'LegionGo2EcRpmOverride'; ROGAlly = 'AsusAcpiCurves'
}
$strategyMismatches = @($expectedStrategies.GetEnumerator() | ForEach-Object {
  $row = $routeRows | Where-Object Route -eq $_.Key
  if ($null -eq $row -or $row.RegistryRestoreStrategy -ne $_.Value) { [pscustomobject]@{ Route = $_.Key; Expected = $_.Value; Actual = if ($null -eq $row) { '<missing>' } else { $row.RegistryRestoreStrategy } } }
})

$searchTokens = [ordered]@{
  'HC.CurrentDevice.Close' = 'CurrentDevice\.Close\s*\('
  'HC.Window_Closed' = 'Window_Closed'
  'HC.SystemPending' = 'SystemPending'
  'HC.IDevice.Close' = '\bClose\s*\('
  'HC.AsusACPI.Close' = 'AsusACPI\.Close\s*\('
  'HC.SetFanControl' = '\bSetFanControl\s*\('
  'HC.SetFanDuty' = '\bSetFanDuty\s*\('
  'HC.SetFanCurve' = 'SetFanCurve\s*\('
  'HC.PowerProfileManager_Applied' = 'PowerProfileManager_Applied'
  'HC.ManagerFactory' = 'ManagerFactory'
  'YM.api.close' = '/api/close'
  'YM.api.restore' = '/api/restore'
  'YM.nativeHttp' = 'HttpListener|HttpClient|native HTTP'
}
$searchRows = @()
$searchFiles = @(Get-ChildItem -LiteralPath $HcRoot -Recurse -Filter '*.cs' -File) + @([System.IO.FileInfo]$HostPath)
foreach ($token in $searchTokens.GetEnumerator()) {
  $hits = @()
  foreach ($file in $searchFiles) {
    $raw = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    $matches = [regex]::Matches($raw, $token.Value, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($matches.Count -gt 0) {
      $hits += [pscustomobject]@{ Path = $file.FullName; Count = $matches.Count; FirstLine = Get-LineNumber $raw $matches[0].Index }
    }
  }
  $searchRows += [pscustomobject]@{ Token = $token.Key; Pattern = $token.Value; FileCount = $hits.Count; MatchCount = @($hits | Measure-Object -Property Count -Sum).Sum; Hits = $hits }
}

$inventory = [ordered]@{
  generatedAtUtc = [DateTime]::UtcNow.ToString('o')
  hardwareWrites = $false
  hcDevicesRoot = $HcDevicesRoot
  factorySource = $factoryPath
  hostSource = $hostPath
  hcCsFileCount = @(Get-ChildItem -LiteralPath $HcRoot -Recurse -Filter '*.cs' -File).Count
  hcClassCount = $classMap.Count
  hcFactoryFanClassCount = $factoryFanClasses.Count
  yemanRouteCount = $routeClasses.Count
  routeCoverageExact = ($missing.Count -eq 0 -and $unexpected.Count -eq 0)
  missingRoutes = $missing
  unexpectedRoutes = $unexpected
  unresolvedOwnerRouteCount = $unresolvedRows.Count
  notWriteReadyRouteCount = $notReadyRows.Count
  profileCurveRouteCount = $profileKindRows.Count
  customProfileCallbackRouteCount = $customProfileRows.Count
  curveChecks = $curveChecks
  routeSemanticMismatchCount = $semanticMismatches.Count
  strategyMismatchCount = $strategyMismatches.Count
  sourceSearch = $searchRows
  hcSourceFilesSha256 = @(
    foreach ($f in Get-ChildItem -LiteralPath $HcRoot -Recurse -Filter '*.cs' -File | Sort-Object FullName) {
      [ordered]@{ path = $f.FullName; sha256 = (Get-Sha256 $f.FullName) }
    }
  )
}

$diffs = @()
if (!$inventory.routeCoverageExact) { $diffs += [pscustomobject]@{ id='T2-ROUTE-COVERAGE'; severity='P1'; status='needs-investigation'; detail='Factory fan classes and YeMan route registry differ'; missing=$missing; unexpected=$unexpected } }
if ($unresolvedRows.Count -gt 0) { $diffs += [pscustomobject]@{ id='T2-OWNER-UNRESOLVED'; severity='P1'; status='needs-investigation'; detail='One or more route lifecycle/fan methods cannot be resolved through HC inheritance'; routes=@($unresolvedRows.Route) } }
if ($notReadyRows.Count -gt 0) { $diffs += [pscustomobject]@{ id='T2-ROUTE-NOT-WRITE-READY'; severity='P1'; status='needs-investigation'; detail='Mapped route does not declare write-ready HC callback gate'; routes=@($notReadyRows.Route) } }
if ($customProfileRows.Count -eq 0) { $diffs += [pscustomobject]@{ id='T2-PROFILE-OWNER-MISSING'; severity='needs-investigation'; status='needs-investigation'; detail='No device-specific profile callback resolved; inspect parser/source boundary' } }
if (@($curveChecks.GetEnumerator() | Where-Object { !$_.Value }).Count -gt 0) {
  $diffs += [pscustomobject]@{ id='T2-CURVE-CONVERSION'; severity='P1'; status='needs-investigation'; detail='HC 11-point interpolation and YeMan four-node expansion could not be source-confirmed'; failed=@($curveChecks.GetEnumerator() | Where-Object { !$_.Value } | ForEach-Object Key) }
}
if ($semanticMismatches.Count -gt 0) {
  $diffs += [pscustomobject]@{ id='T2-ROUTE-SEMANTICS'; severity='P1'; status='needs-investigation'; detail='A mapped route lacks the HC entry-point owner required by its registry kind'; routes=@($semanticMismatches.Route) }
}
if ($strategyMismatches.Count -gt 0) {
  $diffs += [pscustomobject]@{ id='T2-RESTORE-STRATEGY-MISMATCH'; severity='P1'; status='needs-investigation'; detail='Special route restore strategy differs from the pinned source expectation'; routes=$strategyMismatches }
}
$diffs += [pscustomobject]@{ id='T2-PHYSICAL-OEM-ACK'; severity='P1'; status='needs-investigation'; detail='HC source has no universal physical OEM ownership acknowledgement; route readback evidence remains separate' }
$diffs += [pscustomobject]@{ id='T2-HC-FULL-MANAGER-GRAPH'; severity='P1'; status='needs-investigation'; detail='Route coverage does not prove complete HC ManagerFactory ownership or callback timing' }

$summary = @"
# T2 HC Route Deep Audit

- Generated UTC: $($inventory.generatedAtUtc)
- Hardware writes: false
- HC C# files: $($inventory.hcCsFileCount)
- Parsed HC classes: $($inventory.hcClassCount)
- HC factory fan classes: $($inventory.hcFactoryFanClassCount)
- YeMan route registry classes: $($inventory.yemanRouteCount)
- Exact route coverage: $($inventory.routeCoverageExact)
- Unresolved method-owner routes: $($unresolvedRows.Count)
- Not-write-ready routes: $($notReadyRows.Count)
- ProfileCurve routes: $($profileKindRows.Count)
- Routes with device-specific profile callback: $($customProfileRows.Count)
- Route semantic mismatches: $($semanticMismatches.Count)
- Restore strategy mismatches: $($strategyMismatches.Count)
- Curve conversion checks failed: $(@($curveChecks.GetEnumerator() | Where-Object { !$_.Value }).Count)

## Standing rule

Any unexplained difference, unproven success, or pass without sufficient evidence is [needs-investigation]. Temperature source HW is the only pre-excluded difference. Every other issue is treated as non-isolated and must be expanded through related lifecycle and recovery paths.

## Disposition

This is source-only evidence. Exact route-name coverage does not prove physical fan control, complete HC ManagerFactory equivalence, or universal OEM ownership. Those remain needs-investigation until separately evidenced.
"@

$needs = @"
# T2 Needs Investigation

The following are intentionally not declared complete:

1. Physical OEM ownership acknowledgement is not a universal HC contract.
2. Full HC ManagerFactory ownership/timing is not established by a Fan Host route registry.
3. Every route's runtime firmware behavior requires real-device or route-specific evidence; source owner resolution is not physical proof.
4. Any route registry metadata that cannot be tied to an exact HC callback body must be reviewed as a non-isolated issue.
"@

$testPlan = @"
# T2 Test Plan

- Run existing 70/70 route capability audit.
- Run HC close-route and lifecycle-order source audits.
- Run Fan Host self-test with hardwareWrites=false.
- Run route-level mock curve/restore/lease regressions.
- For each newly authorized route, collect handshake, Open/OpenEvents, enable, restore, Close and physical OEM evidence separately.
- Never infer physical success from HTTP 2xx or a void HC callback return.
"@

$inventory | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'source-inventory.json') -Encoding UTF8
$routeRows | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputRoot 'route-owners.json') -Encoding UTF8
$diffs | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputRoot 'parity-diff.json') -Encoding UTF8
$curveChecks | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'curve-parity.json') -Encoding UTF8
$searchRows | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'source-search.json') -Encoding UTF8
$summary | Set-Content -LiteralPath (Join-Path $OutputRoot 'audit-summary.md') -Encoding UTF8
$needs | Set-Content -LiteralPath (Join-Path $OutputRoot 'needs-investigation.md') -Encoding UTF8
$testPlan | Set-Content -LiteralPath (Join-Path $OutputRoot 'test-plan.md') -Encoding UTF8

$hashLines = foreach ($f in Get-ChildItem -LiteralPath $OutputRoot -File | Where-Object Name -ne 'sha256.txt' | Sort-Object Name) {
  "{0}  {1}" -f (Get-Sha256 $f.FullName), $f.Name
}
$hashLines | Set-Content -LiteralPath (Join-Path $OutputRoot 'sha256.txt') -Encoding ASCII

Write-Output ("T2 HC route deep audit: EXECUTED (factoryFanClasses={0}; yemanRoutes={1}; exactCoverage={2}; unresolvedOwners={3}; hardwareWrites=false)" -f $factoryFanClasses.Count, $routeClasses.Count, $inventory.routeCoverageExact, $unresolvedRows.Count)
Write-Output ("T2 disposition: needs-investigation (physicalOemAck=false; fullManagerFactory=unproven; output={0})" -f $OutputRoot)
