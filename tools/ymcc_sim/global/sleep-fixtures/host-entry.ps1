<# Entry for reviewed legacy sleep tests. The global runner must place this
   process in its gated KILL_ON_JOB_CLOSE job before permitting execution.
   Read-only use of the explicitly selected current Host: no binary copying or
   modification. New logs and synthetic sessions stay inside the case directory.
   Never attach, elevate, launch an app, read a real token, or enable hardware. #>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('forms','typeseries','rapidwake','sleeptypes')][string]$Mode,
  [Parameter(Mandatory=$true)][string]$EvidenceDir,
  [ValidateSet(18874,18875,18876)][int]$Port = 18874
)
$ErrorActionPreference = 'Stop'
$outcome = [ordered]@{
  schemaVersion = 1; domain = 'sleep'; mode = $Mode; status = 'INCOMPLETE'; reason = 'NOT_EXECUTED'
  operatingSystemSleepPerformed = $false; physicalHardwareValidated = $false
  installedApplicationExercised = $false; fullApplicationValidated = $false
  checks = 0; forms = 0; hostStarted = $false; hostExited = $false
}
function Finish-SleepCase([string]$Status, [string]$Reason, [int]$Code) {
  $outcome.status = $Status; $outcome.reason = $Reason
  $json = $outcome | ConvertTo-Json -Depth 5
  [IO.File]::WriteAllText((Join-Path $EvidenceDir 'sleep-host-result.json'), $json, [Text.UTF8Encoding]::new($false))
  Write-Output $json
  exit $Code
}
try {
  if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) { throw 'EVIDENCE_DIRECTORY_MISSING' }
  if ($env:YMCC_SIM_GLOBAL_RUN -ne '1' -or $PSVersionTable.PSVersion.Major -lt 7) {
    Finish-SleepCase 'BLOCKED' 'GLOBAL_OWNED_PWSH_RUNNER_REQUIRED' 10
  }
  $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
  $domain = Join-Path $repo 'tools/ymcc_sim/domains/sleep/sleep_domain.ps1'
  $isHost = $Mode -ne 'sleeptypes'
  if ($isHost) {
    if (-not $env:YEMAN_SIM_PAYLOAD_ROOT) { Finish-SleepCase 'BLOCKED' 'EXPLICIT_CURRENT_HOST_PAYLOAD_REQUIRED' 10 }
    if (-not (Test-Path -LiteralPath $env:YEMAN_SIM_PAYLOAD_ROOT -PathType Container)) {
      Finish-SleepCase 'BLOCKED' 'EXPLICIT_CURRENT_HOST_PAYLOAD_MISSING' 10
    }
    $payload = (Resolve-Path -LiteralPath $env:YEMAN_SIM_PAYLOAD_ROOT -ErrorAction Stop).ProviderPath
    $exe = Join-Path $payload 'YeManFanHost.exe'
    $dll = Join-Path $payload 'YeManFanHost.dll'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf) -or -not (Test-Path -LiteralPath $dll -PathType Leaf)) {
      Finish-SleepCase 'BLOCKED' 'EXPLICIT_CURRENT_HOST_PAYLOAD_MISSING' 10
    }
    # Reuse the explicit current binary in place; never copy, modify or
    # populate its payload directory with settings, flags or session files.
    # Read the exact existing lifetime gates, without acquiring/replacing them.
    # Unknown/access-denied is BLOCKED. There is no name-based process cleanup.
    foreach ($gateName in @('Global\YeManFanHost-v2','Local\YeManFanHost-v2')) {
      $gate = $null
      try {
        if ([Threading.Mutex]::TryOpenExisting($gateName, [ref]$gate)) {
          Finish-SleepCase 'BLOCKED' 'EXISTING_HOST_MACHINE_OWNERSHIP' 10
        }
      } catch { Finish-SleepCase 'BLOCKED' 'HOST_MACHINE_OWNERSHIP_UNREADABLE' 10 }
      finally { if ($gate) { $gate.Dispose() } }
    }
    if (@(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue).Count -gt 0) {
      Finish-SleepCase 'BLOCKED' 'EXISTING_HOST_PROCESS_REQUIRES_DEDICATED_WINDOW' 10
    }
    # Reserve only long enough to establish that this independent port is free.
    # A race after release cannot authorize takeover: the host's own machine
    # lease and the fresh per-run API token remain authoritative.
    $listener = $null
    try {
      $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
      $listener.Start()
    } catch { Finish-SleepCase 'BLOCKED' 'DEDICATED_MOCK_PORT_UNAVAILABLE' 10 }
    finally { if ($listener) { $listener.Stop() } }
    $env:YEMAN_SIM_PAYLOAD_ROOT = $payload
  }
  # Isolate both the legacy core defaults and every newly created host log.
  # These directories are empty at creation; no user files are imported.
  $env:LOCALAPPDATA = Join-Path $EvidenceDir 'local-app-data'
  $env:YEMANCC_SELFTEST_LOG_ROOT = Join-Path $EvidenceDir 'host-logs'
  $env:TEMP = Join-Path $EvidenceDir 'process-temp'
  $env:TMP = $env:TEMP
  New-Item -ItemType Directory -Path $env:LOCALAPPDATA,$env:YEMANCC_SELFTEST_LOG_ROOT,$env:TEMP -Force | Out-Null
  $arguments = @('-Mode',$Mode,'-EvidenceDir',$EvidenceDir)
  if ($isHost) { $arguments += @('-HostExe',$exe,'-Port',"$Port",'-RequestTimeoutMs','5000') }
  # The reviewed domain appends --mock-handshake and never --real-backend.
  # Fixed arguments cannot select live, attach, app start or persistent modes.
  # Until the legacy evidence is read back, a started host's outcome is unknown.
  if ($isHost) { $outcome.hostStarted = $null; $outcome.hostExited = $null }
  & (Join-Path $PSHOME 'pwsh.exe') -NoLogo -NoProfile -NonInteractive -File $domain @arguments
  $childCode = $LASTEXITCODE
  $results = @(Get-ChildItem -LiteralPath $EvidenceDir -Filter 'sleep-sim-*.json' -File)
  if ($results.Count -ne 1) { Finish-SleepCase 'FAIL' 'LEGACY_EVIDENCE_MISSING_OR_AMBIGUOUS' 2 }
  $result = Get-Content -LiteralPath $results[0].FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  $outcome.checks = @($result.checks).Count
  $outcome.forms = @($result.forms).Count
  if ($isHost) {
    $outcome.hostStarted = $result.extra.startedByUs -eq $true
    $outcome.hostExited = $result.extra.hostExit.exited -eq $true
  }
  if ($result.realSleepPerformed -ne $false -or $result.hardwareActionPerformed -ne $false) {
    Finish-SleepCase 'FAIL' 'LEGACY_SCOPE_CLAIMS_INVALID' 2
  }
  if ($childCode -ne 0 -or $result.exitCode -ne 0 -or $result.verdict -ne 'ALL_PASS' -or $outcome.checks -eq 0) {
    Finish-SleepCase 'FAIL' 'LEGACY_ASSERTIONS_FAILED_OR_NOT_EXECUTED' 2
  }
  if ($isHost) {
    if (-not $outcome.hostStarted -or -not $outcome.hostExited -or
        $result.extra.hostMode -ne 'mock-handshake' -or $result.extra.sandboxFacts.realBackendPassed -ne $false) {
      Finish-SleepCase 'FAIL' 'MOCK_HOST_IDENTITY_OR_CLEAN_EXIT_UNPROVEN' 2
    }
    if ($Mode -eq 'forms' -and $outcome.forms -ne 20) { Finish-SleepCase 'FAIL' 'TWENTY_FORMS_NOT_EXECUTED' 2 }
  }
  Finish-SleepCase 'PASS' $(if ($isHost) { 'MOCK_HOST_ASSERTIONS_ONLY' } else { 'LEGACY_CLASSIFIER_FIXTURES_ONLY_NOT_PHYSICAL_SLEEP_TYPE' }) 0
} catch {
  # Keep exception strings/paths/session values out of the fixed summary.
  Finish-SleepCase 'FAIL' 'SLEEP_FIXTURE_ENTRY_FAILED' 2
}
