[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$out=[IO.Path]::GetFullPath($EvidenceDir)
if($out -eq $repo -or $out.StartsWith($repo.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Use evidence outside the source repository'}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$build=Join-Path $out 'build'
$targets=Join-Path $out 'isolated.targets'
$xml='<Project><Target Name="ExcludeOriginalOutputs" BeforeTargets="CoreCompile"><ItemGroup><Compile Remove="$(MSBuildProjectDirectory)/bin/**/*.cs"/><Compile Remove="$(MSBuildProjectDirectory)/obj/**/*.cs"/></ItemGroup></Target></Project>'
Set-Content -LiteralPath $targets -Value $xml -Encoding utf8
Push-Location -LiteralPath $repo
try {
 & node tools/hc_capability_catalog_generate.mjs --check --out (Join-Path $out 'generation') 2>&1 | Tee-Object -FilePath (Join-Path $out 'generation.log')
 if($LASTEXITCODE -ne 0){throw 'Unified catalog is stale'}
 & dotnet build tools/HcLightingCapabilitySelfTest/HcLightingCapabilitySelfTest.csproj -c Release --artifacts-path $build "-p:CustomAfterMicrosoftCommonTargets=$targets" --nologo -v:q 2>&1 | Tee-Object -FilePath (Join-Path $out 'build.log')
 if($LASTEXITCODE -ne 0){throw 'Actual lighting consumer fixture failed to compile'}
 & dotnet (Join-Path $build 'bin/HcLightingCapabilitySelfTest/release/HcLightingCapabilitySelfTest.dll') (Join-Path $repo 'capabilities/hc-device-catalog.json') (Join-Path $out 'result.json') 2>&1 | Tee-Object -FilePath (Join-Path $out 'consumer.log')
 if($LASTEXITCODE -ne 0){throw 'Actual lighting consumer failed'}
 & node tools/hc_lighting_native_parity_selftest.mjs --out (Join-Path $out 'native') 2>&1 | Tee-Object -FilePath (Join-Path $out 'native.log')
 if($LASTEXITCODE -ne 0){throw 'Native lighting parity failed'}
 & node tools/hc_lighting_native_consumer_selftest.mjs --out (Join-Path $out 'native-consumers') 2>&1 | Tee-Object -FilePath (Join-Path $out 'native-consumers.log')
 if($LASTEXITCODE -ne 0){throw 'Actual native packet consumer failed'}
 & node tools/hc_lighting_native_feedback_selftest.mjs --out (Join-Path $out 'native-feedback') 2>&1 | Tee-Object -FilePath (Join-Path $out 'native-feedback.log')
 if($LASTEXITCODE -ne 0){throw 'Actual native feedback/receipt consumer failed'}
} finally {Pop-Location}
