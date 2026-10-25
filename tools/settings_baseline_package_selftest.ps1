[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
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
Case 'valid staged baseline produces a hash-bound updater rule' {
 $stage=New-Stage 'valid';$binding=Assert-YmccSettingsBaseline $project $stage
 Need ($binding.userData -eq 'preserve' -and $binding.revision -eq 1) 'Wrong updater rule'
 Need ($binding.manifestSha256 -eq (Get-FileHash -LiteralPath (Join-Path $stage 'settings-baseline.json')).Hash) 'Binding mismatch'
}
$mutable=@([string]$contract.document.path,[string]$contract.document.backup)+@($contract.legacyImports|ForEach-Object{[string]$_.path})+@($contract.independentUserFiles|ForEach-Object{[string]$_.path})
$mutable+=@('yeman-settings.json.corrupt-native-1234','yeman-settings.json.before-user-reset-42-1-1','ui-background/dynamic.json','game-custom.json.bak','game-custom.json.corrupt-1234','yeman-settings.json/private.json','game-custom.json.bak/private.json')
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
$failed=@($results|Where-Object{!$_.passed}).Count
$report=@{status=$(if($failed){'FAIL'}else{'PASS'});passed=$results.Count-$failed;failed=$failed;cases=@($results);engine=$PSVersionTable.PSVersion.ToString();realSettingsWritten=$false;fullPackageExecuted=$false}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$results|Where-Object{!$_.passed}|ForEach-Object{Write-Output ('FAIL '+$_.name+': '+$_.error)}
Write-Output ('SETTINGS_BASELINE_PACKAGE_TESTS '+$report.status+' passed='+$report.passed+' failed='+$failed)
if($failed){exit 1}
