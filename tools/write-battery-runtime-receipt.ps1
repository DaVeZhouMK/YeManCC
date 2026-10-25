# Called only after the full release archive/source identity gates have passed.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReleaseRoot,[Parameter(Mandatory=$true)][string]$BuildId)
$ErrorActionPreference='Stop'
$manifest=Get-Content -LiteralPath (Join-Path $ReleaseRoot 'PowerControl\battery-host\battery-host.payload.json') -Raw | ConvertFrom-Json
if(-not $manifest.contract -or $manifest.contract.protocol -ne 2){throw 'Release lacks the paired battery runtime contract'}
$relative=@('YeManCC/YeManCC.exe','YeManCC/index.html','PowerControl/battery-host/battery-host.payload.json')
$relative+=@($manifest.files | ForEach-Object {'PowerControl/battery-host/'+$_.path})
$files=@(foreach($path in $relative){$full=Join-Path $ReleaseRoot $path;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()}})
$receipt=[ordered]@{schema=1;buildId=$BuildId;protocol=$manifest.contract.protocol;sourceId=$manifest.contract.sourceId;files=$files}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ReleaseRoot 'battery-runtime.expected.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verify-battery-runtime.ps1') -Destination $ReleaseRoot -Force
Write-Output 'BATTERY_DELIVERY_RECEIPT_OK: native, web entry and complete helper payload bound; verifier is read-only.'
