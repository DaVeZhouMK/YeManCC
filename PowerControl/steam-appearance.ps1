# YMCC Steam appearance: bounded RPC, preserved patches, cross-process singleton.
[CmdletBinding()]
param(
    [ValidateSet('Status','Apply','Refresh','Reload','Ensure','Prepare','Reset','Restore','Idle','Reconcile','Automatic','Library')][string]$Action='Status',
    [string]$Payload='',
    [uint32]$SessionPid=0,
    [string]$SessionBirth='',
    [string]$BackendPath='',
    [string]$ReadyEvent='',
    [string]$IdleEvent='',
    [ValidateRange(-1,120000)][int]$GateTimeoutMs=-1,
    [switch]$StartBackend,
    [switch]$VerifyOnly
)
$ErrorActionPreference='Stop'
$script:ResidentBackendPolicy=3
$script:ManualObservationProtocol=1
$script:AppearanceObservationOnly=[bool]$VerifyOnly
if($VerifyOnly -and ($Action -ne 'Automatic' -or $StartBackend)){throw 'Read-only CSS verification cannot initialize or control a backend'}
$ProgressPreference='SilentlyContinue'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$script:ThemeNames=@('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details')
$script:RetiredThemeNames=@('Game Cover Reflections')
$script:ThemeRoot=Join-Path $env:USERPROFILE 'homebrew\themes'
$script:BackendExe='C:\SOFT\CSSLoader Desktop\CssLoader-Standalone-Headless.exe'
if($BackendPath){
    if($BackendPath -notmatch '^[A-Za-z]:[\\/]' -or $BackendPath -match '["\r\n]' -or [IO.Path]::GetFileName($BackendPath) -ine 'CssLoader-Standalone-Headless.exe'){throw 'Invalid leased CSS backend path'}
    $script:BackendExe=[IO.Path]::GetFullPath($BackendPath)
}
$script:LinkFile=Join-Path $PSScriptRoot 'steam-appearance\backend.path'

function Get-ThemeRevision($Rows) {
    $inputText=[IO.Path]::GetFullPath($script:ThemeRoot).ToLowerInvariant()+'|'+(($Rows|ForEach-Object {$_.name+':'+$_.hash}) -join '|')
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($inputText)))).Replace('-','').ToLowerInvariant() } finally {$sha.Dispose()}
}
function Get-ThemeTargetId {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($script:ThemeRoot).ToLowerInvariant())))).Replace('-','').Substring(0,16)}finally{$sha.Dispose()}
}
function Get-ConfigHash([string]$Path) {
    # .NET hashing works even when a Core host passed PSModulePath to Windows PowerShell.
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
    try {return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
}
function Get-ThemeRows {
    $contentIndex=Get-AppearanceContentIndex
    foreach($name in $script:ThemeNames) {
        $dir=Join-Path $script:ThemeRoot $name; $config=Join-Path $dir 'config_USER.json'; $manifest=Join-Path $dir 'theme.json'
        # Missing files use the all-on preset selection; installed stays false.
        # Otherwise first takeover would replay synthetic off flags over the preset.
        $row=[ordered]@{name=$name;installed=$false;configured=$true;backendEnabled=$null;parameterCount=0;hash='missing';error='';path=$config;raw='';config=$null;renderPlan=$null}
        try {
            if((Test-Path -LiteralPath $manifest -PathType Leaf) -and (Test-Path -LiteralPath $config -PathType Leaf)) {
                if((Get-Item -LiteralPath $config).Length -gt 262144){throw 'Theme config too large'}
                if(Get-Command Assert-ThemeTarget -ErrorAction SilentlyContinue){Assert-ThemeTarget $config|Out-Null;Assert-ThemeTarget $manifest|Out-Null}
                $row.raw=[IO.File]::ReadAllText($config,[Text.Encoding]::UTF8);$row.config=$row.raw|ConvertFrom-Json
                if($row.config.active -isnot [bool]){throw 'Invalid active field'}
                $row.configured=$row.config.active;$row.hash=(Get-ConfigHash $config)
                $j=[IO.File]::ReadAllText($manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json
                if($j.name -ne $name){throw 'Theme manifest/name mismatch'}
                $row.parameterCount=if($j.patches){@($j.patches.PSObject.Properties).Count}else{0};$row.installed=$true
                $row.renderPlan=Get-AppearanceThemeRenderPlan $name $j $row.config $contentIndex
            }
        }catch{$row.error=$_.Exception.Message}
        [pscustomobject]$row
    }
}
$script:CurrentSessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId
$script:CurrentUserSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$script:ProcessOwnerCache=@{}
function Test-CurrentUserProcess($Process) {
    if(!$Process -or $Process.SessionId -ne $script:CurrentSessionId){return $false}
    try{
        $key=[string]$Process.Id+':'+$Process.StartTime.ToUniversalTime().Ticks
        if($script:ProcessOwnerCache.ContainsKey($key)){return $script:ProcessOwnerCache[$key]}
        $native=Get-CimInstance -ClassName Win32_Process -Filter ('ProcessId = '+[int]$Process.Id) -OperationTimeoutSec 2 -ErrorAction Stop
        if(!$native){throw 'Process no longer exists'}
        $identity=Invoke-CimMethod -InputObject $native -MethodName GetOwnerSid -OperationTimeoutSec 2 -ErrorAction Stop
        if($identity.ReturnValue -ne 0 -or !$identity.Sid){throw 'Owner SID unavailable'}
        $mine=$identity.Sid -eq $script:CurrentUserSid;$script:ProcessOwnerCache[$key]=$mine;return $mine
    }catch{throw ('无法核实加载器/Steam 进程所属用户，未发出控制操作：'+$_.Exception.Message)}
}
function Get-BackendProcesses {
    # Do not use default tasklist TABLE: it truncates this 32-character image name.
    @(Get-Process -Name 'CssLoader-Standalone-Headless' -ErrorAction SilentlyContinue|Where-Object {
        if(!(Test-CurrentUserProcess $_)){return $false}
        try {$image=Get-AppearanceProcessPath $_;[IO.Path]::GetFullPath($image) -eq [IO.Path]::GetFullPath($script:BackendExe)}catch{$true}
    })
}
function Test-BackendOwner {
    $listeners=@(Get-NetTCPConnection -State Listen -LocalPort 35821 -ErrorAction SilentlyContinue)
    foreach($listener in $listeners) {
        if($listener.LocalAddress -ne '127.0.0.1'){continue}
        $process=Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
        if($process -and (Test-CurrentUserProcess $process)){
            $image=Get-AppearanceProcessPath $process
            if([IO.Path]::GetFullPath($image) -eq [IO.Path]::GetFullPath($script:BackendExe)){return $true}
        }
    }
    return $false
}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}) {
    if($script:AppearanceObservationOnly -and $Method -notin @('get_themes','fetch_theme_path')){throw 'Read-only appearance RPC denied'}
    if($Method -notin @('dummy_function','get_themes','fetch_theme_path','set_theme_state','reset')){throw 'RPC method denied'}
    if($Method -eq 'set_theme_state' -and ($Arguments.name -notin $script:ThemeNames -or $Arguments.state -isnot [bool])){throw 'Theme RPC arguments denied'}
    if(!(Test-BackendOwner)){throw 'CSSLoader API owner changed; request was not sent'}
    if($Method -eq 'reset' -and @($Arguments.Keys).Count){throw 'Reload RPC arguments denied'}
    $timeout=if($Method -eq 'reset'){20}elseif($Method -eq 'set_theme_state'){12}else{6}
    if($script:RpcDeadline){$remaining=($script:RpcDeadline-[DateTime]::UtcNow).TotalSeconds;if($remaining -lt 1){throw '接口结果未确认，请读取状态后重试'};$timeout=[Math]::Min($timeout,[Math]::Ceiling($remaining))}
    $body=@{method=$Method;args=$Arguments}|ConvertTo-Json -Depth 8 -Compress
    $response=Invoke-RestMethod -Uri 'http://127.0.0.1:35821/req' -Method Post -Body ([Text.Encoding]::UTF8.GetBytes($body)) -ContentType 'application/json; charset=utf-8' -TimeoutSec $timeout
    if($response.success -ne $true){throw "CSSLoader: $($response.res)"}
    if($response.res -is [pscustomobject] -and $null -ne $response.res.PSObject.Properties['success'] -and $response.res.success -ne $true){throw "CSSLoader: $($response.res.message)"}
    return $response.res
}
function Get-AppearanceStatus {
    if(!$script:AppearanceObservationOnly -and (Get-Command Recover-PresetTransaction -ErrorAction SilentlyContinue)){Recover-PresetTransaction}
    $ready=$false;$note='';$backendThemes=@();$processCount=@(Get-BackendProcesses).Count
    if($processCount -gt 0) {
        try {
            $path=[string](Invoke-LoaderRpc 'fetch_theme_path')
            if(![IO.Path]::IsPathRooted($path) -or [IO.Path]::GetFullPath($path).TrimEnd('\').Length -lt 8){throw 'Invalid CSSLoader theme path'}
            $script:ThemeRoot=[IO.Path]::GetFullPath($path).TrimEnd('\');if(!$script:AppearanceObservationOnly -and (Get-Command Recover-PresetTransaction -ErrorAction SilentlyContinue)){Recover-PresetTransaction}
            $backendThemes=@(Invoke-LoaderRpc 'get_themes');if(!$script:AppearanceObservationOnly -and (Get-Command Save-PresetTargetHint -ErrorAction SilentlyContinue)){Save-PresetTargetHint};$ready=$true
        }catch{$note=$_.Exception.Message}
    }
    # Resolve the real backend target before reading configs, and read them once.
    # Each RPC still verifies its current listener owner in Invoke-LoaderRpc.
    $rows=@(Get-ThemeRows)
    foreach($row in $rows){$found=@($backendThemes|Where-Object name -eq $row.name);if($found.Count -eq 1 -and $found[0].enabled -is [bool]){$row.backendEnabled=$found[0].enabled}}
    $matched=$ready -and @($rows|Where-Object installed).Count -gt 0 -and @($rows|Where-Object {$_.installed -and ($null -eq $_.backendEnabled -or $_.configured -ne $_.backendEnabled)}).Count -eq 0
    $steamRunning=@(Get-Process -Name 'steam' -ErrorAction SilentlyContinue|Where-Object {Test-CurrentUserProcess $_}).Count -gt 0
    # Historical idle markers cannot confirm the resident policy.
    $idle=$false
    $snapshot=[pscustomobject]@{controlProtocol=2;targetId=(Get-ThemeTargetId);revision=(Get-ThemeRevision $rows);linked=(Test-Path -LiteralPath $script:LinkFile -PathType Leaf);backendReady=$ready;backendIdle=[bool]$idle;backendProcessCount=$processCount;steamRunning=$steamRunning;configuredMatchesBackend=$matched;note=$note;themes=@($rows|Select-Object name,installed,configured,backendEnabled,parameterCount,error,renderPlan)}
    if(Get-Command Get-PresetMetadata -ErrorAction SilentlyContinue){
        $meta=Get-PresetMetadata $rows $backendThemes
        foreach($property in $meta.PSObject.Properties){$snapshot|Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value -Force}
        if($snapshot.backendReloadRequired){$snapshot.configuredMatchesBackend=$false}
    }
    $snapshot
}
function Write-ConfigAtomic([string]$Path,[string]$Text) {
    if($script:AppearanceObservationOnly){throw 'Read-only appearance file write denied'}
    $temp=$Path+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp'; $old=$temp+'.bak'
    try {
        [IO.File]::WriteAllText($temp,$Text,(New-Object Text.UTF8Encoding($false)))
        if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temp,$Path,$old)}else{[IO.File]::Move($temp,$Path)}
    }finally {if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp -Force};if(Test-Path -LiteralPath $old){Remove-Item -LiteralPath $old -Force}}
}
function Write-ThemeConfigAtomic([string]$Path,[string]$Text) {
    if(!(Get-Command Assert-ThemeTarget -ErrorAction SilentlyContinue)){throw 'Preset safety module missing; theme writes are disabled'}
    Write-ConfigAtomic (Assert-ThemeTarget $Path) $Text
}
function Sync-AppearanceBackend($Rows,[bool]$Force=$false,[string[]]$ForceNames=@()) {
    try {
        $current=@(Invoke-LoaderRpc 'get_themes')
        # Art Hero first, then reconcile every independently chosen dependency.
        foreach($row in $Rows) {
            if(!$row.installed){continue}
            $found=@($current|Where-Object name -eq $row.name)
            if($found.Count -ne 1 -or $found[0].enabled -isnot [bool]){throw "Backend theme missing/invalid: $($row.name)"}
            if($Force -or $row.name -in $ForceNames -or $found[0].enabled -ne $row.configured) {
                Invoke-LoaderRpc 'set_theme_state' @{name=$row.name;state=[bool]$row.configured;set_deps=$false;set_deps_value=$false}|Out-Null
                $current=@(Invoke-LoaderRpc 'get_themes')
            }
        }
        foreach($row in $Rows|Where-Object installed){$found=@($current|Where-Object name -eq $row.name);if($found.Count -ne 1 -or $found[0].enabled -isnot [bool] -or $found[0].enabled -ne $row.configured){throw "Backend did not adopt selected state: $($row.name)"}}
    }catch{
        $problem=$_.Exception.Message;$repairErrors=New-Object Collections.Generic.List[string]
        # A loader failure can cascade into config writes even during Refresh/Ensure.
        # Restore only active flags, preserving all independently stored parameters.
        foreach($desired in $Rows|Where-Object installed){
            try{
                Assert-ThemeTarget $desired.path|Out-Null
                $live=[IO.File]::ReadAllText($desired.path,[Text.Encoding]::UTF8)|ConvertFrom-Json
                if($live.active -ne $desired.configured){$live.active=[bool]$desired.configured;Write-ThemeConfigAtomic $desired.path ($live|ConvertTo-Json -Depth 64 -Compress)}
            }catch{$repairErrors.Add($_.Exception.Message)}
        }
        if($repairErrors.Count){$problem+='; some disk states could not be preserved: '+($repairErrors -join '; ')}
        throw $problem
    }
}
function Assert-AppearanceApplyReady($Current) {
    # Pending reload cannot prevent saving switches when the backend is absent.
    # A live stale backend still requires a verified reload before RPC writes.
    if($Current.packPrepared -eq $false -or $Current.packRepairRequired -or $Current.foreignActiveCount -gt 0 -or ($Current.backendReloadRequired -and $Current.backendReady)){throw '外观环境已变化，请先重新应用 YMCC 预设后再切换'}
}
function Invoke-AppearanceApply($Request) {
    if(Get-Command Read-PresetState -ErrorAction SilentlyContinue){$owner=Read-PresetState;if($owner -and $owner.owned){Read-PresetBackup ([string]$owner.originalBackup)|Out-Null}}
    if(!$Request -or !$Request.states -or $Request.expectedRevision -isnot [string]){throw 'Invalid appearance request'}
    $changes=@($Request.states.PSObject.Properties)
    if($changes.Count -lt 1 -or $changes.Count -gt $script:ThemeNames.Count){throw 'Invalid theme count'}
    foreach($item in $changes){if($item.Name -notin $script:ThemeNames -or $item.Value -isnot [bool]){throw 'Theme/value denied'}}
    $rows=@(Get-ThemeRows)
    if((Get-ThemeRevision $rows) -ne $Request.expectedRevision){throw 'Theme settings changed; reread before applying'}
    foreach($item in $changes){$row=$rows|Where-Object name -eq $item.Name;if(!$row.installed){throw "Theme unavailable: $($item.Name)"};Assert-ThemeTarget $row.path|Out-Null}
    $undo=New-PresetBackup @($rows|Where-Object installed|ForEach-Object path)
    Begin-PresetTransaction $undo
    try {
        foreach($item in $changes){
            $row=$rows|Where-Object name -eq $item.Name
            if(!$row.installed){throw "Theme unavailable: $($item.Name)"}
            if($row.configured -eq $item.Value){continue}
            $row.config.active=$item.Value; $row.configured=$item.Value
            $text=$row.config|ConvertTo-Json -Depth 64 -Compress
            Write-ThemeConfigAtomic $row.path $text
        }
    }catch {
        $problem=$_.Exception.Message;try{Recover-PresetTransaction -Force}catch{throw ($problem+'; '+$_.Exception.Message)};throw $problem
    }
    $status=Get-AppearanceStatus
    if($status.backendReady) {
        try {Sync-AppearanceBackend $rows; $status=Get-AppearanceStatus}
        catch {
            $problem=$_.Exception.Message;Complete-PresetTransaction
            return [pscustomobject]@{ok=$false;saved=$true;message=('配置已保存，重新应用失败：'+$problem);snapshot=(Get-AppearanceStatus)}
        }
        if(!$status.configuredMatchesBackend){Complete-PresetTransaction;return [pscustomobject]@{ok=$false;saved=$true;message='配置已保存，但加载器状态未确认；请重新应用';snapshot=$status}}
    }
    Complete-PresetTransaction
    if($status.backendReady -and $status.steamRunning){
        $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
        return [pscustomobject]@{ok=[bool]$render.confirmed;saved=$true;renderConfirmed=[bool]$render.confirmed;message=$(if($render.confirmed){'配置已保存，各主题样式已确认'}else{'配置已保存，页面样式尚未确认：'+$render.reason});snapshot=$status}
    }
    [pscustomobject]@{ok=$true;saved=$true;message='配置已保存，等待 Steam / CSSLoader 启动';snapshot=$status}
}
function Get-AppearanceConfigValue($Value,[int]$Depth=0) {
    if($Depth -gt 64){throw 'Theme config is too deeply nested'}
    if($null -eq $Value){return 'null'}
    if($Value -is [pscustomobject]){
        $pairs=@(foreach($property in @($Value.PSObject.Properties|Sort-Object Name -CaseSensitive)){
            ($property.Name|ConvertTo-Json -Compress)+':'+(Get-AppearanceConfigValue $property.Value ($Depth+1))
        })
        return '{'+($pairs -join ',')+'}'
    }
    if($Value -is [System.Array]){
        $items=@(foreach($item in $Value){Get-AppearanceConfigValue $item ($Depth+1)})
        return '['+($items -join ',')+']'
    }
    return ($Value|ConvertTo-Json -Compress)
}
function Invoke-AppearanceReload($Request=$null) {
    # Explicit resource repair only. The existing verified backend rereads disk;
    # no helper launch, Desktop termination, CDP style deletion or Steam startup.
    $before=Get-AppearanceStatus
    if(!$before.backendReady){throw 'CSSLoader 尚未就绪，无法在原进程重读主题'}
    if($before.packPrepared -ne $true -or $before.packRepairRequired -or $before.foreignActiveCount -gt 0){throw '请先准备完整的 YMCC 主题包，再重新应用'}
    if($Request -and ($Request.expectedRevision -ne $before.revision -or $Request.targetId -ine $before.targetId)){throw 'Theme settings or target changed; reread before reloading'}
    $desired=@(Get-ThemeRows)
    if(@($desired|Where-Object {!$_.installed -or $_.error}).Count){throw 'Managed theme files unreadable; no reload was sent'}
    $values=@{};foreach($row in $desired){$values[$row.name]=Get-AppearanceConfigValue $row.config}
    $result=Invoke-LoaderRpc 'reset'
    if(!$result -or $null -eq $result.PSObject.Properties['fails'] -or @($result.fails).Count){throw 'CSSLoader theme reread returned unconfirmed load errors'}
    $after=Get-AppearanceStatus
    if(!$after.backendReady -or $after.targetId -ine $before.targetId){throw 'Theme target changed during reload; configuration preserved'}
    # The loader rewrites JSON formatting while loading. Compare parsed values
    # so whitespace is harmless, but a real concurrent user edit is rejected.
    foreach($row in Get-ThemeRows){if(!$row.installed -or (Get-AppearanceConfigValue $row.config) -cne $values[$row.name]){throw 'Theme settings changed during reload; configuration preserved'}}
    foreach($row in $after.themes){if(!$row.installed -or $row.backendEnabled -isnot [bool]){throw ('CSSLoader did not load bundled theme: '+$row.name)}}
    Sync-AppearanceBackend $desired
    Complete-PresetReload
    $confirmed=Get-AppearanceStatus
    if(!$confirmed.configuredMatchesBackend){throw 'CSSLoader theme reread did not confirm the selected states'}
    [pscustomobject]@{ok=$true;reloaded=$true;previousRevision=$before.revision;started=$false;message='已在现有 CSSLoader 中重读主题，无需重启 Steam';snapshot=$confirmed}
}
function New-AppearanceMutex([string]$Purpose) {
    if($Purpose -notin @('Launch','ThemeControl')){throw 'Invalid appearance mutex purpose'}
    # Production names remain fixed. Isolated tests replace only this OS boundary.
    New-Object Threading.Mutex($false,('Local\YMCC.CssLoader.'+$Purpose))
}
function Start-AppearanceBackend {
    $mutex=New-AppearanceMutex 'Launch';$locked=$false
    try {
        try{$locked=$mutex.WaitOne(1000)}catch [Threading.AbandonedMutexException]{$locked=$true}
        if(!$locked){throw 'CSSLoader launch busy; no duplicate instance started'}
        if(!(Test-Path -LiteralPath $script:BackendExe -PathType Leaf)){throw 'CSSLoader executable missing'}
        $state=if(Get-Command Read-PresetState -ErrorAction SilentlyContinue){Read-PresetState}else{$null}
        if($state -and $state.owned){Read-PresetBackup ([string]$state.originalBackup)|Out-Null}
        $reload=$state -and $state.pendingReload
        $existing=@(Get-BackendProcesses)
        # Refuse competing installations too, including one still opening its port.
        $others=@(Get-Process -Name 'CssLoader-Standalone-Headless' -ErrorAction SilentlyContinue|Where-Object {Test-CurrentUserProcess $_})
        foreach($process in $others){
            $image=Get-AppearanceProcessPath $process
            if([IO.Path]::GetFullPath($image) -ine [IO.Path]::GetFullPath($script:BackendExe)){throw 'Another CSSLoader path is active; no duplicate instance started'}
        }
        if(!$existing.Count){
            Assert-CssDesktopNotEditing
            if(@(Get-NetTCPConnection -State Listen -LocalPort 35821 -ErrorAction SilentlyContinue).Count){throw 'CSSLoader port 35821 is occupied; no duplicate instance started'}
            Start-ResidentAppearanceBackend
        }
        # A launching, unresponsive or stale existing process is never permission
        # to terminate it or create another. Only a bounded readiness wait.
        $deadline=[DateTime]::UtcNow.AddSeconds(12)
        do {
            $running=@(Get-BackendProcesses)
            if($running.Count -and (Test-BackendOwner)){
                Assert-SingleAppearanceBackend $running
                if($reload -and !(Test-CssDesktopEditing)){return Invoke-AppearanceReload}
                return [pscustomobject]@{ok=$true;started=(!$existing.Count);resident=$true;message='CSSLoader verified; reusing the single resident backend'}
            }
            if([DateTime]::UtcNow -ge $deadline){break}
            Start-Sleep -Milliseconds 500
        }while($true)
        throw 'Existing CSSLoader is not ready; retained without launching a second instance'
    }finally{if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
}
function Write-JsonResult($Value) {
    # shell.run decodes OEM bytes. ASCII JSON escapes prevent UTF-8/GBK mojibake.
    $text=$Value|ConvertTo-Json -Depth 12 -Compress
    $escape=[Text.RegularExpressions.MatchEvaluator]{param($match) '\u{0:x4}' -f [int][char]$match.Value}
    [Console]::WriteLine([regex]::Replace($text,'[^\x00-\x7F]',$escape))
}
# Module parse failures happen before the action dispatcher: always return JSON.
try {
$appearanceLoadingComponent='steam-appearance\preset.ps1'
$presetModule=Join-Path $PSScriptRoot 'steam-appearance\preset.ps1'
if(!(Test-Path -LiteralPath $presetModule -PathType Leaf)){throw 'Appearance component missing'}
. $presetModule
$appearanceLoadingComponent='steam-appearance\resident.ps1'
$residentModule=Join-Path $PSScriptRoot 'steam-appearance\resident.ps1'
if(!(Test-Path -LiteralPath $residentModule -PathType Leaf)){throw 'Appearance component missing'}
. $residentModule
# Reuse an already installed backend even when another user installed it outside C:\SOFT.
$portOwners=@()
if(!$BackendPath){$portOwners=@(Get-NetTCPConnection -State Listen -LocalPort 35821 -ErrorAction SilentlyContinue)}
if(!$BackendPath){foreach($listener in $portOwners){if($listener.LocalAddress -eq '127.0.0.1'){try{$process=Get-Process -Id $listener.OwningProcess -ErrorAction Stop;if(Test-CurrentUserProcess $process){$image=Get-AppearanceProcessPath $process;if([IO.Path]::GetFileName($image) -ieq 'CssLoader-Standalone-Headless.exe'){$script:BackendExe=$image;break}}}catch{}}}}
if(!$BackendPath -and !$portOwners.Count -and (Get-Command Read-PresetState -ErrorAction SilentlyContinue)){
    $remembered=$null;try{$remembered=Read-PresetState}catch{}
    if($remembered -and $remembered.backendExe -and [IO.Path]::GetFileName([string]$remembered.backendExe) -ieq 'CssLoader-Standalone-Headless.exe' -and (Test-Path -LiteralPath $remembered.backendExe -PathType Leaf)){$script:BackendExe=[string]$remembered.backendExe}
}
$appearanceLoadingComponent='steam-appearance\idle.ps1'
$idleModule=Join-Path $PSScriptRoot 'steam-appearance\idle.ps1'
if(!(Test-Path -LiteralPath $idleModule -PathType Leaf)){throw 'Appearance component missing'}
. $idleModule
}catch {
    Write-JsonResult ([pscustomobject]@{ok=$false;message=('外观脚本加载失败：'+$appearanceLoadingComponent+'。文件缺失或损坏，请修复外观组件后重试。')})
    exit 1
}
function Invoke-AutomaticAppearanceRefresh {
    # Reuse the finite startup window for read-only renderer readiness. A
    # correction is submitted at most once; later offsets verify without replay.
    $script:AppearanceHelperPhase=3;$script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0;$script:AppearanceHelperBackendCount=0
    $expected=Get-AppearanceSteamSession
    if(!$expected -or !$SessionPid -or [uint32]$expected.pid -ne $SessionPid -or [string]$expected.birth -ne $SessionBirth){$script:AppearanceFailureCode=1;throw 'Automatic appearance Steam session changed'}
    $started=[DateTime]::UtcNow;$corrected=$false;$correctedRevision='';$correctedTarget=''
    foreach($offset in @(0,2000,7000,15000,24000)){
        $delay=$offset-([DateTime]::UtcNow-$started).TotalMilliseconds
        if($delay -gt 0){Start-Sleep -Milliseconds ([int][Math]::Ceiling($delay))}
        $current=Get-AppearanceSteamSession
        if(!$current -or [uint32]$current.pid -ne $SessionPid -or [string]$current.birth -ne $SessionBirth -or $current.root -ne $expected.root){throw 'Automatic appearance Steam session changed'}
        $script:AppearanceHelperPhase=4
        $status=Invoke-AppearanceControl 'Status'
        if(!$status.ok -or !$status.snapshot){throw 'Automatic appearance status unconfirmed'}
        $script:AppearanceHelperFlags=if($status.snapshot.backendReady){1}else{0}
        if($status.snapshot.configuredMatchesBackend){$script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 2}
        $script:AppearanceHelperBackendCount=[Math]::Min(128,[Math]::Max(0,[int]$status.snapshot.backendProcessCount))
        if(!$status.snapshot.backendReady){continue}
        if($status.snapshot.packError){throw $status.snapshot.packError}
        if($corrected -and ($status.snapshot.revision -ne $correctedRevision -or $status.snapshot.targetId -ne $correctedTarget)){throw 'Appearance changed after the one correction; no automatic replay'}
        if(!$corrected -and ($status.snapshot.packPrepared -eq $false -or $status.snapshot.packRepairRequired -or $status.snapshot.foreignActiveCount -gt 0)){
            $prepared=Invoke-AppearanceControl 'Prepare'
            if(!$prepared.ok -or !$prepared.snapshot){throw 'Automatic appearance preset preparation unconfirmed'}
            $status=$prepared
        }
        if(!$corrected -and $status.snapshot.backendReloadRequired){
            $loaded=Invoke-AppearanceControl 'Reload'
            if(!$loaded.ok){throw 'Automatic appearance resource reread unconfirmed'}
            $status=$loaded
        }
        $script:AppearanceHelperPhase=5
        $render=Get-AppearanceRenderedStatus $current $status.snapshot
        if($render.confirmed){$script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 4}
        if($status.snapshot.configuredMatchesBackend -and !$status.snapshot.backendReloadRequired -and $render.confirmed){return $status}
        if($corrected -or !$render.checked){continue}
        # A verified receipt mismatch or backend flag drift admits one targeted
        # transaction. Unknown/starting CDP is only observed at the finite offsets.
        $applied=Invoke-AppearanceControl 'Reconcile'
        $corrected=$true
        if(!$applied.snapshot){throw 'Automatic appearance correction unconfirmed'}
        $correctedRevision=$applied.snapshot.revision;$correctedTarget=$applied.snapshot.targetId
        if($applied.ok -and $applied.renderConfirmed -and $applied.snapshot.configuredMatchesBackend){return $applied}
    }
    $script:AppearanceFailureCode=if($script:AppearanceHelperPhase -eq 5){6}else{3}
    throw 'Automatic appearance not confirmed within the finite loading window; configuration and resident CSS retained'
}
if($Action -eq 'Library'){return}
# Automatic result protocol 2: eight Int32 fields, bounded and no user data.
$script:AutomaticResultProtocol=2
$script:AutomaticResultMap=$null
$script:AppearanceHelperPhase=0;$script:AppearanceFailureCode=0
$script:AppearanceHelperFlags=0;$script:AppearanceHelperBackendCount=0
function Write-AppearanceHelperResult([bool]$Success,[int]$HResult=0) {
    if(!$script:AutomaticResultMap){return}
    $view=$null
    try {
        $view=$script:AutomaticResultMap.CreateViewAccessor(0,32,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
        $reason=if($Success){0}elseif($script:AppearanceFailureCode){[int]$script:AppearanceFailureCode}else{99}
        $view.Write(4,[int]2);$view.Write(8,[int]$script:AppearanceHelperPhase);$view.Write(12,[int]$reason)
        $view.Write(16,[int]$script:AppearanceHelperFlags);$view.Write(20,[int]$script:AppearanceHelperBackendCount)
        $view.Write(24,[int]$HResult);$view.Write(28,[int]0)
        $view.Write(0,[int]0x594D4353);$view.Flush()
    }finally{if($view){$view.Dispose()}}
}
function Invoke-AppearanceControl([string]$ControlAction) {
if($script:AppearanceObservationOnly -and $ControlAction -notin @('Idle','Status')){throw 'Read-only appearance control denied'}
$controlMutex=$null;$controlLocked=$false
try {
    if($ControlAction -in @('Status','Ensure','UpdateOwnedResources','Apply','Refresh','Reload','Prepare','Reset','Restore','Idle','Reconcile')) {
        $script:RpcDeadline=[DateTime]::UtcNow.AddSeconds(35)
        $controlMutex=New-AppearanceMutex 'ThemeControl'
        try{$controlLocked=$controlMutex.WaitOne(3000)}catch [Threading.AbandonedMutexException]{$controlLocked=$true}
        if(!$controlLocked){$script:AppearanceFailureCode=11;throw 'Theme operation busy; retry after rereading'}
    }
    if(Get-Command Initialize-PresetTargetHint -ErrorAction SilentlyContinue){Initialize-PresetTargetHint -KeepBackendPath:([bool]$BackendPath -or [bool]$portOwners.Count)}
    switch($ControlAction) {
        Status {$result=[pscustomobject]@{ok=$true;snapshot=(Get-AppearanceStatus)}}
        Idle {
            # Idle itself checks editor ownership before and after observation.
            if(!(Get-Command Invoke-AppearanceIdle -ErrorAction SilentlyContinue)){throw 'CSS idle component missing'}
            $revision='';$target=''
            if($Payload){if($Payload.Length -gt 2048){throw 'Idle request too large'};$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;$revision=[string]$request.expectedRevision;$target=[string]$request.targetId;$SessionPid=[uint32]$request.expectedPid;$SessionBirth=[string]$request.expectedBirth;if($revision -notmatch '^[a-f0-9]{64}$' -or $target -notmatch '^[a-f0-9]{16}$'){throw 'Invalid idle request'}}
            if(!$SessionPid -or $SessionBirth -notmatch '^[1-9]\d{0,19}$'){throw 'Idle session required'}
            $result=Invoke-AppearanceIdle $SessionPid $SessionBirth $revision $target
        }
        Ensure {$current=Get-AppearanceStatus;$current=Invoke-OwnedPresetResourceUpdate $current;$result=Start-AppearanceBackend}
        # Internal Automatic branch for an already-running backend. This action
        # is deliberately absent from the public -Action ValidateSet.
        UpdateOwnedResources {
            $before=Get-AppearanceStatus
            $current=Invoke-OwnedPresetResourceUpdate $before
            $changed=($before.packUpdateAvailable -or $before.packRepairRequired) -and !$current.packUpdateAvailable -and !$current.packRepairRequired -and $current.packPrepared
            if($changed -and $current.backendReady){$result=Invoke-AppearanceReload}
            else{$result=[pscustomobject]@{ok=$true;saved=[bool]$changed;snapshot=$current;message='Existing theme resources checked without starting a backend'}}
        }
        Reload {
            $request=$null
            if($Payload){if($Payload.Length -gt 2048){throw 'Reload request too large'};$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;if($request.expectedRevision -notmatch '^[a-fA-F0-9]{64}$' -or $request.targetId -notmatch '^[a-fA-F0-9]{16}$'){throw 'Invalid reload request'}}
            $result=Invoke-AppearanceReload $request
        }
        Prepare {
            Get-AppearanceStatus|Out-Null
            $expected=''
            if($Payload){$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;$expected=[string]$request.expectedRevision}
            $result=Invoke-PresetPrepare $false $expected
        }
        Reset {Get-AppearanceStatus|Out-Null;$result=Invoke-PresetPrepare $true}
        Restore {Get-AppearanceStatus|Out-Null;$result=Invoke-PresetRestore}
        Apply {
            if($Payload.Length -gt 22000){throw 'Request too large'}
            $request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json
            $current=Get-AppearanceStatus
            Assert-AppearanceApplyReady $current
            $result=Invoke-AppearanceApply $request
        }
        Reconcile {
            $status=Get-AppearanceStatus
            if($Payload){
                if($Payload.Length -gt 2048){throw 'Reconcile request too large'}
                $request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json
                if($request.expectedRevision -notmatch '^[a-f0-9]{64}$' -or $request.targetId -notmatch '^[a-f0-9]{16}$' -or $request.expectedRevision -ne $status.revision -or $request.targetId -ne $status.targetId){throw 'Appearance changed before reconciliation'}
            }
            if(!$status.backendReady){throw 'Existing CSS backend unavailable; no backend was started'}
            if(Test-CssDesktopEditing){throw 'CSSLoader Desktop is editing; reconciliation postponed'}
            if(!$status.packOwned){throw 'Appearance target is not owned; reconciliation skipped'}
            $status=Invoke-OwnedPresetResourceUpdate $status
            if($status.backendReloadRequired){$loaded=Invoke-AppearanceReload;if(!$loaded.ok){throw 'Appearance reread unconfirmed'};$status=$loaded.snapshot}
            if(!$status.backendReady -or $status.packPrepared -eq $false -or $status.packRepairRequired -or $status.backendReloadRequired -or $status.foreignActiveCount){throw 'Appearance reconciliation prerequisites changed'}
            # A generic style count cannot prove each managed theme reached the
            # document. Reconcile only flags or per-theme receipts proven wrong.
            $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
            if(!$render.checked -and $status.configuredMatchesBackend){throw ('Appearance DOM unconfirmed; no blind theme writes: '+$render.reason)}
            # DOM IO can span a user edit or Desktop opening. Do not authorize
            # writes from the old receipt plan after either boundary changed.
            Assert-CssDesktopNotEditing
            $currentRows=@(Get-ThemeRows)
            if((Get-ThemeRevision $currentRows) -ne $status.revision -or (Get-ThemeTargetId) -ne $status.targetId){throw 'Appearance changed during DOM verification; no correction submitted'}
            Sync-AppearanceBackend $currentRows $false @($render.mismatched)
            $status=Get-AppearanceStatus
            $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
            $confirmed=$status.configuredMatchesBackend -and $render.confirmed
            $result=[pscustomobject]@{ok=$confirmed;snapshot=$status;renderConfirmed=[bool]$render.confirmed;message=$(if($confirmed){'Appearance differences reconciled and each theme receipt confirmed'}else{'Configuration retained; theme rendering remains unconfirmed: '+$render.reason})}
        }
        Refresh {
            $status=Get-AppearanceStatus
            if(!$status.backendReady){throw 'CSSLoader 尚未就绪，请先开启联动或启动美化后端'}
            if($status.packPrepared -eq $false -or $status.packRepairRequired -or $status.foreignActiveCount -gt 0 -or $status.backendReloadRequired){throw '外观环境已变化，请先重新套用 YMCC 预设'}
            # Global reset touches broken/blank CEF targets on the installed backend.
            # Reapply the managed theme group through the engine's own transactions instead.
            Sync-AppearanceBackend @(Get-ThemeRows) $true
            $status=Get-AppearanceStatus
            $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
            $confirmed=$status.configuredMatchesBackend -and $render.confirmed
            $result=[pscustomobject]@{ok=[bool]$confirmed;saved=$true;renderConfirmed=[bool]$render.confirmed;message=$(if($confirmed){'已重新应用，各主题样式已确认'}else{'配置已保留，重新应用后页面样式尚未确认：'+$render.reason});snapshot=$status}
        }
    }
    return $result
}finally {if($controlMutex){if($controlLocked){$controlMutex.ReleaseMutex()};$controlMutex.Dispose()}}
}
try {
    if($Action -eq 'Automatic'){
        if(!$SessionPid -or $SessionBirth -notmatch '^[1-9]\d{0,19}$'){throw 'Automatic session required'}
        if($ReadyEvent -cnotmatch '^Local\\YMCC\.CssUse\.[0-9a-f]{32}\.ready$' -or
           $IdleEvent -cne ($ReadyEvent.Substring(0,$ReadyEvent.Length-6)+'.idle')){throw 'Invalid automatic use events'}
        $ready=$null;$gate=$null
        try {
            # The one-argument overload works in both Windows PowerShell and Core; native ACLs grant only this user.
            $ready=[Threading.EventWaitHandle]::OpenExisting($ReadyEvent)
            $gate=[Threading.EventWaitHandle]::OpenExisting($IdleEvent)
            $script:AutomaticResultMap=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(($ReadyEvent.Substring(0,$ReadyEvent.Length-6)+'.result'),[IO.MemoryMappedFiles.MemoryMappedFileRights]::ReadWrite)
            $script:AppearanceHelperPhase=1
            if(!$VerifyOnly){
                if($StartBackend){$initial=Invoke-AppearanceControl 'Ensure'}
                else{$initial=Invoke-AppearanceControl 'UpdateOwnedResources'}
                if(!$initial.ok){$script:AppearanceFailureCode=14;throw 'Automatic CSS initialization unconfirmed'}
                Invoke-AutomaticAppearanceRefresh|Out-Null
            }
            # No theme/launch mutex spans this wait. Native immediately releases
            # the same helper for resident confirmation; CSS has no exit timer.
            if(!$ready.Set()){throw 'Automatic ready signal failed'}
            $script:AppearanceHelperPhase=2
            # Only the helper is Job-owned. -1 is an event-only wait bounded by
            # the native request deadline; releasing it never closes CSS.
            if(!$gate.WaitOne($GateTimeoutMs)){$script:AppearanceFailureCode=13;throw 'Automatic idle gate expired'}
            $result=Invoke-AppearanceControl 'Idle'
        }finally {if($ready){$ready.Dispose()};if($gate){$gate.Dispose()}}
    }else{$result=Invoke-AppearanceControl $Action}
    if($Action -eq 'Automatic'){
        if($result.ok){$script:AppearanceHelperPhase=8}
        Write-AppearanceHelperResult ([bool]$result.ok)
    }
    Write-JsonResult $result
    if(!$result.ok){exit 1}
}catch {
    $failure=$_
    try{Write-AppearanceHelperResult $false ([int]$failure.Exception.HResult)}catch{}
    Write-JsonResult ([pscustomobject]@{ok=$false;message=$failure.Exception.Message});exit 1
}finally{if($script:AutomaticResultMap){$script:AutomaticResultMap.Dispose()}}