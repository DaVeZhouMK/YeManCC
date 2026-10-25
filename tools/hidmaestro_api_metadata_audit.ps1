[CmdletBinding()]
param(
  [string]$Dll = 'deps/hidmaestro/HIDMaestro.Core.dll',
  [string]$Output = '',
  [switch]$LegacyA1
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# Preserve the named 2026-09-03 replay explicitly; -Dll/-Output can override it.
if ($LegacyA1 -and -not $PSBoundParameters.ContainsKey('Dll')) {
  $Dll = '../../../Archives/Migration-Backup/20260831-152113/YMCC-Workspace/Build/External/HIDMaestro/v1.7.0/HIDMaestro.Core.dll'
}
if (-not $Output) {
  $Output = if ($LegacyA1) { '../../Build/Validation/HC-Parity/A1-hidmaestro-api-metadata-20260903.json' }
    else { '../../Build/Validation/HC-Parity/hidmaestro-api-metadata.json' }
}
$dllInput = if ([IO.Path]::IsPathRooted($Dll)) { $Dll } else { Join-Path $repo $Dll }
$dllPath = (Resolve-Path -LiteralPath $dllInput).Path
$bytes = (Get-Item -LiteralPath $dllPath).Length
$hash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
$dependencyLock = $null
if ($dllPath -ieq [IO.Path]::GetFullPath((Join-Path $repo 'deps/hidmaestro/HIDMaestro.Core.dll'))) {
  $dependencyLock = 'deps/hidmaestro/dependency-lock.json'
  $lock = Get-Content -Raw -LiteralPath (Join-Path $repo $dependencyLock) | ConvertFrom-Json
  $core = @($lock.files | Where-Object { $_.path -eq 'deps/hidmaestro/HIDMaestro.Core.dll' })
  if ($core.Count -ne 1 -or $bytes -ne [long]$core[0].bytes -or $hash -ine $core[0].sha256) {
    throw 'Mainline HIDMaestro dependency lock mismatch'
  }
}
$project = Join-Path $PSScriptRoot 'hidmaestro_api_probe\hidmaestro_api_probe.csproj'
dotnet build $project --nologo -v:q | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'HIDMaestro metadata probe build failed' }
$raw = dotnet run --project $project --no-build -- $dllPath
if ($LASTEXITCODE -ne 0) { throw 'HIDMaestro metadata probe execution failed' }
$observed = $raw | ConvertFrom-Json
$status = if ($observed.assembly.load -eq 'succeeded') { 'LOCATED' } else { 'BLOCKED' }
$result = [ordered]@{
  evidenceId = $(if ($LegacyA1) { 'A1-HIDMAESTRO-API-METADATA-20260903' } else { 'HIDMAESTRO-API-METADATA' })
  status = $status
  systemMutation = $false
  productRuntimeMutated = $false
  constructorsInvoked = $false
  staticMethodsInvoked = $false
  asset = [ordered]@{
    path = $dllPath; bytes = $bytes; sha256 = $hash; dependencyLock = $dependencyLock
    dependencyLockMatches = $(if ($dependencyLock) { $true } else { $null })
  }
  observed = $observed
  conclusion = 'Metadata-only ABI inventory. No HIDMaestro controller/profile object was constructed and no static method was called.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $repo $Output }
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $outPath -Encoding UTF8
$label = if ($LegacyA1) { 'A1 HIDMAESTRO API METADATA' } else { 'HIDMAESTRO API METADATA' }
Write-Output "${label}: $status"
Write-Output "Evidence: $outPath"
