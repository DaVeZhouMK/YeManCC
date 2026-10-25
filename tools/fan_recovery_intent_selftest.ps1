[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot, [string]$HostAssembly='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$repo=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($HostAssembly)){ $HostAssembly=Join-Path $repo 'PowerControl\fan-host\YeManFanHost.dll' }
$hostPath=(Resolve-Path -LiteralPath $HostAssembly).Path
$hostSha=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path $root -Force | Out-Null
& dotnet run --project (Join-Path $PSScriptRoot 'FanRecoveryIntentSelfTest\FanRecoveryIntentSelfTest.csproj') -c Release --artifacts-path (Join-Path $root 'build') -- (Join-Path $root 'run') $hostPath
if($LASTEXITCODE -ne 0){ throw 'Fan recovery-intent self-test failed' }
$result=Get-Content -LiteralPath (Join-Path $root 'run\result.json') -Raw | ConvertFrom-Json
if(-not $result.ok -or $result.count -lt 51 -or $result.hostAssemblySha256 -ne $hostSha){throw 'Fan recovery-intent result incomplete or different payload'}
if((Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hostSha){throw 'Host payload changed during test'}
Write-Output 'fan recovery intent self-test: PASS (51 checks; actual Host persistence and startup, mock hardware)'