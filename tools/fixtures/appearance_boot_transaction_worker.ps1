param([string]$Source,[string]$FixtureRoot,[string]$ThemeRoot,[string]$StateBase,[string]$Backup,[string]$ReadyName,[string]$MutexPrefix,[string]$Mode)
$ErrorActionPreference='Stop'
$fixtureSource=$Source;$fixtureThemeRoot=$ThemeRoot;$fixtureStateBase=$StateBase;$fixtureMode=$Mode
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. $fixtureSource -Action Library
$ThemeRoot=$fixtureThemeRoot;$StateBase=$fixtureStateBase;$Mode=$fixtureMode
$fixture=[IO.Path]::GetFullPath($FixtureRoot).TrimEnd('\')+'\'
foreach($candidate in @($ThemeRoot,$StateBase,$Backup)){if(![IO.Path]::GetFullPath($candidate).StartsWith($fixture,[StringComparison]::OrdinalIgnoreCase)){throw 'Transaction worker path escaped owned fixture'}}
if($ReadyName -notmatch '^Local\\YMCC\.AppearanceTransactionFixture\.[a-f0-9]{32}\.ready$' -or $MutexPrefix -notmatch '^Local\\YMCC\.CssLoader\.Fixture\.[a-f0-9]{32}\.$'){throw 'Invalid owned transaction synchronization name'}
$script:ThemeRoot=$ThemeRoot;$script:PresetStateBase=$StateBase
$originalMutex=${function:New-AppearanceMutex}.ToString();$anchor="'Local\YMCC.CssLoader.'"
if($originalMutex.Split(@($anchor),[StringSplitOptions]::None).Count -ne 2){throw 'Original appearance mutex declaration changed'}
Set-Item -LiteralPath 'Function:\New-AppearanceMutex' -Value ([scriptblock]::Create($originalMutex.Replace($anchor,("'"+$MutexPrefix+"'"))))
$mutex=New-AppearanceMutex 'ThemeControl';$held=$false;$ready=$null
try{
 try{$held=$mutex.WaitOne(1000)}catch [Threading.AbandonedMutexException]{$held=$true}
 if(!$held){throw 'Owned transaction mutex unavailable'}
 $undo=Read-PresetBackup $Backup;Begin-PresetTransaction $undo
 $round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$owner=Join-Path (Get-PresetStateDir) 'owner.json'
 Write-PresetBytes $round ([Text.Encoding]::UTF8.GetBytes('{interrupted '+$Mode+' theme'))
 Write-ConfigAtomic $owner ('{interrupted '+$Mode+' owner')
 $ready=[Threading.EventWaitHandle]::OpenExisting($ReadyName);$null=$ready.Set()
 # The test deliberately kills this owned root while it owns the real mutex.
 # Both atomic writes completed; interruption is within the multi-file transaction.
 [Threading.Thread]::Sleep(15000)
 throw 'Owned transaction worker was not interrupted'
}finally{if($ready){$ready.Dispose()};if($held){$mutex.ReleaseMutex()};$mutex.Dispose()}
