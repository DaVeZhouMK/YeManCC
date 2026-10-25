param(
    [string]$InputRuntime = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\sdl-timer\final\runtime-no-sdl',
    [string]$OriginalSdlPath = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\sdl-timer\final\input-snapshot\SDL3.dll',
    [string]$OutputDirectory = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\sdl-touchpad\final',
    [string]$ScratchRoot = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim'
)
$ErrorActionPreference='Stop'
$dotnet=Join-Path ${env:ProgramFiles} 'dotnet\dotnet.exe';if(!(Test-Path -LiteralPath $dotnet)){$dotnet=(Get-Command dotnet -ErrorAction Stop).Source}
$env:PATH=(Split-Path $dotnet)+[IO.Path]::PathSeparator+$env:PATH
$env:DOTNET_CLI_HOME=Join-Path $ScratchRoot 'dotnet-home';$env:NUGET_PACKAGES=Join-Path $ScratchRoot 'nuget-cache'
function Hash {param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Child {param([string]$Root,[string]$Relative) $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar;$path=[IO.Path]::GetFullPath((Join-Path $Root $Relative));if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest path escapes root'};return $path}
function Run {param([string[]]$Argv,[string]$Log) if($Log){& $dotnet @Argv 2>&1|Tee-Object -FilePath $Log}else{& $dotnet @Argv};if($LASTEXITCODE -ne 0){throw ('dotnet failed: '+($Argv -join ' '))}}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);$prefix=[IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
if(!$OutputDirectory.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must remain in approved scratch'}
if((Test-Path -LiteralPath $OutputDirectory)-and(Get-ChildItem -LiteralPath $OutputDirectory -Force|Select-Object -First 1)){throw 'Use a new empty output directory'}
New-Item -ItemType Directory -Force -Path $OutputDirectory|Out-Null
& (Join-Path $PSScriptRoot 'build-offline.ps1') -ScratchRoot $ScratchRoot
$toolDll=Join-Path $ScratchRoot 'bin\HcDeviceCoreTrim.dll';$policy=Join-Path $PSScriptRoot 'profiles\sdl-zero-touchpad.policy.json'
$manifestPath=Join-Path $InputRuntime 'HandheldCompanion.runtime.json';$sourceManifestHash=Hash $manifestPath;$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$frozen=Join-Path $OutputDirectory 'input-snapshot';New-Item -ItemType Directory -Path $frozen|Out-Null
foreach($entry in $manifest.files){$src=Child $InputRuntime $entry.path;if((Hash $src)-ne $entry.sha256){throw ('Input manifest mismatch: '+$entry.path)};$dst=Child $frozen $entry.path;New-Item -ItemType Directory -Force -Path (Split-Path $dst)|Out-Null;Copy-Item -LiteralPath $src -Destination $dst;if((Hash $dst)-ne $entry.sha256){throw 'Input changed while copying'}}
if((Hash $manifestPath)-ne $sourceManifestHash){throw 'Input manifest changed'}
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $frozen 'HandheldCompanion.runtime.json')
if(Get-ChildItem -LiteralPath $frozen -Recurse -Filter SDL3.dll){throw 'The timer baseline probe runtime must already be physically without SDL3.dll'}
$source=Join-Path $frozen 'HandheldCompanion.dll';$sourceHash=Hash $source
$independent='D:\Codex\2026-10-18\kai-s\work\hc-large-strip\native-closure\consumer-closure\sdl-remaining\SDL-REMAINING.json'
$independentHash=Hash $independent;Copy-Item -LiteralPath $independent -Destination (Join-Path $OutputDirectory 'independent-sdl-remaining.readonly-snapshot.json')
if((Hash (Join-Path $OutputDirectory 'independent-sdl-remaining.readonly-snapshot.json'))-ne $independentHash){throw 'Independent audit changed while copying'}
$candidate=Join-Path $OutputDirectory 'candidate';Run -Argv @($toolDll,'sdl-touchpad-guard',$source,$policy,$candidate) -Log (Join-Path $OutputDirectory 'candidate-build.log')
$dll=Join-Path $candidate 'HandheldCompanion.dll';$newHash=Hash $dll;$recordPath=Join-Path $candidate 'CANDIDATE.json';$record=Get-Content -LiteralPath $recordPath -Raw|ConvertFrom-Json
$runtime=Join-Path $OutputDirectory 'runtime-no-sdl';New-Item -ItemType Directory -Path $runtime|Out-Null
foreach($entry in $manifest.files){$dst=Child $runtime $entry.path;New-Item -ItemType Directory -Force -Path (Split-Path $dst)|Out-Null;Copy-Item -LiteralPath (Child $frozen $entry.path) -Destination $dst}
Copy-Item -LiteralPath $dll -Destination (Join-Path $runtime 'HandheldCompanion.dll')
foreach($entry in $manifest.files){if($entry.path -eq 'HandheldCompanion.dll'){$entry.sha256=$newHash}}
$manifest.runtimeId=$record.RuntimeId;$manifest.derivative.runtimeValidationPending=$true
$manifest.derivative|Add-Member -NotePropertyName ZeroTouchpadGuards -NotePropertyValue ([pscustomobject]@{InputHcSha256=$sourceHash;SourceManifestSha256=$sourceManifestHash;PolicySha256=(Hash $policy);IndependentAuditSha256=$independentHash;Scope='Zero-handle GetTouchpads/GetTouchpadFingers + unchanged actual InitializeInputOutput on controlled passive states; no full dummy/device constructors';ProbeRuntimeOnly=$true}) -Force
$manifest|ConvertTo-Json -Depth 25|Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
$validation=Join-Path $OutputDirectory 'validation';New-Item -ItemType Directory -Path $validation|Out-Null
foreach($name in @('SdlTouchpadProbe','SdlTimerProbe','PathLoadProbe','CallbackPathProbe','ModernHeadlessProbe')){
    $project=Join-Path $PSScriptRoot ('probes\'+$name+'\'+$name+'.csproj');$obj=Join-Path $OutputDirectory ('probe-build\'+$name+'\obj\');$bin=Join-Path $OutputDirectory ('probe-build\'+$name+'\bin\')
    Run -Argv @('build',$project,('-p:BaseIntermediateOutputPath='+$obj),('-p:OutputPath='+$bin),'--source',(Join-Path $ScratchRoot 'packages'),'--nologo','-v:q') -Log (Join-Path $validation ($name+'.build.log'))
}
$probe=Join-Path $OutputDirectory 'probe-build\SdlTouchpadProbe\bin\SdlTouchpadProbe.dll'
# The sole authorized native oracle is isolated in its own fresh process: exactly two NULL-handle queries, no SDL_Init.
Run -Argv @($probe,$OriginalSdlPath,'2a2704678bf6c9c6a944270ab35079df76f5afe92b780394ed72d9c8218b98d8','native-null-oracle',(Join-Path $validation 'native-sdl-null-oracle.json')) -Log (Join-Path $validation 'native-sdl-null-oracle.log')
Run -Argv @($probe,$frozen,$sourceHash,'baseline-zero',(Join-Path $validation 'baseline-110156-zero-handle.json')) -Log (Join-Path $validation 'baseline-110156-zero-handle.log')
Run -Argv @($probe,$runtime,$newHash,'candidate-nonzero',(Join-Path $validation 'nonzero-sentinel-native-denied.json')) -Log (Join-Path $validation 'nonzero-sentinel-native-denied.log')
Run -Argv @((Join-Path $OutputDirectory 'probe-build\PathLoadProbe\bin\PathLoadProbe.dll'),$runtime,(Join-Path $runtime 'Microsoft.Windows.SDK.NET.dll'),(Join-Path $validation 'path-load-gettypes.json')) -Log (Join-Path $validation 'path-load-gettypes.log')
Run -Argv @((Join-Path $OutputDirectory 'probe-build\CallbackPathProbe\bin\CallbackPathProbe.dll'),(Join-Path $runtime 'HandheldCompanion.dll'),$newHash,(Join-Path $ScratchRoot 'callback-original-path-golden.json'),(Join-Path $validation 'actual-callback-path-comparison.json')) -Log (Join-Path $validation 'actual-callback-path-comparison.log')
Run -Argv @((Join-Path $OutputDirectory 'probe-build\ModernHeadlessProbe\bin\ModernHeadlessProbe.dll'),$runtime,'538ea1f68f629a04',(Join-Path $validation 'modern-headless-construction.json')) -Log (Join-Path $validation 'modern-headless-construction.log')
Run -Argv @((Join-Path $OutputDirectory 'probe-build\SdlTimerProbe\bin\SdlTimerProbe.dll'),$runtime,$newHash,'candidate',(Join-Path $validation 'timer-prefix-no-sdl.json')) -Log (Join-Path $validation 'timer-prefix-no-sdl.log')
Run -Argv @($probe,$runtime,$newHash,'candidate-zero',(Join-Path $validation 'passive-zero-handle.json')) -Log (Join-Path $validation 'passive-zero-handle.log')
function Report {param([string]$Name) Get-Content -LiteralPath (Join-Path $validation $Name) -Raw|ConvertFrom-Json}
$zero=Report 'passive-zero-handle.json';$nonzero=Report 'nonzero-sentinel-native-denied.json';$baseline=Report 'baseline-110156-zero-handle.json';$oracle=Report 'native-sdl-null-oracle.json';$timer=Report 'timer-prefix-no-sdl.json';$pathResult=Report 'path-load-gettypes.json';$callbacks=Report 'actual-callback-path-comparison.json';$modern=Report 'modern-headless-construction.json'
foreach($r in @($zero,$nonzero,$baseline,$oracle,$timer)){if($r.Status -ne 'passed'){throw 'A scoped software gate failed'}}
if($zero.NativeAttemptCount-ne 0-or @($zero.Operations).Count-ne 14-or @($zero.ProcessModulesBefore).Count-ne 0-or @($zero.ProcessModulesAfter).Count-ne 0-or $nonzero.NativeAttemptCount-ne 4-or $baseline.NativeAttemptCount-ne 6-or $timer.NativeAttemptCount-ne 0-or $zero.HcAllGetTypes-ne $pathResult.hcAllTypes-or @($callbacks.checks).Count-ne 22-or $callbacks.status-ne 'passed'-or $modern.Status-ne 'passed'-or $modern.ConstructedCount-ne 7-or $modern.NativeImportsDenied-ne 0-or $oracle.Oracle.GetNumGamepadTouchpads-ne 0-or $oracle.Oracle.GetNumGamepadTouchpadFingers-ne 0-or $oracle.Oracle.NativeCalls-ne 2){throw 'Actual evidence does not satisfy the scoped gates'}
$pids=@($zero.ProcessId,$nonzero.ProcessId,$baseline.ProcessId,$oracle.ProcessId,$timer.ProcessId);if(@($pids|Select-Object -Unique).Count-ne $pids.Count){throw 'Expected distinct fresh OS probe processes'}
$total=0L;foreach($entry in $manifest.files){$p=Child $runtime $entry.path;if((Hash $p)-ne $entry.sha256){throw 'Final probe manifest mismatch'};$total+=(Get-Item -LiteralPath $p).Length}
if(Get-ChildItem -LiteralPath $runtime -Recurse -Filter SDL3.dll){throw 'Native SDL3.dll found in probe runtime'}
$manifest.derivative.runtimeValidationPending=$false;$manifest|ConvertTo-Json -Depth 25|Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
$record.Status='READY-FOR-HEADLESS-PASSIVE-TIMER-AND-ZERO-TOUCHPAD-INTEGRATION';$record.RuntimeValidationPending=$false;$record|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $recordPath -Encoding utf8
$delivery=[pscustomobject]@{Status=$record.Status;RuntimeId=$record.RuntimeId;Replacement=[pscustomobject]@{Path='HandheldCompanion.dll';File=$dll;Bytes=(Get-Item -LiteralPath $dll).Length;Sha256=$newHash;SourceSha256=$sourceHash;Mvid=$record.Mvid;AssemblyName=$record.AssemblyName;StrongName='unsigned';Authenticode='NotSigned'};OtherReplacements=@();ModernControlsSdkUnchanged=$true;PolicySha256=(Hash $policy);SourceManifestSha256=$sourceManifestHash;IndependentAuditSha256=$independentHash;ProbeRuntimePath=$runtime;ProbeRuntimeFiles=$manifest.files.Count;ProbeRuntimeBytes=$total;ProbeManifestSha256=(Hash (Join-Path $runtime 'HandheldCompanion.runtime.json'));Gates=[pscustomobject]@{HcAllGetTypes=$zero.HcAllGetTypes;ZeroHandleOperations=@($zero.Operations).Count;ActualInitializeInputOutputReceivers=2;ZeroHandleNativeAttempts=$zero.NativeAttemptCount;NonzeroOriginalCallsDenied=$nonzero.NativeAttemptCount;BaselineOriginalCallsDenied=$baseline.NativeAttemptCount;OriginalNativeNullQueries=$oracle.Oracle.NativeCalls;Callbacks=@($callbacks.checks).Count;ModernPureConstructions=$modern.ConstructedCount;TimerNativeAttempts=$timer.NativeAttemptCount;PhysicallyNoSdl3Dll=$true;NoNativeSdl3ProcessModules=$true;FreshProcessIds=$pids};FullDummyConstructorExecuted=$false;DeviceConstructorsExecuted=$false;RemainingFullConstructorBoundary='IController creates GamepadMotion.dll!CreateGamepadMotion and accesses ManagerFactory with Tablet/manager initialization; full constructors were not executed.';PassiveSdlClosureScope='Independent receiver audit states=59/budget=400 closed the approved passive SDL edges; timer+two guarded methods cover its confirmed sinks. Software evidence runs exact getter implementations and actual unchanged InitializeInputOutput on two controlled passive receiver states.';Sdl3CsMetadataRetained=$true;Integration='Replace HC only and regenerate owner pins/manifest. Probe runtime is not a production payload. No profile/consumer/production files modified.'}
$delivery|ConvertTo-Json -Depth 18|Set-Content -LiteralPath (Join-Path $OutputDirectory 'DELIVERY.json') -Encoding utf8
Write-Output ('DELIVERY '+(Join-Path $OutputDirectory 'DELIVERY.json'))