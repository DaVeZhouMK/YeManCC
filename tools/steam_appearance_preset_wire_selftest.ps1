[CmdletBinding()]
param([Parameter(Mandatory)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
if(Test-Path -LiteralPath $Out){throw 'Use a fresh output path'}
$null=[IO.Directory]::CreateDirectory($Out)
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
function Require($Condition,[string]$Message){if(!$Condition){throw $Message}}
$presetAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance/preset.ps1'),[ref]$null,[ref]$null)
$scopeMarker=$presetAst.EndBlock.Statements|Where-Object {$_.Left.Extent.Text -ceq '$script:PresetScopeProtocol'}
Require ($scopeMarker.Extent.Text -ceq '$script:PresetScopeProtocol=1') 'Scoped preset capability missing'
. ([scriptblock]::Create($scopeMarker.Extent.Text))
$script:RetiredThemeNames=@('Game Cover Reflections')
$script:Online=$false;$script:Rows=@();$script:Rpc=[Collections.Generic.List[string]]::new();$script:Dom=0;$script:BadDom=$false
function Get-AppearanceSteamSession {if($script:Online){[pscustomobject]@{pid=43;birth='100';root='C:\fixture-steam'}}}
function Get-BackendProcesses {if($script:Online){[pscustomobject]@{Id=42}}}
function Test-BackendOwner {$script:Online}
function Test-CssDesktopEditing {$script:EditorInitiallyOpen -or ($script:Fetches -gt 0 -and $script:Fetches -eq $script:EditorAtFetch)}
function Start-ResidentAppearanceBackend {throw 'No backend launch in preset receipt tests'}
function Invoke-LoaderRpc([string]$Method,$Arguments){
    $script:Rpc.Add($Method)
    switch($Method){
        fetch_theme_path {
            $script:Fetches++
            if($script:Fetches -eq $script:UnavailableAt){throw 'Unknown CSSLoader owner'}
            if($script:Fetches -eq $script:MoveAt){$script:NewRoot}else{$script:ThemeRoot}
        }
        get_themes {$script:Rows}
        reset {$script:Rows=@(Get-ThemeRows -RevisionOnly|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});[pscustomobject]@{fails=@()}}
        set_theme_state {($script:Rows|Where-Object name -eq $Arguments.name).enabled=$Arguments.state}
        default {throw ('Unexpected RPC '+$Method)}
    }
}
function Get-AppearanceRenderedStatus($Session,$Status){
    $script:Dom++
    Require ($Session.pid -eq 43 -and @($Status.themes|Where-Object renderPlan).Count -eq 7) 'Reconciliation lost full plans before DOM confirmation'
    [pscustomobject]@{checked=$true;confirmed=(!$script:BadDom -or $script:Dom -eq 1);reason='fixture-dom';mismatched=@()}
}
$planner=${function:Get-AppearanceThemeRenderPlan}
function Get-AppearanceThemeRenderPlan([string]$Name,$Manifest,$Config,$ContentIndex,[string]$Target='SP',[switch]$ValidateOnly){
    if($ValidateOnly){$script:ValidatePlans++}else{$script:FullPlans++}
    & $planner @PSBoundParameters
}
function Json($Value){ConvertTo-Json -InputObject $Value -Compress -Depth 16}
function Encode($Value){[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((Json $Value)))}
function WireBytes($Value){
    $previous=[Console]::Out;$sink=[IO.StringWriter]::new()
    try{[Console]::SetOut($sink);Write-JsonResult $Value;[Text.Encoding]::UTF8.GetByteCount($sink.ToString().TrimEnd("`r","`n"))}
    finally{[Console]::SetOut($previous);$sink.Dispose()}
}
function Invoke-Case([string]$Name,[string]$RunAction,[string]$Profile,[switch]$Internal){
    $old=Get-AppearanceStatus -RevisionOnly
    $payload=[ordered]@{}
    if($RunAction -in @('Prepare','Reload','Reconcile')){$payload.expectedRevision=$old.revision;$payload.targetId=$old.targetId}
    if($Profile -ne 'legacy'){$payload.profile=$Profile}
    $Action=if($Internal){'Automatic'}else{$RunAction};$Payload=if($payload.Count){Encode $payload}else{''}
    $script:FullPlans=0;$script:ValidatePlans=0;$script:Dom=0;$script:Rpc.Clear()
    $reply=Invoke-AppearanceControl $RunAction
    $fullRows=@($reply.snapshot.themes|Where-Object {$_.PSObject.Properties['renderPlan']}).Count
    $expectSummary=$Profile -ceq 'ui-summary-v1' -and !$Internal
    Require ($fullRows -eq $(if($expectSummary){0}else{7})) "Wrong plan shape: $Name / $Profile"
    if($RunAction -in @('Prepare','Reset','Restore')){
        Require ($script:FullPlans -eq $(if($expectSummary){0}else{7}) -and $script:ValidatePlans -eq $(if($expectSummary){7}else{0})) "Wrong plan work: $Name / $Profile"
    }
    if($RunAction -eq 'Reset'){
        Require (@($reply.snapshot.themes|Where-Object configured).Count -eq 7) 'Reset did not enable all seven managed themes'
    }
    $size=WireBytes $reply
    foreach($row in $reply.snapshot.themes){$row.PSObject.Properties.Remove('renderPlan')}
    if($reply.previousRevision){$reply.previousRevision='prior-revision'}
    $reply.snapshot.targetId='target';$reply.snapshot.revision='revision'
    $hashes=foreach($name in $script:ThemeNames){Get-ConfigHash (Join-Path $script:ThemeRoot ($name+'/config_USER.json')) -MaximumBytes 262144}
    Require (!(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) "Transaction left open: $Name"
    [pscustomobject]@{name=$Name;reply=(Json $reply);ok=$reply.ok;round=($reply.snapshot.themes|Where-Object name -eq 'Round');wireBytes=$size;full=$script:FullPlans;validation=$script:ValidatePlans;dom=$script:Dom;rpc=($script:Rpc.ToArray() -join ',');configHash=($hashes -join ',')}
}
function Run-Series([string]$Folder,[string]$Profile){
    $script:ThemeRoot=Join-Path $Out "$Folder/themes";$script:PresetStateBase=Join-Path $Out "$Folder/states"
    $script:Online=$false;$script:BadDom=$false
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $script:ThemeRoot))
    Copy-Item -LiteralPath (Join-Path $script:PackRoot 'themes') -Destination $script:ThemeRoot -Recurse
    $configPath=Join-Path $script:ThemeRoot 'Round/config_USER.json'
    $original='{"active":false,"Intensity":"unsupported"}'
    [IO.File]::WriteAllText($configPath,$original)
    Invoke-Case 'first-prepare' 'Prepare' $Profile
    Invoke-Case 'existing-prepare' 'Prepare' $Profile
    [IO.File]::WriteAllText($configPath,'{"active":false,"Intensity":"10px"}')
    $reset=Invoke-Case 'offline-reset' 'Reset' $Profile
    Require ($reset.ok -and $reset.round.configured) 'Reset did not restore the 7/7 preset';$reset
    $script:Online=$true
    $reloaded=Invoke-Case 'resident-reload' 'Reload' $Profile
    Require ($reloaded.ok -and $reloaded.rpc -match '(?:^|,)reset(?:,|$)' -and !$reloaded.dom) 'Reload did not confirm the existing backend';$reloaded
    ($script:Rows|Where-Object name -eq 'Round').enabled=$false
    $reconciled=Invoke-Case 'dom-confirmed' 'Reconcile' $Profile
    Require ($reconciled.ok -and $reconciled.dom -eq 2 -and $reconciled.rpc -match '(?:^|,)set_theme_state(?:,|$)') 'Reconcile did not check both DOM observations';$reconciled
    $script:BadDom=$true;($script:Rows|Where-Object name -eq 'Round').enabled=$false
    $undone=Invoke-Case 'dom-unconfirmed' 'Reconcile' $Profile
    Require (!$undone.ok -and $undone.dom -eq 2) 'A failed DOM acknowledgement must stay unconfirmed';$undone
    $script:Online=$false
    $restored=Invoke-Case 'offline-restore' 'Restore' $Profile
    Require ($restored.ok -and $restored.round.error -ceq 'render-patch-selection-invalid' -and [IO.File]::ReadAllText($configPath) -ceq $original) 'Original configuration bytes or its invalid option diagnostic lost';$restored
}
$legacy=@(Run-Series 'legacy' 'legacy')
$current=@(Run-Series 'compact' 'ui-summary-v1')
Require ($legacy.Count -eq 7 -and $current.Count -eq 7) 'Missing parity paths'
$parity=@(for($i=0;$i -lt $legacy.Count;$i++){
    $a=$legacy[$i];$b=$current[$i]
    Require ($a.name -ceq $b.name -and $a.reply -ceq $b.reply -and $a.rpc -ceq $b.rpc -and $a.dom -eq $b.dom -and $a.configHash -ceq $b.configHash) ('Changed normalized receipt, requests or config bytes: '+$a.name)
    Require ($b.wireBytes -lt $a.wireBytes -and ($a.dom -eq 0 -or $a.full -eq $b.full)) ('Missing wire savings or fewer verifier plans: '+$a.name)
    [pscustomobject]@{case=$a.name;legacyBytes=$a.wireBytes;compactBytes=$b.wireBytes;legacyFullPlans=$a.full;compactFullPlans=$b.full;validationPlans=$b.validation;dom=$b.dom;rpc=$b.rpc}
})
$null=Invoke-Case 'case-sensitive-profile' 'Prepare' 'UI-SUMMARY-V1'
$null=Invoke-Case 'automatic-never-trims' 'Prepare' 'ui-summary-v1' -Internal
function Invoke-Scoped($Body){
    $Action='ScopedPreset';$Payload=if($Body){Encode $Body}else{''}
    $script:Fetches=0;$script:Rpc.Clear()
    Invoke-AppearanceControl 'ScopedPreset'
}
function Init-Scoped([string]$Case){
    $script:ThemeRoot=Join-Path $Out "$Case/themes";$script:PresetStateBase=Join-Path $Out "$Case/states"
    $script:NewRoot=Join-Path $Out "$Case/other-themes";$script:Online=$false;$script:Fetches=0;$script:MoveAt=0;$script:UnavailableAt=0
    $script:EditorInitiallyOpen=$false;$script:EditorAtFetch=0
    $script:Config=Join-Path $script:ThemeRoot 'Round/config_USER.json';$script:Other=Join-Path $script:NewRoot 'Round/config_USER.json'
    $script:OriginalBytes=[Text.Encoding]::UTF8.GetBytes("{`r`n  `"active`": false, `"Intensity`": `"unsupported`"`r`n}`r`n")
    foreach($file in @($script:Config,$script:Other)){
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file));[IO.File]::WriteAllBytes($file,$script:OriginalBytes)
    }
    $script:Target=(Get-ThemeTargetId);$script:OldRoot=$script:ThemeRoot
}
function FileBytes($Path){[Convert]::ToBase64String([IO.File]::ReadAllBytes($Path))}
function Require-Rejection($Body,[string]$Pattern){
    $problem='';try{$null=Invoke-Scoped $Body}catch{$problem=$_.Exception.Message}
    Require ($problem -match $Pattern) ('Expected rejection: '+$problem)
}
$scoped=@(
    foreach($online in @($false,$true)){
        Init-Scoped "scoped-$online"
        $script:Online=$online;$script:Rows=@()
        $reset=Invoke-Scoped @{mode='preset';targetId=$script:Target.ToLowerInvariant()}
        Require ($reset.ok -and $reset.saved -and $reset.snapshot.presetScopeProtocol -eq 1 -and $reset.snapshot.targetId -ceq $script:Target) 'Scoped reset did not acknowledge the bound target'
        Require (@($reset.snapshot.themes|Where-Object configured).Count -eq 7 -and @($reset.snapshot.themes|Where-Object renderPlan).Count -eq 0) 'Scoped reset did not restore seven compact themes'
        $resetRpc=$script:Rpc.ToArray() -join ','
        $restore=Invoke-Scoped @{mode='previous';targetId=$script:Target}
        Require ($restore.ok -and $restore.saved -and !$restore.snapshot.packOwned -and $restore.snapshot.backendReloadRequired -and $restore.snapshot.targetId -ceq $script:Target -and (FileBytes $script:Config) -ceq [Convert]::ToBase64String($script:OriginalBytes)) 'Scoped restore lost exact original bytes or ownership state'
        Require ((FileBytes $script:Other) -ceq (FileBytes $script:Config)) 'Scoped restore touched another target'
        Require (!(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'Scoped transaction left open'
        [pscustomobject]@{case="same-target-online-$online";enabled=7;restoreExact=$true;resetRpc=$resetRpc;restoreRpc=($script:Rpc.ToArray() -join ',')}
    }
    foreach($editorAlreadyOpen in @($true,$false)){
        Init-Scoped "scope-editor-$editorAlreadyOpen"
        $before=FileBytes $script:Config;$script:Online=$true;$script:EditorInitiallyOpen=$editorAlreadyOpen
        if(!$editorAlreadyOpen){$script:EditorAtFetch=2}
        Require-Rejection @{mode='preset';targetId=$script:Target} 'Desktop is open'
        Require ((FileBytes $script:Config) -ceq $before -and !(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'Desktop protection changed a config or left an unfinished transaction'
        if($editorAlreadyOpen){Require (!$script:Rpc.Count -and !(Test-Path -LiteralPath $script:PresetStateBase)) 'Initial editor denial contacted or wrote the backend'}
        [pscustomobject]@{case="editor-open-at-start-$editorAlreadyOpen";saved=$false;rpc=($script:Rpc.ToArray() -join ',')}
    }
    foreach($mode in @('preset','previous')){
        foreach($stage in @('initial-move','initial-unavailable','staging-move','committed-move')){
            Init-Scoped "scope-$mode-$stage"
            if($mode -eq 'previous'){$null=Invoke-Scoped @{mode='preset';targetId=$script:Target}}
            $before=FileBytes $script:Config;$otherBefore=FileBytes $script:Other
            if($stage -eq 'initial-move'){$script:MoveAt=1}
            if($stage -eq 'initial-unavailable'){$script:UnavailableAt=1}
            if($stage -eq 'staging-move'){$script:MoveAt=2}
            if($stage -eq 'committed-move'){$script:MoveAt=3}
            $script:Online=$true;$body=@{mode=$mode;targetId=$script:Target}
            if($stage -eq 'committed-move'){
                $reply=Invoke-Scoped $body
                Require (!$reply.ok -and $reply.saved -and !$reply.snapshot -and $reply.message -match '\u5df2\u4fdd\u5b58.*\u672a\u786e\u8ba4') 'Committed scoped mutation lost its saved fact'
                Require ((FileBytes $script:Config) -cne $before) 'Commit did not change the bound config'
            }else{
                Require-Rejection $body '\u76ee\u5f55\u5df2\u53d8\u5316|\u65e0\u6cd5\u6838\u5b9e'
                Require ((FileBytes $script:Config) -ceq $before) 'Rejected scope touched the original config'
            }
            $hint=Join-Path $script:PresetStateBase 'active-target.json'
            if($stage.StartsWith('initial-')){Require (!(Test-Path -LiteralPath $hint)) 'Rejected initial scope wrote a target hint'}
            else{Require ((Get-Content -LiteralPath $hint -Raw|ConvertFrom-Json).themeRoot -ceq $script:OldRoot) 'Scoped mutation changed the hint to another target'}
            Require ((FileBytes $script:Other) -ceq $otherBefore -and $script:ThemeRoot -ceq $script:OldRoot) 'Moved scope selected or touched another target'
            Require (!(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'Moved scope left a transaction open'
            [pscustomobject]@{case="$mode-$stage";saved=($stage -eq 'committed-move');rpc=($script:Rpc.ToArray() -join ',');otherUnchanged=$true}
        }
    }
    Init-Scoped 'scope-invalid'
    foreach($body in @($null,@{mode='Reset';targetId=$script:Target},@{mode='preset';targetId=$script:Target+"`n"},@{mode='preset'},@{mode='previous';targetId=('b'*16)},(,@{mode='preset';targetId=$script:Target}))){
        Require-Rejection $body 'scoped appearance preset request|\u76ee\u5f55\u5df2\u53d8\u5316'
        Require (!(Test-Path -LiteralPath $script:PresetStateBase) -and !$script:Rpc.Count) 'Invalid or offline mismatched scope performed work'
    }
    $script:PresetScopeProtocol=0
    try{Require-Rejection @{mode='preset';targetId=$script:Target} 'unsupported'}finally{$script:PresetScopeProtocol=1}
    [pscustomobject]@{case='malformed-offline-and-mixed-module';saved=$false}
)
[pscustomobject]@{ok=$true;passed=(9+$scoped.Count);engine=[string]$PSVersionTable.PSVersion;parity=$parity;scoped=$scoped;realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output ('PASS: 9 legacy wire cases and '+$scoped.Count+' scoped preset cases')
