[CmdletBinding()]
param([Parameter(Mandatory)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Use a new fixture output directory'}
New-Item -ItemType Directory -Path $Out|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
function Get-AppearanceSteamSession {[pscustomobject]@{pid=123;birth='456';root='C:\fixture-steam'}}
function Test-BackendOwner {$true}
function Test-CurrentUserProcess($Process) {$true}
function Get-AppearanceProcessPath($Process) {$Process.image}
function Get-AppearanceProcessCandidates($Role,[switch]$Refresh){
    if($Role -ne 2 -or !$Refresh){throw 'Expected a fresh editor-only query'}
    $script:EditorQueries++
    if($script:Editor -eq 'unknown'){throw 'Editor inventory unavailable'}
    if($script:Editor -in @('open','opens-briefly')){
        if($script:Editor -eq 'opens-briefly'){$script:Editor='closed'}
        [pscustomobject]@{HasExited=$false;image='C:\fixture\CSSLoader Desktop.exe'}
    }
}
function Invoke-RestMethod($Uri,$Method,$Body,$ContentType,$TimeoutSec){
    if($Uri -cne 'http://127.0.0.1:35821/req' -or $Method -cne 'Post'){throw 'Unexpected transport'}
    $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
    switch($request.method){
        get_themes {
            $script:Reads++
            if($script:Reads -eq 2 -and $script:Scenario -in @('open','opens-briefly','unknown','manual')){
                $script:Editor=if($script:Scenario -eq 'manual'){'open'}else{$script:Scenario}
                [IO.File]::WriteAllText($script:RoundPath,$script:EditorBytes)
            }
            $res=@(foreach($name in @('Art Hero','Round')){[pscustomobject]@{name=$name;enabled=$script:Flags[$name]}})
        }
        set_theme_state {
            $name=[string]$request.args.name
            if($name -cnotin @('Art Hero','Round')){throw 'Unexpected mutation'}
            $script:Submits.Add($name)
            $script:Flags[$name]=[bool]$request.args.state
            if($name -ceq 'Round'){[IO.File]::WriteAllText($script:RoundPath,$script:OriginalBytes)}
            $res=[pscustomobject]@{success=$true}
        }
        default {throw "Unexpected RPC: $($request.method)"}
    }
    @{success=$true;res=$res}
}
$checks=foreach($scenario in @('open','opens-briefly','unknown','healthy-two','only-one','no-writes','manual')){
    $script:Scenario=$scenario;$script:Editor='closed';$script:EditorQueries=0;$script:Reads=0;$script:AppearanceFailureCode=0
    $script:ThemeRoot=Join-Path $Out $scenario
    $script:Submits=[Collections.Generic.List[string]]::new()
    $script:OriginalBytes='{"active":true,"Tone":"original"}'
    $script:EditorBytes='{ "active": false, "Tone": "external edit" }'
    $script:Flags=@{'Art Hero'=($scenario -eq 'no-writes');'Round'=($scenario -in @('only-one','no-writes'))}
    $rows=@(foreach($name in @('Art Hero','Round')){
        $path=Join-Path $script:ThemeRoot "$name\config_USER.json"
        New-Item -ItemType Directory -Path (Split-Path -Parent $path)|Out-Null
        [IO.File]::WriteAllText($path,$script:OriginalBytes)
        if($name -ceq 'Round'){$script:RoundPath=$path}
        [pscustomobject]@{name=$name;installed=$true;configured=$true;path=$path}
    })
    $problem=''
    try{Sync-AppearanceBackend $rows -RequireCurrentSteamScope:($scenario -ne 'manual')}catch{$problem=$_.Exception.Message}
    $blocked=$scenario -in @('open','opens-briefly','unknown')
    $writes=$script:Submits -join ','
    $expectedWrites=if($blocked -or $scenario -eq 'only-one'){'Art Hero'}elseif($scenario -eq 'no-writes'){''}else{'Art Hero,Round'}
    $configPreserved=[IO.File]::ReadAllText($script:RoundPath) -ceq $(if($blocked){$script:EditorBytes}else{$script:OriginalBytes})
    [pscustomobject]@{name=$scenario;ok=($writes -ceq $expectedWrites -and $configPreserved -and
        $script:Reads -eq (1+$script:Submits.Count) -and $script:EditorQueries -eq [int]($blocked -or $scenario -eq 'healthy-two') -and
        $script:AppearanceFailureCode -eq (15*[int]$blocked) -and
        $(if($blocked){$problem -like '*CSSLoader Desktop is open*'}else{!$problem}));writes=$writes;reads=$script:Reads;
        editorQueries=$script:EditorQueries;configPreserved=$configPreserved;error=$problem}
}
$failed=@($checks|Where-Object {!$_.ok})
[pscustomobject]@{ok=!$failed.Count;passed=($checks.Count-$failed.Count);cases=@($checks);installedSteamCssOperations=0}|
    ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
$checks|ForEach-Object {Write-Output (($(if($_.ok){'PASS'}else{'FAIL'}))+': '+$_.name)}
if($failed.Count){exit 1}
