param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot
)
$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidencePath = [System.IO.Path]::GetFullPath($EvidenceRoot)
$repoPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if ($evidencePath.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $evidencePath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an evidence directory outside the source repository for isolated build outputs.'
}
New-Item -ItemType Directory -Path $evidencePath -Force | Out-Null
$artifactsPath = Join-Path $evidencePath 'build'
$targetsPath = Join-Path $evidencePath 'isolated-build.targets'
# ArtifactsPath moves obj, so exclude pre-existing in-repository obj/bin source globs explicitly.
$targets = [string]::Join([Environment]::NewLine, @(
    '<Project>',
    '  <Target Name="ExcludeOriginalWorkspaceBuildOutputs" BeforeTargets="CoreCompile">',
    '    <ItemGroup>',
    '      <Compile Remove="$(MSBuildProjectDirectory)\bin\**\*.cs" />',
    '      <Compile Remove="$(MSBuildProjectDirectory)\obj\**\*.cs" />',
    '    </ItemGroup>',
    '  </Target>',
    '</Project>'
))
[System.IO.File]::WriteAllText($targetsPath, $targets)
& dotnet build (Join-Path $repoRoot 'FanLab\real-host\YeManFanHost.csproj') --configuration Release --artifacts-path $artifactsPath "-p:CustomAfterMicrosoftCommonTargets=$targetsPath" --nologo 2>&1 |
    Tee-Object -FilePath (Join-Path $evidencePath 'host-build.log')
if ($LASTEXITCODE -ne 0) { throw 'Isolated Host compilation failed.' }
& dotnet build (Join-Path $PSScriptRoot 'FanHcInitializationDiagnosticsSelfTest.csproj') --configuration Release --artifacts-path $artifactsPath --nologo 2>&1 |
    Tee-Object -FilePath (Join-Path $evidencePath 'self-test-build.log')
if ($LASTEXITCODE -ne 0) { throw 'Offline self-test compilation failed.' }
& dotnet (Join-Path $artifactsPath 'bin\FanHcInitializationDiagnosticsSelfTest\release\FanHcInitializationDiagnosticsSelfTest.dll') `
    (Join-Path $artifactsPath 'bin\YeManFanHost\release_win-x64\YeManFanHost.dll') `
    (Join-Path $repoRoot 'FanLab\real-host\Program.cs') (Join-Path $evidencePath 'self-test') 2>&1 |
    Tee-Object -FilePath (Join-Path $evidencePath 'self-test.log')
if ($LASTEXITCODE -ne 0) { throw 'Offline self-test failed.' }