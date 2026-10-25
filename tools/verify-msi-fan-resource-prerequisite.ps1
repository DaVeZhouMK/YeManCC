[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot,[string]$HcAssemblyPath)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(!$HcAssemblyPath) {
    $candidates=@(Get-ChildItem -LiteralPath "$repo\PowerControl\handheldcompanion-runtime" -Filter HandheldCompanion.dll -Recurse)
    if($candidates.Count -ne 1){throw 'Supply exact HC candidate path; never guess among multiple runtime assemblies'}
    $HcAssemblyPath=$candidates[0].FullName
}
$HcAssemblyPath=[IO.Path]::GetFullPath($HcAssemblyPath)
$project=Join-Path $OutputRoot 'resource-harness'
New-Item -ItemType Directory -Path "$project\Resources" -Force|Out-Null
$payload=Join-Path $repo 'deps\handheldcompanion-runtime\source\Resources\msiapcfg.dll'
Copy-Item -LiteralPath $payload -Destination "$project\Resources\msiapcfg.dll" -Force
$helper=Join-Path $repo 'FanLab\real-host\MsiWmiPayloadResource.cs'
$test=Join-Path $repo 'tools\msi_fan_resource_prerequisite_selftest.cs'
$before=@($helper,$test,$payload,$HcAssemblyPath|ForEach-Object{Get-FileHash -LiteralPath $_ -Algorithm SHA256|Select-Object Path,Hash})
$escapedHelper=[Security.SecurityElement]::Escape($helper)
$escapedTest=[Security.SecurityElement]::Escape($test)
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><UseWPF>true</UseWPF><AssemblyName>HandheldCompanion</AssemblyName><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup>
  <ItemGroup><Compile Include="$escapedHelper" Link="MsiWmiPayloadResource.cs"/><Compile Include="$escapedTest" Link="Program.cs"/><Resource Include="Resources\msiapcfg.dll"/></ItemGroup>
</Project>
"@|Set-Content -LiteralPath "$project\ResourceHarness.csproj" -Encoding UTF8
& dotnet build "$project\ResourceHarness.csproj" -c Release --nologo
if($LASTEXITCODE -ne 0){throw 'MSI vendor resource helper fixture compile failed'}
$run=Join-Path $OutputRoot ('isolated-run-'+[Guid]::NewGuid().ToString('N'))
& dotnet "$project\bin\Release\net10.0-windows\HandheldCompanion.dll" $run $payload $HcAssemblyPath
if($LASTEXITCODE -ne 0){throw 'MSI fan existing prerequisite/helper verification failed'}
$after=@($helper,$test,$payload,$HcAssemblyPath|ForEach-Object{Get-FileHash -LiteralPath $_ -Algorithm SHA256|Select-Object Path,Hash})
for($i=0;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw 'Source/runtime changed during verification; repeat against stable files'}}
Copy-Item -LiteralPath "$run\msi-fan-resource-prerequisite-result.json" -Destination "$OutputRoot\msi-fan-resource-prerequisite-result.json" -Force
$after|ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\source-runtime-hashes.json" -Encoding UTF8
Write-Output "MSI_FAN_RESOURCE_EXISTING_REPAIR_OK $OutputRoot"
