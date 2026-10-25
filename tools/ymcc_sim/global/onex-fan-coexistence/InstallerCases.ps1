[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InstallerSource,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$localModuleRoot=Join-Path $PSHOME 'Modules'
$env:PSModulePath=$localModuleRoot+';'+$env:PSModulePath
function Test-InstallerFixturePlainPath([string]$Path) {
 for($candidate=[IO.Path]::GetFullPath($Path);$candidate;$candidate=[IO.Path]::GetDirectoryName($candidate)) {
  if((Test-Path -LiteralPath $candidate) -and (([IO.File]::GetAttributes($candidate) -band [IO.FileAttributes]::ReparsePoint) -ne 0)){return $false}
 }
 return $true
}
function Get-InstallerFixtureOwnedParent([string]$Output) {
 try {
  $stage=[IO.Path]::GetDirectoryName($Output);$root=[IO.Path]::GetDirectoryName($stage)
  if([IO.Path]::GetFileName($stage) -ne 'installer-admission' -or [IO.Path]::GetFileName($Output) -notin @('positive','original','fault')){return $null}
  $ownerPath=Join-Path $root '.ymcc-parent-build-owner.json';$manifestPath=Join-Path $root 'PARENT-BUILD-PREPARED.json';$executionPath=Join-Path $root '.ymcc-parent-execution.json'
  if(Test-Path -LiteralPath (Join-Path $root 'PARENT-BUILD-EXECUTION.json')){return $null}
  foreach($file in @($ownerPath,$manifestPath,$executionPath)){if(-not [IO.File]::Exists($file) -or -not (Test-InstallerFixturePlainPath $file)){return $null}}
  $owner=Get-Content -LiteralPath $ownerPath -Raw | ConvertFrom-Json;$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json;$execution=Get-Content -LiteralPath $executionPath -Raw | ConvertFrom-Json
  $kind='ymcc-original-fan-parent-build-handoff';$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
  if($owner.schemaVersion -ne 1 -or $manifest.schemaVersion -ne 1 -or $execution.schemaVersion -ne 1 -or $owner.kind -ne $kind -or $manifest.kind -ne $kind -or $execution.kind -ne $kind){return $null}
  $ownerId=[guid]::Empty;if(-not [guid]::TryParse([string]$owner.ownerId,[ref]$ownerId) -or $ownerId -eq [guid]::Empty -or $owner.ownerId -ne $execution.ownerId){return $null}
  foreach($document in @($owner,$manifest)){if([IO.Path]::GetFullPath($document.repoRoot) -ne $repo -or [IO.Path]::GetFullPath($document.evidenceDir) -ne $root){return $null}}
  if([IO.Path]::GetFullPath($manifest.owner.path) -ne $ownerPath -or [IO.Path]::GetFullPath($execution.prepared.path) -ne $manifestPath){return $null}
  $ownerSha=(Get-FileHash -LiteralPath $ownerPath -Algorithm SHA256).Hash.ToLowerInvariant();$manifestSha=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if($manifest.owner.sha256 -cne $ownerSha -or $execution.prepared.sha256 -cne $manifestSha -or $execution.state -ne 'running'){return $null}
  $fixturePath=[IO.Path]::GetFullPath($PSCommandPath);$fixtureSha=(Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
  $fixturePins=@($manifest.sourcePins|Where-Object{[IO.Path]::GetFullPath($_.path) -eq $fixturePath})
  if($fixturePins.Count -ne 1 -or $fixturePins[0].sha256 -cne $fixtureSha){return $null}
  $ownerProcess=[Diagnostics.Process]::GetProcessById([int]$execution.pid)
  if($ownerProcess.HasExited -or $ownerProcess.ProcessName -ne 'node' -or $ownerProcess.StartTime.ToUniversalTime() -gt ([datetime]$execution.startedAt).ToUniversalTime()){return $null}
  return @{kind='owned-parent';root=$root;ownerId=$owner.ownerId;preparedSha256=$manifestSha}
 } catch { return $null }
}

$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$allowedBuildRoots=@([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..\Build')),[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..\..\Build')))
$withinBuild=@($allowedBuildRoots|Where-Object{$OutputDirectory.StartsWith($_.TrimEnd([char]92)+[char]92,[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0
if(-not (Test-InstallerFixturePlainPath $OutputDirectory)){throw 'Installer fixture output must not traverse a reparse point'}
$outputAdmission=if($withinBuild){@{kind='legacy-build'}}else{Get-InstallerFixtureOwnedParent $OutputDirectory}
if($null -eq $outputAdmission){throw 'Installer fixture output must remain in a workspace Build directory or a verified running parent evidence root'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Fresh installer fixture root required'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($InstallerSource,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual installer source did not parse'}
$loops=@($ast.FindAll({param($a) $a -is [Management.Automation.Language.ForEachStatementAst] -and $a.Variable.VariablePath.UserPath -eq 'required' -and $a.Body.Extent.Text.Contains('$expectedFiles.ContainsKey')},$true))
if($loops.Count -ne 1){throw 'Actual Host required-file consumer is missing or ambiguous'}
$gate=$loops[0].Extent.Text
$block=[ScriptBlock]::Create($gate)
$required=@('YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.authorization.md','install-fan-host-payload.ps1')
$rows=@()
foreach($missing in @('complete')+$required+@('unrelated-extra')){
 $expectedFiles=@{};foreach($file in $required){if($file -ne $missing){$expectedFiles[$file.ToLowerInvariant()]='INERT-DECLARATION'}}
 if($missing -eq 'unrelated-extra'){$expectedFiles['unrelated.txt']='INERT-DECLARATION'}
 $failure=$null;try{& $block}catch{$failure=$_.Exception.Message}
 $mustReject=$required -contains $missing
 $passed=if($mustReject){$null -ne $failure -and $failure.Contains($missing)}else{$null -eq $failure}
 $rows+=@{id='installer-required:'+ $missing;passed=$passed;rejected=$null -ne $failure;error=$(if($passed){$null}elseif($mustReject -and $null -eq $failure){'Mandatory member accepted: '+$missing}else{$failure})}
}
$failed=@($rows|Where-Object{-not $_.passed})
$sha=[Security.Cryptography.SHA256]::Create();try{$gateHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($gate)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
$report=@{status=$(if($failed.Count){'FAIL'}else{'PASS'});caseCount=$rows.Count;failedCount=$failed.Count;cases=$rows;source=[IO.Path]::GetFullPath($InstallerSource);sourceSha256=(Get-FileHash -LiteralPath $InstallerSource -Algorithm SHA256).Hash.ToLowerInvariant();actualGateSha256=$gateHash;actualGate=$gate;powerShellVersion=$PSVersionTable.PSVersion.ToString();outputRootAdmission=$outputAdmission;fullInstallerExecuted=$false;administratorGateExecuted=$false;deploymentPerformed=$false;hardwareIo=$false;scope='Only the actual unique Host mandatory-file foreach AST extent executes with inert declaration maps'}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
Write-Output ('INSTALLER_REQUIRED_CONSUMER '+$report.status+' cases='+$rows.Count+' failed='+$failed.Count)
if($failed.Count){exit 2}
