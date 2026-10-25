<# Explicit Host DLL, no Main. The all mode preserves the legacy fixtures'
   fresh-process boundary instead of mixing their engine histories. #>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$HostPayloadRoot,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedHostSha256,
  [Parameter(Mandatory=$true)][string]$EvidenceRoot,
  [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedBackendSha256,
  [ValidateSet('memory','probe','all','forms','typeseries','rapidwake')][string]$Mode = 'all',
  [ValidateRange(30,180)][int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'receipt-contract.ps1')
$evidencePath = [IO.Path]::GetFullPath($EvidenceRoot)
if (Test-Path -LiteralPath $evidencePath) { throw 'FRESH_EVIDENCE_DIRECTORY_REQUIRED' }
New-Item -ItemType Directory -Path $evidencePath | Out-Null
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
$report = $null
$exitCode = 1
$groups = [Collections.Generic.List[object]]::new()
function Invoke-OwnedEngine([string]$GroupMode, [string]$ToolDll, [string]$Runtime, [string]$PayloadRoot) {
  $execution = Join-Path $evidencePath ('groups/' + $GroupMode)
  $localData = Join-Path $execution 'local-appdata'
  $temporary = Join-Path $execution 'temp'
  $selftestLogs = Join-Path $execution 'selftest-root'
  New-Item -ItemType Directory -Path $execution,$localData,$temporary,$selftestLogs | Out-Null
  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $Runtime
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.WorkingDirectory = $execution
  foreach ($argument in @($ToolDll,$PayloadRoot,$ExpectedHostSha256,$execution,$repoRoot,$GroupMode,[string]$ExpectedBackendSha256)) { $startInfo.ArgumentList.Add($argument) }
  $startInfo.Environment['LOCALAPPDATA'] = $localData
  $startInfo.Environment['TEMP'] = $temporary
  $startInfo.Environment['TMP'] = $temporary
  $startInfo.Environment['YEMANCC_SELFTEST_LOG_ROOT'] = $selftestLogs
  $child = $null
  $forced = $false
  try {
    $child = [Diagnostics.Process]::Start($startInfo)
    $birth = $child.StartTime.ToUniversalTime()
    $stdout = $child.StandardOutput.ReadToEndAsync()
    $stderr = $child.StandardError.ReadToEndAsync()
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $child.WaitForExit(500)) {
      if ($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
        # Retained handle from this exact Start call. No name/PID-based lookup.
        if (-not $child.HasExited -and $child.StartTime.ToUniversalTime() -eq $birth) { $child.Kill($false); $forced = $true; $child.WaitForExit(5000) | Out-Null }
        throw 'OWNED_ENGINE_PROBE_TIMEOUT'
      }
    }
    [IO.File]::WriteAllText((Join-Path $execution 'stdout.log'), $stdout.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $execution 'stderr.log'), $stderr.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
    $receiptPath = Join-Path $execution 'sleep-engine-result.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'ENGINE_RESULT_RECEIPT_MISSING' }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw -Encoding utf8 | ConvertFrom-Json
    $receipt.process.naturalExitObservedByParent = (-not $forced -and $child.HasExited)
    $receipt.process | Add-Member -Force -NotePropertyMembers @{
      exitCode = $child.ExitCode; creationTimeUtc = $birth.ToString('o'); forcedCleanup = $forced; executable = $Runtime
    }
    $completed = if ($GroupMode -eq 'memory') { [bool]$receipt.process.memoryEngineCompleted } else { [bool]$receipt.process.serverCompleted }
    $exitVerdict = Test-SleepEngineChildReceipt -ChildExitCode $child.ExitCode -ReportedStatus $receipt.status -NaturalExit $receipt.process.naturalExitObservedByParent -ConsumerCompleted $completed
    if (-not $exitVerdict.passed) { $receipt.status = 'FAIL'; $receipt.failure = $exitVerdict.code; $receipt | Add-Member -Force -NotePropertyName failurePhase -NotePropertyValue $exitVerdict.stage }
    $receipt | Add-Member -Force -NotePropertyName evidencePath -NotePropertyValue $receiptPath
    return $receipt
  } finally { if ($null -ne $child) { $child.Dispose() } }
}
try {
  if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'POWERSHELL_7_REQUIRED' }
  $payloadRoot = (Resolve-Path -LiteralPath $HostPayloadRoot).ProviderPath
  $hostDll = Join-Path $payloadRoot 'YeManFanHost.dll'
  $backendDll = Join-Path $payloadRoot 'YeManFanBackend.dll'
  if (-not (Test-Path -LiteralPath $hostDll -PathType Leaf)) { throw 'EXPLICIT_HOST_DLL_MISSING' }
  $beforeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $hostDll).Hash.ToLowerInvariant()
  if ($beforeHash -ne $ExpectedHostSha256.ToLowerInvariant()) { throw 'EXPLICIT_HOST_HASH_MISMATCH' }
  $backendBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $backendDll).Hash.ToLowerInvariant()
  if ($ExpectedBackendSha256 -and $backendBefore -ne $ExpectedBackendSha256.ToLowerInvariant()) { throw 'EXPLICIT_BACKEND_HASH_MISMATCH' }
  $runtime = (Get-Command dotnet -CommandType Application -ErrorAction Stop).Source
  $sourceBefore = @(Get-SleepEngineSourceSnapshot -SourceRoot $PSScriptRoot)
  $sourceBefore | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidencePath 'source-before.json') -Encoding utf8
  $buildRoot = Join-Path $evidencePath 'build'
  & $runtime build (Join-Path $PSScriptRoot 'SleepEngineProbe.csproj') -c Release --artifacts-path $buildRoot --disable-build-servers -p:NuGetAudit=false -v quiet *> (Join-Path $evidencePath 'build.log')
  if ($LASTEXITCODE -ne 0) { throw 'SLEEP_ENGINE_PROBE_BUILD_FAILED' }
  $toolDll = Join-Path $buildRoot 'bin/SleepEngineProbe/release/SleepEngineProbe.dll'
  if (-not (Test-Path -LiteralPath $toolDll -PathType Leaf)) { throw 'SLEEP_ENGINE_PROBE_OUTPUT_MISSING' }
  $selected = if ($Mode -eq 'all') { @('forms','typeseries','rapidwake') } else { @($Mode) }
  foreach ($groupMode in $selected) { $groups.Add((Invoke-OwnedEngine $groupMode $toolDll $runtime $payloadRoot)) }
  if ($Mode -eq 'all') {
    $events = [Collections.Generic.List[object]]::new()
    $scenarios = [Collections.Generic.List[object]]::new()
    $checks = [Collections.Generic.List[object]]::new()
    foreach ($group in $groups) {
      foreach ($event in $group.inputConsumerStateFeedback) {
        $event = $event | ConvertTo-Json -Depth 40 | ConvertFrom-Json
        $event.input | Add-Member -NotePropertyName originalEventId -NotePropertyValue $event.input.eventId
        $event.input.eventId = $group.mode + '/' + $event.input.eventId
        $event.consumer | Add-Member -NotePropertyMembers @{ group = $group.mode; processId = $group.process.pid }
        $events.Add($event)
      }
      foreach ($scenario in $group.scenarios) {
        $scenario = $scenario | ConvertTo-Json -Depth 20 | ConvertFrom-Json
        $scenario.id = $group.mode + '/' + $scenario.id
        $scenario.requestEventIds = @($scenario.requestEventIds | ForEach-Object { $group.mode + '/' + $_ })
        $scenarios.Add($scenario)
      }
      foreach ($check in $group.checks) { $checks.Add([ordered]@{ name = $group.mode + '/' + $check.name; passed = $check.passed; detail = $check.detail }) }
    }
    $loaded = @($groups | ForEach-Object { $_.actualHost.loaded } | Sort-Object path,sha256 -Unique)
    $fixtureIdentities = @($groups | ForEach-Object { $_.fixtureIdentities } | Sort-Object path,sha256 -Unique)
    $statuses = @($groups | ForEach-Object { $_.status })
    $status = if ($statuses -contains 'FAIL') { 'FAIL' } elseif ($statuses -contains 'INCOMPLETE') { 'INCOMPLETE' } else { 'PASS' }
    $report = [pscustomobject]@{
      schemaVersion = 1; kind = 'sleep-engine-consumption'; mode = 'all'; status = $status
      summary = [ordered]@{ checks = $checks.Count; scenarios = $scenarios.Count; forms = @($scenarios | Where-Object mode -eq 'forms').Count; typeseries = @($scenarios | Where-Object mode -eq 'typeseries').Count; rapidwake = @($scenarios | Where-Object mode -eq 'rapidwake').Count; negative = @($scenarios | Where-Object mode -eq 'negative').Count; negativeKinds = 4; failed = @($scenarios | Where-Object status -ne 'PASS').Count; requests = $events.Count; memoryOperations = 0; engineProcesses = $groups.Count; compatibilityDifferences = 0 }
      process = [pscustomobject]@{ role = 'aggregate-only'; engineProcessCount = $groups.Count; serverCompleted = @($groups | Where-Object { -not $_.process.serverCompleted }).Count -eq 0; mainCalled = $false; naturalExitObservedByParent = @($groups | Where-Object { -not $_.process.naturalExitObservedByParent }).Count -eq 0; exitCode = if ($status -eq 'PASS') { 0 } else { 1 }; forcedCleanup = @($groups | Where-Object { $_.process.forcedCleanup }).Count -gt 0 }
      processes = @($groups | ForEach-Object { $_.process })
      actualHost = [ordered]@{ payloadRoot = $payloadRoot; expectedSha256 = $ExpectedHostSha256.ToLowerInvariant(); expectedBackendSha256 = $ExpectedBackendSha256; loaded = $loaded; loadedByProcess = @($groups | ForEach-Object { [ordered]@{ group = $_.mode; processId = $_.process.pid; backendFileSha256 = $_.actualHost.backendFileSha256; pinnedArtifacts = $_.actualHost.pinnedArtifacts; loaded = $_.actualHost.loaded } }) }
      http = [ordered]@{ actualDispatch = $true; baseUrl = $groups[0].http.baseUrl; baseUrls = @($groups | ForEach-Object { $_.http.baseUrl }); tokenSource = 'fresh-random-in-memory'; tokenPersisted = $false; isolation = 'one-fresh-process-engine-server-token-per-original-mode' }
      safety = [ordered]@{ operatingSystemSleepPerformed = $false; hardwareWritesPerformed = $false; hcLoaded = @($groups | Where-Object { $_.safety.hcLoaded }).Count -gt 0; nativeAttempts = @($groups | ForEach-Object { $_.safety.nativeAttempts }); productionMainInvoked = $false; nativeSchedulerExercised = $false; networkUsed = $true; localServerConstructed = $true; physicalHardwareValidated = $false; machineGateAcquired = $false; existingHostStopped = $false }
      productionMachineGates = @($groups | ForEach-Object { [ordered]@{ group = $_.mode; observed = $_.productionMachineGates } })
      checks = @($checks); scenarios = @($scenarios); fixtureIdentities = $fixtureIdentities; compatibilityDifferences = @($groups | ForEach-Object { $_.compatibilityDifferences }); inputConsumerStateFeedback = @($events)
      groups = @($groups); failure = if ($status -eq 'FAIL') { 'GROUP_EXECUTION_FAILED' } else { $null }; sourceScope = 'actual selected DLL and real HTTP dispatch in three isolated original fixture modes;no Main/native scheduler/physical sleep'
    }
  } else { $report = $groups[0] }
  $afterHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $hostDll).Hash.ToLowerInvariant()
  $backendAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $backendDll).Hash.ToLowerInvariant()
  $report | Add-Member -Force -NotePropertyName wrapperVerification -NotePropertyValue ([ordered]@{
    hostUnchanged = $afterHash -eq $beforeHash -and $backendAfter -eq $backendBefore; expectedHostSha256 = $ExpectedHostSha256.ToLowerInvariant(); actualHostSha256 = $afterHash
    expectedBackendSha256 = $ExpectedBackendSha256; actualBackendSha256 = $backendAfter; childNaturalExit = $report.process.naturalExitObservedByParent
    mainMachineGateBypassed = $false; productionMainGateExercised = $false; realHostMainStarted = $false
  })
  $sourceAfter = @(Get-SleepEngineSourceSnapshot -SourceRoot $PSScriptRoot)
  $sourceAfter | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidencePath 'source-after.json') -Encoding utf8
  $sourceVerdict = Test-SleepEngineSourceSnapshots -Before $sourceBefore -After $sourceAfter
  $report | Add-Member -Force -NotePropertyName sourceInputs -NotePropertyValue @($sourceBefore)
  $report | Add-Member -Force -NotePropertyName sourceInputsAfter -NotePropertyValue @($sourceAfter)
  $report | Add-Member -Force -NotePropertyName sourceStability -NotePropertyValue $sourceVerdict
  if (-not $sourceVerdict.unchanged) { $report.status = 'FAIL'; $report.failure = 'SOURCE_CHANGED_DURING_BUILD_OR_RUN'; $report | Add-Member -Force -NotePropertyName failurePhase -NotePropertyValue 'consumer' }
  if (-not $report.wrapperVerification.hostUnchanged -or -not $report.process.naturalExitObservedByParent) { $report.status = 'FAIL'; $report.failure = 'HOST_IDENTITY_OR_NATURAL_EXIT_NOT_CONFIRMED' }
  $exitCode = if ($report.status -eq 'PASS') { 0 } elseif ($report.status -eq 'INCOMPLETE') { 10 } else { 1 }
} catch {
  $reason = $_.Exception.Message
  if ($reason -notmatch '^[A-Z0-9_]+$') { $reason = 'SLEEP_ENGINE_ENTRY_FAILED' }
  $report = [ordered]@{ schemaVersion = 1; kind = 'sleep-engine-consumption'; status = 'FAIL'; mode = $Mode; failure = $reason; failurePhase = 'input-or-wrapper'
    inputConsumerStateFeedback = @(); completedGroups = @($groups); process = @{ naturalExitObservedByParent = $false; forcedCleanup = $false }
    safety = @{ productionMainInvoked = $false; operatingSystemSleepPerformed = $false; hardwareWritesPerformed = $false; existingHostStopped = $false }
  }
} finally {
  $json = $report | ConvertTo-Json -Depth 50
  [IO.File]::WriteAllText((Join-Path $evidencePath 'sleep-engine-result.json'), $json, [Text.UTF8Encoding]::new($false))
}
Write-Output $json
exit $exitCode


