$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$output = 'C:\SOFT\YeManCC-Work\Build\Validation\fan-host-real-test-20260821\YeManFanHost-safe'

dotnet publish (Join-Path $PSScriptRoot 'YeManFanHost.csproj') `
  -c Release -r win-x64 --self-contained true `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -p:DebugType=None -o $output

$exe = Join-Path $output 'YeManFanHost.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Publish did not produce $exe" }
& $exe --self-test
if ($LASTEXITCODE -ne 0) { throw "YeManFanHost safe self-test failed: $LASTEXITCODE" }
Write-Host "Safe host published: $exe"
