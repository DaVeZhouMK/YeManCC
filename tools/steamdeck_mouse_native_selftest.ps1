param([string]$OutputDirectory='G:\YeManCC-Work\_scratch\steamdeck-mouse-20261005\native-selftest', [string]$CompilerEnvironment='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat', [switch]$ReceiptStormOnly, [switch]$ReadObservationOnly, [switch]$PrepareOnly, [string]$CaseFilter="read-observation")
$ErrorActionPreference='Stop'
if($CaseFilter -notin @('read-observation','read-probe-owner-control','reconcile-identity','reconcile-after-get','reconcile-target-after-get','apply-identity','apply-post-set-control','apply-offline-guards','apply-target-control','apply-final-value-control','enqueue-identity','enqueue-stale-control')){throw 'Unknown or empty native mouse case filter'}
if($CaseFilter -ne 'read-observation' -and -not $ReadObservationOnly){throw 'Reconcile selection requires original ReadObservationOnly scope'}
$root=Split-Path $PSScriptRoot
$main=[IO.File]::ReadAllText((Join-Path $root 'native\main.cpp'))
$identitySource=[IO.File]::ReadAllText((Join-Path $root 'native\steam_session_observer.h'))
$identityStart=$identitySource.IndexOf('struct Session {');$identityEnd=$identitySource.IndexOf('using IdentityPublished',$identityStart)
if($identityStart -lt 0 -or $identityEnd -lt $identityStart){throw 'Original public Steam identity types missing'}
$identityTypes='namespace ymcc::steamsession {'+"`n"+$identitySource.Substring($identityStart,$identityEnd-$identityStart)+"`n}"
$identityCurrentStart=$identitySource.IndexOf('    bool sameIdentityEpoch(');$identityCurrentEnd=$identitySource.IndexOf('    // Called while constructing',$identityCurrentStart)
if($identityCurrentStart -lt 0 -or $identityCurrentEnd -lt $identityCurrentStart){throw 'Original public identity current method missing'}
$identityCurrent=$identitySource.Substring($identityCurrentStart,$identityCurrentEnd-$identityCurrentStart)
$start=$main.IndexOf('// SteamDeck desktop right-stick sensitivity.')
$end=$main.IndexOf('#include "steam_settings_runtime.h"',$start)
if($start -lt 0 -or $end -lt $start){throw 'Production SteamDeck mouse runtime block not found'}
$storm=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\receipt_storm.inc'))
if($ReceiptStormOnly -and $ReadObservationOnly){throw 'Select only one native mouse scope'}
if($PrepareOnly -and -not $ReadObservationOnly){throw 'PrepareOnly requires ReadObservationOnly'}
if($ReadObservationOnly){
 $presenceStart=$main.IndexOf('static bool sofSteamRunning() {')
 $presenceEnd=$main.IndexOf('// localconfig.vdf',$presenceStart)
 if($presenceStart -lt 0 -or $presenceEnd -lt $presenceStart){throw 'Production Steam presence block not found'}
 $ports=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\read_observation_ports.inc')).Replace('//__PRODUCTION_PRESENCE__',$main.Substring($presenceStart,$presenceEnd-$presenceStart))
 $cases=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\read_observation_cases.inc'))
 if($CaseFilter -eq 'read-probe-owner-control'){$cases='#define YMCC_SDM_READ_PROBE_OWNER_CONTROL 1'+"`n"+$cases}
 if($CaseFilter -in @('reconcile-identity','reconcile-after-get','reconcile-target-after-get')){$cases=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\reconcile_identity_cases.inc'));if($CaseFilter -eq 'reconcile-after-get'){$cases='#define YMCC_SDM_RECONCILE_AFTER_GET_ONLY 1'+"`n"+$cases};if($CaseFilter -eq 'reconcile-target-after-get'){$cases='#define YMCC_SDM_RECONCILE_TARGET_ONLY 1'+"`n"+$cases}}
 if($CaseFilter -in @('apply-identity','apply-post-set-control','apply-target-control','apply-final-value-control','enqueue-identity','enqueue-stale-control')){$cases=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\apply_identity_cases.inc'));if($CaseFilter -eq 'apply-post-set-control'){$cases='#define YMCC_SDM_APPLY_POST_SET_ONLY 1'+"`n"+$cases};if($CaseFilter -eq 'apply-target-control'){$cases='#define YMCC_SDM_APPLY_TARGET_ONLY 1'+"`n"+$cases};if($CaseFilter -eq 'apply-final-value-control'){$cases='#define YMCC_SDM_APPLY_FINAL_VALUE_ONLY 1'+"`n"+$cases}}
 if($CaseFilter -eq 'apply-offline-guards'){$cases=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\apply_offline_cases.inc'))}
 if($CaseFilter -in @('enqueue-identity','enqueue-stale-control')){$cases=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\enqueue_identity_cases.inc'));if($CaseFilter -eq 'enqueue-stale-control'){$cases='#define YMCC_SDM_ENQUEUE_STALE_ONLY 1'+"`n"+$cases}}
 $fixture='#define YMCC_SDM_READ_OBSERVATION_ONLY 1'+"`n"+[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\receipt_harness.cpp')).Replace('//__PRODUCTION_RUNTIME__',$main.Substring($start,$end-$start)).Replace('//__READ_OBSERVATION_PORTS__',$ports).Replace('//__READ_OBSERVATION_CASES__',$cases)
}elseif($ReceiptStormOnly){
 $fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\steamdeck-mouse\receipt_harness.cpp')).Replace('//__PRODUCTION_RUNTIME__',$main.Substring($start,$end-$start)).Replace('//__RECEIPT_STORM__',$storm)
}else{
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'steamdeck_mouse_native_fixture.cpp')).Replace('//__PRODUCTION_RUNTIME__',$main.Substring($start,$end-$start).Replace('static json steamDeckMouseSet(', 'static json fixtureMouseEnqueue('))
$overlayStart=$main.IndexOf('static std::atomic<bool> g_sofQueued{false};')
$overlayEnd=$main.IndexOf('// SteamDeck desktop right-stick sensitivity.',$overlayStart)
if($overlayStart -lt 0 -or $overlayEnd -lt $overlayStart){throw 'Production Steam overlay runtime block not found'}
$fixture=$fixture.Replace('//__PRODUCTION_OVERLAY_RUNTIME__',$main.Substring($overlayStart,$overlayEnd-$overlayStart).Replace('static void steamOverlayFixKick(', 'static void fixtureOverlayEnqueue('))
$liveStart=$main.IndexOf('static json steamLiveRequest(DWORD account, json request) {')
$liveEnd=$main.IndexOf('static std::atomic<bool> g_sofQueued{false};',$liveStart)
if($liveStart -lt 0 -or $liveEnd -lt $liveStart){throw 'Production Steam live preflight block not found'}
$fixture=$fixture.Replace('//__PRODUCTION_LIVE_PREFLIGHT__',$main.Substring($liveStart,$liveEnd-$liveStart))
$transport=[IO.File]::ReadAllText((Join-Path $root 'native\steam_live_cdp.h'))
$policyStart=$transport.IndexOf('inline bool canDefer(')
$policyEnd=$transport.LastIndexOf('}')
if($policyStart -lt 0 -or $policyEnd -lt $policyStart){throw 'Production Steam live deferral policy not found'}
$fixture=$fixture.Replace('//__PRODUCTION_DEFER_POLICY__',$transport.Substring($policyStart,$policyEnd-$policyStart))
$observerStart=$main.IndexOf('static void steamSettingsObserverStart() {')
$observerEnd=$main.IndexOf('static bool sofApplyValueToConfigText(const std::string& in, int target, std::string* out) {',$observerStart)
$fixture=$fixture.Replace('//__PRODUCTION_OBSERVER__',$main.Substring($observerStart,$observerEnd-$observerStart))

 $fixture=$fixture.Replace('//__RECEIPT_STORM__',$storm)
}

$fixture=$fixture.Replace('//__PUBLIC_IDENTITY_TYPES__',$identityTypes).Replace('//__PUBLIC_IDENTITY_CURRENT__',$identityCurrent)
if($fixture.Contains('//__PUBLIC_IDENTITY_')){throw 'Public identity fixture source was not paired'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source=Join-Path $OutputDirectory 'native-selftest.cpp'
if($ReadObservationOnly -and [IO.File]::Exists($source)){throw 'Native mouse generated source already exists'}
[IO.File]::WriteAllText($source,$fixture,[Text.UTF8Encoding]::new($false))
if($PrepareOnly){Write-Output $source;return}
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "$CompilerEnvironment" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" /I"$root\native" "$source" /Fo"$OutputDirectory\native-selftest.obj" /Fe"$OutputDirectory\native-selftest.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\native-selftest.exe" "$root\tools\fixtures\steamdeck-mouse\configset_controller_neptune.vdf" "$root\tools\fixtures\steamdeck-mouse\controller_neptune-100.vdf" "$root\tools\fixtures\steamdeck-mouse\controller_neptune-137.vdf"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "SteamDeck native selftest failed: $LASTEXITCODE"}
