# Pure export verification + explicitly invoked copy/backup helpers. No process,
# hardware, Steam, service, or installed-path action when this file is dot-sourced.
function Get-MonitorHudExportHash([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}
function Get-MonitorHudSafeExportPath([string]$Root, [string]$Relative) {
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  $full = [IO.Path]::GetFullPath((Join-Path $rootFull $Relative))
  if (-not $full.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "HUD export path escapes its explicit root: $Relative"
  }
  # Refuse aliases before any recursive move, including aliases in ancestors.
  $probe = $full
  while ($probe) {
    if (Test-Path -LiteralPath $probe) {
      if ((Get-Item -LiteralPath $probe -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "HUD export refuses a reparse-point path: $probe"
      }
    }
    $parent = Split-Path -Parent $probe
    if ($parent -eq $probe) { break }
    $probe = $parent
  }
  return $full
}
function Assert-MonitorHudWebExport([string]$WebRoot, [string]$ExpectedRoot = '', [string[]]$EntryPages = @('index.html','hud-output.html')) {
  $root = [IO.Path]::GetFullPath($WebRoot).TrimEnd('\')
  $references = @()
  if (-not $EntryPages.Count) { throw 'HUD web entry contract is empty' }
  foreach ($page in $EntryPages) {
    $document = Get-MonitorHudSafeExportPath $root $page
    if (-not (Test-Path -LiteralPath $document -PathType Leaf)) { throw "HUD web entry missing: $document" }
    $html = Get-Content -LiteralPath $document -Raw -Encoding UTF8
    if ($html -notmatch '<script\b[^>]*\bsrc="\./assets/[^"]+\.js"') { throw "HUD web entry has no built module: $page" }
    $links = @([regex]::Matches($html, '(?:src|href)="\./(assets/[^"]+)"') | ForEach-Object { $_.Groups[1].Value })
    foreach ($relative in $links) {
      if ($relative -notmatch '^assets/[A-Za-z0-9_./-]+\.(?:js|css)$') { throw "Unexpected HUD web reference: $relative" }
      $asset = Get-MonitorHudSafeExportPath $root $relative
      if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) { throw "HUD web reference missing: $relative" }
      $references += $relative
    }
    if ($ExpectedRoot) {
      $expected = Get-MonitorHudSafeExportPath $ExpectedRoot $page
      if ((Get-MonitorHudExportHash $document) -ne (Get-MonitorHudExportHash $expected)) { throw "HUD HTML differs from build: $page" }
    }
  }
  $assets = Get-MonitorHudSafeExportPath $root 'assets'
  $files = @(Get-ChildItem -LiteralPath $assets -Recurse -File | ForEach-Object { $_.FullName.Substring($root.Length + 1) } | Sort-Object)
  if ($ExpectedRoot) {
    $expected = [IO.Path]::GetFullPath($ExpectedRoot).TrimEnd('\')
    $expectedAssets = Get-MonitorHudSafeExportPath $expected 'assets'
    $expectedFiles = @(Get-ChildItem -LiteralPath $expectedAssets -Recurse -File | ForEach-Object { $_.FullName.Substring($expected.Length + 1) } | Sort-Object)
    if (Compare-Object $files $expectedFiles) { throw 'HUD installed asset set differs from build' }
    foreach ($relative in $files) {
      $a = Get-MonitorHudSafeExportPath $root $relative
      $b = Get-MonitorHudSafeExportPath $expected $relative
      if ((Get-MonitorHudExportHash $a) -ne (Get-MonitorHudExportHash $b)) { throw "HUD installed asset differs from build: $relative" }
    }
  }
  return [PSCustomObject]@{ pages = @($EntryPages); assetCount = $files.Count; referencedAssets = @($references | Sort-Object -Unique) }
}
function Assert-MonitorHudDeckyExport([string]$PluginRoot, [string]$ExpectedRoot = '') {
  $root = [IO.Path]::GetFullPath($PluginRoot).TrimEnd('\')
  $allow = @('dist\index.js','package.json','plugin.json','LICENSE.decky-api','LICENSE.vue') | Sort-Object
  foreach ($relative in $allow) {
    $file = Get-MonitorHudSafeExportPath $root $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "HUD Decky payload missing: $relative" }
    if ($ExpectedRoot) {
      $expected = Get-MonitorHudSafeExportPath $ExpectedRoot $relative
      if ((Get-MonitorHudExportHash $file) -ne (Get-MonitorHudExportHash $expected)) { throw "HUD Decky payload differs from build: $relative" }
    }
  }
  $actual = @(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object { $_.FullName.Substring($root.Length + 1) } | Sort-Object)
  if (Compare-Object $allow $actual) { throw 'HUD Decky payload is not the exact immutable five-file export' }
  $manifest = Get-Content -LiteralPath (Join-Path $root 'plugin.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($manifest.name -ne 'ymcc-sidebar') { throw 'Unexpected HUD Decky plugin identity' }
  return [PSCustomObject]@{ name = 'ymcc-sidebar'; files = $allow }
}
function Move-MonitorHudDeckyBackup([string]$PowerControlRoot, [string]$BackupRoot) {
  $source = Get-MonitorHudSafeExportPath $PowerControlRoot 'decky\plugins\ymcc-sidebar'
  $backup = Get-MonitorHudSafeExportPath $BackupRoot 'PowerControl\decky\plugins\ymcc-sidebar'
  if (-not (Test-Path -LiteralPath $source)) { return }
  if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw 'Installed HUD plugin path is not a directory' }
  if (Test-Path -LiteralPath $backup) { throw 'HUD plugin backup already exists; refusing to merge/overwrite it' }
  # Absolute source and destination are checked above against their named roots.
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
  Move-Item -LiteralPath $source -Destination $backup
}
function Copy-MonitorHudDeckyExport([string]$BuildPluginRoot, [string]$PowerControlRoot) {
  $payload = Assert-MonitorHudDeckyExport $BuildPluginRoot
  $destination = Get-MonitorHudSafeExportPath $PowerControlRoot 'decky\plugins\ymcc-sidebar'
  foreach ($relative in $payload.files) {
    $source = Get-MonitorHudSafeExportPath $BuildPluginRoot $relative
    $target = Get-MonitorHudSafeExportPath $destination $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
  }
  Assert-MonitorHudDeckyExport $destination $BuildPluginRoot | Out-Null
}


# User-facing wheel pages follow the same closed product policy as both consumers.
# Missing, malformed or divergent flags fail the build instead of enabling a page.
function Get-YmccProductWebEntryPages([string]$ProjectRoot) {
  $front = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'src/radial/availability.ts'))
  $native = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/radial_release.h'))
  $frontFlags = [regex]::Matches($front, '(?m)^\s*export const RADIAL_USER_AVAILABLE\s*=\s*(true|false)\s*;')
  $nativeFlags = [regex]::Matches($native, '(?m)^\s*inline constexpr bool kUserAvailable\s*=\s*(true|false)\s*;')
  if ($frontFlags.Count -ne 1 -or $nativeFlags.Count -ne 1 -or $frontFlags[0].Groups[1].Value -cne $nativeFlags[0].Groups[1].Value) {
    throw 'RADIAL_RELEASE_POLICY_INVALID_OR_DIVERGENT'
  }
  $pages = @('index.html', 'hud-output.html', 'notice-output.html')
  if ($frontFlags[0].Groups[1].Value -ceq 'true') { $pages += 'radial-output.html' }
  return $pages
}
function Assert-YmccProductWebExport([string]$WebRoot, [string]$ProjectRoot) {
  $pages = @(Get-YmccProductWebEntryPages $ProjectRoot)
  if ($pages -notcontains 'radial-output.html' -and (Test-Path -LiteralPath (Join-Path $WebRoot 'radial-output.html'))) {
    throw 'RADIAL_USER_ENTRY_MUST_NOT_BE_PACKAGED'
  }
  Assert-MonitorHudWebExport $WebRoot -EntryPages $pages | Out-Null
}
