param([string]$SourceRoot=$PSScriptRoot)
$ErrorActionPreference='Stop'
. (Join-Path $SourceRoot 'state.ps1')
$script:checks=0
function Assert($Ok,$Message) { $script:checks++;if (-not $Ok) { throw $Message } }
function Value($Exists,$Kind=$null,$Value=$null) { [pscustomobject]@{exists=$Exists;kind=$Kind;value=$Value} }
function Clone($Object) { $Object | ConvertTo-Json -Depth 12 | ConvertFrom-Json }
$old=Value $true 'String' 'Xbox!App';$off=Value $true 'DWord' 0
$our='YMCC!Home'
function New-State {
 [pscustomobject]@{saved=[pscustomobject]@{home=(Clone $old);startup=(Clone $off)};before=[pscustomobject]@{home=(Clone $old);startup=(Clone $off)};writes=[pscustomobject]@{home=$false;startup=$false};attempt=[pscustomobject]@{home=$false;startup=$false};aumid=$our;phase='prepared'}
}
$script:reg=@{};$script:writes=0;$script:failAt=0;$script:saves=0;$script:raceAt=0
$read={param($Name) $script:reg[$Name]}
$write={param($Name,$Value)
 $script:writes++
 if ($script:failAt -eq $script:writes) { throw 'synthetic-write-failure' }
 $script:reg[$Name]=Clone $Value
}
$save={param($State) $script:saves++;if ($script:raceAt -eq $script:saves) { $script:reg.GamingHomeApp=Value $true 'String' 'Other!Home' }}
function Reset { $script:reg=@{GamingHomeApp=(Clone $old);StartupToGamingHome=(Clone $off)};$script:writes=0;$script:failAt=0;$script:saves=0;$script:raceAt=0 }
foreach ($stage in @(0,1,2)) {
 Reset;$s=New-State;$script:failAt=$stage
 $failed=$false
 try { Invoke-GamingHomeSelection $s $read $write $save } catch { $failed=$true }
 Assert ($failed -eq ($stage -ne 0)) 'expected write failure'
 $script:failAt=0
 if ($stage -ne 0) {
  Restore-GamingHomeSelection $s $read $write -RollbackOnly | Out-Null
  Assert (Test-GamingHomeValue $script:reg.GamingHomeApp $old) 'failure restores/preserves original home'
  Assert (Test-GamingHomeValue $script:reg.StartupToGamingHome $off) 'failure restores/preserves original startup'
 } else {
  Assert ($s.phase -eq 'enabled') 'selection committed'
  Assert ($script:reg.GamingHomeApp.value -eq $our) 'ours selected'
  Assert ($s.writes.home -and $s.writes.startup) 'durable journal records both writes'
  $plan=Restore-GamingHomeSelection $s $read $write
  Assert ($plan.homeRestored -and $plan.startupRestored) 'explicit restore owns both values'
  Assert (Test-GamingHomeValue $script:reg.GamingHomeApp $old) 'old Xbox home restored'
  Assert (Test-GamingHomeValue $script:reg.StartupToGamingHome $off) 'old boot flag restored'
 }
}
Reset;$s=New-State;Invoke-GamingHomeSelection $s $read $write $save
$script:reg.GamingHomeApp=Value $true 'String' 'Other!Home';$before=$script:writes
Restore-GamingHomeSelection $s $read $write | Out-Null
Assert ($script:writes -eq $before) 'later other home and its startup preserved'
Reset;$s=New-State;Invoke-GamingHomeSelection $s $read $write $save
$script:reg.StartupToGamingHome=Value $true 'DWord' 0
Restore-GamingHomeSelection $s $read $write | Out-Null
Assert ($script:reg.GamingHomeApp.value -eq $old.value) 'restore owns home'
Assert ($script:reg.StartupToGamingHome.value -eq 0) 'later startup OFF preserved'
Reset;$s=New-State;$script:reg.GamingHomeApp=Value $true 'String' 'Other!Home';$failed=$false
try { Invoke-GamingHomeSelection $s $read $write $save } catch { $failed=$true }
Assert ($failed -and $script:writes -eq 0) 'precommit selection race has no effects'
Reset;$s=New-State;$script:raceAt=2;$failed=$false
try { Invoke-GamingHomeSelection $s $read $write $save } catch { $failed=$true }
Assert $failed 'midcommit selection race is detected'
Restore-GamingHomeSelection $s $read $write -RollbackOnly | Out-Null
Assert ($script:reg.GamingHomeApp.value -eq 'Other!Home') 'midcommit preserves other home'
# Idempotent enable then an unrelated early failure must not undo a valid home.
Reset;$s=New-State;Invoke-GamingHomeSelection $s $read $write $save
$s.before=[pscustomobject]@{home=(Clone $script:reg.GamingHomeApp);startup=(Clone $script:reg.StartupToGamingHome)}
$s.attempt=[pscustomobject]@{home=$false;startup=$false};$before=$script:writes
Restore-GamingHomeSelection $s $read $write -RollbackOnly | Out-Null
Assert ($script:writes -eq $before -and $script:reg.GamingHomeApp.value -eq $our) 'early retry failure preserves earlier registration'
Invoke-GamingHomeSelection $s $read $write $save
Assert ($script:writes -eq $before) 'idempotent commit writes nothing'
Restore-GamingHomeSelection $s $read $write | Out-Null
Assert (Test-GamingHomeValue $script:reg.GamingHomeApp $old) 'original backup survives retry'
# Missing old values restore by deletion, not empty strings or synthesized defaults.
Reset;$s=New-State;$s.saved.home=Value $false;$s.before.home=Value $false;$script:reg.GamingHomeApp=Value $false
$s.saved.startup=Value $false;$s.before.startup=Value $false;$script:reg.StartupToGamingHome=Value $false
Invoke-GamingHomeSelection $s $read $write $save;Restore-GamingHomeSelection $s $read $write | Out-Null
Assert (-not $script:reg.GamingHomeApp.exists -and -not $script:reg.StartupToGamingHome.exists) 'absent originals restored as absent'
$task=[pscustomobject]@{Description='YMCC Gaming Home demand start v1';Actions=@([pscustomobject]@{Execute='C:\SOFT\YeMan\YeManCC\YeManCC.exe';Arguments='--minimized';WorkingDirectory='C:\SOFT\YeMan\YeManCC'});Principal=[pscustomobject]@{UserId='S-1-5-21-100-200-300-1001';RunLevel='Highest';LogonType='Interactive'};Triggers=$null;Settings=[pscustomobject]@{MultipleInstances='IgnoreNew';DisallowStartIfOnBatteries=$false;StopIfGoingOnBatteries=$false}}
$ymcc='C:\SOFT\YeMan\YeManCC\YeManCC.exe';$sid='S-1-5-21-100-200-300-1001'
Assert (Test-GamingHomeTask $task $ymcc $sid) 'valid demand task'
foreach ($fault in @('description','path','arguments','directory','user','level','logon','trigger','actions','battery-start','battery-stop','instances')) {
 $t=Clone $task
 switch ($fault) {
  description {$t.Description='Other'}
  path {$t.Actions[0].Execute='C:\Other.exe'}
  arguments {$t.Actions[0].Arguments='--dev injected'}
  directory {$t.Actions[0].WorkingDirectory='C:\Other'}
  user {$t.Principal.UserId='S-1-5-18'}
  level {$t.Principal.RunLevel='Limited'}
  logon {$t.Principal.LogonType='ServiceAccount'}
  trigger {$t.Triggers=@([pscustomobject]@{Type='Logon'})}
  actions {$t.Actions+=Clone $t.Actions[0]}
  battery-start {$t.Settings.DisallowStartIfOnBatteries=$true}
  battery-stop {$t.Settings.StopIfGoingOnBatteries=$true}
  instances {$t.Settings.MultipleInstances='Parallel'}
 }
 Assert (-not (Test-GamingHomeTask $t $ymcc $sid)) ('reject wrong task '+$fault)
}
# Parse shipping scripts without invoking setup, registry or scheduled tasks.
foreach ($f in @('Manage-GamingHome.ps1','state.ps1')) {
 $tokens=$null;$errors=$null
 [Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceRoot $f),[ref]$tokens,[ref]$errors) | Out-Null
 Assert ($errors.Count -eq 0) ('shipping script parses '+$f)
}
[ordered]@{status='PASS';checks=$script:checks;registry='memory-only';tasks='memory-only';installedApplicationExecuted=$false} | ConvertTo-Json
