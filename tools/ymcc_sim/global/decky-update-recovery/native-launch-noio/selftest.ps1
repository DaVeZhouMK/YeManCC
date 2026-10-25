param([Parameter(Mandatory=$true)][string]$Out,[string]$CoreSource='',[string]$WindowsSource='',
      [Parameter(Mandatory=$true)][string]$SourceRoot,
      [Parameter(Mandatory=$true)][string]$NoIoFixture,
      [Parameter(Mandatory=$true)][string]$ExpectedFixtureSha256)
$ErrorActionPreference='Stop'
function Read-NoIoSha([string]$Path){
    $stream=[IO.File]::OpenRead($Path);$algorithm=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
    finally{$algorithm.Dispose();$stream.Dispose()}
}
if($ExpectedFixtureSha256 -notmatch '^[a-f0-9]{64}$' -or (Read-NoIoSha $NoIoFixture) -ne $ExpectedFixtureSha256){throw 'STEAM_NATIVE_NOIO_FIXTURE_BINDING_CHANGED'}
$text=[IO.File]::ReadAllText($NoIoFixture)
if($text -notmatch '"kind"\s*:\s*"steam-native-launch-memory-terminal-fixture"'){throw 'STEAM_NATIVE_NOIO_FIXTURE_REQUIRED'}
# Verify every declared executable/fixture member before starting the bound Node entry.
$members=[regex]::Matches($text,'"path"\s*:\s*"((?:[^"\\]|\\.)*)"\s*,\s*"sha256"\s*:\s*"([a-f0-9]{64})"')
if($members.Count -lt 17){throw 'STEAM_NATIVE_NOIO_INCOMPLETE_FIXTURE'}
foreach($row in $members){$memberPath=[regex]::Unescape($row.Groups[1].Value);if((Read-NoIoSha $memberPath) -ne $row.Groups[2].Value){throw ('STEAM_NATIVE_NOIO_FIXTURE_MEMBER_CHANGED:'+ $memberPath)}}
$entryMatch=[regex]::Match($text,'"entry"\s*:\s*"((?:[^"\\]|\\.)*)"')
$nodeMatch=[regex]::Match($text,'"node"\s*:\s*\{\s*"path"\s*:\s*"((?:[^"\\]|\\.)*)"')
if(-not $entryMatch.Success -or -not $nodeMatch.Success){throw 'STEAM_NATIVE_NOIO_ENTRY_REQUIRED'}
$entry=[regex]::Unescape($entryMatch.Groups[1].Value);$node=[regex]::Unescape($nodeMatch.Groups[1].Value)
$entryDeclared=$false
foreach($row in $members){if([regex]::Unescape($row.Groups[1].Value) -eq $entry){$entryDeclared=$true}}
if(-not $entryDeclared){throw 'STEAM_NATIVE_NOIO_ENTRY_UNDECLARED'}
$argsForFixture=@($entry,'--source-root',$SourceRoot,'--out',$Out,'--fixture',$NoIoFixture,'--expected-fixture-sha256',$ExpectedFixtureSha256)
if($CoreSource){$argsForFixture+=@('--core-source',$CoreSource)}
if($WindowsSource){$argsForFixture+=@('--windows-source',$WindowsSource)}
& $node @argsForFixture
if($LASTEXITCODE -ne 0){throw ('Native Steam launch regression failed: '+$LASTEXITCODE)}
