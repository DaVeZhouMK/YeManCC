[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Worker, [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\Build'))
if(-not $OutputDirectory){$OutputDirectory=Join-Path $buildRoot ('CustomSteamLibrary\cli-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $OutputDirectory.StartsWith($buildRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $OutputDirectory)){throw 'CLI fixtures must be a fresh directory in workspace Build'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Worker=[IO.Path]::GetFullPath($Worker)
$reports=[Collections.Generic.List[object]]::new()
$oldData=$env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT
function JsonFile($Path,$Value){New-Item -ItemType Directory -Path (Split-Path $Path) -Force | Out-Null;[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
function Assert($Value,$Text){if(-not $Value){throw $Text}}
function Scan($Case,$Root,[switch]$FirstRun){$env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $Case 'data';$trainer=Join-Path $Case 'trainers';New-Item -ItemType Directory -Path $trainer -Force|Out-Null;$workerArgs=@('--scan-library',(Join-Path $Case 'jobs'),'--root',$Root,'--trainer-root',$trainer,'--max-depth','2');if($FirstRun){$workerArgs+='--first-run-defaults'};& $Worker @workerArgs > (Join-Path $Case 'worker.log');if($LASTEXITCODE -ne 0){throw "Worker scan exit $LASTEXITCODE"};return (Get-Content -LiteralPath (Join-Path $Case 'data\state\library-scan.json') -Raw -Encoding UTF8|ConvertFrom-Json)}
function Setup($Case,$Root,$OldGame=''){$config=@{schemaVersion=1;roots=@($Root);trainerRoots=@();manualPrimary=@{};manualBuckets=@{};manualGameDirectories=@();excludedGameDirectories=@();firstRunDefaultScan=@{status='skipped-existing-roots'}};JsonFile (Join-Path $Case 'data\config\library-config.json') $config;if($OldGame){JsonFile (Join-Path $Case 'data\state\library-scan.json') @{games=@(@{gameDirectory=$OldGame;primaryExecutable=(Join-Path $OldGame 'Game.exe');status='ready'});missingGames=@();unscannedGames=@()}}}
function Run($Name,$Action){$case=Join-Path $OutputDirectory ('c'+$reports.Count);New-Item -ItemType Directory -Path $case|Out-Null;try{&$Action $case;$reports.Add(@{name=$Name;passed=$true})}catch{$reports.Add(@{name=$Name;passed=$false;error=$_.Exception.Message})}}
try{
Run 'healthy-empty-scan-preserves-explicit-root' {param($p)$root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root;$scan=Scan $p $root;$config=Get-Content (Join-Path $p 'data\config\library-config.json') -Raw|ConvertFrom-Json;Assert (@($config.roots).Count -eq 1 -and $config.roots[0] -eq $root) 'Empty scan removed user root';Assert (@($scan.roots).Count -eq 1) 'Empty scan lost root evidence'}
Run 'offline-scan-retains-game-without-missing-transition' {param($p)$root=Join-Path $p 'Offline';Setup $p $root (Join-Path $root 'Game');$scan=Scan $p $root;Assert (@($scan.unscannedGames).Count -eq 1) 'Offline game not retained';Assert (@($scan.missingGames).Count -eq 0) 'Offline game marked missing';Assert ($scan.roots[0].status -eq 'root-unavailable') 'Offline root not diagnosed';$config=Get-Content (Join-Path $p 'data\config\library-config.json') -Raw|ConvertFrom-Json;Assert (@($config.roots).Count -eq 1) 'Offline root removed'}
Run 'online-empty-scan-detects-real-deletion' {param($p)$root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root (Join-Path $root 'Deleted');$scan=Scan $p $root;Assert (@($scan.missingGames).Count -eq 1) 'Real deletion not recorded';Assert ($scan.missingGames[0].lifecycleStatus -eq 'game-directory-missing') 'Wrong deletion transition'}
Run 'partial-scan-retains-outside-game-data' {param($p)$root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root (Join-Path $p 'Other\Game');$scan=Scan $p $root;Assert (@($scan.unscannedGames).Count -eq 1 -and @($scan.missingGames).Count -eq 0) 'Partial scan falsely deleted unrelated game'}
Run 'malformed-scan-inventory-recovers-backup-in-cli' {param($p)$root=Join-Path $p 'Offline';Setup $p $root (Join-Path $root 'Game');$state=Join-Path $p 'data\state\library-scan.json';Copy-Item -LiteralPath $state -Destination ($state+'.bak');JsonFile $state @{games=7};$scan=Scan $p $root;Assert (@($scan.unscannedGames).Count -eq 1) 'CLI failed to recover valid lifecycle backup'}
Run 'first-run-offline-root-stays-retryable' {param($p)$root=Join-Path $p 'Offline';Setup $p $root;$configPath=Join-Path $p 'data\config\library-config.json';$config=Get-Content $configPath -Raw|ConvertFrom-Json;$config.firstRunDefaultScan.status='running';JsonFile $configPath $config;$scan=Scan $p $root -FirstRun;$after=Get-Content $configPath -Raw|ConvertFrom-Json;Assert ($after.firstRunDefaultScan.status -eq 'running') 'Incomplete initial scan marked completed';Assert (@($after.roots).Count -eq 1) 'Initial offline root removed'}

Run 'empty-config-first-run-scan-does-not-emit-missing-root-error' {param($p)
  $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
  New-Item -ItemType Directory -Path (Join-Path $p 'trainers') -Force | Out-Null
  $log=Join-Path $p 'first-run.log';$ErrorActionPreference='Continue'
  & $Worker '--scan-library' (Join-Path $p 'jobs') '--first-run-defaults' '--trainer-root' (Join-Path $p 'trainers') '--max-depth' '1' *> $log
  $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
  Assert ($code -eq 0) 'First-run scan with no configured roots failed'
  Assert (-not ((Get-Content -LiteralPath $log -Raw) -match 'No library roots are configured')) 'Internal missing-root error leaked to first-run path'
}
foreach($legacyStatus in @('completed','skipped-existing-roots')) {
  foreach($withFirstRunFlag in @($false,$true)) {
    $name='empty-config-'+$legacyStatus+'-retries-discovery-'+$(if($withFirstRunFlag){'with-flag'}else{'direct'})
    $statusForCase=$legacyStatus;$flagForCase=$withFirstRunFlag
    Run $name {param($p)
      $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
      $configPath=Join-Path $p 'data\config\library-config.json'
      JsonFile $configPath @{schemaVersion=1;roots=@();trainerRoots=@();firstRunDefaultScan=@{status=$statusForCase}}
      $trainer=Join-Path $p 'trainers';New-Item -ItemType Directory -Path $trainer | Out-Null
      $argsForCase=@('--scan-library',(Join-Path $p 'jobs'),'--trainer-root',$trainer,'--max-depth','1')
      if($flagForCase){$argsForCase+='--first-run-defaults'}
      $log=Join-Path $p 'scan.log';$ErrorActionPreference='Continue'
      & $Worker @argsForCase *> $log
      $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
      Assert ($code -eq 0) ('Empty roots with legacy status '+$statusForCase+' failed')
      Assert (-not ((Get-Content -LiteralPath $log -Raw) -match 'No library roots are configured')) 'Legacy missing-root error leaked'
      Assert (Test-Path -LiteralPath (Join-Path $p 'data\state\library-scan.json')) 'Retry did not publish valid scan state'
    }
  }
}
Run 'builtin-steam-tool-old-cache-does-not-create-lifecycle-ghost' {param($p)
  $root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root
  $row=@{gameDirectory=(Join-Path $p 'OldTool');primaryExecutable=(Join-Path $p 'OldTool\tool.exe');status='already-in-steam';nativeSteamAppId=228980}
  JsonFile (Join-Path $p 'data\state\library-scan.json') @{games=@($row);missingGames=@($row);unscannedGames=@($row)}
  $scan=Scan $p $root
  foreach($section in @('games','missingGames','unscannedGames')){Assert (@($scan.$section).Count -eq 0) ('Tool survived in '+$section)}
}
Run 'builtin-steam-tool-direct-id-rejects-before-save-override' {param($p)
  $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
  $exe=Join-Path $p 'Game.exe';[IO.File]::WriteAllText($exe,'fixture, never executed')
  $log=Join-Path $p 'rejected.log';$ErrorActionPreference='Continue'
  & $Worker $exe (Join-Path $p 'jobs') '--steam-id' '228980' '--save-id' '--metadata-only' *> $log
  $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
  Assert ($code -ne 0) 'CLI accepted excluded tool ID'
  Assert ((Get-Content -LiteralPath $log -Raw) -match 'built-in excluded Steam tool') 'CLI rejected for unrelated reason'
  Assert (-not (Test-Path -LiteralPath (Join-Path $p 'data\config\manual-overrides.json'))) 'Excluded ID was persisted'
}
Run 'builtin-steam-tool-artwork-search-is-blocked' {param($p)
  $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
  $log=Join-Path $p 'rejected.log';$ErrorActionPreference='Continue'
  & $Worker '--search-artwork' 'Steamworks Common Redistributables' '--type' 'cover' *> $log
  $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
  Assert ($code -ne 0) 'CLI searched artwork for excluded tool'
  Assert ((Get-Content -LiteralPath $log -Raw) -match 'built-in excluded Steam tool') 'CLI rejected for unrelated reason'
}
Run 'clear-last-root-persists-empty-and-does-not-rediscover' {param($p)
  $root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root
  $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
  $trainer=Join-Path $p 'trainers';New-Item -ItemType Directory -Path $trainer|Out-Null
  $configPath=Join-Path $p 'data\config\library-config.json'
  & $Worker '--scan-library' (Join-Path $p 'jobs') '--clear-roots' '--trainer-root' $trainer *> (Join-Path $p 'clear.log')
  Assert ($LASTEXITCODE -eq 0) 'Clearing the last root failed'
  $config=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json
  Assert (@($config.roots).Count -eq 0 -and $config.scanRootsExplicitlyCleared) 'Empty-root choice was not persisted'
  foreach($withDefaults in @($false,$true)) {
    $scanArgs=@('--scan-library',(Join-Path $p 'refresh'),'--trainer-root',$trainer)
    if($withDefaults){$scanArgs+='--first-run-defaults'}
    & $Worker @scanArgs *> (Join-Path $p 'refresh.log')
    Assert ($LASTEXITCODE -eq 0) 'Refreshing intentionally empty roots failed'
    $after=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json
    Assert (@($after.roots).Count -eq 0 -and $after.scanRootsExplicitlyCleared) 'Refresh rediscovered removed roots'
    $state=Get-Content -LiteralPath (Join-Path $p 'data\state\library-scan.json') -Raw|ConvertFrom-Json
    Assert (@($state.roots).Count -eq 0) 'Removed root stayed in scan scope'
  }
  & $Worker '--scan-library' (Join-Path $p 'add') '--root' $root '--save-roots' '--trainer-root' $trainer *> (Join-Path $p 'add.log')
  Assert ($LASTEXITCODE -eq 0) 'Adding a root after clearing failed'
  $after=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json
  Assert (@($after.roots).Count -eq 1 -and -not $after.scanRootsExplicitlyCleared) 'Adding a root did not restore scanning'
}
Run 'clear-roots-conflicting-explicit-root-is-rejected-without-config-loss' {param($p)
  $root=Join-Path $p 'Games';New-Item -ItemType Directory -Path $root|Out-Null;Setup $p $root
  $env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=Join-Path $p 'data'
  $configPath=Join-Path $p 'data\config\library-config.json';$before=[IO.File]::ReadAllText($configPath)
  $ErrorActionPreference='Continue'
  & $Worker '--scan-library' (Join-Path $p 'jobs') '--clear-roots' '--root' $root *> (Join-Path $p 'rejected.log')
  $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
  Assert ($code -ne 0) 'Conflicting scan scope was accepted'
  Assert ([IO.File]::ReadAllText($configPath) -eq $before) 'Rejected request modified configuration'
}
$failed=@($reports|Where-Object{-not $_.passed});$summary=@{allPassed=$failed.Count -eq 0;caseCount=$reports.Count;cases=@($reports.ToArray());fixtures=$OutputDirectory;realSteamFilesModified=$false;steamStoppedOrLaunched=$false};JsonFile (Join-Path $OutputDirectory 'summary.json') $summary;$summary|ConvertTo-Json -Depth 20;if($failed.Count){throw 'CLI datachain regression failed'}
# Expected-rejection cases intentionally leave the child exit code nonzero.
# A successful fixture script must not propagate that as its own result.
$global:LASTEXITCODE=0
}finally{$env:YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=$oldData}
