# Shared with src/bridge/gamedetect.ts; the JSON is the sole title-normalization policy.
$script:YmccGameTitlePolicy=$null
function ConvertTo-YmccGameTitle([string]$Value) {
    if(-not $script:YmccGameTitlePolicy){
        $policyPath=Join-Path $PSScriptRoot 'GameTitlePolicy.json'
        $script:YmccGameTitlePolicy=[IO.File]::ReadAllText($policyPath,[Text.Encoding]::UTF8)|ConvertFrom-Json
        if($script:YmccGameTitlePolicy.schema -ne 1){throw '共享游戏名称规则版本无效'}
    }
    $title=([regex]::Replace($Value.Trim(),'(?i)\.exe$',''))
    do {
        $before=$title
        foreach($pattern in $script:YmccGameTitlePolicy.launchSuffixes){$title=([regex]::Replace($title,$pattern,'',[Text.RegularExpressions.RegexOptions]::IgnoreCase)).Trim()}
    }while($title -cne $before)
    $title=([regex]::Replace($title,'\s+',' ')).Trim()
    $title=([regex]::Replace($title,[string]$script:YmccGameTitlePolicy.copyrightSuffix,'',[Text.RegularExpressions.RegexOptions]::IgnoreCase)).Trim()
    return ([regex]::Replace($title,'[|:,-]+\s*$','')).Trim()
}
