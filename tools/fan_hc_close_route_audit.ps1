<#
.SYNOPSIS
  Read-only lifecycle ownership audit for every HC fan route in YeManFanHost.

.DESCRIPTION
  Resolves each registered route through frozen HC source inheritance and
  verifies that Close/SetFanControl ownership is source-resolvable. This is an
  audit, not an equivalence acceptance: it reports architectural gaps that a
  route-count or mock pass cannot prove. It never loads HC or accesses WMI,
  ACPI, HID, EC, or hardware.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcDevicesRoot,
  [Parameter(Mandatory = $true)][string]$HostSource
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($HcDevicesRoot, $HostSource)) {
  if (-not (Test-Path -LiteralPath $path)) { throw "Required source is missing: $path" }
}

function Remove-CSharpComments([string]$text) {
  $withoutBlocks = [regex]::Replace($text, '(?s)/\*.*?\*/', '')
  return [regex]::Replace($withoutBlocks, '(?m)//.*$', '')
}

$classes = @{}
Get-ChildItem -LiteralPath $HcDevicesRoot -Recurse -Filter '*.cs' | ForEach-Object {
  $source = Remove-CSharpComments (Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8)
  foreach ($match in [regex]::Matches($source, '(?m)\b(?:public\s+|internal\s+|private\s+|protected\s+)?(?:abstract\s+|partial\s+)*class\s+(?<name>\w+)(?:\s*:\s*(?<base>[\w\.]+))?')) {
    $name = $match.Groups['name'].Value
    $base = if ($match.Groups['base'].Success) { ($match.Groups['base'].Value -split '\.')[-1] } else { '' }
    if (-not $classes.ContainsKey($name)) {
      $classes[$name] = [pscustomobject]@{ Name = $name; Base = $base; Source = $source; Paths = @($_.FullName) }
    } else {
      $record = $classes[$name]
      if ([string]::IsNullOrWhiteSpace($record.Base) -and -not [string]::IsNullOrWhiteSpace($base)) { $record.Base = $base }
      $record.Source = $record.Source + "`n" + $source
      $record.Paths += $_.FullName
    }
  }
}

if (-not $classes.ContainsKey('IDevice')) { throw 'Frozen HC IDevice class was not found' }

$hostText = Get-Content -LiteralPath $HostSource -Raw -Encoding UTF8
$routesStart = $hostText.IndexOf('private static IReadOnlyDictionary<string, FanRoute> BuildFanRoutes()')
$routesEnd = $hostText.IndexOf('private void LoadAssemblyAndFactory()', $routesStart)
if ($routesStart -lt 0 -or $routesEnd -le $routesStart) { throw 'Fan Host route registry boundary was not found' }
$routeRegistry = $hostText.Substring($routesStart, $routesEnd - $routesStart)

$routeClasses = @(
  [regex]::Matches($routeRegistry, '"HandheldCompanion\.Devices\.([A-Za-z][A-Za-z0-9_]*)"') |
    ForEach-Object { $_.Groups[1].Value }
)
$routeClasses += @(
  [regex]::Matches($routeRegistry, '"([A-Za-z][A-Za-z0-9_]*)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object { $classes.ContainsKey($_) }
)
$routeClasses = @($routeClasses | Sort-Object -Unique)
if ($routeClasses.Count -eq 0) { throw 'Fan Host route registry is empty' }

function Resolve-MethodOwner([string]$className, [string]$pattern) {
  $seen = @{}
  while (-not [string]::IsNullOrWhiteSpace($className) -and -not $seen.ContainsKey($className)) {
    $seen[$className] = $true
    if (-not $classes.ContainsKey($className)) { return "<missing:$className>" }
    $record = $classes[$className]
    if ($record.Source -match $pattern) { return $className }
    $className = $record.Base
  }
  return '<unresolved>'
}

$closePattern = '(?m)\b(?:public\s+)?(?:override\s+|virtual\s+)void\s+Close\s*\('
$fanControlPattern = '(?m)\b(?:public\s+)?(?:override\s+|virtual\s+)void\s+SetFanControl\s*\('
$profilePattern = '(?m)\b(?:public\s+)?(?:override\s+|virtual\s+)void\s+PowerProfileManager_Applied\s*\('
$unresolved = @()
$summary = @()
foreach ($route in $routeClasses) {
  if (-not $classes.ContainsKey($route)) { $unresolved += "${route}: class missing"; continue }
  $closeOwner = Resolve-MethodOwner $route $closePattern
  $fanOwner = Resolve-MethodOwner $route $fanControlPattern
  $profileOwner = Resolve-MethodOwner $route $profilePattern
  if ($closeOwner -match '^<' -or $fanOwner -match '^<') {
    $unresolved += "${route}: close=$closeOwner; setFanControl=$fanOwner"
  }
  $summary += [pscustomobject]@{ Route = $route; CloseOwner = $closeOwner; SetFanControlOwner = $fanOwner; ProfileOwner = $profileOwner }
}
if ($unresolved.Count -gt 0) { throw "Unresolved HC fan lifecycle route(s): $($unresolved -join '; ')" }

$rog = $classes['ROGAlly'].Source
$rogCloseAcpi = $rog.IndexOf('AsusACPI.Close();')
$rogCloseBase = $rog.IndexOf('base.Close();', $rogCloseAcpi)
$rogFanGuard = $rog.IndexOf('if (!IsOpen)', $rog.IndexOf('public override void SetFanControl'))
$rogDefaultTable = $rog.IndexOf('AsusACPI.SetFanCurve(AsusFan.CPU, defaultCPUFan);')
if ($rogCloseAcpi -lt 0 -or $rogCloseBase -le $rogCloseAcpi -or $rogFanGuard -lt 0 -or $rogDefaultTable -le $rogFanGuard) {
  throw 'Frozen HC ROG Close/SetFanControl ordering changed; review OEM evidence assumptions'
}

$base = $classes['IDevice'].Source
if ($base.IndexOf('SetFanControl(false);', $base.IndexOf('public virtual void Close()')) -lt 0) {
  throw 'Frozen HC IDevice.Close no longer calls SetFanControl(false)'
}

$closeCoreStart = $hostText.IndexOf('private void CloseCore(bool stopDeviceManager)')
$closeCoreEnd = $hostText.IndexOf('private void CloseHcDevice()', $closeCoreStart)
if ($closeCoreStart -lt 0 -or $closeCoreEnd -le $closeCoreStart) { throw 'Fan Host CloseCore boundary missing' }
$closeCore = $hostText.Substring($closeCoreStart, $closeCoreEnd - $closeCoreStart)
if ($closeCore -notmatch 'CloseHcDevice\(\)' -or $closeCore -match '(?m)^\s*(?:\w+\.)?ApplyPowerProfile\(') {
  throw 'Fan Host process close no longer has the direct HC virtual Close boundary'
}

$fullManagerGraph = $hostText.Contains('foreach (IManager manager in ManagerFactory.Managers)')
$fanOnlyManagerIsolation = (-not $hostText.Contains('StartHcDeviceManager()')) -and
  (-not $hostText.Contains('StopHcDeviceManager()')) -and
  $hostText.Contains('not-started/no-stop-required')
$profileDirectCallback = $hostText.Contains('Invoke("PowerProfileManager_Applied", profile')
$profileManagerStarted = $hostText.Contains('Invoke(hcDeviceManager, "Start")') -and $hostText.Contains('powerProfileManager, "Start"')
$ownerGroups = @($summary | Group-Object CloseOwner | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" })

Write-Output ("fan HC close route audit: EXECUTED (routes={0}; closeOwners={1}; hardwareWrites=false)" -f $summary.Count, ($ownerGroups -join ', '))
Write-Output ("fan HC close route audit: needs-investigation (fullManagerGraph={0}; fanOnlyManagerIsolation={1}; directProfileCallback={2}; powerProfileManagerStarted={3}; physicalOemAck=false)" -f $fullManagerGraph, $fanOnlyManagerIsolation, $profileDirectCallback, $profileManagerStarted)
Write-Output 'This audit verifies source resolution only. It is not proof of physical OEM handoff or complete HC lifecycle equivalence.'
