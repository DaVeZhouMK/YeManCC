[CmdletBinding()]
param(
  [string]$Dll = '..\\..\\..\\Archives\\Migration-Backup\\20260831-152113\\YMCC-Workspace\\Build\\External\\HIDMaestro\\v1.7.0\\HIDMaestro.Core.dll',
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hidmaestro-api-metadata-20260903.json'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dllPath = (Resolve-Path (Join-Path $repo $Dll)).Path
$project = Join-Path $PSScriptRoot 'hidmaestro_api_probe\hidmaestro_api_probe.csproj'
dotnet build $project --nologo -v:q | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'HIDMaestro metadata probe build failed' }
$raw = dotnet run --project $project --no-build -- $dllPath
if ($LASTEXITCODE -ne 0) { throw 'HIDMaestro metadata probe execution failed' }
$observed = $raw | ConvertFrom-Json
$status = if ($observed.assembly.load -eq 'succeeded') { 'LOCATED' } else { 'BLOCKED' }
$result = [ordered]@{
  evidenceId = 'A1-HIDMAESTRO-API-METADATA-20260903'
  status = $status
  systemMutation = $false
  productRuntimeMutated = $false
  constructorsInvoked = $false
  staticMethodsInvoked = $false
  observed = $observed
  conclusion = 'Metadata-only ABI inventory. No HIDMaestro controller/profile object was constructed and no static method was called.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HIDMAESTRO API METADATA: $status"
Write-Output "Evidence: $outPath"
