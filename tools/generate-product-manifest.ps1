<#
.SYNOPSIS
  Generate a complete manifest for the two finished YeMan product folders.

.DESCRIPTION
  Records every file's relative path, size and SHA256. The manifest is kept in
  the source documentation area so it never becomes a runtime payload.
#>
[CmdletBinding()]
param(
  [string]$ProductRoot = 'C:\SOFT\YeMan',
  [string]$OutputPath = '..\docs\YeManCC-成品资产清单.md'
)

$ErrorActionPreference = 'Stop'
$resolvedOutput = if ([IO.Path]::IsPathRooted($OutputPath)) {
  [IO.Path]::GetFullPath($OutputPath)
} else {
  [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputPath))
}

$roots = [ordered]@{
  'YeManCC' = Join-Path $ProductRoot 'YeManCC'
  'PowerControl' = Join-Path $ProductRoot 'PowerControl'
}

function Get-Category([string]$rootName, [string]$relativePath) {
  $first = ($relativePath -split '[\\/]')[0]
  if ($rootName -eq 'YeManCC') {
    if ($first -eq 'assets') { return 'Web UI build assets' }
    if ($first -eq 'icons') { return 'UI icon assets' }
    if ($relativePath -eq 'YeManCC.exe') { return 'Finished executable' }
    return 'Program support file'
  }

  switch -Regex ($first) {
    '^TDP$'           { return 'TDP performance scripts' }
    '^pawnio$'        { return 'TDP kernel runtime' }
    '^Sleep$'         { return 'Sleep optimization config' }
    '^ui-background$' { return 'Background media' }
    '^Display$'       { return 'Display resolution scripts' }
    '^MG-AUTO$'       { return 'Memory cleanup tool' }
    '^OpenSpeedy$'    { return 'Game acceleration runtime' }
    '^YeManSteam$'    { return 'Steam support assets' }
    '^virtualmemory$' { return 'Virtual memory scripts' }
    default           { return 'System scripts, tasks and hardware assets' }
  }
}

$records = @()
foreach ($root in $roots.GetEnumerator()) {
  if (-not (Test-Path -LiteralPath $root.Value -PathType Container)) {
    throw "Product folder is missing: $($root.Value)"
  }
  $rootPath = (Resolve-Path -LiteralPath $root.Value).Path.TrimEnd('\')
  foreach ($file in (Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\').Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $records += [pscustomobject]@{
      Root = $root.Key
      Category = Get-Category $root.Key $relative
      RelativePath = $relative
      Size = [int64]$file.Length
      Sha256 = $hash
      LastWriteTime = $file.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
    }
  }
}

$totalBytes = ($records | Measure-Object -Property Size -Sum).Sum
$generatedAt = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz')
$settingsPath = Join-Path $roots['PowerControl'] 'yeman-settings.json'
$settings = if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
  Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
} else {
  $null
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# YeManCC Product Asset Manifest')
$lines.Add('')
$lines.Add("> Generated: $generatedAt")
$lines.Add('> Scope: C:\SOFT\YeMan\YeManCC and C:\SOFT\YeMan\PowerControl')
$lines.Add('> Purpose: compare the finished package after future publishing and detect missing, replaced or stale files.')
$lines.Add('')
$lines.Add('## Summary')
$lines.Add('')
$lines.Add('| Product folder | File count | Total size |')
$lines.Add('|---|---:|---:|')
foreach ($root in $roots.Keys) {
  $subset = @($records | Where-Object Root -eq $root)
  $bytes = ($subset | Measure-Object -Property Size -Sum).Sum
  $lines.Add("| $root | $($subset.Count) | $([math]::Round($bytes / 1MB, 2)) MB |")
}
$lines.Add("| Total | $($records.Count) | $([math]::Round($totalBytes / 1MB, 2)) MB |")
$lines.Add('')
$lines.Add('## Current Unified Settings')
$lines.Add('')
if ($null -ne $settings) {
  $lines.Add('| Setting | Current value |')
  $lines.Add('|---|---|')
  $lines.Add('| Settings file | yeman-settings.json |')
  $lines.Add("| AC performance profile | $($settings.performanceSchedule.active.ac) |")
  $lines.Add("| DC performance profile | $($settings.performanceSchedule.active.dc) |")
  $lines.Add("| TDP maximum | $($settings.tdp.tdpMax) W |")
  $lines.Add("| FPS limit | $($settings.tdp.fpsLimit) |")
  $lines.Add("| Gamepad mouse backend | $($settings.gamepad.mouseBackend) |")
  $lines.Add("| Dynamic background | $($settings.ui.dynamicBackgroundEnabled) |")
  $lines.Add("| Background opacity | $($settings.ui.backgroundOpacity) |")
  $lines.Add("| Background blur | $($settings.ui.backgroundBlur) |")
} else {
  $lines.Add('Unified settings file not found.')
}
$lines.Add('')
$lines.Add('## Rules')
$lines.Add('')
$lines.Add('- yeman-settings.json is the unified user configuration; its .bak file is the backup.')
$lines.Add('- TDP is the only valid TDP script directory; TPD must not exist.')
$lines.Add('- pawnio contains finished runtime files only. Python source, spec, build and dist files stay in source.')
$lines.Add('- Runtime heartbeats, logs, PID files, temporary snapshots and legacy TXT files are not product assets.')
$lines.Add('- YeManCC/assets must contain only the current dist/assets files; stale hashed chunks are archived in source.')
$lines.Add('')
$lines.Add('## File Details')
$lines.Add('')
$lines.Add('| Product folder | Category | Relative path | Size (bytes) | SHA256 | Modified |')
$lines.Add('|---|---|---|---:|---|---|')
foreach ($record in ($records | Sort-Object Root, Category, RelativePath)) {
  $safePath = $record.RelativePath.Replace('|', '\|')
  $lines.Add("| $($record.Root) | $($record.Category) | $safePath | $($record.Size) | $($record.Sha256) | $($record.LastWriteTime) |")
}

$parent = Split-Path -Parent $resolvedOutput
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
[IO.File]::WriteAllLines($resolvedOutput, $lines.ToArray(), (New-Object Text.UTF8Encoding($false)))
Write-Output "Manifest written: $resolvedOutput"
Write-Output "Files: $($records.Count); bytes: $totalBytes"
