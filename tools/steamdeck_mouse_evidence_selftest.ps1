# Isolated probe test: registry/process queries are stubbed and all data is fake.
$ErrorActionPreference='Stop'
$root=Join-Path 'G:\YeManCC-Work\_scratch\steamdeck-mouse-20261005' ('probe-selftest-'+[guid]::NewGuid().ToString('N'))
$steam=Join-Path $root 'steam'
$config=Join-Path $steam 'steamapps\common\Steam Controller Configs\12345678\config'
New-Item -ItemType Directory -Force -Path (Join-Path $config '413080'),(Join-Path $steam 'controller_base'),(Join-Path $steam 'userdata\12345678\config') | Out-Null
$layout=Join-Path $config '413080\controller_neptune.vdf'
[IO.File]::WriteAllText($layout,'"creator" "76561198012345678" "account" "12345678" "sensitivity" "145"')
[IO.File]::WriteAllText((Join-Path $config 'configset_controller_neptune.vdf'),'"controller_config" { "413080" { "autosave" "1" } }')
[IO.File]::WriteAllText((Join-Path $steam 'controller_base\desktop_neptune.vdf'),'"creator" "76561198012345678"')
[IO.File]::WriteAllText((Join-Path $steam 'userdata\12345678\config\localconfig.vdf'),'TOKEN_SHOULD_NEVER_BE_COLLECTED')
$before=(Get-FileHash -LiteralPath $layout).Hash
function Get-Process { param($Name,$ErrorAction) return $null }
function Get-ItemProperty { param($LiteralPath,$ErrorAction)
    if($LiteralPath -like '*ActiveProcess'){return [pscustomobject]@{ActiveUser=0}}
    return [pscustomobject]@{SteamPath=$steam}
}
& (Join-Path $PSScriptRoot 'collect-steamdeck-mouse-evidence.ps1') -Percent 145 -OutputDirectory (Join-Path $root 'exports')
$zip=Get-ChildItem -LiteralPath (Join-Path $root 'exports') -Filter '*.zip' | Select-Object -First 1
$read=Join-Path $root 'inspect'
Expand-Archive -LiteralPath $zip.FullName -DestinationPath $read
$all=@(Get-ChildItem -LiteralPath $read -File -Recurse)
$text=($all | ForEach-Object {[IO.File]::ReadAllText($_.FullName)}) -join "`n"
if($text.Contains('76561198012345678') -or $text.Contains('12345678') -or $text.Contains('TOKEN_SHOULD_NEVER_BE_COLLECTED')){throw 'Private IDs or login config leaked'}
if(-not $text.Contains('"sensitivity" "145"')){throw 'Sensitivity absent'}
if((Get-FileHash -LiteralPath $layout).Hash -ne $before){throw 'Probe modified Steam layout'}
if(-not $text.Contains('ACCOUNT_REDACTED') -or -not $text.Contains('STEAMID_REDACTED')){throw 'IDs not redacted'}
Write-Output 'STEAMDECK_MOUSE_PROBE_OK: read-only capture, last-account fallback, ZIP, account redaction, no login data'
