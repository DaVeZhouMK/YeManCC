# Windows PowerShell 5.1. Downloads exact public FLiNG/GCM author-source files; never launches a trainer.
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Get-TrainerNameKey([string]$Name) {
    $value = [Net.WebUtility]::HtmlDecode($Name).Normalize([Text.NormalizationForm]::FormKC)
    $value = $value -replace '(?i)\s+trainer\s*$', '' -replace '修改器\s*$', ''
    # '&' separates semantic words/numerals: I & II must never collapse to III.
    $value = $value.Replace('&', ' and ')
    return ($value.ToLowerInvariant() -replace '[^\p{L}\p{N}]', '')
}
function Get-TrainerHash([string]$Value) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Read-TrainerJson([string]$Path, [int64]$Limit = 8388608) {
    if (-not [IO.File]::Exists($Path)) { return $null }
    if ((Get-Item -LiteralPath $Path).Length -gt $Limit) { throw '修改器缓存文件过大' }
    return ([IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) | ConvertFrom-Json)
}
function Write-TrainerJson([string]$Path, $Value) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 12 -Compress), (New-Object Text.UTF8Encoding($false)))
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp, $Path, [NullString]::Value) }
        else { [IO.File]::Move($temp, $Path) }
    } finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
}
function Get-TrainerProperty($Object, [string]$Name, $Default = $null) {
    if ($Object -is [Collections.IDictionary] -and $Object.Contains($Name)) { return $Object[$Name] }
    if ($null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $Default
}
function Assert-TrainerChild([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) { throw '修改器文件路径超出目标目录' }
    return $full
}
function Assert-TrainerNotReparse([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw '修改器工作目录不能经过链接或联接点' }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Assert-TrainerActive($Context) {
    if ([IO.File]::Exists($Context.CancelPath)) { throw [OperationCanceledException]::new('已取消修改器准备') }
    if ([DateTime]::UtcNow -gt $Context.Deadline) { throw [TimeoutException]::new('修改器准备超时，请稍后重试') }
}
function Send-TrainerProgress($Context, [string]$State, [string]$Message, $Extra = @{}) {
    $result = @{ requestId = $Context.RequestId; gameName = $Context.GameName; ok = $State -eq 'ready'; state = $State; message = $Message; checkedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); gameExecutablePath = [string](Get-TrainerProperty $Context 'GameExecutablePath' ''); gameProcessId = [int](Get-TrainerProperty $Context 'GameProcessId' 0); gameProcessCreated = [string](Get-TrainerProperty $Context 'GameProcessCreated' '') }
    foreach ($key in $Extra.Keys) { $result[$key] = $Extra[$key] }
    Write-TrainerJson $Context.ResultPath $result
    # Keep the most recent per-game result after the renderer removes its IPC receipt.
    # Catalog checks never overwrite game evidence; this is a bounded diagnostic file.
    if($Context.GameName -ne 'YMCC Trainer Catalog'){
        $requests=[IO.Path]::GetDirectoryName($Context.ResultPath)
        Write-TrainerJson (Join-Path ([IO.Path]::GetDirectoryName($requests)) 'last-game-status.json') $result
    }
    return $result
}
function Assert-TrainerUri([string]$Url) {
    $uri = $null
    if (-not [Uri]::TryCreate($Url, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -ne 'https' -or $uri.Host -notin @('flingtrainer.com', 'www.flingtrainer.com') -or
        -not $uri.IsDefaultPort -or $uri.UserInfo -or
        $uri.AbsolutePath -notmatch '^/(all-trainers(?:-a-z)?/?$|trainer/|downloads/|download-trainer\.php$|wp-content/uploads/)') {
        throw '下载链接不属于已支持的风灵月影官网地址'
    }
    return $uri
}
function Get-TrainerResponse($Context, [string]$Url, [string]$Destination = '', [string]$Referer = '') {
    Add-Type -AssemblyName System.Net.Http
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $false
    $handler.AutomaticDecompression = [Net.DecompressionMethods]::GZip -bor [Net.DecompressionMethods]::Deflate
    $client = [Net.Http.HttpClient]::new($handler)
    $requestTimeout = if ($Context.ContainsKey('CatalogResponse')) { 12 } else { 60 }
    $client.Timeout = [TimeSpan]::FromSeconds($requestTimeout)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('YMCC-Trainer/1.0')
    if ($Referer) { $client.DefaultRequestHeaders.Referrer = Assert-TrainerRequestUri $Context $Referer }
    if ($Context.ContainsKey('ConditionalETag') -and $Context.ConditionalETag) { $client.DefaultRequestHeaders.TryAddWithoutValidation('If-None-Match', [string]$Context.ConditionalETag) | Out-Null }
    $limit = if ($Destination) { if ($Context.ContainsKey('MaxDownloadBytes')) { [long]$Context.MaxDownloadBytes } else { 83886080L } } else { if ($Context.ContainsKey('MaxTextBytes')) { [long]$Context.MaxTextBytes } else { 8388608L } }
    try {
        for ($redirect = 0; $redirect -lt 6; $redirect++) {
            Assert-TrainerActive $Context
            Write-Verbose ('Trainer request: ' + $Url)
            $uri = Assert-TrainerRequestUri $Context $Url
            $cts = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($requestTimeout))
            $response = $null
            try {
                $response = $client.GetAsync($uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $cts.Token).GetAwaiter().GetResult()
                $status = [int]$response.StatusCode
                if ($status -in @(301, 302, 303, 307, 308)) {
                    if (-not $response.Headers.Location) { throw '下载服务未返回跳转地址' }
                    $Url = [Uri]::new($uri, $response.Headers.Location).AbsoluteUri
                    continue
                }
                if ($status -eq 304 -and $Context.ContainsKey('CatalogResponse')) { return @{notModified=$true;bytes=0;etag=[string]$response.Headers.ETag} }
                if ($status -ne 200) { throw "修改器资源服务暂时不可用（HTTP $status）" }
                if ($response.Content.Headers.ContentLength -gt $limit) { throw '修改器下载文件超过大小限制' }
                $inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                $outputStream = if ($Destination) { [IO.File]::Open($Destination, [IO.FileMode]::CreateNew) } else { [IO.MemoryStream]::new() }
                try {
                    $buffer = New-Object byte[] 65536
                    $total = 0L
                    $lastProgress = [DateTime]::UtcNow
                    while (($count = $inputStream.ReadAsync($buffer, 0, $buffer.Length, $cts.Token).GetAwaiter().GetResult()) -gt 0) {
                        Assert-TrainerActive $Context
                        $total += $count
                        if ($Destination) { $Context.DownloadBytes = $total }
                        if ($Context.ContainsKey('CatalogResponse')) { $Context.CatalogBytes = $total }
                        if ($total -gt $limit) { throw '修改器下载文件超过大小限制' }
                        $outputStream.Write($buffer, 0, $count)
                        if ($Destination -and ([DateTime]::UtcNow - $lastProgress).TotalSeconds -ge 1) {
                            Send-TrainerProgress $Context 'downloading' ('正在后台下载修改器 · {0:N1} MB' -f ($total / 1MB)) @{ bytesDownloaded = $total } | Out-Null
                            $lastProgress = [DateTime]::UtcNow
                        }
                    }
                    Assert-TrainerActive $Context
                    if ($Destination) { return @{ url = $uri.AbsoluteUri; bytes = $total } }
                    $text = [Text.Encoding]::UTF8.GetString($outputStream.ToArray())
                    if ($Context.ContainsKey('CatalogResponse')) { return @{notModified=$false;text=$text;bytes=$total;etag=[string]$response.Headers.ETag} }
                    return $text
                } finally { $outputStream.Dispose(); $inputStream.Dispose() }
            } finally { if ($response) { $response.Dispose() }; $cts.Dispose() }
        }
        throw '下载地址跳转次数过多'
    } finally { $client.Dispose(); $handler.Dispose() }
}
function Select-TrainerSafeAliases([string[]]$Names, $Rows, $SelectedRows) {
    # Exclude aliases owned by another game/edition, even when reached indirectly
    # from an otherwise unique English name or a verified Steam ID.
    $selected=@{}
    foreach($row in $SelectedRows){
        $scope=[string](Get-TrainerProperty $row 'steam_id' '')+'|'+(Get-TrainerNameKey ([string](Get-TrainerProperty $row 'en_US' '')))
        $selected[$scope]=$true
    }
    $wanted=@{};foreach($name in $Names){$wanted[(Get-TrainerNameKey $name)]=$true}
    $blocked=@{}
    foreach($row in $Rows){
        $scope=[string](Get-TrainerProperty $row 'steam_id' '')+'|'+(Get-TrainerNameKey ([string](Get-TrainerProperty $row 'en_US' '')))
        if($selected.ContainsKey($scope)){continue}
        foreach($language in @('en_US','zh_CN','zh_TW')){
            $key=Get-TrainerNameKey ([string](Get-TrainerProperty $row $language ''))
            if($key -and $wanted.ContainsKey($key)){$blocked[$key]=$true}
        }
    }
    return @($Names|Where-Object{$_ -and -not $blocked.ContainsKey((Get-TrainerNameKey $_))}|Select-Object -Unique)
}
function Resolve-TrainerAliases([string]$GameName, [string]$DatabasePath) {
    $key=Get-TrainerNameKey $GameName
    $rows=@(Get-GcmCatalogRows $DatabasePath 'translations.json')
    $matched=@($rows|Where-Object{
        $row=$_
        @('en_US','zh_CN','zh_TW'|Where-Object{(Get-TrainerNameKey ([string](Get-TrainerProperty $row $_ ''))) -eq $key}).Count -gt 0
    })
    if(-not $matched.Count){return @($GameName)}
    $ids=@($matched|ForEach-Object{[string](Get-TrainerProperty $_ 'steam_id' '')}|Where-Object{$_}|Select-Object -Unique)
    $canonical=@($matched|ForEach-Object{Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))}|Select-Object -Unique)
    if($ids.Count -gt 1 -or $canonical.Count -gt 1){throw '游戏别名对应多个游戏或版本，请先确认游戏身份'}
    $names=@($GameName)+@($matched|ForEach-Object{foreach($language in @('en_US','zh_CN','zh_TW')){[string](Get-TrainerProperty $_ $language '')}})
    $safe=@(Select-TrainerSafeAliases $names $rows $matched)
    if(-not $safe.Count){throw '游戏别名存在冲突，未自动选择修改器'}
    return $safe
}
function ConvertFrom-FlingIndex([string]$Html) {
    $seen = @{}
    foreach ($match in [regex]::Matches($Html, '(?is)<a\b[^>]*href\s*=\s*["''](?<url>https://(?:www\.)?flingtrainer\.com/trainer/[^"'']+)["''][^>]*>(?<text>.*?)</a>')) {
        $name = [Net.WebUtility]::HtmlDecode(($match.Groups['text'].Value -replace '<[^>]+>', '')).Trim()
        if ($name -notmatch '(?i)\s+Trainer$') { continue }
        $url = [Net.WebUtility]::HtmlDecode($match.Groups['url'].Value)
        if ($seen.ContainsKey($url)) { continue }
        Assert-TrainerUri $url | Out-Null
        $seen[$url] = $true
        [pscustomobject]@{ game_name = ($name -replace '(?i)\s+Trainer$', ''); official_url = $url }
    }
}
function Find-FlingCandidate($Rows, [string[]]$Aliases) {
    $keys = @($Aliases | ForEach-Object { Get-TrainerNameKey $_ })
    $matches = @($Rows | Where-Object { (Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'game_name' ''))) -in $keys })
    $unique = @($matches | Group-Object official_url | ForEach-Object { $_.Group[0] })
    if ($unique.Count -gt 1) { throw '找到多个同名修改器，暂不自动选择，请在修改器管理器中确认' }
    if ($unique.Count -eq 1) {
        Assert-TrainerUri ([string]$unique[0].official_url) | Out-Null
        return $unique[0]
    }
    return $null
}
function Get-FlingDownload([string]$Html, [string]$ExpectedName) {
    $heading = [regex]::Match($Html, '(?is)<h1\b[^>]*>(.*?)</h1>')
    $title = [Net.WebUtility]::HtmlDecode(($heading.Groups[1].Value -replace '<[^>]+>', '')).Trim()
    if (-not $heading.Success -or (Get-TrainerNameKey $title) -ne (Get-TrainerNameKey $ExpectedName)) { throw '官网页面游戏名与识别结果不一致，已停止下载' }
    $downloads = @(
        foreach ($row in [regex]::Matches($Html, '(?is)<tr\b[^>]*>.*?</tr>')) {
            $anchor = [regex]::Match($row.Value, '(?is)<a\b(?=[^>]*class=["''][^"'']*attachment-link)[^>]*href=["''](?<url>[^"'']+)["''][^>]*>(?<name>.*?)</a>')
            $date = [regex]::Match($row.Value, '(?is)<td\b[^>]*class=["'']attachment-date["''][^>]*>\s*(?<date>\d{4}-\d{2}-\d{2}(?: \d{2}:\d{2})?)')
            if (-not $anchor.Success -or -not $date.Success) { continue }
            $name = [Net.WebUtility]::HtmlDecode(($anchor.Groups['name'].Value -replace '<[^>]+>', '')).Trim()
            if ($name -notmatch '(?i)trainer' -or $name -match '(?i)wemod|wand|installer|setup') { continue }
            $url = [Net.WebUtility]::HtmlDecode($anchor.Groups['url'].Value)
            Assert-TrainerUri $url | Out-Null
            [pscustomobject]@{ name = $name; url = $url; version = $date.Groups['date'].Value }
        }
    )
    if (-not $downloads.Count) { throw '官网没有可直接后台下载的独立修改器' }
    return ($downloads | Sort-Object version -Descending | Select-Object -First 1)
}
function Get-TrainerLaunchFile([string]$Folder) {
    $metadataPath = Join-Path $Folder 'gcm_info.json'
    if ([IO.File]::Exists($metadataPath)) {
        try {
            $metadata = Read-TrainerJson $metadataPath 65536
            $extension = ([string](Get-TrainerProperty $metadata 'extension' '')).Trim()
            if ($extension -ieq 'none') { return $null }
            if ($extension.Contains('.')) {
                $selected = Assert-TrainerChild (Join-Path $Folder $extension) $Folder
                Assert-TrainerNotReparse $selected
                if ([IO.File]::Exists($selected) -and [IO.Path]::GetExtension($selected) -in @('.exe','.ct','.cetrainer')) { return $selected }
                return $null
            }
            if ($extension -and $extension -notin @('exe','ct','cetrainer')) { return $null }
        } catch { return $null }
    } else { $extension = '' }

    $pending = [Collections.Queue]::new()
    $pending.Enqueue(@{ path = $Folder; depth = 0 })
    $found = [Collections.Generic.List[string]]::new()
    $examined = 0
    while ($pending.Count) {
        $item = $pending.Dequeue()
        foreach ($child in (Get-ChildItem -LiteralPath $item.path -Force -ErrorAction SilentlyContinue)) {
            $examined++
            if ($examined -gt 2048) { return $null }
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            if ($child.PSIsContainer) {
                if ($item.depth -lt 3) { $pending.Enqueue(@{ path = $child.FullName; depth = $item.depth + 1 }) }
            } elseif ($child.Extension -in @('.exe', '.ct', '.cetrainer')) { $found.Add($child.FullName) }
        }
    }
    $files = @($found.ToArray())
    if ($extension) { $files = @($files | Where-Object { [IO.Path]::GetExtension($_) -ieq ('.'+$extension) }) }
    $files = @($files | Where-Object { [IO.Path]::GetFileName($_) -notmatch '(?i)unins|setup|install|updat' })
    $executables = @($files | Where-Object { [IO.Path]::GetExtension($_) -eq '.exe' })
    if ($executables.Count -eq 1) { return $executables[0] }
    if (-not $executables.Count -and $files.Count -eq 1) { return $files[0] }
    return $null
}
function Test-TrainerPe([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 64) { return $false }
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { return $false }
        $stream.Position = 60
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length - 4)) { return $false }
        $stream.Position = $offset
        return $reader.ReadUInt32() -eq 0x00004550
    } finally { $stream.Dispose() }
}
function Test-TrainerLocalLaunchFile([string]$Path) {
    try {
        Assert-TrainerNotReparse $Path
        if(-not [IO.File]::Exists($Path)){return $false}
        $readProbe=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        $readProbe.Dispose()
        switch([IO.Path]::GetExtension($Path).ToLowerInvariant()){
            '.exe' {return Test-TrainerPe $Path}
            '.ct' {return Test-TrainerCt $Path}
            '.cetrainer' {return (Get-Item -LiteralPath $Path).Length -gt 0}
        }
    }catch{throw '本地修改器文件暂不可读取，未重复下载，请稍后重试'}
    return $false
}
function Expand-TrainerZip([string]$Archive, [string]$Destination, $Context) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if ($zip.Entries.Count -gt 1024) { throw '修改器压缩包文件数量异常' }
        $total = 0L
        foreach ($entry in $zip.Entries) {
            Assert-TrainerActive $Context
            $name = $entry.FullName.Replace('/', '\')
            if ($name -match '(^[\\/]|:|(^|\\)\.\.(\\|$))' -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw '修改器压缩包包含不安全路径' }
            $target = Assert-TrainerChild (Join-Path $Destination $name) $Destination
            $total += $entry.Length
            if ($entry.Length -gt 134217728L -or $total -gt 268435456L) { throw '修改器解压大小超过限制' }
            if ($name.EndsWith('\')) { [IO.Directory]::CreateDirectory($target) | Out-Null; continue }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally { $zip.Dispose() }
}
function Find-LocalTrainer([string[]]$Roots, [string[]]$Aliases, [string]$DatabasePath = '') {
    $keys = @($Aliases | ForEach-Object { Get-TrainerNameKey $_ })
    $matches = @()
    foreach ($root in ($Roots | Select-Object -Unique)) {
        if (-not $root -or -not [IO.Directory]::Exists($root)) { continue }
        if ([IO.Path]::GetPathRoot($root).TrimEnd('\') -eq $root.TrimEnd('\')) { continue }
        try { Assert-TrainerNotReparse $root } catch { continue }
        foreach ($folder in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | Select-Object -First 2000)) {
            if ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            if ([IO.File]::Exists((Join-Path $folder.FullName '.ymcc-migrated.json'))) { continue }
            $metadataPath = Join-Path $folder.FullName 'gcm_info.json'
            $metadata = $null
            if ([IO.File]::Exists($metadataPath)) {
                try { $metadata = Read-TrainerJson $metadataPath 65536 } catch { continue }
                $name = [string](Get-TrainerProperty $metadata 'game_name' '')
            } else {
                # A renamed folder or anonymous executable is not a trainer-to-game identity.
                continue
            }
            $resourceId=[string](Get-TrainerProperty $metadata 'gcm_url' '')
            if($DatabasePath -and $resourceId){
                $resourceRows=@(foreach($kind in @('fling_main.json','fling_archive.json','gcm_trainers.json')){
                    Get-GcmCatalogRows $DatabasePath $kind|Where-Object{[string](Get-TrainerProperty $_ 'gcm_url' '') -ceq $resourceId}
                })
                $resourceNames=@($resourceRows|ForEach-Object{Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'game_name' ''))}|Select-Object -Unique)
                if($resourceNames.Count -ne 1){continue}
                # GCM's stable resource ID survives a user's renamed display label.
                # A known ID for another game must not fall back to the misleading label.
                if($resourceNames.Count -eq 1){$name=[string](Get-TrainerProperty $resourceRows[0] 'game_name' '')}
            }
            if (-not $name -or (Get-TrainerNameKey $name) -notin $keys) { continue }
            $launch = Get-TrainerLaunchFile $folder.FullName
            if ($launch -and (Test-TrainerLocalLaunchFile $launch)) {
                $origin = [string](Get-TrainerProperty $metadata 'origin' 'local')
                $rank = if ($origin -in @('fling_main', 'fling_archive', 'ymcc_fling_official') -or $folder.Name -match '^\[(风灵|FLiNG)\]') { 0 } else { 1 }
                $matches += @{ gameName = $name; folder = $folder.FullName; path = $launch; origin = $origin; rank = $rank; modified = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
            }
        }
    }
    $preferred = @($matches | Sort-Object { $_.rank })
    if ($preferred.Count) {
        $best=@($preferred|Where-Object{$_.rank -eq $preferred[0].rank}|Group-Object { [IO.Path]::GetFullPath($_.path).ToLowerInvariant() }|ForEach-Object{$_.Group[0]})
        if($best.Count -gt 1){throw '本地有多个同优先级修改器，请在管理器中确认要保留的版本'}
        return $best[0]
    }
    return $null
}
function Get-GcmTrainerRoot([string]$SettingsRoot, [string]$DefaultRoot) {
    $settings = Read-TrainerJson (Join-Path $SettingsRoot 'settings.json') 65536
    $configured = [string](Get-TrainerProperty $settings 'downloadPath' '')
    $root = if ($configured) { $configured } else { $DefaultRoot }
    if (-not [IO.Path]::IsPathRooted($root)) { throw '修改器管理器下载目录必须是绝对路径' }
    $root = [IO.Path]::GetFullPath($root).TrimEnd('\', '/')
    if ($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\', '/')) { throw '修改器管理器下载目录不能是磁盘根目录' }
    Assert-TrainerNotReparse $root
    return $root
}
function Install-GcmTrainer([string]$SourceFolder, [string]$LaunchPath, [string]$LibraryRoot, [string]$GameName, [string[]]$Aliases, $Metadata, $Context) {
    Assert-TrainerNotReparse $SourceFolder
    $LaunchPath = Assert-TrainerChild $LaunchPath $SourceFolder
    $sha = (Get-FileHash -LiteralPath $LaunchPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $expected = [string](Get-TrainerProperty $Metadata 'sha256' '')
    if ($expected -and $expected -ne $sha) { throw '原修改器文件校验失败，未迁入管理器' }
    $displayName = @($Aliases | Where-Object { $_ -match '[\p{IsCJKUnifiedIdeographs}]' } | Select-Object -First 1)
    $displayName = if ($displayName.Count) { [string]$displayName[0] } else { $GameName }
    $displayName = ($displayName -replace ':', '：' -replace '[<>"/\\|?*\x00-\x1f]', '_').Trim().TrimEnd('.')
    if ($displayName.Length -gt 110) { $displayName = $displayName.Substring(0, 110) }
    [IO.Directory]::CreateDirectory($LibraryRoot) | Out-Null
    $origin=[string](Get-TrainerProperty $Metadata 'origin' 'fling_main')
    $author=if($origin -eq 'ct_other'){[string](Get-TrainerProperty $Metadata 'author' 'GCM')}else{'风灵'}
    $author=($author -replace '[<>"/\\|?*\x00-\x1f]','_').Trim()
    $folderName = '['+$author+']《' + $displayName + '》修改器'
    $destination = Assert-TrainerChild (Join-Path $LibraryRoot $folderName) $LibraryRoot
    if (Test-Path -LiteralPath $destination) {
        # Never overwrite a trainer installed or edited by GCM/the player.
        $destination = Assert-TrainerChild (Join-Path $LibraryRoot ($folderName + ' (YMCC-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + ')')) $LibraryRoot
    }
    $parent = [IO.Path]::GetDirectoryName($LibraryRoot)
    $stage = Assert-TrainerChild (Join-Path $parent ('.ymcc-trainer-' + [guid]::NewGuid().ToString('N'))) $parent
    try {
        [IO.Directory]::CreateDirectory($stage) | Out-Null
        foreach ($item in Get-ChildItem -LiteralPath $SourceFolder -Recurse -Force) {
            Assert-TrainerActive $Context
            Assert-TrainerNotReparse $item.FullName
            $relative = $item.FullName.Substring($SourceFolder.Length).TrimStart('\', '/')
            $target = Assert-TrainerChild (Join-Path $stage $relative) $stage
            if ($item.PSIsContainer) { [IO.Directory]::CreateDirectory($target) | Out-Null }
            else {
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
                [IO.File]::Copy($item.FullName, $target, $false)
            }
        }
        $relativeLaunch = $LaunchPath.Substring($SourceFolder.Length).TrimStart('\', '/')
        $version = [string](Get-TrainerProperty $Metadata 'version' '')
        if ($version -match '^(\d{4})[-.](\d{1,2})[-.](\d{1,2})') { $version = '{0}.{1:00}.{2:00}' -f $Matches[1], [int]$Matches[2], [int]$Matches[3] }
        $info = @{ game_name = $GameName; origin = $origin; author = $author; version = $version; extension = $relativeLaunch.Replace('\', '/'); official_url = [string](Get-TrainerProperty $Metadata 'official_url' ''); gcm_url = [string](Get-TrainerProperty $Metadata 'gcm_url' ''); sha256 = $sha; installed_by = 'YMCC' }
        Write-TrainerJson (Join-Path $stage 'gcm_info.json') $info
        if ((Get-FileHash -LiteralPath (Join-Path $stage $relativeLaunch) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha) { throw '修改器复制校验失败' }
        Assert-TrainerActive $Context
        Assert-TrainerNotReparse $LibraryRoot
        [IO.Directory]::Move($stage, $destination)
        return @{ gameName = $GameName; folder = $destination; path = (Join-Path $destination $relativeLaunch); origin = $origin; sha256 = $sha }
    } finally {
        if ([IO.Directory]::Exists($stage)) {
            $verified = Assert-TrainerChild $stage $parent
            Assert-TrainerNotReparse $verified
            Remove-Item -LiteralPath $verified -Recurse -Force
        }
    }
}

function ConvertTo-TrainerMap($Value) {
    $map = @{}
    if ($Value -is [Collections.IDictionary]) { foreach ($key in $Value.Keys) { $map[$key] = $Value[$key] } }
    elseif ($Value) { foreach ($property in $Value.PSObject.Properties) { $map[$property.Name] = $property.Value } }
    return $map
}
function Read-GcmLibraryRecord([string]$Path) {
    $saved = Read-TrainerJson $Path
    if (-not $saved) {
        if([IO.File]::Exists($Path)){throw '修改器下载台账为空或损坏，已停止下载且保留原记录'}
        return @{ schema = 1; budget = @{ windowHours = 24; maxDownloads = 3; maxBytes = 209715200L }; downloads = @(); games = @{}; trainers = @{} }
    }
    if ((Get-TrainerProperty $saved 'schema' 0) -ne 1) { throw 'GCM 目录中的 YMCC 下载记录格式无效，已停止下载' }
    $record = ConvertTo-TrainerMap $saved
    $record.budget = ConvertTo-TrainerMap (Get-TrainerProperty $saved 'budget')
    $record.games = ConvertTo-TrainerMap (Get-TrainerProperty $saved 'games')
    $record.trainers = ConvertTo-TrainerMap (Get-TrainerProperty $saved 'trainers')
    $record.downloads = @(Get-TrainerProperty $saved 'downloads' @() | ForEach-Object { ConvertTo-TrainerMap $_ })
    if (-not $record.budget.ContainsKey('maxDownloads') -or -not $record.budget.ContainsKey('maxBytes') -or
        [double]$record.budget.maxDownloads -lt 0 -or [double]$record.budget.maxBytes -lt 0) { throw '修改器下载预算无效，已停止下载' }
    foreach($download in $record.downloads){
        if(-not $download.ContainsKey('at') -or -not $download.ContainsKey('chargedBytes') -or [long]$download.at -le 0 -or [long]$download.chargedBytes -lt 0 -or [long](Get-TrainerProperty $download 'bytes' 0) -lt 0){throw '修改器下载计费记录无效，已停止下载且保留原记录'}
    }
    # The persisted ledger can lower, but cannot expand, the fixed allowance.
    $record.budget.maxDownloads=[int][Math]::Min([double]3,[Math]::Floor([double]$record.budget.maxDownloads))
    $record.budget.maxBytes=[long][Math]::Min([double]209715200,[Math]::Floor([double]$record.budget.maxBytes))
    return $record
}
function Reserve-TrainerDownload($Record, [string]$RecordPath, $Context, [string]$ResourceName) {
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $window = 86400000L
    $recent = @($Record.downloads | Where-Object { [long]$_.at -gt $now - $window })
    $usedBytes = 0L
    foreach ($download in $recent) { $usedBytes += [long]$download.chargedBytes }
    $remaining = [long]$Record.budget.maxBytes - $usedBytes
    if ($recent.Count -ge [int]$Record.budget.maxDownloads -or $remaining -le 0) {
        $retryAt = if ($recent.Count) { [long]($recent | Sort-Object { $_.at } | Select-Object -First 1).at + $window } else { $now + $window }
        return @{ allowed = $false; retryAfter = $retryAt; message = '已达到24小时下载额度，自动下载已暂停；已有修改器仍可启动' }
    }
    $reserved = [Math]::Min(83886080L, $remaining)
    $entry = @{ requestId = $Context.RequestId; at = $now; gameName = $ResourceName; executablePath = $Context.GameExecutablePath; state = 'reserved'; chargedBytes = $reserved; bytes = 0L }
    # Charge before opening the network connection. Crashes remain charged for 24h.
    $Record.downloads = @($Record.downloads | Where-Object { [long]$_.at -gt $now - 31536000000L } | Select-Object -Last 4095) + @($entry)
    Write-TrainerJson $RecordPath $Record
    $Context.DownloadReservation = $entry
    $Context.MaxDownloadBytes = $reserved
    return @{ allowed = $true }
}
function Register-GcmTrainer($Record, $Local, [string]$DatabasePath, [string[]]$Aliases, [string]$ExecutablePath) {
    $metadata = Read-TrainerJson (Join-Path $Local.folder 'gcm_info.json') 65536
    $resource = @{ gameName = $Local.gameName; origin = $Local.origin; trainerPath = $Local.path; trainerFolder = $Local.folder; gcmInfoPath = (Join-Path $Local.folder 'gcm_info.json'); gcmResourceId = [string](Get-TrainerProperty $metadata 'gcm_url' ''); steamAppId = ''; sha256 = $Local.sha256; lastSeenAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
    try {
        $catalogName = switch ($Local.origin) { 'fling_main' { 'fling_main.json' }; 'fling_archive' { 'fling_archive.json' }; 'ct_other' { 'gcm_trainers.json' }; 'gcm' { 'gcm_trainers.json' }; 'other' { 'gcm_trainers.json' }; default { '' } }
        if ($catalogName -and -not $resource.gcmResourceId) {
            $rows = @(Read-TrainerJson (Join-Path $DatabasePath $catalogName) | Where-Object { (Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'game_name' ''))) -eq (Get-TrainerNameKey $Local.gameName) })
            if ($rows.Count -eq 1) { $resource.gcmResourceId = [string](Get-TrainerProperty $rows[0] 'gcm_url' '') }
        }
        $translation = @(Read-TrainerJson (Join-Path $DatabasePath 'translations.json') | Where-Object { (Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))) -eq (Get-TrainerNameKey $Local.gameName) })
        if ($translation.Count -eq 1) { $resource.steamAppId = [string](Get-TrainerProperty $translation[0] 'steam_id' '') }
    } catch { # Optional source IDs must not be guessed when the local catalog is unavailable.
    }
    $resourceKey = Get-TrainerHash ($Local.origin + ':' + $Local.gameName + ':' + $resource.gcmResourceId)
    $Record.trainers[$resourceKey] = $resource
    if ($ExecutablePath) {
        $exe = [IO.Path]::GetFullPath($ExecutablePath)
        $exeKey = Get-TrainerHash $exe.ToLowerInvariant()
        $exeHash = if ([IO.File]::Exists($exe)) { (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }
        $Record.games[$exeKey] = @{ executableSha256 = $exeHash; executablePath = $exe; gameName = $Local.gameName; aliases = $Aliases; resourceKey = $resourceKey; lastSeenAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
    }
}

function Invoke-TrainerPrepare {
    param([string]$GameName, [string]$CacheRoot, [string]$ResultPath, [string]$RequestId, [switch]$Force, [string]$GameExecutablePath = '', [int]$GameProcessId = 0, [string]$GameProcessCreated = '',
        [string]$GcmSettingsRoot = (Join-Path $env:APPDATA 'GCM Settings'), [string]$GcmDefaultRoot = (Join-Path $env:APPDATA 'GCM Trainers'), [string]$CustomLibraryRoot = 'D:\YeMan\CustomSteamLibrary\data')
    if (-not $GameName.Trim() -or $GameName.Length -gt 240 -or $RequestId -notmatch '^[a-zA-Z0-9-]{10,80}$') { throw '修改器准备参数无效' }
    $CacheRoot = [IO.Path]::GetFullPath($CacheRoot)
    Assert-TrainerNotReparse $CacheRoot
    $ResultPath = Assert-TrainerChild $ResultPath (Join-Path $CacheRoot 'requests')
    if ([IO.Path]::GetFileName($ResultPath) -ne ($RequestId + '.json')) { throw '修改器任务身份不匹配' }
    if ($GameExecutablePath -and (-not [IO.Path]::IsPathRooted($GameExecutablePath) -or [IO.Path]::GetExtension($GameExecutablePath) -ine '.exe')) { throw '游戏启动程序路径无效' }
    $context = @{ GameExecutablePath = $GameExecutablePath; GameProcessId = $GameProcessId; GameProcessCreated = $GameProcessCreated; DownloadBytes = 0L; GameName = $GameName; RequestId = $RequestId; ResultPath = $ResultPath; CancelPath = $ResultPath + '.cancel'; Deadline = [DateTime]::UtcNow.AddSeconds(180) }
    $mutex = [Threading.Mutex]::new($false, ('Local\YMCC.Trainer.' + (Get-TrainerHash $CacheRoot.ToLowerInvariant())))
    $owned = $false
    $stage = ''
    $libraryMutex = $null
    $libraryOwned = $false
    $record = $null
    $recordPath = ''
    $package = ''
    $downloadedNew = $false
    $indexPath = Join-Path $CacheRoot 'trainers.json'
    $index = @{}
    $key = Get-TrainerNameKey $GameName
    if ($GameExecutablePath) { $key += '|' + (Get-TrainerHash ([IO.Path]::GetFullPath($GameExecutablePath).ToLowerInvariant())) }
    try {
        Send-TrainerProgress $context 'queued' '等待后台准备修改器' | Out-Null
        while (-not $owned) {
            Assert-TrainerActive $context
            try { $owned = $mutex.WaitOne(250) } catch [Threading.AbandonedMutexException] { $owned = $true }
        }
        Assert-TrainerActive $context
        try {
            $saved = Read-TrainerJson $indexPath
            if ($saved) { foreach ($property in $saved.PSObject.Properties) { $index[$property.Name] = $property.Value } }
        } catch { # Rebuild an invalid index from local installations.
        }
        $cached = $index[$key]
        $database = Join-Path $GcmSettingsRoot 'db'
        $aliases = @() # Resolve trusted EXE evidence before considering display-name aliases.
        $libraryRoot = Get-GcmTrainerRoot $GcmSettingsRoot $GcmDefaultRoot
        $libraryMutex = [Threading.Mutex]::new($false, ('Local\YMCC.GcmLibrary.' + (Get-TrainerHash $libraryRoot.ToLowerInvariant())))
        while (-not $libraryOwned) {
            Assert-TrainerActive $context
            try { $libraryOwned = $libraryMutex.WaitOne(250) } catch [Threading.AbandonedMutexException] { $libraryOwned = $true }
        }
        $recordPath = Join-Path $libraryRoot 'ymcc-library.json'
        $record = Read-GcmLibraryRecord $recordPath
        $identity = Resolve-TrainerIdentity $GameName $GameExecutablePath $database $record $CustomLibraryRoot $GameProcessId $GameProcessCreated
        # On a fresh machine, import GCM metadata BEFORE locking the identity used for installation.
        # A title-only match must never download on behalf of an unconfirmed live game.
        if($GameProcessId -gt 0 -and $identity.method -in @('exact-title','exe-binding')) {
            Update-TrainerPublicCatalog $context $libraryRoot $database | Out-Null
            Assert-TrainerActive $context
            $identity=Resolve-TrainerIdentity $GameName $GameExecutablePath $database $record $CustomLibraryRoot $GameProcessId $GameProcessCreated
            if($identity.method -in @('exact-title','exe-binding')) {
                $result=Send-TrainerProgress $context 'unavailable' '已识别顶部游戏，但 GCM 暂无可确认的游戏身份，未自动下载' @{code='identity-unconfirmed';retryable=$false;identity=$identity;libraryRoot=$libraryRoot;retryAfter=[DateTimeOffset]::UtcNow.AddHours(24).ToUnixTimeMilliseconds()}
                $index[$key]=$result;Write-TrainerJson $indexPath $index
                return
            }
        }
        $gameSha256 = ''
        if($GameExecutablePath){
            $gameSha256=[string](Get-TrainerProperty $identity 'executableSha256' '')
            if(-not $gameSha256){$gameSha256=(Get-FileHash -LiteralPath $GameExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant()}
            if($GameProcessId -gt 0 -or $GameProcessCreated){Assert-TrainerSharedGameTarget $GameExecutablePath $GameProcessId $GameProcessCreated|Out-Null}
        }
        $aliases = @($identity.aliases)
        $identityKey = Get-TrainerNameKey $aliases[0]
        if (-not $GameExecutablePath -and $identityKey -ne $key) { $cached = $index[$identityKey] }
        Send-TrainerProgress $context 'searching' '正在查找管理器中的修改器' | Out-Null
        $local = Find-LocalTrainer @($libraryRoot) $aliases $database
        if (-not $local) {
            $legacy = Find-LocalTrainer @((Join-Path $CacheRoot 'installed')) $aliases $database
            if ($legacy -and $legacy.origin -eq 'ymcc_fling_official') {
                $metadata = Read-TrainerJson (Join-Path $legacy.folder 'gcm_info.json') 65536
                $local = Install-GcmTrainer $legacy.folder $legacy.path $libraryRoot $legacy.gameName $aliases $metadata $context
                Write-TrainerJson (Join-Path $legacy.folder '.ymcc-migrated.json') @{ destination = $local.folder }
            }
        }
        if (-not $local -and -not $Force -and $cached -and (Get-TrainerProperty $cached 'state' '') -ne 'unavailable' -and [double](Get-TrainerProperty $cached 'retryAfter' 0) -gt [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) {
            $extra = @{ retryAfter = $cached.retryAfter }
            Send-TrainerProgress $context ([string]$cached.state) ([string]$cached.message) $extra | Out-Null
            return
        }
        if (-not $local) {
            $candidate = Find-FlingCandidate @(Get-FlingCatalogRows $database $libraryRoot) $aliases
            if (-not $candidate) {
                Update-TrainerPublicCatalog $context $libraryRoot $database | Out-Null
                $candidate = Find-FlingCandidate @(Get-FlingCatalogRows $database $libraryRoot) $aliases
            }
            $ctFallback=$false
            if(-not $candidate){
                $candidate=Find-GcmPublicCtCandidate $database $aliases $GcmSettingsRoot
                $ctFallback=[bool]$candidate
            }
            if (-not $candidate) {
                $result = Send-TrainerProgress $context 'unavailable' '暂未找到名称明确匹配的后台下载资源' @{ retryAfter = [DateTimeOffset]::UtcNow.AddHours(24).ToUnixTimeMilliseconds() }
                $index[$key] = $result; Write-TrainerJson $indexPath $index
                return
            }
            $reservation = Reserve-TrainerDownload $record $recordPath $context $candidate.game_name
            if (-not $reservation.allowed) {
                $limited = Send-TrainerProgress $context 'budget-limited' $reservation.message @{ retryAfter = $reservation.retryAfter }
                $index[$key] = $limited; Write-TrainerJson $indexPath $index
                return
            }
            if($ctFallback){$context.SourcePolicy='gcm-public-ct';$context.SourcePage=[string]$candidate.official_url;$context.MaxDownloadBytes=[Math]::Min([long]$context.MaxDownloadBytes,8388608L)}
            $page = Get-TrainerResponse $context $candidate.official_url
            if($ctFallback){
                $download=Get-GcmPublicCtDownload $page $candidate
                $context.CtAttachmentUrl=$download.url
            }else{$download = Get-FlingDownload $page $candidate.game_name}
            Assert-TrainerActive $context
            $stageRoot = Join-Path $CacheRoot 'staging'
            $stage = Assert-TrainerChild (Join-Path $stageRoot $RequestId) $stageRoot
            [IO.Directory]::CreateDirectory($stage) | Out-Null
            $package = Join-Path $stage 'download.part'
            Send-TrainerProgress $context 'downloading' ('正在后台下载 ' + $candidate.game_name + ' 修改器') | Out-Null
            $receipt = Get-TrainerResponse $context $download.url $package $candidate.official_url
            $context.CompletedDownloadBytes = [long]$receipt.bytes
            if ($context.CompletedDownloadBytes -gt $context.MaxDownloadBytes) { throw '修改器下载超过剩余额度' }
            $payload = Join-Path $stage 'payload'
            [IO.Directory]::CreateDirectory($payload) | Out-Null
            if($ctFallback){
                if(-not (Test-TrainerCt $package)){throw '下载内容不是有效 CT 表格，未安装'}
                [IO.File]::Move($package,(Join-Path $payload 'Trainer.ct'))
            }elseif (Test-TrainerPe $package) { [IO.File]::Move($package, (Join-Path $payload 'Trainer.exe')) }
            else { Expand-TrainerZip $package $payload $context }
            $launch = Get-TrainerLaunchFile $payload
            if($ctFallback){if(-not $launch -or -not (Test-TrainerCt $launch)){throw 'CT 表格安装内容核验失败'}}
            elseif (-not $launch -or [IO.Path]::GetExtension($launch) -ne '.exe' -or -not (Test-TrainerPe $launch)) { throw '下载内容未包含唯一且有效的独立修改器程序' }
            Assert-TrainerActive $context
            $sha = (Get-FileHash -LiteralPath $launch -Algorithm SHA256).Hash.ToLowerInvariant()
            $metadata = @{ gcm_url = [string](Get-TrainerProperty $candidate 'gcm_url' ''); game_name = $candidate.game_name; origin = $(if($ctFallback){'ct_other'}else{'fling_main'}); author = [string](Get-TrainerProperty $candidate 'author' ''); version = $download.version; official_url = $candidate.official_url; sha256 = $sha }
            $local = Install-GcmTrainer $payload $launch $libraryRoot $candidate.game_name $aliases $metadata $context
            $downloadedNew = $true
            $aliases = @($aliases + @([string]$candidate.game_name) | Select-Object -Unique)
        }
        Assert-TrainerActive $context
        if(-not (Test-TrainerLocalLaunchFile $local.path)){throw '修改器文件内容已损坏或改变，未标为就绪'}
        $hash = if ($local.ContainsKey('sha256')) { $local.sha256 } else { (Get-FileHash -LiteralPath $local.path -Algorithm SHA256).Hash.ToLowerInvariant() }
        if($GameExecutablePath){
            # A download can outlive a game switch, file update or manifest change.
            # Re-resolve every evidence path, not only native title associations.
            $confirmed=Resolve-TrainerIdentity $GameName $GameExecutablePath $database $record $CustomLibraryRoot $GameProcessId $GameProcessCreated
            $beforeAliases=@($identity.aliases|ForEach-Object{Get-TrainerNameKey $_}|Sort-Object -Unique)
            $afterAliases=@($confirmed.aliases|ForEach-Object{Get-TrainerNameKey $_}|Sort-Object -Unique)
            if($confirmed.method -ne $identity.method -or [string](Get-TrainerProperty $confirmed 'steamAppId' '') -ne [string](Get-TrainerProperty $identity 'steamAppId' '') -or ($beforeAliases -join '|') -cne ($afterAliases -join '|')){throw '准备完成时游戏身份已改变，文件已保留但未标为就绪'}
            if((Get-FileHash -LiteralPath $GameExecutablePath -Algorithm SHA256).Hash -ine $gameSha256){throw '准备期间游戏 EXE 已改变，文件已保留，请重新识别游戏'}
            if($GameProcessId -gt 0 -or $GameProcessCreated){Assert-TrainerSharedGameTarget $GameExecutablePath $GameProcessId $GameProcessCreated|Out-Null}
            $identity.executableSha256=$gameSha256
            if($GameProcessId -gt 0 -or $GameProcessCreated){
                # Manifest/custom-library identities select a resource, but readiness
                # must also belong to this exact running instance, just like the
                # shared-target identity. A same-EXE restart cannot inherit it.
                $identity.processId=$GameProcessId
                $identity.processCreated=$GameProcessCreated
                $identity.executablePath=[IO.Path]::GetFullPath($GameExecutablePath)
            }
        }
        $ready = @{ identity = $identity; libraryRoot = $libraryRoot; storageVersion = 2; state = 'ready'; gameName = $GameName; trainerGameName = $local.gameName; gcmResourceId = [string](Get-TrainerProperty (Read-TrainerJson (Join-Path $local.folder 'gcm_info.json') 65536) 'gcm_url' ''); trainerPath = $local.path; trainerFolder = $local.folder; origin = $local.origin; sha256 = $hash; downloaded = $downloadedNew; downloadOutcome = $(if($downloadedNew){'downloaded'}else{'reused-local'}); bytesDownloaded = [long]$context.DownloadBytes; checkedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
        # GCM remains the source of truth: rebuild inventory from its metadata.
        $record.trainers = @{}
        foreach ($folder in (Get-ChildItem -LiteralPath $libraryRoot -Directory -ErrorAction SilentlyContinue | Select-Object -First 2000)) {
            try {
                # Register the selected entry once below with its resolved GCM resource name.
                if($folder.FullName -ieq $local.folder){continue}
                Assert-TrainerNotReparse $folder.FullName
                $info = Read-TrainerJson (Join-Path $folder.FullName 'gcm_info.json') 65536
                $name = [string](Get-TrainerProperty $info 'game_name' '')
                if (-not $name) { continue }
                $path = Get-TrainerLaunchFile $folder.FullName
                if (-not $path -or -not (Test-TrainerLocalLaunchFile $path)) { continue }
                $item = @{gameName=$name;folder=$folder.FullName;path=$path;origin=[string](Get-TrainerProperty $info 'origin' 'local');sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
                Register-GcmTrainer $record $item $database @() ''
            } catch { # An unrelated incomplete manager entry must not block this game.
            }
        }
        Assert-TrainerActive $context
        if($GameProcessId -gt 0 -or $GameProcessCreated){Assert-TrainerSharedGameTarget $GameExecutablePath $GameProcessId $GameProcessCreated|Out-Null}
        $local.sha256 = $hash
        Register-GcmTrainer $record $local $database $aliases $GameExecutablePath
        if ($GameExecutablePath) {
            $bindingKey = Get-TrainerHash ([IO.Path]::GetFullPath($GameExecutablePath).ToLowerInvariant())
            $record.games[$bindingKey].identity = $identity
        }
        Write-TrainerJson $recordPath $record
        foreach ($alias in $aliases) { $index[(Get-TrainerNameKey $alias)] = $ready }
        $index[$key] = $ready
        Write-TrainerJson $indexPath $index
        $readyMessage=if($downloadedNew){'修改器下载完成，点击即可启动'}else{'已复用 GCM 中的修改器，点击即可启动'}
        Send-TrainerProgress $context 'ready' $readyMessage $ready | Out-Null
    } catch {
        $errorObject = $_.Exception
        $cancelled = $errorObject -is [OperationCanceledException] -or [IO.File]::Exists($context.CancelPath)
        $state = if ($cancelled) { 'cancelled' } else { 'failed' }
        $message = if ($cancelled) { '已取消修改器准备' } else { $errorObject.Message }
        $retryable = -not $cancelled -and $errorObject.Data.Contains('trainerRetryable') -and $errorObject.Data['trainerRetryable'] -eq $true
        $retryAfter=if($retryable){[DateTimeOffset]::UtcNow.AddSeconds(5).ToUnixTimeMilliseconds()}else{[DateTimeOffset]::UtcNow.AddHours(1).ToUnixTimeMilliseconds()}
        $failed = Send-TrainerProgress $context $state $message @{ error = $message; code = [string]$errorObject.Data['trainerCode']; retryable = $retryable; retryAfter = $retryAfter }
        if ($owned) { $index[$key] = $failed; Write-TrainerJson $indexPath $index }
    } finally {
        try {
            if ($record -and $context.ContainsKey('DownloadReservation')) {
                $reservation = $context.DownloadReservation
                $actual = [long]$context.DownloadBytes
                if ($package -and [IO.File]::Exists($package)) { $actual = [Math]::Max($actual, (Get-Item -LiteralPath $package).Length) }
                if ($context.ContainsKey('CompletedDownloadBytes')) { $actual = [Math]::Max($actual, [long]$context.CompletedDownloadBytes) }
                $reservation.bytes = $actual
                $reservation.chargedBytes = $actual
                $reservation.state = if ($index.ContainsKey($key) -and (Get-TrainerProperty $index[$key] 'state') -eq 'ready') { 'ready' } else { 'failed' }
                Write-TrainerJson $recordPath $record
            }
        } finally {
            if ($libraryOwned) { $libraryMutex.ReleaseMutex() }
            if ($libraryMutex) { $libraryMutex.Dispose() }
        }
        if ($stage -and [IO.Directory]::Exists($stage)) {
            $verifiedStage = Assert-TrainerChild $stage (Join-Path $CacheRoot 'staging')
            Assert-TrainerNotReparse $verifiedStage
            Remove-Item -LiteralPath $verifiedStage -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
        if ([IO.File]::Exists($context.CancelPath)) { [IO.File]::Delete($context.CancelPath) }
    }
}
. (Join-Path $PSScriptRoot 'TrainerGcmFallback.ps1')
. (Join-Path $PSScriptRoot 'TrainerCatalog.ps1')
. (Join-Path $PSScriptRoot 'TrainerIdentity.ps1')
. (Join-Path $PSScriptRoot 'TrainerLaunchPaths.ps1')
. (Join-Path $PSScriptRoot 'TrainerLaunch.ps1')
Export-ModuleMember -Function Resolve-TrainerLaunchPlan, Invoke-TrainerLaunch, Invoke-TrainerDataRefresh, Resolve-TrainerIdentity, Get-TrainerSteamIdentity, Get-TrainerNameKey, Resolve-TrainerAliases, ConvertFrom-FlingIndex, Find-FlingCandidate, Get-FlingDownload, Assert-TrainerUri, Expand-TrainerZip, Find-LocalTrainer, Test-TrainerPe, Invoke-TrainerPrepare
