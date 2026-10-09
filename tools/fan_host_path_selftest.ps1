[CmdletBinding()]
param([string]$ValidationRoot='')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
. (Join-Path $PSScriptRoot 'fan-host-layout.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (!$ValidationRoot) {$ValidationRoot=Join-Path (Join-Path $project '..\..') 'Build\Validation\FanHostPaths'}
$allowed=[IO.Path]::GetFullPath($ValidationRoot).TrimEnd('\')
$fixtures=Join-Path $allowed ([guid]::NewGuid().ToString('N'))
if (!$fixtures.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'UNSAFE_TEST_ROOT'}
New-Item -ItemType Directory -Path $fixtures -Force|Out-Null
$results=@()
function Safe-FanFixture([string]$p) { $full=[IO.Path]::GetFullPath($p);if(!$full.StartsWith($fixtures+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'FIXTURE_ESCAPES_ALLOWED_ROOT'};return $full }
function Text-FanFixture([string]$p,[string]$t) { Safe-FanFixture $p|Out-Null;New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force|Out-Null;[IO.File]::WriteAllText($p,$t,[Text.UTF8Encoding]::new($false)) }
function Json-FanFixture([string]$p,$o) {Text-FanFixture $p ($o|ConvertTo-Json -Depth 12)}
function Case([string]$n,[scriptblock]$body,[string]$expected='') {
  $errorText=$null;try {& $body|Out-Null} catch {$errorText=$_.Exception.Message}
  if (($expected -and (!$errorText -or !$errorText.Contains($expected))) -or (!$expected -and $errorText)) {throw "FAIL $n expected=$expected actual=$errorText"}
  $script:results+= [ordered]@{name=$n;passed=$true;expectedReject=[bool]$expected;actualError=$errorText}
  Write-Output "PASS $n"
}
$source=Safe-FanFixture (Join-Path $fixtures 'synthetic-source')
$stage=Safe-FanFixture (Join-Path $fixtures 'synthetic-package')
$hostSource=Join-Path $source 'PowerControl\fan-host'
$hostStage=Join-Path $stage 'PowerControl\fan-host-v2'
$runtimeRelative='handheldcompanion-runtime\HC-FIXTURE'
$runtimeSource=Join-Path $source ('PowerControl\'+$runtimeRelative)
$runtimeStage=Join-Path $stage ('PowerControl\'+$runtimeRelative)
$hostFiles=@()
foreach($n in @('YeManFanHost.exe','YeManFanHost.dll')) {Text-FanFixture (Join-Path $hostSource $n) ('INERT-NOT-EXECUTABLE-'+$n);Text-FanFixture (Join-Path $hostStage $n) ('INERT-NOT-EXECUTABLE-'+$n);$hostFiles+=@{path=$n;sha256=(Get-FanHostLaneHash (Join-Path $hostSource $n))}}
$payload=@{schemaVersion=2;runtimeId='HC-FIXTURE';runtimeManifest='..\handheldcompanion-runtime\HC-FIXTURE\HandheldCompanion.runtime.json';files=$hostFiles}
Json-FanFixture (Join-Path $hostSource 'YeManFanHost.payload.json') $payload
Json-FanFixture (Join-Path $hostStage 'YeManFanHost.payload.json') $payload
Text-FanFixture (Join-Path $runtimeSource 'Resources\HC.dll') 'INERT RUNTIME NEVER LOADED'
Text-FanFixture (Join-Path $runtimeStage 'Resources\HC.dll') 'INERT RUNTIME NEVER LOADED'
$runtime=@{schemaVersion=1;runtimeId='HC-FIXTURE';files=@(@{path='Resources/HC.dll';sha256=(Get-FanHostLaneHash (Join-Path $runtimeSource 'Resources\HC.dll'))})}
Json-FanFixture (Join-Path $runtimeSource 'HandheldCompanion.runtime.json') $runtime
Json-FanFixture (Join-Path $runtimeStage 'HandheldCompanion.runtime.json') $runtime
$layout=@{schemaVersion=1;packageId='yemancc-update';packageVersion='0.0.33';roots=@(@{source='PowerControl'},@{source='YeManCC'});rules=@{fanHost='preserve-existing';fanHostV2='replace';fanHostPayload=@{directory='fan-host-v2';schemaVersion=2;fileCount=2;manifestSha256=(Get-FanHostLaneHash (Join-Path $hostSource 'YeManFanHost.payload.json'));runtimeDirectory=$runtimeRelative;runtimeManifestSha256=(Get-FanHostLaneHash (Join-Path $runtimeSource 'HandheldCompanion.runtime.json'))}}}
Json-FanFixture (Join-Path $stage 'YeManCC\update-manifest.json') $layout
$good=Safe-FanFixture (Join-Path (Join-Path $fixtures 'good') 'YeManCC.zip');New-Item -ItemType Directory -Path (Split-Path -Parent $good)|Out-Null
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$good)
function Mutate-FanZip([string]$name,[string]$entry,[string]$text,[switch]$AppendDuplicate) {
  $z=Safe-FanFixture (Join-Path (Join-Path $fixtures $name) 'YeManCC.zip');New-Item -ItemType Directory -Path (Split-Path -Parent $z) -Force|Out-Null;Copy-Item -LiteralPath $good -Destination $z
  $a=[IO.Compression.ZipFile]::Open($z,[IO.Compression.ZipArchiveMode]::Update)
  try {if(!$AppendDuplicate){$old=@($a.Entries|Where-Object {$_.FullName.Replace('\','/') -ceq $entry});foreach($item in $old){$item.Delete()}};$new=$a.CreateEntry($entry);$writer=[IO.StreamWriter]::new($new.Open(),[Text.UTF8Encoding]::new($false));try{$writer.Write($text)}finally{$writer.Dispose()}}finally{$a.Dispose()};return $z
}
Case 'good V2 archive with nested runtime passes' {Assert-FanHostArchiveLane $source $good}
Case 'legacy runtime folder rejected' { $z=Mutate-FanZip 'legacy' 'PowerControl/fan-host/YeManFanHost.exe' 'OLD';Assert-FanHostArchiveLane $source $z } 'FAN_LEGACY_PAYLOAD_IN_ZIP'
Case 'nested legacy folder in V2 rejected' { $z=Mutate-FanZip 'nested' 'PowerControl/fan-host-v2/fan-host/YeManFanHost.exe' 'OLD';Assert-FanHostArchiveLane $source $z } 'FAN_PAYLOAD_FILE_SET_DRIFT'
Case 'wrong binding directory rejected' { $l=($layout|ConvertTo-Json -Depth 12|ConvertFrom-Json);$l.rules.fanHostPayload.directory='fan-host';$z=Mutate-FanZip 'binding' 'YeManCC/update-manifest.json' ($l|ConvertTo-Json -Depth 12);Assert-FanHostArchiveLane $source $z } 'FAN_PAYLOAD_BINDING_PATH_DRIFT'
Case 'tampered exported host rejected' { $z=Mutate-FanZip 'host-hash' 'PowerControl/fan-host-v2/YeManFanHost.dll' 'ALTERED';Assert-FanHostArchiveLane $source $z } 'FAN_PAYLOAD_FILE_IDENTITY_DRIFT'
Case 'stale manifest rejected' { $z=Mutate-FanZip 'manifest' 'PowerControl/fan-host-v2/YeManFanHost.payload.json' (($payload|ConvertTo-Json -Depth 12)+' ');Assert-FanHostArchiveLane $source $z } 'FAN_PAYLOAD_MANIFEST_IDENTITY_DRIFT'
Case 'runtime path escape rejected' { $l=($layout|ConvertTo-Json -Depth 12|ConvertFrom-Json);$l.rules.fanHostPayload.runtimeDirectory='..\escape';$z=Mutate-FanZip 'escape' 'YeManCC/update-manifest.json' ($l|ConvertTo-Json -Depth 12);Assert-FanHostArchiveLane $source $z } 'FAN_RUNTIME_BINDING_PATH_UNSAFE'
Case 'runtime manifest source identity drift rejected' { $z=Mutate-FanZip 'runtime-manifest' 'PowerControl/handheldcompanion-runtime/HC-FIXTURE/HandheldCompanion.runtime.json' (($runtime|ConvertTo-Json -Depth 12)+' ');Assert-FanHostArchiveLane $source $z } 'FAN_RUNTIME_MANIFEST_SOURCE_DRIFT'
Case 'nested runtime hash drift rejected' { $z=Mutate-FanZip 'runtime-file' 'PowerControl/handheldcompanion-runtime/HC-FIXTURE/Resources/HC.dll' 'ALTERED';Assert-FanHostArchiveLane $source $z } 'FAN_RUNTIME_FILE_HASH_DRIFT'
Case 'duplicate archive path rejected' { $z=Mutate-FanZip 'duplicate' 'PowerControl/fan-host-v2/YeManFanHost.dll' 'DUPLICATE' -AppendDuplicate;Assert-FanHostArchiveLane $source $z } 'FAN_ZIP_PATH_UNSAFE_OR_DUPLICATE'
Case 'wrong Complete package filename rejected' { $z=Safe-FanFixture (Join-Path $fixtures 'YeManCC-Complete.zip');Copy-Item -LiteralPath $good -Destination $z;Assert-FanHostArchiveLane $source $z } 'FAN_PACKAGE_NAME_DRIFT'
Case 'wrong legacy preserve policy rejected' { $l=($layout|ConvertTo-Json -Depth 12|ConvertFrom-Json);$l.rules.fanHost='replace';$z=Mutate-FanZip 'policy' 'YeManCC/update-manifest.json' ($l|ConvertTo-Json -Depth 12);Assert-FanHostArchiveLane $source $z } 'FAN_LAYOUT_POLICY_DRIFT'
$target=Safe-FanFixture (Join-Path $fixtures 'existing-v2')
Case 'missing V2 is a clean export not legacy fallback' {if((Assert-FanHostExistingExport $hostSource $target) -ne 'absent'){throw 'EXPECTED_ABSENT'}}
New-Item -ItemType Directory -Path $target|Out-Null;Get-ChildItem -LiteralPath $hostSource -File|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $target $_.Name)}
Case 'identical V2 may be preserved' {if((Assert-FanHostExistingExport $hostSource $target)-ne'identical'){throw 'EXPECTED_IDENTICAL'}}
Text-FanFixture (Join-Path $target 'YeManFanHost.dll') 'DO NOT SILENTLY KEEP OLD CODE'
Case 'stale preserved V2 fails before export' {Assert-FanHostExistingExport $hostSource $target} 'FAN_V2_EXISTING_TARGET_DRIFT'
Case 'failed drift preflight leaves prior bytes untouched' {if([IO.File]::ReadAllText((Join-Path $target 'YeManFanHost.dll')) -cne 'DO NOT SILENTLY KEEP OLD CODE'){throw 'PRIOR_BYTES_CHANGED'}}
$emptyTarget=Safe-FanFixture (Join-Path $fixtures 'empty-v2')
New-Item -ItemType Directory -Path $emptyTarget|Out-Null
Case 'empty existing V2 fails with explicit drift code' {Assert-FanHostExistingExport $hostSource $emptyTarget} 'FAN_V2_EXISTING_TARGET_DRIFT'
$installFixture=Safe-FanFixture (Join-Path $fixtures 'install-layout')
$InstallRoot=Join-Path $installFixture 'YeManCC'
$sourcePowerControl=Join-Path $source 'PowerControl'
$actualSibling=Join-Path $installFixture 'PowerControl\fan-host-v2'
Text-FanFixture (Join-Path $actualSibling 'YeManFanHost.dll') 'STALE SIBLING MUST BE REJECTED'
$tokens=$null;$parseErrors=$null
$deployAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $project 'tools\deploy-installed.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'DEPLOY_PARSE_ERROR'}
$checks=@($deployAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Assert-FanHostExistingExport'},$true))
Case 'production deploy preflight checks sibling V2 not nested old address' {
  if($checks.Count -ne 1){throw 'EXPECTED_ONE_DEPLOY_PREFLIGHT'}
  & ([scriptblock]::Create($checks[0].Extent.Text))
} 'FAN_V2_EXISTING_TARGET_DRIFT'
Case 'deploy rejects sibling drift without touching installed fixture' {
  if([IO.File]::ReadAllText((Join-Path $actualSibling 'YeManFanHost.dll')) -cne 'STALE SIBLING MUST BE REJECTED'){throw 'PRIOR_BYTES_CHANGED'}
  if(Test-Path -LiteralPath (Join-Path $InstallRoot 'PowerControl')){throw 'NESTED_OUTPUT_CREATED'}
}
Case 'source/runtime/state roles remain separate' { $l=Get-FanHostLaneContract;if($l.sourceDirectory-ne'fan-host'-or$l.runtimeDirectory-ne'fan-host-v2'-or$l.stateDirectory-ne'fan-host'){throw 'ROLE_DRIFT'} }
Case 'current production tooling declares only V2 runtime lane' {Assert-FanHostToolingPaths $project}
# Exercise the real diagnostic-export copy functions with inert files only.
$copyTokens=$null;$copyErrors=$null
$copyAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $project 'tools\package-fan-coordinator-test.ps1'),[ref]$copyTokens,[ref]$copyErrors)
if($copyErrors.Count){throw 'COORDINATOR_PARSE_ERROR'}
foreach($name in @('Get-FullPath','Copy-DirectoryContents','Copy-TestPowerControl')) {
  $f=@($copyAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
  if($f.Count -ne 1){throw 'COORDINATOR_FUNCTION_EXTRACTION_ERROR'}
  Invoke-Expression $f[0].Extent.Text
}
$copySource=Safe-FanFixture (Join-Path $fixtures 'coordinator-source\PowerControl')
$copyTarget=Safe-FanFixture (Join-Path $fixtures 'coordinator-export\PowerControl')
foreach($lane in @('fan-host','fan-host-v2','fan-host-quarantine')){Text-FanFixture (Join-Path $copySource ($lane+'\sentinel.dll')) ('INERT '+$lane)}
Text-FanFixture (Join-Path $copySource 'ordinary.dll') 'INERT ORDINARY'
Text-FanFixture (Join-Path $copySource 'handheldcompanion-runtime\FIXTURE\Resources\HC.dll') 'INERT RUNTIME'
Case 'diagnostic generic copy excludes all competing Fan Host trees' {
  Copy-TestPowerControl $copySource $copyTarget
  foreach($lane in @('fan-host','fan-host-v2','fan-host-quarantine')){if(Test-Path -LiteralPath (Join-Path $copyTarget $lane)){throw 'COMPETING_PAYLOAD_COPIED'}}
  if(!(Test-Path -LiteralPath (Join-Path $copyTarget 'ordinary.dll')) -or !(Test-Path -LiteralPath (Join-Path $copyTarget 'handheldcompanion-runtime\FIXTURE\Resources\HC.dll'))){throw 'NORMAL_PAYLOAD_DROPPED'}
}
Case 'diagnostic explicit source copy lands only in V2 lane' {
  $sourceFanHost=Join-Path $copySource 'fan-host';$stageFanHost=Join-Path $copyTarget 'fan-host-v2'
  $copy=@($copyAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.Extent.Text -ceq 'Copy-DirectoryContents $sourceFanHost $stageFanHost'},$true))
  if($copy.Count -ne 1){throw 'EXPLICIT_FAN_COPY_MISSING'}
  & ([scriptblock]::Create($copy[0].Extent.Text))
  if([IO.File]::ReadAllText((Join-Path $stageFanHost 'sentinel.dll')) -cne 'INERT fan-host'){throw 'SOURCE_PAYLOAD_NOT_COPIED'}
  if(Test-Path -LiteralPath (Join-Path $copyTarget 'fan-host')){throw 'LEGACY_LANE_CREATED'}
}
$verifyTokens=$null;$verifyErrors=$null
$verifyAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $project 'tools\verify-yemancc-export.ps1'),[ref]$verifyTokens,[ref]$verifyErrors)
if($verifyErrors.Count){throw 'VERIFY_PARSE_ERROR'}
$resolve=@($verifyAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Resolve-PayloadDir'},$true))
if($resolve.Count -ne 1){throw 'VERIFY_FUNCTION_EXTRACTION_ERROR'}
Invoke-Expression $resolve[0].Extent.Text
$verifyRoot=Safe-FanFixture (Join-Path $fixtures 'verify-layout')
$verifyExe=Join-Path $verifyRoot 'YeManCC\YeManCC.exe'
Text-FanFixture (Join-Path $verifyRoot 'YeManCC\PowerControl\fan-host-v2\YeManFanHost.dll') 'INERT WRONG NESTED'
Text-FanFixture (Join-Path $verifyRoot 'PowerControl\fan-host\YeManFanHost.dll') 'INERT OLD SIBLING'
Case 'export verification never falls back to nested or legacy payload' {
  $row=Resolve-PayloadDir $verifyExe
  if($row.source -cne 'none' -or $row.dir -ine (Join-Path $verifyRoot 'PowerControl\fan-host-v2')){throw 'WRONG_PAYLOAD_FALLBACK'}
}
Text-FanFixture (Join-Path $verifyRoot 'PowerControl\fan-host-v2\YeManFanHost.dll') 'INERT CORRECT SIBLING'
Case 'export verification resolves only the sibling V2 payload' {
  $row=Resolve-PayloadDir $verifyExe
  if($row.source -cne 'exe-parent' -or $row.dir -ine (Join-Path $verifyRoot 'PowerControl\fan-host-v2')){throw 'SIBLING_V2_NOT_RESOLVED'}
}

# Clone only the audited code surfaces; never mutate actual product or installed paths.
$paths=@('src/bridge/fanHost.ts','native/main.cpp','tools/package-release.ps1','tools/deploy-installed.ps1','tools/package-fan-coordinator-test.ps1','tools/verify-yemancc-export.ps1','tools/audit-source-export-coverage.ps1','tools/Send-YMCC-AI-Fan-Mock.ps1','tools/cpu-rog/Send-YMCC-AI-Fan-Mock.ps1','tools/fan_bridge_real_host_smoke_selftest.ts','tools/ai_fan_runtime_selftest.ts','tools/fan_host_supersede_exit_selftest.ts','tools/fan_p4_refreeze_package.ps1','.github/workflows/release.yml')
function Code-Clone([string]$name) { $r=Safe-FanFixture (Join-Path $fixtures $name);foreach($p in $paths){Text-FanFixture (Join-Path $r $p) ([IO.File]::ReadAllText((Join-Path $project $p)))};return $r }
Case 'injected native legacy executable address rejected' { $r=Code-Clone 'native-code';$p=Join-Path $r 'native/main.cpp';Text-FanFixture $p ([IO.File]::ReadAllText($p)+'POWER_CONTROL_DIR + L"\\fan-host\\YeManFanHost.exe"');Assert-FanHostToolingPaths $r } 'FAN_LEGACY_RUNTIME_PATH'
Case 'competing V2 tree in source rejected' { $r=Code-Clone 'mixed-source';New-Item -ItemType Directory -Path (Join-Path $r 'PowerControl/fan-host-v2') -Force|Out-Null;Assert-FanHostToolingPaths $r } 'FAN_SOURCE_OUTPUT_MIXED'
Case 'publishing before lane preflight rejected' { $r=Code-Clone 'publish-order';$p=Join-Path $r '.github/workflows/release.yml';$w=[IO.File]::ReadAllText($p).Replace('name: Verify local Fan Host V2 lane before publishing','name: Removed preflight');Text-FanFixture $p ($w+"`n# name: Verify local Fan Host V2 lane before publishing");Assert-FanHostToolingPaths $r } 'FAN_PUBLISH_GATE_ORDER_DRIFT'
$report=[ordered]@{status='PASS';cases=$results;fixtureRoot=$fixtures;scope='No hardware, real product process, ACL, installed deployment or remote action. Only read-only production helpers and inert code/ZIP fixtures.'}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $fixtures 'result.json') -Encoding UTF8
Write-Output ('FAN_HOST_PATH_SELFTEST_OK cases='+$results.Count+' evidence='+$fixtures)