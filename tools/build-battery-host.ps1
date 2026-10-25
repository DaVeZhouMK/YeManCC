[CmdletBinding()]
param([string]$ProjectRoot='', [string]$WorkspaceRoot=$env:YEMAN_WORKSPACE_ROOT)
$ErrorActionPreference='Stop'
if(-not $ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
if (-not $WorkspaceRoot) { $WorkspaceRoot=Join-Path $ProjectRoot '..\..' }
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$WorkspaceRoot=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
$output=Join-Path $WorkspaceRoot 'Build\App\BatteryHost'
$destination=Join-Path $ProjectRoot 'PowerControl\battery-host'
. (Join-Path $PSScriptRoot 'ymcc-shared-runtime\shared-runtime.ps1')
Assert-YmccSharedRuntimeSource $ProjectRoot
& node (Join-Path $PSScriptRoot 'battery-charge-contract.mjs') --check
if($LASTEXITCODE -ne 0){throw 'Battery runtime contract is stale'}
& node (Join-Path $PSScriptRoot 'battery-charge-generate-routes.mjs') --check
if($LASTEXITCODE -ne 0){throw 'Battery runtime allowlist is stale'}
& dotnet publish (Join-Path $ProjectRoot 'native\BatteryHost\YeManBatteryHost.csproj') -c Release --nologo -o $output
if($LASTEXITCODE -ne 0){throw 'BatteryHost publish failed'}
Assert-YmccBatteryHostCopies $ProjectRoot $output
$payloadFiles=(Get-Content -LiteralPath (Join-Path $ProjectRoot 'native\BatteryHost\payload-files.json') -Raw | ConvertFrom-Json)
foreach($name in @('THIRD-PARTY-NOTICES.md','LICENSE.LibreHardwareMonitor.txt','LICENSE.System.Management.txt')) {
  Copy-Item -LiteralPath (Join-Path $ProjectRoot ('native\BatteryHost\'+$name)) -Destination $output -Force
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$files=@(foreach($name in $payloadFiles) {
  if($name -eq 'battery-host.payload.json'){continue}
  $source=Join-Path $output $name
  if(-not (Test-Path -LiteralPath $source -PathType Leaf)){throw "BatteryHost dependency missing: $name"}
  Copy-Item -LiteralPath $source -Destination $destination -Force
  $sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
  if((Get-FileHash -LiteralPath (Join-Path $destination $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sourceHash){throw "Battery staging mismatch: $name"}
  [ordered]@{path=$name;sha256=$sourceHash;bytes=(Get-Item -LiteralPath $source).Length}
})
Assert-YmccBatteryHostCopies $ProjectRoot $destination
$contract=Get-Content -LiteralPath (Join-Path $ProjectRoot 'native\BatteryHost\runtime-contract.json') -Raw | ConvertFrom-Json
$manifest=[ordered]@{schema=1;component='YeManBatteryHost';frameworkDependent=$true;hardwareValidated=$false;contract=$contract;files=$files}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $destination 'battery-host.payload.json')
Write-Output "BATTERY_HOST_BUILD_OK: $destination"
