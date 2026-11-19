$script:AppearanceSharedSteamProtocol=1
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
    $facts=Invoke-AppearanceSharedSteam @{operation='identity'}
    if($facts.protocol -ne 1 -or $facts.presence -notin @(0,1) -or $facts.processEpoch -isnot [string] -or $facts.accountEpoch -isnot [string] -or $facts.powerGeneration -isnot [string] -or
       $facts.powerGeneration -notmatch '^[1-9]\d{0,19}$' -or $facts.processEpoch -notmatch '^[1-9]\d{0,19}$' -or $facts.accountEpoch -notmatch '^[1-9]\d{0,19}$'){throw 'Shared Steam identity is Unknown'}
    $key=[string]$facts.presence+'|'+$facts.processEpoch+'|'+$facts.accountEpoch+'|'+$facts.powerGeneration
    if($script:AppearanceSharedIdentityKey -and $script:AppearanceSharedIdentityKey -cne $key){throw 'Shared Steam identity changed during the appearance request'}
    $script:AppearanceSharedIdentityKey=$key
    if($facts.presence -eq 0){if($SessionPid){throw 'Expected shared Steam session disappeared'};return $null}
    if($facts.pid -isnot [int] -and $facts.pid -isnot [long]){throw 'Shared Steam PID invalid'}
    if($facts.pid -le 0 -or $facts.pid -gt [uint32]::MaxValue -or $facts.birth -notmatch '^[1-9]\d{0,19}$' -or
       $facts.root -isnot [string] -or ![IO.Path]::IsPathRooted($facts.root) -or $facts.root.Length -gt 32768 -or $facts.root -match '[\x00\r\n]'){throw 'Shared Steam identity invalid'}
    if(($SessionPid -and $facts.pid -ne $SessionPid) -or ($SessionBirth -and $facts.birth -cne $SessionBirth)){throw 'Expected shared Steam identity changed'}
    [pscustomobject]@{pid=[uint32]$facts.pid;birth=$facts.birth;root=$facts.root;processEpoch=$facts.processEpoch;accountEpoch=$facts.accountEpoch;powerGeneration=$facts.powerGeneration}
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
    try{$processes=@(Get-AppearanceProcessCandidates 2 -Refresh)}catch{return $true}
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
function Invoke-AppearanceIdle([uint32]$ExpectedPid=0,[string]$ExpectedBirth='',[string]$ExpectedRevision='',[string]$ExpectedTarget='',[switch]$DeferDomMismatch,[switch]$RequireSettledTransaction,[switch]$UseScopedStatusIdentity) {
    Assert-CssDesktopNotEditing
    $script:AppearanceHelperPhase=3
    if($UseScopedStatusIdentity -and $RequireSettledTransaction -and $script:AppearanceObservationOnly){
        # The status read owns admission and returns its fresh pre-DOM identity.
        $identity=@{pid=$ExpectedPid;birth=$ExpectedBirth;session=$null}
        $status=Get-AppearanceStatus -FinalIdleIdentity $identity
        $session=$identity.session
    }else{
        $session=Get-AppearanceSteamSession
        if(!$session -or ($ExpectedPid -and ($session.pid -ne $ExpectedPid -or $session.birth -ne $ExpectedBirth))){Stop-AppearanceIdle 1 'Steam session changed; CSS remains resident'}
        $script:AppearanceHelperPhase=4
        $status=Get-AppearanceStatus
    }
    if(!$status.backendReady -or !$status.configuredMatchesBackend -or $status.packPrepared -eq $false -or $status.packRepairRequired -or $status.backendReloadRequired -or $status.packError -or $status.foreignActiveCount){Stop-AppearanceIdle 3 'Appearance loading unconfirmed; CSS remains resident'}
    $contentIndexStamp=$script:AppearanceContentIndexStamp
    if(($ExpectedRevision -and $status.revision -ne $ExpectedRevision) -or ($ExpectedTarget -and $status.targetId -ne $ExpectedTarget)){Stop-AppearanceIdle 4 'Appearance changed; CSS remains resident'}
    if($RequireSettledTransaction){
        $journal=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'transaction.json')
        if(Test-Path -LiteralPath $journal -ErrorAction Stop){Stop-AppearanceIdle 3 'Appearance transaction unfinished; CSS remains resident'}
    }
    $script:AppearanceHelperFlags=3
    $script:AppearanceHelperPhase=5
    if($DeferDomMismatch){
        $render=Get-AppearanceRenderedStatus $session $status
        # Lost transport can recheck only the gated, read-only final snapshot.
        if(!$render.confirmed -and (($render.checked -and $render.reason -ceq 'render-theme-receipts-mismatch') -or
           (!$render.checked -and (Test-AppearanceDocumentRecheck $render.reason -FinalReadOnly:($RequireSettledTransaction -and $script:AppearanceObservationOnly))))){
            return [pscustomobject]@{ok=$false;retryDom=$true}
        }
        $confirmed=$render.confirmed
    }else{$confirmed=Test-AppearanceRendered $session $status}
    # The page proof must refer to the same content index as the status plans.
    if($script:AppearanceContentIndexStamp -cne $contentIndexStamp){Stop-AppearanceIdle 9 'Appearance content index changed during DOM confirmation'}
    if(!$confirmed){Stop-AppearanceIdle 6 'Steam bigpicture CSS DOM not confirmed; CSS remains resident'}
    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 4
    $script:AppearanceHelperPhase=6
    $processes=@(Get-BackendProcesses)
    if(!(Test-BackendOwner -ExpectedFamily $processes)){Stop-AppearanceIdle 7 'CSS owner changed; backend retained'}
    Assert-SingleAppearanceBackend $processes
    $script:AppearanceHelperBackendCount=[int]$processes.Count
    $script:AppearanceHelperFlags=$script:AppearanceHelperFlags -bor 8
    $script:AppearanceHelperPhase=7
    Assert-CssDesktopNotEditing
    if(!(Test-AppearanceIdleSession ([pscustomobject]@{pid=$session.pid;birth=$session.birth;root=$session.root;backendExe=$script:BackendExe}) (Get-AppearanceSteamSession))){Stop-AppearanceIdle 9 'Appearance/session changed after DOM confirmation'}
    $rows=if($status.packPrepared -is [bool] -and $status.packPrepared){@(Get-ThemeRows -RevisionOnly -DigestOnly)}else{@(Get-ThemeRows -RevisionOnly)}
    if((Get-ThemeRevision $rows) -ne $status.revision -or (Get-ThemeTargetId) -ne $status.targetId){Stop-AppearanceIdle 9 'Appearance/session changed after DOM confirmation'}
    if($script:AppearanceContentIndexStamp -cne $contentIndexStamp){Stop-AppearanceIdle 9 'Appearance content index changed after DOM confirmation'}
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
    # Cache the parsed table, not filesystem timestamps. Read only the small index, never its CSS files.
    $item=Get-Item -LiteralPath $script:AppearanceContentManifestFile -Force -ErrorAction Stop
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'render-content-index-invalid'}
    $metadata=[string]$item.Length+':'+$item.LastWriteTimeUtc.Ticks+':'+$item.CreationTimeUtc.Ticks
    $stream=[IO.File]::OpenRead($item.FullName)
    try {
        if($stream.Length -gt 131072){throw 'render-content-index-invalid'}
        $sha=[Security.Cryptography.SHA256]::Create()
        try{$stamp=[Convert]::ToBase64String($sha.ComputeHash($stream))}finally{$sha.Dispose()}
        if($script:AppearanceContentIndex -and $script:AppearanceContentIndexStamp -ceq $stamp){return $script:AppearanceContentIndex}
        $stream.Position=0
        $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true,4096,$true)
        try{$raw=$reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
    $index=$raw|ConvertFrom-Json
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
    if($result.Count -ne 43){throw 'render-content-index-incomplete'}
    $after=Get-Item -LiteralPath $item.FullName -Force -ErrorAction Stop
    if(($after.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ([string]$after.Length+':'+$after.LastWriteTimeUtc.Ticks+':'+$after.CreationTimeUtc.Ticks) -cne $metadata){throw 'render-content-index-changed'}
    $script:AppearanceContentIndex=$result;$script:AppearanceContentIndexStamp=$stamp
    return $result
}
function Get-AppearanceFileReceipt([string]$Name,[string]$Relative) {
    $relativePath=$Relative.Replace('\','/')
    if($Name -notin @('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details') -or
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
    $files=@{};$fileReceipts=@{};$defaults=Get-AppearanceVariableDefaults $Name;$variables=@{};$variableTargets=@{};$chosen=@{}
    $collect={param($Map,[bool]$Enabled)
        if($Map -isnot [pscustomobject]){throw 'render-inject-map-invalid'}
        if(@($Map.PSObject.Properties).Count -gt 128){throw 'render-inject-map-too-large'}
        foreach($entry in $Map.PSObject.Properties){
            if($entry.Name.StartsWith('--')){
                $property=[string]$entry.Name
                if($property -cnotmatch '^--[a-z][a-z0-9-]{0,79}$' -or !$defaults.ContainsKey($property) -or $entry.Value -isnot [Array] -or $entry.Value.Count -lt 2 -or $entry.Value.Count -gt 16){throw 'render-variable-definition-invalid'}
                foreach($definitionPart in $entry.Value){if($definitionPart -isnot [string]){throw 'render-variable-definition-invalid'}}
                $null=ConvertTo-AppearanceVariableValue $entry.Value[0]
                if(!$variables.ContainsKey($property)){$variables[$property]='';$variableTargets[$property]=$false}
                $targetsCurrent=$false
                for($targetIndex=1;$targetIndex -lt $entry.Value.Count;$targetIndex++){if($Target -ceq $entry.Value[$targetIndex]){$targetsCurrent=$true;break}}
                if(!$targetsCurrent){continue}
                if(!$variableTargets[$property]){$variables[$property]=if($Config.active){[string]$defaults[$property]}else{''};$variableTargets[$property]=$true}
                if($Enabled){
                    if($chosen.ContainsKey($property) -and $chosen[$property] -cne [string]$entry.Value[0]){throw 'render-variable-selection-conflict'}
                    $chosen[$property]=[string]$entry.Value[0];$variables[$property]=[string]$entry.Value[0]
                }
                continue
            }
            if($entry.Value -isnot [Array] -or @($entry.Value).Count -gt 16){throw 'render-inject-targets-invalid'}
            foreach($injectionTarget in $entry.Value){if($injectionTarget -isnot [string]){throw 'render-inject-targets-invalid'}}
            $applies=$Target -cin @($entry.Value)
            $file=([string]$entry.Name).Replace('\','/')
            # The name/path hash is pure. Validate each distinct normalized file
            # once in this plan; all options still validate their target arrays.
            if(!$files.ContainsKey($file)){$fileReceipts[$file]=Get-AppearanceFileReceipt $Name $file;$files[$file]=$false}
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
            if($selected -isnot [string]){throw 'render-patch-selection-invalid'}
            $selectionMatches=0
            foreach($selectionOption in $values.PSObject.Properties){if($selectionOption.Name -ceq $selected){$selectionMatches++}}
            if($selectionMatches -ne 1){throw 'render-patch-selection-invalid'}
            foreach($option in $values.PSObject.Properties){& $collect $option.Value ([bool]($Config.active -and $option.Name -ceq $selected))}
        }
    }
    if(!$files.ContainsKey('shared.css')){throw 'render-shared-file-missing'}
    $rows=@(foreach($file in @($files.Keys|Sort-Object -CaseSensitive)){
        $contentProperty=$ContentIndex[$Name+'/'+$file]
        if($contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$'){throw 'render-content-file-not-managed'}
        [pscustomobject]@{path=$file;property=$fileReceipts[$file];contentProperty=$contentProperty;enabled=[bool]$files[$file]}
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
function Get-AppearanceRenderExpectation($Status,[string]$Target='SP',[Collections.Generic.Dictionary[string,string]]$FileReceipts=$null,[hashtable]$ContentIndex=$null) {
    if($Target -cnotin @('SP','MainMenu','QuickAccess')){throw 'render-expectation-target-invalid'}
    $names=@('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details')
    if($null -eq $ContentIndex){$ContentIndex=Get-AppearanceContentIndex}
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
            if($null -ne $FileReceipts){
                $key=$name+'/'+$file.path;$property=''
                if(!$FileReceipts.TryGetValue($key,[ref]$property)){
                    $property=Get-AppearanceFileReceipt $name $file.path
                    $FileReceipts.Add($key,$property)
                }
            }else{$property=Get-AppearanceFileReceipt $name $file.path}
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
            $normalizedValue=ConvertTo-AppearanceVariableValue $variable.value
            $seenVariables[$variable.property]=$true;$properties[$variable.property]=$true;$count++;if($count -gt 256){throw 'render-plan-too-large'}
            [pscustomobject]@{name=$name;property=$variable.property;enabled=[bool]$variable.enabled;kind='variable';value=[string]$variable.value;normalizedValue=$normalizedValue;path=''}
        }
    }
}

function Get-AppearanceRenderedStatus($Session,$Status) {
    $probe=[pscustomobject]@{checked=$false;confirmed=$false;reason='render-unconfirmed';mismatched=@();pages=0;mainPages=0;contextPages=0;contextProtocol=1;contextComplete=$false;sequential=$true}
    try {
        if(!$Session -or !$Session.pid -or !$Session.birth -or !$Session.root -or !$Session.processEpoch -or !$Session.accountEpoch -or !$Session.powerGeneration){throw 'render-session-unconfirmed'}
        $expectations=@{};$plans=@{}
        # Build all three plans from the same validated index for this document observation.
        $contentIndex=Get-AppearanceContentIndex
        $fileReceipts=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
        foreach($target in @('SP','MainMenu','QuickAccess')){
            $expected=@(Get-AppearanceRenderExpectation $Status $target $fileReceipts $contentIndex)
            $properties=[string[]]::new($expected.Count)
            $files=[Collections.Generic.List[object]]::new()
            $fileProperties=[Collections.Generic.List[string]]::new()
            for($i=0;$i -lt $expected.Count;$i++){
                $row=$expected[$i];$properties[$i]=$row.property
                if($row.kind -eq 'file'){$files.Add($row);$fileProperties.Add($row.property)}
            }
            $expectations[$target]=@{rows=$expected;files=$files.ToArray()}
            $plans[$target]=@{properties=$properties;fileCounts=$fileProperties.ToArray()}
        }
        $observed=Invoke-AppearanceSharedSteam @{operation='documents';identity=$Session;plans=$plans}
        if($observed.protocol -ne 1 -or $observed.sequential -ne $true -or $observed.complete -isnot [bool] -or $observed.documents -isnot [Array] -or !$observed.documents.Count -or $observed.documents.Count -gt 12){throw 'render-shared-receipt-invalid'}
        $bad=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        $ids=@{}
        foreach($document in $observed.documents){
            $target=[string]$document.kind
            if(!$expectations.ContainsKey($target) -or $document.id -isnot [string] -or $document.id -cnotmatch '^[A-Za-z0-9_-]{1,128}$' -or $ids.ContainsKey($document.id)){throw 'render-target-id-invalid'}
            $ids[$document.id]=$true;$expected=$expectations[$target].rows;$fileRows=$expectations[$target].files;$reply=$document.value
            if(!$reply -or $reply.documentKey -isnot [string] -or !$reply.documentKey.Length -or $reply.documentKey.Length -gt 96 -or !(Test-AppearanceReceiptInteger $reply.protocol 4 4) -or $reply.ready -isnot [bool] -or !$reply.ready -or !(Test-AppearanceReceiptInteger $reply.styles 0 16384) -or $reply.values -isnot [Array] -or $reply.values.Count -ne $expected.Count){throw 'render-receipt-invalid'}
            for($index=0;$index -lt $expected.Count;$index++){
                $value=$reply.values[$index]
                if($value -isnot [string] -or $value.Length -gt 96){throw 'render-receipt-value-invalid'}
                $ok=if($expected[$index].kind -eq 'variable'){(ConvertTo-AppearanceVariableValue $value) -ceq $expected[$index].normalizedValue}elseif($expected[$index].enabled){$value -ceq '1'}else{$value.Length -eq 0}
                if(!$ok){$null=$bad.Add([string]$expected[$index].name)}
            }
            if($reply.fileCounts -isnot [Array] -or $reply.fileCounts.Count -ne $fileRows.Count){throw 'render-content-counts-invalid'}
            for($i=0;$i -lt $fileRows.Count;$i++){
                $copies=$reply.fileCounts[$i]
                if(!(Test-AppearanceReceiptInteger $copies 0 16384)){throw 'render-content-count-invalid'}
                if($copies -ne $(if($fileRows[$i].enabled){1}else{0})){$null=$bad.Add([string]$fileRows[$i].name)}
            }
            if($reply.styles -eq 0){foreach($row in $expected){if($row.enabled){$null=$bad.Add([string]$row.name)}}}
            $probe.pages++
            if($target -ceq 'SP'){$probe.mainPages++}else{$probe.contextPages++}
        }
        if(!$probe.mainPages -or $probe.mainPages -gt 4 -or $probe.contextPages -gt 8){throw 'render-main-target-unconfirmed'}
        $probe.contextComplete=$observed.complete
        $probe.checked=$true;$probe.mismatched=@($Status.themes|Where-Object {$bad.Contains([string]$_.name)}|ForEach-Object name|Select-Object -Unique)
        $probe.confirmed=$probe.mismatched.Count -eq 0
        $probe.reason=if(!$probe.confirmed){'render-theme-receipts-mismatch'}elseif($observed.complete){'render-receipts-confirmed'}else{'render-observed-documents-confirmed-contexts-partial'}
    }catch {
        $probe.checked=$false;$probe.confirmed=$false;$probe.mismatched=@()
        $probe.reason=if($_.Exception.Message -match '^render-[a-z-]+$'){$_.Exception.Message}else{'render-shared-steam-observation-failed'}
    }
    return $probe
}
function Test-AppearanceRendered($Session,$Status) {
    return [bool](Get-AppearanceRenderedStatus $Session $Status).confirmed
}
