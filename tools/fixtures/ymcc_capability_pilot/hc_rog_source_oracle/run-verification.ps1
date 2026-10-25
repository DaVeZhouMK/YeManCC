param(
    [string]$ArtifactsRoot = 'D:\Codex\2026-10-18\kai-s\work\fan-pilot\oracle-prep\hc-rog-source-oracle-artifacts',
    [switch]$ValidatePathsOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Build/cache/temp files stay in the dedicated D: prep directory; reports stay in this fixture.
$fixtureRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$allowedArtifactRoot = [System.IO.Path]::GetFullPath('D:\Codex\2026-10-18\kai-s\work\fan-pilot\oracle-prep\hc-rog-source-oracle-artifacts')
$artifactRoot = [System.IO.Path]::GetFullPath($ArtifactsRoot)
if ($artifactRoot -ne $allowedArtifactRoot -and
    -not $artifactRoot.StartsWith($allowedArtifactRoot + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Artifacts must remain in the dedicated oracle-prep/hc-rog-source-oracle-artifacts directory.'
}
if (Test-Path -LiteralPath $artifactRoot) {
    $artifactItem = Get-Item -LiteralPath $artifactRoot -Force
    if (($artifactItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Artifact output cannot be a reparse point.'
    }
}
$evidenceRoot = Join-Path $fixtureRoot 'results'
if ($ValidatePathsOnly) {
    [pscustomobject]@{ FixtureRoot = $fixtureRoot; ArtifactRoot = $artifactRoot; EvidenceRoot = $evidenceRoot; TestsExecuted = $false }
    return
}
[void](New-Item -ItemType Directory -Path $artifactRoot -Force)
[void](New-Item -ItemType Directory -Path $evidenceRoot -Force)

$isolatedEnvironment = @{
    DOTNET_CLI_HOME = (Join-Path $artifactRoot 'dotnet-home')
    DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
    DOTNET_ADD_GLOBAL_TOOLS_TO_PATH = '0'
    DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
    DOTNET_CLI_USE_MSBUILD_SERVER = '0'
    DOTNET_NOLOGO = '1'
    MSBUILDDISABLENODEREUSE = '1'
    NUGET_PACKAGES = (Join-Path $artifactRoot 'nuget-packages')
    NUGET_HTTP_CACHE_PATH = (Join-Path $artifactRoot 'nuget-http-cache')
    NUGET_PLUGINS_CACHE_PATH = (Join-Path $artifactRoot 'nuget-plugins-cache')
    TEMP = (Join-Path $artifactRoot 'tmp')
    TMP = (Join-Path $artifactRoot 'tmp')
    PYTHONDONTWRITEBYTECODE = '1'
}
$previousEnvironment = @{}
foreach ($environmentName in $isolatedEnvironment.Keys) {
    $previousEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName, 'Process')
    [Environment]::SetEnvironmentVariable($environmentName, $isolatedEnvironment[$environmentName], 'Process')
}
foreach ($directoryName in @('dotnet-home', 'nuget-packages', 'nuget-http-cache', 'nuget-plugins-cache', 'tmp')) {
    [void](New-Item -ItemType Directory -Path (Join-Path $artifactRoot $directoryName) -Force)
}

try {
    $generator = Join-Path $fixtureRoot 'generate_hc_rog_source_oracle.py'
    & python $generator --check 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'source-extraction-check.log')
    if ($LASTEXITCODE -ne 0) { throw 'Pinned source extraction verification failed.' }

    $verificationProject = Join-Path $fixtureRoot 'Verification\HcRogSourceOracle.Verification.csproj'
    & dotnet build $verificationProject --configuration Release --artifacts-path (Join-Path $artifactRoot 'build') --disable-build-servers --configfile (Join-Path $fixtureRoot 'NuGet.Config') -p:NuGetAudit=false -p:UseSharedCompilation=false --verbosity minimal 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Standalone C# oracle/verification build failed.' }

    $verificationBinaries = @(Get-ChildItem -LiteralPath (Join-Path $artifactRoot 'build\bin') -Recurse -File -Filter 'HcRogSourceOracle.Verification.dll')
    if ($verificationBinaries.Count -ne 1) { throw 'Expected exactly one built standalone verification executable.' }
    & dotnet $verificationBinaries[0].FullName $fixtureRoot $evidenceRoot 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'verification.log')
    if ($LASTEXITCODE -ne 0) { throw 'Source-reference parity checks failed; inspect oracle-parity-results.json.' }

    & python $generator --check 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'source-extraction-postcheck.log')
    if ($LASTEXITCODE -ne 0) { throw 'Pinned source extraction changed during verification.' }
}
finally {
    foreach ($environmentName in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($environmentName, $previousEnvironment[$environmentName], 'Process')
    }
}