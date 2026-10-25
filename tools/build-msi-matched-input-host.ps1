#requires -Version 7.0
<#
.SYNOPSIS
Builds a byte-bound InputHost alongside an existing Native candidate, without running either.
.DESCRIPTION
Only this helper and its private Build\Validation output are written. Every invocation
gets a fresh run directory; no previous output is deleted. Project/source snapshots,
obj, bin, NuGet/CLI caches, logs and binding receipts all remain in that directory.
The three direct references are verified against the approved repository lock before
staging. Native/probe and ZIP entries are read as bytes; their contents never supply
commands. Build warnings are preserved. Requires the installed .NET SDK and pwsh 7+.
.EXAMPLE
pwsh -NoProfile -File tools\build-msi-matched-input-host.ps1
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\MSI-Controller-Delivery-R7-20261013\matched-host',
    [string]$CandidateBindingPath = 'G:\YeManCC-Work\Build\Validation\MSI-SD-Lifecycle-R6-20261013\matched-candidate-with-service-evidence\candidate-binding.json',
    [string]$ReleasePackagePath = 'G:\YeManCC-Work\Release\Packages\YeManCC-0.0.35-FAN971-SIDEBAR-WAKE-20261013-72E3526E.zip'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Get-AbsolutePath([string]$Path) { [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') }
function Test-Within([string]$Path, [string]$Root) {
    (Get-AbsolutePath $Path).StartsWith((Get-AbsolutePath $Root) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
function Assert-OrdinaryPath([string]$Path) {
    $at = Get-AbsolutePath $Path
    while ($at) {
        if (Test-Path -LiteralPath $at) {
            if ((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed in build inputs/output: $at"
            }
        }
        $parent = Split-Path -Parent $at
        if ($parent -eq $at) { break }
        $at = $parent
    }
}
function Get-Receipt([string]$Path) {
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer) { throw "Expected a file: $Path" }
    [pscustomobject][ordered]@{
        path = $file.FullName
        bytes = [int64]$file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 40) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
function Get-HostSourceFiles([string]$Root) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.PSIsContainer -and $item.Name -in @('bin', 'obj', '.git')) { continue }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source snapshot refuses reparse point: $($item.FullName)" }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) } else { $item.FullName }
        }
    }
}
function Invoke-Sdk([string[]]$Arguments, [string]$LogPath) {
    $lines = @(& $script:dotnetPath @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $LogPath)
    $code = $LASTEXITCODE
    [pscustomobject]@{ exitCode = $code; logPath = $LogPath; lines = $lines }
}
function Get-ZipReceipt($Archive, [string]$EntryPath) {
    $entries = @($Archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -ieq $EntryPath })
    if ($entries.Count -ne 1) { throw "Expected one ZIP entry '$EntryPath', found $($entries.Count)" }
    $stream = $entries[0].Open()
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        [pscustomobject][ordered]@{
            entry = $entries[0].FullName
            bytes = [int64]$entries[0].Length
            sha256 = [Convert]::ToHexString($sha.ComputeHash($stream))
        }
    } finally { $sha.Dispose(); $stream.Dispose() }
}

$ProjectRoot = Get-AbsolutePath $ProjectRoot
$OutputRoot = Get-AbsolutePath $OutputRoot
$CandidateBindingPath = Get-AbsolutePath $CandidateBindingPath
$ReleasePackagePath = Get-AbsolutePath $ReleasePackagePath
$workspaceRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ProjectRoot))
$validationRoot = Join-Path $workspaceRoot 'Build\Validation'
if (-not (Test-Within $OutputRoot $validationRoot)) { throw "OutputRoot must be a private directory below $validationRoot" }
foreach ($path in @($ProjectRoot, $OutputRoot, $CandidateBindingPath, $ReleasePackagePath)) { Assert-OrdinaryPath $path }

# Run the reviewed verifier before creating any snapshot or invoking the SDK.
$lockPath = Join-Path $ProjectRoot 'deps\hidmaestro\dependency-lock.json'
$verifierPath = Join-Path $ProjectRoot 'tools\verify-input-host-dependencies.ps1'
$verification = @(& $verifierPath -ProjectRoot $ProjectRoot)
$verification | Write-Output
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$dependencyPaths = @($lock.files | ForEach-Object { Get-AbsolutePath (Join-Path $ProjectRoot $_.path) })
foreach ($path in $dependencyPaths) { Assert-OrdinaryPath $path }

$binding = Get-Content -LiteralPath $CandidateBindingPath -Raw -Encoding UTF8 | ConvertFrom-Json
$nativeChecks = @()
foreach ($record in @($binding.source) + @($binding.native) + @($binding.probe)) {
    $path = Get-AbsolutePath $record.Path
    Assert-OrdinaryPath $path
    $actual = Get-Receipt $path
    if ($actual.sha256 -ine [string]$record.Hash) { throw "Existing Native binding is stale or changed: $path" }
    $nativeChecks += [pscustomobject]@{ path = $path; bytes = $actual.bytes; expectedSha256 = [string]$record.Hash; sha256 = $actual.sha256; matched = $true }
}
if (@($binding.source).Count -eq 0) { throw 'Native binding contains no source hashes' }
foreach ($record in @($binding.source)) {
    if (-not (Test-Within $record.Path $ProjectRoot)) { throw "Native source binding is outside ProjectRoot: $($record.Path)" }
}

$sourceRoot = Join-Path $ProjectRoot 'InputHost'
$sourceFiles = @(Get-HostSourceFiles $sourceRoot | Sort-Object)
$projectPath = Join-Path $sourceRoot 'YeManInputHost.csproj'
[xml]$projectXml = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
if ($projectXml.Project.Sdk -ne 'Microsoft.NET.Sdk' -or
    [string]$projectXml.Project.PropertyGroup.TargetFramework -ne 'net10.0-windows' -or
    [string]$projectXml.Project.PropertyGroup.PlatformTarget -ne 'x64') { throw 'Unexpected InputHost SDK/framework/platform; review before building' }
if ($projectXml.SelectNodes('//Import|//Target|//UsingTask|//PackageReference|//ProjectReference').Count) {
    throw 'Snapshot only supports the reviewed SDK project and three direct DLL references'
}
$references = @($projectXml.SelectNodes('//Reference'))
if ($references.Count -ne 3) { throw 'Expected exactly three direct locked references' }
foreach ($reference in $references) {
    $referenceMatches = @($dependencyPaths | Where-Object { [IO.Path]::GetFileNameWithoutExtension($_) -ceq [string]$reference.Include })
    if ($referenceMatches.Count -ne 1 -or @($reference.SelectNodes('HintPath')).Count -ne 1) { throw "Reference does not map to exactly one locked DLL: $($reference.Include)" }
    $reference.HintPath = $referenceMatches[0]
}

# Baseline every possible repository-local RAR dependency, then bind the actual
# resolved subset below. This catches concurrent changes to transitive DLLs too.
$resolverFiles = @($dependencyPaths | ForEach-Object { Split-Path -Parent $_ } | Sort-Object -Unique | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Filter '*.dll' -File | ForEach-Object { $_.FullName }
})
$originalPaths = @($sourceFiles + $resolverFiles + @($PSCommandPath, $verifierPath, $lockPath, $CandidateBindingPath, $ReleasePackagePath) + @($nativeChecks.path) | Sort-Object -Unique)
$before = @($originalPaths | ForEach-Object { Get-Receipt $_ })
$beforeByPath = @{}
foreach ($record in $before) { $beforeByPath[$record.path] = $record }

$runRoot = Join-Path $OutputRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
Assert-OrdinaryPath $runRoot
$null = New-Item -ItemType Directory -Path $runRoot
$snapshotRoot = Join-Path $runRoot 'snapshot\InputHost'
$binRoot = Join-Path $runRoot 'bin'
$objRoot = Join-Path $runRoot 'obj'
$evidenceRoot = Join-Path $runRoot 'evidence'
foreach ($directory in @($snapshotRoot, $binRoot, $objRoot, $evidenceRoot, (Join-Path $runRoot 'tmp'))) { $null = New-Item -ItemType Directory -Path $directory }
Write-Output "MATCHED_HOST_RUN=$runRoot"
$envNames = @('DOTNET_CLI_HOME', 'DOTNET_CLI_TELEMETRY_OPTOUT', 'DOTNET_SKIP_FIRST_TIME_EXPERIENCE', 'DOTNET_NOLOGO', 'DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE', 'DOTNET_CLI_USE_MSBUILD_SERVER', 'NUGET_PACKAGES', 'NUGET_HTTP_CACHE_PATH', 'TEMP', 'TMP')
$oldEnvironment = @{}
foreach ($name in $envNames) { $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$environment = @{
    DOTNET_CLI_HOME = (Join-Path $runRoot 'dotnet-home')
    DOTNET_CLI_TELEMETRY_OPTOUT = '1'; DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'; DOTNET_NOLOGO = '1'
    DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'; DOTNET_CLI_USE_MSBUILD_SERVER = '0'
    NUGET_PACKAGES = (Join-Path $runRoot 'nuget-packages'); NUGET_HTTP_CACHE_PATH = (Join-Path $runRoot 'nuget-http-cache')
    TEMP = (Join-Path $runRoot 'tmp'); TMP = (Join-Path $runRoot 'tmp')
}
foreach ($name in $environment.Keys) { [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process') }
$startedUtc = [DateTime]::UtcNow.ToString('o')
$buildExitCode = $null
$failure = $null
$originalStable = $false
$compileInputs = @()
$copyChecks = @()
$runtime = $null
$zipComparison = $null
$sdkInfo = $null
$warningLines = @()
$warningDiagnostics = @()
$buildArguments = @()
$locationPushed = $false
try {
    Push-Location -LiteralPath $runRoot
    $locationPushed = $true
    Write-Json (Join-Path $evidenceRoot 'original-inputs-before.json') $before
    [IO.File]::WriteAllLines((Join-Path $evidenceRoot 'dependency-verification-before.log'), [string[]]$verification)
    foreach ($path in $sourceFiles) {
        $relative = [IO.Path]::GetRelativePath($sourceRoot, $path)
        $destination = Join-Path $snapshotRoot $relative
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $path -Destination $destination
        if ((Get-Receipt $destination).sha256 -ine $beforeByPath[$path].sha256) { throw "Snapshot changed while copying: $path" }
    }
    $snapshotProject = Join-Path $snapshotRoot 'YeManInputHost.csproj'
    $projectXml.Save($snapshotProject)
    Copy-Item -LiteralPath $lockPath -Destination (Join-Path $evidenceRoot 'approved-dependency-lock.json')
    Copy-Item -LiteralPath $CandidateBindingPath -Destination (Join-Path $evidenceRoot 'native-candidate-binding.json')
    Write-Json (Join-Path $evidenceRoot 'native-binding-checks.json') $nativeChecks

    $script:dotnetPath = @(Get-Command dotnet -CommandType Application)[0].Path
    $sdkVersionResult = Invoke-Sdk @('--version') (Join-Path $evidenceRoot 'sdk-version.log')
    if ($sdkVersionResult.exitCode -ne 0) { throw "dotnet --version failed: $($sdkVersionResult.exitCode)" }
    $sdkVersion = ($sdkVersionResult.lines -join '').Trim()
    Write-Json (Join-Path $runRoot 'global.json') ([ordered]@{ sdk = [ordered]@{ version = $sdkVersion; rollForward = 'disable'; allowPrerelease = $false } })
    $sdkList = Invoke-Sdk @('--list-sdks') (Join-Path $evidenceRoot 'installed-sdks.log')
    $sdkMatches = @($sdkList.lines | Where-Object { $_ -match ('^' + [regex]::Escape($sdkVersion) + ' \[(.+)\]$') })
    if ($sdkList.exitCode -ne 0 -or $sdkMatches.Count -ne 1) { throw 'Unable to bind selected SDK directory' }
    $null = $sdkMatches[0] -match ('^' + [regex]::Escape($sdkVersion) + ' \[(.+)\]$')
    $sdkRoot = Join-Path $Matches[1] $sdkVersion
    $referencePackRoot = Join-Path (Split-Path -Parent $script:dotnetPath) 'packs'
    $info = Invoke-Sdk @('--info') (Join-Path $evidenceRoot 'dotnet-info.log')
    $runtimeList = Invoke-Sdk @('--list-runtimes') (Join-Path $evidenceRoot 'installed-runtimes.log')
    if ($info.exitCode -ne 0 -or $runtimeList.exitCode -ne 0) { throw 'SDK/runtime inventory command failed' }
    $sdkInfo = [ordered]@{ version = $sdkVersion; directory = $sdkRoot; executable = Get-Receipt $script:dotnetPath; installedRuntimes = $runtimeList.lines }

    # Empty feeds and isolated caches prevent restore from consulting scratch DLLs
    # or fetching packages. Directory.Build imports from ancestors are disabled.
    $nugetConfig = Join-Path $runRoot 'NuGet.Config'
    [IO.File]::WriteAllText($nugetConfig, '<configuration><packageSources><clear /></packageSources><fallbackPackageFolders><clear /></fallbackPackageFolders></configuration>')
    $recordTargets = Join-Path $runRoot 'record-build-inputs.targets'
    $targets = @"
<Project>
  <Target Name="RecordMatchedHostInputs" BeforeTargets="CoreCompile" DependsOnTargets="ResolveReferences">
    <WriteLinesToFile File="__EVIDENCE__/compiler-inputs.txt" Lines="@(Compile->'%(FullPath)');@(ReferencePath->'%(FullPath)');@(Analyzer->'%(FullPath)');@(AdditionalFiles->'%(FullPath)');@(EmbeddedResource->'%(FullPath)');@(EditorConfigFiles->'%(FullPath)')" Overwrite="true" />
    <WriteLinesToFile File="__EVIDENCE__/copy-local-inputs.txt" Lines="@(ReferenceCopyLocalPaths->'%(FullPath)')" Overwrite="true" />
    <WriteLinesToFile File="__EVIDENCE__/imported-projects.txt" Lines="`$(MSBuildAllProjects)" Overwrite="true" />
  </Target>
</Project>
"@
    $targets = $targets.Replace('__EVIDENCE__', [Security.SecurityElement]::Escape($evidenceRoot.Replace('\', '/')))
    [IO.File]::WriteAllText($recordTargets, $targets, [Text.UTF8Encoding]::new($false))
    $buildArguments = @(
        'build', $snapshotProject, '-c', 'Release', '--nologo', '--disable-build-servers', '--output', $binRoot,
        '-p:Platform=x64', '-p:PlatformTarget=x64', '-p:SelfContained=false', '-p:UseSharedCompilation=false',
        "-p:BaseIntermediateOutputPath=$objRoot\", "-p:MSBuildProjectExtensionsPath=$objRoot\", "-p:BaseOutputPath=$binRoot\",
        '-p:ImportDirectoryBuildProps=false', '-p:ImportDirectoryBuildTargets=false',
        "-p:RestoreConfigFile=$nugetConfig", "-p:RestorePackagesPath=$($environment.NUGET_PACKAGES)",
        '-p:DisableImplicitNuGetFallbackFolder=true', '-p:NuGetAudit=false',
        "-p:CustomAfterMicrosoftCommonTargets=$recordTargets", "-bl:$(Join-Path $evidenceRoot 'build.binlog')"
    )
    Write-Json (Join-Path $evidenceRoot 'build-command.json') ([ordered]@{ executable = $script:dotnetPath; workingDirectory = $runRoot; arguments = $buildArguments; environment = $environment })
    $buildResult = Invoke-Sdk $buildArguments (Join-Path $evidenceRoot 'build.log')
    $buildExitCode = $buildResult.exitCode
    $warningLines = @($buildResult.lines | Where-Object { $_ -match '\bwarning\s+[A-Za-z]+\d+\b' })
    $warningDiagnostics = @($warningLines | ForEach-Object { $_.Trim() } | Sort-Object -Unique)
    Write-Json (Join-Path $evidenceRoot 'build-result.json') ([ordered]@{ exitCode = $buildExitCode; warningLineOccurrences = $warningLines.Count; uniqueWarningCount = $warningDiagnostics.Count; warnings = $warningDiagnostics })
    if ($buildExitCode -ne 0) { throw "Release x64 build failed: exit=$buildExitCode" }

    $inputPaths = @($sourceFiles + $dependencyPaths + @($PSCommandPath, $verifierPath, $lockPath, $CandidateBindingPath, $ReleasePackagePath, $snapshotProject, $nugetConfig, $recordTargets, (Join-Path $runRoot 'global.json'), $script:dotnetPath, (Join-Path $sdkRoot 'MSBuild.dll'), (Join-Path $sdkRoot 'Roslyn\bincore\csc.dll')))
    foreach ($listName in @('compiler-inputs.txt', 'copy-local-inputs.txt', 'imported-projects.txt')) {
        $inputPaths += @(Get-Content -LiteralPath (Join-Path $evidenceRoot $listName) | Where-Object { $_.Trim() } | ForEach-Object { Get-AbsolutePath $_.Trim() })
    }
    $inputPaths = @($inputPaths + @($nativeChecks.path) | Sort-Object -Unique)
    foreach ($path in $inputPaths) {
        if (-not ((Test-Within $path $ProjectRoot) -or (Test-Within $path $runRoot) -or
            (Test-Within $path $sdkRoot) -or (Test-Within $path $referencePackRoot) -or $path -eq $script:dotnetPath -or
            $path -eq $CandidateBindingPath -or $path -eq $ReleasePackagePath -or
            $path -in @($binding.native.Path, $binding.probe.Path))) {
            throw "Resolved build input outside repository, private run or bound SDK: $path"
        }
        $compileInputs += Get-Receipt $path
    }
    Write-Json (Join-Path $evidenceRoot 'build-inputs.sha256.json') $compileInputs

    $copySources = @(Get-Content -LiteralPath (Join-Path $evidenceRoot 'copy-local-inputs.txt') | Where-Object { $_.Trim() } | Sort-Object -Unique)
    foreach ($path in $copySources) {
        if (-not (Test-Within $path $ProjectRoot) -or -not $beforeByPath.ContainsKey($path)) { throw "Unbound copy-local dependency: $path" }
        $output = Get-Receipt (Join-Path $binRoot ([IO.Path]::GetFileName($path)))
        $source = $beforeByPath[$path]
        $equal = $source.sha256 -ieq $output.sha256 -and $source.bytes -eq $output.bytes
        $copyChecks += [pscustomobject]@{ source = $source; output = $output; approvedDirectLock = $path -in $dependencyPaths; byteIdentical = $equal }
        if (-not $equal) { throw "Copied runtime DLL differs from repository input: $path" }
    }
    foreach ($path in $dependencyPaths) {
        if (@($copyChecks | Where-Object { $_.source.path -ieq $path -and $_.byteIdentical }).Count -ne 1) { throw "Direct locked dependency absent from output: $path" }
    }
    Write-Json (Join-Path $evidenceRoot 'dependency-copy-checks.json') $copyChecks
    foreach ($name in @('YeManInputHost.exe', 'YeManInputHost.dll', 'YeManInputHost.pdb', 'YeManInputHost.deps.json', 'YeManInputHost.runtimeconfig.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $binRoot $name) -PathType Leaf)) { throw "Missing host output: $name" }
    }
    $runtimeConfig = Get-Content -LiteralPath (Join-Path $binRoot 'YeManInputHost.runtimeconfig.json') -Raw | ConvertFrom-Json
    $runtime = [ordered]@{ platform = 'Windows x64'; targetFramework = 'net10.0-windows'; selfContained = $false; runtimeOptions = $runtimeConfig.runtimeOptions; installedRuntimes = $runtimeList.lines; dependencyCopies = $copyChecks }
    Write-Json (Join-Path $evidenceRoot 'runtime-requirements.json') $runtime

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ReleasePackagePath)
    try {
        $packagedHost = Get-ZipReceipt $archive 'PowerControl/feature-assets/virtual-gamepad/YeManInputHost.dll'
        $newHost = Get-Receipt (Join-Path $binRoot 'YeManInputHost.dll')
        $zipDependencyChecks = @()
        foreach ($path in $dependencyPaths) {
            $name = [IO.Path]::GetFileName($path)
            $entry = if ($name -eq 'Nefarius.Utilities.DeviceManagement.dll') {
                'PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/' + $name
            } else { 'PowerControl/feature-assets/virtual-gamepad/' + $name }
            $zipRecord = Get-ZipReceipt $archive $entry
            $zipDependencyChecks += [pscustomobject]@{ repository = $beforeByPath[$path]; packaged = $zipRecord; byteIdentical = $zipRecord.sha256 -ieq $beforeByPath[$path].sha256 }
        }
        $zipComparison = [ordered]@{
            package = $beforeByPath[$ReleasePackagePath]; packagedHost = $packagedHost; newHost = $newHost
            hostByteIdentical = $packagedHost.sha256 -ieq $newHost.sha256
            pairedNativeAndHostUpdateRequired = $packagedHost.sha256 -ine $newHost.sha256
            dependencyChecks = $zipDependencyChecks; readOnly = $true; extracted = $false; packageCreated = $false
        }
    } finally { $archive.Dispose() }
    Write-Json (Join-Path $evidenceRoot 'release-package-comparison.json') $zipComparison
} catch {
    $failure = $_.Exception.Message
    Write-Output "MATCHED_HOST_ERROR=$failure"
} finally {
    try {
        $after = @($originalPaths | ForEach-Object { Get-Receipt $_ })
        $sourceFilesAfter = @(Get-HostSourceFiles $sourceRoot | Sort-Object)
        $changes = @()
        foreach ($record in $after) {
            $previous = $beforeByPath[$record.path]
            if ($record.sha256 -ine $previous.sha256 -or $record.bytes -ne $previous.bytes) { $changes += $record.path }
        }
        $sourceSetChanges = @(Compare-Object $sourceFiles $sourceFilesAfter)
        $originalStable = $changes.Count -eq 0 -and $sourceSetChanges.Count -eq 0
        Write-Json (Join-Path $evidenceRoot 'original-inputs-after.json') $after
        Write-Json (Join-Path $evidenceRoot 'original-source-stability.json') ([ordered]@{ stable = $originalStable; checkedFiles = $originalPaths.Count; changedFiles = $changes; sourceFileSetChanges = $sourceSetChanges })
        $verificationAfter = @(& $verifierPath -ProjectRoot $ProjectRoot)
        [IO.File]::WriteAllLines((Join-Path $evidenceRoot 'dependency-verification-after.log'), [string[]]$verificationAfter)
        if (-not $originalStable -and -not $failure) { $failure = 'Original inputs changed during build; this run is not a matched delivery candidate' }
    } catch { if (-not $failure) { $failure = 'Post-build stability/lock verification failed: ' + $_.Exception.Message } }
    if ($locationPushed) { Pop-Location }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
}

$payload = @(Get-ChildItem -LiteralPath $binRoot -File -Recurse | Sort-Object FullName | ForEach-Object { Get-Receipt $_.FullName })
$receiptPath = Join-Path $runRoot 'candidate-binding.json'
$receipt = [ordered]@{
    schemaVersion = 1; status = if ($failure) { 'FAILED' } else { 'BUILT_BYTE_BOUND_NOT_EXECUTED' }
    startedUtc = $startedUtc; finishedUtc = [DateTime]::UtcNow.ToString('o'); projectRoot = $ProjectRoot; outputRoot = $OutputRoot; runRoot = $runRoot
    configuration = 'Release'; platform = 'x64'; sdk = $sdkInfo
    buildExitCode = $buildExitCode; helperExitCode = if ($failure) { 1 } else { 0 }; failure = $failure
    warnings = [ordered]@{ uniqueCount = $warningDiagnostics.Count; lineOccurrences = $warningLines.Count; diagnostics = $warningDiagnostics; preservedWithoutSourceOrWarningPolicyChanges = $true }
    originalInputsStable = $originalStable; originalInputFileCount = $originalPaths.Count
    originalBeforePath = Join-Path $evidenceRoot 'original-inputs-before.json'; originalAfterPath = Join-Path $evidenceRoot 'original-inputs-after.json'
    nativeCandidateBinding = $beforeByPath[$CandidateBindingPath]; native = Get-Receipt $binding.native.Path; probeEvidenceOnly = Get-Receipt $binding.probe.Path
    nativeBindingCheckedFileCount = $nativeChecks.Count; nativeSourceHashesMatched = $true
    host = if (Test-Path -LiteralPath (Join-Path $binRoot 'YeManInputHost.dll')) { Get-Receipt (Join-Path $binRoot 'YeManInputHost.dll') } else { $null }
    inputs = $compileInputs; outputs = $payload; dependencyCopies = $copyChecks; requiredRuntime = $runtime; releasePackageComparison = $zipComparison
    productionLaunched = $false; inputHostLaunched = $false; probeLaunched = $false; selfTestsRun = $false
    hardwareOrServiceOperations = $false; nativeRebuilt = $false; deployed = $false; testPackageCreated = $false
    fileInventoryPath = Join-Path $runRoot 'files.sha256.json'
    scope = 'Private InputHost compile and byte evidence bound to the existing Native candidate. No execution or integration acceptance.'
}
Write-Json $receiptPath $receipt
# A manifest cannot hash itself. All other run files, including the binding and
# logs/intermediates, are covered; the manifest gets a separate checksum sidecar.
$runFiles = @(Get-ChildItem -LiteralPath $runRoot -File -Recurse -Force | Sort-Object FullName | ForEach-Object { Get-Receipt $_.FullName })
$inventoryPath = Join-Path $runRoot 'files.sha256.json'
Write-Json $inventoryPath ([ordered]@{ algorithm = 'SHA256'; inputs = $before + $compileInputs; outputFiles = $runFiles; excludedSelfAndSidecar = @($inventoryPath, ($inventoryPath + '.sha256')) })
$inventoryHash = (Get-Receipt $inventoryPath).sha256
[IO.File]::WriteAllText($inventoryPath + '.sha256', $inventoryHash + '  files.sha256.json' + [Environment]::NewLine)
Write-Output "MATCHED_HOST_BINDING=$receiptPath"
Write-Output "MATCHED_HOST_INVENTORY_SHA256=$inventoryHash"
Write-Output "BUILD_EXIT_CODE=$buildExitCode; UNIQUE_WARNINGS=$($warningDiagnostics.Count); ORIGINAL_INPUTS_STABLE=$originalStable"
if ($null -ne $zipComparison) { Write-Output "RELEASE_ZIP_HOST_IDENTICAL=$($zipComparison.hostByteIdentical); PAIRED_UPDATE_REQUIRED=$($zipComparison.pairedNativeAndHostUpdateRequired)" }
if ($failure) { Write-Error -Message $failure -ErrorAction Continue; exit 1 }
