[CmdletBinding()]
param([Parameter(Mandatory)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Use a new fixture output directory'}
New-Item -ItemType Directory -Path $Out|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$script:RpcBudgetClock=$null;$script:AppearanceObservationOnly=$false;$script:AppearanceTimingFirstControl=$false
function Get-AppearanceProcessCandidates($Role,[switch]$Refresh){
    if($Role -ne 2 -or !$Refresh){throw 'Unexpected fixture process query'}
    $script:EditorReads++
}
function Test-BackendOwner {++$script:OwnerChecks -ne 2 -or $script:Scenario -ne 'owner-denied'}
function Get-AppearanceSteamSession {
    if($script:Scenario -eq 'session-denied'){throw 'fixture session changed'}
    [pscustomobject]@{pid=123;birth='456';root='C:\fixture-steam'}
}
$badAcks=@{
    'outer-string-ack'='{"success":"True","res":"fixture malformed outer"}'
    'outer-integer-ack'='{"success":1,"res":"fixture malformed outer"}'
    'outer-array-ack'='{"success":[true,true],"res":"fixture malformed outer"}'
    'nested-string-ack'='{"success":true,"res":{"success":"True","message":"fixture malformed nested"}}'
    'nested-integer-ack'='{"success":true,"res":{"success":1,"message":"fixture malformed nested"}}'
    'nested-array-ack'='{"success":true,"res":{"success":[true,true],"message":"fixture malformed nested"}}'
}
function Invoke-RestMethod {
    param($Uri,$Method,$Body,$ContentType,$TimeoutSec)
    if($Uri -cne 'http://127.0.0.1:35821/req' -or $Method -cne 'Post'){throw 'Unexpected fixture transport'}
    $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
    $script:Requests.Add($request.method)
    switch($request.method){
        get_themes {
            if($script:Requests.Count -eq 1){
                if($script:Scenario -in @('first-read-failed','invalid-rows','owner-denied','session-denied','already-matched')){
                    [IO.File]::WriteAllText($script:MiniPath,$script:ConcurrentSave)
                }
                if($script:Scenario -eq 'first-read-failed'){throw 'fixture initial read failed'}
                if($script:Scenario -eq 'invalid-rows'){return @{success=$true;res=@()}}
            }
            if($script:Scenario -eq 'read-after-write-failed' -and $script:Requests.Count -gt 1){throw 'fixture read after submitted write failed'}
            return @{success=$true;res=@(
                [pscustomobject]@{name='Art Hero';enabled=$script:ArtEnabled},
                [pscustomobject]@{name='Mini Carousel';enabled=$true}
            )}
        }
        set_theme_state {
            if($request.args.name -cne 'Art Hero'){throw 'Unexpected fixture mutation'}
            $script:ArtEnabled=[bool]$request.args.state
            if($script:Scenario -in @('http-write-failed','nested-reply-rejected','read-after-write-failed') -or $badAcks.ContainsKey($script:Scenario)){
                [IO.File]::WriteAllText($script:MiniPath,'{"active":false,"Tone":"preserve backend parameter"}')
            }
            if($script:Scenario -eq 'http-write-failed'){throw 'fixture unknown transport result'}
            if($script:Scenario -eq 'nested-reply-rejected'){
                return @{success=$true;res=[pscustomobject]@{success=$false;message='fixture backend mutation rejected'}}
            }
            if($badAcks.ContainsKey($script:Scenario)){return ($badAcks[$script:Scenario]|ConvertFrom-Json)}
            return @{success=$true;res=[pscustomobject]@{success=$true}}
        }
        default {throw 'Unexpected fixture method'}
    }
}
$checks=foreach($scenario in (@('first-read-failed','invalid-rows','owner-denied','session-denied','already-matched','http-write-failed','nested-reply-rejected','read-after-write-failed','successful-write')+@($badAcks.Keys|Sort-Object))){
    $script:Scenario=$scenario;$script:OwnerChecks=0;$script:EditorReads=0;$script:Requests=[Collections.Generic.List[string]]::new()
    $script:ThemeRoot=Join-Path $Out $scenario
    $script:ConcurrentSave='{ "active": false, "Tone": "new user choice" }'
    $artPath=Join-Path $script:ThemeRoot 'Art Hero\config_USER.json'
    $script:MiniPath=Join-Path $script:ThemeRoot 'Mini Carousel\config_USER.json'
    foreach($path in $artPath,$script:MiniPath){
        New-Item -ItemType Directory -Path (Split-Path -Parent $path)|Out-Null
        [IO.File]::WriteAllText($path,'{"active":true,"Tone":"original"}')
    }
    $rows=@(
        [pscustomobject]@{installed=$true;name='Art Hero';path=$artPath;configured=$true},
        [pscustomobject]@{installed=$true;name='Mini Carousel';path=$script:MiniPath;configured=$true}
    )
    $script:ArtEnabled=$scenario -eq 'already-matched'
    $malformed=$badAcks.ContainsKey($scenario)
    $script:AppearanceTimingClock=[Diagnostics.Stopwatch]::StartNew()
    $script:AppearanceCorrectionTiming=[long[]]@(0,-1,-1,-1,0,0,1,0,0,0)
    $script:AppearanceMeasureCorrectionWrites=$true
    $failure=''
    try {Sync-AppearanceBackend $rows}catch{$failure=$_.Exception.Message}
    $raw=[IO.File]::ReadAllText($script:MiniPath)
    $actual=$raw|ConvertFrom-Json
    $writes=@($script:Requests|Where-Object {$_ -ceq 'set_theme_state'}).Count
    $expected=@{
        'first-read-failed'='fixture initial read failed';'invalid-rows'='Backend theme missing/invalid: Art Hero';
        'owner-denied'='CSSLoader API owner changed; request was not sent';'session-denied'='fixture session changed';
        'already-matched'='';'http-write-failed'='fixture unknown transport result';
        'nested-reply-rejected'='CSSLoader: fixture backend mutation rejected';
        'read-after-write-failed'='fixture read after submitted write failed';'successful-write'=''
    }[$scenario]
    if($malformed){$expected=if($scenario.StartsWith('outer-')){'CSSLoader: fixture malformed outer'}else{'CSSLoader: fixture malformed nested'}}
    $submitted=$malformed -or $scenario -in @('http-write-failed','nested-reply-rejected','read-after-write-failed','successful-write')
    $configOk=if(!$submitted){$raw -ceq $script:ConcurrentSave}
        elseif($scenario -eq 'successful-write'){$actual.active -and $actual.Tone -ceq 'original'}
        else{$actual.active -and $actual.Tone -ceq 'preserve backend parameter'}
    $expectedEditorReads=[int]($submitted -and [bool]$expected)
    $expectedAcknowledgements=[int]($scenario -in @('successful-write','read-after-write-failed'))
    $ackCount=$script:AppearanceCorrectionTiming[5]
    $dispatchOk=$script:AppearanceCorrectionTiming[4] -eq $writes -and $ackCount -eq $expectedAcknowledgements -and (!$malformed -or ($script:Requests -join ',') -ceq 'get_themes,set_theme_state')
    [pscustomobject]@{name=$scenario;ok=($dispatchOk -and $configOk -and $writes -eq [int]$submitted -and $failure -ceq $expected -and $script:EditorReads -eq $expectedEditorReads);configPreserved=$configOk;requests=$script:Requests.ToArray();failure=$failure;editorReads=$script:EditorReads;acknowledged=$ackCount}
}
$failed=@($checks|Where-Object {!$_.ok})
[pscustomobject]@{ok=!$failed.Count;passed=($checks.Count-$failed.Count);cases=@($checks);installedSteamCssOperations=0}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
$checks|ForEach-Object {Write-Output (($(if($_.ok){'PASS'}else{'FAIL'}))+': '+$_.name)}
if($failed.Count){exit 1}
