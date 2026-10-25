[CmdletBinding()]
param(
 [string]$InstallDirectory='C:\SOFT\YeMan\YeManCC',
 [string]$DataDirectory='',
 [string[]]$PowerControlDirectory=@(),
 [string]$OutputDirectory=$PSScriptRoot
)
$ErrorActionPreference='Stop'
function Read-DiagnosticJson([string]$Path,[long]$Limit=8388608){
 try{
  if(-not [IO.File]::Exists($Path)){return @{available=$false;path=$Path}}
  if((Get-Item -LiteralPath $Path).Length -gt $Limit){return @{available=$false;path=$Path;error='Size limit exceeded'}}
  $data=[IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8)|ConvertFrom-Json
  if($null -eq $data){return @{available=$false;path=$Path;error='Empty or null JSON'}}
  return $data
 }catch{return @{available=$false;path=$Path;error='Unreadable or incomplete JSON'}}
}
function Get-DiagnosticHash([string]$Path){try{if([IO.File]::Exists($Path)){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}}catch{};return ''}
function Property($Value,[string]$Name,$Default=$null){if($Value -is [Collections.IDictionary]){if($Value.Contains($Name)){return $Value[$Name]}}elseif($Value){$p=$Value.PSObject.Properties[$Name];if($p){return $p.Value}};return $Default}
function Process-Diagnostic([string]$Name){
 return @(Get-Process -Name $Name -ErrorAction SilentlyContinue|ForEach-Object{
  $p=$_;$path='';$created='';$title=''
  try{$path=$p.Path}catch{};try{$created=[string]$p.StartTime.ToUniversalTime().ToFileTimeUtc()}catch{};try{$title=$p.MainWindowTitle}catch{}
  @{pid=$p.Id;path=$path;processCreated=$created;windowTitle=$title}
 })
}
$report=[ordered]@{schema=2;at=[DateTimeOffset]::Now.ToString('o');mode='read-only';powershellVersion=$PSVersionTable.PSVersion.ToString();target='';targetStoreId='';realLaunches=0;downloadsPerformed=0}
$expected=Read-DiagnosticJson (Join-Path $PSScriptRoot 'trainer-diagnostics-expected.json') 65536
if(-not $DataDirectory){$DataDirectory=$env:YEMAN_WEBVIEW_DATA_DIR;if(-not $DataDirectory){$DataDirectory=[Environment]::GetEnvironmentVariable('YEMAN_WEBVIEW_DATA_DIR','User')};if(-not $DataDirectory){$DataDirectory=Join-Path $env:LOCALAPPDATA 'YeManCC'}}
if(-not [IO.Path]::IsPathRooted($DataDirectory)){$DataDirectory=Join-Path $InstallDirectory $DataDirectory}
$report.dataDirectoryCandidate=$DataDirectory
$cache=Join-Path $DataDirectory 'trainer-cache'
foreach($name in @('last-game-status.json','last-launch-status.json','trainers.json')){$report[$name]=Read-DiagnosticJson (Join-Path $cache $name)}
$roots=@($PowerControlDirectory)+@((Join-Path (Split-Path $InstallDirectory) 'PowerControl'),$env:YEMAN_POWER_CONTROL_DIR,[Environment]::GetEnvironmentVariable('YEMAN_POWER_CONTROL_DIR','User'))
$statusIdentity=Property $report['last-game-status.json'] 'identity'
$report.target=[string](Property $report['last-game-status.json'] 'gameName' '')
$report.targetStoreId=[string](Property $statusIdentity 'steamAppId' '')
$launchStatus=$report['last-launch-status.json'];$launchResult=Property $launchStatus 'result'
$report.launchProcessObservations=@(foreach($kind in @('game','trainer')){
 $requestedId=if($kind -eq 'game'){Property $launchStatus 'gameProcessId' 0}else{Property $launchResult 'pid' 0}
 $observed=$null;$numericId=0
 if([int]::TryParse([string]$requestedId,[ref]$numericId) -and $numericId -gt 0){
  $process=Get-Process -Id $numericId -ErrorAction SilentlyContinue
  if($process){$image='';$created='';$title='';try{$image=$process.Path}catch{};try{$created=[string]$process.StartTime.ToUniversalTime().ToFileTimeUtc()}catch{};try{$title=$process.MainWindowTitle}catch{};$observed=@{pid=$process.Id;path=$image;processCreated=$created;windowTitle=$title}}
 }
 @{kind=$kind;requestedPid=$requestedId;observation=$observed}
})
$observedSnapshot=[string](Property $statusIdentity 'snapshotPath' '')
if($observedSnapshot -match '^[a-zA-Z]:[\\/]' -and [IO.Path]::GetFileName($observedSnapshot) -ieq 'game-target.json'){$roots+=@([IO.Path]::GetDirectoryName($observedSnapshot))}
$roots=@($roots|Where-Object{$_ -and $_ -match '^[a-zA-Z]:[\\/]'}|ForEach-Object{[IO.Path]::GetFullPath($_).TrimEnd('\')}|Select-Object -Unique|Select-Object -First 4)
$modulePaths=@('YeManTrainerPrepare.ps1','YeManTrainerLaunch.ps1','GameTitlePolicy.json','GameTitlePolicy.ps1','Trainer/TrainerBackend.psm1','Trainer/TrainerProcessIdentity.ps1','Trainer/TrainerIdentity.ps1','Trainer/TrainerLaunch.ps1','Trainer/TrainerForeground.ps1','Trainer/TrainerLaunchPaths.ps1','Trainer/TrainerGcmFallback.ps1')
$report.sharedGameTargets=@(foreach($directory in $roots){
 $snapshot=Read-DiagnosticJson (Join-Path $directory 'game-target.json') 65536
 $lastSeen=Property $snapshot 'lastSeen';$age=$null;try{if($null -ne $lastSeen){$age=[DateTimeOffset]::Now.ToUnixTimeMilliseconds()-[long]$lastSeen}}catch{}
 $files=@(foreach($relative in $modulePaths){$sha=Get-DiagnosticHash (Join-Path $directory $relative);$wanted=[string](Property (Property $expected 'powerControl') $relative '');@{path=$relative;sha256=$sha;expectedSha256=$wanted;matchesExpected=($sha -and $wanted -and $sha -ceq $wanted)}})
 @{root=$directory;snapshot=$snapshot;ageMs=$age;files=$files}
})
$indexHash=Get-DiagnosticHash (Join-Path $InstallDirectory 'index.html');$expectedIndex=[string](Property $expected 'indexSha256' '')
$report.installation=@{installDirectory=$InstallDirectory;indexSha256=$indexHash;expectedIndexSha256=$expectedIndex;indexMatchesExpected=($indexHash -and $expectedIndex -and $indexHash -ceq $expectedIndex);nativeSha256=(Get-DiagnosticHash (Join-Path $InstallDirectory 'YeManCC.exe'))}
$settingsPath=Join-Path $env:APPDATA 'GCM Settings/settings.json';$settings=Read-DiagnosticJson $settingsPath 65536
$library=[string](Property $settings 'downloadPath' '')
if(-not $library -or -not [IO.Path]::IsPathRooted($library)){$library=Join-Path $env:APPDATA 'GCM Trainers'}
$report.gcm=@{downloadPath=$library;enableGCM=(Property $settings 'enableGCM');cePath=(Property $settings 'cePath');launchCTAsAdmin=(Property $settings 'launchCTAsAdmin');safePath=(Property $settings 'safePath');launchAsAdmin=(Property $settings 'launchAsAdmin')}
$report.library=Read-DiagnosticJson (Join-Path $library 'ymcc-library.json')
$report.tmntProcesses=@(Process-Diagnostic 'TMNT');$report.appProcesses=@(Process-Diagnostic 'YeManCC')
$report.frontend=if([IO.File]::Exists((Join-Path $InstallDirectory 'index.html'))){[IO.File]::ReadAllText((Join-Path $InstallDirectory 'index.html'),[Text.Encoding]::UTF8)}else{''}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$destination=Join-Path $OutputDirectory ('Trainer-Diagnostics-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
[IO.File]::WriteAllText($destination,($report|ConvertTo-Json -Depth 28),[Text.UTF8Encoding]::new($false))
Write-Output $destination
