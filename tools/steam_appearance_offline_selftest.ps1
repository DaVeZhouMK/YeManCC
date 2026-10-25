param([string]$Out='G:\YeManCC-Work\_scratch\steam-appearance-unlock-20261011\offline')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
# No live process or listener lookups in an offline fixture.
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. (Join-Path $root 'PowerControl\steam-appearance.ps1') -Action Library
$liveFiles=@((Read-PresetManifest).files | ForEach-Object {$file=Join-Path (Join-Path $env:USERPROFILE 'homebrew\themes') $_.path;if(Test-Path -LiteralPath $file -PathType Leaf){@{path=$file;hash=(Get-ConfigHash $file)}}})
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:ThemeRoot=Join-Path $Out ('profile 中文 with spaces-'+[guid]::NewGuid().ToString('N'))
$script:PresetStateBase=Join-Path $Out 'states'
New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
function Get-BackendProcesses {@()}
function Test-BackendOwner {$false}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}) {throw ('No live backend operations permitted in fixture: '+$Method)}
$checks=New-Object Collections.Generic.List[string]
function Assert($Value,[string]$Label){if(!$Value){throw $Label};$checks.Add($Label);Write-Output ('PASS '+$Label)}
function Rejected([scriptblock]$Work){try{& $Work|Out-Null;return $false}catch{return $true}}
$prepared=Invoke-PresetPrepare
$status=$prepared.snapshot
Assert ($prepared.ok -and $status.packPrepared -and $status.backendReloadRequired -and !$status.backendReady) 'New offline user has complete default preset and a pending backend reload'
Assert (!(Rejected {Assert-AppearanceApplyReady $status})) 'Actual production Apply admission does not lock switches behind absent CSSLoader'
$parameters=@{}
foreach($row in Get-ThemeRows){$copy=$row.config|ConvertTo-Json -Depth 32|ConvertFrom-Json;$copy.PSObject.Properties.Remove('active');$parameters[$row.name]=$copy|ConvertTo-Json -Depth 32 -Compress}
$off=[pscustomobject]@{expectedRevision=$status.revision;states=[pscustomobject]@{'Round'=$false}}
$result=Invoke-AppearanceApply $off
Assert ($result.ok -and $result.saved -and $result.snapshot.backendReloadRequired) 'Switch choice is durably saved while backend reload remains pending'
Assert (!(($result.snapshot.themes|Where-Object name -eq 'Round').configured)) 'Offline Round switch can be turned off instead of being locked'
Assert ((Read-PresetState).pendingReload) 'Saving switches cannot falsely acknowledge that backend reloaded'
$allOn=@{};foreach($name in $script:ThemeNames){$allOn[$name]=$true}
$status=Get-AppearanceStatus;Assert-AppearanceApplyReady $status
$result=Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=[pscustomobject]$allOn})
Assert ($result.ok -and @($result.snapshot.themes|Where-Object {!$_.configured}).Count -eq 0) 'Default master can enable all seven themes before Steam/CSS startup'
foreach($row in Get-ThemeRows){$copy=$row.config|ConvertTo-Json -Depth 32|ConvertFrom-Json;$copy.PSObject.Properties.Remove('active');Assert (($copy|ConvertTo-Json -Depth 32 -Compress) -eq $parameters[$row.name]) ('Confirmed preset parameters unchanged: '+$row.name)}
$ready=$result.snapshot|ConvertTo-Json -Depth 12|ConvertFrom-Json;$ready.backendReady=$true
Assert (Rejected {Assert-AppearanceApplyReady $ready}) 'Live stale backend is still blocked from mismatched RPC writes until verified reload'
foreach($case in @('packPrepared','packRepairRequired','foreignActiveCount')){
 $bad=$result.snapshot|ConvertTo-Json -Depth 12|ConvertFrom-Json
 if($case -eq 'packPrepared'){$bad.packPrepared=$false}elseif($case -eq 'packRepairRequired'){$bad.packRepairRequired=$true}else{$bad.foreignActiveCount=1}
 Assert (Rejected {Assert-AppearanceApplyReady $bad}) ('Real resource/conflict safety gate remains active: '+$case)
}
Assert ((Get-ThemeRows|Where-Object name -eq 'Round').configured) 'Rejected admission never alters previously saved switch bytes'
foreach($entry in $liveFiles){Assert ((Get-ConfigHash $entry.path) -eq $entry.hash) ('Live user file unchanged by isolated tests: '+$entry.path)}
[pscustomobject]@{ok=$true;scope='Isolated second-user profile and mock backend; actual live themes read-only';checks=$checks.ToArray();actualLoaderLaunches=0;actualSteamOperations=0}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Out 'offline-result.json') -Encoding utf8
Write-Output "STEAM_APPEARANCE_OFFLINE_OK tests=$($checks.Count)"
