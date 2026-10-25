<# Reuses the established real-component/engine UI harness with memory IO.
   Outputs live in a new evidence directory; no installed app is contacted. #>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$uiToolEnvKeys=@('YMCC_UI_TOOL_MANIFEST','YMCC_UI_TOOL_HELPER_URL','YMCC_SIM_BROWSER_RUNTIME_ENTRY','ESBUILD_BINARY_PATH','NODE_OPTIONS','NODE_PATH','PLAYWRIGHT_NODEJS_PATH','PSModulePath')
$uiToolEnvironment=@{};foreach($key in $uiToolEnvKeys){$uiToolEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
# The outer owned Job remains supplied by the shared runner; this pins only this script and its tool children.
$env:PSModulePath=$PSHOME+[IO.Path]::DirectorySeparatorChar+'Modules'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if($EvidenceDir.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'UI evidence must be outside source'}
if(Test-Path -LiteralPath $EvidenceDir){throw 'UI evidence directory must be fresh'}
New-Item -ItemType Directory -Path $EvidenceDir,(Join-Path $EvidenceDir 'site'),(Join-Path $EvidenceDir 'evidence') | Out-Null
$uiToolManifest=Join-Path $PSScriptRoot 'toolchain-inputs.json'
$uiToolHelper=Join-Path $PSScriptRoot 'tool-inputs.mjs'
$uiToolSpec=Get-Content -LiteralPath $uiToolManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$uiNodeSpec=$uiToolSpec.executables.node
$node=if($uiNodeSpec.repoRelative){[IO.Path]::GetFullPath((Join-Path $repo $uiNodeSpec.repoRelative))}else{[IO.Path]::GetFullPath($uiNodeSpec.path)}
if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant() -ne $uiNodeSpec.sha256){throw 'UI_NODE_BOOTSTRAP_HASH_MISMATCH'}
$env:YMCC_UI_TOOL_MANIFEST=$uiToolManifest
$env:YMCC_UI_TOOL_HELPER_URL=([Uri]$uiToolHelper).AbsoluteUri
$env:YMCC_SIM_BROWSER_RUNTIME_ENTRY=$uiToolSpec.playwrightCreateRequireAnchor
$env:ESBUILD_BINARY_PATH=if($uiToolSpec.executables.esbuild.repoRelative){Join-Path $repo $uiToolSpec.executables.esbuild.repoRelative}else{$uiToolSpec.executables.esbuild.path}
foreach($key in @('NODE_OPTIONS','NODE_PATH','PLAYWRIGHT_NODEJS_PATH')){[Environment]::SetEnvironmentVariable($key,$null,'Process')}
. (Join-Path $PSScriptRoot 'source-contract.ps1')
$templates=Join-Path $PSScriptRoot 'templates'
$server=$null;$serverPid=$null;$createdUtc=$null;$nav=$null;$stdout=$null;$stderr=$null;$failure=$null;$stage='copy';$status='FAIL';$closed=$false;$sourceHashes=[ordered]@{}
$previousRoot=$env:YMCC_UI_SOURCE_ROOT
$build=$null
$templateBefore=Get-SimUiTemplateSnapshot -Root $templates
$templateChanges=@();$templateStable=$false
foreach($row in $templateBefore.files){$sourceHashes['tools/ymcc_sim/global/ui-navigation/templates/'+$row.relative]=$row.sha256}
$sourceHashes['tools/ymcc_sim/global/ui-navigation/Run-Ui.ps1']=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHashes['tools/ymcc_sim/global/ui-navigation/source-contract.ps1']=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'source-contract.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()
try {
 $stage='tool-preflight'
 & $node $uiToolHelper --phase pre --repo-root $repo --evidence-dir $EvidenceDir --manifest $uiToolManifest --powershell-path ([Environment]::ProcessPath) *> (Join-Path $EvidenceDir 'tool-preflight.log')
 if($LASTEXITCODE -ne 0){throw 'UI_TOOL_PREFLIGHT_FAILED'}
 Copy-SimUiTemplateSnapshot -Snapshot $templateBefore -Destination $EvidenceDir
 $env:YMCC_UI_SOURCE_ROOT=$repo
 $stage='build'
 & $node (Join-Path $EvidenceDir 'build.mjs') *> (Join-Path $EvidenceDir 'build.log')
 if($LASTEXITCODE -ne 0){throw 'REAL_COMPONENT_BUILD_FAILED'}
 $stage='server'
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$node;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.WorkingDirectory=$EvidenceDir;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.ArgumentList.Add((Join-Path $EvidenceDir 'serve.mjs'))
 $server=[Diagnostics.Process]::Start($start);$serverPid=$server.Id;$createdUtc=$server.StartTime.ToUniversalTime().ToString('o')
 $stdout=$server.StandardOutput.ReadToEndAsync();$stderr=$server.StandardError.ReadToEndAsync()
 $ready=$false
 for($i=0;$i -lt 100 -and -not $server.HasExited;$i++){
  if(Test-Path -LiteralPath (Join-Path $EvidenceDir 'server.json')){$ready=$true;break}
  Start-Sleep -Milliseconds 50
 }
 if(-not $ready){throw 'OWN_UI_SERVER_NOT_READY'}
 $stage='navigation'
 & $node (Join-Path $EvidenceDir 'verify.mjs') *> (Join-Path $EvidenceDir 'verify.log')
 if($LASTEXITCODE -ne 0){throw 'REAL_ENGINE_NAVIGATION_FAILED'}
 $stage='source-gate'
 & $node (Join-Path $repo 'tools\gamepad_ui_release_gate.mjs') $EvidenceDir *> (Join-Path $EvidenceDir 'release-gate.txt')
 if($LASTEXITCODE -ne 0){throw 'CURRENT_UI_SOURCE_GATE_FAILED'}
 $build=Get-Content -LiteralPath (Join-Path $EvidenceDir 'build-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 $nav=Get-Content -LiteralPath (Join-Path $EvidenceDir 'navigation-gate.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 if($build.pass -ne $true -or $nav.pass -ne $true -or $nav.inputsUnchanged -ne $true){throw 'UI_RECEIPT_INCOMPLETE'}
 foreach($property in $build.sourceHashes.PSObject.Properties){$sourceHashes[$property.Name]=$property.Value}
 # Keep the exact pre-copy identities instead of hashing newer templates.
 $status='PASS'
} catch { $failure=[ordered]@{stage=$stage;code='UI_NAVIGATION_OR_SOURCE_RECEIPT_FAILED';exceptionType=$_.Exception.GetType().FullName} }
finally {
 $env:YMCC_UI_SOURCE_ROOT=$previousRoot
 if($server){
  try {if(-not $server.HasExited){$server.Kill($true)};$closed=$server.WaitForExit(5000)}catch{$closed=$false}
  try {$stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $EvidenceDir 'server.stdout.log') -Encoding UTF8;$stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $EvidenceDir 'server.stderr.log') -Encoding UTF8}catch{}
  $server.Dispose()
 }
}
try{$templateAfter=Get-SimUiTemplateSnapshot -Root $templates;$comparison=Compare-SimUiTemplateSnapshot -Before $templateBefore -After $templateAfter;$templateChanges=@($comparison.changes);$templateStable=$comparison.unchanged;if(-not $comparison.unchanged){$status='FAIL';$failure=@{stage='source';code='UI_TEMPLATE_SOURCE_CHANGED_DURING_RUN'}}}catch{$status='FAIL';$failure=@{stage='source';code='UI_TEMPLATE_SOURCE_SNAPSHOT_UNAVAILABLE'}}
foreach($own in @('tools/ymcc_sim/global/ui-navigation/Run-Ui.ps1','tools/ymcc_sim/global/ui-navigation/source-contract.ps1')){if((Get-FileHash -LiteralPath (Join-Path $repo $own) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sourceHashes[$own]){$templateChanges+=@{path=$own;kind='changed'};$templateStable=$false;$status='FAIL';$failure=@{stage='source';code='UI_HARNESS_SOURCE_CHANGED_DURING_RUN'}}}
if(-not $nav -and (Test-Path -LiteralPath (Join-Path $EvidenceDir 'navigation-gate.json'))){try{$nav=Get-Content -LiteralPath (Join-Path $EvidenceDir 'navigation-gate.json') -Raw -Encoding UTF8|ConvertFrom-Json}catch{}}
if($server -and -not $closed){$status='FAIL';$failure=[ordered]@{stage='cleanup';code='OWN_UI_SERVER_EXIT_UNCONFIRMED'}}
try {
 & $node $uiToolHelper --phase post --repo-root $repo --evidence-dir $EvidenceDir --manifest $uiToolManifest --powershell-path ([Environment]::ProcessPath) *> (Join-Path $EvidenceDir 'tool-postflight.log')
 if($LASTEXITCODE -ne 0){throw 'UI_TOOL_POSTFLIGHT_FAILED'}
 foreach($name in @('build-tool-runtime.json','verify-tool-runtime.json')){if(-not(Test-Path -LiteralPath (Join-Path $EvidenceDir $name))){throw 'UI_TOOL_STAGE_RECEIPT_MISSING'}}
}catch{$status='FAIL';$uiToolFailure=@{stage='tool-postflight';code='UI_TOOL_RUNTIME_CLOSURE_UNCONFIRMED'};if($failure){$failure['toolPostflight']=$uiToolFailure}else{$failure=$uiToolFailure}}
foreach($key in $uiToolEnvKeys){[Environment]::SetEnvironmentVariable($key,$uiToolEnvironment[$key],'Process')}
[ordered]@{schemaVersion=1;status=$status;sourceHashes=$sourceHashes;failure=$failure;states=$(if($nav){$nav.states}else{0});directions=$(if($nav){$nav.directions}else{0});actionCases=$(if($nav){$nav.actionCases}else{0});sourceChanges=$templateChanges;templateSourceStable=$templateStable;executedProductModules=(@($build.components | Where-Object {$_ -match '^src[\\/]'} )+@('src/gamepad/engine.ts','src/gamepad/spatial.ts','src/gamepad/focus.ts'));sourceScopeMeaning='repository-relative keys are product/tool sources; absolute case keys are run-local fixture sources';hardwareValidated=$false;operatingSystemSleepPerformed=$false;installedApplicationMutated=$false;ownServerClosed=$closed;serverPid=$serverPid;serverCreatedUtc=$createdUtc;boundary='real components and engine with explicit memory IO; not physical controller acceptance'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'verification.json') -Encoding UTF8
Write-Output ('UI_NAVIGATION '+$status)
if($status -ne 'PASS'){exit 2}
exit 0
