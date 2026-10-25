# Dot-sourced by TrainerBackend.psm1. GCM-owned JSON files are never overwritten.
$script:TrainerCatalogMemory = @{}
function Test-GcmCatalogRows($Rows, [string]$Kind) {
    if ($Rows -isnot [array] -or $Rows.Count -eq 0 -or $Rows.Count -gt 100000) { return $false }
    foreach ($row in $Rows) {
        if ($Kind -eq 'translations.json') {
            if (-not ([string](Get-TrainerProperty $row 'en_US' '')).Trim()) { return $false }
        } elseif (-not ([string](Get-TrainerProperty $row 'game_name' '')).Trim()) { return $false }
    }
    return $true
}
function Get-GcmCatalogRows([string]$DatabasePath, [string]$Name) {
    if ($Name -notin @('translations.json','fling_main.json','fling_archive.json','gcm_trainers.json','cheat_table.json','xiaoxing.json')) { throw '不支持的 GCM 数据文件' }
    $path = Join-Path $DatabasePath $Name
    $snapshot = Join-Path $DatabasePath ('ymcc-validated\' + $Name)
    try {
        Assert-TrainerNotReparse $DatabasePath
        if (-not [IO.File]::Exists($path) -or (Get-Item -LiteralPath $path).Length -gt 8388608) { throw 'GCM 数据文件不可用' }
        $raw = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
        if (-not $raw.TrimStart().StartsWith('[')) { throw 'GCM 数据根格式发生变化' }
        # Re-read bytes on every lookup; reuse parsing only when the actual content is identical.
        $contentHash = Get-TrainerHash $raw
        $cacheKey = [IO.Path]::GetFullPath($path).ToLowerInvariant()
        if ($script:TrainerCatalogMemory.ContainsKey($cacheKey) -and $script:TrainerCatalogMemory[$cacheKey].hash -eq $contentHash) { return $script:TrainerCatalogMemory[$cacheKey].rows }
        # Enumerate once to remove PS 5.1's decorated JSON-array wrapper before snapshotting.
        $rows = @($raw | ConvertFrom-Json | ForEach-Object { $_ })
        if (-not (Test-GcmCatalogRows $rows $Name)) { throw 'GCM 数据格式发生变化或数据不完整' }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $previous = Read-TrainerJson $snapshot
        if (-not $previous -or (Get-TrainerProperty $previous 'sourceSha256' '') -ne $hash -or -not (Test-GcmCatalogRows @(Get-TrainerProperty $previous 'rows') $Name)) {
            Write-TrainerJson $snapshot @{schema=1;sourceSha256=$hash;importedAt=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();rows=$rows}
        }
        if ($script:TrainerCatalogMemory.Count -ge 12) { $script:TrainerCatalogMemory.Clear() }
        $script:TrainerCatalogMemory[$cacheKey] = @{hash=$contentHash;rows=$rows}
        return $rows
    } catch {
        try {
            $previous = Read-TrainerJson $snapshot
            $rows = @(Get-TrainerProperty $previous 'rows')
            if ((Get-TrainerProperty $previous 'schema' 0) -eq 1 -and (Test-GcmCatalogRows $rows $Name)) { return $rows }
        } catch { }
        return @()
    }
}
function Get-FlingCatalogRows([string]$DatabasePath, [string]$LibraryRoot) {
    $gcm = @(Get-GcmCatalogRows $DatabasePath 'fling_main.json')
    $public = $null
    try { $public = Read-TrainerJson (Join-Path $LibraryRoot 'ymcc-fling-public.json') } catch { }
    $rows = @(Get-TrainerProperty $public 'rows' @())
    if ((Get-TrainerProperty $public 'schema' 0) -ne 1 -or -not (Test-GcmCatalogRows $rows 'fling_main.json')) { return $gcm }
    $gcmByKey = @{}
    foreach ($item in $gcm) { $key = Get-TrainerNameKey $item.game_name; if (-not $gcmByKey.ContainsKey($key)) { $gcmByKey[$key] = @() }; $gcmByKey[$key] += $item }
    $publicKeys = @{}
    $result = @()
    foreach ($row in $rows) {
        try { Assert-TrainerUri ([string](Get-TrainerProperty $row 'official_url' '')) | Out-Null } catch { continue }
        $key = Get-TrainerNameKey $row.game_name
        $publicKeys[$key] = $true
        $legacy = @(if ($gcmByKey.ContainsKey($key)) { $gcmByKey[$key] })
        $item = @{game_name=$row.game_name;origin='fling_main';official_url=$row.official_url;catalog_source='fling-public'}
        if ($legacy.Count -eq 1) {
            $item.gcm_url = [string](Get-TrainerProperty $legacy[0] 'gcm_url' '')
            $item.version = [string](Get-TrainerProperty $legacy[0] 'version' '')
        }
        $result += [pscustomobject]$item
    }
    foreach ($row in $gcm) { if (-not $publicKeys.ContainsKey((Get-TrainerNameKey $row.game_name))) { $result += $row } }
    return $result
}
function Update-TrainerPublicCatalog($Context, [string]$LibraryRoot, [string]$DatabasePath) {
    # Inherit GCM changes first; this step only reads its files and creates our snapshots.
    $imports = @{}
    foreach ($name in @('translations.json','fling_main.json','fling_archive.json','gcm_trainers.json')) { $imports[$name] = @(Get-GcmCatalogRows $DatabasePath $name).Count }
    $statePath = Join-Path $LibraryRoot 'ymcc-data-state.json'
    $catalogPath = Join-Path $LibraryRoot 'ymcc-fling-public.json'
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $state = @{schema=1;attempts=@();nextCheckAt=0L;lastSuccessAt=0L;etag=''}
    if ([IO.File]::Exists($statePath)) {
        $saved = Read-TrainerJson $statePath
        if ((Get-TrainerProperty $saved 'schema' 0) -ne 1) { throw '后台数据更新记录无效，未重置下载额度' }
        if ($null -eq $saved.PSObject.Properties['attempts'] -or $saved.attempts -isnot [array] -or
            $null -eq $saved.PSObject.Properties['nextCheckAt'] -or $null -eq $saved.PSObject.Properties['lastSuccessAt'] -or $null -eq $saved.PSObject.Properties['etag']) { throw '后台数据记录不完整，未重置下载额度' }
        foreach ($entry in $saved.attempts) {
            if ($null -eq $entry.PSObject.Properties['at'] -or $null -eq $entry.PSObject.Properties['chargedBytes'] -or
                [long]$entry.at -le 0 -or [long]$entry.chargedBytes -lt 0) { throw '后台数据预算记录无效，未重置下载额度' }
        }
        $state = ConvertTo-TrainerMap $saved
        $state.attempts = @(Get-TrainerProperty $saved 'attempts' @() | ForEach-Object { ConvertTo-TrainerMap $_ })
    }
    $oldCatalog = $null
    try { $oldCatalog = Read-TrainerJson $catalogPath } catch { }
    $hasCatalog = $oldCatalog -and (Get-TrainerProperty $oldCatalog 'schema' 0) -eq 1 -and (Test-GcmCatalogRows @(Get-TrainerProperty $oldCatalog 'rows') 'fling_main.json')
    if ([long]$state.nextCheckAt -gt $now) {
        return @{updated=$false;available=[bool]$hasCatalog;nextCheckAt=$state.nextCheckAt;imports=$imports;reason='cooldown'}
    }
    $recent = @($state.attempts | Where-Object { [long]$_.at -gt $now - 86400000L })
    $bytes = 0L
    foreach ($attempt in $recent) { $bytes += [long]$attempt.chargedBytes }
    if ($recent.Count -ge 3 -or $bytes -ge 8388608L) {
        return @{updated=$false;available=[bool]$hasCatalog;nextCheckAt=$now+21600000L;imports=$imports;reason='metadata-budget'}
    }
    $charge = [Math]::Min(2097152L, 8388608L - $bytes)
    $attempt = @{at=$now;chargedBytes=$charge;state='reserved'}
    $state.attempts = @($recent) + @($attempt)
    $state.nextCheckAt = $now + 21600000L
    Write-TrainerJson $statePath $state
    $Context.CatalogResponse = $true
    $Context.MaxTextBytes = $charge
    $Context.CatalogBytes = 0L
    $Context.ConditionalETag = if ($hasCatalog) { [string]$state.etag } else { '' }
    try {
        $response = Get-TrainerResponse $Context 'https://flingtrainer.com/all-trainers-a-z/'
        if ($response -is [string]) { $response = @{text=$response;bytes=[Text.Encoding]::UTF8.GetByteCount($response);etag='';notModified=$false} }
        $attempt.chargedBytes = [long](Get-TrainerProperty $response 'bytes' 0)
        if ($attempt.chargedBytes -gt $charge) { throw '风灵目录超过后台数据大小限额' }
        if (Get-TrainerProperty $response 'notModified' $false) {
            if (-not $hasCatalog) { throw '没有可复用的本地风灵目录' }
            $rows = @(Get-TrainerProperty $oldCatalog 'rows')
        } else {
            $rows = @(ConvertFrom-FlingIndex ([string]$response.text))
            if ($rows.Count -lt 20 -or $rows.Count -gt 20000) { throw '风灵目录内容异常，保留上一次可用数据' }
            Write-TrainerJson $catalogPath @{schema=1;source='https://flingtrainer.com/all-trainers-a-z/';checkedAt=$now;rows=$rows}
        }
        $state.etag = [string](Get-TrainerProperty $response 'etag' $state.etag)
        $state.lastSuccessAt = $now
        $state.nextCheckAt = $now + 86400000L
        $state.lastError = ''
        $attempt.state = 'ready'
        return @{updated= -not [bool](Get-TrainerProperty $response 'notModified' $false);available=$true;count=$rows.Count;nextCheckAt=$state.nextCheckAt;imports=$imports;reason='ready'}
    } catch {
        $attempt.chargedBytes = [Math]::Max([long]$Context.CatalogBytes, [long]$attempt.chargedBytes)
        $attempt.state = 'failed'
        $state.lastError = $_.Exception.Message
        return @{updated=$false;available=[bool]$hasCatalog;nextCheckAt=$state.nextCheckAt;imports=$imports;reason='failed';error=$_.Exception.Message}
    } finally {
        $Context.Remove('CatalogResponse');$Context.Remove('MaxTextBytes');$Context.Remove('ConditionalETag')
        Write-TrainerJson $statePath $state
    }
}
function Invoke-TrainerDataRefresh {
    param([string]$CacheRoot, [string]$ResultPath, [string]$RequestId,
        [string]$GcmSettingsRoot=(Join-Path $env:APPDATA 'GCM Settings'), [string]$GcmDefaultRoot=(Join-Path $env:APPDATA 'GCM Trainers'))
    $CacheRoot = [IO.Path]::GetFullPath($CacheRoot)
    Assert-TrainerNotReparse $CacheRoot
    $ResultPath = Assert-TrainerChild $ResultPath (Join-Path $CacheRoot 'requests')
    if ($RequestId -notmatch '^[a-zA-Z0-9-]{10,80}$' -or [IO.Path]::GetFileName($ResultPath) -ne ($RequestId+'.json')) { throw '数据更新任务身份无效' }
    $context = @{GameName='YMCC Trainer Catalog';RequestId=$RequestId;ResultPath=$ResultPath;CancelPath=$ResultPath+'.cancel';Deadline=[DateTime]::UtcNow.AddSeconds(45)}
    $owned=$false;$mutex=$null
    try {
        $library = Get-GcmTrainerRoot $GcmSettingsRoot $GcmDefaultRoot
        $mutex = [Threading.Mutex]::new($false, ('Local\YMCC.GcmLibrary.'+(Get-TrainerHash $library.ToLowerInvariant())))
        while (-not $owned) { Assert-TrainerActive $context;try{$owned=$mutex.WaitOne(250)}catch [Threading.AbandonedMutexException]{$owned=$true} }
        Send-TrainerProgress $context 'searching' '正在后台更新修改器数据'|Out-Null
        $result=Update-TrainerPublicCatalog $context $library (Join-Path $GcmSettingsRoot 'db')
        $state=if($result.available){'ready'}else{'unavailable'}
        Send-TrainerProgress $context $state '后台修改器数据检查完成' @{catalog=$result;retryAfter=$result.nextCheckAt}|Out-Null
    } catch { Send-TrainerProgress $context 'failed' $_.Exception.Message @{retryAfter=[DateTimeOffset]::UtcNow.AddHours(6).ToUnixTimeMilliseconds()}|Out-Null }
    finally {if($owned){$mutex.ReleaseMutex()};if($mutex){$mutex.Dispose()}}
}
