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
# Retire only the former apphost; preserve payload directories and user state.
function Remove-BatteryHostLegacyAppHost([string]$Directory, [switch]$RejectOnly) {
  $rootPath=[IO.Path]::GetFullPath($Directory).TrimEnd('\')
  $legacyPath=[IO.Path]::GetFullPath((Join-Path $rootPath 'YeManBatteryHost.exe'))
  if([IO.Path]::GetDirectoryName($legacyPath) -ine $rootPath -or [IO.Path]::GetFileName($legacyPath) -cne 'YeManBatteryHost.exe') { throw 'Invalid BatteryHost legacy apphost path' }
  if(Test-Path -LiteralPath $legacyPath) {
    if($RejectOnly) { throw "Legacy BatteryHost apphost remains: $legacyPath" }
    if(-not (Test-Path -LiteralPath $legacyPath -PathType Leaf)) { throw "Legacy BatteryHost apphost is not a file: $legacyPath" }
    Remove-Item -LiteralPath $legacyPath -Force
    if(Test-Path -LiteralPath $legacyPath) { throw "Legacy BatteryHost apphost cleanup failed: $legacyPath" }
  }
}
. (Join-Path $PSScriptRoot 'ymcc-shared-runtime\shared-runtime.ps1')
Assert-YmccSharedRuntimeSource $ProjectRoot
& node (Join-Path $PSScriptRoot 'battery-charge-contract.mjs') --check
if($LASTEXITCODE -ne 0){throw 'Battery runtime contract is stale'}
& node (Join-Path $PSScriptRoot 'battery-charge-generate-routes.mjs') --check
if($LASTEXITCODE -ne 0){throw 'Battery runtime allowlist is stale'}
Remove-BatteryHostLegacyAppHost $output
Remove-BatteryHostLegacyAppHost $destination
& dotnet publish (Join-Path $ProjectRoot 'native\BatteryHost\YeManBatteryHost.csproj') -c Release --nologo -o $output
if($LASTEXITCODE -ne 0){throw 'BatteryHost publish failed'}
Remove-BatteryHostLegacyAppHost $output -RejectOnly
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
Remove-BatteryHostLegacyAppHost $destination -RejectOnly
Assert-YmccBatteryHostCopies $ProjectRoot $destination
$contract=Get-Content -LiteralPath (Join-Path $ProjectRoot 'native\BatteryHost\runtime-contract.json') -Raw | ConvertFrom-Json
$manifest=[ordered]@{schema=1;component='YeManBatteryHost';execution='in-process';frameworkDependent=$true;hardwareValidated=$false;contract=$contract;files=$files}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $destination 'battery-host.payload.json')
Write-Output "BATTERY_HOST_BUILD_OK: $destination"
