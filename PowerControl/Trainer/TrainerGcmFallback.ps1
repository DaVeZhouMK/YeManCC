# Public author-source CT fallback for exact GCM resource records. No GCM signing secrets.
function Find-GcmPublicCtCandidate([string]$DatabasePath,[string[]]$Aliases,[string]$SettingsRoot) {
    $settings=Read-TrainerJson (Join-Path $SettingsRoot 'settings.json') 65536
    if((Get-TrainerProperty $settings 'enableGCM' $true) -eq $false){return $null}
    $keys=@($Aliases|ForEach-Object{Get-TrainerNameKey $_})
    $matches=@(Get-GcmCatalogRows $DatabasePath 'gcm_trainers.json'|Where-Object{
        (Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'game_name' ''))) -in $keys -and
        (Get-TrainerProperty $_ 'origin' '') -eq 'ct_other' -and
        [string](Get-TrainerProperty $_ 'gcm_url' '') -match '^GCM/GCM Trainers/[^\\/:]+\.ct$' -and
        [string](Get-TrainerProperty $_ 'author' '') -match '^[\p{L}\p{N}_ .-]{1,60}$' -and
        [string](Get-TrainerProperty $_ 'official_url' '') -match '^https://fearlessrevolution\.com/viewtopic\.php\?t=[1-9][0-9]*$'
    })
    $unique=@($matches|Group-Object gcm_url|ForEach-Object{$_.Group[0]})
    if($unique.Count -gt 1){throw 'GCM 中有多个同名 CT 资源，请在管理器确认作者或版本'}
    if($unique.Count -eq 1){return $unique[0]}
    return $null
}
function Assert-TrainerRequestUri($Context,[string]$Url) {
    if((Get-TrainerProperty $Context 'SourcePolicy' '') -ne 'gcm-public-ct'){return Assert-TrainerUri $Url}
    $uri=$null
    if(-not [Uri]::TryCreate($Url,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -ne 'https' -or $uri.Host -ne 'fearlessrevolution.com' -or -not $uri.IsDefaultPort -or $uri.UserInfo -or $uri.Fragment){throw 'CT 链接不属于已确认的作者资源'}
    $allowed=@([string]$Context.SourcePage)
    if($Context.ContainsKey('CtAttachmentUrl')){$allowed+=[string]$Context.CtAttachmentUrl}
    if($uri.AbsoluteUri -cnotin $allowed){throw 'CT 下载跳转离开已确认的页面或附件'}
    return $uri
}
function Get-GcmPublicCtDownload([string]$Html,$Candidate) {
    $expected=[string]$Candidate.game_name;$author=[string]$Candidate.author
    $title=[regex]::Match($Html,'(?is)<h2\b[^>]*class=["''][^"'']*topic-title[^"'']*["''][^>]*>\s*<a\b[^>]*>(?<title>.*?)</a>')
    $actual=[Net.WebUtility]::HtmlDecode(($title.Groups['title'].Value -replace '<[^>]+>','')).Trim()
    $suffix=' ['+$author+']'
    if(-not $title.Success -or -not $actual.EndsWith($suffix,[StringComparison]::OrdinalIgnoreCase) -or (Get-TrainerNameKey $actual.Substring(0,$actual.Length-$suffix.Length)) -ne (Get-TrainerNameKey $expected)){throw 'CT 作者页面与 GCM 游戏或作者不一致'}
    $posts=[regex]::Matches($Html,'(?is)<div\b[^>]*\bid=["'']p[0-9]+["''][^>]*\bclass=["''][^"'']*\bpost\b[^"'']*["''][^>]*>')
    if(-not $posts.Count){throw 'CT 页面未包含原作者帖子'}
    $end=if($posts.Count -gt 1){$posts[1].Index}else{$Html.Length}
    $first=$Html.Substring($posts[0].Index,$end-$posts[0].Index)
    $username=[regex]::Match($first,'(?is)<a\b[^>]*class=["''][^"'']*\busername\b[^"'']*["''][^>]*>(?<name>.*?)</a>')
    if(-not $username.Success -or [Net.WebUtility]::HtmlDecode(($username.Groups['name'].Value -replace '<[^>]+>','')).Trim() -ine $author){throw 'CT 附件并非 GCM 记录的原作者发布'}
    $attachments=@(foreach($anchor in [regex]::Matches($first,'(?is)<a\b[^>]*href=["''](?<url>[^"'']+)["''][^>]*>(?<text>.*?)</a>')){
        $name=[Net.WebUtility]::HtmlDecode(($anchor.Groups['text'].Value -replace '<[^>]+>','')).Trim()
        $url=[Net.WebUtility]::HtmlDecode($anchor.Groups['url'].Value)
        if($name -notmatch '^[^\\/:<>"|?*]+\.ct$'){continue}
        $uri=[Uri]::new([Uri]$Candidate.official_url,$url)
        if($uri.Scheme -ne 'https' -or $uri.Host -ne 'fearlessrevolution.com' -or $uri.AbsolutePath -ne '/download/file.php' -or $uri.UserInfo -or -not $uri.IsDefaultPort){continue}
        $id=[regex]::Match($uri.Query,'(?:^\?|&)id=([1-9][0-9]*)(?:&|$)')
        if($id.Success){[pscustomobject]@{name=$name;url=('https://fearlessrevolution.com/download/file.php?id='+$id.Groups[1].Value);version=[string](Get-TrainerProperty $Candidate 'version' '')}}
    })
    $attachments=@($attachments|Group-Object url|ForEach-Object{$_.Group[0]})
    if($attachments.Count -ne 1){throw '原作者帖子中没有唯一 CT 附件，请在管理器确认版本'}
    return $attachments[0]
}
function Test-TrainerCt([string]$Path) {
    if(-not [IO.File]::Exists($Path) -or (Get-Item -LiteralPath $Path).Length -gt 8388608){return $false}
    $reader=$null
    try {
        $settings=[Xml.XmlReaderSettings]::new();$settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit;$settings.XmlResolver=$null;$settings.MaxCharactersInDocument=8388608
        $reader=[Xml.XmlReader]::Create($Path,$settings)
        $document=[Xml.XmlDocument]::new();$document.XmlResolver=$null;$document.Load($reader)
        return $document.DocumentElement.Name -ceq 'CheatTable' -and $null -ne $document.DocumentElement.SelectSingleNode('CheatEntries')
    }catch{return $false}finally{if($reader){$reader.Dispose()}}
}
