[CmdletBinding()]
param(
  [string]$ProbeRoot,
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hc-net10-probe.json'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ProbeRoot)) {
  $ProbeRoot = (Get-ChildItem -LiteralPath (Join-Path $repo '..\..\Temp') -Directory -Filter 'hc-input-probe-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not (Test-Path -LiteralPath $ProbeRoot -PathType Container)) { throw "probe root not found: $ProbeRoot" }
dotnet build (Join-Path $PSScriptRoot 'hc_net10_probe\hc_net10_probe.csproj') --nologo -v:q | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'dotnet net10 probe build failed' }
$raw = dotnet run --project (Join-Path $PSScriptRoot 'hc_net10_probe\hc_net10_probe.csproj') --no-build -- $ProbeRoot
if ($LASTEXITCODE -ne 0) { throw 'dotnet net10 probe execution failed' }
$observed = $raw | ConvertFrom-Json
$result = [ordered]@{
  evidenceId = 'A1-HC-NET10-PROBE-20260903'
  status = if ($observed.handHeldCompanion.getTypes -eq 'succeeded' -and $observed.gamepadMotionNative.load -eq 'succeeded' -and @($observed.gamepadMotionNative.exports.PSObject.Properties | Where-Object { -not $_.Value }).Count -eq 0) { 'PARTIAL_LOAD_PASS' } else { 'BLOCKED' }
  systemMutation = $false
  productRuntimeMutated = $false
  targetFramework = 'net10.0-windows / win-x64'
  probeRoot = $ProbeRoot
  managerFactoryInvoked = $false
  observed = $observed
  conclusion = 'Matched .NET 10 isolated probe only; no ManagerFactory/static manager method invoked. HC managed types enumerate successfully; GamepadMotion is a native P/Invoke backend and is assessed through load/export presence without invoking any export.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HC NET10 PROBE: $($result.status)"
Write-Output "Evidence: $outPath"
