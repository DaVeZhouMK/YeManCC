# Read-only packaging check: PowerShell scripts and their theme payload ship together.
function Get-SteamAppearanceFileHash([string]$Path) {
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
    try { ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally {$stream.Dispose();$sha.Dispose()}
}
function Get-SteamAppearanceFileReceiptProperty([string]$Relative) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {'--ymcc-file-'+([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Relative.Replace('\','/').ToLowerInvariant())))).Replace('-','').Substring(0,16).ToLowerInvariant()}finally{$sha.Dispose()}
}
function Get-SteamAppearanceContentProperty([string]$Path) {
    $text=[IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8)
    $pattern='(?m)^/\* YMCC content receipt: exact bundled file revision\. \*/\n:root \{ --ymcc-content-[0-9a-f]{16}: 1; \}\n\z'
    $match=[regex]::Match($text,$pattern)
    if(!$match.Success -or [regex]::Matches($text,'--ymcc-content-[0-9a-f]{16}\s*:').Count -ne 1){throw "Steam appearance content marker missing or repeated: $Path"}
    $body=$text.Substring(0,$match.Index);$sha=[Security.Cryptography.SHA256]::Create()
    try {'--ymcc-content-'+([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($body)))).Replace('-','').Substring(0,16).ToLowerInvariant()}finally{$sha.Dispose()}
}
function Get-SteamAppearanceExportFiles([string]$PowerControl) {
    $root=[IO.Path]::GetFullPath($PowerControl).TrimEnd('\')
    $required=@('steam-appearance\backend.path','steam-appearance.ps1','steam-appearance\preset.ps1','steam-appearance\idle.ps1','steam-appearance\resident.ps1','steam-appearance\manifest.json')
    foreach($file in $required){if(!(Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)){throw "Steam appearance component missing: $file"}}
    # These modules are dot-sourced before the action dispatcher; a syntax error
    # in either prevents all appearance actions, even when capability markers exist.
    foreach($module in @('steam-appearance\preset.ps1','steam-appearance\resident.ps1')){
        $moduleTokens=$null;$moduleErrors=$null
        [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $root $module),[ref]$moduleTokens,[ref]$moduleErrors)
        if($moduleErrors.Count){throw "Steam appearance module has syntax errors: $module"}
    }
    if(Test-Path -LiteralPath (Join-Path $root 'YeManSteam\CssLoader-Standalone-Headless.txt')){throw 'Retired CSSLoader linked preset must not be exported'}
    $entry=Join-Path $root 'steam-appearance.ps1';$tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($entry,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'Steam appearance script has syntax errors'}
    $parameters=@($ast.ParamBlock.Parameters|ForEach-Object {$_.Name.VariablePath.UserPath})
    foreach($name in @('Action','Payload','BackendPath','SessionPid','SessionBirth','ReadyEvent','IdleEvent','GateTimeoutMs','StartBackend','VerifyOnly')){if($name -notin $parameters){throw "Steam appearance script protocol mismatch: missing $name"}}
    if([IO.File]::ReadAllText($entry) -notmatch "'Automatic'"){throw 'Steam appearance automatic use component missing'}
    if([IO.File]::ReadAllText($entry) -notmatch 'AutomaticResultProtocol\s*=\s*2'){throw 'Steam appearance bounded automatic result protocol missing'}
    if([IO.File]::ReadAllText($entry) -notmatch 'ManualObservationProtocol\s*=\s*1'){throw 'Steam appearance read-only manual observation protocol missing'}
    if([IO.File]::ReadAllText($entry) -notmatch 'ResidentBackendPolicy\s*=\s*3'){throw 'Steam appearance resident native policy mismatch'}
    if([IO.File]::ReadAllText($entry) -notmatch 'controlProtocol\s*=\s*2'){throw 'Steam appearance script protocol 2 required'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\idle.ps1')) -notmatch 'function Assert-CssDesktopNotEditing'){throw 'Steam appearance Desktop editing protection missing'}
    if([IO.File]::ReadAllText($entry) -notmatch 'Invoke-OwnedPresetResourceUpdate \$current' -or [IO.File]::ReadAllText($entry) -notmatch "Invoke-AppearanceControl 'UpdateOwnedResources'" -or [IO.File]::ReadAllText((Join-Path $root 'steam-appearance\preset.ps1')) -notmatch 'function Invoke-OwnedPresetResourceUpdate'){throw 'Steam appearance owned resource update protocol missing'}
    $refresh=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-AutomaticAppearanceRefresh'},$true)
    if(!$refresh -or $refresh.Extent.Text -notmatch "Invoke-AppearanceControl 'Reconcile'" -or [IO.File]::ReadAllText($entry) -notmatch 'Invoke-AutomaticAppearanceRefresh\|Out-Null'){throw 'Steam appearance automatic reapply protocol missing'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\resident.ps1')) -notmatch 'function Start-ResidentAppearanceBackend'){throw 'Steam appearance resident singleton component missing'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\idle.ps1')) -match 'Stop-VerifiedIdleProcesses|\.Kill\('){throw 'Steam appearance resident policy forbids backend termination'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\idle.ps1')) -notmatch 'AppearanceRenderProtocol\s*=\s*4'){throw 'Steam appearance per-theme render receipt protocol missing'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\idle.ps1')) -notmatch 'AppearanceContextProtocol\s*=\s*1'){throw 'Steam appearance independent context protocol missing'}
    if([IO.File]::ReadAllText((Join-Path $root 'steam-appearance\resident.ps1')) -notmatch 'AppearanceProcessQueryProtocol\s*=\s*1'){throw 'Steam appearance limited process-query protocol missing'}
    $renderTokens=$null;$renderErrors=$null
    $renderAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'steam-appearance\idle.ps1'),[ref]$renderTokens,[ref]$renderErrors)
    if($renderErrors.Count){throw 'Steam appearance renderer has syntax errors'}
    $defaultFunction=$renderAst.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-AppearanceVariableDefaults'},$true)
    if(!$defaultFunction){throw 'Steam appearance variable baseline component missing'}
    $readDefaults=[scriptblock]::Create($defaultFunction.Extent.Text+"
Get-AppearanceVariableDefaults @args")
    $manifest=Get-Content -LiteralPath (Join-Path $root 'steam-appearance\manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $names=@('Art Hero','Centered Game Text','Darken Unfocused Games','Mini Carousel','NEX-85 Keyboard','Round','YMCC Game Details')
    if($manifest.id -ne 'ymcc-steam-appearance' -or $manifest.version -ne 1 -or @($manifest.themes).Count -ne $names.Count){throw 'Steam appearance manifest invalid'}
    if($manifest.renderContentProtocol -ne 1){throw 'Steam appearance content protocol missing'}
    if(Test-Path -LiteralPath (Join-Path $root 'steam-appearance\themes\Game Cover Reflections')){throw 'Retired reflection payload must not ship'}
    $seen=@{};$result=@($required)
    foreach($item in $manifest.files){
        $relative=([string]$item.path).Replace('/','\')
        if(!$relative -or $relative -match '[:\x00-\x1f]' -or [IO.Path]::IsPathRooted($relative) -or @($relative.Split('\')|Where-Object {$_ -in @('','..','.')}).Count -or $relative.Split('\')[0] -notin $names -or $seen.ContainsKey($relative)){throw "Steam appearance manifest path invalid: $relative"}
        $seen[$relative]=$true;$file=Join-Path $root ('steam-appearance\themes\'+$relative)
        if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Steam appearance file missing or linked: $relative"}
        if((Get-SteamAppearanceFileHash $file) -ne $item.sha256 -or (Get-Item -LiteralPath $file).Length -ne $item.bytes){throw "Steam appearance checksum mismatch: $relative"}
        if([IO.Path]::GetFileName($relative) -eq 'shared.css'){
            $name=$relative.Split('\')[0];$property='--ymcc-receipt-'+($name.ToLowerInvariant() -replace '[^a-z0-9]+','-')
            $shared=[IO.File]::ReadAllText($file)
            if($shared -notmatch ([regex]::Escape($property)+':\s*1\s*;')){throw "Steam appearance render receipt missing: $name"}
            $defaults=& $readDefaults $name
            if($defaults.Count){
                $rootRule=[regex]::Match($shared,'(?s)\A\s*:root\s*\{([^{}]*)\}')
                if(!$rootRule.Success){throw "Steam appearance variable baseline missing: $name"}
                foreach($key in $defaults.Keys){
                    $value=[regex]::Match($rootRule.Groups[1].Value,('(?m)^\s*'+[regex]::Escape($key)+'\s*:\s*([^;{}]+);'))
                    if(!$value.Success -or $value.Groups[1].Value.Trim() -cne $defaults[$key]){throw "Steam appearance variable baseline mismatch: $name / $key"}
                }
            }
        }
        if([IO.Path]::GetExtension($relative) -ieq '.css'){
            $fileReceipt=Get-SteamAppearanceFileReceiptProperty $relative
            if([IO.File]::ReadAllText($file) -notmatch ([regex]::Escape($fileReceipt)+':\s*1\s*;')){throw "Steam appearance file receipt missing: $relative"}
            $contentProperty=Get-SteamAppearanceContentProperty $file
            if($item.contentProperty -cne $contentProperty -or [IO.File]::ReadAllText($file) -notmatch ([regex]::Escape($contentProperty)+':\s*1\s*;')){throw "Steam appearance content revision mismatch: $relative"}
        }
        $result+=('steam-appearance\themes\'+$relative)
    }
    foreach($name in $names){
        if(@($manifest.themes|Where-Object name -eq $name).Count -ne 1 -or !$seen.ContainsKey($name+'\theme.json') -or !$seen.ContainsKey($name+'\config_USER.json')){throw "Steam appearance theme incomplete: $name"}
        $config=Get-Content -LiteralPath (Join-Path $root ('steam-appearance\themes\'+$name+'\config_USER.json')) -Raw -Encoding UTF8|ConvertFrom-Json
        if($config.active -isnot [bool] -or !$config.active){throw "Steam appearance default must enable all managed themes: $name"}
    }
    return $result
}
function Assert-SteamAppearanceExport([string]$SourcePowerControl,[string]$DestinationPowerControl) {
    if(Test-Path -LiteralPath (Join-Path $DestinationPowerControl 'steam-appearance\themes\Game Cover Reflections')){throw 'Retired reflection payload remains in export destination'}
    $files=@(Get-SteamAppearanceExportFiles $SourcePowerControl)
    foreach($relative in $files){
        $source=Join-Path $SourcePowerControl $relative;$destination=Join-Path $DestinationPowerControl $relative
        if(!(Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-SteamAppearanceFileHash $source) -ne (Get-SteamAppearanceFileHash $destination)){throw "Steam appearance export is mixed or incomplete: $relative"}
    }
    $themeCount=@((Get-Content -LiteralPath (Join-Path $SourcePowerControl 'steam-appearance\manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json).themes).Count
    [pscustomobject]@{ok=$true;controlProtocol=2;themes=$themeCount;defaultsEnabled=$themeCount;files=$files.Count}
}
