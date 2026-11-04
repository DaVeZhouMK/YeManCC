# Resume only processes recorded by the unified Sleep Guard markers.
# The marker is the native game valve's ownership lease. No process
# enumeration, current-game replacement, or "resume every large process"
# fallback is allowed.
$ErrorActionPreference = 'Stop'
$DIR = $PSScriptRoot
$MARKER_DIRS = @(
  (Join-Path $DIR 'Sleep\manual-suspended'),
  (Join-Path $DIR 'Sleep\suspended')
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YmGameValveResumeApi {
    [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool GetProcessTimes(IntPtr h, out System.Runtime.InteropServices.ComTypes.FILETIME creation, out System.Runtime.InteropServices.ComTypes.FILETIME exit, out System.Runtime.InteropServices.ComTypes.FILETIME kernel, out System.Runtime.InteropServices.ComTypes.FILETIME user);
    public static long ProcessCreated(IntPtr h) {
        System.Runtime.InteropServices.ComTypes.FILETIME creation, exit, kernel, user;
        if (!GetProcessTimes(h, out creation, out exit, out kernel, out user)) return 0;
        return ((long)(uint)creation.dwHighDateTime << 32) | (uint)creation.dwLowDateTime;
    }
}
'@

function Test-ResumeInteger($Value) {
  return ($Value -is [int] -or $Value -is [long])
}

function Assert-ResumePlainPath([string]$Path) {
  $current = [IO.Path]::GetFullPath($Path)
  while ($current) {
    try {
      if (([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse path refused' }
    } catch [IO.FileNotFoundException] { } catch [IO.DirectoryNotFoundException] { }
    $parent = [IO.Path]::GetDirectoryName($current)
    if ($parent -eq $current) { break }
    $current = $parent
  }
}

function Read-ResumeBytes([string]$Path, [int]$Limit = 65536) {
  Assert-ResumePlainPath $Path
  $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
  try {
    $length = $stream.Length
    if ($length -le 0 -or $length -gt $Limit) { throw 'Empty or oversized valve document' }
    $bytes = New-Object byte[] ([int]$length)
    $offset = 0
    while ($offset -lt $bytes.Length) {
      $count = $stream.Read($bytes, $offset, $bytes.Length - $offset)
      if ($count -le 0) { throw 'Incomplete valve document' }
      $offset += $count
    }
    if ($stream.Length -ne $length) { throw 'Valve document changed during read' }
    return ,$bytes
  } finally { $stream.Dispose() }
}

function ConvertFrom-ResumeJson([byte[]]$Bytes) {
  $text = ([Text.UTF8Encoding]::new($false, $true)).GetString($Bytes).TrimStart([char]0xFEFF)
  if (-not $text.TrimStart().StartsWith('{')) { throw 'Valve root must be an object' }
  # Bound structure before full deserialization. Windows PowerShell also accepts
  # non-JSON NaN/Infinity primitives and can silently keep the last decoded key.
  # The built-in parser still validates grammar after this bounded lexical pass.
  $stack = [Collections.Generic.List[object]]::new()
  $tokens = [regex]::Matches($text, '"(?:\\.|[^"\\])*"|[{}\[\],:]|[^ \t\r\n{}\[\],:"]+')
  if ($tokens.Count -gt 20000) { throw 'Valve JSON complexity refused' }
  $cursor = 0
  foreach ($token in $tokens) {
    if ($text.Substring($cursor, $token.Index - $cursor) -cnotmatch '\A[ \t\r\n]*\z') { throw 'Invalid valve JSON token' }
    $cursor = $token.Index + $token.Length
    $part = $token.Value
    if ($part -eq '{' -or $part -eq '[') {
      if ($stack.Count -ge 32) { throw 'Valve JSON depth refused' }
      $stack.Add([pscustomobject]@{ Object = ($part -eq '{'); Key = ($part -eq '{'); Keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase) })
    } elseif ($part -eq '}' -or $part -eq ']') {
      if (-not $stack.Count) { throw 'Unbalanced valve JSON' }
      $stack.RemoveAt($stack.Count - 1)
    } elseif ($part -ne ',' -and $part -ne ':' -and -not $part.StartsWith('"')) {
      if ($part -cnotmatch '\A(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\z') { throw 'Invalid valve JSON primitive' }
      if ($part -cmatch '\A[-0-9]') {
        $number = 0.0
        if (-not [double]::TryParse($part, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            [double]::IsNaN($number) -or [double]::IsInfinity($number)) { throw 'Non-finite valve JSON number' }
      }
    } elseif ($stack.Count) {
      $frame = $stack[$stack.Count - 1]
      if ($part -eq ',') { $frame.Key = $frame.Object }
      elseif ($part -eq ':') { $frame.Key = $false }
      elseif ($part.StartsWith('"') -and $frame.Object -and $frame.Key) {
        $key = ConvertFrom-Json -InputObject $part -ErrorAction Stop
        if (-not $frame.Keys.Add($key)) { throw 'Duplicate decoded valve key' }
        $frame.Key = $false
      }
    }
  }
  if ($stack.Count -or $text.Substring($cursor) -cnotmatch '\A[ \t\r\n]*\z') { throw 'Incomplete valve JSON' }
  return ConvertFrom-Json -InputObject $text -ErrorAction Stop
}

function Get-ResumeImageKey($Path) {
  if ($Path -isnot [string] -or $Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Contains([char]0)) { throw 'Unknown valve image' }
  return [IO.Path]::GetFullPath($Path).TrimEnd('\').ToLowerInvariant()
}


function Read-ResumeSnapshot([string]$Path, [int]$Limit = 65536) {
  Assert-ResumePlainPath $Path
  try { [void][IO.File]::GetAttributes($Path) }
  catch [IO.FileNotFoundException] { return $null }
  catch [IO.DirectoryNotFoundException] { return $null }
  $before = [IO.FileInfo]::new($Path)
  $created = [string]$before.CreationTimeUtc.Ticks; $written = [string]$before.LastWriteTimeUtc.Ticks
  $bytes = Read-ResumeBytes $Path $Limit
  $after = [IO.FileInfo]::new($Path)
  if (-not $after.Exists -or [string]$after.CreationTimeUtc.Ticks -cne $created -or
      [string]$after.LastWriteTimeUtc.Ticks -cne $written -or $after.Length -ne $bytes.Length) { throw 'Resume record changed during read' }
  return [pscustomobject]@{ Path = $Path; Bytes = $bytes; Token = ($created + ':' + $written + ':' + $bytes.Length + ':' + [Convert]::ToBase64String($bytes)) }
}

function Test-ResumeSnapshot($Expected, $Actual) {
  if ($null -eq $Expected -or $null -eq $Actual) { return ($null -eq $Expected -and $null -eq $Actual) }
  return $Expected.Token -ceq $Actual.Token
}

function Read-ResumeMarker($Snapshot, [int]$TargetPid) {
  $text = ([Text.UTF8Encoding]::new($false, $true)).GetString($Snapshot.Bytes)
  $fields = @{}
  foreach ($part in $text.Split('|')) {
    if ($part -notmatch '^([A-Za-z][A-Za-z0-9]*)=(.+)$' -or $fields.ContainsKey($matches[1])) { throw 'Malformed or duplicate suspension marker' }
    $fields[$matches[1]] = $matches[2]
  }
  foreach ($key in $fields.Keys) {
    if ($key -cnotin @('pid','created','state','generation','epoch','origin','schemaVersion')) { throw 'Unknown suspension marker field' }
  }
  if ($fields['pid'] -cne [string]$TargetPid -or $fields['state'] -cne 'suspended' -or
      $fields['created'] -notmatch '^[1-9][0-9]{0,18}$') { throw 'Unknown marker ownership' }
  $created = 0L
  if (-not [long]::TryParse($fields['created'], [ref]$created) -or $created -le 0) { throw 'Unknown marker creation' }
  if ($fields.ContainsKey('schemaVersion') -and $fields['schemaVersion'] -cne '1') { throw 'Future marker schema' }
  $generation = 0L
  if ($fields.ContainsKey('generation') -and ($fields['generation'] -notmatch '^[1-9][0-9]{0,18}$' -or
      -not [long]::TryParse($fields['generation'], [ref]$generation))) { throw 'Unknown marker generation' }
  if ($fields.ContainsKey('epoch') -and $fields['epoch'] -notmatch '^[0-9]+(?:\.[0-9]+)?$') { throw 'Unknown marker epoch' }
  if ($fields.ContainsKey('origin') -and $fields['origin'] -cne 'explicit-wake-recovery') { throw 'Unknown marker origin' }
  return [pscustomobject]@{ Pid = $TargetPid; Created = $created; Generation = $generation }
}

function Read-ResumePending($Snapshot, $Marker) {
  if ($null -eq $Snapshot) { return $null }
  $value = ConvertFrom-ResumeJson $Snapshot.Bytes
  if (-not (Test-ResumeInteger $value.schemaVersion) -or $value.schemaVersion -ne 1 -or
      $value.phase -isnot [string] -or $value.phase -cne 'attempt-not-confirmed' -or
      -not (Test-ResumeInteger $value.pid) -or $value.pid -ne $Marker.Pid -or
      $value.processCreated -isnot [string] -or $value.processCreated -cne [string]$Marker.Created -or
      -not (Test-ResumeInteger $value.generation) -or $value.generation -le 0 -or $value.generation -ne $Marker.Generation) { throw 'Unknown or mismatched suspend attempt' }
  [void](Get-ResumeImageKey $value.path)
  return $value
}

function Write-ResumeNewFile([string]$Path, [byte[]]$Bytes, $Created = $null) {
  Assert-ResumePlainPath $Path
  $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
  if ($null -ne $Created) { $Created.Value = $true }
  try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) }
  finally { $stream.Dispose() }
  $saved = Read-ResumeSnapshot $Path 262144
  if ([Convert]::ToBase64String($saved.Bytes) -cne [Convert]::ToBase64String($Bytes)) { throw 'Resume record readback failed' }
  return $saved
}

function Remove-ResumeSnapshot($Snapshot, [int]$Limit = 65536) {
  if ($null -eq $Snapshot) { return }
  if (-not (Test-ResumeSnapshot $Snapshot (Read-ResumeSnapshot $Snapshot.Path $Limit))) { throw 'Changed resume record preserved' }
  [IO.File]::Delete($Snapshot.Path)
  if ($null -ne (Read-ResumeSnapshot $Snapshot.Path $Limit)) { throw 'Resume record retirement not confirmed' }
}

function Read-ResumeJournal($Snapshot, [int]$TargetPid) {
  $value = ConvertFrom-ResumeJson $Snapshot.Bytes
  $created = 0L; $operation = [guid]::Empty
  if (-not (Test-ResumeInteger $value.schemaVersion) -or $value.schemaVersion -ne 1 -or
      -not (Test-ResumeInteger $value.pid) -or $value.pid -ne $TargetPid -or
      $value.processCreated -isnot [string] -or $value.processCreated -notmatch '^[1-9][0-9]{0,18}$' -or
      -not [long]::TryParse($value.processCreated, [ref]$created) -or
      $value.operation -isnot [string] -or -not [guid]::TryParse($value.operation, [ref]$operation) -or $operation -eq [guid]::Empty -or
      $value.markerToken -isnot [string] -or [string]::IsNullOrEmpty($value.markerToken) -or
      $value.pendingToken -isnot [string] -or $value.markerPath -isnot [string] -or $value.pendingPath -isnot [string] -or $value.phase -isnot [string] -or
      $value.phase -cnotin @('attempt-not-confirmed','completed')) { throw 'Unknown resume journal' }
  if ($value.phase -ceq 'completed' -and (-not (Test-ResumeInteger $value.ntStatus) -or $value.ntStatus -ne 0)) { throw 'Unconfirmed resume completion' }
  return $value
}

function Complete-ResumeCleanup($JournalSnapshot, $Journal, [string]$MarkerPath, [string]$PendingPath) {
  if ($Journal.markerPath -cne [IO.Path]::GetFullPath($MarkerPath).ToLowerInvariant() -or
      $Journal.pendingPath -cne [IO.Path]::GetFullPath($PendingPath).ToLowerInvariant()) { throw 'Completion record path binding differs' }
  $marker = Read-ResumeSnapshot $MarkerPath; $pending = Read-ResumeSnapshot $PendingPath
  if (($marker -and $marker.Token -cne $Journal.markerToken) -or
      ($pending -and $pending.Token -cne $Journal.pendingToken) -or
      -not (Test-ResumeSnapshot $JournalSnapshot (Read-ResumeSnapshot $JournalSnapshot.Path 262144))) { throw 'Completion belongs to another record; preserved' }
  # A durable completed receipt permits only file cleanup. Never repeat NT resume.
  Remove-ResumeSnapshot $marker
  Remove-ResumeSnapshot $pending
  Remove-ResumeSnapshot $JournalSnapshot 262144
}

function Invoke-GameValveResumeAction([string]$Action, $Value) {
  switch ($Action) {
    'open' { return [YmGameValveResumeApi]::OpenProcess((0x0800 -bor 0x1000), $false, [int]$Value) }
    'created' { return [YmGameValveResumeApi]::ProcessCreated($Value) }
    'resume' { return [YmGameValveResumeApi]::NtResumeProcess($Value) }
    'close' { [void][YmGameValveResumeApi]::CloseHandle($Value); return }
    default { throw 'Unknown resume action' }
  }
}

function Invoke-GameValveResumeRecord([string]$Directory, [int]$TargetPid) {
  $handle = [IntPtr]::Zero; $lock = $null; $attempted = $false; $confirmed = $false
  $journalOwned = $false; $intentSnapshot = $null; $stage = $null
  $markerPath = Join-Path $Directory ("{0}.txt" -f $TargetPid)
  $pendingPath = Join-Path $Directory (".{0}.suspend-pending.json" -f $TargetPid)
  $journalPath = Join-Path $Directory (".{0}.resume.json" -f $TargetPid)
  try {
    Assert-ResumePlainPath $Directory
    $lockPath = Join-Path $Directory (".{0}.suspend.lock" -f $TargetPid)
    Assert-ResumePlainPath $lockPath
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $marker = Read-ResumeSnapshot $markerPath; $pending = Read-ResumeSnapshot $pendingPath
    $oldJournal = Read-ResumeSnapshot $journalPath 262144
    if ($oldJournal) {
      $saved = Read-ResumeJournal $oldJournal $TargetPid
      if ($saved.phase -cne 'completed') { throw 'Previous resume attempt is unconfirmed; preserve debt for explicit recovery' }
      Complete-ResumeCleanup $oldJournal $saved $markerPath $pendingPath
      return [pscustomobject]@{ Status = 'cleaned'; Pid = $TargetPid; ResumeAttempted = $false; NativeConfirmed = $true }
    }
    if (-not $marker) {
      if ($pending) { throw 'Suspend attempt has no confirmed marker; explicit identity and pause evidence required' }
      return [pscustomobject]@{ Status = 'inactive'; Pid = $TargetPid; ResumeAttempted = $false; NativeConfirmed = $false }
    }
    $identity = Read-ResumeMarker $marker $TargetPid
    $suspendAttempt = Read-ResumePending $pending $identity
    $process = Get-Process -Id $TargetPid -ErrorAction Stop
    if (-not $process -or [long]$process.StartTime.ToFileTimeUtc() -ne $identity.Created) { throw 'Marker process identity is unknown or changed' }
    if ($suspendAttempt -and (Get-ResumeImageKey $process.Path) -cne (Get-ResumeImageKey $suspendAttempt.path)) { throw 'Suspend attempt image changed' }
    $handle = Invoke-GameValveResumeAction 'open' $TargetPid
    if ($handle -eq [IntPtr]::Zero) { throw 'Resume handle unavailable' }
    if ((Invoke-GameValveResumeAction 'created' $handle) -ne $identity.Created) { throw 'Opened handle birth differs from marker' }
    if (-not (Test-ResumeSnapshot $marker (Read-ResumeSnapshot $markerPath)) -or
        -not (Test-ResumeSnapshot $pending (Read-ResumeSnapshot $pendingPath))) { throw 'Published suspension lease changed' }
    $utf8 = [Text.UTF8Encoding]::new($false)
    $intent = [ordered]@{ schemaVersion = 1; operation = [guid]::NewGuid().ToString('D'); phase = 'attempt-not-confirmed';
      pid = $TargetPid; processCreated = [string]$identity.Created; markerToken = $marker.Token; pendingToken = $(if ($pending) { $pending.Token } else { '' });
      markerPath = [IO.Path]::GetFullPath($markerPath).ToLowerInvariant(); pendingPath = [IO.Path]::GetFullPath($pendingPath).ToLowerInvariant() }
    $intentBytes = $utf8.GetBytes((ConvertTo-Json -InputObject $intent -Compress))
    $intentSnapshot = Write-ResumeNewFile $journalPath $intentBytes ([ref]$journalOwned)
    if (-not (Test-ResumeSnapshot $marker (Read-ResumeSnapshot $markerPath)) -or
        -not (Test-ResumeSnapshot $pending (Read-ResumeSnapshot $pendingPath)) -or
        -not (Test-ResumeSnapshot $intentSnapshot (Read-ResumeSnapshot $journalPath 262144)) -or
        (Invoke-GameValveResumeAction 'created' $handle) -ne $identity.Created) { throw 'Ownership changed before resume' }
    $attempted = $true
    $status = Invoke-GameValveResumeAction 'resume' $handle
    if ($status -ne 0) { throw ('Resume failed: NTSTATUS ' + $status) }
    $confirmed = $true
    $intent.phase = 'completed'; $intent['ntStatus'] = 0
    $completedBytes = $utf8.GetBytes((ConvertTo-Json -InputObject $intent -Compress))
    $stage = Join-Path $Directory ('.' + $TargetPid + '.resume-stage-' + [guid]::NewGuid().ToString('N'))
    [void](Write-ResumeNewFile $stage $completedBytes)
    if (-not (Test-ResumeSnapshot $intentSnapshot (Read-ResumeSnapshot $journalPath 262144))) { throw 'Resume debt changed before completion' }
    [IO.File]::Replace($stage, $journalPath, [NullString]::Value); $stage = $null
    $completedSnapshot = Read-ResumeSnapshot $journalPath 262144
    if ([Convert]::ToBase64String($completedSnapshot.Bytes) -cne [Convert]::ToBase64String($completedBytes)) { throw 'Completion readback failed' }
    Complete-ResumeCleanup $completedSnapshot $intent $markerPath $pendingPath
    return [pscustomobject]@{ Status = 'resumed'; Pid = $TargetPid; ResumeAttempted = $true; NativeConfirmed = $true }
  } catch {
    $message = $_.Exception.Message
    if (-not $attempted -and $journalOwned -and $intentSnapshot) { try { Remove-ResumeSnapshot $intentSnapshot 262144 } catch { } }
    return [pscustomobject]@{ Status = 'unknown'; Pid = $TargetPid; ResumeAttempted = $attempted; NativeConfirmed = $confirmed;
      Error = $message; MarkerPath = $markerPath; PendingPath = $pendingPath; JournalPath = $journalPath;
      Recovery = 'Preserve these records. Retry transient IO only; unconfirmed attempts require exact original-instance/pause or resume-completion evidence before manual repair. Never infer a target from pending alone.' }
  } finally {
    if ($handle -ne [IntPtr]::Zero) { try { Invoke-GameValveResumeAction 'close' $handle } catch { } }
    if ($stage) { try { [IO.File]::Delete($stage) } catch { } }
    if ($lock) { $lock.Dispose() }
  }
}

function Invoke-GameValveResumeAll {
  $records = [Collections.Generic.List[object]]::new()
  $plan = [Collections.Generic.Dictionary[int,object]]::new(); $enumerationKnown = $true
  foreach ($directory in $MARKER_DIRS) {
    try {
      Assert-ResumePlainPath $directory
      try { $attributes = [IO.File]::GetAttributes($directory) }
      catch [IO.FileNotFoundException] { continue }
      catch [IO.DirectoryNotFoundException] { continue }
      if (-not ($attributes -band [IO.FileAttributes]::Directory)) { throw 'Marker root is not a directory' }
      $selected = [Collections.Generic.HashSet[int]]::new(); $count = 0
      foreach ($file in [IO.Directory]::EnumerateFiles($directory)) {
        if (++$count -gt 4096) { throw 'Marker directory limit exceeded' }
        $name = [IO.Path]::GetFileName($file); $digits = $null
        if ($name -cmatch '^([1-9][0-9]{0,9})\.txt$') { $digits = $matches[1] }
        elseif ($name -cmatch '^\.([1-9][0-9]{0,9})\.(?:suspend-pending|resume)\.json$') { $digits = $matches[1] }
        elseif ($name.EndsWith('.txt', [StringComparison]::OrdinalIgnoreCase)) { $records.Add([pscustomobject]@{ Status = 'unknown'; Error = 'Unknown marker filename'; MarkerPath = $file }); continue }
        if ($digits) {
          $number = 0
          if (-not [int]::TryParse($digits, [ref]$number)) { throw 'Unknown marker PID' }
          [void]$selected.Add($number)
        }
      }
      foreach ($number in $selected) {
        if (-not $plan.ContainsKey($number)) { $plan[$number] = [Collections.Generic.List[string]]::new() }
        $plan[$number].Add($directory)
      }
    } catch { $enumerationKnown = $false; $records.Add([pscustomobject]@{ Status = 'unknown'; Error = $_.Exception.Message; MarkerPath = $directory }) }
  }
  foreach ($number in $plan.Keys) {
    if (-not $enumerationKnown -or $plan[$number].Count -ne 1) {
      $records.Add([pscustomobject]@{ Status = 'unknown'; Pid = $number; Error = 'Directory ownership or pause multiplicity is unknown; no resume attempted'; MarkerPath = ($plan[$number] -join '; ') })
    } else { $records.Add((Invoke-GameValveResumeRecord $plan[$number][0] $number)) }
  }
  return [pscustomobject]@{ Resumed = @($records | Where-Object { $_.Status -eq 'resumed' }).Count;
    Cleaned = @($records | Where-Object { $_.Status -eq 'cleaned' }).Count;
    Unknown = @($records | Where-Object { $_.Status -eq 'unknown' }).Count; Records = @($records) }
}

$result = Invoke-GameValveResumeAll
foreach ($record in $result.Records) {
  if ($record.Status -eq 'unknown') { Write-Host ("Unconfirmed suspension preserved: {0}; {1}" -f $record.MarkerPath, $record.Error) -ForegroundColor Yellow }
}
Write-Host ("Resumed: {0}; completed cleanup: {1}; unknown records preserved: {2}" -f $result.Resumed, $result.Cleaned, $result.Unknown)
if ($result.Unknown -gt 0) { exit 1 }
