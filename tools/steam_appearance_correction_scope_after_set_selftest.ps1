[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'A fresh fixture output directory is required'}
$null=New-Item -ItemType Directory -Path $Out
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$SessionPid=123;$SessionBirth='456'

function Test-BackendOwner { $true }
function Test-CssDesktopEditing { $false }
function Invoke-AppearanceSharedSteam($Request) {
    if($Request.operation -cne 'identity'){throw 'Unexpected Steam operation'}
    $script:identityReads++
    $armed=$script:lateRead -and $script:submits -eq 1
    $script:events.Add('identity')
    $presence=1;$account='20';$power='31'
    if($script:fault -ceq 'manual-absent' -or ($script:fault -ceq 'first-missing' -and $script:reads -eq 1)){$presence=0}
    if($script:fault -ceq 'first-unknown' -and $script:reads -eq 1){$presence=-1}
    if($armed){
        switch($script:fault){
            'transient-unknown' {if(!$script:unknownReturned){$script:unknownReturned=$true;$presence=-1}}
            'account' {$account='21'}
            'power' {$power='32'}
            'missing' {$presence=0}
        }
    }
    [pscustomobject]@{protocol=1;presence=$presence;pid=123;birth='456';root='C:\fixture-steam';processEpoch='12';accountEpoch=$account;powerGeneration=$power}
}
function Set-FixtureFlag($Path,[bool]$Flag) {
    $config=[IO.File]::ReadAllText($Path)|ConvertFrom-Json
    $config.active=$Flag
    [IO.File]::WriteAllText($Path,($config|ConvertTo-Json -Depth 32 -Compress))
}
function Invoke-RestMethod($Uri,$Method,$Body,$ContentType,$TimeoutSec) {
    if($Uri -cne 'http://127.0.0.1:35821/req' -or $Method -cne 'Post'){throw 'Unexpected CSS endpoint'}
    $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
    switch -CaseSensitive ($request.method) {
        'get_themes' {
            $script:events.Add('get');$script:reads++
            if($script:submits -eq 1 -and !$script:lateRead){
                $script:lateRead=$true
                if($script:fault -in 'transient-unknown','account','power','missing'){
                    # An external save occurs while the first submission's readback is in flight.
                    Set-FixtureFlag $script:roundConfig $false
                    $script:savedHash=Get-ConfigHash $script:roundConfig
                }
            }
            return [pscustomobject]@{success=$true;res=$script:backendRows}
        }
        'set_theme_state' {
            $script:events.Add('set:'+ $request.args.name);$script:submits++
            if($script:fault -ceq 'http-rejected'){
                # Model a backend side effect before a failed response.
                Set-FixtureFlag $script:roundConfig $false
                return [pscustomobject]@{success=$false;res='fixture refusal'}
            }
            return [pscustomobject]@{success=$true;res=$true}
        }
        default {throw 'Unexpected CSS method'}
    }
}

$observations=@(foreach($mode in 'active','first-missing','first-unknown','transient-unknown','account','power','missing','http-rejected','manual-absent','no-writes'){
    $SessionPid=123;$SessionBirth='456'
    if($mode -in 'manual-absent','first-missing'){$SessionPid=0;$SessionBirth=''}
    $script:fault=$mode;$script:submits=0;$script:reads=0;$script:identityReads=0;$script:lateRead=$false;$script:unknownReturned=$false;$script:savedHash=''
    $script:AppearanceFailureCode=0;$script:AppearanceSharedIdentityKey='';$script:events=New-Object 'Collections.Generic.List[string]'
    $script:ThemeRoot=Join-Path $Out "$mode/themes"
    $rows=@(foreach($name in 'Art Hero','Round'){
        $dir=Join-Path $script:ThemeRoot $name
        $null=New-Item -ItemType Directory -Path $dir -Force
        $path=Join-Path $dir 'config_USER.json'
        [IO.File]::WriteAllText($path,'{"active":true,"fixtureOption":"keep"}')
        if($name -ceq 'Round'){$script:roundConfig=$path}
        [pscustomobject]@{name=$name;installed=$true;configured=$true;path=$path}
    })
    $script:backendRows=@($rows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$true}})
    $failed=''
    $initial=$script:backendRows;$force=@('Art Hero','Round')
    if($mode -in 'first-missing','first-unknown'){$initial=$null}
    if($mode -ceq 'no-writes'){$force=@()}
    try{Sync-AppearanceBackend $rows $false $force -RequireCurrentSteamScope:($mode -cne 'manual-absent') -InitialThemes $initial}
    catch{$failed=$_.Exception.Message}
    $round=[IO.File]::ReadAllText($script:roundConfig)|ConvertFrom-Json
    [pscustomobject]@{mode=$mode;submits=$script:submits;reads=$script:reads;identityReads=$script:identityReads;error=$failed;code=$script:AppearanceFailureCode;
        roundActive=$round.active;savedBytesPreserved=([bool]$script:savedHash -and (Get-ConfigHash $script:roundConfig) -ceq $script:savedHash);option=$round.fixtureOption;
        events=$script:events.ToArray()}
})
$observations|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'observations.json') -Encoding UTF8
$checks=0
foreach($item in $observations){
    $events=$item.events -join ','
    if($item.mode -in 'active','manual-absent'){
        $ok=!$item.error -and $item.submits -eq 2 -and $item.reads -eq 2 -and $item.identityReads -eq 2 -and $item.roundActive -and
            $events -ceq 'identity,set:Art Hero,get,identity,set:Round,get'
    }elseif($item.mode -in 'first-missing','first-unknown'){
        $ok=$item.error -like '*Steam*' -and $item.code -eq 1 -and !$item.submits -and $item.reads -eq 1 -and $item.identityReads -eq 1 -and
            $item.roundActive -and $events -ceq 'get,identity'
    }elseif($item.mode -ceq 'no-writes'){
        $ok=!$item.error -and !$item.submits -and !$item.reads -and !$item.identityReads -and $item.roundActive
    }elseif($item.mode -ceq 'http-rejected'){
        $ok=$item.error -like 'CSSLoader: fixture refusal*' -and $item.code -eq 0 -and $item.submits -eq 1 -and $item.reads -eq 0 -and
            $item.identityReads -eq 2 -and $item.roundActive
    }else{
        $ok=$item.error -like '*Steam*' -and $item.code -eq 1 -and $item.submits -eq 1 -and $item.reads -eq 1 -and
            $item.identityReads -eq 2 -and !$item.roundActive -and $item.savedBytesPreserved -and $events -ceq 'identity,set:Art Hero,get,identity'
    }
    if(!$ok -or $item.option -cne 'keep'){throw ('Correction scope regression: '+($item|ConvertTo-Json -Compress))}
    $checks++
}
[pscustomobject]@{ok=$true;passed=$checks;realSteamOperations=0;realCssOperations=0;realUserConfigWrites=0;fixtureInitialization=$script:AppearanceFixtureInitialization}|
    ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $checks automatic correction scope checks"
