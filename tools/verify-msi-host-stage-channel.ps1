#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\MSI-Controller-Delivery-R9-20261013\stage-channel')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$allowedRoot = 'G:\YeManCC-Work\Build\Validation\MSI-Controller-Delivery-R9-20261013\stage-channel'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if (-not ($OutputRoot.Equals($allowedRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputRoot.StartsWith($allowedRoot + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw 'All standalone writes must remain in the authorized R9 stage-channel directory.'
}
if ($OutputRoot -match '["%!\r\n&|<>^]') { throw 'Output path cannot contain shell metacharacters.' }
$ancestor = $OutputRoot
while ($ancestor) {
    if ((Test-Path -LiteralPath $ancestor) -and
        ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Output ancestry contains a reparse point: $ancestor"
    }
    $ancestor = Split-Path -Parent $ancestor
}
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runRoot = Join-Path $OutputRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
$snapshotRoot = Join-Path $runRoot 'snapshot'
$utf8 = [Text.UTF8Encoding]::new($false)
$checks = [Collections.Generic.List[object]]::new()
$cases = [Collections.Generic.List[object]]::new()
$processRuns = [Collections.Generic.List[object]]::new()
$children = [Collections.Generic.List[object]]::new()
$binding = [Collections.Generic.List[object]]::new()
$artifactPaths = [Collections.Generic.List[string]]::new()
$realMappingTestExecuted = $false
$realMappingCreated = $false
$failure = $null
$producerChild = $null
$readerChild = $null
$readerCalls = 0
$sourceStable = $true
$sdkVersion = $null
$producerBinary = $null
$readerBinary = $null
$readOnlyVerified = $false
$productionAssemblyAbsent = $null

function Assert-PrivatePath([string]$Path) {
    if (-not [IO.Path]::GetFullPath($Path).StartsWith($runRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Write escapes private run: $Path"
    }
}
function Write-Utf8([string]$Path, [string]$Value) {
    Assert-PrivatePath $Path
    [IO.File]::WriteAllText($Path, $Value, $utf8)
    if (-not $artifactPaths.Contains($Path)) { $artifactPaths.Add($Path) }
}
function Write-Json([string]$Path, $Value) { Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth 25) }
function Check([bool]$Condition, [string]$Name, $Actual = $null) {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Condition; actual = $Actual })
    if (-not $Condition) { throw "Verification failed: $Name; actual=$(ConvertTo-Json -InputObject $Actual -Depth 8 -Compress)" }
}
function Start-OwnedChild([string]$Exe, [string[]]$Arguments, [string]$Name, [string]$WorkingDirectory) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    foreach ($argument in $Arguments) { $psi.ArgumentList.Add($argument) }
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardInputEncoding = $utf8
    $psi.StandardOutputEncoding = $utf8
    $psi.StandardErrorEncoding = $utf8
    $psi.Environment['DOTNET_CLI_HOME'] = Join-Path $runRoot 'cli-home'
    $psi.Environment['NUGET_PACKAGES'] = Join-Path $runRoot 'packages'
    $psi.Environment['NUGET_HTTP_CACHE_PATH'] = Join-Path $runRoot 'nuget-http-cache'
    $psi.Environment['NUGET_PLUGINS_CACHE_PATH'] = Join-Path $runRoot 'nuget-plugins-cache'
    $psi.Environment['DOTNET_SKIP_FIRST_TIME_EXPERIENCE'] = '1'
    $psi.Environment['DOTNET_CLI_TELEMETRY_OPTOUT'] = '1'
    $psi.Environment['DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE'] = '1'
    $psi.Environment['DOTNET_NOLOGO'] = '1'
    $psi.Environment['MSBUILDDISABLENODEREUSE'] = '1'
    $psi.Environment['TEMP'] = Join-Path $runRoot 'temp'
    $psi.Environment['TMP'] = Join-Path $runRoot 'temp'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    $timer = [Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) { throw "Could not start own child: $Name" }
    $child = [pscustomobject]@{
        name = $Name; process = $process; timer = $timer; pid = $process.Id
        stderrTask = $process.StandardError.ReadToEndAsync()
        transcript = [Collections.Generic.List[object]]::new()
        closed = $false; killedForTimeout = $false; stdout = $null
        executable = $Exe; arguments = $Arguments
    }
    $children.Add($child)
    return $child
}
function Stop-OwnChild($Child, [bool]$TimedOut = $false) {
    if ($null -ne $Child -and -not $Child.closed -and -not $Child.process.HasExited) {
        # Kill only this created Process and its own build descendants.
        $Child.process.Kill($true)
        $Child.killedForTimeout = $TimedOut
        if (-not $Child.process.WaitForExit(1000)) { throw "Own child did not terminate within one second: $($Child.name)" }
    }
}
function Remaining-Ms($Child) {
    $remaining = 25000 - [int]$Child.timer.ElapsedMilliseconds
    if ($remaining -le 0) { Stop-OwnChild $Child $true; throw "Own child exceeded 25 seconds: $($Child.name)" }
    return $remaining
}
function Close-Child($Child) {
    if ($Child.closed) { return }
    Stop-OwnChild $Child
    $Child.timer.Stop()
    $stderr = $Child.stderrTask.GetAwaiter().GetResult()
    $logBase = Join-Path $runRoot $Child.name
    Write-Utf8 ($logBase + '.stderr.txt') $stderr
    if ($null -ne $Child.stdout) { Write-Utf8 ($logBase + '.stdout.txt') $Child.stdout }
    if ($Child.transcript.Count) { Write-Json ($logBase + '.transcript.json') $Child.transcript.ToArray() }
    $processRuns.Add([pscustomobject]@{
        name = $Child.name; pid = $Child.pid; executable = $Child.executable; arguments = $Child.arguments
        createNoWindow = $true; useShellExecute = $false; lifetimeMs = $Child.timer.ElapsedMilliseconds
        lifetimeLimitMs = 25000; exitCode = $Child.process.ExitCode; killedForTimeout = $Child.killedForTimeout
    })
    $Child.closed = $true
    $Child.process.Dispose()
}
function Run-BoundedChild([string]$Exe, [string[]]$Arguments, [string]$Name, [string]$WorkingDirectory) {
    $child = Start-OwnedChild $Exe $Arguments $Name $WorkingDirectory
    try {
        $child.process.StandardInput.Close()
        $outputTask = $child.process.StandardOutput.ReadToEndAsync()
        if (-not $child.process.WaitForExit((Remaining-Ms $child))) {
            Stop-OwnChild $child $true
            throw "Own build/tool child exceeded 25 seconds: $Name"
        }
        $child.stdout = $outputTask.GetAwaiter().GetResult()
        $result = [pscustomobject]@{ exitCode = $child.process.ExitCode; stdout = $child.stdout }
    }
    finally { Close-Child $child }
    return $result
}
function Receive-Reply($Child) {
    $readTask = $Child.process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait([Math]::Min(5000, (Remaining-Ms $Child)))) {
        Stop-OwnChild $Child $true
        throw "Own fixture response exceeded bounded deadline: $($Child.name)"
    }
    $line = $readTask.GetAwaiter().GetResult()
    if ($null -eq $line) { throw "Own fixture exited without a reply: $($Child.name)" }
    $reply = ConvertFrom-Json -InputObject $line
    $Child.transcript.Add([pscustomobject]@{ direction = 'response'; value = $reply })
    if (-not $reply.ok) { throw "Fixture/API issue in $($Child.name): $line" }
    return $reply
}
function Send-Command($Child, $Command) {
    [void](Remaining-Ms $Child)
    $Child.transcript.Add([pscustomobject]@{ direction = 'command'; value = $Command })
    $Child.process.StandardInput.WriteLine((ConvertTo-Json -InputObject $Command -Depth 10 -Compress))
    $Child.process.StandardInput.Flush()
    return Receive-Reply $Child
}
function Assert-ProducerScope($Snapshot, [string]$Label, [bool]$Disposed = $false) {
    Check ($Snapshot.scope -ceq 'Current MSI host stage diagnostic state only; no tuple, consumer, or Steam acceptance guarantee.') "$Label producer snapshot scope unchanged" $Snapshot.scope
    Check (-not $Snapshot.disabled -and $Snapshot.disposed -eq $Disposed) "$Label producer disposal/enabled state" $Snapshot
}
function Read-Case([string]$Name, $Expected, [bool]$Audit = $false) {
    $reply = Send-Command $readerChild @{ op = 'read'; case = $Name; expected = $Expected; auditReadOnly = $Audit }
    $script:readerCalls++
    $cases.Add($reply)
    Write-Json (Join-Path $runRoot ($Name + '.json')) $reply
    $row = $reply.row
    Check (-not $row.controlAuthority -and $row.steamConsumption -ceq 'unobserved') "$Name observation/control/Steam scope unchanged" $row
    if ($row.observed) {
        Check ($row.snapshotScope -ceq 'latest-coherent-host-checkpoint' -and
            -not $row.parsedMetadataIsAdmissionProof -and $row.submitScope -ceq 'TrySubmitFrame-encoding-and-backend' -and
            -not $row.writeReturnIsNativeReceiptProof) "$Name stage scopes unchanged" $row
        Check ($row.readOnlyMapping -and $row.copyAttempts -ge 1 -and $row.copyAttempts -le 3) "$Name read-only mapping and bounded copy" $row
        Check ($row.parentPid -eq $Expected.parentPid -and $row.hostPid -eq $Expected.hostPid -and
            $row.hostInstanceId -ceq $Expected.hostInstance) "$Name exact process identity" $row
        Check ($row.publicationSequence -gt 0 -and $row.publicationSequence % 2 -eq 0 -and
            $row.clockComparable) "$Name coherent publication and comparable real QPC" $row
    }
    return $reply
}
function Expected-Copy($Expected) {
    $copy = [ordered]@{}
    foreach ($key in $Expected.Keys) { $copy[$key] = $Expected[$key] }
    return $copy
}
function Check-Unavailable($Reply, [string]$Name) {
    Check (-not $Reply.row.observed -and $Reply.row.reason -ceq 'mapping-unavailable' -and
        $Reply.row.win32Error -eq 2) "$Name unknown/mapping-unavailable (ERROR_FILE_NOT_FOUND)" $Reply.row
}

New-Item -ItemType Directory -Path $snapshotRoot,(Join-Path $runRoot 'temp'),(Join-Path $runRoot 'native') -Force | Out-Null
try {
    $relativeSources = @(
        'InputHost\MsiHostStageEvidence.cs', 'native\msi_host_stage_evidence.h', 'deps\json\json.hpp',
        'tools\msi_host_stage_contract_v1.json', 'tools\msi_host_stage_channel_fixture.cs',
        'tools\msi_host_stage_channel_reader.cpp', 'tools\verify-msi-host-stage-channel.ps1'
    )
    foreach ($relativeSource in $relativeSources) {
        $source = Join-Path $repoRoot $relativeSource
        $destination = Join-Path $snapshotRoot $relativeSource
        Assert-PrivatePath $destination
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $source -Destination $destination
        Check ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $hash) "Exact source snapshot: $relativeSource" $hash
        $binding.Add([pscustomobject]@{ source = $source; snapshot = $destination; sha256 = $hash })
        $artifactPaths.Add($destination)
    }
    Write-Json (Join-Path $runRoot 'source-binding.json') $binding.ToArray()
    $contract = Get-Content -LiteralPath (Join-Path $snapshotRoot 'tools\msi_host_stage_contract_v1.json') -Raw | ConvertFrom-Json
    $helperText = Get-Content -LiteralPath (Join-Path $snapshotRoot 'InputHost\MsiHostStageEvidence.cs') -Raw
    $headerText = Get-Content -LiteralPath (Join-Path $snapshotRoot 'native\msi_host_stage_evidence.h') -Raw
    $fixtureText = Get-Content -LiteralPath (Join-Path $snapshotRoot 'tools\msi_host_stage_channel_fixture.cs') -Raw
    $readerText = Get-Content -LiteralPath (Join-Path $snapshotRoot 'tools\msi_host_stage_channel_reader.cpp') -Raw
    Check ($contract.version -eq 1 -and $contract.capacity -eq 1024 -and
        $contract.mapPrefix -ceq 'Local\YeManCC-MSI-HostStage-v1-' -and
        -not $contract.controlDecisionsChanged -and -not $contract.transportProtocolChanged) 'Contract scope/format unchanged' $contract
    Check ($helperText.Contains('MemoryMappedFile.CreateNew(name, capacity, MemoryMappedFileAccess.ReadWrite)') -and
        -not $helperText.Contains('MemoryMappedFile.OpenExisting(') -and
        $fixtureText.Contains('MsiHostStageEvidence.TryCreate(') -and
        -not $fixtureText.Contains('MsiHostStageEvidence.TryCreateForTest(')) 'Actual CreateNew factory; no synthetic sink or adoption'
    Check ($headerText.Contains('OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str())') -and
        $headerText.Contains('MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, capacity)') -and
        $headerText.Contains('UnmapViewOfFile(view); CloseHandle(mapping);') -and
        $readerText.Contains('#include "msi_host_stage_evidence.h"') -and
        $readerText.Contains('stage::read(expected)')) 'Actual header requests read-only access and closes handles'

    $dotnet = (Get-Command dotnet -ErrorAction Stop).Source
    $sdk = Run-BoundedChild $dotnet @('--version') 'sdk-version' $snapshotRoot
    Check ($sdk.exitCode -eq 0) 'SDK version child succeeded' $sdk
    $sdkVersion = $sdk.stdout.Trim()
    Check ($sdkVersion.StartsWith('10.')) 'SDK supports net10.0 fixture' $sdkVersion
    Write-Json (Join-Path $snapshotRoot 'global.json') @{ sdk = @{ version = $sdkVersion; rollForward = 'disable' } }
    $nugetPath = Join-Path $snapshotRoot 'NuGet.Config'
    Write-Utf8 $nugetPath '<configuration><packageSources><clear /></packageSources></configuration>'
    $projectPath = Join-Path $snapshotRoot 'MsiHostStageChannelFixture.csproj'
    $projectLines = @(
        '<Project Sdk="Microsoft.NET.Sdk">',
        '  <PropertyGroup>',
        '    <OutputType>Exe</OutputType>',
        '    <TargetFramework>net10.0</TargetFramework>',
        '    <AssemblyName>MsiHostStageChannelFixture</AssemblyName>',
        '    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>',
        '    <ImplicitUsings>disable</ImplicitUsings>',
        '    <Nullable>enable</Nullable>',
        '    <PlatformTarget>x64</PlatformTarget>',
        '    <UseAppHost>false</UseAppHost>',
        '    <NuGetAudit>false</NuGetAudit>',
        '  </PropertyGroup>',
        '  <ItemGroup>',
        '    <Compile Include="InputHost/MsiHostStageEvidence.cs" />',
        '    <Compile Include="tools/msi_host_stage_channel_fixture.cs" />',
        '  </ItemGroup>',
        '</Project>'
    )
    Write-Utf8 $projectPath ($projectLines -join "`n")
    $objectRoot = (Join-Path $runRoot 'obj').Replace('\', '/') + '/'
    $producerOutput = Join-Path $runRoot 'producer'
    $buildArgs = @('build', $projectPath, '--configuration', 'Release', '--nologo', '--disable-build-servers',
        '--configfile', $nugetPath, '--output', $producerOutput, '-m:1', '-nr:false', '-p:UseSharedCompilation=false',
        ('-p:BaseIntermediateOutputPath=' + $objectRoot), ('-p:MSBuildProjectExtensionsPath=' + $objectRoot),
        '-p:ImportDirectoryBuildProps=false', '-p:ImportDirectoryBuildTargets=false', '-p:ImportDirectoryPackagesProps=false')
    Write-Json (Join-Path $runRoot 'managed-build-inputs.json') @{ executable = $dotnet; arguments = $buildArgs; compileItems = @('InputHost/MsiHostStageEvidence.cs','tools/msi_host_stage_channel_fixture.cs'); productionProjectReferences = @(); packageReferences = @() }
    $build = Run-BoundedChild $dotnet $buildArgs 'managed-build' $snapshotRoot
    Check ($build.exitCode -eq 0) 'Standalone helper plus fixture build succeeded' $build.stdout
    $producerBinary = Join-Path $producerOutput 'MsiHostStageChannelFixture.dll'
    foreach ($file in Get-ChildItem -LiteralPath $producerOutput -File) { $artifactPaths.Add($file.FullName) }

    $vcvarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    Check (Test-Path -LiteralPath $vcvarsPath) 'MSVC x64 toolchain available' $vcvarsPath
    $readerBinary = Join-Path $runRoot 'native\msi_host_stage_channel_reader.exe'
    $nativeObject = Join-Path $runRoot 'native\msi_host_stage_channel_reader.obj'
    $batchPath = Join-Path $runRoot 'native-build.cmd'
    $batchLines = @(
        '@echo off',
        ('call "' + $vcvarsPath + '" >nul'),
        'if errorlevel 1 exit /b %errorlevel%',
        ('cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /DYMCC_MSI_HOST_STAGE_DIAGNOSTIC /I"' + $snapshotRoot + '\native" /I"' + $snapshotRoot + '\deps\json" "' + $snapshotRoot + '\tools\msi_host_stage_channel_reader.cpp" /Fo"' + $nativeObject + '" /Fe"' + $readerBinary + '" /link /INCREMENTAL:NO'),
        'exit /b %errorlevel%'
    )
    Write-Utf8 $batchPath (($batchLines -join "`r`n") + "`r`n")
    Write-Json (Join-Path $runRoot 'native-build-inputs.json') @{ vcvars = $vcvarsPath; vcvarsSha256 = (Get-FileHash -LiteralPath $vcvarsPath -Algorithm SHA256).Hash; actualHeader = (Join-Path $snapshotRoot 'native\msi_host_stage_evidence.h'); codecOnly = $false; faultInjection = $false; apiOverrides = $false; commandFile = $batchPath }
    $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $nativeBuild = Run-BoundedChild $cmdExe @('/d','/c',$batchPath) 'native-build' (Join-Path $runRoot 'native')
    Check ($nativeBuild.exitCode -eq 0) 'Standalone reader with actual Windows header build succeeded' $nativeBuild.stdout
    $artifactPaths.Add($readerBinary)
    $artifactPaths.Add($nativeObject)

    $request = [ordered]@{
        parentPid = $PID; hostInstance = [Guid]::NewGuid().ToString('N')
        requestId = 'stage-fixture-' + [Guid]::NewGuid().ToString('N')
        runId = 'stage-run-' + [Guid]::NewGuid().ToString('N'); command = 'fixture_stage_only'
        sequence = [Convert]::ToInt64([Guid]::NewGuid().ToString('N').Substring(0, 8), 16) + 1
        epoch = [Convert]::ToInt64([Guid]::NewGuid().ToString('N').Substring(0, 8), 16) + 1
        powerGeneration = [Convert]::ToInt64([Guid]::NewGuid().ToString('N').Substring(0, 8), 16) + 1
        incomingChars = 137
    }
    $requestPath = Join-Path $runRoot 'fixture-request.json'
    Write-Json $requestPath $request
    $realMappingTestExecuted = $true
    $producerChild = Start-OwnedChild $dotnet @($producerBinary,$requestPath) 'producer-fixture' $snapshotRoot
    $producerReady = Receive-Reply $producerChild
    $realMappingCreated = $producerReady.realMappingCreated
    Check ($producerReady.processId -eq $producerChild.pid -and $producerReady.actualTryCreateCalled -and
        $realMappingCreated) 'Actual TryCreate created a fresh mapping in dedicated producer' $producerReady
    Assert-ProducerScope $producerReady.snapshot 'created'
    $productionAssemblyAbsent = $producerReady.loadedAssemblies -contains 'MsiHostStageChannelFixture' -and
        @($producerReady.loadedAssemblies | Where-Object { $_ -match '^(InputHost|YeManInputHost|YeManCC\.InputHost)$' }).Count -eq 0
    Check $productionAssemblyAbsent 'No InputHost production assembly loaded' $producerReady.loadedAssemblies
    $expected = [ordered]@{
        parentPid = $request.parentPid; hostPid = $producerChild.pid; hostInstance = $request.hostInstance
        requestId = $request.requestId; runId = $request.runId; command = $request.command
        sequence = $request.sequence; epoch = $request.epoch
    }
    $mappingName = $contract.mapPrefix + $expected.parentPid + '-' + $expected.hostPid + '-' + $expected.hostInstance
    Check ($producerReady.snapshot.mapName -ceq $mappingName) 'Mapping uses own fixture IDs and fresh random HostInstance' $mappingName
    Write-Json (Join-Path $runRoot 'native-expected.json') $expected
    $readerChild = Start-OwnedChild $readerBinary @() 'reader-fixture' $snapshotRoot
    $readerReady = Receive-Reply $readerChild
    Check ($readerReady.processId -eq $readerChild.pid -and $readerChild.pid -ne $producerChild.pid -and
        $readerReady.actualWindowsHeaderReadCompiled) 'C++ reader is a different process using actual Windows adapter' $readerReady

    $created = Read-Case '01-created' $expected
    Check ($created.row.observed -and $created.row.phase -ceq 'created' -and
        -not $created.row.currentRequestMatchesNativeRequest -and -not $created.row.requestIdentityKnown) 'Created real mapping has no active request' $created.row
    $published = Send-Command $producerChild @{ op = 'publish' }
    Assert-ProducerScope $published.snapshot 'published'
    Check ($published.inputSubmissions -eq 0 -and $published.snapshot.phase -eq 10) 'Diagnostic checkpoints reach write-returned without input submission' $published
    $matched = Read-Case '02-exact-request' $expected $true
    $row = $matched.row
    Check ($row.observed -and $row.currentRequestMatchesNativeRequest -and $row.requestIdentityKnown -and
        $row.requestId -ceq $request.requestId -and $row.runId -ceq $request.runId -and
        $row.command -ceq $request.command -and $row.sequence -eq $request.sequence -and
        $row.epoch -eq $request.epoch -and $row.powerGeneration -eq $request.powerGeneration -and
        $row.incomingChars -eq $request.incomingChars -and $row.lastResponseWriteReturnedSequence -eq $request.sequence -and
        $row.phase -ceq 'write-returned' -and $row.publicationSequence -eq $published.snapshot.publicationSequence) 'Cross-process decoder returns exact current request/publication' $row
    foreach ($stamp in @('readBegin','readEnd','handleBegin','decoded','submitBegin','submitEnd','handleEnd','writeBegin','writeEnd')) {
        Check ($row.stagesQpc.$stamp -gt 0 -and $row.stagesQpc.$stamp -eq $published.snapshot.$stamp) "Native stage matches producer: $stamp" $row.stagesQpc.$stamp
    }
    $audit = $matched.readOnlyAudit
    $readOnlyVerified = $audit.opened -and $audit.requestedAccess -eq 4 -and -not $audit.handleInheritable -and
        $audit.readViewOpened -and $audit.viewProtectionQueried -and $audit.viewIsReadOnly -and
        $audit.writeViewDenied -and $audit.writeViewError -eq 5 -and $audit.handlesClosedBeforeReturn
    Check $readOnlyVerified 'Real OS audit: PAGE_READONLY; writable view denied on FILE_MAP_READ handle' $audit
    $collision = Send-Command $producerChild @{ op = 'collision' }
    Assert-ProducerScope $collision.before 'collision before'
    Assert-ProducerScope $collision.after 'collision after'
    Check ($collision.collisionReturnedNull -and $collision.existingSnapshotUnchanged) 'Collision returns null without adopting/changing existing mapping' $collision
    $afterCollision = Read-Case '03-after-collision' $expected
    Check ($afterCollision.row.observed -and $afterCollision.row.currentRequestMatchesNativeRequest -and
        $afterCollision.row.publicationSequence -eq $row.publicationSequence -and
        $afterCollision.row.phaseSinceQpc -eq $row.phaseSinceQpc) 'Existing mapping unchanged and readable after collision' $afterCollision.row
    $wrong = Expected-Copy $expected
    $wrong.hostInstance = [Guid]::NewGuid().ToString('N')
    $wrongInstance = Read-Case '04-wrong-host-instance' $wrong
    Check-Unavailable $wrongInstance 'Wrong expected HostInstance'
    $wrong = Expected-Copy $expected
    $wrong.hostPid = $readerChild.pid
    $wrongPid = Read-Case '05-wrong-host-pid' $wrong
    Check-Unavailable $wrongPid 'Wrong expected HostPID (own reader PID)'
    $wrong = Expected-Copy $expected
    $wrong.parentPid = $readerChild.pid
    $wrongParent = Read-Case '06-wrong-parent-pid' $wrong
    Check-Unavailable $wrongParent 'Wrong expected parent PID (own reader PID)'
    $caseNumber = 7
    foreach ($field in @('requestId','runId','command','sequence','epoch')) {
        $wrong = Expected-Copy $expected
        switch ($field) {
            'requestId' { $wrong[$field] = 'stage-fixture-' + [Guid]::NewGuid().ToString('N') }
            'runId' { $wrong[$field] = 'stage-run-' + [Guid]::NewGuid().ToString('N') }
            'command' { $wrong[$field] = 'fixture_other_stage' }
            default { $wrong[$field] = $expected[$field] + 1 }
        }
        $mismatch = Read-Case ($caseNumber.ToString('00') + '-wrong-' + $field) $wrong
        Check ($mismatch.row.observed -and $mismatch.row.requestIdentityKnown -and
            -not $mismatch.row.currentRequestMatchesNativeRequest -and
            $mismatch.row.requestId -ceq $request.requestId -and $mismatch.row.runId -ceq $request.runId -and
            $mismatch.row.command -ceq $request.command -and $mismatch.row.sequence -eq $request.sequence -and
            $mismatch.row.epoch -eq $request.epoch -and $mismatch.row.publicationSequence -eq $row.publicationSequence) "Exact current request mismatch rejected: $field" $mismatch.row
        $caseNumber++
    }
    $begun = Send-Command $producerChild @{ op = 'beginRead' }
    Assert-ProducerScope $begun.snapshot 'next read'
    $cleared = Read-Case '12-next-read-clears-current-request' $expected
    Check ($cleared.row.observed -and $cleared.row.phase -ceq 'read-await-or-continuation' -and
        -not $cleared.row.currentRequestMatchesNativeRequest -and -not $cleared.row.requestIdentityKnown -and
        $cleared.row.requestId -ceq '' -and $cleared.row.runId -ceq '' -and $cleared.row.command -ceq '' -and
        $cleared.row.sequence -eq 0 -and $cleared.row.epoch -eq 0 -and $cleared.row.powerGeneration -eq 0 -and
        $cleared.row.incomingChars -eq 0 -and $cleared.row.lastResponseWriteReturnedSequence -eq $request.sequence) 'Next read clears active identity, retains completed sequence observation' $cleared.row
    foreach ($stamp in @('readEnd','handleBegin','decoded','submitBegin','submitEnd','handleEnd','writeBegin','writeEnd')) {
        Check ($cleared.row.stagesQpc.$stamp -eq 0) "Next read clears old stamp: $stamp" $cleared.row.stagesQpc.$stamp
    }
    $republished = Send-Command $producerChild @{ op = 'publish' }
    Assert-ProducerScope $republished.snapshot 'republished'
    $matchedAgain = Read-Case '13-republished-exact-request' $expected
    Check ($matchedAgain.row.observed -and $matchedAgain.row.currentRequestMatchesNativeRequest -and
        $matchedAgain.row.publicationSequence -gt $row.publicationSequence) 'Real channel republishes current request coherently' $matchedAgain.row
    $disposed = Send-Command $producerChild @{ op = 'dispose' }
    Assert-ProducerScope $disposed.snapshot 'disposed' $true
    Check ($disposed.disposed -and $disposed.snapshot.phase -eq 11 -and -not $producerChild.process.HasExited -and
        -not $readerChild.process.HasExited) 'Producer Dispose completes with both fixture processes still alive' $disposed
    $afterDispose = Read-Case '14-after-producer-dispose' $expected
    Check-Unavailable $afterDispose 'Mapping after producer Dispose, no retained reader handles'
    $readerQuit = Send-Command $readerChild @{ op = 'quit' }
    Check ($readerQuit.actualHeaderReadCalls -eq 14 -and $readerCalls -eq 14) 'All 14 cases invoke actual native header read()' $readerQuit
    $producerQuit = Send-Command $producerChild @{ op = 'quit' }
    foreach ($child in @($readerChild,$producerChild)) {
        $child.process.StandardInput.Close()
        if (-not $child.process.WaitForExit((Remaining-Ms $child))) {
            Stop-OwnChild $child $true
            throw "Own fixture failed to exit within 25 seconds: $($child.name)"
        }
        Check ($child.process.ExitCode -eq 0 -and $child.timer.ElapsedMilliseconds -lt 29000) "Fixture exits cleanly in under 30 seconds: $($child.name)" $child.timer.ElapsedMilliseconds
        Close-Child $child
    }
}
catch {
    $failure = $_.Exception.Message
    Write-Host "Stage channel verification failed: $failure"
}
finally {
    foreach ($child in $children) {
        if (-not $child.closed) {
            try { Close-Child $child } catch { if ($null -eq $failure) { $failure = $_.Exception.Message } }
        }
    }
    $sourceAfter = @()
    foreach ($item in $binding) {
        $currentHash = (Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash
        $snapshotHash = (Get-FileHash -LiteralPath $item.snapshot -Algorithm SHA256).Hash
        $stable = $currentHash -eq $item.sha256 -and $snapshotHash -eq $item.sha256
        if (-not $stable) { $sourceStable = $false }
        $sourceAfter += [pscustomobject]@{ source = $item.source; initialSha256 = $item.sha256; currentSha256 = $currentHash; snapshotSha256 = $snapshotHash; unchanged = $stable }
    }
    Write-Json (Join-Path $runRoot 'source-hashes-after.json') $sourceAfter
    Write-Json (Join-Path $runRoot 'checks.json') $checks.ToArray()
    Write-Json (Join-Path $runRoot 'process-runs.json') $processRuns.ToArray()
    $allBounded = @($processRuns | Where-Object { $_.lifetimeMs -ge 29000 -or $_.killedForTimeout }).Count -eq 0
    $passed = $null -eq $failure -and $sourceStable -and $allBounded -and $cases.Count -eq 14 -and $realMappingCreated
    $summary = [ordered]@{
        passed = $passed; failure = $failure; realMappingTestExecuted = $realMappingTestExecuted
        realMappingCreated = $realMappingCreated; actualNativeHeaderReadCalls = $readerCalls
        caseCount = $cases.Count; checkCount = $checks.Count
        failedCheckCount = @($checks | Where-Object { -not $_.passed }).Count
        scope = 'OS-memory-channel verification only; fixture checkpoints and metadata. Does not establish G3E, input submission, tuple admission, consumer acceptance, or Steam correctness.'
        earlierSyntheticEvidenceSupersededForMappingCoverageOnly = $true
        readOnlyVerified = $readOnlyVerified
        readOnlyScope = 'Actual unmodified header requests FILE_MAP_READ (bound source audit). Separate real OS audit verifies read-only view and denial of writable view on FILE_MAP_READ handle; reader writes no mapping bytes.'
        identityMismatchScope = 'Wrong expected HostInstance/HostPID/parent PID selects an absent fixture mapping and is unknown; no conflicting-payload mapping is manufactured.'
        snapshotScopesUnchanged = $null -eq $failure -and $cases.Count -eq 14
        sourceStable = $sourceStable; sourceBinding = $binding.ToArray()
        sdkVersion = $sdkVersion; targetFramework = 'net10.0'
        compiledManagedSources = @('InputHost/MsiHostStageEvidence.cs','tools/msi_host_stage_channel_fixture.cs')
        productionAssemblyReferences = @(); productionAssemblyAbsenceVerified = $productionAssemblyAbsent
        productionOrProbeExecuted = $false; inputSubmissions = 0; deviceEnumerations = 0
        steamCalls = 0; hidHideCalls = 0; serviceTaskRegistryCalls = 0; oemCalls = 0
        producerBinary = $producerBinary; readerBinary = $readerBinary
        fixtureProcessCount = @($processRuns | Where-Object { $_.name -in @('producer-fixture','reader-fixture') }).Count
        allChildrenCreateNoWindow = $true; allChildrenUseShellExecute = $false
        childLifetimeLimitMs = 25000; allChildrenCompletedUnder30Seconds = $allBounded
        processRuns = $processRuns.ToArray(); outputRoot = $runRoot
        artifactBinding = Join-Path $runRoot 'artifact-binding.json'
    }
    Write-Json (Join-Path $runRoot 'verification-summary.json') $summary
    $artifacts = foreach ($path in $artifactPaths | Sort-Object -Unique) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            [pscustomobject]@{ path = $path; bytes = (Get-Item -LiteralPath $path).Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
        }
    }
    Write-Json (Join-Path $runRoot 'artifact-binding.json') @($artifacts)
    Write-Host (ConvertTo-Json -InputObject @{ passed = $passed; realMappingTestExecuted = $realMappingTestExecuted; caseCount = $cases.Count; checkCount = $checks.Count; failure = $failure; summary = (Join-Path $runRoot 'verification-summary.json') } -Depth 4)
}
if (-not $passed) { exit 1 }
