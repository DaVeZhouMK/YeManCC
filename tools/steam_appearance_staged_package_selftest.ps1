param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$fixturePack=Join-Path $Out 'package'
New-Item -ItemType Directory -Path $fixturePack|Out-Null
Copy-Item -LiteralPath (Join-Path $script:PackRoot 'manifest.json') -Destination $fixturePack
Copy-Item -LiteralPath (Join-Path $script:PackRoot 'themes') -Destination $fixturePack -Recurse
$script:PackRoot=$fixturePack;$script:AppearanceContentManifestFile=Join-Path $fixturePack 'manifest.json'
function Get-AppearanceSteamSession {$null}
function Get-BackendProcesses {@()}
function Test-BackendOwner {$false}
function Test-CssDesktopEditing {$false}
function Get-FixtureTree([string]$Path){
 if(!(Test-Path -LiteralPath $Path)){return ''}
 @(Get-ChildItem -LiteralPath $Path -Recurse -Force|Sort-Object FullName|ForEach-Object {
  $relative=$_.FullName.Substring($Path.Length)
  if($_.PSIsContainer){'d '+$relative}else{'f '+$relative+' '+(Get-ConfigHash $_.FullName)}
 }) -join "`n"
}
$readForeign=${function:Get-ForeignThemeRows}
$script:LatePackagePath='';$script:LatePackageText='';$script:LatePackageChanges=0
function Get-ForeignThemeRows($BackendThemes=@()){
 if($script:LatePackagePath){
  [IO.File]::WriteAllText($script:LatePackagePath,$script:LatePackageText)
  $script:LatePackagePath='';$script:LatePackageChanges++
 }
 & $readForeign $BackendThemes
}
$observed=foreach($mode in 'takeover','reset','owned-repair'){
 $script:ThemeRoot=Join-Path $Out $mode
 $foreign=Join-Path $script:ThemeRoot 'Borrowed'
 New-Item -ItemType Directory -Path $foreign|Out-Null
 [IO.File]::WriteAllText((Join-Path $foreign 'theme.json'),'{"name":"Borrowed","manifest_version":1}')
 [IO.File]::WriteAllText((Join-Path $foreign 'config_USER.json'),'{"active":false,"Tone":"original"}')
 $configPath=Join-Path $script:ThemeRoot 'Round\config_USER.json';$cssPath=Join-Path $script:ThemeRoot 'Round\shared.css'
 if($mode -ne 'takeover'){
  $null=Invoke-PresetPrepare;Complete-PresetReload
  $saved=[IO.File]::ReadAllText($configPath)|ConvertFrom-Json;$saved.active=$false;$saved.Intensity='30px'
  Write-ConfigAtomic $configPath ($saved|ConvertTo-Json -Compress)
  if($mode -eq 'owned-repair'){[IO.File]::WriteAllText($cssPath,'/* user asset before repair */')}
 }
 $stateDir=Get-PresetStateDir
 $beforeThemes=Get-FixtureTree $script:ThemeRoot;$beforeState=Get-FixtureTree $stateDir
 $beforeStatus=Get-AppearanceStatus
 $file=Join-Path $fixturePack ('themes\Round\'+$(if($mode -eq 'owned-repair'){'shared.css'}else{'config_USER.json'}))
 $original=[IO.File]::ReadAllBytes($file)
 $script:LatePackagePath=$file
 $script:LatePackageText=if($mode -eq 'owned-repair'){'/* replaced after validation */'}else{'{"active":false,"Intensity":"900px"}'}
 $changesBefore=$script:LatePackageChanges;$result=$null;$reason=''
 try {
  $result=Invoke-PresetPrepare ($mode -eq 'reset') $beforeStatus.revision -OwnedResourcesOnly:($mode -eq 'owned-repair') -ProtectEditor
 }catch{$reason=$_.Exception.Message}finally{
  $script:LatePackagePath='';[IO.File]::WriteAllBytes($file,$original)
 }
 $stagedReplacement=if($mode -eq 'owned-repair'){$cssPath}else{$configPath}
 [pscustomobject]@{mode=$mode;accepted=[bool]($result -and $result.ok);reason=$reason;injected=($script:LatePackageChanges-$changesBefore);copiedReplacement=((Test-Path -LiteralPath $stagedReplacement -PathType Leaf) -and [IO.File]::ReadAllText($stagedReplacement) -ceq $script:LatePackageText);sameThemeTree=((Get-FixtureTree $script:ThemeRoot) -ceq $beforeThemes);sameStateTree=((Get-FixtureTree $stateDir) -ceq $beforeState);journalAbsent=(!(Test-Path -LiteralPath (Join-Path $stateDir 'transaction.json')))}
}
$observed|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'staged-package-observations.json') -Encoding UTF8
$cases=[Collections.Generic.List[string]]::new()
function Assert($value,[string]$message){if(!$value){throw $message};$cases.Add($message)}
foreach($row in $observed){
 Assert (!$row.accepted -and $row.injected -eq 1 -and !$row.copiedReplacement -and $row.reason -like '*Bundled preset changed before staging*') ('Reject changed package bytes: '+($row|ConvertTo-Json -Compress))
 Assert ($row.sameThemeTree -and $row.sameStateTree -and $row.journalAbsent) ('No original, backup, or journal change after rejection: '+$row.mode)
}
$repaired=Invoke-PresetPrepare $false (Get-AppearanceStatus).revision -OwnedResourcesOnly -ProtectEditor
$personal=[IO.File]::ReadAllText($configPath)|ConvertFrom-Json
Assert ($repaired.ok -and $repaired.snapshot.packPrepared -and !$personal.active -and $personal.Intensity -eq '30px') 'Verified package repairs resources without overwriting personal settings'
$reset=Invoke-PresetPrepare $true -ProtectEditor
Assert ($reset.ok -and @($reset.snapshot.themes|Where-Object {$_.installed -and $_.configured}).Count -eq 7) 'Verified defaults still restore all seven themes'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;installedApplicationModified=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output ('PASS: '+$cases.Count+' staged package checks')
