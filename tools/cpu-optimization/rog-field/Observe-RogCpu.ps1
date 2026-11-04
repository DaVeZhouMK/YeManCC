<#
.SYNOPSIS
Passive, bounded external CPU sampling of an exact YMCC root and initial InputHost descendants.
.DESCRIPTION
Windows PowerShell 5.1 / PowerShell 7 on Windows. The human controls all applications,
controllers, gyro and power state. No WPR, thread enumeration, elevation or process control.
One Win32_Process snapshot only; later starts/replacements are UNKNOWN, never rebound.
Exit codes: 0 COMPLETE for the initially bound cohort; 2 PARTIAL; 1 ERROR.
COMPLETE is a collection result, not full Host lifecycle coverage or CPU acceptance.
.EXAMPLE
.\Observe-RogCpu.ps1 -ExpectedImagePath 'C:\YMCC\YeManCC.exe' -EvidenceRoot 'D:\Evidence' -Scenario idle
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$ExpectedImagePath,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$EvidenceRoot,
    [Parameter(Mandatory = $true)][ValidatePattern('\A[A-Za-z0-9][A-Za-z0-9_-]{0,47}\z')][string]$Scenario,
    [ValidateRange(10, 600)][int]$DurationSeconds = 120,
    [ValidateRange(100, 1000)][int]$IntervalMs = 250,
    [uint32]$ExpectedRootProcessId = 0,
    [ValidatePattern('\A[1-9][0-9]{0,18}\z')][string]$ExpectedRootBirthFileTime100ns = ''
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$rogEntryUtc = [DateTime]::UtcNow.ToString('o')
$rogEntryClock = [Diagnostics.Stopwatch]::StartNew()
$rogEntrySelf = [Diagnostics.Process]::GetCurrentProcess()
try { $rogEntryCpu100ns = $rogEntrySelf.TotalProcessorTime.Ticks } finally { $rogEntrySelf.Dispose() }
$rogInvariant = [Globalization.CultureInfo]::InvariantCulture
$rogUtf8 = [Text.UTF8Encoding]::new($false)
$rogReaders = [Collections.Generic.List[object]]::new()
$rogTargets = [Collections.Generic.List[object]]::new()
$rogIssues = [Collections.Generic.List[object]]::new()
$rogUnknowns = [Collections.Generic.List[object]]::new()
$rogWriter = $null
$rogRunPath = $null
$rogIdentity = $null
$rogSelf = $null
$rogSelfStart = $null
$rogSelfFinal = $null
$rogFrequency = $null
$rogLogical = $null
$rogLogicalEvidence = $null
$rogSampleStart = $null
$rogSampleEnd = $null
$rogRows = 0
$rogMissedSlots = 0
$rogWorkTotalMs = 0.0
$rogWorkMaxMs = 0.0
$rogReadTotalMs = 0.0
$rogPreviousWorkMs = $null
$rogLoopFinished = $false
$rogStatus = 'ERROR'
$rogExitCode = 1
$rogFinalChecks = [Collections.Generic.List[object]]::new()

function Save-RogJson([string]$Path, [object]$Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 18), $rogUtf8)
}
function Add-RogIssue([string]$Kind, [string]$Detail) {
    $rogIssues.Add([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); kind = $Kind; detail = $Detail })
}
function Get-RogFullPath([string]$Path) {
    if ($Path -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+[\\/])') {
        throw "An absolute drive or UNC path is required: $Path"
    }
    return [IO.Path]::GetFullPath($Path)
}
function Get-RogSha([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $algorithm.Dispose()
    }
}
function Get-RogCimBirth([object]$Process) {
    if ($null -eq $Process.CreationDate) { throw 'Snapshot CreationDate is unavailable.' }
    return ([DateTime]$Process.CreationDate).ToUniversalTime().ToFileTimeUtc()
}
function Read-RogHostCompanions([string]$ImagePath, [uint32]$TargetId, [string]$Phase) {
    $snapshot = [Ymcc.RogField.V1.DiskCompanionIdentity]::Read($ImagePath)
    if ($snapshot.Status -ne 'OK') {
        Add-RogIssue 'HOST_COMPANION_UNKNOWN' ('PID ' + $TargetId + ' disk companion identity is UNKNOWN at ' + $Phase + '.')
        foreach ($file in $snapshot.Files) {
            if ($file.Status -ne 'OK') {
                $rogUnknowns.Add([pscustomobject]@{ kind = 'HOST_DISK_COMPANION'; pid = $TargetId
                    phase = $Phase; name = $file.Name; path = $file.Path; detail = $file.Error })
            }
        }
    }
    return $snapshot
}
function Compare-RogHostCompanions([object]$Initial, [object]$Final, [uint32]$TargetId) {
    $files = [Collections.Generic.List[object]]::new()
    $changed = $false; $unknown = $false
    foreach ($name in @('YeManInputHost.dll', 'HIDMaestro.Core.dll', 'YeManInputHost.runtimeconfig.json')) {
        $before = @(); $after = @()
        if ($null -ne $Initial) { $before = @($Initial.Files | Where-Object { $_.Name -ceq $name }) }
        if ($null -ne $Final) { $after = @($Final.Files | Where-Object { $_.Name -ceq $name }) }
        $file = [ordered]@{ name = $name; status = 'UNKNOWN'; initialDiskSha256 = $null
            finalDiskSha256 = $null; initialSizeBytes = $null; finalSizeBytes = $null }
        if ($before.Count -eq 1) { $file.initialDiskSha256 = $before[0].DiskSha256; $file.initialSizeBytes = $before[0].SizeBytes }
        if ($after.Count -eq 1) { $file.finalDiskSha256 = $after[0].DiskSha256; $file.finalSizeBytes = $after[0].SizeBytes }
        if ($before.Count -eq 1 -and $after.Count -eq 1 -and $before[0].Status -eq 'OK' -and $after[0].Status -eq 'OK') {
            if ($file.initialDiskSha256 -cne $file.finalDiskSha256 -or $file.initialSizeBytes -cne $file.finalSizeBytes) {
                $file.status = 'CHANGED'; $changed = $true
            } else { $file.status = 'UNCHANGED' }
        } else { $unknown = $true }
        $files.Add($file)
    }
    if ($changed) { Add-RogIssue 'HOST_COMPANION_CHANGED' ('PID ' + $TargetId + ' disk companion SHA/size changed between binding and finalization; numeric CPU retained.') }
    # Also covers a missing initial snapshot without inventing a baseline.
    if ($unknown -and $null -eq $Initial) { Add-RogIssue 'HOST_COMPANION_UNKNOWN' ('PID ' + $TargetId + ' has no initial disk companion snapshot.') }
    return [ordered]@{ status = if ($changed) { 'CHANGED' } elseif ($unknown) { 'UNKNOWN' } else { 'UNCHANGED' }
        hasChanges = $changed; hasUnknown = $unknown; files = $files
        scope = 'DISK_COMPANION_IDENTITY_ONLY_NOT_LOADED_MODULE_PROOF; two endpoint observations do not prove stability between them.' }
}
function New-RogBinding([object]$Process, [string]$Role, [string]$ExpectedPath) {
    $identity = [ordered]@{
        role = $Role; pid = [uint32]$Process.ProcessId; parentPid = [uint32]$Process.ParentProcessId
        snapshotName = [string]$Process.Name; snapshotImagePath = [string]$Process.ExecutablePath
        snapshotCreationUtc = $null; snapshotCreationFileTime100ns = $null
        birthFileTime100ns = $null; birthUtc = $null; cimBirthDifference100ns = $null
        cimPrecisionTolerance100ns = 9; imagePath = $null; diskSha256 = $null; imagePathAuthority = $null
        heldAccessMask = '0x00101000'; queryAndSynchronizeHandleHeld = $false
        rawAtBind = $null; rawAfterHash = $null; ancestry = @(); status = 'REJECTED'; error = $null
        diskCompanionsAtBind = $null; diskCompanionsFinal = $null; diskCompanionComparison = $null
    }
    $binding = [pscustomobject]@{ identity = $identity; reader = $null; previous = $null; first = $null; last = $null
        validIntervals = 0; invalidReads = 0; sampledWallSeconds = 0.0; cpu100ns = [decimal]0
        maximumMachinePercent = $null; maximumSingleCorePercent = $null }
    try {
        $snapshotBirth = Get-RogCimBirth $Process
        $identity.snapshotCreationFileTime100ns = $snapshotBirth.ToString($rogInvariant)
        $identity.snapshotCreationUtc = [DateTime]::FromFileTimeUtc($snapshotBirth).ToString('o')
        $binding.reader = [Ymcc.RogField.V1.ReadOnlyCounters]::Open([uint32]$Process.ProcessId)
        $rogReaders.Add($binding.reader)
        $identity.queryAndSynchronizeHandleHeld = $true
        $raw = $binding.reader.Read()
        $identity.rawAtBind = $raw
        if ($raw.Status -ne 'OK') { throw ('Initial counter/liveness read failed: ' + $raw.Status + '; ' + $raw.Error) }
        $nativeBirth = [long]::Parse($raw.CreationFileTime100ns, $rogInvariant)
        $identity.birthFileTime100ns = $raw.CreationFileTime100ns
        $identity.birthUtc = [DateTime]::FromFileTimeUtc($nativeBirth).ToString('o')
        $difference = $nativeBirth - $snapshotBirth
        $identity.cimBirthDifference100ns = $difference
        # CIM dates have microsecond precision; all later checks use exact native FILETIME.
        if ($difference -lt 0 -or $difference -gt 9) { throw 'Snapshot/native birth mismatch (possible PID reuse).' }
        $actualPath = Get-RogFullPath ($binding.reader.ImagePath())
        $identity.imagePath = $actualPath
        if ([string]::IsNullOrWhiteSpace($ExpectedPath)) {
            if ($Role -ne 'host' -and $Role -ne 'lineageWitness') { throw 'Root requires an explicit expected full image path.' }
            if (-not [string]::Equals([IO.Path]::GetFileName($actualPath), [string]$Process.Name, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Held image basename differs from the one-time snapshot name.'
            }
            $fullExpected = $actualPath
            $identity.imagePathAuthority = 'HELD_NATIVE_PID_BIRTH_AND_SNAPSHOT_NAME_CIM_PATH_UNAVAILABLE'
        } else {
            $fullExpected = Get-RogFullPath $ExpectedPath
            $identity.imagePathAuthority = 'HELD_NATIVE_PID_BIRTH_AND_EXPECTED_FULL_PATH'
        }
        if (-not [string]::Equals($actualPath, $fullExpected, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Held native image path differs from the discovered/expected path.'
        }
        if ([string]::Equals([IO.Path]::GetFileName($actualPath), 'YeManInputHost.exe', [StringComparison]::OrdinalIgnoreCase)) {
            # Read once even if this Host first appears as an intermediary in another Host's ancestry.
            $identity.diskCompanionsAtBind = Read-RogHostCompanions $actualPath $identity.pid 'BIND'
        }
        $identity.diskSha256 = Get-RogSha $actualPath
        $after = $binding.reader.Read()
        $identity.rawAfterHash = $after
        if ($after.Status -ne 'OK' -or $after.CreationFileTime100ns -cne $identity.birthFileTime100ns) {
            throw 'Counter/liveness/identity changed during startup hashing.'
        }
        if (-not [string]::Equals($binding.reader.ImagePath(), $actualPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Held native image path changed during startup.'
        }
        $identity.status = 'BOUND'
    } catch { $identity.error = $_.Exception.Message }
    return $binding
}
function Get-RogLineage([object]$HostProcess, [object]$RootProcess, [hashtable]$ById) {
    $chain = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new()
    $child = $HostProcess
    try {
        while ($true) {
            $key = ([uint32]$child.ProcessId).ToString($rogInvariant)
            if (-not $seen.Add($key)) { throw 'Cycle in snapshot parent chain.' }
            $parentId = [uint32]$child.ParentProcessId
            if ($parentId -eq 0) { return [pscustomobject]@{ status = 'OUTSIDE_ROOT'; chain = @(); reason = $null } }
            $parentKey = $parentId.ToString($rogInvariant)
            if (-not $ById.ContainsKey($parentKey)) { throw 'Snapshot parent absent; ancestry cannot be proved.' }
            $parent = $ById[$parentKey]
            if ((Get-RogCimBirth $parent) -gt (Get-RogCimBirth $child)) {
                throw 'Snapshot parent born after child (reused parent PID); ancestry rejected.'
            }
            if ($parentId -eq [uint32]$RootProcess.ProcessId) {
                return [pscustomobject]@{ status = 'DESCENDANT'; chain = $chain.ToArray(); reason = $null }
            }
            $chain.Add($parent)
            $child = $parent
        }
    } catch { return [pscustomobject]@{ status = 'UNKNOWN'; chain = $chain.ToArray(); reason = $_.Exception.Message } }
}
function Get-RogSample([object]$Binding) {
    $raw = $Binding.reader.Read()
    $metric = [ordered]@{ status = 'UNKNOWN'; actualIntervalSeconds = $null; deltaKernel100ns = $null
        deltaUser100ns = $null; deltaCpu100ns = $null; deltaCycles = $null
        machineCpuPercent = $null; singleCoreCpuPercent = $null; error = $null }
    if ($raw.Status -eq 'OK' -and $raw.CreationFileTime100ns -cne $Binding.identity.birthFileTime100ns) {
        $raw.Status = 'IDENTITY_ERROR'; $raw.Error = 'Exact held-handle native creation FILETIME differs from binding.'
    }
    if ($raw.Status -ne 'OK') {
        $Binding.invalidReads++
        if ($Binding.invalidReads -eq 1) { Add-RogIssue $raw.Status ('PID ' + $Binding.identity.pid + ': ' + $raw.Error) }
        $metric.status = $raw.Status; $metric.error = $raw.Error
        $Binding.previous = $null
    } else {
        if ($null -eq $Binding.first) { $Binding.first = $raw }
        $Binding.last = $raw
        if ($null -eq $Binding.previous) { $metric.status = 'BASELINE' } else {
            try {
                $previous = $Binding.previous
                $qpcDelta = [long]::Parse($raw.QpcMidpoint, $rogInvariant) - [long]::Parse($previous.QpcMidpoint, $rogInvariant)
                $kernel = [decimal]::Parse($raw.Kernel100ns, $rogInvariant) - [decimal]::Parse($previous.Kernel100ns, $rogInvariant)
                $user = [decimal]::Parse($raw.User100ns, $rogInvariant) - [decimal]::Parse($previous.User100ns, $rogInvariant)
                $cycles = [decimal]::Parse($raw.Cycles, $rogInvariant) - [decimal]::Parse($previous.Cycles, $rogInvariant)
                if ($qpcDelta -le 0 -or $kernel -lt 0 -or $user -lt 0 -or $cycles -lt 0) { throw 'Non-positive QPC or decreasing cumulative counters.' }
                $seconds = [double]$qpcDelta / [double]$rogFrequency
                $cpu = $kernel + $user
                $single = 100.0 * [double]$cpu / (10000000.0 * $seconds)
                $metric.status = 'OK'; $metric.actualIntervalSeconds = $seconds
                $metric.deltaKernel100ns = $kernel.ToString('0', $rogInvariant)
                $metric.deltaUser100ns = $user.ToString('0', $rogInvariant)
                $metric.deltaCpu100ns = $cpu.ToString('0', $rogInvariant)
                $metric.deltaCycles = $cycles.ToString('0', $rogInvariant)
                $metric.singleCoreCpuPercent = $single
                if ($null -ne $rogLogical -and $rogLogical -gt 0) { $metric.machineCpuPercent = $single / [double]$rogLogical }
                $Binding.validIntervals++; $Binding.sampledWallSeconds += $seconds; $Binding.cpu100ns += $cpu
                if ($null -ne $metric.machineCpuPercent -and ($null -eq $Binding.maximumMachinePercent -or $metric.machineCpuPercent -gt $Binding.maximumMachinePercent)) {
                    $Binding.maximumMachinePercent = $metric.machineCpuPercent
                }
                if ($null -eq $Binding.maximumSingleCorePercent -or $single -gt $Binding.maximumSingleCorePercent) { $Binding.maximumSingleCorePercent = $single }
            } catch {
                $metric.status = 'DELTA_ERROR'; $metric.error = $_.Exception.Message
                $Binding.invalidReads++; Add-RogIssue 'DELTA_ERROR' ('PID ' + $Binding.identity.pid + ': ' + $metric.error)
            }
        }
        $Binding.previous = $raw
    }
    return [pscustomobject]@{ role = $Binding.identity.role; pid = $Binding.identity.pid
        boundBirthFileTime100ns = $Binding.identity.birthFileTime100ns; raw = $raw; interval = $metric }
}
function Get-RogStatistics([object]$Binding) {
    $averageSingle = $null; $averageMachine = $null
    if ($Binding.validIntervals -gt 0 -and $Binding.sampledWallSeconds -gt 0) {
        $averageSingle = 100.0 * [double]$Binding.cpu100ns / (10000000.0 * $Binding.sampledWallSeconds)
        if ($null -ne $rogLogical -and $rogLogical -gt 0) { $averageMachine = $averageSingle / [double]$rogLogical }
    }
    return [pscustomobject]@{ role = $Binding.identity.role; pid = $Binding.identity.pid
        birthFileTime100ns = $Binding.identity.birthFileTime100ns; birth = $Binding.identity.birthFileTime100ns
        imagePath = $Binding.identity.imagePath; image = $Binding.identity.imagePath; diskSha256 = $Binding.identity.diskSha256
        validIntervals = $Binding.validIntervals; invalidReads = $Binding.invalidReads
        coveredSeconds = $Binding.sampledWallSeconds
        deltaCpu100ns = if ($Binding.validIntervals -gt 0) { $Binding.cpu100ns.ToString('0', $rogInvariant) } else { $null }
        averageMachineCpuPercent = $averageMachine; averageSingleCoreCpuPercent = $averageSingle
        maximumIntervalMachineCpuPercent = $Binding.maximumMachinePercent
        maximumIntervalSingleCoreCpuPercent = $Binding.maximumSingleCorePercent
        firstAcceptedRaw = $Binding.first; lastAcceptedRaw = $Binding.last
        diskCompanionsAtBind = if ($Binding.identity.role -eq 'host') { $Binding.identity.diskCompanionsAtBind } else { $null }
        diskCompanionsFinal = if ($Binding.identity.role -eq 'host') { $Binding.identity.diskCompanionsFinal } else { $null }
        diskCompanionComparison = if ($Binding.identity.role -eq 'host') { $Binding.identity.diskCompanionComparison } else { $null } }
}
try {
    $rootPath = Get-RogFullPath $EvidenceRoot
    if (($ExpectedRootProcessId -gt 0) -ne (-not [string]::IsNullOrWhiteSpace($ExpectedRootBirthFileTime100ns))) {
        throw 'Explicit root selection requires both ExpectedRootProcessId and ExpectedRootBirthFileTime100ns.'
    }
    [IO.Directory]::CreateDirectory($rootPath) | Out-Null
    $runName = 'ROG-' + $Scenario + '-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N')
    $proposedPath = Join-Path $rootPath $runName
    New-Item -ItemType Directory -Path $proposedPath -ErrorAction Stop | Out-Null
    $rogRunPath = $proposedPath
    $rogIdentity = [ordered]@{
        schemaVersion = 1; tool = 'Observe-RogCpu'; status = 'STARTING'; scenario = $Scenario
        startedUtc = $rogEntryUtc; parameters = [ordered]@{ expectedImagePath = $ExpectedImagePath
            evidenceRoot = $rootPath; durationSeconds = $DurationSeconds; intervalMs = $IntervalMs
            expectedRootProcessId = if ($ExpectedRootProcessId -gt 0) { $ExpectedRootProcessId } else { $null }
            expectedRootBirthFileTime100ns = if ($ExpectedRootProcessId -gt 0) { $ExpectedRootBirthFileTime100ns } else { $null } }
        tools = [ordered]@{ observerPath = $PSCommandPath; observerSha256 = $null
            countersPath = (Join-Path $PSScriptRoot 'ReadOnlyCounters.cs'); countersSha256 = $null }
        powershell = $PSVersionTable.PSVersion.ToString(); qpcFrequency = $null; logicalProcessors = $null; logicalProcessorCount = $null
        processSnapshot = [ordered]@{ queriesAttempted = 0; queriesSucceeded = 0; startedUtc = $null; completedUtc = $null
            rootMatches = $null; initialHostCount = 0; unrelatedSameNameHostCount = 0; hostStatus = 'UNKNOWN' }
        targets = [Collections.Generic.List[object]]::new(); sampler = $null
        lineageWitnesses = [Collections.Generic.List[object]]::new(); rejectedHosts = [Collections.Generic.List[object]]::new()
        environment = [ordered]@{ os = $null; model = $null; cpu = $null; powerAtSetup = $null }
        unknowns = $rogUnknowns; issues = $rogIssues
        scope = 'Exact root and proven initial InputHost descendants only; no WebView or thread counters.'
        laterHostInventory = 'UNKNOWN_NOT_RESCANNED'; allHostCpuPercent = $null
        diskHashMeaning = 'On-disk image SHA256 at binding and final check; loaded-memory bytes are UNKNOWN.'
        hostCompanionIdentityMeaning = 'Three same-directory disk companions, read at binding/finalization only; NOT loaded-module proof. Missing/read failure UNKNOWN; endpoint changes or unknown identity make collection PARTIAL without changing CPU counters.'
        applicationControllerGyroPowerState = 'HUMAN_CONTROLLED_NOT_INFERRED'; cpuAcceptance = $false
    }
    Save-RogJson (Join-Path $rogRunPath 'IDENTITY.json') $rogIdentity
    $rogWriter = [IO.StreamWriter]::new((Join-Path $rogRunPath 'CPU.jsonl'), $false, $rogUtf8)
    $rogWriter.AutoFlush = $true
    Write-Verbose ('Evidence directory: ' + $rogRunPath)
    $rogIdentity.tools.observerSha256 = Get-RogSha $PSCommandPath
    $rogIdentity.tools.countersSha256 = Get-RogSha $rogIdentity.tools.countersPath
    $expectedPath = Get-RogFullPath $ExpectedImagePath
    if (-not [string]::Equals([IO.Path]::GetFileName($expectedPath), 'YeManCC.exe', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ExpectedImagePath must be the full path to YeManCC.exe.'
    }
    if ($env:OS -ne 'Windows_NT') { throw 'This observer requires Windows.' }
    if ($null -ne ('Ymcc.RogField.V1.ReadOnlyCounters' -as [type])) {
        throw 'Counter type is already loaded; use a fresh PowerShell process to bind the recorded source SHA.'
    }
    Add-Type -TypeDefinition ([IO.File]::ReadAllText($rogIdentity.tools.countersPath)) -Language CSharp -ErrorAction Stop
    if ((Get-RogSha $rogIdentity.tools.countersPath) -cne $rogIdentity.tools.countersSha256) { throw 'Counter source changed during compilation.' }
    $rogFrequency = [Ymcc.RogField.V1.ReadOnlyCounters]::QpcFrequency()
    $nativeLogical = $null
    try { $nativeLogical = [Ymcc.RogField.V1.ReadOnlyCounters]::LogicalProcessors() } catch {
        $rogUnknowns.Add([pscustomobject]@{ kind = 'ENVIRONMENT'; field = 'nativeLogicalProcessors'; detail = $_.Exception.Message })
    }
    $environmentLogical = [Environment]::ProcessorCount
    $rogIdentity.qpcFrequency = $rogFrequency.ToString($rogInvariant)

    # Self uses its own held native handle, not another Win32_Process query.
    $selfReader = [Ymcc.RogField.V1.ReadOnlyCounters]::Open([uint32]$PID)
    $rogReaders.Add($selfReader)
    $rogSelfStart = $selfReader.Read()
    if ($rogSelfStart.Status -ne 'OK') { throw 'Sampler initial native counters unavailable.' }
    $selfImage = $selfReader.ImagePath()
    $selfIdentity = [ordered]@{ role = 'sampler'; pid = [uint32]$PID; birthFileTime100ns = $rogSelfStart.CreationFileTime100ns
        imagePath = $selfImage; diskSha256 = (Get-RogSha $selfImage); heldAccessMask = '0x00101000'; rawAtBind = $rogSelfStart }
    $rogIdentity.sampler = $selfIdentity
    $rogSelf = [pscustomobject]@{ identity = $selfIdentity; reader = $selfReader; previous = $null; first = $null; last = $null
        validIntervals = 0; invalidReads = 0; sampledWallSeconds = 0.0; cpu100ns = [decimal]0
        maximumMachinePercent = $null; maximumSingleCorePercent = $null }

    # Nonessential environment queries: one attempt per class, UNKNOWN on failure.
    foreach ($environmentQuery in @(
        @{ key = 'os'; class = 'Win32_OperatingSystem'; properties = @('Caption', 'Version', 'BuildNumber', 'OSArchitecture') },
        @{ key = 'model'; class = 'Win32_ComputerSystem'; properties = @('Manufacturer', 'Model', 'NumberOfLogicalProcessors') },
        @{ key = 'cpu'; class = 'Win32_Processor'; properties = @('Name', 'Manufacturer', 'NumberOfCores', 'NumberOfLogicalProcessors') }
    )) {
        try {
            $envRows = @(Get-CimInstance -ClassName $environmentQuery.class -Property $environmentQuery.properties -OperationTimeoutSec 5 -ErrorAction Stop |
                Select-Object -Property $environmentQuery.properties)
            if ($envRows.Count -eq 0) { throw 'Empty CIM result.' }
            $rogIdentity.environment[$environmentQuery.key] = [ordered]@{ status = 'OK'; atUtc = [DateTime]::UtcNow.ToString('o'); data = $envRows }
        } catch {
            $rogIdentity.environment[$environmentQuery.key] = [ordered]@{ status = 'UNKNOWN'; error = $_.Exception.Message }
            $rogUnknowns.Add([pscustomobject]@{ kind = 'ENVIRONMENT'; field = $environmentQuery.key; detail = $_.Exception.Message })
        }
    }
    try { $rogIdentity.environment.powerAtSetup = [Ymcc.RogField.V1.ReadOnlyCounters]::ReadPowerOnce() } catch {
        $rogIdentity.environment.powerAtSetup = [ordered]@{ Status = 'UNKNOWN'; error = $_.Exception.Message }
    }
    $cimLogical = $null
    if ($rogIdentity.environment.model.status -eq 'OK') {
        $modelRows = $rogIdentity.environment.model.data
        if ($modelRows.Count -eq 1 -and $null -ne $modelRows[0].NumberOfLogicalProcessors -and
            [long]$modelRows[0].NumberOfLogicalProcessors -gt 0) { $cimLogical = [uint32]$modelRows[0].NumberOfLogicalProcessors }
    }
    $logicalSource = 'UNKNOWN'
    if ($null -ne $cimLogical) { $rogLogical = $cimLogical; $logicalSource = 'CIM.Win32_ComputerSystem.NumberOfLogicalProcessors' }
    elseif ($null -ne $nativeLogical -and $nativeLogical -gt 0) {
        $rogLogical = $nativeLogical; $logicalSource = 'GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)'
    } else {
        $rogUnknowns.Add([pscustomobject]@{ kind = 'MACHINE_CPU_DENOMINATOR'; detail = 'Machine logical count unavailable; process-available count is not substituted. Machine CPU percent remains null.' })
    }
    $countDifferences = [Collections.Generic.List[object]]::new()
    foreach ($count in @(@{ source = 'CIM.Win32_ComputerSystem'; value = $cimLogical },
        @{ source = 'GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)'; value = $nativeLogical },
        @{ source = 'Environment.ProcessorCount'; value = $environmentLogical })) {
        if ($null -ne $rogLogical -and $null -ne $count.value -and $count.value -ne $rogLogical) {
            $countDifferences.Add([pscustomobject]@{ source = $count.source; reported = $count.value; selected = $rogLogical })
        }
    }
    $rogLogicalEvidence = [ordered]@{ selected = $rogLogical; source = $logicalSource
        cimComputerSystem = $cimLogical; nativeAllProcessorGroups = $nativeLogical
        environmentProcessAvailable = $environmentLogical; inconsistent = ($countDifferences.Count -gt 0)
        differences = $countDifferences; note = 'Environment.ProcessorCount may reflect affinity/job restrictions; never used as the machine denominator.' }
    $rogIdentity.logicalProcessors = $rogLogical
    $rogIdentity.logicalProcessorCount = $rogLogicalEvidence
    $rogIdentity.processSnapshot.queriesAttempted = 1
    $rogIdentity.processSnapshot.startedUtc = [DateTime]::UtcNow.ToString('o')
    $snapshot = @(Get-CimInstance -ClassName Win32_Process -Property ProcessId,ParentProcessId,Name,ExecutablePath,CreationDate -OperationTimeoutSec 10 -ErrorAction Stop)
    $rogIdentity.processSnapshot.queriesSucceeded = 1
    $rogIdentity.processSnapshot.completedUtc = [DateTime]::UtcNow.ToString('o')
    $byId = @{}
    foreach ($process in $snapshot) { $byId[([uint32]$process.ProcessId).ToString($rogInvariant)] = $process }
    $roots = @($snapshot | Where-Object {
        if ($ExpectedRootProcessId -gt 0) { [uint32]$_.ProcessId -eq $ExpectedRootProcessId }
        else {
            -not [string]::IsNullOrWhiteSpace([string]$_.ExecutablePath) -and
            [string]::Equals([string]$_.ExecutablePath, $expectedPath, [StringComparison]::OrdinalIgnoreCase)
        }
    })
    $rogIdentity.processSnapshot.rootMatches = $roots.Count
    if ($roots.Count -ne 1) { throw ('Expected exactly one exact-image root in the one-time snapshot; found ' + $roots.Count + '.') }
    $rootProcess = $roots[0]
    $rootBinding = New-RogBinding $rootProcess 'root' $expectedPath
    $rogIdentity.targets.Add($rootBinding.identity)
    if ($rootBinding.identity.status -ne 'BOUND') { throw ('Root identity rejected: ' + $rootBinding.identity.error) }
    if ($ExpectedRootProcessId -gt 0 -and $rootBinding.identity.birthFileTime100ns -cne $ExpectedRootBirthFileTime100ns) {
        throw 'Explicit root native birth differs from the expected instance; no sampling started.'
    }
    $rogTargets.Add($rootBinding)
    $bindingCache = @{}
    $bindingCache[([uint32]$rootProcess.ProcessId).ToString($rogInvariant)] = $rootBinding
    foreach ($hostProcess in @($snapshot | Where-Object { [string]::Equals([string]$_.Name, 'YeManInputHost.exe', [StringComparison]::OrdinalIgnoreCase) })) {
        $lineage = Get-RogLineage $hostProcess $rootProcess $byId
        if ($lineage.status -eq 'OUTSIDE_ROOT') { $rogIdentity.processSnapshot.unrelatedSameNameHostCount++; continue }
        if ($lineage.status -eq 'UNKNOWN') {
            $rogIdentity.rejectedHosts.Add([pscustomobject]@{ pid = [uint32]$hostProcess.ProcessId; status = 'UNKNOWN_ANCESTRY'; error = $lineage.reason })
            continue
        }
        $lineageOkay = $true
        foreach ($ancestor in $lineage.chain) {
            $ancestorKey = ([uint32]$ancestor.ProcessId).ToString($rogInvariant)
            if (-not $bindingCache.ContainsKey($ancestorKey)) {
                $witness = New-RogBinding $ancestor 'lineageWitness' ([string]$ancestor.ExecutablePath)
                $bindingCache[$ancestorKey] = $witness
                $rogIdentity.lineageWitnesses.Add($witness.identity)
            }
            if ($bindingCache[$ancestorKey].identity.status -ne 'BOUND') { $lineageOkay = $false }
        }
        if (-not $lineageOkay) {
            $rogIdentity.rejectedHosts.Add([pscustomobject]@{ pid = [uint32]$hostProcess.ProcessId; status = 'UNKNOWN_ANCESTOR_IDENTITY'; error = 'An intermediary could not be bound to the snapshot instance.' })
            continue
        }
        $hostKey = ([uint32]$hostProcess.ProcessId).ToString($rogInvariant)
        if ($bindingCache.ContainsKey($hostKey)) { $hostBinding = $bindingCache[$hostKey]; $hostBinding.identity.role = 'host' } else {
            $hostBinding = New-RogBinding $hostProcess 'host' ([string]$hostProcess.ExecutablePath)
            $bindingCache[$hostKey] = $hostBinding
        }
        if ($hostBinding.identity.status -eq 'BOUND') {
            $childBinding = $hostBinding
            $ancestry = [Collections.Generic.List[object]]::new()
            foreach ($parentSnapshot in (@($lineage.chain) + @($rootProcess))) {
                $parentKey = ([uint32]$parentSnapshot.ProcessId).ToString($rogInvariant)
                $parentBinding = $bindingCache[$parentKey]
                $ancestry.Add([pscustomobject]@{ pid = $parentBinding.identity.pid
                    birthFileTime100ns = $parentBinding.identity.birthFileTime100ns })
                if ([long]::Parse($parentBinding.identity.birthFileTime100ns, $rogInvariant) -gt
                    [long]::Parse($childBinding.identity.birthFileTime100ns, $rogInvariant)) {
                    $hostBinding.identity.status = 'REJECTED'
                    $hostBinding.identity.error = 'Exact native ancestor birth is later than child; reused parent PID rejected.'
                    break
                }
                $childBinding = $parentBinding
            }
            $hostBinding.identity.ancestry = $ancestry.ToArray()
        }
        $rogIdentity.targets.Add($hostBinding.identity)
        if ($hostBinding.identity.status -eq 'BOUND') { $rogTargets.Add($hostBinding); $rogIdentity.processSnapshot.initialHostCount++ } else {
            $rogIdentity.rejectedHosts.Add($hostBinding.identity)
        }
    }
    if ($rogIdentity.processSnapshot.initialHostCount -eq 0) {
        Add-RogIssue 'HOST_UNKNOWN' 'No InputHost descendant could be bound at the one-time snapshot; root-only samples remain usable.'
        $rogUnknowns.Add([pscustomobject]@{ kind = 'HOST_MISSING_AT_DISCOVERY'; detail = 'Host CPU is UNKNOWN, not zero.' })
    } elseif ($rogIdentity.rejectedHosts.Count -eq 0) { $rogIdentity.processSnapshot.hostStatus = 'INITIAL_BOUND_SET_ONLY' } else {
        Add-RogIssue 'HOST_UNKNOWN' 'One or more possible Host descendants were rejected or have unknown identity/ancestry.'
    }
    $rogUnknowns.Add([pscustomobject]@{ kind = 'LATER_HOST_INVENTORY'; detail = 'One snapshot only. Later starts, replacements and missing ancestry remain UNKNOWN; no late PID binding.' })
    $rogIdentity.status = 'SAMPLING'
    Save-RogJson (Join-Path $rogRunPath 'IDENTITY.json') $rogIdentity
    $rogStatus = 'PARTIAL'
    $rogSampleStart = [Ymcc.RogField.V1.ReadOnlyCounters]::QpcNow()
    $nextDue = [long]$rogSampleStart
    $intervalTicks = [long][Math]::Round([double]$rogFrequency * $IntervalMs / 1000.0)
    $deadline = [long]($rogSampleStart + [long]$rogFrequency * $DurationSeconds)
    # Include an endpoint at/after the deadline rather than silently losing the final interval.
    while ($true) {
        $now = [Ymcc.RogField.V1.ReadOnlyCounters]::QpcNow()
        if ($now -lt $nextDue) {
            [Threading.Thread]::Sleep([int][Math]::Max(1, [Math]::Ceiling(1000.0 * ($nextDue - $now) / $rogFrequency)))
            continue
        }
        $workStart = $now
        $samples = @($rogTargets | ForEach-Object { Get-RogSample $_ })
        $selfSample = Get-RogSample $rogSelf
        $readEnd = [Ymcc.RogField.V1.ReadOnlyCounters]::QpcNow()
        $readMs = 1000.0 * ($readEnd - $workStart) / $rogFrequency
        $rogReadTotalMs += $readMs
        $row = [ordered]@{ schemaVersion = 1; index = $rogRows; utc = [DateTime]::UtcNow.ToString('o')
            qpcIterationStart = $workStart.ToString($rogInvariant); qpcAfterReads = $readEnd.ToString($rogInvariant)
            qpcFrequency = $rogFrequency.ToString($rogInvariant); logicalProcessors = $rogLogical; logicalProcessorCountSource = $logicalSource
            requestedIntervalMs = $IntervalMs; readWallMs = $readMs; previousIterationWorkWallMs = $rogPreviousWorkMs
            targets = $samples; sampler = $selfSample
            hostStatusAtDiscovery = $rogIdentity.processSnapshot.hostStatus; laterHostInventory = 'UNKNOWN_NOT_RESCANNED'
            allHostMachineCpuPercent = $null; missedSlotsBeforeThisRow = $rogMissedSlots }
        $rogWriter.WriteLine((ConvertTo-Json -InputObject $row -Depth 14 -Compress))
        $rogRows++
        $workEnd = [Ymcc.RogField.V1.ReadOnlyCounters]::QpcNow()
        $rogPreviousWorkMs = 1000.0 * ($workEnd - $workStart) / $rogFrequency
        $rogWorkTotalMs += $rogPreviousWorkMs
        $rogWorkMaxMs = [Math]::Max($rogWorkMaxMs, $rogPreviousWorkMs)
        $rogSampleEnd = $readEnd
        if ($workStart -ge $deadline) { $rogLoopFinished = $true; break }
        $nextDue += $intervalTicks
        if ($nextDue -lt $workEnd) {
            $skipped = [long][Math]::Floor(($workEnd - $nextDue) / [double]$intervalTicks) + 1
            $rogMissedSlots += $skipped; $nextDue += $skipped * $intervalTicks
        }
        if ($nextDue -gt $deadline) { $nextDue = $deadline }
    }
} catch {
    $rogStatus = 'ERROR'
    Add-RogIssue 'ERROR' ($_.Exception.GetType().FullName + ': ' + $_.Exception.Message)
    [Console]::Error.WriteLine($_.Exception.Message)
} finally {
    # Final checks and output failure cannot skip handle release. No re-enumeration or reopening.
    try {
        foreach ($target in $rogTargets) {
            $check = [ordered]@{ pid = $target.identity.pid; role = $target.identity.role; status = 'ERROR'
                raw = $null; imagePath = $null; diskSha256 = $null; rawAfterHash = $null; error = $null
                diskCompanions = $null; diskCompanionComparison = $null }
            try {
                $check.raw = $target.reader.Read()
                if ($target.identity.role -eq 'host') {
                    # The bound path remains usable as a disk witness even if the held process exited.
                    $check.diskCompanions = Read-RogHostCompanions $target.identity.imagePath $target.identity.pid 'FINAL'
                    $target.identity.diskCompanionsFinal = $check.diskCompanions
                    $check.diskCompanionComparison = Compare-RogHostCompanions $target.identity.diskCompanionsAtBind $check.diskCompanions $target.identity.pid
                    $target.identity.diskCompanionComparison = $check.diskCompanionComparison
                }
                $check.imagePath = $target.reader.ImagePath()
                $check.diskSha256 = Get-RogSha $check.imagePath
                $check.rawAfterHash = $target.reader.Read()
                if ($check.raw.Status -ne 'OK' -or $check.rawAfterHash.Status -ne 'OK') { throw 'Target exited or final counters/liveness failed.' }
                if ($check.raw.CreationFileTime100ns -cne $target.identity.birthFileTime100ns -or
                    $check.rawAfterHash.CreationFileTime100ns -cne $target.identity.birthFileTime100ns) { throw 'Final exact birth mismatch.' }
                if (-not [string]::Equals($check.imagePath, $target.identity.imagePath, [StringComparison]::OrdinalIgnoreCase) -or
                    $check.diskSha256 -cne $target.identity.diskSha256) { throw 'Final native image path or disk SHA changed.' }
                $check.status = 'OK'
            } catch { $check.error = $_.Exception.Message; Add-RogIssue 'FINAL_IDENTITY' ('PID ' + $check.pid + ': ' + $check.error) }
            $rogFinalChecks.Add($check)
        }
        if ($null -ne $rogSelf) {
            $rogSelfFinal = $rogSelf.reader.Read()
            if ($rogSelfFinal.Status -ne 'OK' -or $rogSelfFinal.CreationFileTime100ns -cne $rogSelf.identity.birthFileTime100ns) {
                Add-RogIssue 'SAMPLER_COUNTER_ERROR' 'Final sampler counter/liveness/identity check failed.'
            }
        }
        if ($null -ne $rogIdentity -and $null -ne $rogIdentity.tools.observerSha256) {
            try {
                if ((Get-RogSha $rogIdentity.tools.observerPath) -cne $rogIdentity.tools.observerSha256 -or
                    (Get-RogSha $rogIdentity.tools.countersPath) -cne $rogIdentity.tools.countersSha256) { throw 'Tool source changed during collection.' }
            } catch { Add-RogIssue 'TOOL_IDENTITY' $_.Exception.Message }
        }
    } catch { Add-RogIssue 'FINALIZATION_ERROR' $_.Exception.Message; $rogStatus = 'ERROR' }
    finally {
        if ($null -ne $rogWriter) {
            try { $rogWriter.Dispose() } catch { Add-RogIssue 'OUTPUT_ERROR' $_.Exception.Message; $rogStatus = 'ERROR' }
        }
        foreach ($reader in $rogReaders) {
            try { $reader.Dispose() } catch { Add-RogIssue 'HANDLE_RELEASE_ERROR' $_.Exception.Message; $rogStatus = 'ERROR' }
        }
    }
    if ($null -ne $rogRunPath) {
        try {
            if ($rogStatus -ne 'ERROR') {
                if ($rogLoopFinished -and $rogIssues.Count -eq 0) { $rogStatus = 'COMPLETE' } else { $rogStatus = 'PARTIAL' }
            }
            $rogExitCode = if ($rogStatus -eq 'COMPLETE') { 0 } elseif ($rogStatus -eq 'PARTIAL') { 2 } else { 1 }
            if ($null -ne $rogIdentity) {
                $rogIdentity.status = $rogStatus
                Save-RogJson (Join-Path $rogRunPath 'IDENTITY.json') $rogIdentity
            }
            $duration = $null
            if ($null -ne $rogSampleStart -and $null -ne $rogSampleEnd) { $duration = [double]($rogSampleEnd - $rogSampleStart) / $rogFrequency }
            $statistics = @($rogTargets | ForEach-Object { Get-RogStatistics $_ })
            $selfStatistics = $null
            if ($null -ne $rogSelf) { $selfStatistics = Get-RogStatistics $rogSelf }
            $entryEndProcess = [Diagnostics.Process]::GetCurrentProcess()
            try { $entryEndCpu100ns = $entryEndProcess.TotalProcessorTime.Ticks } finally { $entryEndProcess.Dispose() }
            $summary = [ordered]@{
                schemaVersion = 1; status = $rogStatus; exitCode = $rogExitCode; scenario = $Scenario
                startedUtc = $rogEntryUtc; finishedUtc = [DateTime]::UtcNow.ToString('o'); runDirectory = $rogRunPath; path = $rogRunPath
                requestedDurationSeconds = $DurationSeconds; actualSamplingSeconds = $duration; completedDeadline = $rogLoopFinished
                intervalMs = $IntervalMs; rows = $rogRows; missedSlots = $rogMissedSlots
                qpcFrequency = if ($null -ne $rogFrequency) { $rogFrequency.ToString($rogInvariant) } else { $null }
                logicalProcessors = $rogLogical; logicalProcessorCount = $rogLogicalEvidence; targets = $statistics; sampler = $selfStatistics; finalTargetChecks = $rogFinalChecks
                hostCoverage = 'Initial snapshot and bound descendants only; later Host inventory UNKNOWN; no application-wide total.'
                allHostMachineCpuPercent = $null; cpuAcceptance = $false; issues = $rogIssues; unknowns = $rogUnknowns
                overhead = [ordered]@{ samplingReadWallMs = $rogReadTotalMs; samplingWorkWallMs = $rogWorkTotalMs
                    maximumIterationWorkWallMs = $rogWorkMaxMs; previousIterationWorkWallMs = $rogPreviousWorkMs
                    samplerNativeAfterCompile = $rogSelfStart; samplerNativeBeforeRelease = $rogSelfFinal
                    samplerCpuAtScriptEntry100ns = $rogEntryCpu100ns.ToString($rogInvariant)
                    samplerCpuBeforeSummary100ns = $entryEndCpu100ns.ToString($rogInvariant)
                    samplerCpuThroughSetupSamplingFinalChecks100ns = ($entryEndCpu100ns - $rogEntryCpu100ns).ToString($rogInvariant)
                    scriptWallSecondsBeforeSummary = $rogEntryClock.Elapsed.TotalSeconds
                    limitations = 'Self CPU includes setup/final checks in this process; compiler-child CPU and final SUMMARY serialization are excluded. Work wall includes reads/serialization/JSONL flush, excludes sleeps. Never subtract sampler CPU from target CPU.' }
                definitions = [ordered]@{ machineCpuPercent = '100 * delta(kernel100ns+user100ns) / (10000000 * actualQpcSeconds * logicalProcessors)'
                    singleCoreCpuPercent = '100 * delta(kernel100ns+user100ns) / (10000000 * actualQpcSeconds); may exceed 100'
                    cycles = 'Raw cumulative/delta decimal strings; never converted to time or CPU percent.'
                    liveness = 'Held handle WaitForSingleObject(0): 258 live, 0 exited; all other results fail. Exit FILETIME is undefined while alive.'
                    rawIntegers = 'FILETIME/kernel/user/cycles/QPC/frequency are decimal strings to preserve integer precision.'
                    missing = 'Baseline, unavailable counters, exit, identity failure and unknown Host are null, never inferred zero.' }
            }
            Save-RogJson (Join-Path $rogRunPath 'SUMMARY.json') $summary
        } catch {
            $rogStatus = 'ERROR'; $rogExitCode = 1
            Add-RogIssue 'OUTPUT_ERROR' $_.Exception.Message
            [Console]::Error.WriteLine('Evidence finalization failed; original JSONL is retained: ' + $_.Exception.Message)
            try {
                Save-RogJson (Join-Path $rogRunPath 'SUMMARY.json') ([ordered]@{ schemaVersion = 1; status = 'ERROR'; exitCode = 1
                    runDirectory = $rogRunPath; rows = $rogRows; issues = $rogIssues; unknowns = $rogUnknowns })
                if ($null -ne $rogIdentity) { $rogIdentity.status = 'ERROR'; Save-RogJson (Join-Path $rogRunPath 'IDENTITY.json') $rogIdentity }
            } catch { [Console]::Error.WriteLine('Cannot write fallback summary: ' + $_.Exception.Message) }
        }
    }
}
# Exactly one compact JSON result on stdout. Warnings/verbose use their own streams.
# Wrapper may inspect evidence even when exitCode is nonzero; identityStatus gates usable targets.
$rogResultTargets = @()
if ($null -ne $rogIdentity) {
    $rogResultTargets = @($rogIdentity.targets | ForEach-Object {
        [ordered]@{ role = $_.role; pid = $_.pid; birthFileTime100ns = $_.birthFileTime100ns
            birth = $_.birthFileTime100ns; birthUtc = $_.birthUtc; imagePath = $_.imagePath
            image = $_.imagePath; diskSha256 = $_.diskSha256; identityStatus = $_.status }
    })
}
Write-Output (ConvertTo-Json -InputObject ([ordered]@{ schemaVersion = 1; status = $rogStatus
    exitCode = $rogExitCode; runDirectory = $rogRunPath; path = $rogRunPath; targets = $rogResultTargets
    issues = $rogIssues }) -Depth 8 -Compress)
exit $rogExitCode
