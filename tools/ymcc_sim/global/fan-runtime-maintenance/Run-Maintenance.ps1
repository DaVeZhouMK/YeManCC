[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$out = [IO.Path]::GetFullPath($EvidenceDir)
if ($env:YMCC_SIM_GLOBAL_RUN -ne '1') { throw 'Existing ymcc_sim global guarded run required' }
if ($out -eq $repo -or $out.StartsWith($repo.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Use fresh evidence outside the source repository' }
if (Test-Path -LiteralPath (Join-Path $out 'result.json')) { throw 'Fresh evidence directory required' }
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Get-SourceInputs {
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($folder in @('FanLab/real-host','FanLab/fan-backend')) {
        $prefix = Join-Path $repo $folder
        foreach ($file in Get-ChildItem -LiteralPath $prefix -Recurse -File) {
            $relative = [IO.Path]::GetRelativePath($prefix, $file.FullName)
            if (($relative -split '[\\/]') -contains 'bin' -or ($relative -split '[\\/]') -contains 'obj') { continue }
            if ($file.Extension -in @('.cs','.csproj','.props','.targets','.json')) { $paths.Add($file.FullName) }
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo 'capabilities') -File -Filter '*.cs') { $paths.Add($file.FullName) }
    foreach ($relative in @('FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs','native/ProcessEfficiency.cs',
        'tools/fan_runtime_maintenance_isolation_selftest.ps1','tools/fan_runtime_maintenance_isolation_selftest.cs',
        'tools/fan_maintenance_result_buffers_selftest.ps1','tools/fan_maintenance_result_buffers_selftest.cs',
        'tools/ymcc_sim/global/fan-runtime-maintenance/Run-Maintenance.ps1','tools/ymcc_sim/global/fan-runtime-maintenance/adapter.mjs',
        'tools/ymcc_sim/global/fan-runtime-maintenance/buffer-shape-contract.mjs')) {
        $paths.Add((Join-Path $repo $relative))
    }
    # Include optional root build configuration without inventing absent inputs.
    foreach ($relative in @('global.json','Directory.Build.props','Directory.Build.targets','FanLab/Directory.Build.props','FanLab/Directory.Build.targets','NuGet.Config')) {
        $file = Join-Path $repo $relative
        if (Test-Path -LiteralPath $file -PathType Leaf) { $paths.Add($file) }
    }
    @($paths | Sort-Object -Unique)
}
function Hash-File([string]$file) {
    $item = Get-Item -LiteralPath $file
    if (-not $item.PSIsContainer -and (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)) {
        return (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    throw "Non-regular source/artifact input: $file"
}
function Write-Json([string]$file, $value) {
    [IO.File]::WriteAllText($file, ($value | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
}

$result = [ordered]@{ schemaVersion=1; kind='fan-source-current-runtime-maintenance'; status='FAIL';
    provenance='fresh-current-mainline-Host-project-build'; actualHostMethods=$true; hostMainStarted=$false;
    HCAssemblyLoaded=$false; hardwareCalls=0; operatingSystemSleepPerformed=$false; installedApplicationMutated=$false;
    candidateInstalled=$false; releaseApproved=$false; cpuBenchmarkPerformed=$false; sourceChanges=@();
    checks=@(); sourceHashes=@(); artifactHashes=@(); issues=@(); scope='Actual runtime diagnostic/maintenance consumers; in-memory fake device, diagnostic fault injection; no real HC/Host Main/device IO or CPU benchmark' }
$before = @()
try {
    $paths = Get-SourceInputs
    $before = @(foreach ($file in $paths) {
        $relative = [IO.Path]::GetRelativePath($repo, $file).Replace('\','/')
        $copy = Join-Path (Join-Path $out 'input-sources') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
        Copy-Item -LiteralPath $file -Destination $copy
        $hash = Hash-File $file
        if ((Hash-File $copy) -ne $hash) { throw "Source copy differs: $relative" }
        [pscustomobject]@{ path=$relative; sha256=$hash; snapshotPath=$copy }
    })
    Write-Json (Join-Path $out 'inputs-before.json') $before
    $result.sourceHashes = $before
    # Explicit source-contract only: catches eager result-buffer regression; not allocation proof.
    & node (Join-Path $repo 'tools/ymcc_sim/global/fan-runtime-maintenance/buffer-shape-contract.mjs') --result (Join-Path $out 'buffer-shape-contract.json') *> (Join-Path $out 'buffer-shape-contract.log')
    if ($LASTEXITCODE -ne 0) { throw 'Result-buffer source shape contract failed' }
    $shape = Get-Content -LiteralPath (Join-Path $out 'buffer-shape-contract.json') -Raw | ConvertFrom-Json
    if (-not $shape.ok -or $shape.scope -ne 'source-contract' -or $shape.checks -ne 14 -or $shape.allocationBehaviorProven -ne $false -or @($shape.cases | Where-Object { $_.ok -ne $true }).Count -ne 0) { throw 'Result-buffer source shape receipt incomplete' }
    $result.checks = @([pscustomobject]@{ name='buffer-lazy-source-contract'; status='PASS'; checks=$shape.checks; scope='source-contract'; allocationBehaviorProven=$false; originalReport='buffer-shape-contract.json' })
    $artifacts = Join-Path $out 'build'
    $targets = Join-Path $out 'isolated.targets'
    # Same source-output exclusion as the existing Run-Catalog project build.
    [IO.File]::WriteAllText($targets, '<Project><Target Name="ExcludeWorkspaceOutputs" BeforeTargets="CoreCompile"><ItemGroup><Compile Remove="$(MSBuildProjectDirectory)/bin/**/*.cs"/><Compile Remove="$(MSBuildProjectDirectory)/obj/**/*.cs"/></ItemGroup></Target></Project>', [Text.UTF8Encoding]::new($false))
    $buildArguments = @('build', 'FanLab/real-host/YeManFanHost.csproj', '-c', 'Release', '--artifacts-path', $artifacts,
        "-p:CustomAfterMicrosoftCommonTargets=$targets", '--nologo', '-v:q')
    Write-Json (Join-Path $out 'build-arguments.json') $buildArguments
    Push-Location -LiteralPath $repo
    try {
        & dotnet @buildArguments *> (Join-Path $out 'host-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Current production Host project build failed' }
        $candidate = Join-Path $artifacts 'bin/YeManFanHost/release_win-x64/YeManFanHost.dll'
        $buildFiles = @(Get-ChildItem -LiteralPath (Split-Path -Parent $candidate) -File)
        $result.artifactHashes = @(foreach ($file in $buildFiles) { [pscustomobject]@{ path=$file.FullName; sha256=(Hash-File $file.FullName) } })
        $result.hostDllSha256 = Hash-File $candidate
        $result.backendSha256 = Hash-File (Join-Path (Split-Path -Parent $candidate) 'YeManFanBackend.dll')
        $gateOut = Join-Path $out 'isolation'
        & (Join-Path $repo 'tools/fan_runtime_maintenance_isolation_selftest.ps1') -CandidateDll $candidate -EvidenceRoot $gateOut *> (Join-Path $out 'maintenance-gate.log')
        if ($LASTEXITCODE -ne 0) { throw 'Runtime maintenance original gate failed' }
        $line = Get-Content -LiteralPath (Join-Path $out 'maintenance-gate.log') | Where-Object { $_.TrimStart().StartsWith('{"ok"') } | Select-Object -Last 1
        if (-not $line) { throw 'Original maintenance structured result missing' }
        $gate = $line | ConvertFrom-Json
        if (-not $gate.ok -or $gate.hardwareCalls -ne 0 -or $gate.hostDllSha256 -ne $result.hostDllSha256 -or $gate.checks -ne 8) { throw 'Original maintenance receipt/identity/boundary incomplete' }
        Write-Json (Join-Path $out 'original-maintenance-result.json') $gate
        $result.checks += @([pscustomobject]@{ name='original-runtime-maintenance-isolation'; status='PASS'; checks=$gate.checks; originalReport='original-maintenance-result.json'; receiptScope=$gate.scope })
        # Fixed existing-tool entry only: behavioral checks, never an allocation/CPU benchmark.
        $buffersOut = Join-Path $out 'result-buffers'
        & (Join-Path $repo 'tools/fan_maintenance_result_buffers_selftest.ps1') -HostDll $candidate -OutputRoot $buffersOut -Mode behavior *> (Join-Path $out 'result-buffers-gate.log')
        if ($LASTEXITCODE -ne 0) { throw 'Runtime maintenance result-buffer original gate failed' }
        $buffersLine = Get-Content -LiteralPath (Join-Path $out 'result-buffers-gate.log') | Where-Object { $_.TrimStart().StartsWith('{"ok"') } | Select-Object -Last 1
        if (-not $buffersLine) { throw 'Original result-buffer structured summary missing' }
        $buffers = $buffersLine | ConvertFrom-Json
        $buffersReportPath = Join-Path $buffersOut 'result.json'
        if (-not (Test-Path -LiteralPath $buffersReportPath -PathType Leaf)) { throw 'Original result-buffer detail missing' }
        $buffersReport = Get-Content -LiteralPath $buffersReportPath -Raw | ConvertFrom-Json
        if (-not $buffers.ok -or $buffers.checks -ne 20 -or $buffers.hardwareCalls -ne 0 -or $buffers.hostMainExecuted -ne $false -or $buffers.mode -ne 'behavior' -or $buffers.hostDllSha256 -ne $result.hostDllSha256 -or
            -not $buffersReport.ok -or $buffersReport.checks -ne 20 -or $buffersReport.mode -ne 'behavior' -or $buffersReport.hostDllSha256 -ne $result.hostDllSha256 -or @($buffersReport.failures).Count -ne 0 -or
            @($buffersReport.results).Count -ne 20 -or @($buffersReport.results | Where-Object { $_.ok -ne $true }).Count -ne 0) { throw 'Original result-buffer receipt/identity/boundary incomplete' }
        Write-Json (Join-Path $out 'original-result-buffers-result.json') $buffers
        $result.checks += [pscustomobject]@{ name='original-maintenance-result-buffers'; status='PASS'; checks=$buffers.checks; originalReport='result-buffers/result.json'; receiptScope=$buffers.scope }
    } finally { Pop-Location }
    $afterPaths = Get-SourceInputs
    $changes = @(foreach ($row in $before) { if ((Hash-File (Join-Path $repo $row.path)) -ne $row.sha256 -or (Hash-File $row.snapshotPath) -ne $row.sha256) { $row.path } })
    if (($paths -join "`n") -cne ($afterPaths -join "`n")) { $changes += 'source-input-membership-changed' }
    foreach ($row in $result.artifactHashes) { if ((Hash-File $row.path) -ne $row.sha256) { $changes += $row.path } }
    $result.sourceChanges = $changes
    if ($changes.Count -gt 0) { throw 'Source or built payload changed during maintenance validation' }
    $result.status = 'PASS'
} catch {
    $result.issues = @($_.Exception.Message)
} finally {
    Write-Json (Join-Path $out 'result.json') $result
}
if ($result.status -ne 'PASS') { throw ($result.issues -join '; ') }
$result | ConvertTo-Json -Depth 4 -Compress
