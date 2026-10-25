[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
# Each invocation builds outside the shared checkout with the real helper and
# pinned SDK reference. The executable calls only SelfTest (all I/O injected).
$taskOutput = Join-Path ([IO.Path]::GetTempPath()) ('YMCC-ExternalControllerPortCycle-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskOutput
$project = Join-Path $PSScriptRoot 'external_controller_port_cycle_selftest.csproj'
Write-Output "Isolated build output: $taskOutput"
& dotnet build $project --configuration Release --nologo --verbosity minimal "-p:BaseIntermediateOutputPath=$taskOutput\obj\" "-p:BaseOutputPath=$taskOutput\bin\" -p:TreatWarningsAsErrors=true
if ($LASTEXITCODE -ne 0) { throw "Isolated build failed with exit code $LASTEXITCODE" }
$assembly = Join-Path $taskOutput 'bin\Release\net10.0-windows\external_controller_port_cycle_selftest.dll'
& dotnet $assembly
if ($LASTEXITCODE -ne 0) { throw "Injected self-test failed with exit code $LASTEXITCODE" }
