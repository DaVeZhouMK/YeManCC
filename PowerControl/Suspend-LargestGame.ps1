# Compatibility entry point for the native game recognition valve.
# The native valve selects exactly one PID; this script only revalidates and
# suspends that published PID. It must never perform a second process scan or
# rebuild a target from a name/working-set heuristic.
$ErrorActionPreference = 'Stop'
$DIR = $PSScriptRoot
$GATE = Join-Path $DIR 'game-target.json'
$MARKER_DIR = Join-Path $DIR 'Sleep\manual-suspended'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YmGameValveNtApi {
    [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
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

function Test-GameValveInteger($Value) {
  return ($Value -is [int] -or $Value -is [long])
}

function Assert-GameValvePlainPath([string]$Path) {
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

function Read-GameValveBytes([string]$Path, [int]$Limit = 65536) {
  Assert-GameValvePlainPath $Path
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

function ConvertFrom-GameValveJson([byte[]]$Bytes) {
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

function Get-GameValveImageKey($Path) {
  if ($Path -isnot [string] -or $Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Contains([char]0)) { throw 'Unknown valve image' }
  return [IO.Path]::GetFullPath($Path).TrimEnd('\').ToLowerInvariant()
}

function Read-GameValveTarget {
  try {
    $bytes = Read-GameValveBytes $GATE
    $target = ConvertFrom-GameValveJson $bytes
    if ($target.valid -isnot [bool] -or -not $target.valid -or
        -not (Test-GameValveInteger $target.pid) -or $target.pid -le 0 -or $target.pid -gt [int]::MaxValue -or
        -not (Test-GameValveInteger $target.generation) -or $target.generation -le 0 -or
        -not (Test-GameValveInteger $target.lastSeen) -or $target.lastSeen -le 0) { return $null }
    if ($target.PSObject.Properties['schemaVersion'] -and
        (-not (Test-GameValveInteger $target.schemaVersion) -or $target.schemaVersion -ne 1)) { return $null }
    if ($target.identityPolicy -isnot [string] -or $target.identityPolicy -cne 'ymcc-original-game-identity-v2') { return $null }
    $created = 0L
    if ($target.processCreated -isnot [string] -or $target.processCreated -notmatch '^[1-9][0-9]{0,18}$' -or
        -not [long]::TryParse($target.processCreated, [ref]$created) -or $created -le 0) { return $null }
    $image = Get-GameValveImageKey $target.path
    $age = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $target.lastSeen
    if ($age -lt 0 -or $age -gt 10000) { return $null }
    $process = Get-Process -Id ([int]$target.pid) -ErrorAction Stop
    if (-not $process -or [long]$process.StartTime.ToFileTimeUtc() -ne $created -or
        (Get-GameValveImageKey $process.Path) -cne $image) { return $null }
    $again = Read-GameValveBytes $GATE
    if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String($again)) { return $null }
    $age = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $target.lastSeen
    if ($age -lt 0 -or $age -gt 10000) { return $null }
    return [pscustomobject]@{ Pid = [int]$target.pid; Created = $created; Path = $image; Generation = $target.generation;
      Name = $process.ProcessName; Snapshot = [Convert]::ToBase64String($bytes) }
  } catch { return $null }
}

function Test-GameValveFilePresent([string]$Path) {
  # Only conclusive absence is missing. Access and type failures stay unknown.
  Assert-GameValvePlainPath $Path
  try { [void][IO.File]::GetAttributes($Path); return $true }
  catch [IO.FileNotFoundException] { return $false }
  catch [IO.DirectoryNotFoundException] { return $false }
}

function Write-GameValveNewFile([string]$Path, [byte[]]$Bytes, $Created = $null) {
  $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
  if ($Created) { $Created.Value = $true }
  try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) }
  finally { $stream.Dispose() }
  if ([Convert]::ToBase64String((Read-GameValveBytes $Path)) -cne [Convert]::ToBase64String($Bytes)) { throw 'Valve write readback failed' }
}

function Invoke-GameValveAction([string]$Action, $Value) {
  switch ($Action) {
    'open' { return [YmGameValveNtApi]::OpenProcess((0x0800 -bor 0x1000), $false, [int]$Value) }
    'created' { return [YmGameValveNtApi]::ProcessCreated($Value) }
    'suspend' { return [YmGameValveNtApi]::NtSuspendProcess($Value) }
    'close' { [void][YmGameValveNtApi]::CloseHandle($Value); return }
    default { throw 'Unknown valve action' }
  }
}

function Invoke-GameValveSuspend {
  $handle = [IntPtr]::Zero; $lock = $null; $stage = $null; $attempted = $false
  $pending = $null; $pendingOwned = $false; $intentBytes = $null
  try {
    $target = Read-GameValveTarget
    if ($null -eq $target) { return [pscustomobject]@{ Status = 'inactive'; MarkerPersisted = $false; Attempted = $false } }
    Assert-GameValvePlainPath $MARKER_DIR
    [IO.Directory]::CreateDirectory($MARKER_DIR) | Out-Null
    $marker = Join-Path $MARKER_DIR ("{0}.txt" -f $target.Pid)
    $pending = Join-Path $MARKER_DIR (".{0}.suspend-pending.json" -f $target.Pid)
    $lockPath = Join-Path $MARKER_DIR (".{0}.suspend.lock" -f $target.Pid)
    Assert-GameValvePlainPath $lockPath
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    # Existing records are possible pause debt, even when empty or malformed.
    # Never infer ownership/gone/resumed from a parse failure or a PID alone.
    if ((Test-GameValveFilePresent $marker) -or (Test-GameValveFilePresent $pending)) { throw 'Existing suspension record requires explicit ownership recovery' }
    $handle = Invoke-GameValveAction 'open' $target.Pid
    if ($handle -eq [IntPtr]::Zero) { throw 'Valve PID open unavailable' }
    if ((Invoke-GameValveAction 'created' $handle) -ne $target.Created) { throw 'Valve PID identity changed' }
    $utf8 = [Text.UTF8Encoding]::new($false)
    $stage = Join-Path $MARKER_DIR ('.' + $target.Pid + '.suspend-stage-' + [guid]::NewGuid().ToString('N'))
    $markerBytes = $utf8.GetBytes("pid=$($target.Pid)|created=$($target.Created)|generation=$($target.Generation)|state=suspended")
    Write-GameValveNewFile $stage $markerBytes
    $current = Read-GameValveTarget
    if ($null -eq $current -or $current.Snapshot -cne $target.Snapshot -or
        (Test-GameValveFilePresent $marker) -or (Test-GameValveFilePresent $pending)) { throw 'Valve selection or suspension ownership changed' }
    # An intent is not a success marker. Keep it after an attempted action or
    # interrupted publish so a later invocation cannot blindly suspend twice.
    $intent = @{ schemaVersion = 1; phase = 'attempt-not-confirmed'; pid = $target.Pid; processCreated = [string]$target.Created;
      generation = $target.Generation; path = $target.Path }
    $intentBytes = $utf8.GetBytes((ConvertTo-Json -InputObject $intent -Compress))
    Write-GameValveNewFile $pending $intentBytes ([ref]$pendingOwned)
    $current = Read-GameValveTarget
    if ($null -eq $current -or $current.Snapshot -cne $target.Snapshot -or
        (Invoke-GameValveAction 'created' $handle) -ne $target.Created -or (Test-GameValveFilePresent $marker)) { throw 'Valve changed before suspend attempt' }
    $attempted = $true
    $status = Invoke-GameValveAction 'suspend' $handle
    if ($status -ne 0) { throw ('Suspend failed: NTSTATUS ' + $status) }
    # Same-volume move refuses an existing destination; no replace/delete of
    # another writer's marker. The pending record survives any failed receipt.
    [IO.File]::Move($stage, $marker); $stage = $null
    if ([Convert]::ToBase64String((Read-GameValveBytes $marker)) -cne [Convert]::ToBase64String($markerBytes)) { throw 'Suspension marker readback failed' }
    if ([Convert]::ToBase64String((Read-GameValveBytes $pending)) -cne [Convert]::ToBase64String($intentBytes)) { throw 'Pending suspension debt changed' }
    [IO.File]::Delete($pending)
    return [pscustomobject]@{ Status = 'suspended'; MarkerPersisted = $true; Attempted = $true; Pid = $target.Pid; Name = $target.Name }
  } catch {
    $failure = $_.Exception.Message
    # Only a newly created, unchanged intent can be retired when this call has
    # made no suspend attempt. Unknown/changed/partial debt always remains.
    if (-not $attempted -and $pendingOwned) {
      try {
        if ([Convert]::ToBase64String((Read-GameValveBytes $pending)) -ceq [Convert]::ToBase64String($intentBytes)) { [IO.File]::Delete($pending) }
      } catch { }
    }
    return [pscustomobject]@{ Status = 'unknown'; MarkerPersisted = $false; Attempted = $attempted; Error = $failure }
  } finally {
    if ($handle -ne [IntPtr]::Zero) { try { Invoke-GameValveAction 'close' $handle } catch { } }
    if ($stage) { try { [IO.File]::Delete($stage) } catch { } }
    if ($lock) { $lock.Dispose() }
  }
}

$receipt = Invoke-GameValveSuspend
if ($receipt.Status -eq 'suspended') {
  Write-Host ("Suspended valve target: {0} (PID {1})" -f $receipt.Name, $receipt.Pid) -ForegroundColor Green
} elseif ($receipt.Status -eq 'inactive') {
  Write-Host 'No current game admitted by the native game valve.' -ForegroundColor Yellow
} else {
  Write-Host ("Suspension state is unknown; records preserved: {0}" -f $receipt.Error) -ForegroundColor Yellow
  exit 1
}
