[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[Parameter(Mandatory=$true)][string]$HostAssembly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$hostPath=(Resolve-Path -LiteralPath $HostAssembly).Path
$hostSha=(Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path $root -Force | Out-Null
& dotnet run --project (Join-Path $PSScriptRoot 'FanStaAdmissionSelfTest\FanStaAdmissionSelfTest.csproj') -c Release --artifacts-path (Join-Path $root 'build') -- $hostPath (Join-Path $root 'run')
if($LASTEXITCODE -ne 0){throw 'Fan STA admission regression failed'}
$result=Get-Content -LiteralPath (Join-Path $root 'run\result.json') -Raw | ConvertFrom-Json
if(-not $result.ok -or $result.checks -ne 51 -or $result.hostAssemblySha256 -ne $hostSha){throw 'Fan STA gate is incomplete or targets another Host'}
if((Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hostSha){throw 'Host changed during STA gate'}
Write-Output 'fan STA admission: PASS (51 checks; real engine and STA, fixture session/owner actions, no physical proof)'