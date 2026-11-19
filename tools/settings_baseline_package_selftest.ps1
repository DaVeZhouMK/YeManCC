[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[switch]$FourModeCopyOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
. (Join-Path $PSScriptRoot 'settings-baseline-contract.ps1')
$contract=Read-YmccSettingsBaselineContract $project
$results=[Collections.Generic.List[object]]::new()
$packageAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'package-release.ps1'),[ref]$null,[ref]$null)
$filter=$packageAst.Find({param($ast)$ast -is [Management.Automation.Language.FunctionDefinitionAst] -and $ast.Name -eq 'Test-IsExcludedPowerControlPath'},$true)
if(!$filter){throw 'Production package filter missing'}
. ([scriptblock]::Create($filter.Extent.Text))
$settingsBaselineContract=$contract
function Need([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Case([string]$Name,[scriptblock]$Test){try{& $Test;$results.Add(@{name=$Name;passed=$true})}catch{$results.Add(@{name=$Name;passed=$false;error=$_.Exception.Message})}}
function New-Stage([string]$Name){
  $stage=Join-Path $evidence $Name
  Need (!$stage.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)) 'Fixture cannot write inside source'
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  foreach($name in @('settings-baseline.json',[string]$contract.defaultsFile)){Copy-Item -LiteralPath (Join-Path $project ('PowerControl\'+$name)) -Destination (Join-Path $stage $name)}
  return $stage
}
function Put([string]$Stage,[string]$Name,[string]$Text){$target=Join-Path $Stage $Name;New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null;[IO.File]::WriteAllText($target,$Text,[Text.UTF8Encoding]::new($false))}
if($FourModeCopyOnly){
 $packFile=Join-Path $PSScriptRoot 'package-release.ps1';$deployFile=Join-Path $PSScriptRoot 'deploy-installed.ps1'
 $definitions=@(@{path=$packFile;names=@('Get-FullPath','Get-RelativePath','Copy-PowerControlTemplates')},@{path=$deployFile;names=@('Test-InstalledPowerControlExcluded','Copy-InstalledPowerControlMainAssets')})
 foreach($definition in $definitions){
  $ast=[Management.Automation.Language.Parser]::ParseFile($definition.path,[ref]$null,[ref]$null)
  foreach($name in $definition.names){$matches=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true));Need ($matches.Count -eq 1) ('Expected original copier '+$name);. ([scriptblock]::Create($matches[0].Extent.Text))}
 }
 $ProjectRoot=$project;$script:installedSettingsBaseline=$contract
 foreach($consumer in @('package','installed-assets')){foreach($mode in @('global','xbox','steam','xbox-steam')){
  Case ('actual '+$consumer+' copy preserves four-mode/global/dedicated configuration: '+$mode) {
   $caseRoot=Join-Path $evidence ($consumer+'-'+$mode);New-Item -ItemType Directory -Path $caseRoot|Out-Null
   $incoming=New-Stage ($consumer+'-'+$mode+'-incoming');$destination=Join-Path $caseRoot 'owned-target';New-Item -ItemType Directory -Path $destination|Out-Null
   $document=Get-Content -LiteralPath (Join-Path $project ('PowerControl/'+$contract.defaultsFile)) -Raw -Encoding UTF8|ConvertFrom-Json
   $document.startupDesired|Add-Member -NotePropertyName steamOverlayOnBoot -NotePropertyValue $false -Force
   $document.startupDesired|Add-Member -NotePropertyName virtualGamepadPersona -NotePropertyValue 'steamdeck' -Force
   $module=@{schema=1;revision=17;current=$mode;boot=$mode;bootEnabled=$true;global=@{background=@{ui=@{backgroundOpacity=41}};input=@{persona='steamdeck'}};scenes=@{xbox=@{name='Xbox';overrides=@{monitor=@{preferences=@{enabled=$true}}};rememberedOverrides=@{fan=@{preset='aggressive'}}};steam=@{name='Steam';overrides=@{}};'xbox-steam'=@{name='组合';overrides=@{}}};presets=@{steam=@{myPreset=@{name='保存的独立模板';overrides=@{fan=@{preset='soft'}}}}};opaqueFuture=@{retain='配置未知字段'}}
   $document.extensions|Add-Member -NotePropertyName bootModes -NotePropertyValue $module -Force
   $document.extensions|Add-Member -NotePropertyName bootModesStartupResult -NotePropertyValue @{schema=1;boot='owned-boot';mode=$mode;sourceRevision=16;revision=17;status='partial';applied=@();skipped=@();pending=@('监控待确认');modules=@{monitor=@{state='waiting'}}} -Force
   $document|Add-Member -NotePropertyName gameProfiles -NotePropertyValue @{'C:/Games/超长中文游戏.exe'=@{name=('专属游戏名称允许超长中文'*12);keep=$true}} -Force
   $owned=@{'yeman-settings.json'=($document|ConvertTo-Json -Depth 48);'yeman-settings.json.bak'='original opaque backup';'game-custom.json'='{"version":1,"entries":{"game.exe":{"enabled":true,"padPersona":"dualsense-edge","gyroOverride":"racing","future":17}},"future":"keep"}';'game-custom.json.bak'='dedicated backup';'ui-background/dynamic.json'='{"opacity":41,"future":"keep"}';'boot_config.json'='old once-imported startup record'}
   $before=@{};foreach($name in $owned.Keys){Put $destination $name $owned[$name];Put $incoming $name 'source user-state must never replace destination';$before[$name]=(Get-FileHash -LiteralPath (Join-Path $destination $name) -Algorithm SHA256).Hash}
   Put $incoming 'configuration-copy-resource.bin' 'new immutable resource';Put $destination 'configuration-copy-resource.bin' 'old immutable resource'
   if($consumer -eq 'package'){Copy-PowerControlTemplates $incoming $destination}else{Copy-InstalledPowerControlMainAssets $incoming $destination}
   foreach($name in $owned.Keys){Need ((Get-FileHash -LiteralPath (Join-Path $destination $name) -Algorithm SHA256).Hash -ceq $before[$name]) ('Original user bytes changed: '+$name)}
   Need (([IO.File]::ReadAllText((Join-Path $destination 'configuration-copy-resource.bin'))) -ceq 'new immutable resource') 'Managed resource was not updated'
   foreach($name in @('settings-baseline.json',[string]$contract.defaultsFile)){Need ((Get-FileHash -LiteralPath (Join-Path $destination $name) -Algorithm SHA256).Hash -ceq (Get-FileHash -LiteralPath (Join-Path $incoming $name) -Algorithm SHA256).Hash) 'Immutable defaults not paired'}
  }
 }}
}else{
Case 'valid staged baseline produces a hash-bound updater rule' {
 $stage=New-Stage 'valid';$binding=Assert-YmccSettingsBaseline $project $stage
 Need ($binding.userData -eq 'preserve' -and $binding.revision -eq 1) 'Wrong updater rule'
 Need ($binding.manifestSha256 -eq (Get-FileHash -LiteralPath (Join-Path $stage 'settings-baseline.json')).Hash) 'Binding mismatch'
}
$mutable=@([string]$contract.document.path,[string]$contract.document.backup)+@($contract.legacyImports|ForEach-Object{[string]$_.path})+@($contract.independentUserFiles|ForEach-Object{[string]$_.path})
$mutable+=@('yeman-settings.json.corrupt-native-1234','yeman-settings.json.before-user-reset-42-1-1','ui-background/dynamic.json','game-custom.json.bak','game-custom.json.corrupt-1234','yeman-settings.json/private.json','game-custom.json.bak/private.json')
$mutable+=@('yeman-settings.json.before-upgrade-42-1','yeman-settings.json.ymcc-retired-42-1/private.json','yeman-settings.json.before-auto-recovery-42-1','yeman-settings.json.before-bounded-recovery-42-1','yeman-settings.json.before-backup-replacement-42-1')
$index=0
foreach($relative in @($mutable|Select-Object -Unique)){
 $index++
 Case ('actual package filter excludes mutable settings/archive: '+$relative) {
  Need (Test-IsExcludedPowerControlPath $relative.Replace('/','\') $false) 'Actual production package filter admitted user data'
  $stage=New-Stage ('leak-'+$index);Put $stage $relative 'inert personal configuration marker'
  $message='';try{Assert-YmccSettingsBaseline $project $stage|Out-Null}catch{$message=$_.Exception.Message}
  Need ($message.StartsWith('SETTINGS_BASELINE_USER_DATA_IN_PACKAGE:')) ('Expected rejection, got '+$message)
 }
}
Case 'known configuration paths remain excluded even when damaged into a directory' { Need (Test-IsExcludedPowerControlPath 'yeman-settings.json' $true) 'Mutable directory admitted' }
foreach($relative in @('settings-baseline.json',[string]$contract.defaultsFile,'HandheldCompanion.runtime.json','other-user-owned-unknown.json')){
 Case ('configuration filter leaves resources/unknown paths to their owners: '+$relative) {Need (!(Test-YmccMutableSettingsPath $relative $contract)) 'Unknown/resource path claimed'}
}
foreach($relative in @('settings-baseline.json',[string]$contract.defaultsFile)){
 Case ('changed immutable staging asset is rejected: '+$relative){
  $stage=New-Stage ('drift-'+$relative);Put $stage $relative '{}'
  $message='';try{Assert-YmccSettingsBaseline $project $stage|Out-Null}catch{$message=$_.Exception.Message}
  Need ($message.StartsWith('SETTINGS_BASELINE_STAGING_DRIFT:')) 'Changed immutable asset accepted'
 }
}
foreach($file in @('build-workspace.ps1','package-release.ps1','release-export-identity.ps1','settings-baseline-contract.ps1')){
 Case ('PowerShell parser accepts maintained consumer: '+$file){
  $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $file),[ref]$tokens,[ref]$errors)|Out-Null
  Need (@($errors).Count -eq 0) ('Parse error: '+(@($errors|ForEach-Object{$_.Message}) -join '; '))
 }
}
}
$failed=@($results|Where-Object{!$_.passed}).Count
$report=@{status=$(if($failed){'FAIL'}else{'PASS'});passed=$results.Count-$failed;failed=$failed;cases=@($results);engine=$PSVersionTable.PSVersion.ToString();realSettingsWritten=$false;fullPackageExecuted=$false;scope=$(if($FourModeCopyOnly){'four-mode-copy-only'}else{'original-default'})}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$results|Where-Object{!$_.passed}|ForEach-Object{Write-Output ('FAIL '+$_.name+': '+$_.error)}
Write-Output ('SETTINGS_BASELINE_PACKAGE_TESTS '+$report.status+' passed='+$report.passed+' failed='+$failed)
if($failed){exit 1}
