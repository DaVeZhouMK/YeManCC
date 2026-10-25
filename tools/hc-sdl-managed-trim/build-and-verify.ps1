param(
    [string]$InputRuntime='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\sdl\input-snapshot',
    [string]$OutputDirectory='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\sdl\final',
    [string]$ScratchRoot='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\sdl',
    [string]$CecilPath="$env:USERPROFILE\.nuget\packages\microsoft.net.illink.tasks\10.0.11\tools\net\Mono.Cecil.dll"
)
$ErrorActionPreference='Stop'
$dotnet=(Get-Command dotnet -ErrorAction Stop).Source
$tool=$PSScriptRoot
$pins=Get-Content -LiteralPath (Join-Path $tool 'pins.json') -Raw|ConvertFrom-Json
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Child([string]$Root,[string]$Relative){$prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar;$p=[IO.Path]::GetFullPath((Join-Path $Root $Relative));if(!$p.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes intended root'};$p}
function Need([bool]$Value,[string]$Message){if(!$Value){throw $Message}}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$prefix=[IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
Need ($OutputDirectory.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) 'Output must be a new directory within ScratchRoot'
Need (!(Test-Path -LiteralPath $OutputDirectory)) 'Output must not already exist; previous evidence is preserved'
Need ((Hash $CecilPath)-eq $pins.cecilSha256) 'Offline Mono.Cecil hash mismatch'
Need ((Hash (Join-Path $InputRuntime 'HandheldCompanion.runtime.json'))-eq $pins.sourceManifestSha256) 'Frozen source manifest mismatch; use the pinned source snapshot, not the concurrently changed production runtime'
Need ((Hash (Join-Path $InputRuntime 'HandheldCompanion.dll'))-eq $pins.hcSha256) 'HC input pin mismatch'
Need ((Hash (Join-Path $InputRuntime 'SDL3-CS.dll'))-eq $pins.sdlInputSha256) 'SDL input pin mismatch'
$manifest=Get-Content -LiteralPath (Join-Path $InputRuntime 'HandheldCompanion.runtime.json') -Raw|ConvertFrom-Json
$inputItems=@(Get-ChildItem -LiteralPath $InputRuntime -Recurse -Force)
Need (@($inputItems|Where-Object { $_.Attributes-band [IO.FileAttributes]::ReparsePoint }).Count-eq 0) 'Source contains reparse points'
Need (@($inputItems|Where-Object {!$_.PSIsContainer}).Count-eq (@($manifest.files).Count+1)) 'Frozen source has unexpected file count'
Need (@($manifest.files|Where-Object {[IO.Path]::GetFileName($_.path)-ieq 'SDL3.dll'}).Count-eq 0) 'Native SDL is prohibited'
foreach($entry in $manifest.files){Need ((Hash (Child $InputRuntime $entry.path))-eq $entry.sha256) ('Source file mismatch: '+$entry.path)}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$validation=Join-Path $OutputDirectory 'validation';New-Item -ItemType Directory -Path $validation|Out-Null
$offline=Join-Path $OutputDirectory 'offline-source';New-Item -ItemType Directory -Path $offline|Out-Null
$snapshot=Join-Path $OutputDirectory 'input-snapshot';New-Item -ItemType Directory -Path $snapshot|Out-Null
foreach($entry in $manifest.files){$a=Child $InputRuntime $entry.path;$b=Child $snapshot $entry.path;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($b))|Out-Null;Copy-Item -LiteralPath $a -Destination $b;Need ((Hash $b)-eq $entry.sha256) 'Snapshot copy differs'}
Copy-Item -LiteralPath (Join-Path $InputRuntime 'HandheldCompanion.runtime.json') -Destination $snapshot
$probeRoot=Join-Path (Split-Path $tool) 'hc-devicecore-trim\probes'
$probeNames=@('SdlTouchpadProbe','SdlTimerProbe','CallbackPathProbe')
$sourceFiles=@(Get-ChildItem -LiteralPath $tool -File|Where-Object {$_.Extension-in @('.cs','.csproj','.json','.ps1')})
foreach($name in $probeNames){$sourceFiles+=@(Get-ChildItem -LiteralPath (Join-Path $probeRoot $name) -File|Where-Object {$_.Extension-in @('.cs','.csproj')})}
$sourceReceipt=@($sourceFiles|Sort-Object FullName|ForEach-Object {[pscustomobject]@{Path=$_.FullName;Sha256=(Hash $_.FullName);Bytes=$_.Length}})
$sourceReceipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'source-receipt.json') -Encoding utf8
function Run([string[]]$Arguments,[string]$Log,[bool]$ExpectFailure=$false){$logPath=Join-Path $validation $Log;& $dotnet @Arguments *> $logPath;$code=$LASTEXITCODE;if($ExpectFailure){Need ($code-ne 0) ('Negative unexpectedly passed: '+$Log)}else{Need ($code-eq 0) ('Command failed: '+$Log)};Get-Content -LiteralPath $logPath -Tail 2|Write-Output}
function Build([string]$Project,[string]$Name){$build=Join-Path $OutputDirectory ('build\'+$Name);$bin=Join-Path $build 'bin';$obj=(Join-Path $build 'obj')+'\';Run @('build',$Project,'--nologo','--source',$offline,'-p:NuGetAudit=false',('-p:CecilPath='+$CecilPath),('-p:BaseIntermediateOutputPath='+$obj),'-o',$bin) ($Name+'.build.log');return (Join-Path $bin ($Name+'.dll'))}
# Suppress routine command output from function return values, keeping executable paths exact.
function BuildOnly([string]$Project,[string]$Name){$build=Join-Path $OutputDirectory ('build\'+$Name);$bin=Join-Path $build 'bin';$obj=(Join-Path $build 'obj')+'\';Run @('build',$Project,'--nologo','--source',$offline,'-p:NuGetAudit=false',('-p:CecilPath='+$CecilPath),('-p:BaseIntermediateOutputPath='+$obj),'-o',$bin) ($Name+'.build.log')|Out-Host;return (Join-Path $bin ($Name+'.dll'))}
$slicer=BuildOnly (Join-Path $tool 'SdlManagedTrim.csproj') 'SdlManagedTrim'
$audit=BuildOnly (Join-Path $tool 'Audit.csproj') 'Audit'
$surface=BuildOnly (Join-Path $tool 'SurfaceProbe.csproj') 'SurfaceProbe'
$mutator=BuildOnly (Join-Path $tool 'NegativeMutation.csproj') 'NegativeMutation'
$probes=@{};foreach($name in $probeNames){$probes[$name]=BuildOnly (Join-Path $probeRoot ($name+'\'+$name+'.csproj')) $name}
$full=Join-Path $OutputDirectory 'conservative';$leaf=Join-Path $OutputDirectory 'candidate'
Run @($slicer,'slice',$snapshot,'untouched',$full) 'conservative-slice.log'
Run @($slicer,'slice',$snapshot,'delegate-leaf',$leaf) 'candidate-slice.log'
$dll=Join-Path $leaf 'SDL3-CS.dll';$candidateHash=Hash $dll
Need ($candidateHash-eq $pins.candidateSha256) 'Candidate is not byte-for-byte reproducible'
Need ((Get-Item -LiteralPath $dll).Length-eq $pins.candidateBytes) 'Candidate size differs'
Need ((Hash (Join-Path $full 'SDL3-CS.dll'))-eq $pins.conservativeSha256) 'Conservative candidate differs'
Run @($audit,$snapshot,$dll,(Join-Path $leaf 'slice.json'),(Join-Path $validation 'independent-audit.json')) 'independent-audit.log'
$runtime=Join-Path $OutputDirectory 'runtime-no-sdl';New-Item -ItemType Directory -Path $runtime|Out-Null
Get-ChildItem -LiteralPath $snapshot|Copy-Item -Destination $runtime -Recurse
Copy-Item -LiteralPath $dll -Destination (Join-Path $runtime 'SDL3-CS.dll') -Force
# This diagnostic manifest describes the actual candidate files. It is not the production profile.
$runtimeManifest=Get-Content -LiteralPath (Join-Path $snapshot 'HandheldCompanion.runtime.json') -Raw|ConvertFrom-Json
$runtimeManifest.runtimeId='YMCC-SDL-MANAGED-LEAF-SCRATCH-20261018'
($runtimeManifest.files|Where-Object {$_.path-eq 'SDL3-CS.dll'}).sha256=$candidateHash
$runtimeManifest|Add-Member -NotePropertyName 'sdlManagedTrial' -NotePropertyValue ([pscustomobject]@{notProductionPayload=$true;sourceManifestSha256=$pins.sourceManifestSha256;slice='candidate/slice.json';hcUnchanged=$true}) -Force
$runtimeManifest|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
$typeList=Join-Path $leaf 'retained-types.json'
Run @($surface,$snapshot,$pins.hcSha256,$pins.sdlInputSha256,$typeList,'-',(Join-Path $validation 'surface-golden.json'),'baseline') 'surface-golden.log'
Run @($surface,$runtime,$pins.hcSha256,$candidateHash,$typeList,(Join-Path $validation 'surface-golden.json'),(Join-Path $validation 'surface-candidate.json'),'candidate') 'surface-candidate.log'
Run @($probes.SdlTouchpadProbe,$runtime,$pins.hcSha256,'candidate-zero',(Join-Path $validation 'passive-zero.json')) 'passive-zero.log'
Run @($probes.SdlTouchpadProbe,$runtime,$pins.hcSha256,'candidate-nonzero',(Join-Path $validation 'nonzero-native-denied.json')) 'nonzero-native-denied.log'
Run @($probes.SdlTimerProbe,$runtime,$pins.hcSha256,'candidate',(Join-Path $validation 'passive-timer.json')) 'passive-timer.log'
Run @($probes.CallbackPathProbe,(Join-Path $snapshot 'HandheldCompanion.dll'),$pins.hcSha256,'-',(Join-Path $validation 'callbacks-golden.json')) 'callbacks-golden.log'
Run @($probes.CallbackPathProbe,(Join-Path $runtime 'HandheldCompanion.dll'),$pins.hcSha256,(Join-Path $validation 'callbacks-golden.json'),(Join-Path $validation 'callbacks-candidate.json')) 'callbacks-candidate.log'
$bad=Join-Path $OutputDirectory 'INTENTIONALLY-INVALID-event-layout'
Run @($mutator,$dll,'event-layout',$bad) 'negative-fixture.log'
$badDll=Join-Path $bad 'SDL3-CS.dll';$badHash=Hash $badDll
$badRuntime=Join-Path $OutputDirectory 'INTENTIONALLY-INVALID-runtime';New-Item -ItemType Directory -Path $badRuntime|Out-Null
Get-ChildItem -LiteralPath $runtime|Copy-Item -Destination $badRuntime -Recurse
Copy-Item -LiteralPath $badDll -Destination (Join-Path $badRuntime 'SDL3-CS.dll') -Force
Run @($surface,$badRuntime,$pins.hcSha256,$badHash,$typeList,(Join-Path $validation 'surface-golden.json'),(Join-Path $validation 'negative-layout-surface.json'),'intentional-invalid-layout') 'negative-layout-surface.log' $true
$badSlice=Get-Content -LiteralPath (Join-Path $leaf 'slice.json') -Raw|ConvertFrom-Json;$badSlice.OutputSha256=$badHash
$badSlice|ConvertTo-Json -Depth 16|Set-Content -LiteralPath (Join-Path $bad 'slice.json') -Encoding utf8
Run @($audit,$snapshot,$badDll,(Join-Path $bad 'slice.json'),(Join-Path $validation 'negative-layout-audit.json')) 'negative-layout-audit.log' $true
function Report([string]$Name){Get-Content -LiteralPath (Join-Path $validation $Name) -Raw|ConvertFrom-Json}
$zero=Report 'passive-zero.json';$nonzero=Report 'nonzero-native-denied.json';$timer=Report 'passive-timer.json';$surfaceResult=Report 'surface-candidate.json';$surfaceGolden=Report 'surface-golden.json';$callbacks=Report 'callbacks-candidate.json';$auditResult=Report 'independent-audit.json';$negative=Report 'negative-layout-surface.json';$negativeAudit=Report 'negative-layout-audit.json'
foreach($r in @($zero,$nonzero,$timer,$surfaceResult,$surfaceGolden,$auditResult)){Need ($r.Status-eq 'passed') 'Required gate status failed'}
Need ($zero.HcAllGetTypes-eq 1768-and @($zero.Operations).Count-eq 14-and $zero.NativeAttemptCount-eq 0) 'Passive getter/InitializeInputOutput evidence incomplete'
Need ($nonzero.NativeAttemptCount-eq 4-and @($nonzero.Operations).Count-eq 4) 'Real nonzero native bridges were not intercepted four times'
Need ($timer.NativeAttemptCount-eq 0-and $timer.HcAllGetTypes-eq 1768) 'Passive timer evidence incomplete'
Need ($surfaceResult.ComparedStructLayouts-eq 42-and $surfaceResult.ComparedEnums-eq 15-and $surfaceResult.ComparedInstanceFields-eq 337-and $surfaceResult.ComparedTypes-eq 92) 'Type layout evidence incomplete'
Need ($callbacks.status-eq 'passed'-and @($callbacks.checks).Count-eq 22) 'Real callback parity incomplete'
Need ($auditResult.AllExternalSdlTypeRefsResolved-eq 10-and $auditResult.AllExternalSdlMemberRefsResolved-eq 35-and $auditResult.KeptMethodParity-eq 218-and $auditResult.RetainedNativeImports-eq 33-and @($auditResult.Errors).Count-eq 0) 'Independent closure audit incomplete'
Need ($negative.Status-eq 'failed'-and $negative.Exception-like '*type metadata, enum constants, field layouts*') 'Negative failed for an unrelated reason'
Need ($negativeAudit.Status-eq 'failed'-and @($negativeAudit.Errors).Count-eq 1-and $negativeAudit.Errors[0]-like '*Field signature/offset/constant changed*') 'Independent negative failed for an unrelated reason'
$pids=@($zero.ProcessId,$nonzero.ProcessId,$timer.ProcessId,$surfaceResult.ProcessId,$surfaceGolden.ProcessId,$negative.ProcessId)
Need (@($pids|Select-Object -Unique).Count-eq $pids.Count) 'Fresh-process evidence reused a process'
foreach($r in @($zero,$nonzero,$timer)){Need (@($r.ProcessModulesBefore).Count-eq 0-and @($r.ProcessModulesAfter).Count-eq 0) 'Native SDL3 process module appeared'}
foreach($entry in $runtimeManifest.files){Need ((Hash (Child $runtime $entry.path))-eq $entry.sha256) 'Final diagnostic runtime file mismatch'}
Need (@(Get-ChildItem -LiteralPath $runtime -Recurse -File|Where-Object {$_.Name-ieq 'SDL3.dll'}).Count-eq 0) 'Native SDL3 file appeared'
Need ((Hash (Join-Path $runtime 'HandheldCompanion.dll'))-eq $pins.hcSha256) 'HC was modified'
foreach($source in $sourceReceipt){Need ((Hash $source.Path)-eq $source.Sha256) ('Source changed during reproduction: '+$source.Path)}
$delivery=[ordered]@{Status='READY-FOR-OWNER-INTEGRATION-SDL-MANAGED-LEAF';Date='2026-10-18';Replacement=[ordered]@{Path='SDL3-CS.dll';File=$dll;Bytes=(Get-Item -LiteralPath $dll).Length;Sha256=$candidateHash;Identity=$pins.candidateIdentity;InputBytes=$pins.sdlInputBytes;InputSha256=$pins.sdlInputSha256;SavedBytes=($pins.sdlInputBytes-$pins.candidateBytes)};HcSha256=$pins.hcSha256;HcModified=$false;FrozenSourceManifestSha256=$pins.sourceManifestSha256;ProbeRuntimePath=$runtime;ProbeManifestSha256=(Hash (Join-Path $runtime 'HandheldCompanion.runtime.json'));Gates=[ordered]@{HcGetTypes=1768;SdlReflectionTypes=92;StructLayouts=42;Enums=15;InstanceFields=337;ExternalTypes=10;ExternalMembers=35;InternalReferences=$auditResult.InternalMemberAndTypeReferencesResolved;KeptMethods=218;MarshalDescriptorParity=$auditResult.MarshalDescriptorParity;NativeImports=33;RemovedPrivateDelegateInitializers=1178;PassiveOperations=14;PassiveNativeAttempts=0;NonzeroOriginalBridgeInterceptions=4;ActualCallbackChecks=22;BrokenLayoutRejectedByReflectionAndIndependentAudit=$true;NoNativeSdlFileOrModule=$true;DistinctProcessIds=$pids};SourceReceiptSha256=(Hash (Join-Path $OutputDirectory 'source-receipt.json'));NoProductionFilesModified=$true;FullDummyControllerConstructorsExecuted=$false;DeviceOrHostExecuted=$false;Integration='Replace only SDL3-CS.dll and update owner runtime hash/manifest. Do not copy the diagnostic runtime wholesale. All native implementations are unchanged; active SDL controller services remain outside this headless runtime profile.';Unverified=@('Real device/controller lifecycle, EC and transport behavior','Full dummy/IDevice constructors (GamepadMotion/ManagerFactory boundary)','Any unknown dynamically loaded SDL type outside audited current metadata/string roots','CPU/power benefits and real sleep/wake behavior')}
$delivery|ConvertTo-Json -Depth 16|Set-Content -LiteralPath (Join-Path $OutputDirectory 'DELIVERY.json') -Encoding utf8
Write-Output ('DELIVERY '+(Join-Path $OutputDirectory 'DELIVERY.json'))
