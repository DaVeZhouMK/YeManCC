<# Global software-only adapter. Load only the explicitly selected payload;
   production Main, its ownership gates and real hardware stay separate. #>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('memory','all')][string]$Mode,
  [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { Write-Error 'POWERSHELL_7_REQUIRED'; exit 10 }
if ([string]::IsNullOrWhiteSpace($env:YEMAN_SIM_PAYLOAD_ROOT)) { Write-Error 'EXPLICIT_CURRENT_HOST_PAYLOAD_REQUIRED'; exit 10 }
$payload = (Resolve-Path -LiteralPath $env:YEMAN_SIM_PAYLOAD_ROOT).ProviderPath
$hostDll = Join-Path $payload 'YeManFanHost.dll'
$backendDll = Join-Path $payload 'YeManFanBackend.dll'
foreach ($file in @($hostDll,$backendDll)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'EXPLICIT_HOST_OR_BACKEND_MISSING' } }
$hostHash = (Get-FileHash -LiteralPath $hostDll -Algorithm SHA256).Hash.ToLowerInvariant()
$backendHash = (Get-FileHash -LiteralPath $backendDll -Algorithm SHA256).Hash.ToLowerInvariant()
& (Join-Path $PSHOME 'pwsh.exe') -NoLogo -NoProfile -NonInteractive -File (Join-Path $PSScriptRoot 'consumer/sleep-engine/Invoke-SleepEngine.ps1') -HostPayloadRoot $payload -ExpectedHostSha256 $hostHash -ExpectedBackendSha256 $backendHash -EvidenceRoot $EvidenceDir -Mode $Mode
$engineExit = $LASTEXITCODE
if ($engineExit -ne 0) { exit $engineExit }
$nodeCommand = Get-Command node.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1
& $nodeCommand.Source (Join-Path $PSScriptRoot 'sleep-engine-verify.mjs') (Join-Path $EvidenceDir 'sleep-engine-result.json') $hostHash $backendHash $Mode (Join-Path $EvidenceDir 'receipt-audit.json')
exit $LASTEXITCODE
