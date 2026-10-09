param(
    [Parameter(Mandatory=$true)][ValidateRange(1,300)][int]$Percent,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'SteamDeckMouseEvidence')
)
$ErrorActionPreference = 'Stop'
# Read-only Steam probe: no registry writes, no Steam launch/kill, no layout edits.
if (Get-Process -Name steam -ErrorAction SilentlyContinue) {
    throw '请在 Steam 中保存右摇杆灵敏度，然后从 Steam 菜单正常退出，再运行采集（不会替你关闭 Steam）。'
}
$steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam').SteamPath
$account = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam\ActiveProcess' -ErrorAction SilentlyContinue).ActiveUser
if (-not $account -and $steam) {
    # Steam may clear ActiveUser on exit. Match the existing sleep-fix fallback:
    # last modified userdata account's localconfig; read file metadata only.
    $userdata = Join-Path $steam 'userdata'
    $last = @(Get-ChildItem -LiteralPath $userdata -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+$' } | ForEach-Object {
        $local = Join-Path $_.FullName 'config\localconfig.vdf'
        if (Test-Path -LiteralPath $local -PathType Leaf) { [pscustomobject]@{Account=$_.Name;Time=(Get-Item -LiteralPath $local).LastWriteTimeUtc} }
    } | Sort-Object Time -Descending | Select-Object -First 1)
    if ($last.Count) { $account = $last[0].Account }
}
if (-not $steam -or -not $account) { throw '未找到 Steam 路径或最近登录账号。请先登录 Steam 并正常退出。' }
$config = Join-Path $steam "steamapps\common\Steam Controller Configs\$account\config"
if (-not (Test-Path -LiteralPath $config -PathType Container)) { throw '未找到当前账号的 Steam Controller Configs。' }
$stamp = [DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss')
$dest = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) "$Percent-percent-$stamp-$([Guid]::NewGuid().ToString('N').Substring(0,6))"
New-Item -ItemType Directory -Path $dest -Force | Out-Null
$files = @()
$selected = @(Get-ChildItem -LiteralPath $config -File -Filter '*.vdf' | Where-Object {
    $_.Name -like 'configset_*' -and ($_.Name -match 'neptune|28de|1205')
})
# App 413080 is the desktop layout. Also capture literal desktop directories,
# if present, for Steam versions that name the non-game layouts differently.
foreach ($dir in @('413080','desktop')) {
    $path = Join-Path $config $dir
    if (Test-Path -LiteralPath $path -PathType Container) {
        $selected += @(Get-ChildItem -LiteralPath $path -File -Filter '*.vdf')
    }
}
foreach ($file in $selected) {
    if ($file.Length -gt 2MB) { throw "布局文件异常大，停止采集：$($file.Name)" }
    $relative = $file.FullName.Substring(([IO.Path]::GetFullPath($config)).TrimEnd('\').Length + 1)
    $target = Join-Path (Join-Path $dest 'config') $relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    $text = [IO.File]::ReadAllText($file.FullName)
    # Remove SteamID64/AccountID; never collect loginusers/localconfig/cookies.
    $text = [regex]::Replace($text, '(?<!\d)7656\d{13}(?!\d)', 'STEAMID_REDACTED')
    $text = $text.Replace([string]$account, 'ACCOUNT_REDACTED')
    [IO.File]::WriteAllText($target, $text, [Text.UTF8Encoding]::new($false))
    $files += $relative
}
$base = Join-Path $steam 'controller_base\desktop_neptune.vdf'
if (Test-Path -LiteralPath $base -PathType Leaf) {
    $text = [regex]::Replace([IO.File]::ReadAllText($base), '(?<!\d)7656\d{13}(?!\d)', 'STEAMID_REDACTED')
    [IO.File]::WriteAllText((Join-Path $dest 'desktop_neptune-default.vdf'), $text, [Text.UTF8Encoding]::new($false))
}
$version = ''
$exe = Join-Path $steam 'steam.exe'
if (Test-Path -LiteralPath $exe -PathType Leaf) { $version = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion }
[ordered]@{schemaVersion=1;sliderPercent=$Percent;capturedUtc=[DateTimeOffset]::UtcNow.ToString('o');steamFileVersion=$version;
    scope='SteamDeck desktop controller layout only; account identifiers redacted; no login or cookies';configFiles=$files} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dest 'manifest.json') -Encoding UTF8
$zip = "$dest.zip"
Compress-Archive -LiteralPath $dest -DestinationPath $zip
Write-Host "采集完成：$zip"
Write-Host '请把两个或三个不同灵敏度的 ZIP 发给我，用于确认实际字段、百分比换算与活动布局路径。'
