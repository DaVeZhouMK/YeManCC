param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\steam-appearance.ps1') -Action Library
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:ThemeRoot=Join-Path $Out ('profile-'+[guid]::NewGuid().ToString('N'))
$script:PresetStateBase=Join-Path $Out 'states'
$script:Live=$false;$script:Reloads=0;$script:Mode='success';$script:Rows=@();$script:Stopped=0
$script:AppearanceFixtureMutexPrefix='Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.'
function New-AppearanceMutex([string]$Purpose) {
 if($Purpose -notin @('Launch','ThemeControl')){throw 'APPEARANCE_FIXTURE_MUTEX_PURPOSE_DENIED'}
 New-Object Threading.Mutex($false,($script:AppearanceFixtureMutexPrefix+$Purpose))
}
function Get-BackendProcesses {if($script:Live){[pscustomobject]@{Id=42;Path=$script:BackendExe}}}
function Test-BackendOwner {$script:Live}
function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1){throw "fixture instance identity"}}
function Test-CssDesktopEditing {$true}
function Stop-VerifiedIdleProcesses {$script:Stopped++;throw 'No backend termination allowed in this fixture'}
function Clear-OldLoaderStyles {$script:Stopped++;throw 'No direct CEF style deletion allowed in this fixture'}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}) {
 switch($Method){
  fetch_theme_path {return $script:ThemeRoot}
  get_themes {return $script:Rows}
  reset {
   $script:Reloads++
   if($script:Mode -eq 'errors'){return [pscustomobject]@{fails=@('fixture load failure')}}
   if($script:Mode -eq 'malformed'){return [pscustomobject]@{unexpected=$true}}
   foreach($row in Get-ThemeRows){Write-ConfigAtomic $row.path ($row.config|ConvertTo-Json -Depth 64 -Compress)}
   if($script:Mode -eq 'edited'){$path=Join-Path $script:ThemeRoot 'Round\config_USER.json';$value=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;$value.active=!$value.active;Write-ConfigAtomic $path ($value|ConvertTo-Json -Depth 64 -Compress)}
   $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
   if($script:Mode -eq 'missing'){$script:Rows=@($script:Rows|Where-Object name -ne 'YMCC Game Details')}
   return [pscustomobject]@{fails=@()}
  }
  set_theme_state {($script:Rows|Where-Object name -eq $Arguments.name).enabled=$Arguments.state;return [pscustomobject]@{success=$true}}
  default {throw ('Unexpected fixture RPC '+$Method)}
 }
}
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,[string]$Label){if(!$Value){throw $Label};$cases.Add($Label)}
function Rejected([scriptblock]$Action){try{& $Action|Out-Null;return $false}catch{return $true}}
Invoke-PresetPrepare|Out-Null
$script:Live=$true;$script:Rows=@(Get-ThemeRows|Where-Object name -ne 'YMCC Game Details'|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
$before=Get-AppearanceStatus
Assert ($before.backendReady -and $before.backendReloadRequired -and ($before.themes|Where-Object name -eq 'YMCC Game Details').backendEnabled -eq $null) 'Reproduce running seven-theme backend while the eighth theme already exists and is active on disk'
$hashes=@{};foreach($row in Get-ThemeRows){$hashes[$row.name]=Get-AppearanceConfigValue $row.config}
$result=Invoke-AppearanceReload ([pscustomobject]@{targetId=$before.targetId;expectedRevision=$before.revision})
Assert ($result.ok -and !$result.started -and $result.reloaded -and $result.snapshot.configuredMatchesBackend -and !$result.snapshot.backendReloadRequired) 'In-place reload discovers and confirms all eight themes without a helper or restart'
Assert ((Test-CssDesktopEditing) -and $script:Stopped -eq 0 -and $script:Reloads -eq 1) 'An open Desktop is preserved and no direct style cleanup or backend termination occurs'
Assert (@($result.snapshot.themes|Where-Object {$_.configured -and $_.backendEnabled}).Count -eq 8) 'The resulting eight-of-eight state matches disk and backend'
foreach($row in Get-ThemeRows){Assert ((Get-AppearanceConfigValue $row.config) -ceq $hashes[$row.name]) ('Exact configuration values preserved: '+$row.name)}
Assert (Rejected {Invoke-AppearanceReload ([pscustomobject]@{targetId=$before.targetId;expectedRevision='0'*64})}) 'Stale revision cannot trigger a backend reload'
Assert (Rejected {Invoke-AppearanceReload ([pscustomobject]@{targetId='0'*16;expectedRevision=$before.revision})}) 'A different theme directory cannot trigger a backend reload'
Assert ($script:Reloads -eq 1) 'Rejected requests do not call the backend'
foreach($mode in @('errors','malformed','missing','edited')){
 $state=Read-PresetState;$state.pendingReload=$true;Save-PresetState $state;$script:Mode=$mode
 Assert (Rejected {Invoke-AppearanceReload}) ('Reload rejects '+$mode+' without declaring success')
 Assert ((Read-PresetState).pendingReload -eq $true) ('Unconfirmed '+$mode+' keeps the reload requirement')
}
$script:Mode='success';Invoke-PresetPrepare $true|Out-Null;$started=Start-AppearanceBackend
Assert ($started.ok -and !$started.started -and $script:Stopped -eq 0) 'Ensure can reuse the running editor-owned backend for a pending resource reload'
$script:Live=$false;$count=$script:Reloads
Assert (Rejected {Invoke-AppearanceReload}) 'Reload does not create a missing backend'
Assert ($script:Reloads -eq $count) 'Missing backend rejection makes no reset call'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0;desktopPreserved=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) in-place appearance reload checks"
