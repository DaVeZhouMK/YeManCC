[CmdletBinding()]
param([string]$ProjectRoot = '')
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Path $PSScriptRoot -Parent }
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$project = Join-Path $ProjectRoot 'GamepadPrerequisites\YeManGamepadPrerequisites.csproj'
$publish = Join-Path $ProjectRoot 'GamepadPrerequisites\bin\Release\net10.0-windows\publish-redist'
& dotnet publish $project -c Release --self-contained false --nologo -o $publish
if ($LASTEXITCODE -ne 0) { throw "HIDMaestro setup build failed: $LASTEXITCODE" }
$probe = Join-Path $publish 'HIDMaestroSetup.exe'
& $probe --selftest
if ($LASTEXITCODE -ne 0) { throw 'First-install pure selftest failed.' }
& $probe --check --prepare
if ($LASTEXITCODE -ne 2) { throw 'Conflicting modes did not fail before runtime operations.' }
$dest = Join-Path $ProjectRoot 'PowerControl\redist'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$binaryNames = @('HIDMaestroSetup.exe','HIDMaestroSetup.dll','HIDMaestroSetup.deps.json','HIDMaestroSetup.runtimeconfig.json')
foreach ($name in $binaryNames) { Copy-Item -LiteralPath (Join-Path $publish $name) -Destination (Join-Path $dest $name) -Force }
$files = foreach ($name in $binaryNames) {
  $p = Join-Path $dest $name
  [ordered]@{ name=$name; bytes=(Get-Item -LiteralPath $p).Length; sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
}
$sources = foreach ($name in @('Program.cs','AutoSetup.cs','YeManGamepadPrerequisites.csproj')) {
  [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot ('GamepadPrerequisites\'+$name)) -Algorithm SHA256).Hash }
}
[ordered]@{ batch='GP-PREREQ-2'; deviceVerified=$false; source=$sources; files=$files } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dest 'HIDMaestroSetup.manifest.json') -Encoding utf8
Write-Output 'GAMEPAD_REDIST_SETUP_PAYLOAD_OK'
exit 0
