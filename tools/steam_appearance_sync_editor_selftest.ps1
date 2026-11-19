[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[switch]$Observe)
$ErrorActionPreference='Stop'
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot (Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl') -EvidenceRoot $Out
function Get-AppearanceSteamSession {[pscustomobject]@{pid=123;birth='456';root='C:\fixture-steam'}}
function Get-AppearanceProcessCandidates($Role,[switch]$Refresh){
    $script:EditorReads++
    if($Role -ne 2 -or !$Refresh){throw 'Unexpected editor query'}
    if($script:Editor -eq 'unknown'){throw 'Editor process facts unavailable'}
    if($script:Editor -eq 'open'){[pscustomobject]@{HasExited=$false;image='C:\fixture\CSSLoader Desktop.exe'}}
}
function Test-CurrentUserProcess($Process){return $true}
function Get-AppearanceProcessPath($Process){$Process.image}
function Invoke-LoaderRpc([string]$Method,$Arguments,[ref]$WriteAttempted,[int]$CorrectionReason){
    $script:Rpc+=@($Method)
    switch($Method){
        get_themes {
            if($scenario -eq 'BeforeWrite'){throw 'Fixture initial read failed'}
            if($script:Sent -and $scenario -eq 'ReadWhileEditing'){
                $script:Editor='open'
                [IO.File]::WriteAllText($config,$editedText)
                throw 'Fixture read after write failed'
            }
            [pscustomobject]@{name='Round';enabled=$script:Sent}
        }
        set_theme_state {
            if(!$WriteAttempted -or $Arguments.name -cne 'Round' -or $Arguments.state -ne $true){throw 'Unexpected fixture write'}
            $WriteAttempted.Value=$true;$script:Sent=$true
            if($scenario -eq 'Success'){return}
            if($scenario -eq 'ReadWhileEditing'){return}
            if($scenario -eq 'SendWhileEditing'){$script:Editor='open'}
            if($scenario -eq 'UnknownEditor'){$script:Editor='unknown'}
            [IO.File]::WriteAllText($config,$editedText)
            throw 'Fixture submit failed'
        }
        default {throw "Unexpected RPC: $Method"}
    }
}
$rows=New-Object Collections.Generic.List[object]
foreach($scenario in @('SendWhileEditing','ReadWhileEditing','UnknownEditor','NoEditor','BeforeWrite','Success')){
    $script:ThemeRoot=Join-Path $Out "$scenario/homebrew/themes"
    $themeDir=Join-Path $script:ThemeRoot 'Round'
    New-Item -ItemType Directory -Path $themeDir -Force|Out-Null
    $config=Join-Path $themeDir 'config_USER.json'
    $originalText='{"active":true,"Intensity":"20px"}'
    $editedText='{"active":false,"Intensity":"25px"}'
    [IO.File]::WriteAllText($config,$originalText)
    $script:Editor='closed';$script:EditorReads=0;$script:Rpc=@();$script:Sent=$false;$script:AppearanceFailureCode=0
    $failure=''
    try{Sync-AppearanceBackend @([pscustomobject]@{name='Round';installed=$true;configured=$true;path=$config}) -RequireCurrentSteamScope}catch{$failure=$_.Exception.Message}
    $actual=[IO.File]::ReadAllText($config)
    $parsed=$actual|ConvertFrom-Json
    $calls=$script:Rpc -join ','
    $passed=switch($scenario){
        {$_ -in @('SendWhileEditing','ReadWhileEditing','UnknownEditor')} {
            $initial=if($_ -eq 'ReadWhileEditing'){'Fixture read after write failed'}else{'Fixture submit failed'}
            $expectedCalls=if($_ -eq 'ReadWhileEditing'){'get_themes,set_theme_state,get_themes'}else{'get_themes,set_theme_state'}
            $failure.StartsWith($initial) -and $failure -like '*CSSLoader Desktop is open*' -and $script:AppearanceFailureCode -eq 15 -and $script:EditorReads -eq 1 -and $calls -ceq $expectedCalls -and $actual -ceq $editedText
        }
        NoEditor {$failure -ceq 'Fixture submit failed' -and $script:EditorReads -eq 1 -and $parsed.active -eq $true -and $parsed.Intensity -ceq '25px' -and $calls -ceq 'get_themes,set_theme_state'}
        BeforeWrite {$failure -ceq 'Fixture initial read failed' -and $script:EditorReads -eq 0 -and $actual -ceq $originalText -and $calls -ceq 'get_themes'}
        Success {!$failure -and $script:EditorReads -eq 0 -and $actual -ceq $originalText -and $calls -ceq 'get_themes,set_theme_state,get_themes'}
    }
    $rows.Add([pscustomobject]@{scenario=$scenario;ok=[bool]$passed;error=$failure;code=$script:AppearanceFailureCode;editorReads=$script:EditorReads;rpc=$calls;active=$parsed.active;intensity=$parsed.Intensity;editedBytesRetained=($actual -ceq $editedText)})
}
$failed=@($rows|Where-Object {!$_.ok})
[pscustomobject]@{ok=(!$failed.Count);passed=($rows.Count-$failed.Count);cases=@($rows.ToArray());realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
if($failed.Count){
    $failed|ConvertTo-Json -Depth 4
    if(!$Observe){throw 'CSS sync editor protection checks failed'}
}else{"PASS: $($rows.Count) CSS sync editor protection checks"}
