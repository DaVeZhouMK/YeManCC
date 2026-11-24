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
function Invoke-AppearanceIdle([uint32]$ExpectedPid=0,[string]$ExpectedBirth='',[string]$ExpectedRevision='',[string]$ExpectedTarget='',[switch]$DeferDomMismatch,[switch]$RequireSettledTransaction,[switch]$UseScopedStatusIdentity,[hashtable]$AutomaticPlanMemo) {
    Assert-CssDesktopNotEditing
    $script:AppearanceHelperPhase=3
    if($UseScopedStatusIdentity -and $RequireSettledTransaction -and $script:AppearanceObservationOnly){
        # Admission, files, CSS flags and the DOM must still be read afresh.
        # Only pure plans may be reused after rechecking their input bytes.
        $identity=@{pid=$ExpectedPid;birth=$ExpectedBirth;session=$null}
        $status=Get-AppearanceStatus -FinalIdleIdentity $identity -AutomaticPlanMemo $AutomaticPlanMemo
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
    if($RequireSettledTransaction -and (Test-PresetTransactionPending)){Stop-AppearanceIdle 3 'Appearance transaction unfinished; CSS remains resident'}
    $script:AppearanceHelperFlags=3
    $script:AppearanceHelperPhase=5
    if($DeferDomMismatch){
        $render=Get-AppearanceRenderedStatus $session $status $AutomaticPlanMemo
        # Lost transport can recheck only the gated, read-only final snapshot.
        if(!$render.confirmed -and (($render.checked -and $render.reason -ceq 'render-theme-receipts-mismatch') -or
           (!$render.checked -and (Test-AppearanceDocumentRecheck $render.reason -FinalReadOnly:($RequireSettledTransaction -and $script:AppearanceObservationOnly))))){
            return [pscustomobject]@{ok=$false;retryDom=$true}
        }
        $confirmed=$render.confirmed
    }else{$confirmed=Test-AppearanceRendered $session $status $AutomaticPlanMemo}
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
        # The index only needs a valid path; the plan computes its file receipt.
        Get-AppearanceFileReceipt $parts[0] $parts[1] -ValidateOnly
        if($result.ContainsKey($entry.path) -or $entry.contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$' -or $properties.ContainsKey($entry.contentProperty)){throw 'render-content-index-duplicate-or-invalid'}
        $result[$entry.path]=[string]$entry.contentProperty;$properties[$entry.contentProperty]=$true
    }
    if($result.Count -ne 43){throw 'render-content-index-incomplete'}
    $after=Get-Item -LiteralPath $item.FullName -Force -ErrorAction Stop
    if(($after.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ([string]$after.Length+':'+$after.LastWriteTimeUtc.Ticks+':'+$after.CreationTimeUtc.Ticks) -cne $metadata){throw 'render-content-index-changed'}
    $script:AppearanceContentIndex=$result;$script:AppearanceContentIndexStamp=$stamp
    return $result
}
function Get-AppearanceFileReceipt([string]$Name,[string]$Relative,[switch]$ValidateOnly) {
    $relativePath=$Relative.Replace('\','/')
    if($Name -notin @('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details') -or
       $relativePath -notmatch '^[A-Za-z0-9][A-Za-z0-9_./ -]{0,127}\.css$'){throw 'render-file-path-invalid'}
    foreach($part in $relativePath.Split('/')){
        if($part -in @('','..','.')){throw 'render-file-path-invalid'}
    }
    if($ValidateOnly){return}
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
function Get-AppearanceThemeRenderPlan([string]$Name,$Manifest,$Config,$ContentIndex=$null,[string]$Target='SP',[switch]$ValidateOnly) {
    if($Target -cnotin @('SP','MainMenu','QuickAccess')){throw 'render-plan-target-invalid'}
    if(!$Manifest -or $Manifest.name -cne $Name -or !$Config -or $Config.active -isnot [bool]){throw 'render-plan-source-invalid'}
    if(!$ContentIndex){$ContentIndex=Get-AppearanceContentIndex}
    # These values depend on the theme and exact normalized path, not the target.
    $fileReceipts=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $defaults=Get-AppearanceVariableDefaults $Name
    if($ValidateOnly){
        Get-AppearanceTargetRenderPlan $Name $Manifest $Config $ContentIndex $Target $fileReceipts $defaults -ValidateOnly
        return
    }
    Get-AppearanceTargetRenderPlan $Name $Manifest $Config $ContentIndex $Target $fileReceipts $defaults
}
function Get-AppearanceTargetRenderPlan([string]$Name,$Manifest,$Config,$ContentIndex,[string]$Target,[Collections.Generic.Dictionary[string,string]]$FileReceipts,$Defaults,[switch]$ValidateOnly) {
    $files=@{};$variables=@{};$variableTargets=@{};$chosen=@{}
    $projectMenus=!$ValidateOnly -and $Target -ceq 'SP'
    $otherSelections=if($Target -ceq 'SP'){@{}}
    if($projectMenus){$otherFiles=@{};$otherVariableTargets=@{}}
    # Validate the base injection before any patches; visit each option in
    # order without creating a PowerShell scope for each injection map.
    $patches=@()
    for($patchIndex=-1;$patchIndex -lt $patches.Count;$patchIndex++){
        if($patchIndex -lt 0){$options=@($null)}
        else{
            $patch=$patches[$patchIndex]
            $values=$patch.Value.values
            if($values -isnot [pscustomobject]){throw 'render-patch-values-invalid'}
            $options=@($values.PSObject.Properties)
            if($options.Count -gt 256){throw 'render-patch-values-invalid'}
            $configured=$Config.PSObject.Properties[$patch.Name]
            $selected=if($configured){$configured.Value}else{$patch.Value.default}
            if($selected -isnot [string]){throw 'render-patch-selection-invalid'}
            $selectionMatches=0
            foreach($selectionOption in $options){if($selectionOption.Name -ceq $selected){$selectionMatches++}}
            if($selectionMatches -ne 1){throw 'render-patch-selection-invalid'}
        }
        foreach($option in $options){
            if($null -eq $option){$map=$Manifest.inject;$enabled=$Config.active}
            else{$map=$option.Value;$enabled=$Config.active -and $option.Name -ceq $selected}
            if($map -isnot [pscustomobject]){throw 'render-inject-map-invalid'}
            $entries=@($map.PSObject.Properties)
            if($entries.Count -gt 128){throw 'render-inject-map-too-large'}
            foreach($entry in $entries){
                $definition=$entry.Value
                if($entry.Name.StartsWith('--')){
                    $property=[string]$entry.Name
                    if($property -cnotmatch '^--[a-z][a-z0-9-]{0,79}$' -or !$defaults.ContainsKey($property) -or $definition -isnot [Array] -or $definition.Length -lt 2 -or $definition.Length -gt 16){throw 'render-variable-definition-invalid'}
                    $value=$definition[0]
                    if($value -isnot [string]){throw 'render-variable-definition-invalid'}
                    $targetsCurrent=$false
                    for($i=1;$i -lt $definition.Length;$i++){
                        $destination=$definition[$i]
                        if($destination -isnot [string]){throw 'render-variable-definition-invalid'}
                        if($Target -ceq $destination){$targetsCurrent=$true}
                    }
                    $null=ConvertTo-AppearanceVariableValue $value
                    if(!$variables.ContainsKey($property)){$variables[$property]='';$variableTargets[$property]=$false}
                    # Only selection conflicts depend on the document. Keep the
                    # home page's validation order, then report menu conflicts.
                    if($null -ne $otherSelections -and ($enabled -or $projectMenus)){
                        for($i=1;$i -lt $definition.Length;$i++){
                            $other=$definition[$i]
                            if($other -cne 'MainMenu' -and $other -cne 'QuickAccess'){continue}
                            $key=$other+'/'+$property
                            if($projectMenus){$otherVariableTargets[$key]=$true}
                            if($enabled){
                                if($otherSelections.ContainsKey($key) -and $otherSelections[$key] -cne $value){$otherSelections.conflict=$true}
                                $otherSelections[$key]=$value
                            }
                        }
                    }
                    if(!$targetsCurrent){continue}
                    if(!$variableTargets[$property]){$variables[$property]=if($Config.active){[string]$defaults[$property]}else{''};$variableTargets[$property]=$true}
                    if($enabled){
                        if($chosen.ContainsKey($property) -and $chosen[$property] -cne $value){throw 'render-variable-selection-conflict'}
                        $chosen[$property]=$value;$variables[$property]=$value
                    }
                    continue
                }
                if($definition -isnot [Array] -or $definition.Length -gt 16){throw 'render-inject-targets-invalid'}
                $file=([string]$entry.Name).Replace('\','/')
                $applies=$false
                foreach($injectionTarget in $definition){
                    if($injectionTarget -isnot [string]){throw 'render-inject-targets-invalid'}
                    if($injectionTarget -ceq $Target){$applies=$true}
                    if($projectMenus -and $enabled -and ($injectionTarget -ceq 'MainMenu' -or $injectionTarget -ceq 'QuickAccess')){$otherFiles[$injectionTarget+'/'+$file]=$true}
                }
                if(!$files.ContainsKey($file)){
                    if(!$FileReceipts.ContainsKey($file)){$FileReceipts.Add($file,(Get-AppearanceFileReceipt $Name $file))}
                    $files[$file]=$false
                }
                if($enabled -and $applies){$files[$file]=$true}
                if($files.Count -gt 128){throw 'render-plan-too-large'}
            }
        }
        if($patchIndex -lt 0 -and $Manifest.patches){
            if($Manifest.patches -isnot [pscustomobject]){throw 'render-patch-map-invalid'}
            $patches=@($Manifest.patches.PSObject.Properties)
            if($patches.Count -gt 64){throw 'render-patch-map-invalid'}
        }
    }
    if(!$files.ContainsKey('shared.css')){throw 'render-shared-file-missing'}
    $rows=@(foreach($file in @($files.Keys|Sort-Object -CaseSensitive)){
        $contentProperty=$ContentIndex[$Name+'/'+$file]
        if($contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$'){throw 'render-content-file-not-managed'}
        if(!$ValidateOnly){[pscustomobject]@{path=$file;property=$FileReceipts[$file];contentProperty=$contentProperty;enabled=[bool]$files[$file]}}
    })
    if($variables.Count -ne $defaults.Count){throw 'render-variable-definitions-incomplete'}
    if($otherSelections -and $otherSelections.conflict){throw 'render-variable-selection-conflict'}
    if($ValidateOnly){return}
    $variableRows=@(foreach($property in @($variables.Keys|Sort-Object -CaseSensitive)){
        # Shared CSS can define a default even when the selected dynamic patch
        # only targets SP. Other documents must retain that static fallback.
        $enabled=[bool]($Config.active -and ($variableTargets[$property] -or $files['shared.css']))
        $value=if(!$variableTargets[$property] -and $enabled){[string]$defaults[$property]}else{[string]$variables[$property]}
        [pscustomobject]@{property=$property;enabled=$enabled;value=$value}
    })
    if(!$projectMenus){return [pscustomobject]@{protocol=4;files=$rows;variables=$variableRows}}
    # The menus use the same validated and sorted resource set with independent selections.
    $contexts=[ordered]@{}
    foreach($context in 'MainMenu','QuickAccess'){
        $contextFiles=@(foreach($row in $rows){
            [pscustomobject]@{path=$row.path;property=$row.property;contentProperty=$row.contentProperty;enabled=$otherFiles.ContainsKey($context+'/'+$row.path)}
        })
        $shared=$otherFiles.ContainsKey($context+'/shared.css')
        $contextVariables=@(foreach($row in $variableRows){
            $property=$row.property;$key=$context+'/'+$property
            $enabled=[bool]($Config.active -and ($shared -or $otherVariableTargets.ContainsKey($key)))
            $value=if(!$enabled){''}elseif($otherSelections.ContainsKey($key)){[string]$otherSelections[$key]}else{[string]$defaults[$property]}
            [pscustomobject]@{property=$property;enabled=$enabled;value=$value}
        })
        $contexts[$context]=[pscustomobject]@{protocol=4;files=$contextFiles;variables=$contextVariables}
    }
    [pscustomobject]@{protocol=4;files=$rows;variables=$variableRows;contextProtocol=1;contexts=[pscustomobject]$contexts}
}
function Get-AppearanceRenderExpectation($Status,[string]$Target='SP',[Collections.Generic.Dictionary[string,string]]$FileReceipts=$null,[hashtable]$ContentIndex=$null) {
    if($Target -cnotin @('SP','MainMenu','QuickAccess')){throw 'render-expectation-target-invalid'}
    $names=@('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details')
    if($null -eq $ContentIndex){$ContentIndex=Get-AppearanceContentIndex}
    $rows=@($Status.themes);$count=0;$properties=@{}
    if($rows.Count -ne $names.Count){throw 'render-snapshot-incomplete'}
    foreach($name in $names){
        $row=$null
        foreach($candidate in $rows){
            if($candidate.name -ceq $name){
                if($null -ne $row){throw 'render-snapshot-invalid'}
                $row=$candidate
            }
        }
        if($null -eq $row -or $row.installed -isnot [bool] -or !$row.installed -or $row.configured -isnot [bool] -or $row.error){throw 'render-snapshot-invalid'}
        $plan=$row.renderPlan
        if($Target -cne 'SP'){
            if(!$plan -or !(Test-AppearanceReceiptInteger $plan.contextProtocol 1 1) -or $plan.contexts -isnot [pscustomobject] -or @($plan.contexts.PSObject.Properties).Count -ne 2 -or !$plan.contexts.PSObject.Properties[$Target]){throw 'render-context-plan-unavailable'}
            $plan=$plan.contexts.PSObject.Properties[$Target].Value
        }
        if(!$plan -or !(Test-AppearanceReceiptInteger $plan.protocol 4 4) -or $plan.files -isnot [Array] -or !$plan.files.Count -or $plan.files.Count -gt 128){throw 'render-plan-unavailable'}
        $paths=@{}
        foreach($file in $plan.files){
            if($file -isnot [pscustomobject] -or $file.path -isnot [string] -or $file.property -isnot [string] -or $file.enabled -isnot [bool]){throw 'render-plan-file-invalid'}
            $key=$name+'/'+$file.path
            if($null -ne $FileReceipts){
                $property=''
                if(!$FileReceipts.TryGetValue($key,[ref]$property)){
                    $property=Get-AppearanceFileReceipt $name $file.path
                    $FileReceipts.Add($key,$property)
                }
            }else{$property=Get-AppearanceFileReceipt $name $file.path}
            if($property -cne $file.property -or $properties.ContainsKey($property) -or $paths.ContainsKey($file.path) -or (!$row.configured -and $file.enabled)){throw 'render-plan-file-invalid'}
            if($file.contentProperty -isnot [string] -or $file.contentProperty -cnotmatch '^--ymcc-content-[0-9a-f]{16}$' -or $contentIndex[$key] -cne $file.contentProperty -or $properties.ContainsKey($file.contentProperty)){throw 'render-plan-content-invalid'}
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

function Get-AppearanceDocumentPlan($Status,$ContentIndex,[hashtable]$AutomaticPlanMemo) {
    $plansFromFreshBytes=$null -ne $AutomaticPlanMemo -and $Status.revision -and $Status.targetId
    if($plansFromFreshBytes){
        # Only reuse the unmodified plan objects published by Get-ThemeRows
        # after matching fresh manifest/config bytes. Never memoize a receipt.
        $themes=@($Status.themes)
        $entry=$AutomaticPlanMemo[':document-expectations']
        $hit=$entry -and [object]::ReferenceEquals($entry.index,$ContentIndex) -and $entry.target -ceq $Status.targetId -and $entry.revision -ceq $Status.revision -and $entry.rows.Count -eq $themes.Count
        for($i=0;$i -lt $themes.Count;$i++){
            $theme=$themes[$i]
            if($theme.name -isnot [string] -or !$theme.name -or $theme.installed -isnot [bool] -or !$theme.installed -or $theme.configured -isnot [bool] -or $theme.error){$plansFromFreshBytes=$false;break}
            $source=$AutomaticPlanMemo[$theme.name]
            if(!$source -or !$theme.renderPlan -or ![object]::ReferenceEquals($source.index,$ContentIndex) -or ![object]::ReferenceEquals($source.plan,$theme.renderPlan)){$plansFromFreshBytes=$false;break}
            if($hit -and ($entry.rows[$i].name -cne $theme.name -or $entry.rows[$i].configured -ne $theme.configured -or ![object]::ReferenceEquals($entry.rows[$i].plan,$theme.renderPlan))){$hit=$false}
        }
        if($plansFromFreshBytes -and $hit){return $entry.document}
    }
    $expectations=@{};$plans=@{}
    $fileReceipts=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach($target in @('SP','MainMenu','QuickAccess')){
        $expected=@(Get-AppearanceRenderExpectation $Status $target $fileReceipts $ContentIndex)
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
    $document=@{expectations=$expectations;plans=$plans}
    if($plansFromFreshBytes){
        $AutomaticPlanMemo[':document-expectations']=@{index=$ContentIndex;target=$Status.targetId;revision=$Status.revision;document=$document;
            rows=@(foreach($theme in $themes){@{name=$theme.name;configured=$theme.configured;plan=$theme.renderPlan}})}
    }
    return $document
}

function Get-AppearanceRenderedStatus($Session,$Status,[hashtable]$AutomaticPlanMemo) {
    $probe=[pscustomobject]@{checked=$false;confirmed=$false;reason='render-unconfirmed';mismatched=@();pages=0;mainPages=0;contextPages=0;contextProtocol=1;contextComplete=$false;sequential=$true}
    try {
        if(!$Session -or !$Session.pid -or !$Session.birth -or !$Session.root -or !$Session.processEpoch -or !$Session.accountEpoch -or !$Session.powerGeneration){throw 'render-session-unconfirmed'}
        $contentIndex=Get-AppearanceContentIndex
        $compiled=Get-AppearanceDocumentPlan $Status $contentIndex $AutomaticPlanMemo
        $expectations=$compiled.expectations;$plans=$compiled.plans
        $observed=Invoke-AppearanceSharedSteam @{operation='documents';identity=$Session;plans=$plans}
        if($observed.protocol -ne 1 -or $observed.sequential -ne $true -or $observed.complete -isnot [bool] -or $observed.documents -isnot [Array] -or !$observed.documents.Count -or $observed.documents.Count -gt 12){throw 'render-shared-receipt-invalid'}
        $bad=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $ids=@{}
        foreach($document in $observed.documents){
            $target=[string]$document.kind
            if(!$expectations.ContainsKey($target) -or $document.id -isnot [string] -or $document.id -cnotmatch '^[A-Za-z0-9_-]{1,128}$' -or $ids.ContainsKey($document.id)){throw 'render-target-id-invalid'}
            $ids[$document.id]=$true;$expected=$expectations[$target].rows;$fileRows=$expectations[$target].files;$reply=$document.value
            if(!$reply -or $reply.documentKey -isnot [string] -or !$reply.documentKey.Length -or $reply.documentKey.Length -gt 96 -or !(Test-AppearanceReceiptInteger $reply.protocol 4 4) -or $reply.ready -isnot [bool] -or !$reply.ready -or !(Test-AppearanceReceiptInteger $reply.styles 0 16384) -or $reply.values -isnot [Array] -or $reply.values.Count -ne $expected.Count){throw 'render-receipt-invalid'}
            $values=$reply.values
            for($index=0;$index -lt $expected.Count;$index++){
                $row=$expected[$index];$value=$values[$index]
                if($value -isnot [string] -or $value.Length -gt 96){throw 'render-receipt-value-invalid'}
                if($row.kind -eq 'variable'){
                    # The plan already normalized the exact expected string.
                    $matchesExpectation=[string]::Equals($value,$row.value,[StringComparison]::Ordinal) -or
                        (ConvertTo-AppearanceVariableValue $value) -ceq $row.normalizedValue
                }elseif($row.enabled){$matchesExpectation=$value -ceq '1'}else{$matchesExpectation=$value.Length -eq 0}
                if(!$matchesExpectation){$null=$bad.Add([string]$row.name)}
            }
            $fileCounts=$reply.fileCounts
            if($fileCounts -isnot [Array] -or $fileCounts.Count -ne $fileRows.Count){throw 'render-content-counts-invalid'}
            for($i=0;$i -lt $fileRows.Count;$i++){
                $copies=$fileCounts[$i];$row=$fileRows[$i]
                if(($copies -isnot [int] -and $copies -isnot [long]) -or $copies -lt 0 -or $copies -gt 16384){throw 'render-content-count-invalid'}
                if($copies -ne [int]$row.enabled){$null=$bad.Add([string]$row.name)}
            }
            if($reply.styles -eq 0){foreach($row in $expected){if($row.enabled){$null=$bad.Add([string]$row.name)}}}
            $probe.pages++
            if($target -ceq 'SP'){$probe.mainPages++}else{$probe.contextPages++}
        }
        if(!$probe.mainPages -or $probe.mainPages -gt 4 -or $probe.contextPages -gt 8){throw 'render-main-target-unconfirmed'}
        $probe.contextComplete=$observed.complete
        $probe.checked=$true
        $probe.mismatched=@(foreach($row in $Status.themes){if($bad.Remove([string]$row.name)){$row.name}})
        $probe.confirmed=$probe.mismatched.Count -eq 0
        $probe.reason=if(!$probe.confirmed){'render-theme-receipts-mismatch'}elseif($observed.complete){'render-receipts-confirmed'}else{'render-observed-documents-confirmed-contexts-partial'}
    }catch {
        $probe.checked=$false;$probe.confirmed=$false;$probe.mismatched=@()
        $probe.reason=if($_.Exception.Message -match '^render-[a-z-]+$'){$_.Exception.Message}else{'render-shared-steam-observation-failed'}
    }
    return $probe
}
function Test-AppearanceRendered($Session,$Status,[hashtable]$AutomaticPlanMemo) {
    return [bool](Get-AppearanceRenderedStatus $Session $Status $AutomaticPlanMemo).confirmed
}
