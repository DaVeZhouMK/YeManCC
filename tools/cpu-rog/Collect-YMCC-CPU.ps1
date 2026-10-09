[CmdletBinding()]
param(
  [ValidateRange(0,3600)][int]$WarmupSeconds = 30,
  [ValidateRange(1,3600)][int]$Seconds = 120,
  [ValidateRange(100,5000)][int]$SampleMilliseconds = 500,
  [ValidateRange(250,10000)][int]$InventoryMilliseconds = 1000,
  [ValidateSet('on','off','unknown','unavailable')][string]$FanState = 'unknown',
  [ValidateSet('on','off','unknown','unavailable')][string]$GamepadState = 'unknown',
  [ValidateSet('on','off','unknown','unavailable')][string]$GyroState = 'unknown',
  [string]$Label = 'S4', [string]$StateNote = '', [string]$OutDir = '',
  [switch]$SkipSystemCpu, [switch]$SkipLogManifest, [string[]]$LogFile = @(),
  [string]$LifecycleJsonl = '', [string]$VirtualHandshakeJsonl = '',
  [string]$ExpectedSessionId = '', [switch]$RequireControlEvidence,
  # Required by the dispatcher, rather than Mandatory (which prompts before structured failure).
  [string]$ExePath = '',
  [ValidateRange(30,3600)][int]$EvidenceMaxAgeSeconds = 600
)
# Read-only collector. No lifecycle, device, privilege or recursively scanned log operations.
# Raw command lines/evidence remain in memory only; serialization uses explicit projections.
$ErrorActionPreference = 'Stop'
$script:scriptVersion = 'ymcc-cpu-rog-collector-v1-20261004'
$script:isOfflineFixture = $false

function Get-NowUtc { return [datetime]::UtcNow }
function Get-MonotonicMilliseconds { return [Diagnostics.Stopwatch]::GetTimestamp() * 1000.0 / [Diagnostics.Stopwatch]::Frequency }
function Wait-Collector([double]$Milliseconds) { if ($Milliseconds -gt 0) { Start-Sleep -Milliseconds ([int][math]::Ceiling($Milliseconds)) } }
function Get-Hash([string]$Text) {
  if ([string]::IsNullOrEmpty($Text)) { return $null }
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Test-ExactExePath([string]$Value) {
  return (-not [string]::IsNullOrWhiteSpace($Value) -and $Value -match '^(?:[A-Za-z]:\\|\\\\[^\\]+\\[^\\]+\\)' -and $Value -notmatch '[*?]' -and [IO.Path]::GetExtension($Value) -ieq '.exe')
}
function Initialize-Output {
  if ([string]::IsNullOrWhiteSpace($script:OutDir)) { $script:OutDir = Join-Path (Get-Location) ('YMCC-CPU-' + [guid]::NewGuid().ToString('N')) }
  $script:OutDir = [IO.Path]::GetFullPath($script:OutDir)
  if (Test-Path -LiteralPath $script:OutDir) {
    if (-not (Test-Path -LiteralPath $script:OutDir -PathType Container) -or @(Get-ChildItem -LiteralPath $script:OutDir -Force).Count -gt 0) { throw 'OUTPUT_DIRECTORY_NOT_EMPTY' }
  }
  [void][IO.Directory]::CreateDirectory($script:OutDir)
  $script:outputReady = $true
}
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false))) }
function Write-Json([string]$Name, [object]$Object, [int]$Depth = 40) { Write-Utf8 (Join-Path $script:OutDir $Name) ($Object | ConvertTo-Json -Depth $Depth -Compress) }
function Write-JsonLines([string]$Name, [object[]]$Rows) {
  Write-Utf8 (Join-Path $script:OutDir $Name) ((@($Rows | ForEach-Object { $_ | ConvertTo-Json -Depth 24 -Compress })) -join [Environment]::NewLine)
}
function Add-FactError([System.Collections.IList]$List, [string]$Kind, [string]$Code, [object]$Data) {
  # Never export exception.Message, Exception.ToString, commandLine or arbitrary evidence text.
  [void]$List.Add([pscustomobject]@{utc=(Get-NowUtc).ToString('o');kind=$Kind;code=$Code;data=$Data})
}
function Test-Counter([object]$Value) {
  if ($null -eq $Value -or $Value -is [bool] -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $false }
  $v = 0.0
  return ([double]::TryParse([string]$Value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$v) -and -not [double]::IsNaN($v) -and -not [double]::IsInfinity($v) -and $v -ge 0)
}
function Creation-Utc([object]$Raw) {
  if ($null -eq $Raw) { return '' }
  if ($Raw -is [datetime]) { return $Raw.ToUniversalTime().ToString('o') }
  if ($Raw -is [datetimeoffset]) { return $Raw.UtcDateTime.ToString('o') }
  $t = Get-ScopeCreationTime ([string]$Raw)
  if ($null -ne $t) { return $t.ToString('o') }
  try { return ([Management.ManagementDateTimeConverter]::ToDateTime([string]$Raw)).ToUniversalTime().ToString('o') } catch { return '' }
}
function Get-Inventory {
  $begin = Get-MonotonicMilliseconds
  $script:inventoryCount++
  try {
    $raw = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
    $rows = @(foreach ($p in $raw) {
      $ok = (Test-Counter $p.UserModeTime) -and (Test-Counter $p.KernelModeTime)
      $cpu = $null
      if ($ok) {
        $cpu = ([double]$p.UserModeTime + [double]$p.KernelModeTime) / 10000.0
        if (-not (Test-Counter $cpu)) { $ok = $false; $cpu = $null }
      }
      [pscustomobject]@{pid=[int]$p.ProcessId;ppid=[int]$p.ParentProcessId;name=[string]$p.Name;path=[string]$p.ExecutablePath;
        commandLine=[string]$p.CommandLine;creationTimeUtc=(Creation-Utc $p.CreationDate);cpuMs=$cpu;cpuReadOk=$ok}
    })
    $result = [pscustomobject]@{ok=$true;rows=$rows;error=$null}
  } catch {
    Add-FactError $script:inventoryErrors 'inventory' 'CIM_INVENTORY_UNAVAILABLE' $null
    $result = [pscustomobject]@{ok=$false;rows=@();error='CIM_INVENTORY_UNAVAILABLE'}
  }
  [void]$script:inventoryEvents.Add([pscustomobject]@{inventoryIndex=$script:inventoryCount;elapsedMs=((Get-MonotonicMilliseconds)-$script:originMs);latencyMs=((Get-MonotonicMilliseconds)-$begin);ok=$result.ok;processCount=$result.rows.Count})
  return $result
}
function Is-ExeName([string]$Name, [string]$Base) {
  return $Name -match ('(?i)^' + [regex]::Escape($Base) + '(?:\.exe)?$')
}

function Get-ScopeCreationTime([string]$Value) {
  if ([string]::IsNullOrWhiteSpace($Value) -or $Value -notmatch '(?:Z|[+-]\d{2}:\d{2})$') { return $null }
  $parsed = [datetimeoffset]::MinValue
  if (-not [datetimeoffset]::TryParse($Value, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsed)) { return $null }
  return $parsed.UtcDateTime
}

function New-ScopePidMap([object[]]$All) {
  $map = @{}
  foreach ($p in $All) {
    $id = [int]$p.pid
    if ($map.ContainsKey($id)) { $map[$id] = $null } else { $map[$id] = $p }
  }
  return $map
}

function Test-ScopeParentEdge([object]$Child, [object]$Parent) {
  if ($null -eq $Child -or $null -eq $Parent -or [int]$Child.pid -eq [int]$Parent.pid) { return $false }
  $ct = Get-ScopeCreationTime ([string]$Child.creationTimeUtc)
  $pt = Get-ScopeCreationTime ([string]$Parent.creationTimeUtc)
  return ($null -ne $ct -and $null -ne $pt -and $pt -le $ct)
}

function Get-ScopeExclusion([object]$P, [hashtable]$ByPid, [string]$Role) {
  $seen = New-Object System.Collections.Generic.HashSet[int]
  $current = $P; $disposition = 'unresolved'; $reason = 'parent-chain-unproven'
  while ($null -ne $current) {
    $id = [int]$current.pid
    if (-not $seen.Add($id)) { $reason = 'parent-chain-cycle'; break }
    if ($null -ne $ByPid -and $ByPid.ContainsKey($id) -and $null -eq $ByPid[$id]) { $reason = 'duplicate-pid-in-inventory'; break }
    if (Is-ExeName ([string]$current.name) 'YeManCC') {
      if ([string]::IsNullOrWhiteSpace([string]$current.path)) { $reason = 'root-executable-path-unavailable' }
      elseif (-not [string]::Equals([string]$current.path,$ExePath,[StringComparison]::OrdinalIgnoreCase)) { $disposition = 'foreign'; $reason = 'different-root-exe-path' }
      elseif ($null -eq (Get-ScopeCreationTime ([string]$current.creationTimeUtc))) { $reason = 'root-creation-time-missing-or-invalid' }
      elseif ($scopeRootIdentity -and $scopeRootIdentity -ne ('{0}|{1}' -f $id,[string]$current.creationTimeUtc)) { $reason = 'root-instance-differs-from-pinned' }
      else { $reason = 'root-not-unique-or-chain-unproven' }
      break
    }
    $parentId = [int]$current.ppid
    if ($null -eq $ByPid -or -not $ByPid.ContainsKey($parentId)) { $reason = 'parent-not-in-inventory'; break }
    $parent = $ByPid[$parentId]
    if ($null -eq $parent) { $reason = 'duplicate-parent-pid-in-inventory'; break }
    if (-not (Test-ScopeParentEdge $current $parent)) {
      $ct = Get-ScopeCreationTime ([string]$current.creationTimeUtc)
      $pt = Get-ScopeCreationTime ([string]$parent.creationTimeUtc)
      $reason = if ($null -eq $ct -or $null -eq $pt) { 'parent-or-child-creation-time-missing-or-invalid' } else { 'parent-newer-than-child-or-self-link' }
      break
    }
    $current = $parent
  }
  return [pscustomobject]@{role=$Role;include=$false;association=('scope-' + $disposition);scopeDisposition=$disposition;evidence=$reason}
}

function Get-DescendantSet([object[]]$All, [int[]]$Roots) {
  $children = @{}
  $byPid = New-ScopePidMap $All
  foreach ($p in $All) {
    if ($ExePath) {
      if ($null -eq $byPid[[int]$p.pid] -or -not $byPid.ContainsKey([int]$p.ppid) -or -not (Test-ScopeParentEdge $p $byPid[[int]$p.ppid])) { continue }
      # A second YMCC executable is another instance, not an ordinary child.
      if ((Is-ExeName ([string]$p.name) 'YeManCC') -and [int]$p.pid -notin $Roots) { continue }
    }
    if (-not $children.ContainsKey([int]$p.ppid)) { $children[[int]$p.ppid] = New-Object System.Collections.Generic.List[int] }
    [void]$children[[int]$p.ppid].Add([int]$p.pid)
  }
  $set = New-Object System.Collections.Generic.HashSet[int]
  $q = New-Object System.Collections.Generic.Queue[int]
  foreach ($r in $Roots) { if ($set.Add($r)) { $q.Enqueue($r) } }
  while ($q.Count -gt 0) {
    $cur = $q.Dequeue()
    if ($children.ContainsKey($cur)) { foreach ($c in $children[$cur]) { if ($set.Add($c)) { $q.Enqueue($c) } } }
  }
  return ,$set
}

function Get-WebView2Kind([string]$CommandLine) {
  if ([string]::IsNullOrWhiteSpace($CommandLine)) { return 'unknown' }
  $match = [regex]::Match($CommandLine, '(?i)(?:^|\s)--type(?:\s*=\s*|\s+)(?:"([^"]+)"|([^\s"]+))')
  if (-not $match.Success) {
    if ($CommandLine -match '(?i)(?:^|\s)--type(?:\s|=|$)') { return 'unknown' }
    return 'browser'
  }
  $kind = $(if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }).ToLowerInvariant()
  if ($kind -in @('renderer','gpu-process','utility','crashpad-handler')) { return $kind }
  return 'other'
}

function Ensure-SystemCpuType {
  if (-not ('YmccCpu.NativeSystemTimes' -as [type])) {
    Add-Type @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace YmccCpu {
  public static class NativeSystemTimes {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetSystemTimes(out long idle, out long kernel, out long user);
    public static long[] Read() {
      long idle, kernel, user;
      if (!GetSystemTimes(out idle, out kernel, out user)) throw new Win32Exception(Marshal.GetLastWin32Error());
      return new long[] { idle, kernel, user };
    }
  }
}
"@
  }
}

function Get-SystemCpuInterval([long[]]$Previous,[long[]]$Current) {
  if ($Previous.Count -ne 3 -or $Current.Count -ne 3) { throw 'system-cpu-counter-shape-invalid' }
  $idle=$Current[0]-$Previous[0]
  $kernel=$Current[1]-$Previous[1]
  $user=$Current[2]-$Previous[2]
  $total=$kernel+$user
  $busy=$total-$idle # Windows kernel time already includes idle.
  if ($idle -lt 0 -or $kernel -lt 0 -or $user -lt 0 -or $total -le 0 -or $busy -lt 0 -or $busy -gt $total) { throw 'system-cpu-counter-invalid-delta' }
  return [pscustomobject]@{percent=100.0*$busy/$total;intervalTicks=@($idle,$kernel,$user)}
}

function Is-TrueValue([object]$Value) { return ($Value -is [bool] -and $Value -eq $true) }

function Is-FalseValue([object]$Value) { return ($Value -is [bool] -and $Value -eq $false) }

function Evidence-Utc([object]$Value) {
  try { return ([datetimeoffset]::Parse([string]$Value,[Globalization.CultureInfo]::InvariantCulture)).UtcDateTime } catch { return $null }
}

function Test-SameTarget([object]$Record,[object]$Start) {
  return ($Record -and $Start -and [int]$Record.targetPid -eq [int]$Start.targetPid -and
    [string]::Equals([string]$Record.targetExe,[string]$Start.targetExe,[StringComparison]::OrdinalIgnoreCase) -and
    [string]$Record.targetCreationTimeUtc -eq [string]$Start.targetCreationTimeUtc)
}

function Get-ControlEvidenceAssessment([object]$Life, [object]$Virtual, [string]$ExpectedSid) {
  $errors=New-Object System.Collections.Generic.List[string]
  if (-not $Life.exists) {$errors.Add('lifecycle-jsonl-missing')};if($Life.parseErrors.Count){$errors.Add('lifecycle-jsonl-parse-error')}
  if (-not $Virtual.exists) {$errors.Add('virtual-handshake-jsonl-missing')};if($Virtual.parseErrors.Count){$errors.Add('virtual-handshake-jsonl-parse-error')}
  if ([string]::IsNullOrWhiteSpace($ExpectedSid)) {$errors.Add('expected-session-id-required')}
  $rows=@($Life.records|Where-Object{[string]$_.sessionId -eq $ExpectedSid -and $ExpectedSid})
  $priv=@($rows|Where-Object{$_.action -eq 'privilege.acquire' -and (Is-TrueValue $_.privilege.canMutateYmcc)}|Select-Object -Last 1)
  $stop=@($rows|Where-Object{$_.action -eq 'ymcc.stop' -and $_.result -ne 'started'}|Select-Object -Last 1)
  $startRows=@($rows|Where-Object{$_.action -eq 'ymcc.start'}|Select-Object -Last 1);$startRecord=if($startRows.Count){$startRows[0]}else{$null}
  $restart=@($rows|Where-Object{$_.action -eq 'ymcc.restart'}|Select-Object -Last 1)
  $privOk=($priv.Count -eq 1)
  $stopOk=($stop.Count -eq 1 -and (Is-TrueValue $stop[0].applied) -and (Is-TrueValue $stop[0].rootStopped) -and (Is-TrueValue $stop[0].cleanupComplete))
  $startOk=($startRecord -and (Is-TrueValue $startRecord.applied) -and (Is-TrueValue $startRecord.targetPrivilegeVerified) -and
    (Is-TrueValue $startRecord.targetTokenEvidence.ok) -and (Is-TrueValue $startRecord.targetTokenEvidence.isElevated) -and
    $startRecord.targetTokenEvidence.integrityLevel -in @('high','system') -and [int]$startRecord.targetPid -gt 0 -and
    [string]$startRecord.targetCreationTimeUtc -and (Test-EvidenceFresh $startRecord.timestampUtc))
  $restartOk=($restart.Count -eq 1 -and (Is-TrueValue $restart[0].applied) -and (Is-TrueValue $restart[0].stopApplied) -and (Is-TrueValue $restart[0].startApplied))
  $forced=@($rows|Where-Object{$_.action -eq 'ymcc.stop.force'}).Count -gt 0
  if(-not $privOk){$errors.Add('privilege-not-verified')};if(-not $stopOk){$errors.Add('stop-or-cleanup-not-verified')};if(-not $startOk){$errors.Add('start-token-or-freshness-not-verified')};if(-not $restartOk){$errors.Add('restart-not-verified')};if($forced){$errors.Add('unsafe-force-not-valid-for-cpu-scenario')}
  if($startRecord -and $ExePath -and -not [string]::Equals($ExePath,[string]$startRecord.targetExe,[StringComparison]::OrdinalIgnoreCase)){$errors.Add('lifecycle-exe-path-mismatch')}
  # Chronology prevents a later failed/stop-only operation from reusing an older success.
  if($stopOk -and $startOk -and $restartOk){
    $tStop=Evidence-Utc $stop[0].timestampUtc;$tStart=Evidence-Utc $startRecord.timestampUtc;$tRestart=Evidence-Utc $restart[0].timestampUtc
    if($null -eq $tStop -or $null -eq $tRestart -or $tStop -gt $tStart -or $tRestart -lt $tStart){$errors.Add('lifecycle-order-invalid')}
  }
  $featureResults=[ordered]@{}
  $states=@{fan=$FanState;gamepad=$GamepadState;gyro=$GyroState}
  foreach($feature in @('fan','gamepad','gyro')) {
    $issues=New-Object System.Collections.Generic.List[string]
    $fRows=@($Virtual.records|Where-Object{[string]$_.sessionId -eq $ExpectedSid -and [string]$_.feature -eq $feature}|Select-Object -Last 1)
    $r=if($fRows.Count){$fRows[0]}else{$null};$state=$states[$feature]
    if($state -notin @('on','off')){$issues.Add('requested-state-must-be-on-or-off')}
    if(-not $r){$issues.Add('feature-evidence-missing')}
    else {
      if($r.schema -ne 'ymcc-virtual-handshake-v1'){$issues.Add('unsupported-evidence-schema')}
      if($r.evidenceScope -ne 'ymcc-integrated'){$issues.Add('standalone-sandbox-is-not-product-scenario')}
      if(-not (Test-SameTarget $r $startRecord)){$issues.Add('target-identity-mismatch')}
      if($r.requestedState -ne $state -or $r.readback.actualState -ne $state){$issues.Add('requested-readback-state-mismatch')}
      if(-not (Is-TrueValue $r.applied) -or -not (Is-TrueValue $r.scenarioVerified)){$issues.Add('application-or-runtime-not-verified')}
      if(-not (Test-EvidenceFresh $r.stateReadAtUtc) -or (Evidence-Utc $r.stateReadAtUtc) -lt (Evidence-Utc $startRecord.timestampUtc)){$issues.Add('stale-or-pre-start-readback')}
      if([string]::IsNullOrWhiteSpace([string]$r.evidenceSource) -or $null -eq $r.rawEvidence -or $r.rawEvidence -isnot [pscustomobject] -or @($r.rawEvidence.PSObject.Properties).Count -eq 0){$issues.Add('raw-runtime-evidence-missing')}
      if($r.readback.mode -notin @('virtual','physical','off')){$issues.Add('source-mode-unknown')}
      if($state -eq 'on') {
        if($feature -eq 'gamepad' -and -not (Is-TrueValue $r.readback.virtualTargetEnabled)){$issues.Add('virtual-target-not-enabled')}
        if($feature -eq 'gyro') {
          if(-not (Is-TrueValue $r.readback.motionEnabled)){$issues.Add('motion-not-enabled')}
          if($null -eq $r.readback.gyroSequenceBefore -or $null -eq $r.readback.gyroSequenceAfter -or $null -eq $r.readback.accelSequenceBefore -or $null -eq $r.readback.accelSequenceAfter -or
             [double]$r.readback.gyroSequenceAfter -le [double]$r.readback.gyroSequenceBefore -or [double]$r.readback.accelSequenceAfter -le [double]$r.readback.accelSequenceBefore){$issues.Add('gyro-accel-sequences-not-increasing')}
        }
        if($feature -eq 'fan') {
          if(-not (Is-TrueValue $r.readback.controlActive)){$issues.Add('fan-control-not-active')}
          if($r.readback.mode -eq 'virtual' -and (-not (Is-FalseValue $r.readback.hardwareWritesEnabled) -or -not (Is-FalseValue $r.readback.hardwareWritesObserved))){$issues.Add('virtual-fan-hardware-write-safety-unproven')}
        }
      } elseif($state -eq 'off') {
        if($feature -eq 'gamepad' -and -not (Is-FalseValue $r.readback.virtualTargetEnabled)){$issues.Add('virtual-target-not-confirmed-off')}
        if($feature -eq 'gyro' -and -not (Is-FalseValue $r.readback.motionEnabled)){$issues.Add('motion-not-confirmed-off')}
        if($feature -eq 'fan') {
          if(-not (Is-FalseValue $r.readback.controlActive)){$issues.Add('fan-control-release-unproven')}
          if($r.readback.mode -eq 'virtual') {
            if(-not (Is-TrueValue $r.readback.virtualCloseConfirmed) -or -not (Is-FalseValue $r.readback.hardwareWritesEnabled) -or -not (Is-FalseValue $r.readback.hardwareWritesObserved)){$issues.Add('virtual-fan-close-or-zero-write-unproven')}
          } elseif(-not (Is-TrueValue $r.readback.oemRestoreConfirmed)){$issues.Add('fan-oem-restore-unproven')}
        }
      }
    }
    $featureResults[$feature]=[ordered]@{requestedState=$state;verified=($issues.Count -eq 0);errors=@($issues);stateReadAtUtc=$(if($r){$r.stateReadAtUtc}else{$null});evidenceSource=$(if($r){$r.evidenceSource}else{$null})}
    foreach($issue in $issues){$errors.Add("${feature}:$issue")}
  }
  return [ordered]@{required=[bool]$RequireControlEvidence;expectedSessionId=$ExpectedSid;maxAgeSeconds=$EvidenceMaxAgeSeconds;
    lifecycle=[ordered]@{exists=$Life.exists;recordCount=@($Life.records).Count;privilegeVerified=$privOk;stopVerified=$stopOk;startVerified=[bool]$startOk;restartVerified=$restartOk;unsafeForceObserved=$forced};
    target=$(if($startRecord){[ordered]@{pid=$startRecord.targetPid;path=$startRecord.targetExe;creationTimeUtc=$startRecord.targetCreationTimeUtc}}else{$null});
    virtual=[ordered]@{exists=$Virtual.exists;recordCount=@($Virtual.records).Count;features=$featureResults};
    scenarioVerified=($errors.Count -eq 0);errors=@($errors)}
}
function Get-BaseRole([object]$P) {
  if ([string]::Equals([string]$P.path,$script:ExePath,[StringComparison]::OrdinalIgnoreCase) -or (Is-ExeName $P.name 'YeManCC')) { return 'ymcc-native' }
  $names = @{msedgewebview2='webview2';YeManFanHost='fan-host';YeManInputHost='input-host';YeManRecoveryService='recovery-service';YeManTdpCtl='tdp-helper';YeManLightSetter='light-setter'}
  foreach ($n in $names.Keys) { if (Is-ExeName $P.name $n) { return $names[$n] } }
  return $null
}
function Test-PotentialDescendant([object]$P, [hashtable]$ByPid, [int[]]$RootIds) {
  # Only identifies candidates to investigate; never establishes ownership.
  $seen = New-Object 'Collections.Generic.HashSet[int]'
  $current = $P
  while ($null -ne $current -and $seen.Add([int]$current.pid)) {
    if ([int]$current.ppid -in $RootIds) { return $true }
    $current = $ByPid[[int]$current.ppid]
  }
  return $false
}
function Get-Role([object]$P, [Collections.Generic.HashSet[int]]$Desc, [hashtable]$ByPid) {
  $role = Get-BaseRole $P
  if (-not $role) { $role = 'ymcc-child-other' }
  if (-not (Test-ExactExePath $script:ExePath)) { return [pscustomobject]@{role=$role;include=$false;scopeDisposition='unresolved';evidence='exact-exe-path-required'} }
  if ($Desc.Contains([int]$P.pid)) {
    return [pscustomobject]@{role=$role;include=$true;scopeDisposition='owned';evidence='pinned-root-and-timestamp-checked-parent-chain'}
  }
  return Get-ScopeExclusion $P $ByPid $role
}
function Get-DataRootHash([string]$CommandLine) {
  $m = [regex]::Match($CommandLine,'(?i)(?:^|\s)--user-data-dir(?:=|\s+)(?:"([^"]+)"|([^\s"]+))')
  if (-not $m.Success) { return $null }
  $v = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
  return Get-Hash $v
}
function Get-Candidates([object[]]$All) {
  $byPid = New-ScopePidMap $All
  $matching = @($All | Where-Object { [string]::Equals([string]$_.path,$script:ExePath,[StringComparison]::OrdinalIgnoreCase) -and (Test-ExactExePath $script:ExePath) })
  $roots = @(); $rootReason = 'exact-root-not-observed'
  if ($matching.Count -gt 1) { $rootReason = 'root-not-unique' }
  elseif ($matching.Count -eq 1) {
    $p = $matching[0]
    if ($null -eq $byPid[[int]$p.pid]) { $rootReason = 'duplicate-root-pid' }
    elseif ($null -eq (Get-ScopeCreationTime $p.creationTimeUtc)) { $rootReason = 'root-creation-time-missing-or-invalid' }
    else {
      $key = '{0}|{1}' -f $p.pid,$p.creationTimeUtc
      if (-not $script:scopeRootIdentity) { $script:scopeRootIdentity = $key }
      if ($script:scopeRootIdentity -eq $key) { $roots = @([int]$p.pid); $rootReason = $null; $script:rootObserved = $true }
      else { $rootReason = 'root-instance-differs-from-pinned' }
    }
  }
  $desc = Get-DescendantSet $All $roots
  $rootIds = @($All | Where-Object { (Get-BaseRole $_) -eq 'ymcc-native' } | ForEach-Object { [int]$_.pid })
  $rows = New-Object Collections.ArrayList
  $seen = New-Object 'Collections.Generic.HashSet[string]'
  $unresolved = 0
  foreach ($p in $All) {
    $baseRole = Get-BaseRole $p
    if (-not $baseRole -and -not (Test-PotentialDescendant $p $byPid $rootIds)) { continue }
    $ri = Get-Role $p $desc $byPid
    $key = '{0}|{1}' -f $p.pid,$p.creationTimeUtc
    if (-not $seen.Add($key)) { continue }
    if (-not $ri.include) {
      if ($ri.scopeDisposition -eq 'unresolved') { $unresolved++ }
      # Private bookkeeping includes only role/disposition in addition to the public four fields.
      $script:scopeExcluded[$key] = [pscustomobject]@{pid=[int]$p.pid;creationTimeUtc=[string]$p.creationTimeUtc;name=[string]$p.name;
        exclusionReason=[string]$ri.evidence;role=[string]$ri.role;disposition=[string]$ri.scopeDisposition}
      continue
    }
    $kind = if ($ri.role -eq 'webview2') { Get-WebView2Kind $p.commandLine } else { $null }
    [void]$rows.Add([pscustomobject]@{key=$key;pid=[int]$p.pid;parentPid=[int]$p.ppid;name=[string]$p.name;creationTimeUtc=[string]$p.creationTimeUtc;
      role=[string]$ri.role;association='exact-pinned-root-tree';associationEvidence=[string]$ri.evidence;
      executablePathSha256=(Get-Hash $p.path);dataRootSha256=(Get-DataRootHash $p.commandLine);webview2Kind=$kind;
      inventoryCpuReadOk=[bool]$p.cpuReadOk})
  }
  return [pscustomobject]@{roots=$roots;rows=$rows.ToArray();rootValid=($roots.Count -eq 1);rootReason=$rootReason;unresolvedCount=$unresolved;scopeComplete=($roots.Count -eq 1 -and $unresolved -eq 0)}
}
function Get-PublicExclusions {
  return ,@(foreach ($e in @($script:scopeExcluded.Values | Sort-Object pid,creationTimeUtc)) {
    [pscustomobject]@{pid=$e.pid;creationTimeUtc=$e.creationTimeUtc;name=$e.name;exclusionReason=$e.exclusionReason}
  })
}
function Read-ProcessSnapshot([int]$ProcessId) {
  if (-not ('YmccCpu.ReadOnlyProcessTimes' -as [type])) {
    Add-Type @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace YmccCpu {
  public static class ReadOnlyProcessTimes {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetProcessTimes(IntPtr h, out long creation, out long exit, out long kernel, out long user);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool CloseHandle(IntPtr h);
    public static long[] Read(int pid) {
      // Read-only limited-query handle. One handle and one API call bind creation and CPU.
      IntPtr h = OpenProcess(0x1000, false, pid);
      if (h == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
      try {
        long creation, exit, kernel, user;
        if (!GetProcessTimes(h, out creation, out exit, out kernel, out user))
          throw new Win32Exception(Marshal.GetLastWin32Error());
        return new long[] { creation, kernel, user };
      } finally { CloseHandle(h); }
    }
  }
}
"@
  }
  $times=[YmccCpu.ReadOnlyProcessTimes]::Read($ProcessId)
  return [pscustomobject]@{creationFileTime=$times[0];cpuMs=(([double]$times[1]+[double]$times[2])/10000.0);readAtMs=(Get-MonotonicMilliseconds)}
}
function Get-ProcessCpu([int]$ProcessId, [string]$Name, [string]$CreationTimeUtc) {
  try {
    $created=Get-ScopeCreationTime $CreationTimeUtc
    if ($null -eq $created) { throw 'CPU_PROCESS_IDENTITY_CHANGED' }
    $snapshot=Read-ProcessSnapshot $ProcessId
    if (-not (Test-Counter $snapshot.creationFileTime)) { throw 'CPU_PROCESS_IDENTITY_CHANGED' }
    # CIM_DATETIME is six microsecond digits; FILETIME is 100ns. Integer 1us buckets only.
    [long]$nativeFileTime=$snapshot.creationFileTime
    [long]$cimFileTime=$created.ToFileTimeUtc()
    if (($nativeFileTime-($nativeFileTime % [long]10)) -ne ($cimFileTime-($cimFileTime % [long]10))) { throw 'CPU_PROCESS_IDENTITY_CHANGED' }
    $identityKey='{0}|{1}' -f $ProcessId,$CreationTimeUtc
    if ($script:processCreationFileTimes.ContainsKey($identityKey) -and $script:processCreationFileTimes[$identityKey] -ne $nativeFileTime) { throw 'CPU_PROCESS_IDENTITY_CHANGED' }
    if (-not (Test-Counter $snapshot.cpuMs)) { throw 'CPU_COUNTER_MISSING_OR_INVALID' }
    # Every sample re-reads unrounded native FILETIME with CPU from the same retained query handle.
    $script:processCreationFileTimes[$identityKey]=$nativeFileTime
    return [pscustomobject]@{ok=$true;cpuMs=[double]$snapshot.cpuMs;readAtMs=$snapshot.readAtMs;error=$null}
  } catch {
    $code='CPU_PROCESS_READ_UNAVAILABLE'
    if ($_.Exception.Message -in @('CPU_PROCESS_IDENTITY_CHANGED','CPU_COUNTER_MISSING_OR_INVALID')) { $code=$_.Exception.Message }
    return [pscustomobject]@{ok=$false;cpuMs=$null;readAtMs=(Get-MonotonicMilliseconds);error=$code}
  }
}
function Register-Identity([object]$C) {
  if (-not $script:identityMap.ContainsKey($C.key)) {
    if ($script:lastPidIdentity.ContainsKey([int]$C.pid) -and $script:lastPidIdentity[[int]$C.pid] -ne $C.key) {
      Add-FactError $script:pidReuse 'pid-reuse' 'PID_CREATION_IDENTITY_CHANGED' @{pid=$C.pid}
    }
    $script:lastPidIdentity[[int]$C.pid] = $C.key
    $id = $C | Select-Object *
    foreach ($pair in @(@('firstCpuMs',$null),@('lastCpuMs',$null),@('firstCpuElapsedMs',$null),@('lastCpuElapsedMs',$null),@('cpuReadErrors',0))) {
      Add-Member -InputObject $id -NotePropertyName $pair[0] -NotePropertyValue $pair[1]
    }
    $script:identityMap[$C.key] = $id
  }
  return $script:identityMap[$C.key]
}
function Add-Sample([object]$State, [double]$ScheduledMs, [bool]$InventoryOk) {
  $rows = New-Object Collections.ArrayList
  $keys = New-Object 'Collections.Generic.HashSet[string]'
  foreach ($c in $State.rows) {
    [void]$keys.Add($c.key)
    $id = Register-Identity $c
    $r = Get-ProcessCpu $c.pid $c.name $c.creationTimeUtc
    if (-not $c.inventoryCpuReadOk) { $r.ok=$false; $r.cpuMs=$null; $r.error='CIM_CPU_COUNTER_MISSING_OR_INVALID' }
    if ($r.ok -and $null -ne $id.lastCpuMs -and $r.cpuMs -lt $id.lastCpuMs) { $r.ok=$false; $r.cpuMs=$null; $r.error='CPU_COUNTER_REGRESSION' }
    $at = $r.readAtMs - $script:originMs
    if ($r.ok) {
      if ($null -eq $id.firstCpuMs) { $id.firstCpuMs=$r.cpuMs; $id.firstCpuElapsedMs=$at }
      $id.lastCpuMs=$r.cpuMs; $id.lastCpuElapsedMs=$at
    } else {
      $id.cpuReadErrors++
      Add-FactError $script:readErrors 'process-cpu-read' $r.error @{pid=$c.pid;creationTimeUtc=$c.creationTimeUtc}
    }
    [void]$rows.Add([pscustomobject]@{key=$c.key;pid=$c.pid;role=$c.role;webview2Kind=$c.webview2Kind;readAtElapsedMs=$at;cpuMs=$r.cpuMs;readOk=$r.ok;readError=$r.error})
  }
  if ($script:rawSamples.Count -gt 0) {
    foreach ($k in $script:previousKeys) { if (-not $keys.Contains($k)) { [void]$script:churn.Add($k) } }
    foreach ($k in $keys) { if (-not $script:previousKeys.Contains($k)) { [void]$script:churn.Add($k) } }
  }
  $script:previousKeys = $keys
  $elapsed = (Get-MonotonicMilliseconds)-$script:originMs
  $interval = $null
  if ($script:rawSamples.Count -gt 0) { $interval=$elapsed-$script:rawSamples[$script:rawSamples.Count-1].elapsedMs }
  $scopeOk = ($InventoryOk -and $State.scopeComplete)
  [void]$script:rawSamples.Add([pscustomobject]@{sampleIndex=$script:rawSamples.Count;timestampUtc=(Get-NowUtc).ToString('o');elapsedMs=$elapsed;
    scheduledElapsedMs=$ScheduledMs;latenessMs=($elapsed-$ScheduledMs);sampleIntervalMs=$interval;inventoryAgeMs=($elapsed-$script:inventoryCompletedMs);
    rootPids=@($State.roots);scopeComplete=$scopeOk;scopeError=$State.rootReason;processCount=$rows.Count;sampleReadOk=($scopeOk -and @($rows.ToArray() | Where-Object {-not $_.readOk}).Count -eq 0);processes=$rows.ToArray()})
}
function New-Stats([object[]]$Values, [int]$Logical) {
  $all = @($Values); $good = @($all | Where-Object { Test-Counter $_ })
  $empty = [ordered]@{mean=$null;p50=$null;p95=$null;max=$null}
  $result = [ordered]@{sampleCount=$all.Count;validSampleCount=$good.Count;valid=($all.Count -gt 0 -and $good.Count -eq $all.Count);cpuCores=[ordered]@{mean=$null;p50=$null;p95=$null;max=$null};cpuPercentMachine=[ordered]@{mean=$null;p50=$null;p95=$null;max=$null};cpuPercentOneCore=[ordered]@{mean=$null;p50=$null;p95=$null;max=$null}}
  if (-not $result.valid) { return [pscustomobject]$result }
  $v = @($good | ForEach-Object {[double]$_} | Sort-Object)
  $stats = [ordered]@{mean=([double]($v | Measure-Object -Average).Average);p50=$v[[int][math]::Ceiling(0.5*$v.Count)-1];p95=$v[[int][math]::Ceiling(0.95*$v.Count)-1];max=$v[$v.Count-1]}
  foreach ($n in $stats.Keys) {
    $result.cpuCores[$n]=[math]::Round($stats[$n],6); $result.cpuPercentOneCore[$n]=[math]::Round($stats[$n]*100.0,6)
    if ($Logical -gt 0) { $result.cpuPercentMachine[$n]=[math]::Round($stats[$n]*100.0/$Logical,6) }
  }
  return [pscustomobject]$result
}
function Get-IntervalStats([string[]]$Keys, [object[]]$Samples, [int]$Logical, [bool]$VerifiedAbsent=$false) {
  $set = New-Object 'Collections.Generic.HashSet[string]'
  foreach ($k in $Keys) { [void]$set.Add($k) }
  $intervals = New-Object Collections.ArrayList
  $values = New-Object Collections.ArrayList
  for ($i=1; $i -lt $Samples.Count; $i++) {
    $a=$Samples[$i-1]; $b=$Samples[$i]; $dt=$b.elapsedMs-$a.elapsedMs
    $ok = ($a.scopeComplete -and $b.scopeComplete -and $dt -gt 0 -and ($set.Count -gt 0 -or $VerifiedAbsent))
    $sum=0.0; $cpu=0.0; $reason=$null
    $prev=@{}; $cur=@{}
    foreach ($r in $a.processes) { if ($prev.ContainsKey($r.key)) { $ok=$false; $reason='duplicate-sample-identity' }; $prev[$r.key]=$r }
    foreach ($r in $b.processes) { if ($cur.ContainsKey($r.key)) { $ok=$false; $reason='duplicate-sample-identity' }; $cur[$r.key]=$r }
    foreach ($k in $set) {
      if (-not $prev.ContainsKey($k) -or -not $cur.ContainsKey($k)) { $ok=$false; $reason='identity-missing-in-interval'; continue }
      $x=$prev[$k]; $y=$cur[$k]
      if (-not $x.readOk -or -not $y.readOk -or -not (Test-Counter $x.cpuMs) -or -not (Test-Counter $y.cpuMs)) { $ok=$false; $reason='counter-unreadable'; continue }
      $readDt=$y.readAtElapsedMs-$x.readAtElapsedMs; $delta=$y.cpuMs-$x.cpuMs
      if ($readDt -le 0 -or $delta -lt 0) { $ok=$false; $reason='counter-regression-or-nonmonotonic-time'; continue }
      $sum += $delta/$readDt; $cpu += $delta
    }
    $value=$null
    if ($ok) { $value=$sum }
    [void]$values.Add($value)
    [void]$intervals.Add([pscustomobject]@{sampleIndex=$i;elapsedMs=$dt;valid=$ok;reason=$reason;cpuCores=$value;cpuDeltaMs=$(if($ok){$cpu}else{$null})})
  }
  $stats=New-Stats $values.ToArray() $Logical
  Add-Member -InputObject $stats -NotePropertyName intervals -NotePropertyValue $intervals.ToArray()
  return $stats
}
function Metric([object[]]$Rows, [double]$ElapsedSeconds, [int]$Logical, [bool]$Valid, [bool]$VerifiedAbsent=$false, [object[]]$Samples=@()) {
  $rowsArray=@($Rows); $keys=@($rowsArray | ForEach-Object {$_.key} | Select-Object -Unique)
  $stats=Get-IntervalStats $keys $Samples $Logical $VerifiedAbsent
  $report=($Valid -and $stats.valid -and $Logical -gt 0)
  $weighted=0.0; $span=0.0; $cpu=0.0
  if ($report) { foreach($v in $stats.intervals) { $weighted += $v.cpuCores*$v.elapsedMs; $span += $v.elapsedMs; $cpu += $v.cpuDeltaMs } }
  $cores=$null
  if ($report -and $span -gt 0) { $cores=$weighted/$span }
  return [pscustomobject]@{pids=@($rowsArray | ForEach-Object {$_.pid} | Select-Object -Unique);processCount=$keys.Count;
    elapsedMs=$(if($report){$span}else{$null});cpuDeltaMs=$(if($report){$cpu}else{$null});
    cpuCores=$(if($null -ne $cores){[math]::Round($cores,6)}else{$null});cpuPercentMachine=$(if($null -ne $cores){[math]::Round($cores*100.0/$Logical,6)}else{$null});
    cpuPercentOneCore=$(if($null -ne $cores){[math]::Round($cores*100.0,6)}else{$null});valid=$report;
    verifiedAbsent=($report -and $VerifiedAbsent -and $keys.Count -eq 0);sampleStats=$stats;
    accounting='sum unique per-process deltaCPU/monotonic-read-dt per sample, then percentiles; time-weighted window mean'}
}
function Read-SystemCpuTicks { Ensure-SystemCpuType; return ,([YmccCpu.NativeSystemTimes]::Read()) }
function System-Cpu {
  if ($script:hostInfo.logicalProcessors -gt 64) {
    try {
      $r=@(Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'" -ErrorAction Stop)
      if ($r.Count -ne 1 -or -not (Test-Counter $r[0].PercentProcessorTime) -or $r[0].PercentProcessorTime -gt 100) { throw 'SYSTEM_COUNTER_INVALID' }
      return [pscustomobject]@{ok=$true;percent=[double]$r[0].PercentProcessorTime;method='CIM-_Total-more-than-64-logical';error=$null;counterTicks=$null;intervalTicks=$null}
    } catch { return [pscustomobject]@{ok=$false;percent=$null;method='CIM-_Total-more-than-64-logical';error='SYSTEM_COUNTER_UNAVAILABLE';counterTicks=$null;intervalTicks=$null} }
  }
  try {
    $current=Read-SystemCpuTicks
    if ($current.Count -ne 3 -or @($current | Where-Object {-not (Test-Counter $_)}).Count -gt 0) { throw 'SYSTEM_COUNTER_INVALID' }
    $previous=$script:systemCpuPrevious; $script:systemCpuPrevious=$current
    if ($null -eq $previous) { return [pscustomobject]@{ok=$false;percent=$null;method='GetSystemTimes';error='baseline-required';counterTicks=@($current);intervalTicks=$null} }
    $r=Get-SystemCpuInterval $previous $current
    return [pscustomobject]@{ok=$true;percent=$r.percent;method='GetSystemTimes';error=$null;counterTicks=@($current);intervalTicks=@($r.intervalTicks)}
  } catch { return [pscustomobject]@{ok=$false;percent=$null;method='GetSystemTimes';error='SYSTEM_COUNTER_UNAVAILABLE';counterTicks=$null;intervalTicks=$null} }
}
function Host-Info {
  $rows=@(Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop)
  if ($rows.Count -ne 1 -or -not (Test-Counter $rows[0].NumberOfLogicalProcessors) -or $rows[0].NumberOfLogicalProcessors -lt 1) { throw 'HOST_TOPOLOGY_UNAVAILABLE' }
  # No user name, computer name, serial number or powercfg/device access.
  return [pscustomobject]@{logicalProcessors=[int]$rows[0].NumberOfLogicalProcessors;powershell=$PSVersionTable.PSVersion.ToString()}
}
function Get-ExecutableInfo([string]$Path) {
  try {
    $f=Get-Item -LiteralPath $Path -ErrorAction Stop
    return [pscustomobject]@{available=$true;path=$Path;lengthBytes=$f.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash}
  } catch { return [pscustomobject]@{available=$false;path=$Path;lengthBytes=$null;sha256=$null;unavailableReason='EXECUTABLE_METADATA_UNAVAILABLE'} }
}
function Get-LogFileMetadata([string]$Path) {
  # Explicit literal file and its ancestor metadata only: no enumeration, recursion or contents.
  try {
    $cursor=$Path
    while (-not [string]::IsNullOrWhiteSpace($cursor)) {
      try { $item=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop }
      catch [System.Management.Automation.ItemNotFoundException] { return [pscustomobject]@{status='missing';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code='LOG_FILE_MISSING'} }
      if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return [pscustomobject]@{status='rejected';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code='LOG_REPARSE_POINT_REJECTED'} }
      if ($cursor -eq $Path) { $leaf=$item }
      $next=[IO.Path]::GetDirectoryName($cursor)
      if ($next -eq $cursor) { break }; $cursor=$next
    }
    if ($null -eq $leaf -or $leaf.PSIsContainer) { return [pscustomobject]@{status='rejected';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code='LOG_REGULAR_FILE_REQUIRED'} }
    $openOk=$false; $code=$null
    try {
      $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
      try { $openOk=$true } finally { $stream.Dispose() }
    } catch { $code='LOG_READ_OPEN_UNAVAILABLE' }
    return [pscustomobject]@{status=$(if($openOk){'observed'}else{'error'});lengthBytes=[int64]$leaf.Length;lastWriteTimeUtc=$leaf.LastWriteTimeUtc.ToString('o');readOpenOk=$openOk;code=$code}
  } catch { return [pscustomobject]@{status='error';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$false;code='LOG_METADATA_UNAVAILABLE'} }
}
function Log-Manifest {
  if ($script:SkipLogManifest) { return [pscustomobject]@{status='skipped';requestedCount=$script:LogFile.Count;files=@();reason='explicit-SkipLogManifest';logStateInference='none'} }
  if ($script:LogFile.Count -eq 0) { return [pscustomobject]@{status='not-requested';requestedCount=0;files=@();reason='no-explicit-LogFile';logStateInference='none'} }
  if ($script:LogFile.Count -gt 32) { return [pscustomobject]@{status='error';requestedCount=$script:LogFile.Count;files=@();reason='LOG_FILE_LIMIT_32';logStateInference='none'} }
  $files=New-Object Collections.ArrayList; $seen=New-Object 'Collections.Generic.HashSet[string]'
  foreach($rawPath in $script:LogFile) {
    $path=$rawPath; $reject=$null
    if ([string]::IsNullOrWhiteSpace($path) -or $path -notmatch '^(?:[A-Za-z]:\\|\\\\[^\\]+\\[^\\]+\\)' -or $path -match '[*?]' -or $path -match '^\\\\[.?]\\' -or $path -match ':[^\\]') { $reject='LOG_LITERAL_ABSOLUTE_FILE_REQUIRED' }
    elseif ($path -match '(?i)settings?|tokens?|leases?|profiles?|authorization|credentials?|secrets?|passwords?|confirm(?:ation)?|cookies?|backup|archives?|备份') { $reject='LOG_SENSITIVE_PATH_REJECTED' }
    elseif ([IO.Path]::GetExtension($path) -notin @('.log','.txt','.jsonl','.json')) { $reject='LOG_EXTENSION_REJECTED' }
    if (-not $reject) { try { $path=[IO.Path]::GetFullPath($path) } catch { $reject='LOG_LITERAL_ABSOLUTE_FILE_REQUIRED' } }
    $hash=Get-Hash $path
    if ($hash -and -not $seen.Add($hash)) { continue }
    $basename='other-log'
    # Never preserve arbitrary basenames, which themselves may contain tokens/user names.
    $base=[IO.Path]::GetFileName($path)
    if ($base -match '^(?i)(cpu|fan|gyro|input|native|frontend|collector)\.(log|txt|jsonl|json)$') { $basename=$base.ToLowerInvariant() }
    $m=$null
    if ($reject) { $m=[pscustomobject]@{status='rejected';lengthBytes=$null;lastWriteTimeUtc=$null;readOpenOk=$null;code=$reject} }
    else { $m=Get-LogFileMetadata $path }
    $written=Get-ScopeCreationTime ([string]$m.lastWriteTimeUtc)
    $age=$null; $freshness='unknown'; $empty=$null
    if ($null -ne $written) { $age=((Get-NowUtc)-$written).TotalSeconds; $freshness='fresh'; if($age -gt $script:EvidenceMaxAgeSeconds){$freshness='stale'}; if($age -lt -5){$freshness='future'} }
    if ($null -ne $m.lengthBytes) { $empty=($m.lengthBytes -eq 0) }
    [void]$files.Add([pscustomobject]@{pathSha256=$hash;basename=$basename;status=$m.status;lengthBytes=$m.lengthBytes;
      lastWriteTimeUtc=$(if($null -ne $written){$written.ToString('o')}else{$null});readOpenOk=$m.readOpenOk;code=$m.code;isEmpty=$empty;ageSeconds=$age;freshness=$freshness})
  }
  $status='completed'
  if (@($files.ToArray() | Where-Object {$_.status -in @('error','rejected','missing')}).Count) { $status='partial' }
  return [pscustomobject]@{status=$status;requestedCount=$script:LogFile.Count;observedCount=$files.Count;files=$files.ToArray();staleAfterSeconds=$script:EvidenceMaxAgeSeconds;
    contentsRead=$false;contentsCopied=$false;recursiveScan=$false;logStateInference='none; empty/missing/stale/error does not imply logging disabled'}
}
function Read-EvidenceJsonl([string]$Path, [string]$Kind) {
  $r=[ordered]@{kind=$Kind;exists=$false;records=@();parseErrors=@()}
  if ([string]::IsNullOrWhiteSpace($Path)) { return [pscustomobject]$r }
  try {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return [pscustomobject]$r }
    $r.exists=$true; $records=New-Object Collections.ArrayList; $errors=New-Object Collections.ArrayList
    $index=0
    foreach ($line in @(Get-Content -LiteralPath $Path -Encoding UTF8 -ErrorAction Stop)) {
      $index++; if ([string]::IsNullOrWhiteSpace($line)) { continue }
      try {
        $record=$line | ConvertFrom-Json -ErrorAction Stop
        if ($record -isnot [pscustomobject]) { throw 'OBJECT_REQUIRED' }
        [void]$records.Add($record)
      } catch { [void]$errors.Add([pscustomobject]@{lineIndex=$index;code='EVIDENCE_JSONL_INVALID'}) }
    }
    $r.records=$records.ToArray(); $r.parseErrors=$errors.ToArray()
  } catch { $r.parseErrors=@([pscustomobject]@{lineIndex=$null;code='EVIDENCE_FILE_UNREADABLE'}) }
  return [pscustomobject]$r
}
function Test-EvidenceFresh([object]$Value) {
  $t=Get-ScopeCreationTime ([string]$Value)
  if ($null -eq $t) { return $false }
  $age=((Get-NowUtc)-$t).TotalSeconds
  return ($age -ge -5 -and $age -le $script:EvidenceMaxAgeSeconds)
}
function Get-PublicControlEvidence([object]$Assessment) {
  $features=[ordered]@{}
  foreach ($name in @('fan','gamepad','gyro')) {
    $v=$Assessment.virtual.features[$name]
    $features[$name]=[ordered]@{requestedState=$v.requestedState;verified=[bool]$v.verified;errors=@($v.errors | ForEach-Object {$_});
      stateReadAtUtc=(Creation-Utc $v.stateReadAtUtc);evidenceSourceSha256=(Get-Hash $v.evidenceSource)}
  }
  $target=$null
  if ($Assessment.target) {
    $target=[ordered]@{pid=[int]$Assessment.target.pid;creationTimeUtc=(Creation-Utc $Assessment.target.creationTimeUtc);exePathSha256=(Get-Hash $Assessment.target.path)}
  }
  return [pscustomobject]@{required=[bool]$script:RequireControlEvidence;expectedSessionIdSha256=(Get-Hash $script:ExpectedSessionId);maxAgeSeconds=$script:EvidenceMaxAgeSeconds;
    lifecycle=$Assessment.lifecycle;target=$target;virtual=[ordered]@{exists=$Assessment.virtual.exists;recordCount=$Assessment.virtual.recordCount;features=$features};
    scenarioVerified=[bool]$Assessment.scenarioVerified;errors=@($Assessment.errors | ForEach-Object {$_})}
}
function Test-ControlTarget([object]$Assessment, [object]$State) {
  if (-not $Assessment.target -or -not $State.rootValid) { return $false }
  $key='{0}|{1}' -f [int]$Assessment.target.pid,(Creation-Utc $Assessment.target.creationTimeUtc)
  return ($key -eq $script:scopeRootIdentity -and [string]::Equals([string]$Assessment.target.path,$script:ExePath,[StringComparison]::OrdinalIgnoreCase))
}
function Get-CollectorSelfMetric {
  $at=(Get-MonotonicMilliseconds)-$script:originMs
  try {
    $p=Get-Process -Id $PID -ErrorAction Stop
    if ($null -eq $p.TotalProcessorTime -or -not (Test-Counter $p.TotalProcessorTime.TotalMilliseconds)) { throw 'SELF_COUNTER_UNAVAILABLE' }
    return [pscustomobject]@{elapsedMs=$at;ok=$true;cpuMs=[double]$p.TotalProcessorTime.TotalMilliseconds;
      workingSetBytes=$(if(Test-Counter $p.WorkingSet64){[int64]$p.WorkingSet64}else{$null});privateMemoryBytes=$(if(Test-Counter $p.PrivateMemorySize64){[int64]$p.PrivateMemorySize64}else{$null});error=$null}
  } catch { return [pscustomobject]@{elapsedMs=$at;ok=$false;cpuMs=$null;workingSetBytes=$null;privateMemoryBytes=$null;error='COLLECTOR_SELF_COUNTER_UNAVAILABLE'} }
}
function Get-ScalarStats([object[]]$Values) {
  $v=@($Values | Where-Object {$null -ne $_ -and -not [double]::IsNaN([double]$_) -and -not [double]::IsInfinity([double]$_)} | ForEach-Object {[double]$_} | Sort-Object)
  if ($v.Count -eq 0) { return [pscustomobject]@{count=0;mean=$null;p50=$null;p95=$null;max=$null;min=$null} }
  return [pscustomobject]@{count=$v.Count;mean=[double]($v | Measure-Object -Average).Average;p50=$v[[int][math]::Ceiling(.5*$v.Count)-1];p95=$v[[int][math]::Ceiling(.95*$v.Count)-1];max=$v[$v.Count-1];min=$v[0]}
}
function Get-Coverage([object[]]$Samples, [double]$RequestedMs, [int]$CadenceMs, [int]$SkippedSlots) {
  $expectedIntervals=[int][math]::Ceiling($RequestedMs/$CadenceMs)
  $actualIntervals=[math]::Max(0,$Samples.Count-1)
  $intervals=@($Samples | Where-Object {$null -ne $_.sampleIntervalMs} | ForEach-Object {$_.sampleIntervalMs})
  $jitter=@($intervals | ForEach-Object {[math]::Abs($_-$CadenceMs)})
  $span=$null; $start=$null; $end=$null
  if ($Samples.Count -gt 0) { $start=$Samples[0].elapsedMs; $end=$Samples[$Samples.Count-1].elapsedMs; $span=$end-$start }
  return [pscustomobject]@{expectedSampleCount=($expectedIntervals+1);actualSampleCount=$Samples.Count;expectedIntervalCount=$expectedIntervals;actualIntervalCount=$actualIntervals;
    sampleCountDifference=(($expectedIntervals+1)-$Samples.Count);intervalCountDeficit=($expectedIntervals-$actualIntervals);scheduledSlotsSkipped=$SkippedSlots;
    missedSampleCount=$SkippedSlots;firstReadElapsedMs=$start;lastReadElapsedMs=$end;observedSpanMs=$span;
    intervalsMs=(Get-ScalarStats $intervals);absoluteJitterMs=(Get-ScalarStats $jitter);latenessMs=(Get-ScalarStats @($Samples | ForEach-Object {$_.latenessMs}));
    definition='expected includes baseline and final boundary; difference is cadence coverage, NOT proof of lost samples. missedSampleCount counts only explicitly skipped scheduler slots.'}
}
function Get-Quality([object[]]$Samples) {
  $reasons=New-Object 'Collections.Generic.List[string]'
  if ($script:inventoryErrors.Count) { $reasons.Add('inventory-read-error') }
  if ($script:readErrors.Count) { $reasons.Add('process-or-counter-read-error') }
  if ($script:pidReuse.Count) { $reasons.Add('pid-reuse') }
  if ($script:churn.Count) { $reasons.Add('process-churn-or-tree-change') }
  if (-not $script:rootObserved) { $reasons.Add('exact-root-not-observed') }
  if (@($script:scopeExcluded.Values | Where-Object {$_.disposition -eq 'unresolved'}).Count) { $reasons.Add('process-root-scope-unresolved') }
  if ($Samples.Count -lt 2) { $reasons.Add('insufficient-samples') }
  if (@($Samples | Where-Object {-not $_.scopeComplete}).Count) { $reasons.Add('incomplete-root-scope') }
  if ($script:skippedSlots -gt 0) { $reasons.Add('scheduled-slots-skipped') }
  if ($script:collectorSelfMetrics.Count -lt 2 -or @($script:collectorSelfMetrics.ToArray() | Where-Object {-not $_.ok}).Count) { $reasons.Add('collector-self-counter-unavailable') }
  if (-not $script:SkipSystemCpu -and @($script:systemRows.ToArray() | Where-Object {-not $_.ok}).Count) { $reasons.Add('system-cpu-counter-unavailable') }
  if ($script:hostInfo.logicalProcessors -lt 1) { $reasons.Add('logical-processor-count-unavailable') }
  $keys=@($script:identityMap.Keys | ForEach-Object {[string]$_})
  $total=Get-IntervalStats $keys $Samples $script:hostInfo.logicalProcessors
  if (-not $total.valid) { $reasons.Add('incomplete-or-invalid-cpu-intervals') }
  return [pscustomobject]@{valid=($reasons.Count -eq 0);invalidReasons=$reasons.ToArray();rootObserved=$script:rootObserved;
    scopeMode='exact-exe-pinned-instance';scopeRootIdentity=$script:scopeRootIdentity;inventoryErrors=$script:inventoryErrors.ToArray();readErrors=$script:readErrors.ToArray();pidReuse=$script:pidReuse.ToArray();
    churnKeys=@($script:churn | ForEach-Object {$_});scopeExcludedCount=$script:scopeExcluded.Count;
    foreignScopeCount=@($script:scopeExcluded.Values | Where-Object {$_.disposition -eq 'foreign'}).Count;unresolvedScopeCount=@($script:scopeExcluded.Values | Where-Object {$_.disposition -eq 'unresolved'}).Count}
}
function Build-Groups([object[]]$Rows, [object[]]$Samples, [bool]$Valid) {
  $groups=[ordered]@{}
  foreach ($role in @('ymcc-native','webview2','fan-host','input-host','recovery-service','tdp-helper','light-setter','ymcc-child-other')) {
    $r=@($Rows | Where-Object {$_.role -eq $role})
    $groups[$role]=Metric $r 0 $script:hostInfo.logicalProcessors $Valid ($Valid -and $r.Count -eq 0) $Samples
  }
  foreach($pair in @(@('ymccNative','ymcc-native'),@('webview2Total','webview2'),@('fanHost','fan-host'),@('inputHost','input-host'),@('recoveryService','recovery-service'),@('tdpHelper','tdp-helper'),@('lightSetter','light-setter'),@('ymccChildOther','ymcc-child-other'))) { $groups[$pair[0]]=$groups[$pair[1]] }
  $groups.ymccTotalUnique=Metric $Rows 0 $script:hostInfo.logicalProcessors $Valid $false $Samples
  return $groups
}
function Get-WebView2Breakdown([object[]]$Rows, [object[]]$Samples, [double]$ElapsedSeconds, [int]$Logical, [bool]$Valid, [bool]$VerifiedAbsent) {
  $web=@($Rows | Where-Object {$_.role -eq 'webview2'})
  $unknown=@($web | Where-Object {$_.webview2Kind -eq 'unknown'})
  $groups=[ordered]@{}; $count=0
  foreach($kind in @('browser','renderer','gpu-process','utility','crashpad-handler','other','unknown')) {
    $r=@($web | Where-Object {$_.webview2Kind -eq $kind}); $count+=$r.Count
    $groups[$kind]=Metric $r $ElapsedSeconds $Logical $Valid ($VerifiedAbsent -and $unknown.Count -eq 0 -and $r.Count -eq 0) $Samples
  }
  return [pscustomobject]@{groups=$groups;quality=[ordered]@{source='in-memory-commandline-allowlisted-subtype';classificationComplete=($Valid -and $unknown.Count -eq 0);
    processCount=$web.Count;classifiedCount=$count;countsMatchTotal=($count -eq $web.Count);accounting='disjoint subledger of webview2Total; never add to ymccTotalUnique'}}
}
function Write-Failure([string]$Code, [int]$ExitCode, [bool]$Started=$false) {
  $summary=[ordered]@{schema='ymcc-cpu-capture-v3';scriptVersion=$script:scriptVersion;status='failed';failure=[ordered]@{code=$Code;exitCode=$ExitCode;phase=$script:phase};
    measurementStarted=$Started;measurementValid=$false;scenarioVerified=$false;comparisonEligible=$false;comparisonIneligibleReason='failed';
    validationScope=$(if($script:isOfflineFixture){'SOFTWARE_TOOL_VALIDATION_ONLY'}else{'READ_ONLY_CAPTURE_UNADJUDICATED'});rogRegressionPassed=$false;realCapturePerformed=$script:realCapturePerformed;
    quality=[ordered]@{valid=$false;invalidReasons=@($Code)};groups=$null}
  try {
    if ($script:outputReady) {
      Write-Json 'summary.json' $summary
      Write-Json 'manifest.json' ([ordered]@{schema='ymcc-cpu-capture-v3';status='failed';exitCode=$ExitCode;files=@('summary.json','manifest.json')})
    }
  } catch { $summary.failure.code='FAILURE_OUTPUT_UNAVAILABLE'; $summary.failure.originalCode=$Code }
  # Even invalid/unwritable output paths have a machine-readable failure on stdout.
  Write-Output ($summary | ConvertTo-Json -Depth 12 -Compress)
  return $ExitCode
}
function Invoke-Collector {
  $script:outputReady=$false; $script:realCapturePerformed=$false; $script:phase='output-init'
  $script:inventoryCount=0; $script:scopeRootIdentity=''; $script:rootObserved=$false; $script:skippedSlots=0; $script:systemCpuPrevious=$null
  $script:scopeExcluded=@{}; $script:identityMap=@{}; $script:lastPidIdentity=@{}; $script:processCreationFileTimes=@{}
  foreach ($n in @('inventoryErrors','readErrors','pidReuse','rawSamples','systemRows','collectorSelfMetrics','inventoryEvents')) { Set-Variable -Name $n -Scope Script -Value (New-Object Collections.ArrayList) }
  $script:churn=New-Object 'Collections.Generic.HashSet[string]'; $script:previousKeys=New-Object 'Collections.Generic.HashSet[string]'
  try {
    Initialize-Output
    $script:phase='argument-validation'
    if (-not (Test-ExactExePath $script:ExePath)) { return Write-Failure 'EXACT_EXE_PATH_REQUIRED' 2 }
    $script:ExePath=[IO.Path]::GetFullPath($script:ExePath)
    $script:originMs=Get-MonotonicMilliseconds
    $life=Read-EvidenceJsonl $script:LifecycleJsonl 'lifecycle'; $virtual=Read-EvidenceJsonl $script:VirtualHandshakeJsonl 'virtual-handshake'
    $script:controlEvidence=Get-ControlEvidenceAssessment $life $virtual $script:ExpectedSessionId
    if ($script:isOfflineFixture -or @(@($life.records)+@($virtual.records) | Where-Object {$_.isFixture -eq $true -or $_.evidenceScope -in @('fixture','SOFTWARE_TOOL_VALIDATION_ONLY')}).Count -gt 0) {
      $script:controlEvidence.scenarioVerified=$false; $script:controlEvidence.errors+= 'fixture-is-not-runtime-evidence'
    }
    $script:phase='control-preflight'
    if ($script:RequireControlEvidence -and -not $script:controlEvidence.scenarioVerified) { return Write-Failure 'CONTROL_EVIDENCE_REQUIRED' 7 }
    $script:hostInfo=Host-Info
    Wait-Collector ($script:WarmupSeconds*1000.0)
    if (-not $script:SkipSystemCpu) { $baseline=System-Cpu }
    $script:originMs=Get-MonotonicMilliseconds
    $startUtc=Get-NowUtc; $script:phase='sample-window'
    $target=$script:Seconds*1000.0; $due=0.0; $nextInventory=-1.0; $state=$null; $inventoryOk=$false; $script:inventoryCompletedMs=0.0
    while ($due -le $target) {
      Wait-Collector ($due-((Get-MonotonicMilliseconds)-$script:originMs))
      $begin=(Get-MonotonicMilliseconds)-$script:originMs
      if ($null -eq $state -or $begin -ge $nextInventory -or $due -eq $target) {
        $inv=Get-Inventory; $state=Get-Candidates $inv.rows; $inventoryOk=$inv.ok
        $script:inventoryCompletedMs=(Get-MonotonicMilliseconds)-$script:originMs; $nextInventory=$script:inventoryCompletedMs+$script:InventoryMilliseconds
      }
      if ($script:controlEvidence.scenarioVerified -and -not (Test-ControlTarget $script:controlEvidence $state)) {
        $script:controlEvidence.scenarioVerified=$false; $script:controlEvidence.errors+='live-target-identity-mismatch'
      }
      if ($script:RequireControlEvidence -and -not $script:controlEvidence.scenarioVerified) { return Write-Failure 'CONTROL_TARGET_CHANGED' 7 ($script:rawSamples.Count -gt 0) }
      Add-Sample $state $due $inventoryOk
      if (-not $script:isOfflineFixture) { $script:realCapturePerformed=$true }
      if (-not $script:SkipSystemCpu) { $sys=System-Cpu; Add-Member -InputObject $sys -NotePropertyName elapsedMs -NotePropertyValue ((Get-MonotonicMilliseconds)-$script:originMs); [void]$script:systemRows.Add($sys) }
      $self=Get-CollectorSelfMetric
      $delta=$null; $cores=$null
      if ($script:collectorSelfMetrics.Count -gt 0) {
        $old=$script:collectorSelfMetrics[$script:collectorSelfMetrics.Count-1]
        $dt=$self.elapsedMs-$old.elapsedMs
        if ($self.ok -and $old.ok -and $dt -gt 0 -and $self.cpuMs -ge $old.cpuMs) { $delta=$self.cpuMs-$old.cpuMs; $cores=$delta/$dt }
        elseif ($self.ok -and $old.ok) { $self.ok=$false; $self.error='COLLECTOR_SELF_COUNTER_REGRESSION_OR_TIME_INVALID' }
      }
      Add-Member -InputObject $self -NotePropertyName cpuDeltaMs -NotePropertyValue $delta
      Add-Member -InputObject $self -NotePropertyName cpuCores -NotePropertyValue $cores
      Add-Member -InputObject $self -NotePropertyName sampleCollectionMs -NotePropertyValue (((Get-MonotonicMilliseconds)-$script:originMs)-$begin)
      [void]$script:collectorSelfMetrics.Add($self)
      if ($due -eq $target) { break }
      $due=[math]::Min($target,$due+$script:SampleMilliseconds)
      $now=(Get-MonotonicMilliseconds)-$script:originMs
      # Skip only whole overdue slots; do not run a burst of artificial catch-up samples.
      while ($due -lt $target -and $now -ge ($due+$script:SampleMilliseconds)) { $script:skippedSlots++; $due=[math]::Min($target,$due+$script:SampleMilliseconds) }
    }
    $endUtc=Get-NowUtc; $elapsed=((Get-MonotonicMilliseconds)-$script:originMs)/1000.0
    $samples=$script:rawSamples.ToArray(); $rows=@($script:identityMap.Values | Sort-Object pid,creationTimeUtc)
    $quality=Get-Quality $samples; $groups=Build-Groups $rows $samples $quality.valid
    $web=Get-WebView2Breakdown $rows $samples $elapsed $script:hostInfo.logicalProcessors $quality.valid $quality.valid
    $coverage=Get-Coverage $samples $target $script:SampleMilliseconds $script:skippedSlots
    $publicEvidence=Get-PublicControlEvidence $script:controlEvidence
    $inputPids=@($rows | Where-Object {$_.role -eq 'input-host'} | ForEach-Object {$_.pid} | Select-Object -Unique)
    $logManifest=Log-Manifest
    $summary=[ordered]@{schema='ymcc-cpu-capture-v3';scriptVersion=$script:scriptVersion;status=$(if($quality.valid){'completed'}else{'invalid'});
      sceneSha256=(Get-Hash $script:Label);stateNoteSha256=(Get-Hash $script:StateNote);ymccEntry=(Get-ExecutableInfo $script:ExePath);
      measurementStarted=$true;measurementValid=$quality.valid;scenarioVerified=$publicEvidence.scenarioVerified;
      comparisonEligible=$false;comparisonIneligibleReason='parent-adjudication-and-ROG-regression-required';
      validationScope=$(if($script:isOfflineFixture){'SOFTWARE_TOOL_VALIDATION_ONLY'}else{'READ_ONLY_CAPTURE_UNADJUDICATED'});rogRegressionPassed=$false;realCapturePerformed=$script:realCapturePerformed;
      state=[ordered]@{requestedFanState=$script:FanState;requestedGamepadState=$script:GamepadState;requestedGyroState=$script:GyroState;requestedStatesAreRuntimeEvidence=$false};
      logManifest=$logManifest;host=$script:hostInfo;window=[ordered]@{startUtc=$startUtc.ToString('o');endUtc=$endUtc.ToString('o');warmupSeconds=$script:WarmupSeconds;requestedSeconds=$script:Seconds;actualElapsedSeconds=$elapsed;
        sampleMilliseconds=$script:SampleMilliseconds;inventoryMilliseconds=$script:InventoryMilliseconds;coverage=$coverage;inventoryCount=$script:inventoryCount;
        inventoryLatencyMs=(Get-ScalarStats @($script:inventoryEvents.ToArray() | ForEach-Object {$_.latencyMs}));sampleCollectionMs=(Get-ScalarStats @($script:collectorSelfMetrics.ToArray() | ForEach-Object {$_.sampleCollectionMs}));
        collectorCpuCoresStats=$(if(@($script:collectorSelfMetrics.ToArray() | Where-Object {-not $_.ok}).Count -gt 0){Get-ScalarStats @()}else{Get-ScalarStats @($script:collectorSelfMetrics.ToArray() | ForEach-Object {$_.cpuCores})});collectorSelfMetrics=$script:collectorSelfMetrics.ToArray()};
      quality=$quality;controlEvidence=$publicEvidence;groups=$groups;webview2Breakdown=$web.groups;webview2BreakdownQuality=$web.quality;processes=$rows;scopeExcludedCandidates=(Get-PublicExclusions);
      featureAttribution=[ordered]@{gamepadPids=$inputPids;gyroPids=$inputPids;inputHostSharedByGamepadAndGyro=($inputPids.Count -gt 0);accounting='shared PID is counted ONCE in inputHost and ymccTotalUnique; presence does not prove either feature on'}}
    $script:phase='output-write'
    Write-Json 'summary.json' $summary; Write-Json 'process-table.json' $rows; Write-Json 'control-evidence.json' $publicEvidence
    Write-Json 'log-manifest.json' $logManifest
    Write-JsonLines 'samples.jsonl' $samples; Write-JsonLines 'system-cpu.jsonl' $script:systemRows.ToArray()
    Write-JsonLines 'inventory.jsonl' $script:inventoryEvents.ToArray(); Write-JsonLines 'collector-self-metrics.jsonl' $script:collectorSelfMetrics.ToArray()
    # Compatibility filenames contain allowlisted assessment only, never raw/redacted record copies.
    Write-JsonLines 'lifecycle-control.jsonl' @($publicEvidence.lifecycle); Write-JsonLines 'virtual-handshake.jsonl' @($publicEvidence.virtual)
    $exitCode=0; if (-not $quality.valid) { $exitCode=3 } elseif ($logManifest.status -in @('partial','error')) { $exitCode=4; $summary.status='partial'; Write-Json 'summary.json' $summary }
    Write-Json 'manifest.json' ([ordered]@{schema='ymcc-cpu-capture-v3';scriptVersion=$script:scriptVersion;status=$summary.status;exitCode=$exitCode;
      validationScope=$summary.validationScope;rogRegressionPassed=$false;realCapturePerformed=$summary.realCapturePerformed;
      files=@('summary.json','manifest.json','samples.jsonl','process-table.json','control-evidence.json','log-manifest.json','system-cpu.jsonl','inventory.jsonl','collector-self-metrics.jsonl','lifecycle-control.jsonl','virtual-handshake.jsonl','execution-report.md')})
    Write-Utf8 (Join-Path $script:OutDir 'execution-report.md') ((@('# YMCC read-only CPU collector',
      ('measurementValid: '+$quality.valid),('scenarioVerified: '+$publicEvidence.scenarioVerified),'comparisonEligible: false; parent adjudication required',
      ('validationScope: '+$summary.validationScope),'rogRegressionPassed: false',('realCapturePerformed: '+$summary.realCapturePerformed),
      ('actual/expected samples: '+$coverage.actualSampleCount+'/'+$coverage.expectedSampleCount),('explicitly skipped scheduler slots: '+$coverage.scheduledSlotsSkipped),
      'Count difference is not lost-sample proof. See summary.window.coverage for intervals/jitter and self cost.',
      'Null means unverified/unreadable/incomplete, NOT zero. Shared input host is counted only once.',
      'Raw command lines, raw evidence and private exception text are never exported. Only explicit bounded log metadata is collected; no log contents or implicit enumeration.')) -join [Environment]::NewLine)
    Write-Output ('measurementValid={0}; scenarioVerified={1}; exitCode={2}' -f $quality.valid,$publicEvidence.scenarioVerified,$exitCode)
    return $exitCode
  } catch {
    $code='COLLECTOR_UNHANDLED_EXCEPTION'; $exitCode=1
    if ($script:phase -eq 'output-init') { $code='OUTPUT_DIRECTORY_UNAVAILABLE_OR_NOT_EMPTY'; $exitCode=2 }
    return Write-Failure $code $exitCode ($script:rawSamples.Count -gt 0)
  }
}
# The sole dispatcher is also exercised by the offline full-path fixtures (exit is intercepted).
$collectorOutput = @(Invoke-Collector)
$collectorExitCode = [int]$collectorOutput[$collectorOutput.Count-1]
if ($collectorOutput.Count -gt 1) { $collectorOutput[0..($collectorOutput.Count-2)] | Write-Output }
exit $collectorExitCode
