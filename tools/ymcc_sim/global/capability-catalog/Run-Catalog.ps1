[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir, [string]$BaselineProgram = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$out = [IO.Path]::GetFullPath($EvidenceDir)
if ($out -eq $repo -or $out.StartsWith($repo.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Use evidence outside the source repository' }
New-Item -ItemType Directory -Path $out -Force | Out-Null
$artifacts = Join-Path $out 'build'; $targets = Join-Path $out 'isolated.targets'
$targetsXml = '<Project><Target Name="ExcludeWorkspaceOutputs" BeforeTargets="CoreCompile"><ItemGroup><Compile Remove="$(MSBuildProjectDirectory)/bin/**/*.cs"/><Compile Remove="$(MSBuildProjectDirectory)/obj/**/*.cs"/></ItemGroup><ItemGroup Condition="&apos;$(YmccCapabilityHostProgram)&apos; != &apos;&apos; and &apos;$(MSBuildProjectName)&apos; == &apos;YeManFanHost&apos;"><Compile Remove="Program.cs"/><Compile Remove="$(MSBuildProjectDirectory)/Program.cs"/><Compile Include="$(YmccCapabilityHostProgram)"/></ItemGroup></Target></Project>'
Set-Content -LiteralPath $targets -Value $targetsXml -Encoding utf8
$captureGlobalEvidence = $env:YMCC_SIM_GLOBAL_RUN -eq '1'
$evidenceHelper = Join-Path $PSScriptRoot 'output-evidence.mjs'
if ($captureGlobalEvidence) {
 & node $evidenceHelper capture --case fan.unified-capability-dispatch --evidence-dir $out
 if ($LASTEXITCODE -ne 0) { throw 'Capability input capture failed' }
}
Push-Location -LiteralPath $repo
try {
 & node tools/hc_capability_catalog_generate.mjs --check --out (Join-Path $out 'catalog-check') 2>&1 | Tee-Object -FilePath (Join-Path $out 'generation.log')
 if ($LASTEXITCODE -ne 0) { throw 'Generated capability catalog is stale' }
 & node tools/hc_capability_catalog_selftest.mjs --out (Join-Path $out 'integrity') 2>&1 | Tee-Object -FilePath (Join-Path $out 'integrity.log')
 if ($LASTEXITCODE -ne 0) { throw 'Capability catalog corruption checks failed' }
 & node tools/hc_capability_maintenance_selftest.mjs --out (Join-Path $out 'maintenance-checks') 2>&1 | Tee-Object -FilePath (Join-Path $out 'maintenance-checks.log')
 if ($LASTEXITCODE -ne 0) { throw 'Capability maintenance route/candidate checks failed' }
 $hostArgs = @('build','FanLab/real-host/YeManFanHost.csproj','-c','Release','--artifacts-path',$artifacts,"-p:CustomAfterMicrosoftCommonTargets=$targets",'--nologo','-v:q')
 if ($BaselineProgram) { $hostArgs += "-p:YmccCapabilityHostProgram=$([IO.Path]::GetFullPath($BaselineProgram))" }
 & dotnet @hostArgs 2>&1 | Tee-Object -FilePath (Join-Path $out 'host-build.log')
 if ($LASTEXITCODE -ne 0) { throw 'Actual Host build failed' }
 & dotnet build tools/HcCapabilityCatalogSelfTest/HcCapabilityCatalogSelfTest.csproj -c Release --artifacts-path $artifacts "-p:CustomAfterMicrosoftCommonTargets=$targets" --nologo -v:q 2>&1 | Tee-Object -FilePath (Join-Path $out 'runner-build.log')
 if ($LASTEXITCODE -ne 0) { throw 'Capability consumer regression build failed' }
 & dotnet (Join-Path $artifacts 'bin/HcCapabilityCatalogSelfTest/release/HcCapabilityCatalogSelfTest.dll') (Join-Path $artifacts 'bin/YeManFanHost/release_win-x64/YeManFanHost.dll') (Join-Path $repo 'capabilities/hc-device-catalog.json') (Join-Path $out 'result.json') 2>&1 | Tee-Object -FilePath (Join-Path $out 'consumer.log')
 if ($LASTEXITCODE -ne 0) { throw 'Capability consumer regression failed' }
 & dotnet build tools/HcCapabilityHotPathSelfTest/HcCapabilityHotPathSelfTest.csproj -c Release --artifacts-path $artifacts "-p:CustomAfterMicrosoftCommonTargets=$targets" --nologo -v:q 2>&1 | Tee-Object -FilePath (Join-Path $out 'hotpath-build.log')
 if ($LASTEXITCODE -ne 0) { throw 'Hot route consumer build failed' }
 & dotnet (Join-Path $artifacts 'bin/HcCapabilityHotPathSelfTest/release/HcCapabilityHotPathSelfTest.dll') (Join-Path $artifacts 'bin/YeManFanHost/release_win-x64/YeManFanHost.dll') (Join-Path $out 'hotpath.json') 2>&1 | Tee-Object -FilePath (Join-Path $out 'hotpath.log')
 if ($LASTEXITCODE -ne 0) { throw 'Hot route rebind/cache consumer failed' }
 if ($captureGlobalEvidence) {
  & node $evidenceHelper seal --case fan.unified-capability-dispatch --evidence-dir $out
  if ($LASTEXITCODE -ne 0) { throw 'Capability output/source evidence closure failed' }
 }
} finally { Pop-Location }
