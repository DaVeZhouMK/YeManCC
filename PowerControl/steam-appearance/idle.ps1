# Per-user, per-Steam-process idle receipt. No persistent startup permission.
$script:AppearanceIdleFile=Join-Path $env:LOCALAPPDATA 'YeManCC\SteamAppearance\idle.json'
function Assert-AppearanceIdlePath {
    $path=[IO.Path]::GetFullPath($script:AppearanceIdleFile)
    for($cursor=$path;$cursor;$cursor=[IO.Path]::GetDirectoryName($cursor)) {
        if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Idle marker reparse path denied'}}
        if([IO.Path]::GetDirectoryName($cursor) -eq $cursor){break}
    }
    $path
}
function Get-AppearanceSteamSession {
    $all=@(Get-Process -Name steam -ErrorAction SilentlyContinue|Where-Object {Test-CurrentUserProcess $_})
    if($all.Count -ne 1){return $null}
    $process=$all[0]
    $image=Get-AppearanceProcessPath $process
    if([IO.Path]::GetFileName($image) -ine 'steam.exe'){return $null}
    [pscustomobject]@{pid=[int]$process.Id;birth=([string]$process.StartTime.ToUniversalTime().ToFileTimeUtc());root=[IO.Path]::GetDirectoryName($image)}
}
function Read-AppearanceIdle {
    try {
        $path=Assert-AppearanceIdlePath
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 8192){return $null}
        $value=[IO.File]::ReadAllText($path)|ConvertFrom-Json
        if($value.version -ne 1 -or $value.pid -le 0 -or $value.birth -notmatch '^\d{1,20}$' -or $value.revision -notmatch '^[a-f0-9]{64}$' -or $value.targetId -notmatch '^[a-f0-9]{16}$'){return $null}
        $value
    }catch{return $null}
}
function Test-AppearanceIdleSession($Marker,$Session) {
    $Marker -and $Session -and $Marker.pid -eq $Session.pid -and $Marker.birth -eq $Session.birth -and
        [string]$Marker.root -ieq [string]$Session.root -and [string]$Marker.backendExe -ieq [string]$script:BackendExe
}
function Clear-AppearanceIdle {
    $path=Assert-AppearanceIdlePath
    if(Test-Path -LiteralPath $path -PathType Leaf){Remove-Item -LiteralPath $path -Force}
}
function Test-CssDesktopEditing {
    # No timer/cache: validate once at the destructive action boundary. Minimized
    # Desktop still means an edit; an unreadable matching process preserves CSS.
    try{$processes=@(Get-Process -Name 'CSSLoader Desktop','CSSLoader-Desktop' -ErrorAction SilentlyContinue)}catch{return $true}
    foreach($process in $processes){
        try {
            if($process.HasExited){continue}
            if(!(Test-CurrentUserProcess $process)){continue}
            $image=Get-AppearanceProcessPath $process
            if([IO.Path]::GetFileName($image) -in @('CSSLoader Desktop.exe','CSSLoader-Desktop.exe')){return $true}
        }catch{return $true}
    }
    return $false
}
function Assert-CssDesktopNotEditing {
    if(Test-CssDesktopEditing){Stop-AppearanceIdle 15 'CSSLoader Desktop is open; user editing owns the backend, cleanup skipped'}
}
function Stop-AppearanceIdle([int]$Code,[string]$Message) {
    $script:AppearanceFailureCode=$Code
    throw $Message
}
# Compatibility entry: Idle now confirms a resident backend and never closes it.
function Invoke-AppearanceIdle([uint32]$ExpectedPid=0,[string]$ExpectedBirth='',[string]$ExpectedRevision='',[string]$ExpectedTarget='') {
    Assert-CssDesktopNotEditing
    $script:AppearanceHelperPhase=3
    $session=Get-AppearanceSteamSession
    if(!$session -or ($ExpectedPid -and ($session.pid -ne $ExpectedPid -or $session.birth -ne $ExpectedBirth))){Stop-AppearanceIdle 1 'Steam session changed; CSS remains resident'}
    $script:AppearanceHelperPhase=4
    $status=Get-AppearanceStatus
    if(!$status.backendReady -or !$status.configuredMatchesBackend -or $status.packPrepared -eq $false -or $status.packRepairRequired -or $status.backendReloadRequired -or $status.packError -or $status.foreignActiveCount){Stop-AppearanceIdle 3 'Appearance loading unconfirmed; CSS remains resident'}
    if(($ExpectedRevision -and $status.revision -ne $ExpectedRevision) -or ($ExpectedTarget -and $status.targetId -ne $ExpectedTarget)){Stop-AppearanceIdle 4 'Appearance changed; CSS remains resident'}
    $script:AppearanceHelperFlags=3
    $script:AppearanceHelperPhase=5
    if(!(Test-AppearanceRendered $session $status)){Stop-AppearanceIdle 6 'Steam bigpicture CSS DOM not confirmed; CSS remains resident'}
    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 4
    $script:AppearanceHelperPhase=6
    $processes=@(Get-BackendProcesses)
    if(!(Test-BackendOwner)){Stop-AppearanceIdle 7 'CSS owner changed; backend retained'}
    Assert-SingleAppearanceBackend $processes
    $script:AppearanceHelperBackendCount=[int]$processes.Count
    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 8
    $script:AppearanceHelperPhase=7
    Assert-CssDesktopNotEditing
    if(!(Test-AppearanceIdleSession ([pscustomobject]@{pid=$session.pid;birth=$session.birth;root=$session.root;backendExe=$script:BackendExe}) (Get-AppearanceSteamSession)) -or (Get-ThemeRevision @(Get-ThemeRows)) -ne $status.revision -or (Get-ThemeTargetId) -ne $status.targetId){Stop-AppearanceIdle 9 'Appearance/session changed after DOM confirmation'}
    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 16
    [pscustomobject]@{ok=$true;resident=$true;snapshot=$status;message='外观已确认，复用唯一 CSS 常驻后台'}
}
# Protocol 2 verifies every statically injected CSS file for the selected patch
# values. Plans reuse the already parsed manifest/config; no extra file scan.
$script:AppearanceRenderProtocol=4
$script:AppearanceContextProtocol=1
$script:AppearanceContentManifestFile=Join-Path $PSScriptRoot 'manifest.json'
$script:AppearanceContentIndex=$null
$script:AppearanceContentIndexStamp=''
function Test-AppearanceReceiptInteger($Value,[long]$Minimum,[long]$Maximum) {
    ($Value -is [int] -or $Value -is [long]) -and $Value -ge $Minimum -and $Value -le $Maximum
}
function Get-AppearanceContentIndex {
    # Read one small bundle index per changed identity, never reopen every CSS.
    $item=Get-Item -LiteralPath $script:AppearanceContentManifestFile -Force -ErrorAction Stop
    if($item.Length -gt 131072 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'render-content-index-invalid'}
    $stamp=[string]$item.Length+':'+$item.LastWriteTimeUtc.Ticks+':'+$item.CreationTimeUtc.Ticks
    if($script:AppearanceContentIndex -and $script:AppearanceContentIndexStamp -ceq $stamp){return $script:AppearanceContentIndex}
    $index=[IO.File]::ReadAllText($item.FullName,[Text.Encoding]::UTF8)|ConvertFrom-Json
    if($index.id -cne 'ymcc-steam-appearance' -or $index.renderContentProtocol -ne 1 -or @($index.files).Count -gt 256){throw 'render-content-index-invalid'}
    $result=@{};$properties=@{}
    foreach($entry in $index.files){
        if($entry.path -isnot [string]){throw 'render-content-index-path-invalid'}
        if([IO.Path]::GetExtension($entry.path) -ine '.css'){continue}
        $parts=$entry.path.Replace('\','/').Split('/',2)
        if($parts.Count -ne 2){throw 'render-content-index-path-invalid'}
        $null=Get-AppearanceFileReceipt $parts[0] $parts[1]
        if($result.ContainsKey($entry.path) -or $entry.contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$' -or $properties.ContainsKey($entry.contentProperty)){throw 'render-content-index-duplicate-or-invalid'}
        $result[$entry.path]=[string]$entry.contentProperty;$properties[$entry.contentProperty]=$true
    }
    if($result.Count -ne 44){throw 'render-content-index-incomplete'}
    $after=Get-Item -LiteralPath $item.FullName -Force -ErrorAction Stop
    if(([string]$after.Length+':'+$after.LastWriteTimeUtc.Ticks+':'+$after.CreationTimeUtc.Ticks) -cne $stamp){throw 'render-content-index-changed'}
    $script:AppearanceContentIndex=$result;$script:AppearanceContentIndexStamp=$stamp
    return $result
}
function Get-AppearanceFileReceipt([string]$Name,[string]$Relative) {
    $relativePath=$Relative.Replace('\','/')
    if($Name -notin @('Art Hero','Centered Game Text','Darken Unfocused Games','Game Cover Reflections','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details') -or
       $relativePath -notmatch '^[A-Za-z0-9][A-Za-z0-9_./ -]{0,127}\.css$' -or
       @($relativePath.Split('/')|Where-Object {$_ -in @('','..','.')}).Count){throw 'render-file-path-invalid'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try {
        $bytes=[Text.Encoding]::UTF8.GetBytes(($Name+'/'+$relativePath).ToLowerInvariant())
        '--ymcc-file-'+([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').Substring(0,16).ToLowerInvariant()
    }finally{$sha.Dispose()}
}
function Get-AppearanceVariableDefaults([string]$Name) {
    # Managed SP root defaults; independently checked against actual bundled CSS
    # in the browser regression. Runtime planning does not reopen CSS resources.
    $defaults=@{
        'Art Hero'=@{
            '--met-game-carousel-font-height'='-3.5em'
            '--met-game-carousel-font-size'='18px'
            '--met-game-carousel-height-ones'='-0vh'
            '--met-game-carousel-height-tens'='-50vh'
            '--met-header-bar-height'='0vh'
            '--met-header-bar-height-ones'='0vh'
            '--met-hero-art-position'='center'
            '--met-hero-art-size'='cover'
            '--met-hero-art-z-order'='1'
            '--met-hero-height-ones'='0vh'
            '--met-hero-height-tens'='0vh'
        }
        'Darken Unfocused Games'=@{
            '--ren-game-tile-brightness'='0.5'
            '--ren-game-tile-saturate'='0.5'
        }
        'Game Cover Reflections'=@{
            '--ren-reflection-blur'='2px'
            '--ren-reflection-focused-opacity'='.5'
            '--ren-reflection-gradient'='#ffffff00 30%'
            '--ren-reflection-unfocused-opacity'='.3'
        }
        'Mini Carousel'=@{
            '--scale-value'='0.7'
        }
    }
    if($defaults.ContainsKey($Name)){return $defaults[$Name]}
    if($Name -cin @('Centered Game Text','NEX-85 Keyboard','Round','YMCC Game Details')){return @{}}
    throw 'render-variable-theme-invalid'
}
function ConvertTo-AppearanceVariableValue([string]$Value) {
    if($Value.Length -gt 96 -or $Value -match '[^A-Za-z0-9#.% +_\t\r\n\f-]'){throw 'render-variable-value-invalid'}
    $value=$Value.Trim().ToLowerInvariant() -replace '\s+',' '
    if($value -match '^([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))(px|vh|em|%)?$'){
        $number=[decimal]::Parse($matches[1],[Globalization.NumberStyles]::AllowLeadingSign -bor [Globalization.NumberStyles]::AllowDecimalPoint,[Globalization.CultureInfo]::InvariantCulture)
        return $number.ToString('0.############################',[Globalization.CultureInfo]::InvariantCulture)+$matches[2]
    }
    return $value
}
function Get-AppearanceThemeRenderPlan([string]$Name,$Manifest,$Config,$ContentIndex=$null,[string]$Target='SP') {
    if($Target -cnotin @('SP','MainMenu','QuickAccess')){throw 'render-plan-target-invalid'}
    if(!$Manifest -or $Manifest.name -cne $Name -or !$Config -or $Config.active -isnot [bool]){throw 'render-plan-source-invalid'}
    if(!$ContentIndex){$ContentIndex=Get-AppearanceContentIndex}
    $files=@{};$defaults=Get-AppearanceVariableDefaults $Name;$variables=@{};$variableTargets=@{};$chosen=@{}
    $collect={param($Map,[bool]$Enabled)
        if($Map -isnot [pscustomobject]){throw 'render-inject-map-invalid'}
        if(@($Map.PSObject.Properties).Count -gt 128){throw 'render-inject-map-too-large'}
        foreach($entry in $Map.PSObject.Properties){
            if($entry.Name.StartsWith('--')){
                $property=[string]$entry.Name
                if($property -cnotmatch '^--[a-z][a-z0-9-]{0,79}$' -or !$defaults.ContainsKey($property) -or $entry.Value -isnot [Array] -or $entry.Value.Count -lt 2 -or $entry.Value.Count -gt 16 -or @($entry.Value|Where-Object {$_ -isnot [string]}).Count){throw 'render-variable-definition-invalid'}
                $null=ConvertTo-AppearanceVariableValue $entry.Value[0]
                if(!$variables.ContainsKey($property)){$variables[$property]='';$variableTargets[$property]=$false}
                if($Target -cnotin @($entry.Value|Select-Object -Skip 1)){continue}
                if(!$variableTargets[$property]){$variables[$property]=if($Config.active){[string]$defaults[$property]}else{''};$variableTargets[$property]=$true}
                if($Enabled){
                    if($chosen.ContainsKey($property) -and $chosen[$property] -cne [string]$entry.Value[0]){throw 'render-variable-selection-conflict'}
                    $chosen[$property]=[string]$entry.Value[0];$variables[$property]=[string]$entry.Value[0]
                }
                continue
            }
            if($entry.Value -isnot [Array] -or @($entry.Value).Count -gt 16 -or @($entry.Value|Where-Object {$_ -isnot [string]}).Count){throw 'render-inject-targets-invalid'}
            $applies=$Target -cin @($entry.Value)
            $file=([string]$entry.Name).Replace('\','/')
            $null=Get-AppearanceFileReceipt $Name $file
            if(!$files.ContainsKey($file)){$files[$file]=$false}
            if($Enabled -and $applies){$files[$file]=$true}
            if($files.Count -gt 128){throw 'render-plan-too-large'}
        }
    }
    & $collect $Manifest.inject ([bool]$Config.active)
    if($Manifest.patches){
        if($Manifest.patches -isnot [pscustomobject] -or @($Manifest.patches.PSObject.Properties).Count -gt 64){throw 'render-patch-map-invalid'}
        foreach($patch in $Manifest.patches.PSObject.Properties){
            $values=$patch.Value.values
            if($values -isnot [pscustomobject] -or @($values.PSObject.Properties).Count -gt 256){throw 'render-patch-values-invalid'}
            $configured=$Config.PSObject.Properties[$patch.Name]
            $selected=if($configured){$configured.Value}else{$patch.Value.default}
            if($selected -isnot [string] -or @($values.PSObject.Properties|Where-Object {$_.Name -ceq $selected}).Count -ne 1){throw 'render-patch-selection-invalid'}
            foreach($option in $values.PSObject.Properties){& $collect $option.Value ([bool]($Config.active -and $option.Name -ceq $selected))}
        }
    }
    if(!$files.ContainsKey('shared.css')){throw 'render-shared-file-missing'}
    $rows=@(foreach($file in @($files.Keys|Sort-Object -CaseSensitive)){
        $contentProperty=$ContentIndex[$Name+'/'+$file]
        if($contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$'){throw 'render-content-file-not-managed'}
        [pscustomobject]@{path=$file;property=(Get-AppearanceFileReceipt $Name $file);contentProperty=$contentProperty;enabled=[bool]$files[$file]}
    })
    if($variables.Count -ne $defaults.Count){throw 'render-variable-definitions-incomplete'}
    $variableRows=@(foreach($property in @($variables.Keys|Sort-Object -CaseSensitive)){
        # Shared CSS can define a default even when the selected dynamic patch
        # only targets SP. Other documents must retain that static fallback.
        $enabled=[bool]($Config.active -and ($variableTargets[$property] -or $files['shared.css']))
        $value=if(!$variableTargets[$property] -and $enabled){[string]$defaults[$property]}else{[string]$variables[$property]}
        [pscustomobject]@{property=$property;enabled=$enabled;value=$value}
    })
    $result=[pscustomobject]@{protocol=4;files=$rows;variables=$variableRows}
    if($Target -ceq 'SP'){
        $contexts=[ordered]@{}
        foreach($context in @('MainMenu','QuickAccess')){$contexts[$context]=Get-AppearanceThemeRenderPlan $Name $Manifest $Config $ContentIndex $context}
        $result|Add-Member -NotePropertyName contextProtocol -NotePropertyValue 1
        $result|Add-Member -NotePropertyName contexts -NotePropertyValue ([pscustomobject]$contexts)
    }
    $result
}
function Get-AppearanceRenderExpectation($Status,[string]$Target='SP') {
    if($Target -cnotin @('SP','MainMenu','QuickAccess')){throw 'render-expectation-target-invalid'}
    $names=@('Art Hero','Centered Game Text','Darken Unfocused Games','Game Cover Reflections','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details')
    $contentIndex=Get-AppearanceContentIndex
    $rows=@($Status.themes);$count=0;$properties=@{}
    if($rows.Count -ne $names.Count){throw 'render-snapshot-incomplete'}
    foreach($name in $names){
        $found=@($rows|Where-Object {$_.name -ceq $name})
        if($found.Count -ne 1 -or $found[0].installed -isnot [bool] -or !$found[0].installed -or $found[0].configured -isnot [bool] -or $found[0].error){throw 'render-snapshot-invalid'}
        $row=$found[0];$plan=$row.renderPlan
        if($Target -cne 'SP'){
            if(!$plan -or !(Test-AppearanceReceiptInteger $plan.contextProtocol 1 1) -or $plan.contexts -isnot [pscustomobject] -or @($plan.contexts.PSObject.Properties).Count -ne 2 -or !$plan.contexts.PSObject.Properties[$Target]){throw 'render-context-plan-unavailable'}
            $plan=$plan.contexts.PSObject.Properties[$Target].Value
        }
        if(!$plan -or !(Test-AppearanceReceiptInteger $plan.protocol 4 4) -or $plan.files -isnot [Array] -or !$plan.files.Count -or $plan.files.Count -gt 128){throw 'render-plan-unavailable'}
        $paths=@{}
        foreach($file in $plan.files){
            if($file -isnot [pscustomobject] -or $file.path -isnot [string] -or $file.property -isnot [string] -or $file.enabled -isnot [bool]){throw 'render-plan-file-invalid'}
            $property=Get-AppearanceFileReceipt $name $file.path
            if($property -cne $file.property -or $properties.ContainsKey($property) -or $paths.ContainsKey($file.path) -or (!$row.configured -and $file.enabled)){throw 'render-plan-file-invalid'}
            if($file.contentProperty -isnot [string] -or $file.contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$' -or $contentIndex[$name+'/'+$file.path] -cne $file.contentProperty -or $properties.ContainsKey($file.contentProperty)){throw 'render-plan-content-invalid'}
            $properties[$property]=$true;$properties[$file.contentProperty]=$true;$paths[$file.path]=$true;$count+=2
            if($count -gt 256){throw 'render-plan-too-large'}
            [pscustomobject]@{name=$name;property=$property;contentProperty=$file.contentProperty;enabled=[bool]$file.enabled;path=$file.path;kind='file';value=$(if($file.enabled){'1'}else{''})}
            [pscustomobject]@{name=$name;property=$file.contentProperty;enabled=[bool]$file.enabled;path=$file.path;kind='content';value=$(if($file.enabled){'1'}else{''})}
        }
        if(!$paths.ContainsKey('shared.css')){throw 'render-shared-file-missing'}
        $defaults=Get-AppearanceVariableDefaults $name;$seenVariables=@{}
        if($plan.variables -isnot [Array] -or $plan.variables.Count -ne $defaults.Count -or $plan.variables.Count -gt 64){throw 'render-plan-variables-invalid'}
        foreach($variable in $plan.variables){
            if($variable -isnot [pscustomobject] -or $variable.property -isnot [string] -or !$defaults.ContainsKey($variable.property) -or $properties.ContainsKey($variable.property) -or $seenVariables.ContainsKey($variable.property) -or $variable.enabled -isnot [bool] -or (($Target -ceq 'SP' -and $variable.enabled -ne $row.configured) -or (!$row.configured -and $variable.enabled)) -or $variable.value -isnot [string] -or (!$variable.enabled -and $variable.value -cne '')){throw 'render-plan-variable-invalid'}
            $null=ConvertTo-AppearanceVariableValue $variable.value
            $seenVariables[$variable.property]=$true;$properties[$variable.property]=$true;$count++;if($count -gt 256){throw 'render-plan-too-large'}
            [pscustomobject]@{name=$name;property=$variable.property;enabled=[bool]$variable.enabled;kind='variable';value=[string]$variable.value;path=''}
        }
    }
}

function Get-AppearanceRenderedStatus($Session,$Status) {
    $probe=[pscustomobject]@{checked=$false;confirmed=$false;reason='render-unconfirmed';mismatched=@();pages=0;mainPages=0;contextPages=0;contextProtocol=1}
    try {
        $expectations=@{SP=@(Get-AppearanceRenderExpectation $Status)}
        if(!$Session -or !$Session.root -or ![IO.Path]::IsPathRooted([string]$Session.root)){throw 'render-session-unconfirmed'}
        $listeners=@(Get-NetTCPConnection -State Listen -LocalPort 8080 -ErrorAction SilentlyContinue)
        if(!$listeners.Count){throw 'render-port-missing'}
        foreach($listener in $listeners){
            if($listener.LocalAddress -ne '127.0.0.1'){throw 'render-listener-not-local'}
            $owner=Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
            if(!$owner -or !(Test-CurrentUserProcess $owner)){throw 'render-owner-unconfirmed'}
            $image=[IO.Path]::GetFullPath((Get-AppearanceProcessPath $owner));$prefix=[IO.Path]::GetFullPath($Session.root).TrimEnd('\')+'\'
            if(!$image.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($image) -ine 'steamwebhelper.exe'){throw 'render-owner-path-changed'}
        }
        # Preserve the PS5.1 array value before expansion; never nest JSON pages.
        $response=Invoke-RestMethod -Uri 'http://127.0.0.1:8080/json/list' -TimeoutSec 2
        $tabs=@($response)
        if($response -isnot [Array] -or !$tabs.Count -or $tabs.Count -gt 64){throw 'render-target-list-invalid'}
        foreach($tab in $tabs){if($tab -isnot [pscustomobject]){throw 'render-target-invalid'}}
        $targets=@(foreach($tab in $tabs){
            if($tab.type -cne 'page'){continue}
            $target=if($tab.title -cmatch '^MainMenu(?:_uid[0-9]{1,10})?$'){'MainMenu'}elseif($tab.title -cmatch '^QuickAccess(?:_uid[0-9]{1,10})?$'){'QuickAccess'}elseif($tab.title -match '大屏|big.?picture|gamepad.?ui' -or $tab.url -match 'browserType=3'){'SP'}else{''}
            if($target){[pscustomobject]@{tab=$tab;target=$target}}
        })
        $main=@($targets|Where-Object target -ceq 'SP');$auxiliary=@($targets|Where-Object target -cne 'SP')
        if(!$main.Count -or $main.Count -gt 4){throw 'render-main-target-unconfirmed'}
        if($auxiliary.Count -gt 8){throw 'render-context-target-limit'}
        $seenTargets=@{}
        foreach($entry in $targets){
            if($entry.tab.id -isnot [string] -or $entry.tab.id -cnotmatch '^[A-Za-z0-9_-]{1,128}$' -or $seenTargets.ContainsKey($entry.tab.id)){throw 'render-target-id-invalid'}
            $seenTargets[$entry.tab.id]=$true
        }
        $targetOrder=@($main)+@($auxiliary)
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $bad=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        foreach($entry in $targetOrder){
            $tab=$entry.tab;$target=$entry.target
            if(!$expectations.ContainsKey($target)){$expectations[$target]=@(Get-AppearanceRenderExpectation $Status $target)}
            $expected=$expectations[$target]
            $wanted=@($expected|Where-Object enabled).Count -gt 0
            $keys=ConvertTo-Json -InputObject @($expected|ForEach-Object {$_.property}) -Compress
            $fileRows=@($expected|Where-Object kind -eq 'file')
            $fileKeys=ConvertTo-Json -InputObject @($fileRows|ForEach-Object property) -Compress
        $expression='(()=>{const e=document.documentElement;if(!e)throw new Error("root missing");const s=getComputedStyle(e),nodes=[...document.querySelectorAll("style.css-loader-style")];if(nodes.length>16384)throw new Error("style count");let size=0;const texts=nodes.map(n=>{const t=n.textContent||"";size+=t.length;if(t.length>262144||size>4194304)throw new Error("style size");return t;});return {protocol:4,ready:document.readyState!=="loading",styles:nodes.length,values:'+ $keys +'.map(k=>s.getPropertyValue(k).trim()),fileCounts:'+ $fileKeys +'.map(k=>{const re=new RegExp(k+"\\s*:","g");return texts.reduce((n,t)=>n+(t.match(re)||[]).length,0)})};})()'
            $remaining=4000-[int]$watch.ElapsedMilliseconds
            if($remaining -le 0){throw 'render-context-budget-exhausted'}
            if($tab.webSocketDebuggerUrl -cne ('ws://127.0.0.1:8080/devtools/page/'+$tab.id)){throw 'render-websocket-not-local'}
            $socket=New-Object Net.WebSockets.ClientWebSocket;$cancel=New-Object Threading.CancellationTokenSource;$stream=New-Object IO.MemoryStream
            try {
                $cancel.CancelAfter([Math]::Min(1000,$remaining))
                $socket.ConnectAsync([uri]$tab.webSocketDebuggerUrl,$cancel.Token).GetAwaiter().GetResult()|Out-Null
                $message=@{id=1;method='Runtime.evaluate';params=@{expression=$expression;returnByValue=$true}}|ConvertTo-Json -Depth 6 -Compress
                $bytes=[Text.Encoding]::UTF8.GetBytes($message);$segment=New-Object 'System.ArraySegment[byte]' -ArgumentList (,$bytes)
                $socket.SendAsync($segment,[Net.WebSockets.WebSocketMessageType]::Text,$true,$cancel.Token).GetAwaiter().GetResult()|Out-Null
                $buffer=New-Object byte[] 16384;$segment=New-Object 'System.ArraySegment[byte]' -ArgumentList (,$buffer);$reply=$null
                for($messages=0;$messages -lt 16;$messages++){
                    $stream.SetLength(0)
                    do {
                        $received=$socket.ReceiveAsync($segment,$cancel.Token).GetAwaiter().GetResult()
                        if($received.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text -or $stream.Length+$received.Count -gt 65536){throw 'render-response-invalid'}
                        $stream.Write($buffer,0,$received.Count)
                    }while(!$received.EndOfMessage)
                    $response=[Text.Encoding]::UTF8.GetString($stream.ToArray())|ConvertFrom-Json
                    if($response.id -ne 1){continue}
                    if($response.error -or $response.result.exceptionDetails -or $response.result.result.type -ne 'object'){throw 'render-evaluation-unconfirmed'}
                    $reply=$response.result.result.value;break
                }
                if(!$reply -or !(Test-AppearanceReceiptInteger $reply.protocol 4 4) -or $reply.ready -isnot [bool] -or !$reply.ready -or !(Test-AppearanceReceiptInteger $reply.styles 0 16384) -or $reply.values -isnot [Array] -or $reply.values.Count -ne $expected.Count){throw 'render-receipt-invalid'}
                for($index=0;$index -lt $expected.Count;$index++){
                    $value=$reply.values[$index]
                    if($value -isnot [string] -or $value.Length -gt 96){throw 'render-receipt-value-invalid'}
                    $ok=if($expected[$index].kind -eq 'variable'){(ConvertTo-AppearanceVariableValue $value) -ceq (ConvertTo-AppearanceVariableValue $expected[$index].value)}elseif($expected[$index].enabled){$value -ceq '1'}else{$value.Length -eq 0}
                    if(!$ok){$null=$bad.Add([string]$expected[$index].name)}
                }
                if($reply.fileCounts -isnot [Array] -or $reply.fileCounts.Count -ne $fileRows.Count){throw 'render-content-counts-invalid'}
                for($i=0;$i -lt $fileRows.Count;$i++){
                    $copies=$reply.fileCounts[$i]
                    if(!(Test-AppearanceReceiptInteger $copies 0 16384)){throw 'render-content-count-invalid'}
                    if($copies -ne $(if($fileRows[$i].enabled){1}else{0})){$null=$bad.Add([string]$fileRows[$i].name)}
                }
                if($wanted -and $reply.styles -eq 0){foreach($row in $expected|Where-Object enabled){$null=$bad.Add([string]$row.name)}}
                $probe.pages++
                if($target -ceq 'SP'){$probe.mainPages++}else{$probe.contextPages++}
            }finally {$socket.Abort();$socket.Dispose();$cancel.Dispose();$stream.Dispose()}
        }
        $probe.checked=$true;$probe.mismatched=@($Status.themes|Where-Object {$bad.Contains([string]$_.name)}|ForEach-Object {$_.name}|Select-Object -Unique)
        $probe.confirmed=$probe.mismatched.Count -eq 0
        $probe.reason=if($probe.confirmed){'render-receipts-confirmed'}else{'render-theme-receipts-mismatch'}
    }catch {
        $probe.checked=$false;$probe.confirmed=$false;$probe.mismatched=@()
        # Only a bounded reason leaves this read-only probe; no retry or mutation.
        $probe.reason=if($_.Exception.Message -match '^render-[a-z-]+$'){$_.Exception.Message}else{'render-transport-or-evaluation-failed'}
    }
    return $probe
}
function Test-AppearanceRendered($Session,$Status) {
    return [bool](Get-AppearanceRenderedStatus $Session $Status).confirmed
}
