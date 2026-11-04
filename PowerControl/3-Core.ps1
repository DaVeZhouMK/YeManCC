# 确保以管理员身份运行
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# 游戏识别阀门是唯一的进程选择器；这里只执行既定的 CPU 掩码。
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
      Name = $process.ProcessName; Snapshot = [Convert]::ToBase64String($bytes); Process = $process }
  } catch { return $null }
}


$gate = Join-Path $PSScriptRoot 'game-target.json'
$admission = Read-GameValveTarget
$target = if ($null -ne $admission) { $admission.Process } else { $null }

if ($target) {
    Write-Host "Adjusting CPU affinity for valve PID: $($target.Id)"

    # 设置 CPU 亲和性为12核
    # 计算对应的二进制掩码
    $AffinityMask = 0xFFF # 0xFFF = 111111111111 (二进制)
    $target.ProcessorAffinity = $AffinityMask
} else {
    Write-Host "No current game admitted by the native game valve."
}



