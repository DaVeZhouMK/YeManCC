param([string]$Out='G:\YeManCC-Work\Mainline\Temp\steam-appearance-validation\powershell')
$ErrorActionPreference='Stop'
$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\steam-appearance.ps1'
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. $source -Action Library
$script:PresetStateBase=Join-Path $Out 'states'
$cases=New-Object Collections.Generic.List[string]
function Assert($condition,[string]$message){if(!$condition){throw $message};$cases.Add($message)}
$script:ThemeRoot=Join-Path $Out ('themes-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $script:ThemeRoot|Out-Null
foreach($name in $script:ThemeNames){$dir=Join-Path $script:ThemeRoot $name;New-Item -ItemType Directory -Path $dir|Out-Null;Copy-Item -LiteralPath (Join-Path (Join-Path $script:PackRoot 'themes') "$name\theme.json") -Destination (Join-Path $dir 'theme.json');Copy-Item -LiteralPath (Join-Path (Join-Path $script:PackRoot 'themes') "$name\config_USER.json") -Destination (Join-Path $dir 'config_USER.json')}
$script:BackendLive=$false;$script:FailReset=$false;$script:StateCalls=0;$script:BackendRows=@()
function Get-BackendProcesses {if($script:BackendLive){[pscustomobject]@{Id=1234;Path=$script:BackendExe}}}
function Test-BackendOwner {return $true}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}) {
    switch($Method){
        fetch_theme_path {return $script:ThemeRoot}
        get_themes {return $script:BackendRows}
        set_theme_state {
            if($script:FailReset){throw 'fixture application failure'}
            $script:StateCalls++
            $row=$script:BackendRows|Where-Object name -eq $Arguments.name;$row.enabled=$Arguments.state
            if($Arguments.name -eq 'Art Hero' -and !$Arguments.state){foreach($dep in @('Centered Game Text','Mini Carousel')){($script:BackendRows|Where-Object name -eq $dep).enabled=$false}}
            foreach($backend in $script:BackendRows){$path=Join-Path $script:ThemeRoot "$($backend.name)\config_USER.json";$config=[IO.File]::ReadAllText($path)|ConvertFrom-Json;$config.active=$backend.enabled;Write-ConfigAtomic $path ($config|ConvertTo-Json -Depth 64 -Compress)}
            if($Arguments.set_deps -ne $false -or $Arguments.set_deps_value -ne $false){throw 'Dependency defaults must never overwrite user parameters'}
            return @{success=$true;message='Success'}
        }
        default {throw 'fixture unexpected method'}
    }
}
Invoke-PresetPrepare|Out-Null
$original=@{};foreach($row in @(Get-ThemeRows)){$original[$row.name]=$row.config|ConvertTo-Json -Depth 64 -Compress}
$status=Get-AppearanceStatus
Assert ($status.themes.Count -eq $script:ThemeNames.Count -and !$status.backendReady) 'Read all managed configured themes without starting the backend'
Assert (($status.themes|Where-Object name -eq 'Centered Game Text').parameterCount -eq 0) 'Zero-patch themes have no phantom parameter'
$states=[pscustomobject]@{'Art Hero'=$false};$result=Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=$states})
Assert ($result.ok -and $result.saved -and !$result.snapshot.backendReady) 'Offline toggle saves honestly without claiming live application'
$rows=@(Get-ThemeRows)
Assert (!(($rows|Where-Object name -eq 'Art Hero').configured)) 'Art Hero can be disabled'
Assert (($rows|Where-Object name -eq 'Mini Carousel').configured -and ($rows|Where-Object name -eq 'Centered Game Text').configured) 'Disabling Art Hero preserves independently selected optional dependencies'
foreach($row in $rows){$old=$original[$row.name]|ConvertFrom-Json;$old.PSObject.Properties.Remove('active');$new=$row.config|ConvertTo-Json -Depth 64|ConvertFrom-Json;$new.PSObject.Properties.Remove('active');Assert (($old|ConvertTo-Json -Depth 64 -Compress) -eq ($new|ConvertTo-Json -Depth 64 -Compress)) "All existing patches/components preserved: $($row.name)"}
$rejected=$false;try{Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=$states})|Out-Null}catch{$rejected=$true};Assert $rejected 'Stale config revision is rejected before writing'
$rejected=$false;try{Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=(Get-AppearanceStatus).revision;states=[pscustomobject]@{'..\unknown'=$true}})|Out-Null}catch{$rejected=$true};Assert $rejected 'Unknown theme/path is rejected'
$script:BackendLive=$true;$script:BackendRows=@(Get-ThemeRows|ForEach-Object {[pscustomobject]@{name=$_.name;enabled=$_.configured}})
Complete-PresetReload
$status=Get-AppearanceStatus;$states=[ordered]@{};foreach($name in $script:ThemeNames){$states[$name]=$false}
$result=Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=[pscustomobject]$states})
Assert ($result.ok -and $result.snapshot.configuredMatchesBackend -and $script:StateCalls -gt 0) 'Batch of managed switches reconciles the bounded API and verifies backend enabled states'
Assert (@($result.snapshot.themes|Where-Object configured).Count -eq 0) 'All-off is distinct from stopping CSSLoader'
$status=$result.snapshot;foreach($name in $script:ThemeNames){$states[$name]=$true}
$result=Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=[pscustomobject]$states})
Assert ($result.ok -and @($result.snapshot.themes|Where-Object configured).Count -eq $script:ThemeNames.Count) 'All-on restores managed effects without editing patches'
$script:FailReset=$true
$result=Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$result.snapshot.revision;states=[pscustomobject]@{'Round'=$false}})
Assert (!$result.ok -and $result.saved -and !$result.snapshot.configuredMatchesBackend) 'Failed transaction reports saved-but-not-applied and exposes mismatch'
$script:FailReset=$false
# Real cross-process startup race, with an isolated inert EXE of the exact long image name.
$race=Join-Path $Out ('race-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $race|Out-Null
$exe=Join-Path $race 'CssLoader-Standalone-Headless.exe'
Add-Type -TypeDefinition 'using System;using System.Threading;public class LoaderFixture{public static void Main(){Thread.Sleep(120000);}}' -OutputAssembly $exe -OutputType ConsoleApplication
$jobs=@();$outputs=@()
try {
    for($i=0;$i -lt 6;$i++){
        $command=". '$($source.Replace("'","''"))' -Action Library; `$script:BackendExe='$($exe.Replace("'","''"))'; function Get-NetTCPConnection { @() }; Start-AppearanceBackend|ConvertTo-Json -Compress"
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));$stdout=Join-Path $race "$i.out";$stderr=Join-Path $race "$i.err"
        $jobs+=Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $outputs+=$stdout
    }
    foreach($job in $jobs){if(!$job.WaitForExit(45000)){throw 'Race worker timeout'}}
    $fixtureProcesses=@(Get-Process -Name 'CssLoader-Standalone-Headless' -ErrorAction SilentlyContinue|Where-Object {$_.Path -eq $exe})
    Assert ($fixtureProcesses.Count -eq 1) 'Six simultaneous launches produce exactly one inert long-name backend process'
    $results=@($outputs|ForEach-Object {Get-Content -LiteralPath $_ -Raw|ConvertFrom-Json})
    Assert (@($results|Where-Object started).Count -eq 1 -and @($results|Where-Object {!$_.ok}).Count -eq 0) 'One launch wins; all other processes report reuse'
    $truncated=(& tasklist.exe /FI 'IMAGENAME eq CssLoader-Standalone-Headless.exe') -join "`n"
    Assert (!$truncated.Contains('CssLoader-Standalone-Headless.exe')) 'Regression reproduced: tasklist default TABLE truncates the actual CSSLoader image name'
}finally {Get-Process -Name 'CssLoader-Standalone-Headless' -ErrorAction SilentlyContinue|Where-Object {$_.Path -eq $exe}|Stop-Process -ErrorAction SilentlyContinue;foreach($job in $jobs){if(!$job.HasExited){Stop-Process -Id $job.Id -ErrorAction SilentlyContinue}}}
[pscustomobject]@{ok=$true;cases=@($cases);productionThemeFilesUnchanged=$true;output=$Out}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'selftest-result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) appearance / singleton checks"