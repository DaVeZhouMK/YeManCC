param(
    [string]$InputRuntime = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\modern-closure\final\closure\runtime',
    [string]$OutputDirectory = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\sdl-timer\final',
    [string]$ScratchRoot = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim'
)
$ErrorActionPreference = 'Stop'
$dotnet = Join-Path ${env:ProgramFiles} 'dotnet\dotnet.exe'
if (!(Test-Path -LiteralPath $dotnet)) { $dotnet = (Get-Command dotnet -ErrorAction Stop).Source }
$env:PATH = (Split-Path $dotnet) + [IO.Path]::PathSeparator + $env:PATH
$env:DOTNET_CLI_HOME = Join-Path $ScratchRoot 'dotnet-home'
$env:NUGET_PACKAGES = Join-Path $ScratchRoot 'nuget-cache'
function Hash { param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Invoke-Dotnet { param([string[]]$Argv,[string]$Log) if ($Log) { & $dotnet @Argv 2>&1 | Tee-Object -FilePath $Log } else { & $dotnet @Argv }; if ($LASTEXITCODE -ne 0) { throw ('dotnet failed: '+($Argv -join ' ')) } }
function Safe-Child { param([string]$Root,[string]$Relative) $base=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar; $target=[IO.Path]::GetFullPath((Join-Path $Root $Relative)); if (!$target.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes its runtime root' }; return $target }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$writePrefix=[IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
if (!$OutputDirectory.StartsWith($writePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must remain in the approved scratch directory' }
if ((Test-Path -LiteralPath $OutputDirectory) -and (Get-ChildItem -LiteralPath $OutputDirectory -Force | Select-Object -First 1)) { throw 'Use a new empty output directory' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& (Join-Path $PSScriptRoot 'build-offline.ps1') -ScratchRoot $ScratchRoot
$toolDll=Join-Path $ScratchRoot 'bin\HcDeviceCoreTrim.dll'
$policy=Join-Path $PSScriptRoot 'profiles\sdl-passive-timer.policy.json'
$sourceManifest=Join-Path $InputRuntime 'HandheldCompanion.runtime.json'
$manifestHash=Hash $sourceManifest
$manifest=Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
$sourceHc=Join-Path $InputRuntime 'HandheldCompanion.dll'
$sourceHash=Hash $sourceHc
$expectedPolicy=Get-Content -LiteralPath $policy -Raw | ConvertFrom-Json
if ($sourceHash -ne $expectedPolicy.InputSha256) { throw 'HC input differs from the approved c0e720 baseline' }
$inputFiles=@(); $frozen=Join-Path $OutputDirectory 'input-snapshot'
New-Item -ItemType Directory -Path $frozen | Out-Null
foreach ($entry in $manifest.files) {
    $path=Safe-Child $InputRuntime $entry.path
    if ((Hash $path) -ne $entry.sha256) { throw ('Input manifest hash mismatch: '+$entry.path) }
    $target=Safe-Child $frozen $entry.path
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $path -Destination $target
    if ((Hash $target) -ne $entry.sha256) { throw 'Input changed while freezing snapshot' }
    $inputFiles += [pscustomobject]@{Path=$entry.path;Sha256=$entry.sha256;Bytes=(Get-Item -LiteralPath $target).Length;FileVersion=(Get-Item -LiteralPath $target).VersionInfo.FileVersion}
}
if ((Hash $sourceManifest) -ne $manifestHash) { throw 'Source manifest changed while freezing snapshot' }
Copy-Item -LiteralPath $sourceManifest -Destination (Join-Path $frozen 'HandheldCompanion.runtime.json')
$inputFiles | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'input-fingerprints.json') -Encoding utf8
$sourceHc=Join-Path $frozen 'HandheldCompanion.dll'
Invoke-Dotnet -Argv @($toolDll,'sdl-timer-audit',$sourceHc,(Join-Path $OutputDirectory 'constructor-chain-audit.json'))
$candidate=Join-Path $OutputDirectory 'candidate'
Invoke-Dotnet -Argv @($toolDll,'sdl-timer-patch',$sourceHc,$policy,$candidate) -Log (Join-Path $OutputDirectory 'candidate-build.log')
$candidateDll=Join-Path $candidate 'HandheldCompanion.dll'; $candidateHash=Hash $candidateDll
$runtime=Join-Path $OutputDirectory 'runtime-no-sdl';$baselineRuntime=Join-Path $OutputDirectory 'baseline-runtime-no-sdl'
foreach ($root in @($runtime,$baselineRuntime)) {
    New-Item -ItemType Directory -Path $root | Out-Null
    foreach ($entry in $manifest.files) {
        if ($entry.path.Replace('\','/').Equals('SDL3.dll',[StringComparison]::OrdinalIgnoreCase)) { continue }
        $target=Safe-Child $root $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath (Safe-Child $frozen $entry.path) -Destination $target
    }
    if (Get-ChildItem -LiteralPath $root -Recurse -Filter SDL3.dll) { throw 'SDL3.dll must be physically absent' }
}
Copy-Item -LiteralPath $candidateDll -Destination (Join-Path $runtime 'HandheldCompanion.dll')
$manifest.files=@($manifest.files | Where-Object { !$_.path.Replace('\','/').Equals('SDL3.dll',[StringComparison]::OrdinalIgnoreCase) })
$sourceRuntimeId=$manifest.runtimeId
$manifest.runtimeId=$sourceRuntimeId+'-NOSDL-BASELINE-NEGATIVE'
$manifest | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $baselineRuntime 'HandheldCompanion.runtime.json') -Encoding utf8
foreach ($entry in $manifest.files) { if ($entry.path -eq 'HandheldCompanion.dll') { $entry.sha256=$candidateHash } }
$manifest.runtimeId='YMCC-DEVICECORE-SDL-PASSIVE-TIMER-20261018-'+$candidateHash.Substring(0,16)
$manifest.derivative | Add-Member -NotePropertyName PassiveTimerPatch -NotePropertyValue ([pscustomobject]@{SourceHcSha256=$sourceHash;SourceManifestSha256=$manifestHash;PolicySha256=(Hash $policy);Clock='Stopwatch.GetTimestamp/Frequency, unchecked uint64 raw QPC units';ProbeDirectoryOnly=$true;FullSdlNativeClosureApproved=$false}) -Force
$manifest.derivative.runtimeValidationPending=$true
$manifest | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
$validation=Join-Path $OutputDirectory 'validation'; New-Item -ItemType Directory -Path $validation | Out-Null
foreach ($name in @('SdlTimerProbe','PathLoadProbe','CallbackPathProbe','ModernHeadlessProbe')) {
    $project=Join-Path $PSScriptRoot ('probes\'+$name+'\'+$name+'.csproj')
    $obj=Join-Path $OutputDirectory ('probe-build\'+$name+'\obj\'); $bin=Join-Path $OutputDirectory ('probe-build\'+$name+'\bin\')
    Invoke-Dotnet -Argv @('build',$project,('-p:BaseIntermediateOutputPath='+$obj),('-p:OutputPath='+$bin),'--source',(Join-Path $ScratchRoot 'packages'),'--nologo','-v:q') -Log (Join-Path $validation ($name+'.build.log'))
}
$timerProbe=Join-Path $OutputDirectory 'probe-build\SdlTimerProbe\bin\SdlTimerProbe.dll'
# Each invocation creates a distinct OS process. Neither baseline nor candidate can inherit loaded SDL modules.
Invoke-Dotnet -Argv @($timerProbe,$baselineRuntime,$sourceHash,'baseline',(Join-Path $validation 'baseline-prefix-no-sdl.json')) -Log (Join-Path $validation 'baseline-prefix-no-sdl.log')
Invoke-Dotnet -Argv @((Join-Path $OutputDirectory 'probe-build\PathLoadProbe\bin\PathLoadProbe.dll'),$runtime,(Join-Path $runtime 'Microsoft.Windows.SDK.NET.dll'),(Join-Path $validation 'path-load-gettypes.json')) -Log (Join-Path $validation 'path-load-gettypes.log')
Invoke-Dotnet -Argv @((Join-Path $OutputDirectory 'probe-build\CallbackPathProbe\bin\CallbackPathProbe.dll'),(Join-Path $runtime 'HandheldCompanion.dll'),$candidateHash,(Join-Path $ScratchRoot 'callback-original-path-golden.json'),(Join-Path $validation 'actual-callback-path-comparison.json')) -Log (Join-Path $validation 'actual-callback-path-comparison.log')
Invoke-Dotnet -Argv @((Join-Path $OutputDirectory 'probe-build\ModernHeadlessProbe\bin\ModernHeadlessProbe.dll'),$runtime,'538ea1f68f629a04',(Join-Path $validation 'modern-headless-construction.json')) -Log (Join-Path $validation 'modern-headless-construction.log')
Invoke-Dotnet -Argv @($timerProbe,$runtime,$candidateHash,'candidate',(Join-Path $validation 'timer-prefix-no-sdl.json')) -Log (Join-Path $validation 'timer-prefix-no-sdl.log')
$baseline=Get-Content -LiteralPath (Join-Path $validation 'baseline-prefix-no-sdl.json') -Raw | ConvertFrom-Json
$timer=Get-Content -LiteralPath (Join-Path $validation 'timer-prefix-no-sdl.json') -Raw | ConvertFrom-Json
$pathResult=Get-Content -LiteralPath (Join-Path $validation 'path-load-gettypes.json') -Raw | ConvertFrom-Json
$callback=Get-Content -LiteralPath (Join-Path $validation 'actual-callback-path-comparison.json') -Raw | ConvertFrom-Json
$modern=Get-Content -LiteralPath (Join-Path $validation 'modern-headless-construction.json') -Raw | ConvertFrom-Json
if ($timer.Status -ne 'passed' -or $baseline.Status -ne 'passed' -or !$baseline.ExpectedBaselineFailure -or $baseline.ProcessId -eq $timer.ProcessId -or $timer.NativeAttemptCount -ne 0 -or @($timer.ProcessModulesBefore).Count -ne 0 -or @($timer.ProcessModulesAfter).Count -ne 0 -or @($timer.SdlDllFiles).Count -ne 0 -or $timer.HcAllGetTypes -ne $pathResult.hcAllTypes -or $callback.status -ne 'passed' -or @($callback.checks).Count -ne 22 -or $modern.Status -ne 'passed' -or $modern.ConstructedCount -ne 7 -or $modern.NativeImportsDenied -ne 0) { throw 'A required actual runtime gate failed' }
$totalBytes=0L
foreach($entry in $manifest.files) { $path=Safe-Child $runtime $entry.path; if((Hash $path) -ne $entry.sha256) {throw 'Final probe runtime manifest mismatch'}; $totalBytes += (Get-Item -LiteralPath $path).Length }
$manifest.derivative.runtimeValidationPending=$false
$manifest | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $runtime 'HandheldCompanion.runtime.json') -Encoding utf8
$recordPath=Join-Path $candidate 'CANDIDATE.json';$record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
$record.Status='READY-FOR-HEADLESS-PASSIVE-TIMER-INTEGRATION';$record.RuntimeValidationPending=$false
$record | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $recordPath -Encoding utf8
$delivery=[pscustomobject]@{
    Status=$record.Status; RuntimeId=$record.RuntimeId
    Replacement=[pscustomobject]@{Path='HandheldCompanion.dll';File=$candidateDll;Bytes=(Get-Item -LiteralPath $candidateDll).Length;Sha256=$candidateHash;SourceSha256=$sourceHash;AssemblyName='HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null';Mvid=$record.Mvid;StrongName='unsigned';Authenticode='NotSigned'}
    OtherReplacements=@();ModernAndControlsUnchanged=$true;SdkUnchanged=$true
    PolicyPath=$policy;PolicySha256=(Hash $policy);InputManifestSha256=$manifestHash
    ProbeRuntimePath=$runtime;ProbeRuntimeFiles=$manifest.files.Count;ProbeRuntimeBytes=$totalBytes;ProbeManifestSha256=(Hash (Join-Path $runtime 'HandheldCompanion.runtime.json'))
    Gates=[pscustomobject]@{HcAllGetTypes=$timer.HcAllGetTypes;Callbacks=@($callback.checks).Count;ModernPureConstructions=$modern.ConstructedCount;CandidateProcessId=$timer.ProcessId;BaselineProcessId=$baseline.ProcessId;CandidateNativeAttempts=$timer.NativeAttemptCount;BaselineNativeAttempts=$baseline.NativeAttemptCount;NoSdlNativeModules=$true;PhysicallyNoSdl3Dll=$true;RawQpcBrackets=128;MonotonicSamples=10000}
    SafeExecutionBoundary='Replay actual 57-byte SDLController default field-initializer IL on an uninitialized DummyXbox360Controller object; stop before IController base constructor. No full controller, device, ManagerFactory/ControllerManager cctor, SDLInit, Host or window execution.'
    FullDummyConstructorValidated=$false;RemainingSdlNativeClosureApproved=$false;SdlManagedMetadataRetained=$true
    Integration='Replace HC only; owner regenerates formal source pins/profile/manifest. Physical no-SDL runtime is a scoped probe directory; broader SDL removal awaits the parallel consumer audit. Do not replace the production runtime wholesale.'
}
$delivery | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'DELIVERY.json') -Encoding utf8
Write-Output ('DELIVERY '+(Join-Path $OutputDirectory 'DELIVERY.json'))