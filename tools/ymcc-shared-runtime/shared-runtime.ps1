# Pinned third-party authority and package checks. Windows PowerShell 5.1 compatible.
function Get-YmccSharedSha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Assert-YmccSharedPath([string]$Root, [string]$Relative) {
  $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  $full = [IO.Path]::GetFullPath((Join-Path $base $Relative))
  if (-not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Shared runtime path escapes root: $Relative" }
  $probe = $full
  while ($probe -and $probe.Length -ge $base.Length) {
    if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Shared runtime refuses reparse point: $probe"
    }
    $probe = Split-Path -Parent $probe
  }
  return $full
}
function Get-YmccSharedRuntimeRecords([string]$ProjectRoot) {
  $source = Join-Path $ProjectRoot 'deps\ymcc-shared-runtime'
  $manifestFile = Assert-YmccSharedPath $source 'manifest.json'
  $manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($manifest.schemaVersion -ne 1 -or $manifest.packagedDirectory -cne 'PowerControl/ymcc-shared-runtime' -or $manifest.binaryDirectory -cne 'bin') { throw 'Invalid YMCC shared runtime schema/layout' }
  $expectedNames = @('Newtonsoft.Json.dll', 'Nefarius.Utilities.DeviceManagement.dll', 'Nefarius.Drivers.HidHide.dll',
    'Nefarius.Vicius.Abstractions.dll', 'Microsoft.Extensions.DependencyInjection.Abstractions.dll',
    'Microsoft.Extensions.Logging.Abstractions.dll', 'LibreHardwareMonitorLib.dll', 'RAMSPDToolkit-NDD.dll')
  $seen = @{}
  $records = @()
  foreach ($entry in @($manifest.files)) {
    $name = [string]$entry.packagedPath
    if ($name -notin $expectedNames -or $seen.ContainsKey($name) -or [string]$entry.path -cne ('bin/' + $name) -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [long]$entry.bytes -le 0) { throw "Invalid/duplicate pinned shared DLL: $name" }
    $seen[$name] = $true
    $file = Assert-YmccSharedPath $source ([string]$entry.path)
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne [long]$entry.bytes -or (Get-YmccSharedSha256 $file) -cne ([string]$entry.sha256).ToUpperInvariant()) { throw "Pinned shared DLL missing or changed: $name" }
    if ([Reflection.AssemblyName]::GetAssemblyName($file).FullName -cne [string]$entry.assemblyIdentity) { throw "Pinned shared assembly identity changed: $name" }
    $packaged=$true
    if ($entry.PSObject.Properties['packagedDirectory']) {
      if ($name -cne 'LibreHardwareMonitorLib.dll' -or [string]$entry.packagedDirectory -cne 'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1') { throw 'Unapproved shared distribution override' }
      $packaged=$false
    }
    if ($name -ceq 'LibreHardwareMonitorLib.dll' -and $packaged) { throw 'Canonical device-runtime LHM distribution is required' }
    $records += [pscustomobject]@{ packaged=$packaged; source = $file; relative = $name; sourceRelative = [string]$entry.path; sha256 = ([string]$entry.sha256).ToUpperInvariant(); bytes = [long]$entry.bytes; kind = 'binary'; consumers = @($entry.consumers) }
  }
  if ($seen.Count -ne $expectedNames.Count) { throw 'Pinned shared runtime binary set is incomplete' }
  $metadataSeen = @{}
  foreach ($entry in @($manifest.licenses) + @($manifest.provenanceFiles)) {
    $relative = [string]$entry.path
    if ($relative -notmatch '^(licenses|provenance)/[^/\\]+$' -or $metadataSeen.ContainsKey($relative) -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Invalid/duplicate shared provenance path: $relative" }
    $metadataSeen[$relative] = $true
    $file = Assert-YmccSharedPath $source $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-YmccSharedSha256 $file) -cne ([string]$entry.sha256).ToUpperInvariant()) { throw "Pinned shared license/provenance missing or changed: $relative" }
    $records += [pscustomobject]@{ packaged=$true; source = $file; relative = $relative; sourceRelative = $relative; sha256 = ([string]$entry.sha256).ToUpperInvariant(); bytes = (Get-Item -LiteralPath $file).Length; kind = 'provenance'; consumers = @() }
  }
  foreach ($name in @('manifest.json','README.md')) {
    $file = Assert-YmccSharedPath $source $name
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Shared runtime metadata missing: $name" }
    $records += [pscustomobject]@{ packaged=$true; source = $file; relative = $name; sourceRelative = $name; sha256 = Get-YmccSharedSha256 $file; bytes = (Get-Item -LiteralPath $file).Length; kind = 'metadata'; consumers = @() }
  }
  $actual = @(Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object { $_.FullName.Substring($source.TrimEnd('\').Length + 1).Replace('\','/') } | Sort-Object)
  $expected = @($records | ForEach-Object { $_.sourceRelative } | Sort-Object)
  if (Compare-Object $actual $expected) { throw 'Shared source inventory differs from the pinned manifest' }
  return $records
}
function Assert-YmccSharedRuntimeSource([string]$ProjectRoot) {
  $records = @(Get-YmccSharedRuntimeRecords $ProjectRoot)
  $config = Get-Content -LiteralPath (Join-Path $ProjectRoot 'tools\opt-baseline.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $routes = $config.sharedRuntimeRoutes
  if ($routes.directory -cne 'ymcc-shared-runtime' -or $routes.sourceManifest -cne 'deps/ymcc-shared-runtime/manifest.json' -or
      (Get-YmccSharedSha256 (Join-Path $ProjectRoot $routes.sourceManifest)) -cne [string]$routes.sourceManifestSha256) { throw 'Shared runtime authority/config manifest lock mismatch' }
  foreach ($property in @($routes.sha256.PSObject.Properties)) {
    $matches = @($records | Where-Object { $_.relative -ceq $property.Name -and $_.kind -eq 'binary' })
    if ($matches.Count -ne 1 -or $matches[0].sha256 -cne [string]$property.Value) { throw "Shared runtime resolver/config hash mismatch: $($property.Name)" }
  }
}
function Get-YmccSharedRuntimeSourceFile([string]$ProjectRoot, [string]$Name) {
  $matches = @(Get-YmccSharedRuntimeRecords $ProjectRoot | Where-Object { $_.relative -ceq $Name -and $_.kind -eq 'binary' })
  if ($matches.Count -ne 1) { throw "Unknown shared runtime DLL: $Name" }
  return $matches[0].source
}
function Assert-YmccSharedRuntimeStaged([string]$ProjectRoot, [string]$Directory) {
  Assert-YmccSharedRuntimeSource $ProjectRoot
  $records = @(Get-YmccSharedRuntimeRecords $ProjectRoot | Where-Object { $_.packaged })
  if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { throw "Staged shared runtime missing: $Directory" }
  foreach ($record in $records) {
    $file = Assert-YmccSharedPath $Directory $record.relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne $record.bytes -or (Get-YmccSharedSha256 $file) -cne $record.sha256) { throw "Staged shared runtime missing or changed: $($record.relative)" }
  }
  $actual = @(Get-ChildItem -LiteralPath $Directory -File -Recurse | ForEach-Object { $_.FullName.Substring($Directory.TrimEnd('\').Length + 1).Replace('\','/') } | Sort-Object)
  $expected = @($records | ForEach-Object { $_.relative } | Sort-Object)
  if (Compare-Object $actual $expected) { throw 'Staged shared runtime contains missing/unlisted files' }
}
function Copy-YmccSharedRuntime([string]$ProjectRoot, [string]$Directory, [string]$BackupDirectory = '') {
  Assert-YmccSharedRuntimeSource $ProjectRoot
  $records = @(Get-YmccSharedRuntimeRecords $ProjectRoot | Where-Object { $_.packaged })
  $directoryFull = [IO.Path]::GetFullPath($Directory).TrimEnd('\')
  if ((Split-Path -Leaf $directoryFull) -cne 'ymcc-shared-runtime' -or (Split-Path -Leaf (Split-Path -Parent $directoryFull)) -cne 'PowerControl') { throw 'Shared staging requires PowerControl/ymcc-shared-runtime layout' }
  if (Test-Path -LiteralPath $directoryFull -PathType Container) {
    [void](Assert-YmccSharedPath $directoryFull 'manifest.json')
    $actual = @(Get-ChildItem -LiteralPath $directoryFull -File -Recurse | ForEach-Object { $_.FullName.Substring($directoryFull.Length + 1).Replace('\','/') })
    $expected = @($records | ForEach-Object { $_.relative })
    if (@($actual | Where-Object { $_ -notin $expected }).Count -ne 0) { throw 'Existing shared runtime has unlisted files; refusing to overlay' }
  }
  # Validate every destination before copying any file. Never move/delete a runtime tree.
  foreach ($record in $records) { [void](Assert-YmccSharedPath $directoryFull $record.relative) }
  foreach ($record in $records) {
    $destination = Assert-YmccSharedPath $directoryFull $record.relative
    if ((Test-Path -LiteralPath $destination -PathType Leaf) -and (Get-YmccSharedSha256 $destination) -cne $record.sha256 -and $BackupDirectory) {
      $backup = Assert-YmccSharedPath $BackupDirectory $record.relative
      if (Test-Path -LiteralPath $backup) { throw "Shared runtime backup already exists: $backup" }
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
      Copy-Item -LiteralPath $destination -Destination $backup
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $record.source -Destination $destination -Force
  }
  Assert-YmccSharedRuntimeStaged $ProjectRoot $directoryFull
}
function Assert-YmccInputHostBuild([string]$ProjectRoot, [string]$HostDirectory) {
  Assert-YmccSharedRuntimeSource $ProjectRoot
  foreach ($record in @(Get-YmccSharedRuntimeRecords $ProjectRoot | Where-Object { $_.kind -eq 'binary' -and $_.consumers -contains 'InputHost' })) {
    $file = Assert-YmccSharedPath $HostDirectory $record.relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-YmccSharedSha256 $file) -cne $record.sha256) { throw "InputHost build support dependency differs from shared authority: $($record.relative)" }
  }
}
function Assert-YmccBatteryHostCopies([string]$ProjectRoot, [string]$Directory) {
  foreach ($name in @('LibreHardwareMonitorLib.dll','RAMSPDToolkit-NDD.dll')) {
    $source = Get-YmccSharedRuntimeSourceFile $ProjectRoot $name
    $file = Assert-YmccSharedPath $Directory $name
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-YmccSharedSha256 $file) -cne (Get-YmccSharedSha256 $source)) { throw "BatteryHost local dependency missing or changed: $name" }
  }
}
function Assert-YmccSharedRuntimeArchive([string]$ProjectRoot, $Archive) {
  Assert-YmccSharedRuntimeSource $ProjectRoot
  $records = @(Get-YmccSharedRuntimeRecords $ProjectRoot | Where-Object { $_.packaged })
  $prefix = 'PowerControl/ymcc-shared-runtime/'
  $actual = @($Archive.Entries | Where-Object { $_.FullName.Replace('\','/').StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and -not $_.FullName.Replace('\','/').EndsWith('/') })
  if ($actual.Count -ne $records.Count) { throw 'Shared archive inventory count mismatch' }
  foreach ($record in $records) {
    $expected = $prefix + $record.relative
    $hits = @($actual | Where-Object { $_.FullName.Replace('\','/') -ceq $expected })
    if ($hits.Count -ne 1 -or $hits[0].Length -ne $record.bytes) { throw "Shared archive entry missing/duplicated: $expected" }
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = $hits[0].Open()
    try { $actualHash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
    finally { $stream.Dispose(); $sha.Dispose() }
    if ($actualHash -cne $record.sha256) { throw "Shared archive entry hash mismatch: $expected" }
  }
}
