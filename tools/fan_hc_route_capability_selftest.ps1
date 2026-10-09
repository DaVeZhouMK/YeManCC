<#!
.SYNOPSIS
  Source-only audit of HC factory fan-capability coverage.

.DESCRIPTION
  This is deliberately a static check. It reads the frozen HC source and the
  Fan Host route registry; it never loads HandheldCompanion.dll, constructs a
  device, invokes WMI/ACPI/HID, or writes hardware. A route is considered
  covered only when every class reachable from IDevice.GetCurrent's `new`
  expressions and inheriting a class that declares DeviceCapabilities.FanControl
  is named in RealHcBackend.BuildFanRoutes().

  The check is an audit input, not proof that a physical route works. Runtime
  Open/OpenEvents/profile/restore and device firmware evidence remain separate
  gates under HC-FAN-LIFECYCLE-OEM-AUDIT-20260824.md.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcDevicesRoot,
  [Parameter(Mandatory = $true)][string]$FactorySource,
  [Parameter(Mandatory = $true)][string]$HostSource
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path -LiteralPath $HcDevicesRoot -PathType Container)) { throw "HC devices root missing: $HcDevicesRoot" }
if (!(Test-Path -LiteralPath $FactorySource -PathType Leaf)) { throw "HC factory source missing: $FactorySource" }
if (!(Test-Path -LiteralPath $HostSource -PathType Leaf)) { throw "Fan Host source missing: $HostSource" }

$classes = @{}
$fanDeclarations = @{}
foreach ($file in Get-ChildItem -LiteralPath $HcDevicesRoot -Recurse -Filter '*.cs') {
  $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  # Do not let prose such as "class implementing..." in a comment become a
  # fake declaration. This audit is source-only, so a conservative comment
  # stripper is preferable to treating a false class as HC evidence.
  $source = [regex]::Replace($text, '(?s)/\*.*?\*/', '')
  $source = [regex]::Replace($source, '(?m)//.*$', '')
  foreach ($match in [regex]::Matches($source, '(?m)\b(?:public\s+|internal\s+|private\s+|protected\s+)?(?:abstract\s+|partial\s+)?class\s+(\w+)(?:\s*:\s*([\w\.]+))?')) {
    $name = $match.Groups[1].Value
    $baseName = if ($match.Groups[2].Success) { ($match.Groups[2].Value -split '\.')[-1] } else { '' }
    $classes[$name] = $baseName
  }
  if ($source -match '(?:this\.)?Capabilities\s*(?:\|=|=)\s*DeviceCapabilities\.FanControl') {
    $match = [regex]::Match($source, '(?m)\b(?:public\s+|internal\s+|private\s+|protected\s+)?(?:abstract\s+|partial\s+)?class\s+(\w+)')
    if ($match.Success) { $fanDeclarations[$match.Groups[1].Value] = $true }
  }
}

function Test-FanClass([string]$name) {
  $seen = @{}
  while (![string]::IsNullOrWhiteSpace($name) -and !$seen.ContainsKey($name)) {
    $seen[$name] = $true
    if ($fanDeclarations.ContainsKey($name)) { return $true }
    if (!$classes.ContainsKey($name)) { break }
    $name = $classes[$name]
  }
  return $false
}

$factoryText = Get-Content -LiteralPath $FactorySource -Raw -Encoding UTF8
$factoryClasses = @([regex]::Matches($factoryText, '\bnew\s+(\w+)\s*\(') |
  ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$fanFactoryClasses = @($factoryClasses | Where-Object { Test-FanClass $_ })

$hostText = Get-Content -LiteralPath $HostSource -Raw -Encoding UTF8
$buildStart = $hostText.IndexOf('private static IReadOnlyDictionary<string, FanRoute> BuildFanRoutes()')
$loadStart = $hostText.IndexOf('private void LoadAssemblyAndFactory()', $buildStart)
if ($buildStart -lt 0 -or $loadStart -le $buildStart) { throw 'BuildFanRoutes source boundary missing' }
$routeRegistry = $hostText.Substring($buildStart, $loadStart - $buildStart)

# Class names are taken from the frozen factory set, so descriptive strings in
# route comments cannot accidentally count as a route. BuildFanRoutes uses
# both bare class names and fully-qualified literal keys; account for both.
$unmapped = @($fanFactoryClasses | Where-Object {
  $bare = '"' + [regex]::Escape($_) + '"'
  $qualified = '"HandheldCompanion\.Devices\.' + [regex]::Escape($_) + '"'
  $routeRegistry -notmatch $bare -and $routeRegistry -notmatch $qualified
})

$knownClassNames = @($classes.Keys)
$bareRouteClassNames = @(
  [regex]::Matches($routeRegistry, '"([A-Za-z][A-Za-z0-9_]*)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object { $knownClassNames -contains $_ }
)
$qualifiedRouteClassNames = @(
  [regex]::Matches($routeRegistry, 'HandheldCompanion\.Devices\.([A-Za-z][A-Za-z0-9_]*)') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object { $knownClassNames -contains $_ }
)
$routeClassNames = @($bareRouteClassNames + $qualifiedRouteClassNames | Sort-Object -Unique)
$unexpected = @($routeClassNames | Where-Object { $fanFactoryClasses -notcontains $_ })
$missing = @($fanFactoryClasses | Where-Object { $routeClassNames -notcontains $_ })

if ($unmapped.Count -gt 0) {
  throw "HC fan-capable factory classes missing from FanRoutes: $($unmapped -join ', ')"
}
if ($missing.Count -gt 0) {
  throw "HC fan-capable factory classes missing from route literals: $($missing -join ', ')"
}
if ($unexpected.Count -gt 0) {
  throw "FanRoutes contains classes not reachable from HC fan-capable factory: $($unexpected -join ', ')"
}

# Method-level route evidence. A class that overrides one of HC's fan entry
# points must be present in the route registry; profile callbacks must remain
# profile routes. This is still static evidence (not a firmware readback), but
# it prevents a future route from being covered by class-name count alone.
$fanOverrideClasses = @{}
$profileOverrideClasses = @{}
foreach ($file in Get-ChildItem -LiteralPath $HcDevicesRoot -Recurse -Filter '*.cs') {
  $source = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  $source = [regex]::Replace($source, '(?s)/\*.*?\*/', '')
  $source = [regex]::Replace($source, '(?m)//.*$', '')
  $classMatch = [regex]::Match($source, '(?m)\b(?:public\s+|internal\s+|private\s+|protected\s+)?(?:abstract\s+|partial\s+)?class\s+(\w+)')
  if (!$classMatch.Success) { continue }
  $className = $classMatch.Groups[1].Value
  if ($source -match '\boverride\s+(?:async\s+)?(?:Task|void|bool|double|int)\s+(?:SetFanControl|SetFanDuty)\s*\(') { $fanOverrideClasses[$className] = $true }
  if ($source -match '\boverride\s+void\s+PowerProfileManager_Applied\s*\(') { $profileOverrideClasses[$className] = $true }
}
$missingOverrideRoutes = @($fanOverrideClasses.Keys | Where-Object { $fanFactoryClasses -contains $_ -and $routeClassNames -notcontains $_ })
if ($missingOverrideRoutes.Count -gt 0) { throw "HC fan method override classes missing from FanRoutes: $($missingOverrideRoutes -join ', ')" }
$missingProfileRoutes = @($profileOverrideClasses.Keys | Where-Object {
  $fanFactoryClasses -contains $_ -and $routeClassNames -notcontains $_
})
if ($missingProfileRoutes.Count -gt 0) { throw "HC profile callback classes are not registered as ProfileCurve: $($missingProfileRoutes -join ', ')" }

Write-Output ("fan HC route capability audit: PASS (factoryFanClasses={0}; routeRegistryClasses={1}; fanOverrideClasses={2}; profileOverrideClasses={3}; exactCoverage=true; methodCoverage=true; hardwareWrites=false)" -f $fanFactoryClasses.Count, $routeClassNames.Count, $fanOverrideClasses.Count, $profileOverrideClasses.Count)
Write-Output ("fan-capable factory classes: {0}" -f ($fanFactoryClasses -join ', '))
