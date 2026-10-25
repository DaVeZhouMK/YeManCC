function Test-GamingHomeValue($Left,$Right) {
  if ([bool]$Left.exists -ne [bool]$Right.exists) { return $false }
  return (-not $Left.exists -or ($Left.kind -ceq $Right.kind -and [string]$Left.value -ceq [string]$Right.value))
}
function Get-GamingHomeRestorePlan($CurrentHome,$CurrentStartup,$Saved,$Aumid,$Writes) {
  $ownsHome = $Aumid -and $CurrentHome.exists -and $CurrentHome.kind -eq 'String' -and $CurrentHome.value -ceq $Aumid
  $priorHome = Test-GamingHomeValue $CurrentHome $Saved.home
  [pscustomobject]@{
    RestoreHome = [bool]($Writes.home -and $ownsHome)
    # Startup can have been committed just before a failing home write. Only
    # restore it if it is still ours AND the home is ours or the original home.
    RestoreStartup = [bool]($Writes.startup -and ($ownsHome -or $priorHome) -and $CurrentStartup.exists -and $CurrentStartup.kind -eq 'DWord' -and [int]$CurrentStartup.value -eq 1)
    Home = $Saved.home
    Startup = $Saved.startup
  }
}
function Invoke-GamingHomeSelection($State,[scriptblock]$Read,[scriptblock]$Write,[scriptblock]$Save) {
  $intendedHome=[pscustomobject]@{exists=$true;kind='String';value=$State.aumid}
  $intendedStartup=[pscustomobject]@{exists=$true;kind='DWord';value=1}
  # Do not change a selection made by another setup/user after our preflight.
  $previousHome=& $Read 'GamingHomeApp';$startup=& $Read 'StartupToGamingHome'
  if (-not (Test-GamingHomeValue $previousHome $State.before.home) -or -not (Test-GamingHomeValue $startup $State.before.startup)) { throw 'Gaming home selection changed during setup; preserved.' }
  $State.attempt=[pscustomobject]@{home=$false;startup=$false}
  $State.phase='committing';& $Save $State
  if (-not (Test-GamingHomeValue $startup $intendedStartup)) {
    $State.writes.startup=$true;$State.attempt.startup=$true;& $Save $State
    & $Write 'StartupToGamingHome' $intendedStartup
  }
  # Recheck the exact previous value after committing startup, before home.
  if (-not (Test-GamingHomeValue (& $Read 'GamingHomeApp') $previousHome)) { throw 'GamingHomeApp changed during setup; preserved.' }
  if (-not (Test-GamingHomeValue $previousHome $intendedHome)) {
    $State.writes.home=$true;$State.attempt.home=$true;& $Save $State
    & $Write 'GamingHomeApp' $intendedHome
  }
  if (-not (Test-GamingHomeValue (& $Read 'GamingHomeApp') $intendedHome) -or -not (Test-GamingHomeValue (& $Read 'StartupToGamingHome') $intendedStartup)) { throw 'Gaming home readback mismatch.' }
  $State.phase='enabled';& $Save $State
}
function Restore-GamingHomeSelection($State,[scriptblock]$Read,[scriptblock]$Write,[switch]$RollbackOnly) {
  $saved=$State.saved;$writes=$State.writes
  if ($RollbackOnly) { $saved=$State.before;$writes=$State.attempt }
  $plan=Get-GamingHomeRestorePlan (& $Read 'GamingHomeApp') (& $Read 'StartupToGamingHome') $saved $State.aumid $writes
  if ($plan.RestoreStartup) { & $Write 'StartupToGamingHome' $plan.Startup }
  # Re-evaluate after the first write so another owner's new home is preserved.
  $next=Get-GamingHomeRestorePlan (& $Read 'GamingHomeApp') (& $Read 'StartupToGamingHome') $saved $State.aumid $writes
  if ($next.RestoreHome) { & $Write 'GamingHomeApp' $next.Home }
  [pscustomobject]@{homeRestored=$next.RestoreHome;startupRestored=$plan.RestoreStartup}
}
function Test-GamingHomeTask($Task,$Ymcc,$Sid) {
  if (-not $Task) { return $false }
  $actions=@($Task.Actions | Where-Object { $null -ne $_ })
  $triggers=@($Task.Triggers | Where-Object { $null -ne $_ })
  $user=$Task.Principal.UserId
  if ($user -and $user -notmatch '^S-1-') {
    try { $user=([Security.Principal.NTAccount]$user).Translate([Security.Principal.SecurityIdentifier]).Value } catch { return $false }
  }
  return ($Task.Description -ceq 'YMCC Gaming Home demand start v1' -and $actions.Count -eq 1 -and
    $actions[0].Execute -ieq $Ymcc -and $actions[0].Arguments -ceq '--minimized' -and
    $actions[0].WorkingDirectory -ieq 'C:\SOFT\YeMan\YeManCC' -and
    $user -eq $Sid -and [string]$Task.Principal.RunLevel -eq 'Highest' -and
    [string]$Task.Principal.LogonType -eq 'Interactive' -and $triggers.Count -eq 0 -and
    [string]$Task.Settings.MultipleInstances -eq 'IgnoreNew' -and
    -not $Task.Settings.DisallowStartIfOnBatteries -and -not $Task.Settings.StopIfGoingOnBatteries)
}
