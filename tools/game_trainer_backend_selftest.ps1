[CmdletBinding()]
param([string]$ValidationRoot = '')
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $ValidationRoot) { $ValidationRoot = Join-Path $sourceRoot ('../../Build/Validation/TrainerBackend-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$ValidationRoot = [IO.Path]::GetFullPath($ValidationRoot)
[IO.Directory]::CreateDirectory($ValidationRoot) | Out-Null
Import-Module (Join-Path $sourceRoot 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$script:passed = 0
$script:failed = 0
function Check([string]$Name, [scriptblock]$Test) {
    try { & $Test; $script:passed++; Write-Host ('PASS ' + $Name) }
    catch { $script:failed++; Write-Host ('FAIL ' + $Name + ': ' + $_.Exception.Message); Write-Host $_.ScriptStackTrace }
}
function Assert($Condition, [string]$Message = 'assertion failed') { if (-not $Condition) { throw $Message } }
function Expect-Error([scriptblock]$Action) { $failed = $false; try { & $Action | Out-Null } catch { $failed = $true }; Assert $failed 'Expected rejection' }
function Write-Json([string]$Path, $Value) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 10), [Text.Encoding]::UTF8)
}
function New-Pe([string]$Path) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    $bytes = New-Object byte[] 128
    $bytes[0] = 0x4d; $bytes[1] = 0x5a; $bytes[60] = 80; $bytes[80] = 0x50; $bytes[81] = 0x45
    [IO.File]::WriteAllBytes($Path, $bytes)
}
$settings = Join-Path $ValidationRoot 'settings'
$local = Join-Path $ValidationRoot 'local'
Write-Json (Join-Path $settings 'db/translations.json') @(@{en_US='Victoria 3';zh_CN='维多利亚3';steam_id=529340})
Write-Json (Join-Path $settings 'db/fling_main.json') @(@{game_name='Victoria 3';official_url='https://flingtrainer.com/trainer/victoria-3-trainer/'})
$fixturePe = Join-Path $ValidationRoot 'fixture.exe'
New-Pe $fixturePe
$fixtureZip = Join-Path $ValidationRoot 'fixture.zip'
$zip = [IO.Compression.ZipFile]::Open($fixtureZip, [IO.Compression.ZipArchiveMode]::Create)
try { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $fixturePe, 'nested/Trainer.exe') | Out-Null } finally { $zip.Dispose() }
$fixtureHtml = '<h1 class="entry-title">Victoria 3 Trainer</h1><a href="https://unrelated.example/setup.exe">Advertisement</a><tr><td><a href="https://flingtrainer.com/downloads/old" class="attachment-link">Victoria.3.Trainer-FLiNG</a></td><td class="attachment-date">2025-01-01 12:00</td></tr><tr><td><a href="https://flingtrainer.com/downloads/new" class="attachment-link">Victoria.3.Trainer-FLiNG</a></td><td class="attachment-date">2026-05-03 16:24</td></tr>'
$module = Get-Module TrainerBackend
& $module {
    param($Html, $Pe, $Zip)
    $script:fixtureHtml = $Html; $script:fixturePe = $Pe; $script:fixtureZip = $Zip
    $script:networkCalls = 0; $script:networkMode = 'exe'
    function script:Get-TrainerResponse($Context, [string]$Url, [string]$Destination = '', [string]$Referer = '') {
        $script:networkCalls++
        if ($script:networkMode -eq 'failure') { throw 'fixture HTTP 403' }
        if ($Url -match '/trainer/') { return $script:fixtureHtml }
        if ($Destination) {
            if ($script:networkMode -eq 'cancel') { [IO.File]::WriteAllText($Context.CancelPath, 'cancel') }
            if ($script:networkMode -eq 'html') { [IO.File]::WriteAllText($Destination, '<html>blocked</html>') }
            elseif ($script:networkMode -eq 'zip') { [IO.File]::Copy($script:fixtureZip, $Destination) }
            else { [IO.File]::Copy($script:fixturePe, $Destination) }
            return @{ url = $Url; bytes = (Get-Item -LiteralPath $Destination).Length }
        }
        return '<a href="https://flingtrainer.com/trainer/victoria-3-trainer/">Victoria 3 Trainer</a>'
    }
} $fixtureHtml $fixturePe $fixtureZip
function Run-Worker([string]$CacheName, [string]$GameName = 'Victoria 3', [switch]$Force, [string]$LibraryRoot = '', [string]$ExecutablePath = '') {
    $cache = Join-Path $ValidationRoot $CacheName
    if (-not $LibraryRoot) { $LibraryRoot = if ($CacheName -eq 'local-only') { $local } else { Join-Path $cache 'library' } }
    $id = 'test-' + [guid]::NewGuid().ToString('N')
    $result = Join-Path $cache ('requests/' + $id + '.json')
    Invoke-TrainerPrepare -GameName $GameName -CacheRoot $cache -ResultPath $result -RequestId $id -GcmSettingsRoot $settings -GcmDefaultRoot $LibraryRoot -GameExecutablePath $ExecutablePath -Force:$Force
    return ([IO.File]::ReadAllText($result) | ConvertFrom-Json)
}
Check 'punctuation equality never collapses sequels' {
    Assert ((Get-TrainerNameKey 'Victoria: 3') -eq (Get-TrainerNameKey 'Victoria 3'))
    Assert ((Get-TrainerNameKey 'Victoria 3') -ne (Get-TrainerNameKey 'Victoria 2'))
    Assert ((Get-TrainerNameKey 'Victoria') -ne (Get-TrainerNameKey 'Victoria 3'))
}
Check 'Chinese aliases come from exact translation entries' {
    $aliases = @(Resolve-TrainerAliases '维多利亚3' (Join-Path $settings 'db'))
    Assert ($aliases -contains 'Victoria 3')
    Assert (@(Resolve-TrainerAliases '维多利亚2' (Join-Path $settings 'db')).Count -eq 1)
}
Check 'URL checks reject external, HTTP and userinfo redirects' {
    Assert-TrainerUri 'https://flingtrainer.com/download-trainer.php?path=test' | Out-Null
    Expect-Error { Assert-TrainerUri 'http://flingtrainer.com/downloads/file' }
    Expect-Error { Assert-TrainerUri 'https://flingtrainer.com.evil.example/downloads/file' }
    Expect-Error { Assert-TrainerUri 'https://user@flingtrainer.com/downloads/file' }
    Expect-Error { Assert-TrainerUri 'https://127.0.0.1/downloads/file' }
}
Check 'latest standalone attachment selected and unrelated game refused' {
    $download = Get-FlingDownload $fixtureHtml 'Victoria 3'
    Assert ($download.url -eq 'https://flingtrainer.com/downloads/new')
    Expect-Error { Get-FlingDownload $fixtureHtml 'Victoria 2' }
}
Check 'ambiguous same-name catalog refuses automatic selection' {
    $rows = @(@{game_name='Victoria 3';official_url='https://flingtrainer.com/trainer/a/'},@{game_name='Victoria 3';official_url='https://flingtrainer.com/trainer/b/'}) | ForEach-Object { [pscustomobject]$_ }
    Expect-Error { Find-FlingCandidate $rows @('Victoria 3') }
}
Check 'local GCM metadata overrides misleading folder name' {
    $folder = Join-Path $local '[风灵]《维多利亚3》修改器'
    New-Pe (Join-Path $folder 'Trainer.exe')
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 2';origin='fling_main'}
    Assert ($null -eq (Find-LocalTrainer @($local) @('Victoria 3','维多利亚3')))
}
Check 'fresh Chinese lookup downloads and installs valid executable without running it' {
    $result = Run-Worker 'fresh' '维多利亚3'
    Assert ($result.state -eq 'ready') $result.message
    Assert ([IO.File]::Exists($result.trainerPath))
    Assert ($result.sha256 -eq (Get-FileHash -LiteralPath $result.trainerPath).Hash.ToLowerInvariant())
    Assert ($result.trainerGameName -eq 'Victoria 3')
    $index = [IO.File]::ReadAllText((Join-Path $ValidationRoot 'fresh/trainers.json')) | ConvertFrom-Json
    Assert ($index.victoria3.trainerPath -eq $result.trainerPath)
}
Check 'ready cache makes repeat requests entirely offline' {
    $before = & $module { $script:networkCalls }
    $result = Run-Worker 'fresh' 'Victoria 3'
    Assert ($result.state -eq 'ready') $result.message
    Assert ((& $module { $script:networkCalls }) -eq $before)
}
Check 'ZIP downloads extract nested trainer with metadata' {
    & $module { $script:networkMode = 'zip' }
    $result = Run-Worker 'zip'
    Assert ($result.state -eq 'ready') $result.message
    Assert ($result.trainerPath -match 'nested\\Trainer.exe$')
    & $module { $script:networkMode = 'exe' }
}
Check 'ZIP traversal is rejected before outside writes' {
    $archive = Join-Path $ValidationRoot 'traversal.zip'
    $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
    try { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $fixturePe, '../outside.exe') | Out-Null } finally { $zip.Dispose() }
    $destination = Join-Path $ValidationRoot 'zip-out'
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    Expect-Error { Expand-TrainerZip $archive $destination @{CancelPath=(Join-Path $ValidationRoot 'not.cancel');Deadline=[DateTime]::UtcNow.AddMinutes(1)} }
    Assert (-not [IO.File]::Exists((Join-Path $ValidationRoot 'outside.exe')))
}
Check 'HTML in place of executable never becomes ready' {
    & $module { $script:networkMode = 'html' }
    $result = Run-Worker 'blocked-html'
    Assert ($result.state -eq 'failed')
    & $module { $script:networkMode = 'exe' }
}
Check 'failed downloads cool down; explicit retry performs a new request' {
    & $module { $script:networkMode = 'failure' }
    $a = Run-Worker 'failure'
    Assert ($a.state -eq 'failed')
    $count = & $module { $script:networkCalls }
    $b = Run-Worker 'failure'
    Assert ($b.state -eq 'failed')
    Assert ((& $module { $script:networkCalls }) -eq $count)
    $c = Run-Worker 'failure' -Force
    Assert ((& $module { $script:networkCalls }) -gt $count)
    & $module { $script:networkMode = 'exe' }
}
Check 'cancel during download prevents installation and cleans staging' {
    & $module { $script:networkMode = 'cancel' }
    $result = Run-Worker 'cancel'
    Assert ($result.state -eq 'cancelled') $result.message
    Assert (-not [IO.Directory]::Exists((Join-Path $ValidationRoot 'cancel/installed')))
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $ValidationRoot 'cancel/staging') -Force).Count -eq 0)
    & $module { $script:networkMode = 'exe' }
}
Check 'unavailable game is cached without fuzzy fallback' {
    $result = Run-Worker 'unknown' 'Victoria 4'
    Assert ($result.state -eq 'unavailable') $result.message
    $before = & $module { $script:networkCalls }
    Run-Worker 'unknown' 'Victoria 4' | Out-Null
    Assert ((& $module { $script:networkCalls }) -eq $before)
}
Check 'existing local trainer is reused without network' {
    $folder = Join-Path $local 'real-victoria'
    New-Pe (Join-Path $folder 'Trainer.exe')
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='fling_main'}
    & $module { $script:networkMode = 'failure' }
    $result = Run-Worker 'local-only' '维多利亚3'
    Assert ($result.state -eq 'ready') $result.message
    Assert ($result.trainerPath -eq (Join-Path $folder 'Trainer.exe'))
}
Check 'request identity rejects outside result writes' {
    $cache = Join-Path $ValidationRoot 'identity'
    Expect-Error { Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $cache -ResultPath (Join-Path $ValidationRoot 'escape.json') -RequestId 'request-identity' }
    Assert (-not [IO.File]::Exists((Join-Path $ValidationRoot 'escape.json')))
}

Check 'new files use GCM naming, source, version and nested launch metadata' {
    $result = Run-Worker 'fresh' 'Victoria 3'
    Assert ($result.trainerFolder.StartsWith((Join-Path $ValidationRoot 'fresh/library')))
    Assert ((Split-Path -Leaf $result.trainerFolder) -eq '[风灵]《维多利亚3》修改器')
    $info = [IO.File]::ReadAllText((Join-Path $result.trainerFolder 'gcm_info.json')) | ConvertFrom-Json
    Assert ($info.origin -eq 'fling_main')
    Assert ($info.version -eq '2026.05.03')
    Assert ($info.extension -eq 'Trainer.exe')
    $nested = Run-Worker 'zip'
    $nestedInfo = [IO.File]::ReadAllText((Join-Path $nested.trainerFolder 'gcm_info.json')) | ConvertFrom-Json
    Assert ($nestedInfo.extension -eq 'nested/Trainer.exe')
}
Check 'local FLiNG trainer takes priority over another author without overwriting either' {
    $library = Join-Path $ValidationRoot 'preference-library'
    foreach ($pair in @(@('aaa-recifense','ct_other'), @('zzz-fling','fling_main'))) {
        $folder = Join-Path $library $pair[0]
        New-Pe (Join-Path $folder 'Trainer.exe')
        Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin=$pair[1]}
    }
    $result = Run-Worker 'preference' -LibraryRoot $library
    Assert ($result.trainerFolder -eq (Join-Path $library 'zzz-fling'))
    Assert ([IO.File]::Exists((Join-Path $library 'aaa-recifense/Trainer.exe')))
}
Check 'old YMCC downloads migrate offline and do not spend budget' {
    $legacy = Join-Path $ValidationRoot 'legacy/installed/old-victoria'
    New-Pe (Join-Path $legacy 'Trainer.exe')
    Write-Json (Join-Path $legacy 'gcm_info.json') @{game_name='Victoria 3';origin='ymcc_fling_official';version='2026-05-03 16:24';sha256=(Get-FileHash -LiteralPath (Join-Path $legacy 'Trainer.exe')).Hash}
    $before = & $module { $script:networkCalls }
    $result = Run-Worker 'legacy'
    Assert ($result.state -eq 'ready') $result.message
    Assert ($result.trainerFolder.StartsWith((Join-Path $ValidationRoot 'legacy/library')))
    Assert ((& $module { $script:networkCalls }) -eq $before)
    Assert ([IO.File]::Exists((Join-Path $legacy '.ymcc-migrated.json')))
    $record = [IO.File]::ReadAllText((Join-Path $ValidationRoot 'legacy/library/ymcc-library.json')) | ConvertFrom-Json
    Assert (@($record.downloads).Count -eq 0)
    Assert ($null -eq (Find-LocalTrainer @((Join-Path $ValidationRoot 'legacy/installed')) @('Victoria 3')))
}
Check 'exe fingerprint binds to canonical GCM identity and Steam ID' {
    $exe = Join-Path $ValidationRoot 'game-install/Game.exe'
    New-Pe $exe
    $first = Run-Worker 'identity-binding' -LibraryRoot $local -ExecutablePath $exe
    Assert ($first.state -eq 'ready') $first.message
    $record = [IO.File]::ReadAllText((Join-Path $local 'ymcc-library.json')) | ConvertFrom-Json
    $binding = @($record.games.PSObject.Properties)[0].Value
    $resource = $record.trainers.($binding.resourceKey)
    Assert ($binding.executablePath -eq $exe)
    Assert ($binding.executableSha256 -eq (Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant())
    Assert ($resource.gameName -eq 'Victoria 3')
    Assert ($resource.steamAppId -eq '529340')
    $count = & $module { $script:networkCalls }
    $renamed = Run-Worker 'identity-renamed-title' 'Unknown window caption' -LibraryRoot $local -ExecutablePath $exe
    Assert ($renamed.trainerPath -eq $first.trainerPath)
    Assert ((& $module { $script:networkCalls }) -eq $count)
}
Check 'changed exe cannot reuse another game binding based only on the path' {
    $exe = Join-Path $ValidationRoot 'game-install/Game.exe'
    [IO.File]::AppendAllText($exe, 'different-game')
    & $module { $script:networkMode = 'exe' }
    $result = Run-Worker 'identity-replaced' 'Unknown different game' -LibraryRoot $local -ExecutablePath $exe
    Assert ($result.state -eq 'unavailable') $result.message
}
Check 'GCM edits are discovered and rehashed instead of trusting stale YMCC index' {
    $first = Run-Worker 'local-only'
    [IO.File]::AppendAllText($first.trainerPath, 'gcm-updated-file')
    $next = Run-Worker 'local-only'
    Assert ($next.state -eq 'ready') $next.message
    Assert ($next.sha256 -ne $first.sha256)
    Assert ($next.sha256 -eq (Get-FileHash -LiteralPath $next.trainerPath).Hash.ToLowerInvariant())
}
Check 'three failed downloads consume count budget, Force and a new cache cannot reset it' {
    & $module { $script:networkMode = 'failure' }
    $library = Join-Path $ValidationRoot 'budget-library'
    1..3 | ForEach-Object { $result = Run-Worker ('budget-' + $_) -LibraryRoot $library -Force; Assert ($result.state -eq 'failed') }
    $before = & $module { $script:networkCalls }
    $limited = Run-Worker 'budget-after-restart' -LibraryRoot $library -Force
    Assert ($limited.state -eq 'budget-limited') $limited.message
    Assert ((& $module { $script:networkCalls }) -eq $before)
    $record = [IO.File]::ReadAllText((Join-Path $library 'ymcc-library.json')) | ConvertFrom-Json
    Assert (@($record.downloads).Count -eq 3)
    Assert (@($record.downloads | Where-Object { $_.state -eq 'failed' }).Count -eq 3)
    Assert ($record.budget.maxDownloads -eq 3)
    Assert ($record.budget.maxBytes -eq 209715200)
    & $module { $script:networkMode = 'exe' }
}
Check 'byte budget is enforced independently of download count' {
    $library = Join-Path $ValidationRoot 'bytes-library'
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=10;maxBytes=1024};downloads=@(@{requestId='past';at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();chargedBytes=1024;bytes=1024;state='ready'});games=@{};trainers=@{}}
    $before = & $module { $script:networkCalls }
    $result = Run-Worker 'bytes-cap' -LibraryRoot $library -Force
    Assert ($result.state -eq 'budget-limited') $result.message
    Assert ((& $module { $script:networkCalls }) -eq $before)
}
Check 'rolling budget expires after 24 hours and records actual completed bytes' {
    $library = Join-Path $ValidationRoot 'expired-budget-library'
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=1;maxBytes=1024};downloads=@(@{requestId='old';at=[DateTimeOffset]::UtcNow.AddHours(-25).ToUnixTimeMilliseconds();chargedBytes=1024;bytes=1024;state='ready'});games=@{};trainers=@{}}
    $result = Run-Worker 'expired-budget' -LibraryRoot $library
    Assert ($result.state -eq 'ready') $result.message
    $record = [IO.File]::ReadAllText((Join-Path $library 'ymcc-library.json')) | ConvertFrom-Json
    $last = @($record.downloads)[-1]
    Assert ($last.state -eq 'ready')
    Assert ($last.chargedBytes -eq (Get-Item -LiteralPath $fixturePe).Length)
}
Check 'corrupt budget ledger blocks network rather than resetting the allowance' {
    $library = Join-Path $ValidationRoot 'corrupt-ledger'
    [IO.Directory]::CreateDirectory($library) | Out-Null
    [IO.File]::WriteAllText((Join-Path $library 'ymcc-library.json'), '{broken')
    $before = & $module { $script:networkCalls }
    $result = Run-Worker 'corrupt-ledger-cache' -LibraryRoot $library
    Assert ($result.state -eq 'failed')
    Assert ((& $module { $script:networkCalls }) -eq $before)
}
Check 'anonymous trainer file cannot borrow identity from its folder name' {
    $root=Join-Path $ValidationRoot 'anonymous';New-Pe (Join-Path $root 'Victoria 3/Trainer.exe')
    Assert ($null -eq (Find-LocalTrainer @($root) @('Victoria 3') (Join-Path $settings 'db')))
}
Check 'GCM resource ID reconnects a renamed local trainer without a title guess' {
    $root=Join-Path $ValidationRoot 'id-reconnect';$db=Join-Path $root 'db';$library=Join-Path $root 'library';$folder=Join-Path $library 'custom label'
    New-Pe (Join-Path $folder 'Trainer.exe')
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='My custom title';origin='fling_main';gcm_url='GCM/unique/Victoria3';extension='Trainer.exe'}
    Write-Json (Join-Path $db 'fling_main.json') @(@{game_name='Victoria 3';gcm_url='GCM/unique/Victoria3'})
    $found=Find-LocalTrainer @($library) @('Victoria 3') $db
    Assert ($found.gameName -eq 'Victoria 3' -and $found.path -eq (Join-Path $folder 'Trainer.exe'))
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='';origin='fling_main';gcm_url='GCM/unique/Victoria3';extension='Trainer.exe'}
    Assert ((Find-LocalTrainer @($library) @('Victoria 3') $db).path -eq (Join-Path $folder 'Trainer.exe'))
    Assert ($null -eq (Find-LocalTrainer @($library) @('Victoria 2') $db))
}
Check 'known resource ID for another game overrides a misleading matching label' {
    $root=Join-Path $ValidationRoot 'id-conflict';$db=Join-Path $root 'db';$folder=Join-Path $root 'library/item'
    New-Pe (Join-Path $folder 'Trainer.exe')
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';gcm_url='GCM/unique/Victoria2'}
    Write-Json (Join-Path $db 'fling_main.json') @(@{game_name='Victoria 2';gcm_url='GCM/unique/Victoria2'})
    Assert ($null -eq (Find-LocalTrainer @((Join-Path $root 'library')) @('Victoria 3') $db))
}
Check 'resource ID associated with multiple games is never selected automatically' {
    $root=Join-Path $ValidationRoot 'id-ambiguous';$db=Join-Path $root 'db';$folder=Join-Path $root 'library/item'
    New-Pe (Join-Path $folder 'Trainer.exe')
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';gcm_url='GCM/duplicate'}
    Write-Json (Join-Path $db 'fling_main.json') @(@{game_name='Victoria 2';gcm_url='GCM/duplicate'},@{game_name='Victoria 3';gcm_url='GCM/duplicate'})
    Assert ($null -eq (Find-LocalTrainer @((Join-Path $root 'library')) @('Victoria 3') $db))
}
Check 'local reuse is not reported as a new download' {
    $before=& $module {$script:networkCalls};$result=Run-Worker 'local-reuse-receipt' -LibraryRoot $local
    Assert ($result.state -eq 'ready' -and -not $result.downloaded)
    Assert ((& $module {$script:networkCalls}) -eq $before)
}
Check 'configured GCM downloadPath is authoritative' {
    $library = Join-Path $ValidationRoot 'configured-library'
    Write-Json (Join-Path $settings 'settings.json') @{downloadPath=$library}
    try {
        $result = Run-Worker 'configured'
        Assert ($result.state -eq 'ready') $result.message
        Assert ($result.trainerFolder.StartsWith($library))
    } finally { Remove-Item -LiteralPath (Join-Path $settings 'settings.json') }
}


Check 'ready results explicitly distinguish a newly downloaded file from local reuse' {
    $library=Join-Path $ValidationRoot 'outcome-library'
    $download=Run-Worker 'outcome-cache' -LibraryRoot $library
    Assert ($download.state -eq 'ready' -and $download.downloaded -and $download.downloadOutcome -eq 'downloaded')
    $reuse=Run-Worker 'outcome-cache' -LibraryRoot $library
    Assert ($reuse.state -eq 'ready' -and -not $reuse.downloaded -and $reuse.downloadOutcome -eq 'reused-local')
}
Check 'invalid existing EXE is preserved but cannot suppress a valid replacement download' {
    $library=Join-Path $ValidationRoot 'invalid-local-library';$folder=Join-Path $library 'bad-copy';$invalid=Join-Path $folder 'Trainer.exe'
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='fling_main';extension='Trainer.exe'}
    [IO.File]::WriteAllText($invalid,'<html>download interrupted</html>')
    $result=Run-Worker 'invalid-local-cache' -LibraryRoot $library
    Assert ($result.state -eq 'ready' -and $result.downloaded) $result.message
    Assert ($result.trainerPath -ine $invalid);Assert ([IO.File]::ReadAllText($invalid) -eq '<html>download interrupted</html>')
    Assert (Test-TrainerPe $result.trainerPath)
}
Check 'invalid existing CT is rejected as a local ready file' {
    $library=Join-Path $ValidationRoot 'invalid-table-library';$folder=Join-Path $library 'bad-table';$invalid=Join-Path $folder 'Trainer.ct'
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='ct_other';extension='Trainer.ct'}
    [IO.File]::WriteAllText($invalid,'<html>attachment expired</html>')
    $result=Run-Worker 'invalid-table-cache' -LibraryRoot $library
    Assert ($result.state -eq 'ready' -and $result.downloaded -and $result.trainerPath -ine $invalid) $result.message
}
Check 'an existing valid file works at exhausted quota without any HTTP request' {
    $library=Join-Path $ValidationRoot 'exhausted-local-library';$folder=Join-Path $library 'valid-copy';$valid=Join-Path $folder 'Trainer.exe'
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='fling_main';extension='Trainer.exe'};New-Pe $valid
    $now=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();$events=@(1..3|ForEach-Object{@{at=$now-$_;chargedBytes=0;bytes=0;state='failed'}})
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=3;maxBytes=209715200};downloads=$events;games=@{};trainers=@{}}
    $before=& $module {$script:networkCalls};$result=Run-Worker 'exhausted-local-cache' -LibraryRoot $library
    Assert ($result.state -eq 'ready' -and -not $result.downloaded) $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
}
Check 'a restored local file bypasses old network failure cooldown without downloading' {
    $library=Join-Path $ValidationRoot 'restored-library'
    & $module {$script:networkMode='failure'}
    try{$failed=Run-Worker 'restored-cache' -LibraryRoot $library;Assert ($failed.state -eq 'failed')}finally{& $module {$script:networkMode='exe'}}
    $folder=Join-Path $library 'restored';Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='fling_main';extension='Trainer.exe'};New-Pe (Join-Path $folder 'Trainer.exe')
    $before=& $module {$script:networkCalls};$result=Run-Worker 'restored-cache' -LibraryRoot $library
    Assert ($result.state -eq 'ready' -and -not $result.downloaded) $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
}
Check 'deleted trainer is downloaded again instead of trusting an old ready cache' {
    $library=Join-Path $ValidationRoot 'deleted-library';$first=Run-Worker 'deleted-cache' -LibraryRoot $library;Assert ($first.state -eq 'ready')
    [IO.File]::Delete($first.trainerPath);$next=Run-Worker 'deleted-cache' -LibraryRoot $library
    Assert ($next.state -eq 'ready' -and $next.downloaded -and [IO.File]::Exists($next.trainerPath)) $next.message
}
Check 'edited persisted allowance cannot raise the hard download cap above three attempts' {
    $library=Join-Path $ValidationRoot 'oversized-quota-library';$now=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $events=@(1..3|ForEach-Object{@{at=$now-$_;chargedBytes=0;bytes=0;state='failed'}})
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=999;maxBytes=9999999999};downloads=$events;games=@{};trainers=@{}}
    $before=& $module {$script:networkCalls};$result=Run-Worker 'oversized-quota-cache' -LibraryRoot $library -Force
    Assert ($result.state -eq 'budget-limited') $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
}


Check 'empty or null existing ledger never resets the download allowance' {
    foreach($text in @('', 'null')){
        $library=Join-Path $ValidationRoot ('empty-ledger-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($library)|Out-Null
        $ledgerPath=Join-Path $library 'ymcc-library.json';[IO.File]::WriteAllText($ledgerPath,$text)
        $before=& $module {$script:networkCalls};$result=Run-Worker ('empty-cache-'+[guid]::NewGuid().ToString('N')) -LibraryRoot $library -Force
        Assert ($result.state -eq 'failed') $result.message;Assert ((& $module {$script:networkCalls}) -eq $before);Assert ([IO.File]::ReadAllText($ledgerPath) -ceq $text)
    }
}
Check 'negative persisted download charges cannot create extra byte allowance' {
    $library=Join-Path $ValidationRoot 'negative-charge-library'
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=3;maxBytes=209715200};downloads=@(@{at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();chargedBytes=-1;bytes=0;state='failed'});games=@{};trainers=@{}}
    $before=& $module {$script:networkCalls};$result=Run-Worker 'negative-charge-cache' -LibraryRoot $library -Force
    Assert ($result.state -eq 'failed') $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
}


Check 'a temporarily unreadable local file does not trigger a replacement download' {
    $library=Join-Path $ValidationRoot 'locked-library';$folder=Join-Path $library 'locked';$file=Join-Path $folder 'Trainer.exe'
    Write-Json (Join-Path $folder 'gcm_info.json') @{game_name='Victoria 3';origin='fling_main';extension='Trainer.exe'};New-Pe $file
    $heldLocalFile=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{
        $before=& $module {$script:networkCalls}
        $result=Run-Worker 'locked-cache' -LibraryRoot $library
        Write-Json (Join-Path $ValidationRoot 'locked-case.json') @{file=$file;canRead=$heldLocalFile.CanRead;result=$result;networkBefore=$before;networkAfter=(& $module {$script:networkCalls})}
        Assert ($result.state -eq 'failed') $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
    }finally{$heldLocalFile.Dispose()}
}
Check 'edited byte allowance cannot exceed the fixed 200 MiB aggregate cap' {
    $library=Join-Path $ValidationRoot 'oversized-bytes-library'
    Write-Json (Join-Path $library 'ymcc-library.json') @{schema=1;budget=@{maxDownloads=999;maxBytes=9999999999};downloads=@(@{at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();chargedBytes=209715200;bytes=209715200;state='ready'});games=@{};trainers=@{}}
    $before=& $module {$script:networkCalls};$result=Run-Worker 'oversized-bytes-cache' -LibraryRoot $library -Force
    Assert ($result.state -eq 'budget-limited') $result.message;Assert ((& $module {$script:networkCalls}) -eq $before)
}

Write-Json (Join-Path $ValidationRoot 'result.json') @{passed=$script:passed;failed=$script:failed;network='mocked';executedTrainers=0}
Write-Host "Trainer backend: $script:passed passed, $script:failed failed. Evidence: $ValidationRoot"
if ($script:failed) { exit 1 }