$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
# Infrastructure regression only: reuses original classifier functions and golden data.
# No new business adapter ID or default release gate is registered.
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
. (Join-Path $repo 'tools/ymcc_sim/global/release-host-environment.ps1')
$core=Get-YmccSimulationCoreRoot
$root=Join-Path $core ('Evidence/Build/typing-scope-controls/'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
. (Join-Path $repo 'tools/ymcc_sim/lib/sim_core.ps1')
. (Join-Path $repo 'tools/ymcc_sim/domains/sleep/lib_sleep_typing.ps1')
$scriptDir=Join-Path $repo 'tools/ymcc_sim/domains/sleep'
$inputFiles=@($PSCommandPath,(Join-Path $repo 'tools/ymcc_sim/lib/sim_core.ps1'),(Join-Path $repo 'tools/ymcc_sim/global/release-host-environment.ps1'),(Join-Path $scriptDir 'lib_sleep_typing.ps1'),(Join-Path $scriptDir 'sleep_types.json'))+@(Get-ChildItem -LiteralPath (Join-Path $scriptDir 'fixtures') -Filter 'sleeptype-*.json'|ForEach-Object{$_.FullName})
$sourceBefore=@($inputFiles|ForEach-Object{Get-SimFileIdentity $_})
$runtime=Get-SimFileIdentity ([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName)
$script:PowerCfgCalls=0
function powercfg { $script:PowerCfgCalls++; throw 'SOFTWARE_SCOPE_MUST_NOT_QUERY_PLATFORM' }
$script:Run=New-SimRun -Domain sleep -Mode sleeptypes -ModeKind none -ToolPath (Join-Path $scriptDir 'lib_sleep_typing.ps1')
Invoke-SleepTypes -TypingScope software
$software=Get-SimVerdict $script:Run
if($software.exitCode -ne 0 -or $software.counts.checksTotal -ne 9 -or $script:PowerCfgCalls -ne 0){throw 'SOFTWARE_SCOPE_BOUNDARY_FAILED'}
if($script:Run.extra.fieldTyping.status -ne 'NOT_RUN' -or $script:Run.extra.fieldTyping.historicalEvidenceConsumed -ne $false){throw 'FIELD_SCOPE_WAS_NOT_WITHHELD'}
$softwareRun=$script:Run
$fixture=Get-Content -LiteralPath (Join-Path $scriptDir 'fixtures/sleeptype-s3.json') -Raw -Encoding UTF8|ConvertFrom-Json
$fixture.expect.type='IMPOSSIBLE_TYPE'
$badGolden=Test-SleepTypeFixture $fixture
if($badGolden.fails.Count -eq 0){throw 'GOLDEN_NEGATIVE_WAS_NOT_DETECTED'}
$source=Join-Path $root 'inert-history-identity-control.json'
[IO.File]::WriteAllText($source,'{"cycles":[]}',[Text.UTF8Encoding]::new($false))
$pairRejected=$false
try{Invoke-SleepTypes -S3EvidencePath $source}catch{$pairRejected=$_.Exception.Message -eq 'S3_EVIDENCE_PATH_AND_SHA256_REQUIRED_TOGETHER'}
if(-not $pairRejected){throw 'UNBOUND_EVIDENCE_PATH_ACCEPTED'}
$softwareInputRejected=$false
try{Invoke-SleepTypes -TypingScope software -S3EvidencePath $source -S3EvidenceSha256 ('0'*64)}catch{$softwareInputRejected=$_.Exception.Message -eq 'SOFTWARE_TYPING_DOES_NOT_CONSUME_FIELD_EVIDENCE'}
if(-not $softwareInputRejected){throw 'SOFTWARE_SCOPE_IGNORED_EXPLICIT_FIELD_INPUT'}
function powercfg { $script:PowerCfgCalls++; "The following sleep states are available on this system:`n    Standby (S3)`n    Hibernate" }
$script:Run=New-SimRun -Domain sleep -Mode sleeptypes -ModeKind none -ToolPath (Join-Path $scriptDir 'lib_sleep_typing.ps1')
Invoke-SleepTypes -S3EvidencePath $source -S3EvidenceSha256 ('0'*64)
$badIdentity=Get-SimVerdict $script:Run
if($badIdentity.exitCode -eq 0 -or $script:Run.extra.Contains('s3TypeVerdict')){throw 'BAD_HISTORY_IDENTITY_ALLOWED_CLASSIFICATION'}
$result=[ordered]@{schemaVersion=1;status='TYPING_SCOPE_CONTROLS_PASS';scope='Synthetic controls of original classifier; no actual S3 evidence manufactured';software=$software;softwareField=$softwareRun.extra.fieldTyping;softwarePowerCfgCalls=0;badGoldenDetected=($badGolden.fails.Count -gt 0);unboundPathRejected=$pairRejected;softwareFieldInputRejected=$softwareInputRejected;badHistoryIdentityVerdict=$badIdentity;badHistoryClassificationExecuted=$false;realSleepPerformed=$false;hardwareActionPerformed=$false}
$sourceAfter=@($inputFiles|ForEach-Object{Get-SimFileIdentity $_})
if(($sourceBefore|ConvertTo-Json -Depth 6 -Compress) -cne ($sourceAfter|ConvertTo-Json -Depth 6 -Compress)){throw 'TYPING_CONTROL_INPUT_CHANGED'}
$result['sourceInputs']=$sourceAfter
$result['runtime']=$runtime
$result['outputRoot']=$root
[IO.File]::WriteAllText((Join-Path $root 'SCOPE-CONTROLS.json'),($result|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Output ('TYPING_SCOPE_CONTROLS_PASS evidence='+$root)
