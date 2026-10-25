# Read-only final-archive identity gate. Does not build, extract, install or launch.
function Get-DeckyExportSha256([System.IO.Stream]$Stream) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-', '') }
  finally { $sha.Dispose() }
}
function Get-DeckyExportFileSha256([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  try { return Get-DeckyExportSha256 $stream }
  finally { $stream.Dispose() }
}
function Assert-DeckyExportPayload {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$ReleaseZip,
    [Parameter(Mandatory=$true)][string]$SourcePowerControl,
    [string]$StagedPowerControl = '',
    [string]$BuiltPluginRoot = '',
    [object[]]$CurrentSourcePluginFiles = @(),
    [switch]$RequireManagedManifest
  )
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  $required = @(
    'decky/PluginLoader_noconsole.exe',
    'decky/LICENSE.decky-loader',
    'decky/plugins/ymcc-sidebar/dist/index.js',
    'decky/plugins/ymcc-sidebar/plugin.json',
    'decky/plugins/ymcc-sidebar/package.json',
    'decky/plugins/ymcc-sidebar/LICENSE.decky-api',
    'decky/plugins/ymcc-sidebar/LICENSE.vue'
  )
  $compiled = @{}
  foreach ($file in $CurrentSourcePluginFiles) {
    if ($compiled.ContainsKey($file.name) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or [int64]$file.bytes -le 0) {
      throw 'DECKY_EXPORT_CURRENT_SOURCE_RECORD_INVALID'
    }
    $compiled[$file.name] = $file
  }
  if ($compiled.Count -and ($compiled.Count -ne 5 -or @('dist/index.js','plugin.json','package.json','LICENSE.decky-api','LICENSE.vue' | Where-Object { -not $compiled.ContainsKey($_) }).Count)) {
    throw 'DECKY_EXPORT_CURRENT_SOURCE_RECORD_INVALID'
  }
  $archive = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ReleaseZip))
  try {
    $records = @()
    foreach ($relative in $required) {
      $source = Join-Path $SourcePowerControl $relative.Replace('/', '\')
      if ($BuiltPluginRoot -and $relative.StartsWith('decky/plugins/ymcc-sidebar/')) {
        $source = Join-Path $BuiltPluginRoot $relative.Substring('decky/plugins/ymcc-sidebar/'.Length).Replace('/', '\')
      }
      if ($compiled.Count -and $relative.StartsWith('decky/plugins/ymcc-sidebar/')) {
        $record = $compiled[$relative.Substring('decky/plugins/ymcc-sidebar/'.Length)]
        $expected = [string]$record.sha256
        $expectedLength = [int64]$record.bytes
      } else {
        if (-not [IO.File]::Exists($source)) { throw "DECKY_EXPORT_SOURCE_MISSING: $relative" }
        $expected = Get-DeckyExportFileSha256 $source
        $expectedLength = ([IO.FileInfo]$source).Length
      }
      if ($StagedPowerControl) {
        $staged = Join-Path $StagedPowerControl $relative.Replace('/', '\')
        if (-not [IO.File]::Exists($staged)) { throw "DECKY_EXPORT_STAGED_MISSING: $relative" }
        if (([IO.FileInfo]$staged).Length -ne $expectedLength -or (Get-DeckyExportFileSha256 $staged) -ne $expected) {
          throw "DECKY_EXPORT_STAGED_MISMATCH: $relative"
        }
      }
      $name = 'PowerControl/' + $relative
      # Windows destination paths are case-insensitive; slash/case variants must
      # not hide a second copy which overwrites the verified entry on extraction.
      $entries = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -ieq $name })
      if ($entries.Count -ne 1) { throw "DECKY_EXPORT_ENTRY_COUNT: $name ($($entries.Count))" }
      $stream = $entries[0].Open()
      try { $actual = Get-DeckyExportSha256 $stream }
      finally { $stream.Dispose() }
      if ($entries[0].Length -ne $expectedLength -or $actual -ne $expected) {
        throw "DECKY_EXPORT_ZIP_MISMATCH: $name"
      }
      $records += [ordered]@{path=$name;bytes=$expectedLength;sha256=$actual}
    }
    $managedEntries=@($archive.Entries | Where-Object { $_.FullName.Replace('\','/') -ieq 'YeManCC/decky-package.json' })
    if ($RequireManagedManifest -or $managedEntries.Count) {
      if ($managedEntries.Count -ne 1 -or $managedEntries[0].Length -gt 131072) { throw 'DECKY_EXPORT_MANAGED_MANIFEST_MISSING_OR_DUPLICATED' }
      . (Join-Path $PSScriptRoot '..\native\decky_package_maintenance.ps1')
      $stream=$managedEntries[0].Open();$reader=New-Object IO.StreamReader($stream,[Text.Encoding]::UTF8,$true)
      try { $managed=$reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
      Assert-YmccDeckyManifest $managed
      foreach ($file in $managed.files) {
        $record=@($records | Where-Object { $_.path -ceq ('PowerControl/decky/'+$file.path) })
        if ($record.Count -ne 1 -or $record[0].sha256 -ine $file.sha256 -or $record[0].bytes -ne $file.bytes) { throw 'DECKY_EXPORT_MANAGED_MANIFEST_ASSET_MISMATCH' }
      }
    }
    return [ordered]@{status='passed';releaseZip=[IO.Path]::GetFullPath($ReleaseZip);files=$records;sourceMatched=(-not [bool]$BuiltPluginRoot);builtPluginChecked=[bool]$BuiltPluginRoot;stagingChecked=[bool]$StagedPowerControl;currentSourceChecked=[bool]$compiled.Count;archiveExtracted=$false;productModified=$false}
  } finally { $archive.Dispose() }
}

# Recompile the authoritative Decky/HUD inputs in memory, then audit the chosen
# ZIP. Build caches, source dist, installed files and an old identity receipt are
# never used as proof that this archive carries the current plugin.
function Assert-DeckyCurrentSourceExport {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$ReleaseZip,
    [Parameter(Mandatory=$true)][string]$ProjectRoot
  )
  $project = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
  $description = & node (Join-Path $project 'tools\build-decky-sidebar-plugin.mjs') --describe
  if ($LASTEXITCODE -ne 0) { throw "DECKY_EXPORT_CURRENT_SOURCE_BUILD_FAILED: exit=$LASTEXITCODE" }
  $expected = ($description -join [Environment]::NewLine) | ConvertFrom-Json
  if (-not $expected.described -or [IO.Path]::GetFullPath($expected.sourceRoot).TrimEnd('\') -ine $project -or $expected.files.Count -ne 5) {
    throw 'DECKY_EXPORT_CURRENT_SOURCE_RECORD_INVALID'
  }
  $receipt = Assert-DeckyExportPayload -ReleaseZip $ReleaseZip -SourcePowerControl (Join-Path $project 'PowerControl') -CurrentSourcePluginFiles @($expected.files)
  $receipt['currentSourceRoot'] = $project
  return $receipt
}
