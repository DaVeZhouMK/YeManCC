[CmdletBinding()]
param([string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\MSI-Host-Stage-R8-20261013\producer')
$ErrorActionPreference = 'Stop'
$allowed = 'G:\YeManCC-Work\Build\Validation\MSI-Host-Stage-R8-20261013\producer'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if (-not ($OutputRoot.Equals($allowed, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputRoot.StartsWith($allowed + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw 'Standalone build/results must remain under the authorized R8 producer directory.'
}
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$encoding = New-Object Text.UTF8Encoding($false)
function Write-Utf8([string]$Path, [string]$Value) { [IO.File]::WriteAllText($Path, $Value, $encoding) }
function Quote-Argument([string]$Value) {
    # Standard Windows argv escaping for ProcessStartInfo on Windows PowerShell.
    $escaped = [regex]::Replace($Value, '(\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Run-OfflineProcess([string]$Exe, [string[]]$Arguments, [string]$LogBase, [string]$WorkingDirectory) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = (($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    # Keep CLI home and NuGet restore cache private to this fixture run.
    $psi.EnvironmentVariables['DOTNET_CLI_HOME'] = (Join-Path $runRoot 'cli-home')
    $psi.EnvironmentVariables['NUGET_PACKAGES'] = (Join-Path $runRoot 'packages')
    $psi.EnvironmentVariables['DOTNET_SKIP_FIRST_TIME_EXPERIENCE'] = '1'
    $psi.EnvironmentVariables['DOTNET_CLI_TELEMETRY_OPTOUT'] = '1'
    $process = [Diagnostics.Process]::Start($psi)
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            throw 'Own standalone fixture/build exceeded 60 seconds; only that child process was terminated.'
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        Write-Utf8 ($LogBase + '.stdout.txt') $stdout
        Write-Utf8 ($LogBase + '.stderr.txt') $stderr
        return [pscustomobject]@{ exitCode = $process.ExitCode; stdout = $stdout; stderr = $stderr }
    }
    finally { $process.Dispose() }
}

$sourcePaths = @(
    (Join-Path $repo 'InputHost\MsiHostStageEvidence.cs'),
    (Join-Path $repo 'tools\msi_host_stage_evidence_selftest.cs'),
    (Join-Path $repo 'tools\msi_host_stage_contract_v1.json'),
    $PSCommandPath
)
$testSource = [IO.File]::ReadAllText($sourcePaths[1])
if ([regex]::IsMatch($testSource, 'MsiHostStageEvidence\s*\.\s*TryCreate\s*\(')) {
    throw 'Fixture must never invoke the real mapping factory.'
}
$producerSource = [IO.File]::ReadAllText($sourcePaths[0])
if ([regex]::Matches($producerSource, 'Thread\.MemoryBarrier\(\)').Count -ne 2 -or
    [regex]::Matches($producerSource, 'MemoryMappedFile\.CreateNew\(').Count -ne 1 -or
    [regex]::IsMatch($producerSource, 'MemoryMappedFile\.OpenExisting\(')) {
    throw 'Producer publication/factory source audit failed.'
}
$runRoot = Join-Path $OutputRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$snapshot = Join-Path $runRoot 'snapshot'
New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
$binding = @()
foreach ($path in $sourcePaths) {
    $beforeHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $copy = Join-Path $snapshot ([IO.Path]::GetFileName($path))
    Copy-Item -LiteralPath $path -Destination $copy
    $copyHash = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
    if ($copyHash -ne $beforeHash) { throw "Source changed during snapshot: $path" }
    $binding += [pscustomobject]@{ source = $path; snapshot = $copy; sha256 = $copyHash }
}
Write-Utf8 (Join-Path $runRoot 'source-binding.json') ($binding | ConvertTo-Json -Depth 5)
$project = Join-Path $snapshot 'MsiHostStageEvidenceSynthetic.csproj'
$projectXml = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>MsiHostStageEvidenceSynthetic</AssemblyName>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <ImplicitUsings>disable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <PlatformTarget>x64</PlatformTarget>
    <NuGetAudit>false</NuGetAudit>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="MsiHostStageEvidence.cs" />
    <Compile Include="msi_host_stage_evidence_selftest.cs" />
  </ItemGroup>
</Project>
"@
Write-Utf8 $project $projectXml
$nuget = Join-Path $snapshot 'NuGet.Config'
Write-Utf8 $nuget '<configuration><packageSources><clear /></packageSources></configuration>'
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$sdk = Run-OfflineProcess $dotnet @('--version') (Join-Path $runRoot 'sdk') $snapshot
if ($sdk.exitCode -ne 0) { throw 'Cannot determine SDK.' }
$sdkVersion = $sdk.stdout.Trim()
Write-Utf8 (Join-Path $snapshot 'global.json') (@{ sdk = @{ version = $sdkVersion; rollForward = 'disable' } } | ConvertTo-Json)
$obj = (Join-Path $runRoot 'obj').Replace('\', '/') + '/'
$bin = (Join-Path $runRoot 'bin').Replace('\', '/') + '/'
$buildArgs = @('build', $project, '-c', 'Release', '--nologo', '--configfile', $nuget,
    ('-p:BaseIntermediateOutputPath=' + $obj), ('-p:MSBuildProjectExtensionsPath=' + $obj),
    ('-p:BaseOutputPath=' + $bin), '-p:ImportDirectoryBuildProps=false',
    '-p:ImportDirectoryBuildTargets=false', '-p:ImportDirectoryPackagesProps=false')
$build = Run-OfflineProcess $dotnet $buildArgs (Join-Path $runRoot 'build') $snapshot
if ($build.exitCode -ne 0) { throw "Standalone build failed: $($build.stdout) $($build.stderr)" }
$binary = Join-Path $runRoot 'bin\Release\net10.0\MsiHostStageEvidenceSynthetic.dll'
$resultPath = Join-Path $runRoot 'synthetic-result.json'
$fixtureRoot = Join-Path $runRoot 'interop-fixture'
$execute = Run-OfflineProcess $dotnet @($binary, (Join-Path $snapshot 'msi_host_stage_contract_v1.json'), $resultPath, $fixtureRoot) (Join-Path $runRoot 'fixture') $snapshot
if (-not (Test-Path -LiteralPath $resultPath)) { throw "Fixture produced no result: $($execute.stderr)" }
$result = [IO.File]::ReadAllText($resultPath) | ConvertFrom-Json
$after = @()
$stable = $true
foreach ($item in $binding) {
    $hash = (Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash
    $after += [pscustomobject]@{ source = $item.source; sha256 = $hash }
    if ($hash -ne $item.sha256) { $stable = $false }
    if ((Get-FileHash -LiteralPath $item.snapshot -Algorithm SHA256).Hash -ne $item.sha256) { $stable = $false }
}
Write-Utf8 (Join-Path $runRoot 'source-hashes-after.json') ($after | ConvertTo-Json -Depth 5)
$wireFixture = Join-Path $fixtureRoot 'msi-host-stage-beginsubmit-1024.bin'
$expectedIdentity = Join-Path $fixtureRoot 'msi-host-stage-beginsubmit-expected.json'
if (-not (Test-Path -LiteralPath $wireFixture) -or -not (Test-Path -LiteralPath $expectedIdentity)) { throw 'C# fixture did not produce both requested interop evidence files.' }
$summary = [pscustomobject]@{
    passed = ($execute.exitCode -eq 0 -and $result.passed -and $stable)
    buildExitCode = $build.exitCode; fixtureExitCode = $execute.exitCode
    sdkVersion = $sdkVersion; targetFramework = 'net10.0'; sourceStable = $stable
    compiledSources = @('MsiHostStageEvidence.cs', 'msi_host_stage_evidence_selftest.cs')
    sourceBinding = $binding; projectSha256 = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
    binary = $binary; binarySha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    resultPath = $resultPath; resultSha256 = (Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash
    wireFixture = $wireFixture; wireFixtureBytes = (Get-Item -LiteralPath $wireFixture).Length; wireFixtureSha256 = (Get-FileHash -LiteralPath $wireFixture -Algorithm SHA256).Hash
    expectedIdentity = $expectedIdentity; expectedIdentitySha256 = (Get-FileHash -LiteralPath $expectedIdentity -Algorithm SHA256).Hash
    caseCount = $result.caseCount; checkCount = $result.checkCount; failed = $result.failed
    checkpointAllocationBytes = $result.checkpointAllocationBytes
    scope = $result.scope; allocationScope = $result.allocationScope
    realMappingCreationCalls = $result.realMappingCreationCalls
    inputHostProductionAssemblyLoaded = $result.inputHostProductionAssemblyLoaded
    productionOrHostBuildExecuted = $false; productionOrHostExecuted = $false
    outputRoot = $runRoot
}
Write-Utf8 (Join-Path $runRoot 'verification-summary.json') ($summary | ConvertTo-Json -Depth 8)
$summary | ConvertTo-Json -Depth 8
if (-not $summary.passed) { throw "Synthetic producer verification failed; inspect $runRoot" }
Write-Output "MSI_HOST_STAGE_PRODUCER_SYNTHETIC_OK $runRoot"
