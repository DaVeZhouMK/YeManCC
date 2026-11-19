[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[ValidateSet('All','Known','Discovered','Late','Force')][string]$Case='All')
$ErrorActionPreference='Stop'
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot (Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl') -EvidenceRoot $Out
$SessionPid=123;$SessionBirth='456'
$script:RetiredThemeNames=@('Game Cover Reflections')
function Get-AppearanceSteamSession {[pscustomobject]@{pid=$SessionPid;birth=$SessionBirth;root='C:\fixture-steam'}}
function Get-BackendProcesses {[pscustomobject]@{Id=42}}
function Get-AppearanceTcpListeners {param($LocalPort) @()}
function Invoke-LoaderRpc([string]$Method,$Arguments){
 $script:Rpc+=@($Method)
 switch($Method){
  fetch_theme_path {return $script:ActualRoot}
  get_themes {return $script:ThemeNames|ForEach-Object{[pscustomobject]@{name=$_;enabled=$true}}}
  default {throw "Unexpected write: $Method"}
 }
}
function Test-CssDesktopEditing {$script:EditorReads++;$script:Editing}
$initialBackup=${function:New-PresetBackup}
function New-PresetBackup($Paths){
 $outcome=& $initialBackup $Paths
 if($scenario -eq 'Late' -and $script:ArmLateEditor){$script:Editing=$true}
 $outcome
}
$cases=[Collections.Generic.List[string]]::new()
function Assert($Condition,[string]$Label){if(!$Condition){throw $Label};$cases.Add($Label)}
function Run-Recovery {
 if($scenario -eq 'Force'){Recover-PresetTransaction -Force}
 else{Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null}
}
foreach($scenario in @('Known','Discovered','Late','Force') | Where-Object {$Case -eq 'All' -or $_ -eq $Case}){
 $script:ThemeRoot=Join-Path $Out "$scenario/homebrew/themes"
 $script:PresetStateBase=Join-Path $Out "$scenario/state"
 $script:ActualRoot=$script:ThemeRoot
 $script:PresetTransactionActive=$false;$script:Editing=$false;$script:ArmLateEditor=$false;$script:Rpc=@();$script:EditorReads=0
 Invoke-PresetPrepare|Out-Null
 Complete-PresetReload
 $owner=Join-Path (Get-PresetStateDir) 'owner.json';$ownerHash=Get-ConfigHash $owner
 $config=Join-Path $script:ThemeRoot 'Round/config_USER.json';$before=Get-ConfigHash $config
 $backup=New-PresetBackup @($config)
 Begin-PresetTransaction $backup
 $original=Get-Content -LiteralPath $config -Raw|ConvertFrom-Json
 $original.Intensity='25px';Write-ThemeConfigAtomic $config ($original|ConvertTo-Json -Compress)
 $edited=Get-ConfigHash $config
 if($edited -eq $before){throw 'Unchanged fixture configuration'}
 $marker=Join-Path (Get-PresetStateDir) 'transaction.json';$journalHash=Get-ConfigHash $marker
 $hintPath=Get-PresetTargetHintPath
 if($scenario -eq 'Discovered'){
  $hint=Get-Content -LiteralPath $hintPath -Raw|ConvertFrom-Json
  $hint.themeRoot=Join-Path $Out 'discovered-old/homebrew/themes'
  Write-ConfigAtomic $hintPath ($hint|ConvertTo-Json -Compress)
 }
 $hintHash=Get-ConfigHash $hintPath
 $script:PresetTransactionActive=$scenario -eq 'Force'
 $script:Editing=$scenario -ne 'Late';$script:ArmLateEditor=$scenario -eq 'Late'
 $script:Rpc=@();$script:EditorReads=0
 $failure='';try{Run-Recovery}catch{$failure=$_.Exception.Message}
 $markerKept=[IO.File]::Exists($marker)
 $sameBytes=(Get-ConfigHash $config) -ceq $edited -and (Get-ConfigHash $owner) -ceq $ownerHash -and (Get-ConfigHash $hintPath) -ceq $hintHash
 $expectedReads=if($scenario -eq 'Late'){2}else{1}
 $expectedRpc=if($scenario -eq 'Discovered'){'fetch_theme_path'}else{''}
 Assert ($failure -like '*CSSLoader Desktop is open*' -and $script:AppearanceFailureCode -eq 15 -and $script:EditorReads -eq $expectedReads -and ($script:Rpc -join ',') -ceq $expectedRpc -and $markerKept -and (Get-ConfigHash $marker) -ceq $journalHash -and $sameBytes) ("${scenario}: editor protected without any config, owner, hint, or journal change (failure=$failure; editorReads=$script:EditorReads; rpc=$($script:Rpc -join ','); marker=$markerKept; sameBytes=$sameBytes)")
 $script:Editing=$false;$script:ArmLateEditor=$false
 Run-Recovery
 Assert ((Get-ConfigHash $config) -ceq $before -and (Get-ConfigHash $owner) -ceq $ownerHash -and ![IO.File]::Exists($marker)) ("${scenario}: original recovery still completes after editing ends")
 $script:EditorReads=0
 Invoke-AppearanceControl 'Status'|Out-Null
 Assert ($script:EditorReads -eq 0) ("${scenario}: healthy status without a journal adds no editor observation")
}
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
"PASS: $($cases.Count) CSSLoader Desktop transaction-recovery checks"
