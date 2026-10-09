[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$fan = Join-Path $project 'PowerControl\fan-host'
$runtime = Join-Path $project 'PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
$hostNames = @('YeManFanHost.exe','YeManFanHost.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.json','YeManFanHost.authorization.md','YeManFanHost.payload.json','install-fan-host-payload.ps1','run-emergency-fan-restore.ps1')
if (!(Test-Path $fan -PathType Container)) { throw "FanHost directory missing: $fan" }
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$runtimeItems = @(Get-ChildItem -LiteralPath $fan -File -Force | Where-Object { $_.Name -notin $hostNames })
if ($runtimeItems.Count -eq 0) { throw 'No HC runtime files found' }
foreach ($item in $runtimeItems) {
  Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $runtime $item.Name) -Force
  Remove-Item -LiteralPath $item.FullName -Force
}
function Entry([string]$path) { [ordered]@{ path=$path; sha256=(Get-FileHash -LiteralPath (Join-Path $runtime $path) -Algorithm SHA256).Hash.ToLowerInvariant() } }
$runtimeEntries = @($runtimeItems | Sort-Object Name | ForEach-Object { Entry $_.Name })
[ordered]@{ schemaVersion=1; runtimeId='HC-CANDIDATE-0.32.4.0-06c0b954-20260902'; hcVersion='0.32.4.0'; hcCommit='06c0b9544db2b1f39abf9cd3796225ccfb096103'; files=$runtimeEntries } |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
function HostEntry([string]$path) { [ordered]@{ path=$path; sha256=(Get-FileHash -LiteralPath (Join-Path $fan $path) -Algorithm SHA256).Hash.ToLowerInvariant() } }
$hostEntries = @($hostNames | Sort-Object | ForEach-Object { if (!(Test-Path (Join-Path $fan $_) -PathType Leaf)) { throw "Host file missing: $_" }; HostEntry $_ })
[ordered]@{ schemaVersion=2; runtimeManifest='..\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\HandheldCompanion.runtime.json'; runtimeId='HC-CANDIDATE-0.32.4.0-06c0b954-20260902'; files=$hostEntries } |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fan 'YeManFanHost.payload.json') -Encoding utf8
Write-Output "Migrated runtime files: $($runtimeItems.Count)"
Write-Output "Runtime root: $runtime"
