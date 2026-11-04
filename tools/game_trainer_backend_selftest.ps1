[CmdletBinding()]
param([string]$ValidationRoot = '', [switch]$PersistenceOnly)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$sourceRoot = Split-Path -Parent $PSScriptRoot
if ($PersistenceOnly -and (-not $ValidationRoot -or (Test-Path -LiteralPath $ValidationRoot))) { throw 'PersistenceOnly requires an explicit fresh owned ValidationRoot' }
if (-not $ValidationRoot) { $ValidationRoot = Join-Path $sourceRoot ('../../Build/Validation/TrainerBackend-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$ValidationRoot = [IO.Path]::GetFullPath($ValidationRoot)
[IO.Directory]::CreateDirectory($ValidationRoot) | Out-Null
$persistenceStartedAt=[DateTimeOffset]::UtcNow
$persistenceBefore=@()
if($PersistenceOnly){
    $persistenceBefore=@('PowerControl/Trainer/TrainerBackend.psm1','PowerControl/Trainer/TrainerGcmFallback.ps1','PowerControl/Trainer/TrainerCatalog.ps1','PowerControl/Trainer/TrainerIdentity.ps1','PowerControl/Trainer/TrainerLaunchPaths.ps1','PowerControl/Trainer/TrainerLaunch.ps1','PowerControl/Trainer/TrainerCustomLibrary.ps1','PowerControl/Trainer/TrainerProcessIdentity.ps1','PowerControl/Trainer/TrainerForeground.ps1','PowerControl/GameTitlePolicy.ps1','PowerControl/GameTitlePolicy.json','tools/game_trainer_backend_selftest.ps1','tools/fixtures/trainer_ready_tmnt_20261022.json')|ForEach-Object{$p=Join-Path $sourceRoot $_;@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}}
}
Import-Module (Join-Path $sourceRoot 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking

if ($PersistenceOnly) {
    $module = Get-Module TrainerBackend
    & $module {
        param($scratch, $Repo, $InputPins, $StartedAt)
        $ErrorActionPreference = 'Stop'
        $script:checks = New-Object Collections.Generic.List[object]
        $script:lockObservedMs = $null
        function Check([string]$Name, [scriptblock]$Action) {
            try { & $Action; $script:checks.Add(@{name=$Name;status='PASS'}); Write-Output ('PASS '+$Name) }
            catch { $script:checks.Add(@{name=$Name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace}); Write-Output ('FAIL '+$Name+': '+$_.Exception.Message) }
        }
        function Assert($Value,[string]$Message) { if(-not $Value) { throw $Message } }
        function Expect-Rejection([scriptblock]$Action) { $rejected=$false; try { & $Action|Out-Null } catch { $rejected=$true }; Assert $rejected 'Expected production helper rejection' }
        function Reject([scriptblock]$Action) { Expect-Rejection $Action }
        function CasePath([string]$Name) {
            $p=[IO.Path]::GetFullPath((Join-Path $scratch $Name))
            Assert ($p.StartsWith([IO.Path]::GetFullPath($scratch)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Owned root escape'
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null
            return $p
        }
        function Put([string]$Name,$Value) { $p=CasePath $Name; [IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $Value -Depth 12 -Compress),(New-Object Text.UTF8Encoding($false))); return $p }
        $utf8=New-Object Text.UTF8Encoding($false)
Check 'legal BOM and unknown fields survive actual helper read/write/reread' {
 $p=CasePath 'legal.json';[IO.File]::WriteAllText($p,[char]0xFEFF+'{"schemaVersion":1,"entry":{"enabled":false},"extension":{"kept":[1,true]}}',$utf8)
 $value=Read-TrainerJson $p;Write-TrainerJson $p $value;$again=Read-TrainerJson $p
 Assert ($again.extension.kept[0] -eq 1 -and $again.extension.kept[1] -eq $true -and $again.entry.enabled -eq $false) 'Legal data or unknown extension lost'
}
Check 'empty existing document is distinguished from missing by production reader' {
 $p=CasePath 'empty.json';[IO.File]::WriteAllBytes($p,[byte[]]@());Expect-Rejection {Read-TrainerJson $p}
}
Check 'invalid UTF8 bytes are rejected instead of parsed with replacement characters' {
 $p=CasePath 'encoding.json';[byte[]]$bytes=@([Text.Encoding]::UTF8.GetBytes('{"name":"'))+@([byte]255)+@([Text.Encoding]::UTF8.GetBytes('"}'))
 [IO.File]::WriteAllBytes($p,$bytes);Expect-Rejection {Read-TrainerJson $p}
}
Check 'actual default 8MiB read bound preserves oversized bytes' {
 $p=CasePath 'oversize.json';[IO.File]::WriteAllText($p,(' ' * (8388608+1)),$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
 Expect-Rejection {Read-TrainerJson $p};Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $sha) 'Oversize original changed'
}
Check 'corrupt replacement retains exact original archive before a safe edit' {
 $p=CasePath 'corrupt.json';$raw='{broken';[IO.File]::WriteAllText($p,$raw,$utf8);[IO.File]::WriteAllText((CasePath 'corrupt-original.bin'),$raw,$utf8)
 Write-TrainerJson $p @{schemaVersion=1;entries=@{}}
 $archives=@(Get-ChildItem -LiteralPath $scratch -File | Where-Object {$_.Name -like 'corrupt.json.*' -and [IO.File]::ReadAllText($_.FullName) -ceq $raw})
 Assert ($archives.Count -gt 0) 'Actual writer replaced corrupt bytes without a matching archive'
}
Check 'future document refuses an ordinary lower schema replacement' {
 $p=CasePath 'future.json';$raw='{"schemaVersion":99,"future":{"keep":true}}';[IO.File]::WriteAllText($p,$raw,$utf8);[IO.File]::WriteAllText((CasePath 'future-original.bin'),$raw,$utf8)
 Expect-Rejection {Write-TrainerJson $p @{schemaVersion=1;entries=@{}}};Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Future original changed'
}
Check 'locked replacement fails exactly and later explicit write succeeds' {
 $p=CasePath 'locked.json';$raw='{"kept":true}';[IO.File]::WriteAllText($p,$raw,$utf8);$lock=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
 try{Expect-Rejection {Write-TrainerJson $p @{kept=$false}}}finally{$lock.Dispose()}
 Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Failed replacement lost original';Write-TrainerJson $p @{kept=$false};Assert ((Read-TrainerJson $p).kept -eq $false) 'Explicit retry failed'
}
Check 'valid catalog arrays and foreign metadata versions retain their existing contracts' {
 $p=CasePath 'translations.json';[IO.File]::WriteAllText($p,'[{"en_US":"one","extension":{"keep":true}},{"en_US":"two"}]',$utf8)
 $rows=@(Read-TrainerJson $p);Assert ($rows.Count -eq 2) 'Catalog array enumerated incorrectly';Write-TrainerJson $p $rows
 Assert ([IO.File]::ReadAllText($p).TrimStart().StartsWith('[')) 'Catalog array became an object'
 $again=@(Read-TrainerJson $p);Assert ($again.Count -eq 2 -and $again[0].extension.keep -eq $true) 'Catalog unknown lost'
 $metadata=CasePath 'gcm_info.json';[IO.File]::WriteAllText($metadata,'{"version":"1.2.3","game_name":"kept"}',$utf8);$value=Read-TrainerJson $metadata;Write-TrainerJson $metadata $value
 Assert ((Read-TrainerJson $metadata).version -ceq '1.2.3') 'Foreign game version was treated as our schema'
 foreach($number in @(2,3.5)){
  [IO.File]::WriteAllText($metadata,('{"version":'+$number+',"game_name":"kept","unknown":{"preserve":true}}'),$utf8)
  $value=Read-TrainerJson $metadata;Write-TrainerJson $metadata $value;$value=Read-TrainerJson $metadata
  Assert ($value.version -eq $number -and $value.unknown.preserve -eq $true) 'Foreign numeric game version/unknown changed'
 }
}
Check 'actual TMNT v2 cache keeps absent worker ok and legal Version game entries' {
 $field=[IO.File]::ReadAllText((Join-Path $Repo 'tools/fixtures/trainer_ready_tmnt_20261022.json'))|ConvertFrom-Json
 $p=CasePath 'tmnt/trainers.json';[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null
 Write-TrainerJson $p $field.index;$value=Read-TrainerJson $p;$entry=@($value.PSObject.Properties)[0].Value
 Assert ($entry.storageVersion -eq 2 -and $null -eq $entry.PSObject.Properties['ok']) 'Cache was rewritten to a worker reply'
 $version=CasePath 'legal-title/trainers.json';Write-TrainerJson $version @{version=@{gameName='Version';storageVersion=2;state='ready';extension=@{keep=$true}}}
 Assert ((Read-TrainerJson $version).version.extension.keep -eq $true) 'Legal Version title was lost'
}
Check 'known cache malformed roots archive exact bytes including empty before replacement' {
 $i=0
 foreach($raw in @('','[]','null','false','0','"text"','{unquoted:1}','{"number":+1}','{"number":01}','{"true":true,"a":[1,]}','{"entries":','{"x":NaN}','{"x":1e999}')){
  $p=CasePath ('shape-'+$i+'/trainers.json');$i++;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,$raw,$utf8)
  Expect-Rejection {Read-TrainerJson $p};Write-TrainerJson $p @{}
  $archives=@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($p)) -File|Where-Object{$_.Name -like 'trainers.json.corrupt-*.json'})
  Assert ($archives.Count -eq 1 -and [IO.File]::ReadAllText($archives[0].FullName) -ceq $raw) 'Wrong-root exact archive missing'
 }
}
Check 'future root and future index entries reject ordinary writes without archive' {
 $i=0
 foreach($raw in @('{"schemaVersion":99}','{"version":2}','{"game":{"gameName":"game","storageVersion":3,"state":"ready"}}')){
  $p=CasePath ('future-'+$i+'/trainers.json');$i++;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,$raw,$utf8)
  Expect-Rejection {Read-TrainerJson $p};Expect-Rejection {Write-TrainerJson $p @{}}
  Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Future bytes changed';Assert (@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($p)) -File).Count -eq 1) 'Future write created archive/temp'
 }
}
Check 'late writer cannot overwrite an external replacement and explicit reread can retry' {
 $p=CasePath 'cas.json';[IO.File]::WriteAllText($p,'{"value":1,"extension":{"keep":true}}',$utf8);$old=Read-TrainerJson $p
 $new='{"value":2,"extension":{"keep":true}}';[IO.File]::WriteAllText($p,$new,$utf8);$old.value=3;Expect-Rejection {Write-TrainerJson $p $old}
 Assert ([IO.File]::ReadAllText($p) -ceq $new) 'Old read overwrote new bytes';$fresh=Read-TrainerJson $p;$fresh.value=4;Write-TrainerJson $p $fresh
 $again=Read-TrainerJson $p;Assert ($again.value -eq 4 -and $again.extension.keep -eq $true) 'Explicit reread/save did not heal'
}
Check 'archive IO refusal preserves corrupt bytes and a later explicit write can recover' {
 $p=CasePath 'archive-denied.json';$raw='{broken';[IO.File]::WriteAllText($p,$raw,$utf8)
 $original=(Get-Command Write-TrainerCorruptArchive).ScriptBlock
 Set-Item -Path Function:script:Write-TrainerCorruptArchive -Value {param($Path,$Snapshot)throw 'fixture archive denied'}
 try{Expect-Rejection {Write-TrainerJson $p @{safe=$true}};Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Archive refusal lost original'}finally{Set-Item -Path Function:script:Write-TrainerCorruptArchive -Value $original}
 Write-TrainerJson $p @{safe=$true};Assert ((Read-TrainerJson $p).safe -eq $true) 'Write after archive refusal failed'
}
Check 'atomic replace refusal preserves bytes and permits a later explicit save' {
 $p=CasePath 'replace-denied.json';$raw='{"kept":true}';[IO.File]::WriteAllText($p,$raw,$utf8)
 $held=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
 try{Expect-Rejection {Write-TrainerJson $p @{kept=$false}}}finally{$held.Dispose()}
 Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Replace refusal lost original';Write-TrainerJson $p @{kept=$false};Assert ((Read-TrainerJson $p).kept -eq $false) 'Explicit replace retry failed'
}
Check 'invalid encoding and IO faults are never missing or repairable corruption' {
 $p=CasePath 'bad-encoding.json';[IO.File]::WriteAllBytes($p,[byte[]]@(255));$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
 Expect-Rejection {Write-TrainerJson $p @{safe=$true}};Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $sha) 'Bad encoding overwritten'
 $q=CasePath 'io-denied.json';[IO.File]::WriteAllText($q,'{}',$utf8);$held=[IO.File]::Open($q,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
 try{Expect-Rejection {Read-TrainerJson $q};Expect-Rejection {Write-TrainerJson $q @{safe=$true}}}finally{$held.Dispose()}
 Assert ([IO.File]::ReadAllText($q) -ceq '{}') 'Read IO was treated as absent';Write-TrainerJson $q @{safe=$true}
}
Check 'nonfinite in-memory values refuse writes without inventing a valid string' {
 $p=CasePath 'nonfinite.json'
 foreach($bad in @([double]::NaN,[double]::PositiveInfinity,[double]::NegativeInfinity)){Expect-Rejection {Write-TrainerJson $p @{known=$bad}}}
 Assert (-not [IO.File]::Exists($p)) 'Nonfinite write created a document'
}
Check 'only actual absence initializes a new document' {
 $p=CasePath 'new/deeper/missing.json';Assert ($null -eq (Read-TrainerJson $p)) 'Missing document did not return missing';Write-TrainerJson $p @{safe=$true}
 Assert ((Read-TrainerJson $p).safe -eq $true) 'First save failed'
}
Check 'damaged budget ledger cannot be rebuilt into new download credits' {
 foreach($name in @('ymcc-library.json','ymcc-data-state.json')){
  $p=CasePath $name;$raw='{damaged';[IO.File]::WriteAllText($p,$raw,$utf8)
  Expect-Rejection {Write-TrainerJson $p @{schema=1;budget=@{maxDownloads=3};downloads=@()}}
  Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Damaged budget ledger was reset'
 }
}
Check 'writer mutex contention ends in bounded refusal with no overwrite' {
 $p=CasePath 'contended.json';$raw='{"kept":true}';[IO.File]::WriteAllText($p,$raw,$utf8)
 $name='Local\YMCC.TrainerJson.'+(Get-TrainerHash ([IO.Path]::GetFullPath($p)).ToLowerInvariant())
 $ready=New-Object Threading.ManualResetEvent($false);$release=New-Object Threading.ManualResetEvent($false)
 $runspace=[RunspaceFactory]::CreateRunspace();$runspace.Open();$worker=[PowerShell]::Create();$worker.Runspace=$runspace
 [void]$worker.AddScript('param($name,$ready,$release);$mutex=[Threading.Mutex]::new($false,$name);$owned=$false;try{$owned=$mutex.WaitOne(2000);if(-not $owned){throw "fixture could not acquire lock"};$ready.Set()|Out-Null;$release.WaitOne(10000)|Out-Null}finally{if($owned){$mutex.ReleaseMutex()};$mutex.Dispose()}').AddArgument($name).AddArgument($ready).AddArgument($release)
 $async=$worker.BeginInvoke()
 try{Assert ($ready.WaitOne(3000)) 'Fake lock worker not ready';$watch=[Diagnostics.Stopwatch]::StartNew();Expect-Rejection {Write-TrainerJson $p @{kept=$false}};$watch.Stop();$script:lockObservedMs=$watch.ElapsedMilliseconds;Assert ($script:lockObservedMs -ge 4000 -and $script:lockObservedMs -lt 7500) 'Lock deadline outside bound'}
 finally{$release.Set()|Out-Null;if($async.AsyncWaitHandle.WaitOne(3000)){$worker.EndInvoke($async)|Out-Null}else{$worker.Stop()};$worker.Dispose();$runspace.Dispose();$ready.Dispose();$release.Dispose()}
 Assert ([IO.File]::ReadAllText($p) -ceq $raw) 'Contended write changed original';Write-TrainerJson $p @{kept=$false}
}
Check 'valid ledger keeps lower limits history and unknown fields' {
 $p=Put 'valid-ledger.json' @{schema=1;budget=@{maxDownloads=2;maxBytes=1000};downloads=@(@{at=1700000000000L;chargedBytes=100;bytes=100});games=@{};trainers=@{};extension=@{keep=$true}}
 $record=Read-GcmLibraryRecord $p;Assert ($record.budget.maxDownloads -eq 2 -and $record.downloads.Count -eq 1 -and $record.extension.keep -eq $true) 'Valid ledger changed'
}
Check 'numeric string ledger allowances reject instead of gaining usable credits' {
 $p=Put 'string-ledger.json' @{schema=1;budget=@{maxDownloads='3';maxBytes='209715200'};downloads=@();games=@{};trainers=@{}};Reject {Read-GcmLibraryRecord $p}
}
Check 'existing ledger without download history rejects instead of resetting history' {
 $p=Put 'missing-history.json' @{schema=1;budget=@{maxDownloads=3;maxBytes=209715200L};games=@{};trainers=@{}};Reject {Read-GcmLibraryRecord $p}
}
Check 'future ledger schema rejects and leaves exact source bytes' {
 $p=Put 'future-ledger.json' @{schema=2;budget=@{maxDownloads=3;maxBytes=209715200L};downloads=@();games=@{};trainers=@{}};$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
 Reject {Read-GcmLibraryRecord $p};Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $sha) 'Future ledger changed'
}
Check 'array downloadPath does not coerce into a configured trainer root' {
 $settings=Join-Path $scratch 'settings';[IO.Directory]::CreateDirectory($settings)|Out-Null
 $configured=Join-Path $scratch 'array-configured-library';$default=Join-Path $scratch 'default-library'
 [IO.File]::WriteAllText((Join-Path $settings 'settings.json'),(ConvertTo-Json -InputObject @{downloadPath=@($configured)} -Compress),(New-Object Text.UTF8Encoding($false)))
 $value=Get-GcmTrainerRoot $settings $default;Assert ($value -ceq $default) 'Array downloadPath was coerced to an active configured directory'
}
        Check 'postcommit readback fault refuses acknowledgement and explicit save can retry' {
            $p=Put 'postcommit.json' @{value=1}
            $null=Read-TrainerJson $p
            $original=(Get-Command Get-TrainerJsonBytes -ErrorAction Stop).ScriptBlock
            $script:receiptReadPath=$p; $script:receiptReadCount=0; $script:receiptReader=$original
            Set-Item Function:script:Get-TrainerJsonBytes {
                param($Path,[long]$Limit=8388608)
                if($Path -eq $script:receiptReadPath) {
                    $script:receiptReadCount++
                    if($script:receiptReadCount -eq 3){throw 'fixture postcommit readback denied'}
                }
                & $script:receiptReader $Path $Limit
            }
            try {
                Expect-Rejection { Write-TrainerJson $p @{value=2} }
                Assert (([IO.File]::ReadAllText($p)|ConvertFrom-Json).value -eq 2) 'Fixture did not reach disk commit'
            } finally { Set-Item Function:script:Get-TrainerJsonBytes $original }
            Write-TrainerJson $p @{value=3}
            Assert ((Read-TrainerJson $p).value -eq 3) 'Explicit save could not recover after uncertain acknowledgement'
        }
        Check 'secondary diagnostic write failure does not reverse primary committed receipt' {
            $p=CasePath 'progress/requests/owned.json'
            $context=@{RequestId='owned';GameName='fixture';ResultPath=$p}
            $original=(Get-Command Write-TrainerJson -ErrorAction Stop).ScriptBlock
            $script:progressWriter=$original
            Set-Item Function:script:Write-TrainerJson {
                param($Path,$Value)
                if([IO.Path]::GetFileName($Path) -eq 'last-game-status.json'){throw 'fixture diagnostic denied'}
                & $script:progressWriter $Path $Value
            }
            try { $result=Send-TrainerProgress $context 'ready' 'fixture committed' } finally { Set-Item Function:script:Write-TrainerJson $original }
            Assert ($result.ok -eq $true -and (Read-TrainerJson $p).ok -eq $true) 'Committed primary receipt was reversed by diagnostic failure'
        }
        function Pending([string]$Directory,[string]$Id) {
            $path=CasePath ($Directory+'/requests/'+$Id+'.json')
            $value=@{requestId=$Id;requestProtocol=1;requestToken='fixture-token-123456';sourceSession='fixture-session-123:0';gameExecutablePath='';gameProcessId=0;gameProcessCreated='';ok=$false;pending=$true;extension=@{preserve=$true}}
            [IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $value -Depth 10 -Compress),$utf8)
            return @{path=$path;value=$value;root=[IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($path))}
        }
        Check 'pending producer binding retains unknown fields and cannot be overridden by cached Extra' {
            $fixture=Pending 'bound-progress' 'bound-progress-0001'
            $binding=Get-TrainerRequestBinding $fixture.path $fixture.value.requestId
            Assert ((Read-TrainerJson $fixture.path).extension.preserve -eq $true) 'Read mutated pending unknown data'
            $context=@{RequestId=$fixture.value.requestId;ResultPath=$fixture.path;GameName='fixture';RequestBinding=$binding}
            $result=Send-TrainerProgress $context 'ready' 'fixture ready' @{requestId='stale';sourceSession='stale';requestToken='stale';gameProcessId=99;extension=@{preserve=$true}}
            Assert ($result.requestToken -ceq $fixture.value.requestToken -and $result.sourceSession -ceq $fixture.value.sourceSession -and $result.gameProcessId -eq 0 -and $result.requestId -ceq $fixture.value.requestId) 'Extra replaced authoritative request identity'
            Assert ((Read-TrainerJson $fixture.path).extension.preserve -eq $true) 'Unknown result data lost'
        }
        Check 'malformed future missing and mismatched pending identity refuses before side effects' {
            $i=0
            foreach($pair in @(@('requestProtocol',2),@('requestProtocol',1.5),@('requestProtocol',$true),@('requestProtocol','missing'),@('requestProtocol','1'),@('requestToken',$null),@('requestToken',@('one')),@('sourceSession',99),@('gameExecutablePath','C:\foreign.exe'),@('gameProcessId',42),@('gameProcessCreated','100'),@('ok',$true))){
                $fixture=Pending ('bound-bad-'+$i) ('bound-bad-000'+$i);$i++
                if($pair[0] -eq 'requestProtocol' -and $pair[1] -eq 'missing'){$fixture.value.Remove('requestProtocol')}else{$fixture.value[$pair[0]]=$pair[1]}
                [IO.File]::WriteAllText($fixture.path,(ConvertTo-Json -InputObject $fixture.value -Depth 10 -Compress),$utf8)
                $sha=(Get-FileHash -LiteralPath $fixture.path -Algorithm SHA256).Hash
                Expect-Rejection { Get-TrainerRequestBinding $fixture.path $fixture.value.requestId }
                Assert ((Get-FileHash -LiteralPath $fixture.path -Algorithm SHA256).Hash -ceq $sha) 'Invalid pending was rewritten'
            }
        }
        Check 'late progress cannot overwrite a replacement request and explicit read can retry' {
            $fixture=Pending 'bound-late' 'bound-late-0001';$binding=Get-TrainerRequestBinding $fixture.path $fixture.value.requestId
            $context=@{RequestId=$fixture.value.requestId;ResultPath=$fixture.path;GameName='fixture';RequestBinding=$binding}
            $fixture.value.requestToken='replacement-token-123';[IO.File]::WriteAllText($fixture.path,(ConvertTo-Json -InputObject $fixture.value -Depth 10 -Compress),$utf8)
            Expect-Rejection { Send-TrainerProgress $context 'ready' 'old completion' }
            Assert (([IO.File]::ReadAllText($fixture.path)|ConvertFrom-Json).requestToken -ceq 'replacement-token-123') 'Late progress replaced newer token'
            $context.RequestBinding=Get-TrainerRequestBinding $fixture.path $fixture.value.requestId
            $result=Send-TrainerProgress $context 'failed' 'explicit retry';Assert ($result.requestToken -ceq 'replacement-token-123' -and -not $result.ok) 'Explicit retry lost binding'
        }
        Check 'prepare fault handling repairs only confirmed corrupt index and preserves protected inputs' {
            $original=(Get-Command Get-GcmTrainerRoot).ScriptBlock
            Set-Item Function:script:Get-GcmTrainerRoot { throw 'fixture stops before library or network' }
            try {
                $i=0
                foreach($raw in @('{damaged','','[]','{"schemaVersion":99}',('{"extension":"'+('x'*8388608)+'"}'))){
                    $fixture=Pending ('prepare-protected-'+$i) ('prepare-protected-000'+$i);$i++
                    $index=Join-Path $fixture.root 'trainers.json';[IO.File]::WriteAllText($index,$raw,$utf8);$sha=(Get-FileHash -LiteralPath $index -Algorithm SHA256).Hash
                    Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId -GcmSettingsRoot (Join-Path $scratch 'empty-settings') -GcmDefaultRoot (Join-Path $scratch 'empty-library')
                    $receipt=Read-TrainerJson $fixture.path;Assert (-not $receipt.ok -and $receipt.state -eq 'failed' -and $receipt.requestToken -ceq $fixture.value.requestToken) 'Fault became successful readiness or lost binding'
                    $archives=@(Get-ChildItem -LiteralPath $fixture.root -Filter 'trainers.json.corrupt-*.json' -File)
                    if($raw -eq '{damaged' -or $raw -eq '' -or $raw -eq '[]'){
                        Assert ($archives.Count -eq 1 -and (Get-FileHash -LiteralPath $archives[0].FullName -Algorithm SHA256).Hash -ceq $sha) 'Actual prepare lost corrupt source archive'
                        Assert ((Read-TrainerJson $index).fixture.state -eq 'failed') 'Corrupt cache did not become safe usable state'
                    } else { Assert ((Get-FileHash -LiteralPath $index -Algorithm SHA256).Hash -ceq $sha -and $archives.Count -eq 0) 'Protected cache changed' }
                }
                $fixture=Pending 'prepare-io' 'prepare-io-0001';$index=Join-Path $fixture.root 'trainers.json';[IO.File]::WriteAllText($index,'{}',$utf8)
                $held=[IO.File]::Open($index,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
                try { Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId -GcmSettingsRoot (Join-Path $scratch 'empty-settings') -GcmDefaultRoot (Join-Path $scratch 'empty-library') } finally { $held.Dispose() }
                Assert ([IO.File]::ReadAllText($index) -ceq '{}') 'IO denial became damaged/missing'
            } finally { Set-Item Function:script:Get-GcmTrainerRoot $original }
        }
        Check 'actual launch and catalog echo only validated binding with inert side effect functions' {
            $names=@('Resolve-TrainerLaunchPlan','Assert-TrainerLaunchTarget','Start-TrainerLaunchPlan','Get-GcmTrainerRoot','Update-TrainerPublicCatalog');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $script:fixtureEffects=0
            Set-Item Function:script:Resolve-TrainerLaunchPlan { $script:fixtureEffects++; return @{program='fixture-never-executed.exe';arguments='';trainer=$null} }
            Set-Item Function:script:Assert-TrainerLaunchTarget { }
            Set-Item Function:script:Start-TrainerLaunchPlan { $script:fixtureEffects++; return @{pid=314;reused=$false} }
            Set-Item Function:script:Get-GcmTrainerRoot { return $script:fixtureLibrary }
            Set-Item Function:script:Update-TrainerPublicCatalog { return @{available=$true;nextCheckAt=0} }
            $script:fixtureLibrary=CasePath 'inert-catalog-library'
            try {
                $fixture=Pending 'launch-bound' 'launch-bound-0001'
                Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId -GcmSettingsRoot (Join-Path $scratch 'empty-settings') -GcmDefaultRoot $script:fixtureLibrary
                $receipt=Read-TrainerJson $fixture.path;Assert ($receipt.ok -eq $true -and $receipt.requestToken -ceq $fixture.value.requestToken) 'Actual launch did not echo binding'
                Assert ($script:fixtureEffects -eq 2) 'Unexpected launch seam count'
                $bad=Pending 'launch-bad' 'launch-bad-0001';$bad.value.gameProcessId=42;[IO.File]::WriteAllText($bad.path,(ConvertTo-Json -InputObject $bad.value -Compress),$utf8)
                Expect-Rejection { Invoke-TrainerLaunch -Mode Manager -CacheRoot $bad.root -ResultPath $bad.path -RequestId $bad.value.requestId }
                Assert ($script:fixtureEffects -eq 2) 'Malformed request reached launch side effects'
                $fixture=Pending 'catalog-bound' 'catalog-bound-0001'
                Invoke-TrainerDataRefresh -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId -GcmSettingsRoot (Join-Path $scratch 'empty-settings') -GcmDefaultRoot $script:fixtureLibrary
                $receipt=Read-TrainerJson $fixture.path;Assert ($receipt.ok -eq $true -and $receipt.sourceSession -ceq $fixture.value.sourceSession) 'Actual catalog did not echo binding'
            } finally { foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]} }
        }

        Check 'catalog timing and charge strings cannot create fresh metadata budget' {
            $old=(Get-Command Get-TrainerResponse).ScriptBlock;$script:catalogFixtureCalls=0
            Set-Item Function:script:Get-TrainerResponse {$script:catalogFixtureCalls++;throw 'fixture network must never be reached'}
            try {
                $i=0
                foreach($field in @('nextCheckAt','lastSuccessAt','etag','charge')){
                    $lib=CasePath ('catalog-types-'+$i);$i++;[IO.Directory]::CreateDirectory($lib)|Out-Null
                    $state=@{schema=1;attempts=@();nextCheckAt=0L;lastSuccessAt=0L;etag='';extension=@{preserve=$true}}
                    if($field -eq 'charge'){$state.attempts=@(@{at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();chargedBytes='0'})}
                    elseif($field -eq 'etag'){$state.etag=@('coerced')}else{$state[$field]='0'}
                    $p=Join-Path $lib 'ymcc-data-state.json';[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $state -Depth 10 -Compress),$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
                    $context=@{Deadline=[DateTime]::UtcNow.AddSeconds(10);CancelPath=(Join-Path $lib 'never.cancel')}
                    Expect-Rejection {Update-TrainerPublicCatalog $context $lib (CasePath 'empty-db')}
                    Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Malformed metadata budget changed'
                }
                Assert ($script:catalogFixtureCalls -eq 0) 'Malformed metadata allowance reached network seam'
            } finally { Set-Item Function:script:Get-TrainerResponse $old }
        }
        Check 'validated catalog snapshot archives damage while invalid source keeps last valid snapshot' {
            $db=CasePath 'catalog-snapshot/db';[IO.Directory]::CreateDirectory($db)|Out-Null
            $source=Join-Path $db 'translations.json';[IO.File]::WriteAllText($source,'[{"en_US":"fixture","unknown":{"keep":true}}]',$utf8)
            $snap=Join-Path $db 'ymcc-validated/translations.json';[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($snap))|Out-Null
            [IO.File]::WriteAllText($snap,'{broken',$utf8);$rawHash=(Get-FileHash -LiteralPath $snap -Algorithm SHA256).Hash
            $rows=@(Get-GcmCatalogRows $db 'translations.json');Assert ($rows.Count -eq 1 -and $rows[0].unknown.keep -eq $true) 'Legal external source could not repair damaged owned snapshot'
            $archives=@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($snap)) -Filter 'translations.json.corrupt-*.json' -File)
            Assert ($archives.Count -eq 1 -and (Get-FileHash -LiteralPath $archives[0].FullName -Algorithm SHA256).Hash -ceq $rawHash) 'Damaged snapshot archive missing'
            $script:TrainerCatalogMemory.Clear()
            [IO.File]::WriteAllText($source,'[{"en_US":99}]',$utf8)
            $rows=@(Get-GcmCatalogRows $db 'translations.json');Assert ($rows.Count -eq 1 -and $rows[0].en_US -ceq 'fixture') 'Wrong typed external names replaced validated snapshot'
            Assert ([IO.File]::ReadAllText($source) -ceq '[{"en_US":99}]') 'External GCM source was modified'
        }
        Check 'actual prepare persists a legacy ready cache without worker ok then reuses it with a fresh binding' {
            $fixture=Pending 'prepare-ready' 'prepare-ready-0001'
            $damagedIndex=Join-Path $fixture.root 'trainers.json';[IO.File]::WriteAllText($damagedIndex,(' '*(8388608+1)),$utf8)
            $damagedSha=(Get-FileHash -LiteralPath $damagedIndex -Algorithm SHA256).Hash
            $library=CasePath 'ready-library';$folder=Join-Path $library 'owned-fixture';[IO.Directory]::CreateDirectory($folder)|Out-Null
            $exe=Join-Path $folder 'Trainer.exe';$bytes=New-Object byte[] 128;$bytes[0]=0x4d;$bytes[1]=0x5a;$bytes[60]=80;$bytes[80]=0x50;$bytes[81]=0x45;[IO.File]::WriteAllBytes($exe,$bytes)
            [IO.File]::WriteAllText((Join-Path $folder 'gcm_info.json'),'{"game_name":"fixture","origin":"fixture","version":2}',$utf8)
            $old=(Get-Command Resolve-TrainerIdentity).ScriptBlock
            Set-Item Function:script:Resolve-TrainerIdentity { return @{method='steam-appid';steamAppId='12345';aliases=@('fixture');manifestPath='fixture-owned-manifest'} }
            $oldNetwork=(Get-Command Get-TrainerResponse).ScriptBlock;$script:readyNetwork=0
            Set-Item Function:script:Get-TrainerResponse {$script:readyNetwork++;throw 'fixture network forbidden'}
            try {
                Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId -GcmSettingsRoot (CasePath 'empty-settings') -GcmDefaultRoot $library
                $receipt=Read-TrainerJson $fixture.path;Assert ($receipt.ok -eq $true -and $receipt.requestToken -ceq $fixture.value.requestToken -and -not $receipt.downloaded) $receipt.message
                $index=Read-TrainerJson (Join-Path $fixture.root 'trainers.json');Assert ($index.fixture.storageVersion -eq 2 -and $null -eq $index.fixture.PSObject.Properties['ok']) 'Producer durable v2 contract changed'
                $second=Pending 'prepare-ready' 'prepare-ready-0002'
                Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $second.root -ResultPath $second.path -RequestId $second.value.requestId -GcmSettingsRoot (CasePath 'empty-settings') -GcmDefaultRoot $library
                $again=Read-TrainerJson $second.path
                Assert ($again.ok -eq $true -and $again.requestId -ceq $second.value.requestId -and $again.requestToken -ceq $second.value.requestToken -and -not $again.downloaded) 'Explicit local retry did not reuse cache'
                $archives=@(Get-ChildItem -LiteralPath $fixture.root -Filter 'trainers.json.corrupt-*.json' -File)
                Assert ($archives.Count -eq 1 -and (Get-FileHash -LiteralPath $archives[0].FullName -Algorithm SHA256).Hash -ceq $damagedSha) 'Actual preparation did not retain oversize damaged original'
                Assert ($script:readyNetwork -eq 0) 'Local cache reuse reached network'
            } finally { Set-Item Function:script:Resolve-TrainerIdentity $old;Set-Item Function:script:Get-TrainerResponse $oldNetwork }
        }

        Check 'malformed GCM launch flags and path types cannot reach program resolution' {
            $names=@('Repair-TrainerGcmCePath','Resolve-TrainerManagerProgram');$originals=@{};foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $script:optionResolution=0
            Set-Item Function:script:Repair-TrainerGcmCePath {param($SettingsRoot) return (Read-TrainerJson (Join-Path $SettingsRoot 'settings.json'))}
            Set-Item Function:script:Resolve-TrainerManagerProgram {$script:optionResolution++;return 'fixture-not-executed.exe'}
            try {
                $i=0
                foreach($bad in @(@{safePath='false'},@{launchAsAdmin=1},@{launchCTAsAdmin=@($false)},@{cePath=@('C:\fixture')},@{safePath=$null},@{managerPath=@('C:\fixture.exe')},@{enableGCM='false'})){
                    $dir=CasePath ('launch-options-'+$i);$i++;[IO.Directory]::CreateDirectory($dir)|Out-Null
                    $p=Join-Path $dir 'settings.json';[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $bad -Depth 8 -Compress),$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
                    Expect-Rejection {Resolve-TrainerLaunchPlan -Mode Manager -GcmSettingsRoot $dir -GcmDefaultRoot (CasePath 'empty-library')}
                    Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Malformed options were rewritten'
                }
                Assert ($script:optionResolution -eq 0) 'Malformed options reached resolver'
                $dir=CasePath 'launch-options-valid';[IO.Directory]::CreateDirectory($dir)|Out-Null
                $p=Join-Path $dir 'settings.json';[IO.File]::WriteAllText($p,'{"safePath":false,"launchAsAdmin":false,"launchCTAsAdmin":false,"cePath":"","extension":{"preserve":true}}',$utf8)
                $plan=Resolve-TrainerLaunchPlan -Mode Manager -GcmSettingsRoot $dir -GcmDefaultRoot (CasePath 'empty-library')
                Assert ($plan.program -ceq 'fixture-not-executed.exe' -and (Read-TrainerJson $p).extension.preserve -eq $true) 'Legal launch options/unknown were lost'
            } finally {foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
        }

        Check 'actual CE settings writer preserves unknown data and refuses future schema' {
            $dir=CasePath 'actual-ce-settings';[IO.Directory]::CreateDirectory($dir)|Out-Null
            $default=CasePath 'owned-ce-directory';[IO.Directory]::CreateDirectory($default)|Out-Null
            $binary=if([Environment]::Is64BitOperatingSystem){'cheatengine-x86_64.exe'}else{'cheatengine-i386.exe'}
            [IO.File]::WriteAllBytes((Join-Path $default $binary),[byte[]]@(0))
            $p=Join-Path $dir 'settings.json';[IO.File]::WriteAllText($p,'{"cePath":"","version":2,"extension":{"preserve":true}}',$utf8)
            $settings=Repair-TrainerGcmCePath -SettingsRoot $dir -DefaultDirectory $default
            Assert ($settings.cePath -ceq $default -and (Read-TrainerJson $p).extension.preserve -eq $true) 'Actual CE writer lost configured path/unknown'
            [IO.File]::WriteAllText($p,'{"schemaVersion":99,"cePath":"","extension":{"preserve":true}}',$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
            Expect-Rejection {Repair-TrainerGcmCePath -SettingsRoot $dir -DefaultDirectory $default}
            Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'CE writer downgraded future settings'
        }
        Check 'duplicate launch request cannot dispatch twice while the first fake launch is live' {
            $fixture=Pending 'launch-once' 'launch-once-0001'
            $names=@('Resolve-TrainerLaunchPlan','Assert-TrainerLaunchTarget','Start-TrainerLaunchPlan');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $script:onceDispatches=0;$script:onceNestedRejected=$false;$script:onceFixture=$fixture
            Set-Item Function:script:Resolve-TrainerLaunchPlan {return @{program='fixture-never-executed.exe';arguments='';trainer=$null}}
            Set-Item Function:script:Assert-TrainerLaunchTarget {}
            Set-Item Function:script:Start-TrainerLaunchPlan {
                $script:onceDispatches++
                if($script:onceDispatches -eq 1){
                    try{Invoke-TrainerLaunch -Mode Manager -CacheRoot $script:onceFixture.root -ResultPath $script:onceFixture.path -RequestId $script:onceFixture.value.requestId}
                    catch{$script:onceNestedRejected=$true}
                }
                return @{pid=314;reused=$false}
            }
            try {
                Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId
                Assert ($script:onceDispatches -eq 1 -and $script:onceNestedRejected) 'One pending request dispatched more than once'
                $receipt=Read-TrainerJson $fixture.path;Assert ($receipt.ok -eq $true -and $receipt.requestToken -ceq $fixture.value.requestToken) 'First request did not finish with its binding'
                Expect-Rejection {Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId}
                Assert ($script:onceDispatches -eq 1) 'Completed request replay dispatched again'
            }finally{foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
        }
        Check 'launch request claim write denial starts no side effects and explicit new request retries' {
            $names=@('Resolve-TrainerLaunchPlan','Assert-TrainerLaunchTarget','Start-TrainerLaunchPlan','Write-TrainerJson');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $fixture=Pending 'launch-claim-io' 'launch-claim-io-0001';$sha=(Get-FileHash -LiteralPath $fixture.path -Algorithm SHA256).Hash
            $script:claimEffects=0;$script:claimPath=$fixture.path;$script:claimWriter=$originals['Write-TrainerJson']
            Set-Item Function:script:Resolve-TrainerLaunchPlan {$script:claimEffects++;return @{program='fixture-never-executed.exe';arguments='';trainer=$null}}
            Set-Item Function:script:Assert-TrainerLaunchTarget {}
            Set-Item Function:script:Start-TrainerLaunchPlan {$script:claimEffects++;return @{pid=314;reused=$false}}
            Set-Item Function:script:Write-TrainerJson {param($Path,$Value) if($Path -eq $script:claimPath){throw 'fixture claim IO denied'}; & $script:claimWriter $Path $Value}
            try {
                Expect-Rejection {Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId}
                Assert ($script:claimEffects -eq 0) 'Claim IO denial happened after launch effects'
                Assert ((Get-FileHash -LiteralPath $fixture.path -Algorithm SHA256).Hash -ceq $sha) 'Claim denial mutated pending'
                Set-Item Function:script:Write-TrainerJson $originals['Write-TrainerJson']
                $next=Pending 'launch-claim-io' 'launch-claim-io-0002'
                Invoke-TrainerLaunch -Mode Manager -CacheRoot $next.root -ResultPath $next.path -RequestId $next.value.requestId
                Assert ($script:claimEffects -eq 2 -and (Read-TrainerJson $next.path).ok -eq $true) 'Explicit new request did not recover'
            }finally{foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
        }

        Check 'actual target-bound producer survives index save readback and identity faults with explicit retry' {
            $library=CasePath 'producer-fields/library';$folder=Join-Path $library 'fixture';[IO.Directory]::CreateDirectory($folder)|Out-Null
            $game=CasePath 'producer-fields/game/Game.exe';$trainer=Join-Path $folder 'Trainer.exe'
            $bytes=New-Object byte[] 128;$bytes[0]=0x4d;$bytes[1]=0x5a;$bytes[60]=80;$bytes[80]=0x50;$bytes[81]=0x45
            [IO.File]::WriteAllBytes($trainer,$bytes);$bytes[127]=74;[IO.File]::WriteAllBytes($game,$bytes)
            [IO.File]::WriteAllText((Join-Path $folder 'gcm_info.json'),'{"game_name":"fixture","origin":"fixture","version":2,"extension":"Trainer.exe","unknown":{"keep":true}}',$utf8)
            $manifest=CasePath 'producer-fields/steamapps/appmanifest_12345.acf';[IO.File]::WriteAllText($manifest,'owned inert manifest',$utf8)
            $script:producerGame=$game;$script:producerManifest=$manifest;$script:producerGameSha=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
            $trainerSha=(Get-FileHash -LiteralPath $trainer -Algorithm SHA256).Hash.ToLowerInvariant()
            $names=@('Resolve-TrainerIdentity','Assert-TrainerSharedGameTarget','Get-TrainerResponse','Write-TrainerJson','Get-TrainerJsonBytes');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $script:producerNetwork=0;$script:producerWorkerExitFaults=0;$script:producerTargetChecks=0;$script:producerRejectLateTarget=$false
            Set-Item Function:script:Resolve-TrainerIdentity {return @{method='steam-appid';steamAppId='12345';aliases=@('fixture');manifestPath=$script:producerManifest;executableSha256=$script:producerGameSha}}
            Set-Item Function:script:Assert-TrainerSharedGameTarget {
                param($Path,$PidValue,$Created)
                $script:producerTargetChecks++
                if($Path -cne $script:producerGame -or $PidValue -ne 42 -or $Created -cne '100' -or ($script:producerRejectLateTarget -and $script:producerTargetChecks -gt 1)){throw 'fixture target admission changed'}
                return @{path=$Path;pid=$PidValue;created=$Created}
            }
            Set-Item Function:script:Get-TrainerResponse {$script:producerNetwork++;throw 'fixture network forbidden'}
            function BoundPending([string]$Id) {
                $fx=Pending 'producer-fields/trainer-cache' $Id
                $fx.value.gameExecutablePath=$script:producerGame;$fx.value.gameProcessId=42;$fx.value.gameProcessCreated='100'
                [IO.File]::WriteAllText($fx.path,(ConvertTo-Json -InputObject $fx.value -Depth 10 -Compress),$utf8)
                return $fx
            }
            function BoundPrepare($Fx) {
                # shell.hidden acknowledges spawn; the renderer consumes the primary
                # persisted receipt even if the worker later exits on a cache IO fault.
                $workerFault=$null
                try { Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $Fx.root -ResultPath $Fx.path -RequestId $Fx.value.requestId -GameExecutablePath $script:producerGame -GameProcessId 42 -GameProcessCreated '100' -GcmSettingsRoot (CasePath 'producer-fields/empty-settings') -GcmDefaultRoot $library -CustomLibraryRoot (CasePath 'producer-fields/custom-library') } catch { $workerFault=$_.Exception }
                $doc=Read-TrainerJson $Fx.path
                if($workerFault){
                    $script:producerWorkerExitFaults++
                    if($doc.ok -ne $false -or $doc.state -notin @('failed','cancelled')){throw $workerFault}
                }
                return $doc
            }
            try {
                $first=BoundPending 'producer-fields-0001';$indexPath=Join-Path $first.root 'trainers.json'
                [IO.File]::WriteAllText($indexPath,'{"sibling":{"gameName":"other","state":"failed","ok":false,"extension":{"keep":true}},"extension":{"keep":true}}',$utf8)
                $receipt=BoundPrepare $first
                Assert ($receipt.ok -eq $true -and $receipt.state -eq 'ready') $receipt.message
                $index=Read-TrainerJson $indexPath;$cached=$index.fixture
                foreach($field in @('storageVersion','state','gameName','trainerGameName','trainerPath','trainerFolder','origin','gcmResourceId','sha256','identity','libraryRoot','downloaded','downloadOutcome','bytesDownloaded','checkedAt')){
                    Assert ($null -ne $cached.PSObject.Properties[$field] -and $null -ne $receipt.PSObject.Properties[$field]) ('Actual producer omitted '+$field)
                }
                Assert ($null -eq $cached.PSObject.Properties['ok'] -and $null -eq $cached.PSObject.Properties['requestToken']) 'Durable cache acquired transient acknowledgement/session'
                Assert ($cached.identity.processId -eq 42 -and $cached.identity.processCreated -ceq '100' -and $cached.identity.executablePath -ieq $game -and $cached.identity.executableSha256 -ceq $script:producerGameSha -and $cached.sha256 -ceq $trainerSha) 'Actual target/hash producer fields differ'
                Assert ($receipt.gameProcessId -eq 42 -and $receipt.gameProcessCreated -ceq '100' -and $receipt.requestToken -ceq $first.value.requestToken -and $receipt.sourceSession -ceq $first.value.sourceSession) 'Worker acknowledgement lost bound request identity'
                Assert ($index.sibling.extension.keep -eq $true -and $index.extension.keep -eq $true) 'Saving producer entry lost unrelated unknown data'
                $script:producerIndex=$indexPath;$script:producerWriter=$originals['Write-TrainerJson'];$script:producerReader=$originals['Get-TrainerJsonBytes']
                $script:producerSaveDenied=$true
                Set-Item Function:script:Write-TrainerJson {
                    param($Path,$Value)
                    if($script:producerSaveDenied -and $Path -eq $script:producerIndex){$script:producerSaveDenied=$false;throw 'fixture index save denied'}
                    & $script:producerWriter $Path $Value
                }
                $failed=BoundPrepare (BoundPending 'producer-fields-0002')
                Assert ($failed.ok -eq $false -and $failed.state -eq 'failed') 'Index save failure reported ready'
                Set-Item Function:script:Write-TrainerJson $originals['Write-TrainerJson']
                $script:producerReadbackArmed=$false;$script:producerReadbackCount=0
                Set-Item Function:script:Write-TrainerJson {
                    param($Path,$Value)
                    $arm=$Path -eq $script:producerIndex -and $Value.ContainsKey('fixture') -and $Value.fixture.state -eq 'ready'
                    if($arm){$script:producerReadbackArmed=$true;$script:producerReadbackCount=0}
                    try{& $script:producerWriter $Path $Value}finally{if($arm){$script:producerReadbackArmed=$false}}
                }
                Set-Item Function:script:Get-TrainerJsonBytes {
                    param($Path,[long]$Limit=8388608)
                    if($script:producerReadbackArmed -and $Path -eq $script:producerIndex){$script:producerReadbackCount++;if($script:producerReadbackCount -eq 3){throw 'fixture index postcommit readback denied'}}
                    & $script:producerReader $Path $Limit
                }
                $failed=BoundPrepare (BoundPending 'producer-fields-0003')
                Assert ($script:producerWorkerExitFaults -gt 0 -and $script:producerReadbackCount -eq 3 -and $failed.ok -eq $false -and $failed.state -eq 'failed') 'Postcommit readback refusal reported ready or did not reach commit'
                Set-Item Function:script:Write-TrainerJson $originals['Write-TrainerJson'];Set-Item Function:script:Get-TrainerJsonBytes $originals['Get-TrainerJsonBytes']
                $script:producerTargetChecks=0;$script:producerRejectLateTarget=$true
                $failed=BoundPrepare (BoundPending 'producer-fields-0004')
                Assert ($failed.ok -eq $false -and $failed.state -eq 'failed' -and $script:producerTargetChecks -gt 1) 'Late target admission failure reported ready'
                $script:producerRejectLateTarget=$false;$script:producerTargetChecks=0
                $last=BoundPending 'producer-fields-0005';$receipt=BoundPrepare $last
                Assert ($receipt.ok -eq $true -and $receipt.state -eq 'ready' -and $script:producerNetwork -eq 0) 'Explicit retry after save/readback/admission faults did not recover locally'
                $index=Read-TrainerJson $indexPath;Assert ($index.sibling.extension.keep -eq $true -and $index.extension.keep -eq $true) 'Failure/retry lost unrelated entry'
                $contract=@{gameName='fixture';dataDir=[IO.Path]::GetDirectoryName($last.root);cacheRoot=$last.root;gamePath=$game;trainerPath=$trainer;gameSha256=$script:producerGameSha;trainerSha256=$trainerSha;processId=42;processCreated='100';indexRaw=[IO.File]::ReadAllText($indexPath);receiptRaw=[IO.File]::ReadAllText($last.path);producerSha256=(Get-FileHash -LiteralPath (Join-Path $Repo 'PowerControl/Trainer/TrainerBackend.psm1') -Algorithm SHA256).Hash;scope='Actual Invoke-TrainerPrepare producer/save/reread, fake identity/target/network ports, owned inert PE bytes only';networkCalls=$script:producerNetwork;observedWorkerExitFaults=$script:producerWorkerExitFaults}
                [IO.File]::WriteAllText((CasePath 'actual-producer-contract.json'),(ConvertTo-Json -InputObject $contract -Depth 12),$utf8)
            }finally{foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
        }
        Check 'lost acknowledgement after launch claim commit cannot dispatch or replay that request' {
            $names=@('Resolve-TrainerLaunchPlan','Assert-TrainerLaunchTarget','Start-TrainerLaunchPlan','Write-TrainerJson');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $fixture=Pending 'claim-ack-lost' 'claim-ack-lost-0001';$script:claimAckPath=$fixture.path;$script:claimAckEffects=0;$script:claimAckWriter=$originals['Write-TrainerJson']
            Set-Item Function:script:Resolve-TrainerLaunchPlan {$script:claimAckEffects++;return @{program='fixture-never-executed.exe';arguments='';trainer=$null}}
            Set-Item Function:script:Assert-TrainerLaunchTarget {}
            Set-Item Function:script:Start-TrainerLaunchPlan {$script:claimAckEffects++;return @{pid=314;reused=$false}}
            Set-Item Function:script:Write-TrainerJson {param($Path,$Value) & $script:claimAckWriter $Path $Value;if($Path -eq $script:claimAckPath){throw 'fixture claim acknowledgement lost after commit'}}
            try {
                Expect-Rejection {Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId}
                Assert ((Read-TrainerJson $fixture.path).requestClaimed -eq $true -and $script:claimAckEffects -eq 0) 'Unacknowledged claim reached launch effects'
                Set-Item Function:script:Write-TrainerJson $originals['Write-TrainerJson']
                Expect-Rejection {Invoke-TrainerLaunch -Mode Manager -CacheRoot $fixture.root -ResultPath $fixture.path -RequestId $fixture.value.requestId}
                Assert ($script:claimAckEffects -eq 0) 'Lost-ack request replay dispatched'
                $next=Pending 'claim-ack-lost' 'claim-ack-lost-0002'
                Invoke-TrainerLaunch -Mode Manager -CacheRoot $next.root -ResultPath $next.path -RequestId $next.value.requestId
                Assert ($script:claimAckEffects -eq 2 -and (Read-TrainerJson $next.path).ok -eq $true) 'New explicit request after lost claim acknowledgement did not recover'
            }finally{foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}}
        }

        Check 'oversize nonobject owned cache archives exact bytes before safe edit' {
            $i=0
            foreach($raw in @([string](' '*(8388608+1)),('[0]'+(' '*8388608)))){
                $p=CasePath ('oversize-owned-'+$i+'/trainers.json');$i++
                [IO.File]::WriteAllText($p,$raw,$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
                Expect-Rejection {Read-TrainerJson $p}
                Write-TrainerJson $p @{safe=$true;extension=@{keep=$true}}
                $archives=@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($p)) -Filter 'trainers.json.corrupt-*.json' -File)
                Assert ($archives.Count -eq 1 -and (Get-FileHash -LiteralPath $archives[0].FullName -Algorithm SHA256).Hash -ceq $sha) 'Oversize damaged source was not archived exactly'
                Assert ((Read-TrainerJson $p).safe -eq $true -and (Read-TrainerJson $p).extension.keep -eq $true) 'Safe edit did not become readable'
            }
        }
        Check 'oversize object future object-containing roots ledger and foreign source remain protected' {
            $i=0
            foreach($item in @(@('trainers.json','{"schemaVersion":99}'),@('trainers.json','{"extension":{"keep":true}}'),@('trainers.json','[{"schemaVersion":99}]'),@('ymcc-library.json','[]'),@('ymcc-data-state.json','[]'),@('settings.json','[]'))){
                $p=CasePath ('oversize-protect-'+$i+'/'+$item[0]);$i++
                [IO.File]::WriteAllText($p,($item[1]+(' '*8388608)),$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
                Expect-Rejection {Read-TrainerJson $p};Expect-Rejection {Write-TrainerJson $p @{safe=$true}}
                Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Unclassified oversize or protected source changed'
                Assert (@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($p)) -File).Count -eq 1) 'Protected source created an archive/temp'
            }
        }
        Check 'oversize damage archive denial and stale read refuse without losing retry' {
            $p=CasePath 'oversize-retry/trainers.json';$raw=' '* (8388608+1);[IO.File]::WriteAllText($p,$raw,$utf8);$sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
            $original=(Get-Command Write-TrainerCorruptArchive).ScriptBlock
            Set-Item Function:script:Write-TrainerCorruptArchive {throw 'fixture oversize archive denied'}
            try{Expect-Rejection {Write-TrainerJson $p @{safe=$true}};Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Archive refusal replaced source'}finally{Set-Item Function:script:Write-TrainerCorruptArchive $original}
            Expect-Rejection {Read-TrainerJson $p};$next='[1]'+(' '*8388608);[IO.File]::WriteAllText($p,$next,$utf8)
            Expect-Rejection {Write-TrainerJson $p @{safe=$true}};Assert ([IO.File]::ReadAllText($p) -ceq $next) 'Late writer overwrote newer damaged document'
            Expect-Rejection {Read-TrainerJson $p};Write-TrainerJson $p @{safe=$true}
            Assert ((Read-TrainerJson $p).safe -eq $true) 'Explicit reread and save could not recover'
        }
        Check 'recovery ceiling invalid encoding and restricted read limits cannot become missing' {
            $p=CasePath 'oversize-ceiling/trainers.json';$stream=[IO.File]::Open($p,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try{$stream.SetLength(33554433)}finally{$stream.Dispose()}
            $sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;Expect-Rejection {Write-TrainerJson $p @{safe=$true}}
            Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Above-ceiling source changed'
            $p=CasePath 'oversize-encoding/trainers.json';$stream=[IO.File]::Open($p,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try{$stream.WriteByte(255);$padding=[Text.Encoding]::UTF8.GetBytes(' '*8388608);$stream.Write($padding,0,$padding.Length)}finally{$stream.Dispose()}
            $sha=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;Expect-Rejection {Read-TrainerJson $p};Expect-Rejection {Write-TrainerJson $p @{safe=$true}}
            Assert ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ceq $sha) 'Invalid encoding was overwritten'
            $p=CasePath 'restricted-read/trainers.json';[IO.File]::WriteAllText($p,(' '*65537),$utf8)
            $rejected=$false;try{Read-TrainerJson $p 65536|Out-Null}catch{$rejected=$_.Exception.Data['ymccTrainerJson'] -eq 'oversize'};Assert $rejected 'Restricted read limit was widened'
        }

        Check 'prepare final ledger IO failure releases both locks and permits explicit retry' {
            $fx=Pending 'finalize-locks' 'finalize-locks-0001'
            $library=CasePath 'finalize-locks-library';[IO.Directory]::CreateDirectory($library)|Out-Null
            $names=@('Resolve-TrainerIdentity','Get-FlingCatalogRows','Get-TrainerResponse','Write-TrainerJson');$originals=@{}
            foreach($name in $names){$originals[$name]=(Get-Command $name).ScriptBlock}
            $script:finalizeWriter=$originals['Write-TrainerJson'];$script:finalizeDeny=$true;$script:finalizeCalls=0
            Set-Item Function:script:Resolve-TrainerIdentity {return @{method='steam-appid';steamAppId='12345';aliases=@('fixture');manifestPath='owned inert manifest'}}
            Set-Item Function:script:Get-FlingCatalogRows {return @([pscustomobject]@{game_name='fixture';official_url='https://flingtrainer.com/trainer/fixture-trainer/'})}
            Set-Item Function:script:Get-TrainerResponse {$script:finalizeCalls++;throw 'fixture no-network port stops after persisted reservation'}
            Set-Item Function:script:Write-TrainerJson {
                param($Path,$Value)
                if($script:finalizeDeny -and [IO.Path]::GetFileName($Path) -eq 'ymcc-library.json' -and $Value.downloads.Count -gt 0 -and $Value.downloads[0].state -ne 'reserved'){throw 'fixture final ledger IO denied'}
                & $script:finalizeWriter $Path $Value
            }
            $mutexNames=@(('Local\YMCC.Trainer.'+(Get-TrainerHash $fx.root.ToLowerInvariant())),('Local\YMCC.GcmLibrary.'+(Get-TrainerHash $library.ToLowerInvariant())))
            $mutexNames=@($mutexNames|ForEach-Object{$_.Replace('\\','\')})
            # Keep only our named objects alive while observing ownership on a
            # different in-process runspace; no real worker/process is launched.
            $sentinels=@($mutexNames|ForEach-Object{[Threading.Mutex]::new($false,$_)})
            function Probe-FinalizeLock([string]$Name){
                $runspace=[PowerShell]::Create()
                [void]$runspace.AddScript('param($name);$m=[Threading.Mutex]::new($false,$name);$owned=$false;$state="timeout";try{try{$owned=$m.WaitOne(400);if($owned){$state="acquired"}}catch [Threading.AbandonedMutexException]{$owned=$true;$state="abandoned"};$state}finally{if($owned){$m.ReleaseMutex()};$m.Dispose()}').AddArgument($Name)
                try{return [string]@($runspace.Invoke())[-1]}finally{$runspace.Dispose()}
            }
            try{
                Expect-Rejection {Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $fx.root -ResultPath $fx.path -RequestId $fx.value.requestId -GcmSettingsRoot (CasePath 'finalize-settings') -GcmDefaultRoot $library -CustomLibraryRoot (CasePath 'finalize-custom') -Force}
                $doc=Read-TrainerJson $fx.path;Assert ($doc.state -eq 'failed' -and -not $doc.ok) 'Finalization error fabricated successful ready'
                $ledger=Read-TrainerJson (Join-Path $library 'ymcc-library.json');Assert ($ledger.downloads.Count -eq 1 -and $ledger.downloads[0].state -eq 'reserved' -and $ledger.downloads[0].chargedBytes -gt 0) 'Failed finalization erased pessimistic charge'
                $states=@($mutexNames|ForEach-Object{Probe-FinalizeLock $_})
                [IO.File]::WriteAllText((CasePath 'finalize-lock-observation.json'),(ConvertTo-Json -InputObject @{names=$mutexNames;states=$states;networkPortCalls=$script:finalizeCalls;afterFailedReceipt=$doc.state} -Depth 7),$utf8)
                Assert ($states[0] -ceq 'acquired' -and $states[1] -ceq 'acquired') ('Finalization left owned/abandoned locks: '+($states -join ','))
                $script:finalizeDeny=$false;$next=Pending 'finalize-locks' 'finalize-locks-0002'
                Invoke-TrainerPrepare -GameName 'fixture' -CacheRoot $next.root -ResultPath $next.path -RequestId $next.value.requestId -GcmSettingsRoot (CasePath 'finalize-settings') -GcmDefaultRoot $library -CustomLibraryRoot (CasePath 'finalize-custom') -Force
                $doc=Read-TrainerJson $next.path;Assert ($doc.state -eq 'failed' -and $script:finalizeCalls -eq 2) 'Explicit request could not reach its inert port after IO restored'
                $states=@($mutexNames|ForEach-Object{Probe-FinalizeLock $_});Assert (@($states|Where-Object{$_ -cne 'acquired'}).Count -eq 0) 'Explicit retry retained a lock'
            }finally{
                foreach($name in $names){Set-Item ('Function:script:'+$name) $originals[$name]}
                foreach($mutex in $sentinels){try{$mutex.ReleaseMutex()}catch{};$mutex.Dispose()}
            }
        }

        $failed=@($script:checks|Where-Object {$_.status -eq 'FAIL'})
        $dependencies=@('PowerControl/Trainer/TrainerBackend.psm1','PowerControl/Trainer/TrainerGcmFallback.ps1','PowerControl/Trainer/TrainerCatalog.ps1','PowerControl/Trainer/TrainerIdentity.ps1','PowerControl/Trainer/TrainerLaunchPaths.ps1','PowerControl/Trainer/TrainerLaunch.ps1','PowerControl/Trainer/TrainerCustomLibrary.ps1','PowerControl/Trainer/TrainerProcessIdentity.ps1','PowerControl/Trainer/TrainerForeground.ps1','PowerControl/GameTitlePolicy.ps1','PowerControl/GameTitlePolicy.json','tools/game_trainer_backend_selftest.ps1','tools/fixtures/trainer_ready_tmnt_20261022.json') | ForEach-Object {
            $path=Join-Path $Repo $_; @{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
        }
        $drift=@($InputPins|Where-Object{(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash -cne $_.sha256})
        if($drift.Count){throw 'Persistence input drift during execution'}
        $report=@{status=if($failed.Count){'FAIL'}else{'PASS'};passed=$script:checks.Count-$failed.Count;failed=$failed.Count;checks=@($script:checks.ToArray());source=$dependencies;sourceBefore=$InputPins;sourceDrift=$drift;startedAt=$StartedAt.ToString('o');finishedAt=[DateTimeOffset]::UtcNow.ToString('o');durationMs=([DateTimeOffset]::UtcNow-$StartedAt).TotalMilliseconds;runtimeSha256=(Get-FileHash -LiteralPath ([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName) -Algorithm SHA256).Hash;powerShellVersion=$PSVersionTable.PSVersion.ToString();processPath=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName;pshome=$PSHOME;scope='Original backend selftest persistence selection; actual imported writer functions, owned paths only';network='none';executedTrainers=0;parent57Rerun=$false;recovery26Rerun=$false;lockObservedMs=$script:lockObservedMs}
        [IO.File]::WriteAllText((Join-Path $scratch 'result.json'),(ConvertTo-Json -InputObject $report -Depth 12),$utf8)
        Write-Output ('Trainer persistence: '+$report.passed+'/'+$script:checks.Count+'; '+$report.status)
    } $ValidationRoot $sourceRoot $persistenceBefore $persistenceStartedAt
    $result=[IO.File]::ReadAllText((Join-Path $ValidationRoot 'result.json'),[Text.Encoding]::UTF8) | ConvertFrom-Json
    if($result.failed){exit 1}
    exit 0
}

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