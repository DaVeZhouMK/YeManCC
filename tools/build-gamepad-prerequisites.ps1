[CmdletBinding()]
param([string]$ProjectRoot = '')
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Path $PSScriptRoot -Parent }
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$project = Join-Path $ProjectRoot 'GamepadPrerequisites\YeManGamepadPrerequisites.csproj'
$publish = Join-Path $ProjectRoot 'GamepadPrerequisites\bin\Release\net10.0-windows\publish-redist'
$sources = foreach ($name in @('Program.cs','AutoSetup.cs','ModulePayloadBootstrap.cs','HidMaestroPackagePreparation.cs','UsbipUpgrade.cs','UsbipUpgradeSelfTest.cs','YeManGamepadPrerequisites.csproj')) {
  [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot ('GamepadPrerequisites\'+$name)) -Algorithm SHA256).Hash }
}
$sources += [ordered]@{ name='native/ProcessEfficiency.cs'; sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/ProcessEfficiency.cs') -Algorithm SHA256).Hash }
$sources += [ordered]@{ name='native/UsbipMaintenanceState.cs'; sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/UsbipMaintenanceState.cs') -Algorithm SHA256).Hash }
$sources += [ordered]@{ name='native/ModuleMaintenanceGate.cs'; sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/ModuleMaintenanceGate.cs') -Algorithm SHA256).Hash }
& dotnet publish $project -c Release --self-contained false --nologo -o $publish
if ($LASTEXITCODE -ne 0) { throw "HIDMaestro setup build failed: $LASTEXITCODE" }
$probe = Join-Path $publish 'HIDMaestroSetup.exe'
if ($env:YEMAN_TEST_EXPORT -ne '1') {
& $probe --selftest
if ($LASTEXITCODE -ne 0) { throw 'First-install pure selftest failed.' }
& $probe --selftest-usbip-upgrade
if ($LASTEXITCODE -ne 0) { throw 'USB/IP version upgrade pure selftest failed.' }
& $probe --check --prepare
if ($LASTEXITCODE -ne 2) { throw 'Conflicting modes did not fail before runtime operations.' }
} else { Write-Output 'GAMEPAD_SETUP_FIXTURES_NOT_RUN_TEST_EXPORT' }
# A source edit during compilation must never be certified as the new binary.
foreach ($entry in $sources) {
  $relative=if($entry.name.StartsWith('native/')){$entry.name}else{'GamepadPrerequisites/'+$entry.name}
  if((Get-FileHash -LiteralPath (Join-Path $ProjectRoot $relative) -Algorithm SHA256).Hash -ine $entry.sha256){throw ('GAMEPAD_SETUP_SOURCE_CHANGED_DURING_BUILD '+$relative)}
}
$dest = Join-Path $ProjectRoot 'PowerControl\redist'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$binaryNames = @('HIDMaestroSetup.exe','HIDMaestroSetup.dll','HIDMaestroSetup.deps.json','HIDMaestroSetup.runtimeconfig.json')
foreach ($name in $binaryNames) { Copy-Item -LiteralPath (Join-Path $publish $name) -Destination (Join-Path $dest $name) -Force }
$files = foreach ($name in $binaryNames) {
  $p = Join-Path $dest $name
  [ordered]@{ name=$name; bytes=(Get-Item -LiteralPath $p).Length; sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
}
[ordered]@{ batch='GP-PREREQ-2'; deviceVerified=$false; source=$sources; files=$files } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dest 'HIDMaestroSetup.manifest.json') -Encoding utf8
Write-Output 'GAMEPAD_REDIST_SETUP_PAYLOAD_OK'
exit 0
