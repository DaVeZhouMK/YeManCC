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
    [string]$StagedPowerControl = ''
  )
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  $required = @(
    'decky/PluginLoader_noconsole.exe',
    'decky/LICENSE.decky-loader',
    'decky/plugins/ymcc-sidebar/dist/index.js',
    'decky/plugins/ymcc-sidebar/plugin.json',
    'decky/plugins/ymcc-sidebar/package.json',
    'decky/plugins/ymcc-sidebar/LICENSE.decky-api'
  )
  $archive = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ReleaseZip))
  try {
    $records = @()
    foreach ($relative in $required) {
      $source = Join-Path $SourcePowerControl $relative.Replace('/', '\')
      if (-not [IO.File]::Exists($source)) { throw "DECKY_EXPORT_SOURCE_MISSING: $relative" }
      $expected = Get-DeckyExportFileSha256 $source
      $expectedLength = ([IO.FileInfo]$source).Length
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
    return [ordered]@{status='passed';releaseZip=[IO.Path]::GetFullPath($ReleaseZip);files=$records;sourceMatched=$true;stagingChecked=[bool]$StagedPowerControl;archiveExtracted=$false;productModified=$false}
  } finally { $archive.Dispose() }
}
