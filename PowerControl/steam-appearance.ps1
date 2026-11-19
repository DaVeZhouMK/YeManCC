# YMCC Steam appearance: bounded RPC, preserved patches, cross-process singleton.
[CmdletBinding()]
param(
    [ValidateSet('Status','Apply','Refresh','Reload','Ensure','Prepare','Reset','Restore','Idle','Reconcile','Automatic','Library')][string]$Action='Status',
    [string]$Payload='',
    [uint32]$SessionPid=0,
    [string]$SessionBirth='',
    [string]$BackendPath='',
    [string]$ProcessQuery='',
    [string]$ReadyEvent='',
    [string]$IdleEvent='',
    [ValidateRange(-1,120000)][int]$GateTimeoutMs=-1,
    [switch]$StartBackend,
    [switch]$VerifyOnly
)
$ErrorActionPreference='Stop'
$script:AppearanceTimingClock=$null
$script:AppearanceTiming=$null
$script:AppearanceTimingFirstControl=$false
$script:AppearanceFirstStatusParts=$null
$script:AppearanceCorrectionTiming=$null
$script:AppearanceMeasureCorrectionWrites=$false
if($Action -eq 'Automatic' -and !$VerifyOnly){
    $script:AppearanceTimingClock=[Diagnostics.Stopwatch]::StartNew()
    # Entry to refresh, first control, mutex wait, REST total/count, first DOM, entry to ready.
    $script:AppearanceTiming=[long[]]@(-1,-1,0,0,0,-1,-1)
    # Existing identity requests, backend candidates, RPC owner checks, theme state.
    $script:AppearanceFirstStatusParts=[long[]]::new(4)
}
$script:ResidentBackendPolicy=3
$script:SharedProcessQueryProtocol=1
$script:SharedSteamReadProtocol=1
$script:AppearanceListenerQueryProtocol=0
$script:ManualObservationProtocol=1
$script:AutomaticInitializeGateProtocol=1
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
    $inputText=[IO.Path]::GetFullPath($script:ThemeRoot).ToLowerInvariant()+'|'+(($Rows|ForEach-Object {$_.name+':'+$_.hash+':'+$_.manifestHash}) -join '|')
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($inputText)))).Replace('-','').ToLowerInvariant() } finally {$sha.Dispose()}
}
function Get-ThemeTargetId {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($script:ThemeRoot).ToLowerInvariant())))).Replace('-','').Substring(0,16)}finally{$sha.Dispose()}
}
function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0,$ObservedLength=$null) {
    # .NET hashing works even when a Core host passed PSModulePath to Windows PowerShell.
    $sha=[Security.Cryptography.SHA256]::Create();$stream=$null
    try {
        $stream=[IO.File]::OpenRead($Path)
        if($MaximumBytes -gt 0){
            $length=$stream.Length
            if($length -gt $MaximumBytes){throw 'Preset backup integrity check failed'}
            if($null -ne $ObservedLength){$ObservedLength.Value=$length}
        }
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')
    }finally{if($stream){$stream.Dispose()};$sha.Dispose()}
}
function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest) {
    # Parse and revision must describe the same bytes, including any BOM.
    # The bounded read handle denies writers until the snapshot is captured.
    $stream=$null;$buffer=$null;$reader=$null;$sha=$null
    try {
        $stream=[IO.File]::OpenRead($Path)
        if($stream.Length -gt 262144){if($Manifest){throw 'Theme manifest too large'};throw 'Theme config too large'}
        $buffer=[IO.MemoryStream]::new([int]$stream.Length)
        $stream.CopyTo($buffer,4096)
        $buffer.Position=0
        $reader=[IO.StreamReader]::new($buffer,[Text.Encoding]::UTF8,$true)
        $raw=$reader.ReadToEnd()
        $sha=[Security.Cryptography.SHA256]::Create()
        [pscustomobject]@{raw=$raw;hash=([BitConverter]::ToString($sha.ComputeHash($buffer.GetBuffer(),0,[int]$buffer.Length))).Replace('-','')}
    }finally {
        if($sha){$sha.Dispose()};if($reader){$reader.Dispose()};if($buffer){$buffer.Dispose()};if($stream){$stream.Dispose()}
    }
}
function Get-ThemeRows {
    param([switch]$RevisionOnly,[switch]$DigestOnly)
    if($DigestOnly -and !$RevisionOnly){throw 'Theme digests require revision-only mode'}
    $contentIndex=Get-AppearanceContentIndex
    $targetValidator=Get-Command Assert-ThemeTarget -ErrorAction SilentlyContinue
    foreach($name in $script:ThemeNames) {
        $dir=Join-Path $script:ThemeRoot $name; $config=Join-Path $dir 'config_USER.json'; $manifest=Join-Path $dir 'theme.json'
        # Missing files use the all-on preset selection; installed stays false.
        # Otherwise first takeover would replay synthetic off flags over the preset.
        $row=[ordered]@{name=$name;installed=$false;configured=$true;backendEnabled=$null;parameterCount=0;hash='missing';manifestHash='missing';error='';path=$config;raw='';config=$null;renderPlan=$null}
        try {
            if($targetValidator){
                # The path guard also returns the leaf type; no separate provider probe.
                $manifestItem=$null;$configItem=$null
                $null=Assert-ThemeTarget $manifest -ResourceItem ([ref]$manifestItem)
                if($manifestItem -is [IO.FileInfo]){$null=Assert-ThemeTarget $config -ResourceItem ([ref]$configItem)}
                $hasBoth=($configItem -is [IO.FileInfo])
            }else{
                $hasBoth=(Test-Path -LiteralPath $manifest -PathType Leaf) -and (Test-Path -LiteralPath $config -PathType Leaf)
            }
            if($hasBoth -and $DigestOnly) {
                # A prepared status already parsed these bytes. Compare fresh
                # bounded digests; this branch never supplies settings or plans.
                $row.hash=Get-ConfigHash $config -MaximumBytes 262144
                $row.manifestHash=Get-ConfigHash $manifest -MaximumBytes 262144
            }elseif($hasBoth) {
                $snapshot=Read-AppearanceThemeConfig $config
                $row.raw=$snapshot.raw;$row.config=$row.raw|ConvertFrom-Json
                if($row.config.active -isnot [bool]){throw 'Invalid active field'}
                $row.configured=$row.config.active;$row.hash=$snapshot.hash
                # The rendered plan is tied to the same manifest bytes the
                # final read will compare, not just to the saved user config.
                $manifestSnapshot=Read-AppearanceThemeConfig $manifest -Manifest
                $j=$manifestSnapshot.raw|ConvertFrom-Json
                if($j.name -ne $name){throw 'Theme manifest/name mismatch'}
                $row.manifestHash=$manifestSnapshot.hash
                $row.parameterCount=if($j.patches){@($j.patches.PSObject.Properties).Count}else{0};$row.installed=$true
                # Final revision confirmation needs fresh config and path/index
                # validation, but does not consume another render plan.
                if(!$RevisionOnly){$row.renderPlan=Get-AppearanceThemeRenderPlan $name $j $row.config $contentIndex}
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
        Initialize-ResidentAppearanceInterop
        $identity=[Ymcc.CssResidentProcess]::Identity([uint32]$Process.Id,[long]$Process.StartTime.ToUniversalTime().ToFileTimeUtc())
        $mine=$identity[1] -eq $script:CurrentUserSid;$script:ProcessOwnerCache[$key]=$mine;return $mine
    }catch{throw ('无法核实加载器/Steam 进程所属用户，未发出控制操作：'+$_.Exception.Message)}
}
function Get-BackendProcesses {
    # Do not use default tasklist TABLE: it truncates this 32-character image name.
    @(Get-AppearanceBackendCandidates|Where-Object {
        if(!(Test-CurrentUserProcess $_)){return $false}
        try {$image=Get-AppearanceProcessPath $_;[IO.Path]::GetFullPath($image) -eq [IO.Path]::GetFullPath($script:BackendExe)}catch{$true}
    })
}
function Get-AppearanceTcpListeners([ValidateRange(1,65535)][int]$LocalPort) {
    # A fresh, bounded native listener table replaces repeated CIM cmdlet work.
    # Query failure stays Unknown; never infer that an occupied port is absent.
    Initialize-ResidentAppearanceInterop
    [Ymcc.CssResidentProcess]::Listeners($LocalPort)
}
function Test-BackendOwner([object[]]$ExpectedFamily) {
    $bound=$PSBoundParameters.ContainsKey('ExpectedFamily')
    if($bound -and !$ExpectedFamily.Count){return $false}
    $listeners=@(Get-AppearanceTcpListeners -LocalPort 35821)
    if($bound){
        # Final confirmation must identify the listener in this same family.
        $loopback=@($listeners|Where-Object LocalAddress -eq '127.0.0.1')
        if(!$loopback.Count){return $false}
        $listenerPid=$loopback[0].OwningProcess
        foreach($row in $loopback){if($row.OwningProcess -ne $listenerPid){return $false}}
        $member=@($ExpectedFamily|Where-Object Id -eq $listenerPid)
        if($member.Count -ne 1){return $false}
        $listeners=@($loopback[0])
    }
    foreach($listener in $listeners) {
        if($listener.LocalAddress -ne '127.0.0.1'){continue}
        $process=Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
        if($process -and (Test-CurrentUserProcess $process)){
            if($bound -and ($process.HasExited -or $member[0].HasExited -or
               $process.StartTime.ToUniversalTime() -ne $member[0].StartTime.ToUniversalTime())){return $false}
            $image=Get-AppearanceProcessPath $process
            if([IO.Path]::GetFullPath($image) -eq [IO.Path]::GetFullPath($script:BackendExe)){return $true}
        }
    }
    return $false
}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{},[ref]$WriteAttempted,[int]$CorrectionReason=0,[switch]$RequireCurrentSteamScope) {
    if($Method -in @('set_theme_state','reset')){
        try{
            $session=Get-AppearanceSteamSession
            if($RequireCurrentSteamScope -and !$session){throw 'Automatic correction Steam session disappeared'}
        }
        catch{$_.Exception.Data['YmccAppearanceSteamScopeRejected']=$true;throw}
    }
    if($script:AppearanceObservationOnly -and $Method -notin @('get_themes','fetch_theme_path')){throw 'Read-only appearance RPC denied'}
    if($Method -notin @('dummy_function','get_themes','fetch_theme_path','set_theme_state','reset')){throw 'RPC method denied'}
    if($Method -eq 'set_theme_state' -and ($Arguments.name -notin $script:ThemeNames -or $Arguments.state -isnot [bool])){throw 'Theme RPC arguments denied'}
    $measureOwner=$script:AppearanceTimingFirstControl -and $null -ne $script:AppearanceFirstStatusParts
    if($measureOwner){$ownerStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    try{if(!(Test-BackendOwner)){throw 'CSSLoader API owner changed; request was not sent'}}
    finally{if($measureOwner){$script:AppearanceFirstStatusParts[2]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$ownerStart}}
    if($Method -eq 'reset' -and @($Arguments.Keys).Count){throw 'Reload RPC arguments denied'}
    $timeout=if($Method -eq 'reset'){20}elseif($Method -eq 'set_theme_state'){12}else{6}
    if($script:RpcBudgetClock){$remaining=35-$script:RpcBudgetClock.Elapsed.TotalSeconds;if($remaining -lt 1){throw '接口结果未确认，请读取状态后重试'};$timeout=[Math]::Min($timeout,[Math]::Ceiling($remaining))}
    $body=@{method=$Method;args=$Arguments}|ConvertTo-Json -Depth 8 -Compress
    $bodyBytes=[Text.Encoding]::UTF8.GetBytes($body)
    $measureRead=$script:AppearanceTimingFirstControl -and $Method -in @('fetch_theme_path','get_themes')
    if($measureRead){$rpcStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    try{
        # Once handed to HTTP, even a lost reply may have changed the backend.
        $measureSet=$script:AppearanceMeasureCorrectionWrites -and $WriteAttempted -and $Method -eq 'set_theme_state'
        if($measureSet){
            if(!$script:AppearanceCorrectionTiming[4]){$script:AppearanceCorrectionTiming[2]=$script:AppearanceTimingClock.ElapsedMilliseconds}
            $script:AppearanceCorrectionTiming[4]++
            if($CorrectionReason -ge 1 -and $CorrectionReason -le 3){$script:AppearanceCorrectionTiming[6+$CorrectionReason]++}
        }
        if($WriteAttempted -and $Method -eq 'set_theme_state'){$WriteAttempted.Value=$true}
        $response=Invoke-RestMethod -Uri 'http://127.0.0.1:35821/req' -Method Post -Body $bodyBytes -ContentType 'application/json; charset=utf-8' -TimeoutSec $timeout
    }finally{
        if($measureRead){$script:AppearanceTiming[3]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$rpcStart;$script:AppearanceTiming[4]++}
    }
    if($response.success -isnot [bool] -or !$response.success){throw "CSSLoader: $($response.res)"}
    if($response.res -is [pscustomobject] -and $null -ne $response.res.PSObject.Properties['success'] -and ($response.res.success -isnot [bool] -or !$response.res.success)){throw "CSSLoader: $($response.res.message)"}
    if($measureSet){
        $script:AppearanceCorrectionTiming[3]=$script:AppearanceTimingClock.ElapsedMilliseconds
        $script:AppearanceCorrectionTiming[5]++
    }
    return $response.res
}
function Get-AppearanceStatus([string]$ExpectedCorrectionRoot='',[ref]$CapturedRows,[switch]$RevisionOnly,$FinalIdleIdentity=$null) {
    $measureStatus=$script:AppearanceTimingFirstControl -and $null -ne $script:AppearanceFirstStatusParts
    if($measureStatus){$partStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    $admitted=Get-AppearanceSteamSession
    if($FinalIdleIdentity){
        if(!$admitted -or $admitted.pid -ne $FinalIdleIdentity.pid -or $admitted.birth -cne $FinalIdleIdentity.birth){
            Stop-AppearanceIdle 1 'Steam session changed; CSS remains resident'
        }
        $script:AppearanceHelperPhase=4
    }
    if($measureStatus){$script:AppearanceFirstStatusParts[0]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$partStart}
    $readOnly=$script:AppearanceObservationOnly -or [bool]$ExpectedCorrectionRoot
    if(!$readOnly -and (Get-Command Recover-PresetTransaction -ErrorAction SilentlyContinue)){Recover-PresetTransaction}
    $ready=$false;$note='';$backendThemes=@()
    if($measureStatus){$partStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    $processCount=@(Get-BackendProcesses).Count
    if($measureStatus){$script:AppearanceFirstStatusParts[1]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$partStart}
    if($processCount -gt 0) {
        $resolvedRoot=''
        try {
            $path=[string](Invoke-LoaderRpc 'fetch_theme_path')
            if(![IO.Path]::IsPathRooted($path) -or [IO.Path]::GetFullPath($path).TrimEnd('\').Length -lt 8){throw 'Invalid CSSLoader theme path'}
            $resolvedRoot=[IO.Path]::GetFullPath($path).TrimEnd('\')
        }catch{$note=$_.Exception.Message}
        if($resolvedRoot){
            if($ExpectedCorrectionRoot -and $resolvedRoot -ine $ExpectedCorrectionRoot){Stop-AppearanceIdle 4 'Appearance target changed during automatic CSS correction; no replay'}
            $script:ThemeRoot=$resolvedRoot
            # A refused rollback is not an unavailable backend. Stop before any
            # further preparation or correction on the unresolved transaction.
            if(!$readOnly -and (Get-Command Recover-PresetTransaction -ErrorAction SilentlyContinue)){Recover-PresetTransaction}
            try{
                $backendThemes=@(Invoke-LoaderRpc 'get_themes');if(!$readOnly -and (Get-Command Save-PresetTargetHint -ErrorAction SilentlyContinue)){Save-PresetTargetHint};$ready=$true
            }catch{$note=$_.Exception.Message}
        }
    }
    if($ExpectedCorrectionRoot){
        if($script:ThemeRoot -ine $ExpectedCorrectionRoot){Stop-AppearanceIdle 4 'Appearance target changed during automatic CSS correction; no replay'}
        $journal=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'transaction.json')
        if(Test-Path -LiteralPath $journal -ErrorAction Stop){Stop-AppearanceIdle 3 'Appearance transaction unfinished; CSS remains resident'}
    }
    # Resolve the real backend target before reading configs, and read them once.
    # Each RPC still verifies its current listener owner in Invoke-LoaderRpc.
    if($measureStatus){$partStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    $rows=@(Get-ThemeRows -RevisionOnly:$RevisionOnly)
    if($measureStatus){$script:AppearanceFirstStatusParts[3]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$partStart}
    if($CapturedRows){$CapturedRows.Value=$rows}
    foreach($row in $rows){$found=@($backendThemes|Where-Object name -eq $row.name);if($found.Count -eq 1 -and $found[0].enabled -is [bool]){$row.backendEnabled=$found[0].enabled}}
    $matched=$ready -and @($rows|Where-Object installed).Count -gt 0 -and @($rows|Where-Object {$_.installed -and ($null -eq $_.backendEnabled -or $_.configured -ne $_.backendEnabled)}).Count -eq 0
    if($measureStatus){$partStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
    $currentSession=Get-AppearanceSteamSession
    if($FinalIdleIdentity){
        if(!$currentSession -or $currentSession.pid -ne $admitted.pid -or $currentSession.birth -cne $admitted.birth -or $currentSession.root -ine $admitted.root){
            Stop-AppearanceIdle 1 'Steam session changed before DOM confirmation; CSS remains resident'
        }
        $FinalIdleIdentity.session=$currentSession
    }
    $steamRunning=$null -ne $currentSession
    if($measureStatus){$script:AppearanceFirstStatusParts[0]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$partStart}
    # Historical idle markers cannot confirm the resident policy.
    $idle=$false
    $snapshot=[pscustomobject]@{controlProtocol=2;targetId=(Get-ThemeTargetId);revision=(Get-ThemeRevision $rows);linked=(Test-Path -LiteralPath $script:LinkFile -PathType Leaf);backendReady=$ready;backendIdle=[bool]$idle;backendProcessCount=$processCount;steamRunning=$steamRunning;configuredMatchesBackend=$matched;note=$note;themes=@($rows|Select-Object name,installed,configured,backendEnabled,parameterCount,error,renderPlan)}
    if(Get-Command Get-PresetMetadata -ErrorAction SilentlyContinue){
        if($measureStatus){$partStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
        $meta=Get-PresetMetadata $rows $backendThemes
        if($measureStatus){$script:AppearanceFirstStatusParts[3]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$partStart}
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
    $null=Get-AppearanceSteamSession
    if(!(Get-Command Assert-ThemeTarget -ErrorAction SilentlyContinue)){throw 'Preset safety module missing; theme writes are disabled'}
    Write-ConfigAtomic (Assert-ThemeTarget $Path) $Text
}
function Sync-AppearanceBackend($Rows,[bool]$Force=$false,[string[]]$ForceNames=@(),[switch]$RequireCurrentSteamScope,[object[]]$InitialThemes=$null,[switch]$ProtectEditor) {
    $writeAttempted=$false
    $editorInterrupted=$false
    try {
        $current=$InitialThemes
        if($null -eq $current){$current=@(Invoke-LoaderRpc 'get_themes')}
        # Art Hero first, then reconcile every independently chosen dependency.
        foreach($row in $Rows) {
            if(!$row.installed){continue}
            $found=@($current|Where-Object name -eq $row.name)
            if($found.Count -ne 1 -or $found[0].enabled -isnot [bool]){throw "Backend theme missing/invalid: $($row.name)"}
            $receiptMismatch=$row.name -in $ForceNames
            $flagMismatch=$found[0].enabled -ne $row.configured
            if($Force -or $receiptMismatch -or $flagMismatch) {
                if($ProtectEditor -or ($RequireCurrentSteamScope -and $writeAttempted)){
                    # Reload's initial readback and later submissions can outlast editor admission.
                    try{Assert-CssDesktopNotEditing}catch{$editorInterrupted=$true;throw}
                }
                $correctionReason=([int]$flagMismatch) -bor (([int]$receiptMismatch) -shl 1)
                # The preceding readback can outlive the scope; validate at the RPC boundary for every automatic write.
                Invoke-LoaderRpc 'set_theme_state' @{name=$row.name;state=[bool]$row.configured;set_deps=$false;set_deps_value=$false} -WriteAttempted ([ref]$writeAttempted) -CorrectionReason $correctionReason -RequireCurrentSteamScope:$RequireCurrentSteamScope|Out-Null
                $current=@(Invoke-LoaderRpc 'get_themes')
            }
        }
        foreach($row in $Rows|Where-Object installed){$found=@($current|Where-Object name -eq $row.name);if($found.Count -ne 1 -or $found[0].enabled -isnot [bool] -or $found[0].enabled -ne $row.configured){throw "Backend did not adopt selected state: $($row.name)"}}
    }catch{
        if($RequireCurrentSteamScope -and $_.Exception.Data['YmccAppearanceSteamScopeRejected'] -eq $true){
            # A fresh identity after this denial cannot authorize rollback of the old choices.
            $script:AppearanceFailureCode=1
            throw
        }
        if(!$writeAttempted -or $editorInterrupted){throw}
        $problem=$_.Exception.Message
        # A submitted RPC can outlast the editor check that admitted it.
        try{Assert-CssDesktopNotEditing}catch{throw ($problem+'; '+$_.Exception.Message)}
        $repairErrors=New-Object Collections.Generic.List[string]
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
function Invoke-AppearanceReload($Request=$null,[switch]$ProtectEditor) {
    # Explicit resource repair only. The existing verified backend rereads disk;
    # no helper launch, Desktop termination, CDP style deletion or Steam startup.
    $before=Get-AppearanceStatus
    if(!$before.backendReady){throw 'CSSLoader 尚未就绪，无法在原进程重读主题'}
    if($before.packPrepared -ne $true -or $before.packRepairRequired -or $before.foreignActiveCount -gt 0){throw '请先准备完整的 YMCC 主题包，再重新应用'}
    if($Request -and ($Request.expectedRevision -ne $before.revision -or $Request.targetId -ine $before.targetId)){throw 'Theme settings or target changed; reread before reloading'}
    # Another control action may have completed the reload before this lock.
    # Let the automatic caller confirm DOM and reconcile actual drift normally.
    if($ProtectEditor -and $before.backendReloadRequired -is [bool] -and !$before.backendReloadRequired){
        return [pscustomobject]@{ok=$true;reloaded=$false;previousRevision=$before.revision;started=$false;message='No pending automatic CSS reload';snapshot=$before}
    }
    $desired=@(Get-ThemeRows)
    if(@($desired|Where-Object {!$_.installed -or $_.error}).Count){throw 'Managed theme files unreadable; no reload was sent'}
    if($ProtectEditor -and (Get-ThemeRevision $desired) -cne $before.revision){throw 'Theme settings or target changed; reread before reloading'}
    $values=@{};foreach($row in $desired){$values[$row.name]=Get-AppearanceConfigValue $row.config}
    # Automatic repair does not own an open Desktop editor. Recheck at the
    # mutation boundary, including when the editor opened during status reads.
    if($ProtectEditor){Assert-CssDesktopNotEditing}
    $result=Invoke-LoaderRpc 'reset'
    if(!$result -or $null -eq $result.PSObject.Properties['fails'] -or @($result.fails).Count){throw 'CSSLoader theme reread returned unconfirmed load errors'}
    # Only the final snapshot is returned for render confirmation. These two
    # fresh checks consume backend flags and parsed config, not page plans.
    $after=Get-AppearanceStatus -RevisionOnly
    if(!$after.backendReady -or $after.targetId -ine $before.targetId){throw 'Theme target changed during reload; configuration preserved'}
    # The pending reload is expected here, but observed preset drift must stop follow-up writes.
    if($after.packPrepared -ne $true -or $after.packRepairRequired -or $after.packError -or $after.foreignActiveCount){
        Stop-AppearanceIdle 3 'Theme preset changed during reload; CSS remains resident'
    }
    # The loader rewrites JSON formatting while loading. Compare parsed values
    # so whitespace is harmless, but a real concurrent user edit is rejected.
    foreach($row in Get-ThemeRows -RevisionOnly){if(!$row.installed -or (Get-AppearanceConfigValue $row.config) -cne $values[$row.name]){throw 'Theme settings changed during reload; configuration preserved'}}
    foreach($row in $after.themes){if(!$row.installed -or $row.backendEnabled -isnot [bool]){throw ('CSSLoader did not load bundled theme: '+$row.name)}}
    if($ProtectEditor){Assert-CssDesktopNotEditing}
    Sync-AppearanceBackend $desired -ProtectEditor:$ProtectEditor
    if($ProtectEditor){Assert-CssDesktopNotEditing}
    Complete-PresetReload
    $confirmed=Get-AppearanceStatus
    # Matching backend flags do not prove that the original preset is still loaded.
    if($confirmed.targetId -cne $before.targetId){Stop-AppearanceIdle 4 'Theme target changed before reload confirmation; configuration preserved'}
    if($confirmed.packPrepared -ne $true -or $confirmed.packRepairRequired -or $confirmed.packError -or $confirmed.foreignActiveCount -or $confirmed.backendReloadRequired){
        Stop-AppearanceIdle 3 'Theme preset changed before reload confirmation; CSS remains resident'
    }
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
        $null=Update-AppearanceProcessInventory
        $hasBackend=@(Get-BackendProcesses).Count -gt 0
        # Refuse competing installations too, including one still opening its port.
        $backendPath=[IO.Path]::GetFullPath($script:BackendExe)
        foreach($process in @(Get-AppearanceBackendCandidates)){
            if(!(Test-CurrentUserProcess $process)){continue}
            $image=Get-AppearanceProcessPath $process
            if([IO.Path]::GetFullPath($image) -ine $backendPath){throw 'Another CSSLoader path is active; no duplicate instance started'}
            # The last candidate read can see a backend absent on the first.
            $hasBackend=$true
        }
        if(!$hasBackend){
            Assert-CssDesktopNotEditing
            if(@(Get-AppearanceTcpListeners -LocalPort 35821).Count){throw 'CSSLoader port 35821 is occupied; no duplicate instance started'}
            Start-ResidentAppearanceBackend
        }
        # A launching, unresponsive or stale existing process is never permission
        # to terminate it or create another. Only a bounded readiness wait.
        $clock=[Diagnostics.Stopwatch]::StartNew()
        do {
            $running=@(Get-BackendProcesses)
            if($running.Count -and (Test-BackendOwner -ExpectedFamily $running)){
                Assert-SingleAppearanceBackend $running
                if($reload -and !(Test-CssDesktopEditing)){return Invoke-AppearanceReload -ProtectEditor}
                return [pscustomobject]@{ok=$true;started=(!$hasBackend);resident=$true;message='CSSLoader verified; reusing the single resident backend'}
            }
            if($clock.ElapsedMilliseconds -ge 12000){break}
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
if($script:AppearanceListenerQueryProtocol -ne 1){throw 'Appearance listener-query component mismatch'}
if($script:AppearanceProcessQueryProtocol -ne 3){throw 'Appearance public Steam channel component mismatch'}
# Reuse an already installed backend even when another user installed it outside C:\SOFT.
$portOwners=@()
if(!$BackendPath -and $Action -ne 'Library'){$portOwners=@(Get-AppearanceTcpListeners -LocalPort 35821)}
if(!$BackendPath){foreach($listener in $portOwners){if($listener.LocalAddress -eq '127.0.0.1'){try{$process=Get-Process -Id $listener.OwningProcess -ErrorAction Stop;if(Test-CurrentUserProcess $process){$image=Get-AppearanceProcessPath $process;if([IO.Path]::GetFileName($image) -ieq 'CssLoader-Standalone-Headless.exe'){$script:BackendExe=$image;break}}}catch{}}}}
if(!$BackendPath -and !$portOwners.Count -and (Get-Command Read-PresetState -ErrorAction SilentlyContinue)){
    $remembered=$null;try{$remembered=Read-PresetState}catch{}
    if($remembered -and $remembered.backendExe -and [IO.Path]::GetFileName([string]$remembered.backendExe) -ieq 'CssLoader-Standalone-Headless.exe' -and (Test-Path -LiteralPath $remembered.backendExe -PathType Leaf)){$script:BackendExe=[string]$remembered.backendExe}
}
$appearanceLoadingComponent='steam-appearance\idle.ps1'
$idleModule=Join-Path $PSScriptRoot 'steam-appearance\idle.ps1'
if(!(Test-Path -LiteralPath $idleModule -PathType Leaf)){throw 'Appearance component missing'}
. $idleModule
if($script:AppearanceSharedSteamProtocol -ne 1){throw 'Appearance shared document consumer mismatch'}
}catch {
    Write-JsonResult ([pscustomobject]@{ok=$false;message=('外观脚本加载失败：'+$appearanceLoadingComponent+'。文件缺失或损坏，请修复外观组件后重试。')})
    exit 1
}
function Invoke-AutomaticAppearanceRefresh {
    param([switch]$UpdateOwnedResources)
    if($script:AppearanceTiming){$script:AppearanceTiming[0]=$script:AppearanceTimingClock.ElapsedMilliseconds}
    # Reuse the finite startup window for read-only renderer readiness. A
    # correction is submitted at most once. A transient document read can defer it once.
    $script:AppearanceHelperPhase=3;$script:AppearanceFailureCode=0;$script:AppearanceHelperFlags=0;$script:AppearanceHelperBackendCount=0
    $script:AppearanceRenderTrace=[int[]]::new(10)
    $script:AppearanceRenderAttempts=0;$script:AppearanceCorrectionAttempt=0
    $script:AppearanceCorrectionTiming=$null
    $expected=Get-AppearanceSteamSession
    if(!$expected -or !$SessionPid -or [uint32]$expected.pid -ne $SessionPid -or [string]$expected.birth -ne $SessionBirth){$script:AppearanceFailureCode=1;throw 'Automatic appearance Steam session changed'}
    # A wall-clock correction after wake must not stretch the renderer window.
    $clock=[Diagnostics.Stopwatch]::StartNew();$correctionChecked=$false;$deferredCorrection=$false;$correctionSynchronized=$false;$correctedRevision='';$correctedTarget='';$correctedRoot=''
    $offsets=@(0,2000,7000,15000,24000)
    for($i=0;$i -lt $offsets.Count;$i++){
        $offset=$offsets[$i]
        $beforeWait=$clock.Elapsed.TotalMilliseconds
        $delay=$offset-$beforeWait
        $slot=2*$script:AppearanceRenderAttempts
        $script:AppearanceRenderAttempts++
        # 1 interrupted, 2 backend not ready for confirmation, 3 document unconfirmed,
        # 4 style mismatch, 5 matched, 6 backend drift with matching styles.
        $script:AppearanceRenderTrace[$slot+1]=1
        if($delay -gt 0){
            Start-Sleep -Milliseconds ([int][Math]::Ceiling($delay))
            $now=$clock.Elapsed.TotalMilliseconds
            $script:AppearanceRenderTrace[$slot]=[int][Math]::Min(180000,($now-$beforeWait))
        }else{$now=$beforeWait}
        if($i -gt 0){
            # A slow read or late wake can cross several retry offsets. One
            # fresh observation covers all overdue slots, not a burst of reads.
            while($i+1 -lt $offsets.Count -and $now -ge $offsets[$i+1]){$i++}
        }
        # The first slot never waits; its status still checks live identity before backend work.
        $current=if($offset -eq 0){$expected}else{Get-AppearanceSteamSession}
        if(!$current -or [uint32]$current.pid -ne $SessionPid -or [string]$current.birth -ne $SessionBirth -or $current.root -ne $expected.root){throw 'Automatic appearance Steam session changed'}
        $script:AppearanceHelperPhase=4
        # Initialization already reads the complete current status under the
        # existing control mutex. Use that same result for the first DOM probe;
        # later offsets and the final Idle still read everything afresh.
        $measureFirst=$null -ne $script:AppearanceTiming -and $offset -eq 0
        if($measureFirst){$controlStart=$script:AppearanceTimingClock.ElapsedMilliseconds;$script:AppearanceTimingFirstControl=$true}
        try{
            $status=if($UpdateOwnedResources -and $offset -eq 0){
                $initial=Invoke-AppearanceControl 'UpdateOwnedResources'
                if(!$initial.ok){$script:AppearanceFailureCode=14;throw 'Automatic CSS initialization unconfirmed'}
                $initial
            }elseif($correctionChecked){
                Invoke-AppearanceControl 'Status' -CorrectionObservationRoot $correctedRoot
            }else{Invoke-AppearanceControl 'Status'}
        }finally{
            if($measureFirst){$script:AppearanceTimingFirstControl=$false;$script:AppearanceTiming[1]=$script:AppearanceTimingClock.ElapsedMilliseconds-$controlStart}
        }
        if(!$status.ok -or !$status.snapshot){throw 'Automatic appearance status unconfirmed'}
        $script:AppearanceHelperFlags=if($status.snapshot.backendReady){1}else{0}
        if($status.snapshot.configuredMatchesBackend){$script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 2}
        $script:AppearanceHelperBackendCount=[Math]::Min(128,[Math]::Max(0,[int]$status.snapshot.backendProcessCount))
        if(!$status.snapshot.backendReady){$script:AppearanceRenderTrace[$slot+1]=2;continue}
        if($status.snapshot.packError){throw $status.snapshot.packError}
        if($correctionChecked -and ($status.snapshot.revision -ne $correctedRevision -or $status.snapshot.targetId -ne $correctedTarget)){throw 'Appearance changed after the one correction; no automatic replay'}
        $needsPrepare=$status.snapshot.packPrepared -eq $false -or $status.snapshot.packRepairRequired -or $status.snapshot.foreignActiveCount -gt 0
        if($needsPrepare){
            # Resource or ownership drift can leave the config revision unchanged.
            if($correctionChecked){Stop-AppearanceIdle 3 'Appearance preset changed while waiting for rendering; CSS remains resident'}
            $prepared=Invoke-AppearanceControl 'Prepare' -ProtectEditor -AutomaticPrepareScope @{root=$script:ThemeRoot;revision=$status.snapshot.revision;targetId=$status.snapshot.targetId}
            if(!$prepared.ok -or !$prepared.snapshot){throw 'Automatic appearance preset preparation unconfirmed'}
            $status=$prepared
        }
        if(!$correctionChecked -and $status.snapshot.backendReloadRequired){
            $loaded=Invoke-AppearanceControl 'Reload' -ProtectEditor -AutomaticReloadStatus $status.snapshot
            if(!$loaded.ok){throw 'Automatic appearance resource reread unconfirmed'}
            $status=$loaded
        }
        $script:AppearanceHelperPhase=5
        # Retrying document readiness cannot fix an unsupported saved option.
        # Inspect the current managed rows only after any preset preparation or reload.
        foreach($row in $status.snapshot.themes){
            if($row.installed -eq $true -and $row.error -ceq 'render-patch-selection-invalid'){
                $script:AppearanceFailureCode=6
                throw 'Automatic appearance contains an unsupported theme option; configuration retained'
            }
        }
        if($correctionSynchronized -and !$status.snapshot.configuredMatchesBackend){
            # No second write is admitted. A fresh backend match is required before another DOM batch can confirm anything.
            $script:AppearanceHelperPhase=4
            $script:AppearanceRenderTrace[$slot+1]=2
            continue
        }
        if($measureFirst){$domStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
        $observedRoot=$script:ThemeRoot
        $render=Get-AppearanceRenderedStatus $current $status.snapshot
        if($measureFirst){$script:AppearanceTiming[5]=$script:AppearanceTimingClock.ElapsedMilliseconds-$domStart}
        if($render.confirmed){$script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 4}
        if($status.snapshot.configuredMatchesBackend -and !$status.snapshot.backendReloadRequired -and $render.confirmed){$script:AppearanceRenderTrace[$slot+1]=5;return $status}
        $script:AppearanceRenderTrace[$slot+1]=if(!$render.checked){3}elseif(!$render.confirmed){4}else{6}
        if(($correctionChecked -and !$deferredCorrection) -or !$render.checked){continue}
        # A verified receipt mismatch or backend flag drift admits one targeted
        # transaction. Unknown/starting CDP is only observed at the finite offsets.
        $script:AppearanceCorrectionAttempt=$script:AppearanceRenderAttempts
        if($script:AppearanceTimingClock){
            # Report the last correction check, including a deferred submission if any.
            $script:AppearanceCorrectionTiming=[long[]]@($script:AppearanceTimingClock.ElapsedMilliseconds,-1,-1,-1,0,0,1,0,0,0)
        }
        try{$applied=Invoke-AppearanceControl 'Reconcile' -AutomaticCorrectionStatus $status.snapshot -DeferredCorrection:$deferredCorrection -CorrectionObservationRoot $observedRoot}
        finally{if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[1]=$script:AppearanceTimingClock.ElapsedMilliseconds}}
        if(!$applied.snapshot){throw 'Automatic appearance correction unconfirmed'}
        $deferredCorrection=!$correctionChecked -and $applied.canDeferCorrection -eq $true
        $correctionChecked=$true
        $correctionSynchronized=$applied.correctionSynchronized -eq $true
        $correctedRoot=$observedRoot
        $correctedRevision=$applied.snapshot.revision;$correctedTarget=$applied.snapshot.targetId
        if($applied.ok -and $applied.renderConfirmed -and $applied.snapshot.configuredMatchesBackend){return $applied}
    }
    $script:AppearanceFailureCode=if($script:AppearanceHelperPhase -eq 5){6}else{3}
    throw 'Automatic appearance not confirmed within the finite loading window; configuration and resident CSS retained'
}
function Invoke-AutomaticAppearanceIdle($ConfirmedSnapshot) {
    if($ConfirmedSnapshot.revision -isnot [string] -or $ConfirmedSnapshot.revision -cnotmatch '^[a-f0-9]{64}$' -or
       $ConfirmedSnapshot.targetId -isnot [string] -or $ConfirmedSnapshot.targetId -notmatch '^[a-f0-9]{16}$'){
        Stop-AppearanceIdle 4 'Approved appearance scope missing; CSS remains resident'
    }
    $wasObservationOnly=$script:AppearanceObservationOnly
    try {
        # Native releases this gate to verify the approved appearance, not recover transactions.
        $script:AppearanceObservationOnly=$true
        $result=Invoke-AppearanceControl 'Idle' -AutomaticIdleScope $ConfirmedSnapshot -DeferIdleMismatch
        if(!$result.retryDom){return $result}
        # A new context can still be loading its styles. Do not hold ThemeControl while it settles.
        Start-Sleep -Milliseconds 500
        Invoke-AppearanceControl 'Idle' -AutomaticIdleScope $ConfirmedSnapshot
    }finally{$script:AppearanceObservationOnly=$wasObservationOnly}
}
if($Action -eq 'Library'){return}
# Automatic result protocol 2: eight Int32 fields, bounded and no user data.
$script:AutomaticResultProtocol=2
$script:AutomaticResultMap=$null
$script:AppearanceHelperPhase=0;$script:AppearanceFailureCode=0
$script:AppearanceHelperFlags=0;$script:AppearanceHelperBackendCount=0
$script:AppearanceRenderAttempts=0
function Write-AppearanceHelperResult([bool]$Success,[int]$HResult=0) {
    if(!$script:AutomaticResultMap){return}
    $view=$null
    try {
        if($script:AppearanceRenderAttempts){
            # Optional tail, advertised by Native. The v2 prefix (including
            # its reserved zero) is still sufficient for either generation.
            try {
                $view=$script:AutomaticResultMap.CreateViewAccessor(0,88,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
                if($view.ReadInt32(32) -eq 0x594D5452){
                    $view.Write(40,[int]$script:AppearanceRenderAttempts)
                    $view.Write(44,[int]$script:AppearanceCorrectionAttempt)
                    for($i=0;$i -lt 2*$script:AppearanceRenderAttempts;$i++){$view.Write((48+4*$i),$script:AppearanceRenderTrace[$i])}
                    $view.Write(36,[int]0x594D5431)
                }
            }catch{} # Diagnostics cannot fail the original receipt.
        }
        if($script:AppearanceTiming -and $script:AppearanceTiming[5] -ge 0 -and $script:AppearanceTiming[6] -ge 0 -and $script:AppearanceTiming[6] -le 180000){
            $timingView=$null
            try{
                $timingView=$script:AutomaticResultMap.CreateViewAccessor(0,0,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
                if($timingView.Capacity -ge 124 -and $timingView.ReadInt32(88) -eq 0x594D544F){
                    for($i=0;$i -lt 7;$i++){$timingView.Write((96+4*$i),[int]$script:AppearanceTiming[$i])}
                    $timingView.Write(92,[int]0x594D5450)
                    if($script:AppearanceFirstStatusParts -and $timingView.Capacity -ge 188 -and $timingView.ReadInt32(164) -eq 0x594D534F){
                        for($i=0;$i -lt 4;$i++){$timingView.Write((172+4*$i),[int]$script:AppearanceFirstStatusParts[$i])}
                        $timingView.Write(168,[int]0x594D5331)
                    }
                }
            }catch{}finally{if($timingView){$timingView.Dispose()}}
        }
        if($script:AppearanceCorrectionTiming -and $script:AppearanceCorrectionTiming[1] -ge 0 -and $script:AppearanceCorrectionTiming[1] -le 180000){
            $correctionView=$null
            try{
                $correctionView=$script:AutomaticResultMap.CreateViewAccessor(0,0,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
                if($correctionView.Capacity -ge 160 -and $correctionView.ReadInt32(124) -eq 0x594D434F){
                    for($i=0;$i -lt 7;$i++){$correctionView.Write((132+4*$i),[int]$script:AppearanceCorrectionTiming[$i])}
                    if($correctionView.Capacity -ge 164 -and $correctionView.ReadInt32(160) -eq 0x594D524F){
                        $flag,$receipt,$both=$script:AppearanceCorrectionTiming[7..9]
                        if($flag+$receipt+$both -eq $script:AppearanceCorrectionTiming[4] -and $flag+$receipt+$both -le 7){
                            $packed=0x594D5A00 -bor $flag -bor ($receipt -shl 3) -bor ($both -shl 6)
                            $correctionView.Write(160,[int]$packed)
                        }
                    }
                    $correctionView.Write(128,[int]0x594D4331)
                }
            }catch{}finally{if($correctionView){$correctionView.Dispose()}}
        }
        if(!$view){$view=$script:AutomaticResultMap.CreateViewAccessor(0,32,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)}
        $reason=if($Success){0}elseif($script:AppearanceFailureCode){[int]$script:AppearanceFailureCode}else{99}
        $view.Write(4,[int]2);$view.Write(8,[int]$script:AppearanceHelperPhase);$view.Write(12,[int]$reason)
        $view.Write(16,[int]$script:AppearanceHelperFlags);$view.Write(20,[int]$script:AppearanceHelperBackendCount)
        $view.Write(24,[int]$HResult);$view.Write(28,[int]0)
        $view.Write(0,[int]0x594D4353);$view.Flush()
    }finally{if($view){$view.Dispose()}}
}
function Invoke-AppearanceControl([string]$ControlAction,$AutomaticCorrectionStatus=$null,[switch]$ProtectEditor,$AutomaticReloadStatus=$null,[switch]$DeferredCorrection,$AutomaticIdleScope=$null,[switch]$DeferIdleMismatch,[string]$CorrectionObservationRoot='',$AutomaticPrepareScope=$null) {
if($script:AppearanceObservationOnly -and $ControlAction -notin @('Idle','Status')){throw 'Read-only appearance control denied'}
$controlMutex=$null;$controlLocked=$false
try {
    if($ControlAction -in @('Status','Ensure','UpdateOwnedResources','Apply','Refresh','Reload','Prepare','Reset','Restore','Idle','Reconcile')) {
        $script:RpcBudgetClock=[Diagnostics.Stopwatch]::StartNew()
        $controlMutex=New-AppearanceMutex 'ThemeControl'
        # Final read-only confirmation may overlap a slow Status/Apply request.
        # Wait on the existing mutex within the native 30s verification bound;
        # mutation requests retain their short contention limit and never replay.
        $controlWaitMs=if($ControlAction -eq 'Idle'){30000}else{3000}
        if($script:AppearanceTimingFirstControl){$mutexStart=$script:AppearanceTimingClock.ElapsedMilliseconds}
        try{$controlLocked=$controlMutex.WaitOne($controlWaitMs)}catch [Threading.AbandonedMutexException]{$controlLocked=$true}
        finally{if($script:AppearanceTimingFirstControl){$script:AppearanceTiming[2]+=$script:AppearanceTimingClock.ElapsedMilliseconds-$mutexStart}}
        if(!$controlLocked){$script:AppearanceFailureCode=11;throw 'Theme operation busy; retry after rereading'}
    }
    # Hints only select paths; these scoped reads revalidate before backend or recovery work.
    if($ControlAction -notin @('Status','UpdateOwnedResources') -and !($ControlAction -eq 'Idle' -and $AutomaticIdleScope) -and
        !($ControlAction -eq 'Reconcile' -and $AutomaticCorrectionStatus)){$null=Get-AppearanceSteamSession}
    # Scoped automatic work owns an observed target, not a later hint or transaction.
    if(!$CorrectionObservationRoot -and !$AutomaticPrepareScope -and (Get-Command Initialize-PresetTargetHint -ErrorAction SilentlyContinue)){Initialize-PresetTargetHint -KeepBackendPath:([bool]$BackendPath -or [bool]$portOwners.Count)}
    switch($ControlAction) {
        Status {
            $status=if($CorrectionObservationRoot){Get-AppearanceStatus -ExpectedCorrectionRoot $CorrectionObservationRoot}else{Get-AppearanceStatus}
            $result=[pscustomobject]@{ok=$true;snapshot=$status}
        }
        Idle {
            # Idle itself checks editor ownership before and after observation.
            if(!(Get-Command Invoke-AppearanceIdle -ErrorAction SilentlyContinue)){throw 'CSS idle component missing'}
            $revision='';$target=''
            if($Payload){if($Payload.Length -gt 2048){throw 'Idle request too large'};$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;$revision=[string]$request.expectedRevision;$target=[string]$request.targetId;$SessionPid=[uint32]$request.expectedPid;$SessionBirth=[string]$request.expectedBirth;if($revision -notmatch '^[a-f0-9]{64}$' -or $target -notmatch '^[a-f0-9]{16}$'){throw 'Invalid idle request'}}
            if(!$SessionPid -or $SessionBirth -notmatch '^[1-9]\d{0,19}$'){throw 'Idle session required'}
            if($AutomaticIdleScope){
                if($Payload){throw 'Unexpected external payload for automatic confirmation'}
                $revision=$AutomaticIdleScope.revision;$target=$AutomaticIdleScope.targetId
                $result=Invoke-AppearanceIdle $SessionPid $SessionBirth $revision $target -DeferDomMismatch:$DeferIdleMismatch -RequireSettledTransaction:$script:AppearanceObservationOnly -UseScopedStatusIdentity
            }else{$result=Invoke-AppearanceIdle $SessionPid $SessionBirth $revision $target -RequireSettledTransaction:$script:AppearanceObservationOnly}
        }
        # Ensure consumes ownership and the settings revision, never page plans.
        Ensure {$current=Get-AppearanceStatus -RevisionOnly;$current=Invoke-OwnedPresetResourceUpdate $current;$result=Start-AppearanceBackend}
        # Internal Automatic branch for an already-running backend. This action
        # is deliberately absent from the public -Action ValidateSet.
        UpdateOwnedResources {
            $before=Get-AppearanceStatus
            $current=Invoke-OwnedPresetResourceUpdate $before
            $changed=($before.packUpdateAvailable -or $before.packRepairRequired) -and !$current.packUpdateAvailable -and !$current.packRepairRequired -and $current.packPrepared
            if($changed -and $current.backendReady){$result=Invoke-AppearanceReload @{expectedRevision=$current.revision;targetId=$current.targetId} -ProtectEditor}
            else{$result=[pscustomobject]@{ok=$true;saved=[bool]$changed;snapshot=$current;message='Existing theme resources checked without starting a backend'}}
        }
        Reload {
            $request=$null
            if($AutomaticReloadStatus){$request=@{expectedRevision=$AutomaticReloadStatus.revision;targetId=$AutomaticReloadStatus.targetId}}
            elseif($Payload){if($Payload.Length -gt 2048){throw 'Reload request too large'};$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;if($request.expectedRevision -notmatch '^[a-fA-F0-9]{64}$' -or $request.targetId -notmatch '^[a-fA-F0-9]{16}$'){throw 'Invalid reload request'}}
            $result=Invoke-AppearanceReload $request -ProtectEditor:$ProtectEditor
        }
        Prepare {
            if($AutomaticPrepareScope){
                Assert-CssDesktopNotEditing
                if(!$AutomaticPrepareScope.root -or $AutomaticPrepareScope.revision -cnotmatch '^[a-f0-9]{64}$' -or
                    $AutomaticPrepareScope.targetId -cnotmatch '^[A-F0-9]{16}$'){throw 'Automatic appearance preparation scope missing'}
                $current=Get-AppearanceStatus -ExpectedCorrectionRoot $AutomaticPrepareScope.root
                if(!$current.backendReady -or $current.revision -cne $AutomaticPrepareScope.revision -or
                    $current.targetId -cne $AutomaticPrepareScope.targetId){throw 'Appearance changed before automatic preparation; no preset written'}
                if($current.packError){throw $current.packError}
                if($current.packPrepared -ne $false -and !$current.packRepairRequired -and !$current.foreignActiveCount){
                    $result=[pscustomobject]@{ok=$true;snapshot=$current}
                    break
                }
                $result=Invoke-PresetPrepare $false $current.revision -ProtectEditor
                break
            }
            if($ProtectEditor){Assert-CssDesktopNotEditing}
            Get-AppearanceStatus|Out-Null
            $expected=''
            if($Payload){$request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json;$expected=[string]$request.expectedRevision}
            $result=Invoke-PresetPrepare $false $expected -ProtectEditor:$ProtectEditor
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
            if($AutomaticCorrectionStatus -and !$CorrectionObservationRoot){throw 'Automatic appearance correction scope missing'}
            $status=Get-AppearanceStatus -ExpectedCorrectionRoot $CorrectionObservationRoot
            # The automatic caller's mismatch was observed before this lock.
            # A newer user's selection must not inherit that correction.
            if($AutomaticCorrectionStatus -and (!$AutomaticCorrectionStatus.revision -or !$AutomaticCorrectionStatus.targetId -or
                $AutomaticCorrectionStatus.revision -cne $status.revision -or $AutomaticCorrectionStatus.targetId -cne $status.targetId)){
                throw 'Appearance changed before automatic reconciliation; no correction submitted'
            }
            if($Payload){
                if($Payload.Length -gt 2048){throw 'Reconcile request too large'}
                $request=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Payload))|ConvertFrom-Json
                if($request.expectedRevision -notmatch '^[a-f0-9]{64}$' -or $request.targetId -notmatch '^[a-f0-9]{16}$' -or $request.expectedRevision -ne $status.revision -or $request.targetId -ne $status.targetId){throw 'Appearance changed before reconciliation'}
            }
            if(!$status.backendReady){throw 'Existing CSS backend unavailable; no backend was started'}
            if(Test-CssDesktopEditing){throw 'CSSLoader Desktop is editing; reconciliation postponed'}
            if(!$status.packOwned){throw 'Appearance target is not owned; reconciliation skipped'}
            # A deferred check can submit theme differences only, not replay resource work.
            $themeOnly=$status.packPrepared -eq $true -and !$status.packUpdateAvailable -and !$status.packRepairRequired -and
                !$status.backendReloadRequired -and !$status.packError -and !$status.foreignActiveCount
            if($DeferredCorrection){
                if(!$AutomaticCorrectionStatus -or !$themeOnly){throw 'Appearance prerequisites changed before deferred correction; no correction submitted'}
            }else{$status=Invoke-OwnedPresetResourceUpdate $status}
            if($status.backendReloadRequired){
                $reloadRequest=if($AutomaticCorrectionStatus){@{expectedRevision=$AutomaticCorrectionStatus.revision;targetId=$AutomaticCorrectionStatus.targetId}}
                $loaded=Invoke-AppearanceReload $reloadRequest -ProtectEditor:([bool]$AutomaticCorrectionStatus)
                if(!$loaded.ok){throw 'Appearance reread unconfirmed'}
                $status=$loaded.snapshot
            }
            if($AutomaticCorrectionStatus -and ($AutomaticCorrectionStatus.revision -cne $status.revision -or $AutomaticCorrectionStatus.targetId -cne $status.targetId)){
                throw 'Appearance changed during automatic reconciliation; no correction submitted'
            }
            if(!$status.backendReady -or $status.packPrepared -eq $false -or $status.packRepairRequired -or $status.backendReloadRequired -or $status.foreignActiveCount){throw 'Appearance reconciliation prerequisites changed'}
            # The earlier DOM proof cannot authorize automatic writes after the
            # main document disappears. Only known transient failures can defer.
            $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
            if($AutomaticCorrectionStatus -and !$render.checked -and (Test-AppearanceDocumentRecheck $render.reason)){
                if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]=2}
                $result=[pscustomobject]@{ok=$false;snapshot=$status;renderConfirmed=$false;message=$render.reason;canDeferCorrection=($themeOnly -and !$DeferredCorrection)}
                break
            }
            if(!$render.checked -and ($AutomaticCorrectionStatus -or $status.configuredMatchesBackend)){throw ('Appearance DOM unconfirmed; no blind theme writes: '+$render.reason)}
            $alreadyMatched=$AutomaticCorrectionStatus -and $status.configuredMatchesBackend -and $render.checked -and $render.confirmed
            $initialThemes=$null
            if($AutomaticCorrectionStatus -and !$alreadyMatched){$initialThemes=@(Invoke-LoaderRpc 'get_themes')}
            # The page and the last backend read can both outlive a user edit.
            # Check the editor and revision after those reads, before any write.
            Assert-CssDesktopNotEditing
            $currentRows=@(Get-ThemeRows -RevisionOnly)
            if((Get-ThemeRevision $currentRows) -ne $status.revision -or (Get-ThemeTargetId) -ne $status.targetId){throw 'Appearance changed during DOM verification; no correction submitted'}
            # The earlier mismatch can clear on its own. Automatic still has
            # a separate final Idle gate, so do not sync an already matched page.
            if($alreadyMatched){
                $null=Get-AppearanceSteamSession
                if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]=3}
                $result=[pscustomobject]@{ok=$true;snapshot=$status;renderConfirmed=$true;message='Appearance differences reconciled and each theme receipt confirmed'}
                break
            }
            $script:AppearanceMeasureCorrectionWrites=[bool]($AutomaticCorrectionStatus -and $script:AppearanceCorrectionTiming)
            $correctionRoot=$script:ThemeRoot
            try{Sync-AppearanceBackend $currentRows $false @($render.mismatched) -RequireCurrentSteamScope:([bool]$AutomaticCorrectionStatus) -InitialThemes $initialThemes}
            finally{$script:AppearanceMeasureCorrectionWrites=$false}
            if($AutomaticCorrectionStatus){
                # The post-write observation must not recover another target
                # or treat a concurrent selection as this correction's result.
                $afterRows=$null
                $status=Get-AppearanceStatus -ExpectedCorrectionRoot $correctionRoot -CapturedRows ([ref]$afterRows)
                if($status.revision -cne $AutomaticCorrectionStatus.revision){
                    if($afterRows.Count -ne $currentRows.Count){Stop-AppearanceIdle 4 'Appearance settings changed during automatic CSS correction; no replay'}
                    for($i=0;$i -lt $currentRows.Count;$i++){
                        $before=$currentRows[$i];$after=$afterRows[$i]
                        if($before.name -cne $after.name -or !$before.installed -or !$after.installed -or $before.manifestHash -cne $after.manifestHash -or
                            ($before.hash -cne $after.hash -and (Get-AppearanceConfigValue $before.config) -cne (Get-AppearanceConfigValue $after.config))){
                            Stop-AppearanceIdle 4 'Appearance settings changed during automatic CSS correction; no replay'
                        }
                    }
                }
                if(!$status.configuredMatchesBackend){
                    # The next bounded slot needs fresh backend flags and styles.
                    if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]=4}
                    $result=[pscustomobject]@{ok=$false;snapshot=$status;renderConfirmed=$false;correctionSynchronized=$true;message='Configuration retained; backend flags unconfirmed after correction'}
                    break
                }
                if($status.packPrepared -eq $false -or $status.packRepairRequired -or $status.packError -or $status.foreignActiveCount){
                    # A DOM match cannot make this known-invalid preset eligible for final confirmation.
                    if($script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]=4}
                    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -band 3
                    Stop-AppearanceIdle 3 'Appearance loading unconfirmed after automatic correction; CSS remains resident'
                }
            }else{$status=Get-AppearanceStatus}
            $render=Get-AppearanceRenderedStatus (Get-AppearanceSteamSession) $status
            $confirmed=$status.configuredMatchesBackend -and $render.confirmed
            if($AutomaticCorrectionStatus -and $script:AppearanceCorrectionTiming){$script:AppearanceCorrectionTiming[6]=if($confirmed){5}else{4}}
            $result=[pscustomobject]@{ok=$confirmed;snapshot=$status;renderConfirmed=[bool]$render.confirmed;message=$(if($confirmed){'Appearance differences reconciled and each theme receipt confirmed'}else{'Configuration retained; theme rendering remains unconfirmed: '+$render.reason})}
            if($AutomaticCorrectionStatus){$result|Add-Member -NotePropertyName correctionSynchronized -NotePropertyValue $true}
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
                if($StartBackend){
                    $initial=Invoke-AppearanceControl 'Ensure'
                    if(!$initial.ok){$script:AppearanceFailureCode=14;throw 'Automatic CSS initialization unconfirmed'}
                    # Native rechecks the current request, public power and
                    # Desktop editor before this same helper may continue.
                    Write-AppearanceHelperResult $true
                    if(!$ready.Set()){throw 'Automatic initialization ready signal failed'}
                    if(!$gate.WaitOne($GateTimeoutMs)){$script:AppearanceFailureCode=13;throw 'Automatic initialization gate expired'}
                    if(!$gate.Reset()){throw 'Automatic initialization gate reset failed'}
                }
                $confirmed=Invoke-AutomaticAppearanceRefresh -UpdateOwnedResources:(!$StartBackend)
            }
            # No theme/launch mutex spans this wait. Native immediately releases
            # the same helper for resident confirmation; CSS has no exit timer.
            if($script:AppearanceTiming){$script:AppearanceTiming[6]=$script:AppearanceTimingClock.ElapsedMilliseconds}
            if(!$ready.Set()){throw 'Automatic ready signal failed'}
            $script:AppearanceHelperPhase=2
            # Only the helper is Job-owned. -1 is an event-only wait bounded by
            # the native request deadline; releasing it never closes CSS.
            if(!$gate.WaitOne($GateTimeoutMs)){$script:AppearanceFailureCode=13;throw 'Automatic idle gate expired'}
            $result=if($VerifyOnly){Invoke-AppearanceControl 'Idle'}else{Invoke-AutomaticAppearanceIdle $confirmed.snapshot}
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
