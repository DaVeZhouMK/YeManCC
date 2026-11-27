param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot,[string]$NativeSourcePath,[switch]$CallersOnly,[switch]$SharedPowerOnly,[switch]$WakeDiagnosticsOnly,[switch]$SharedInputAdmission,[switch]$S4WireOnly)
$ErrorActionPreference='Stop'
if(@($CallersOnly,$SharedPowerOnly,$WakeDiagnosticsOnly,$SharedInputAdmission,$S4WireOnly).Where({$_}).Count -gt 1){throw 'Explicit selections are mutually exclusive'}
if($SharedInputAdmission -and (-not $EvidenceRoot -or (Test-Path -LiteralPath $EvidenceRoot))){throw 'Shared input admission requires a fresh explicit evidence root'}
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $env:TEMP ('fan-physical-wake-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
# NativeSourcePath is an explicit private Evidence mutation input; default is production.
$nativePath=if($NativeSourcePath){[IO.Path]::GetFullPath($NativeSourcePath)}else{Join-Path $ProjectRoot 'native/main.cpp'}
$headerPath=Join-Path $ProjectRoot 'native/fan_physical_wake.h'
$fixturePath=Join-Path $ProjectRoot 'tools/fan_physical_wake_selftest.cpp'
$sharedHeaders=@('native/sleep_power_state.h','native/sleep_power_observation.h')|ForEach-Object{ $p=Join-Path $ProjectRoot $_;@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash} }
$runnerPath=$PSCommandPath
$jsonPath=Join-Path $ProjectRoot 'deps/json/json.hpp'
$jsonSha=(Get-FileHash -LiteralPath $jsonPath -Algorithm SHA256).Hash
$nativeBytes=[IO.File]::ReadAllBytes($nativePath)
$native=[Text.Encoding]::UTF8.GetString($nativeBytes)
$hasher=[Security.Cryptography.SHA256]::Create()
try{$nativeSha=([BitConverter]::ToString($hasher.ComputeHash($nativeBytes))).Replace('-','')}finally{$hasher.Dispose()}
$headerSha=(Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash
$fixtureSha=(Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash
$runnerSha=(Get-FileHash -LiteralPath $runnerPath -Algorithm SHA256).Hash
function Get-ProductionFunction([string]$signature){
    $start=$native.IndexOf($signature,[StringComparison]::Ordinal)
    if($start -lt 0 -or $native.IndexOf($signature,$start+1,[StringComparison]::Ordinal) -ge 0){throw "production function missing/ambiguous: $signature"}
    # Native function braces close at column zero; nested scopes are indented.
    # Preserve the body verbatim, including std::thread and all guard branches.
    $end=$native.IndexOf("`n}",$start,[StringComparison]::Ordinal)
    if($end -le $start){throw "production function end missing: $signature"}
    return $native.Substring($start,$end+2-$start)
}
function Get-ProductionIpcBody([string]$signature){
    $start=$native.IndexOf($signature,[StringComparison]::Ordinal)
    if($start -lt 0 -or $native.IndexOf($signature,$start+1,[StringComparison]::Ordinal) -ge 0){throw "production IPC missing/ambiguous: $signature"}
    $open=$native.IndexOf('{',$start)
    $end=$native.IndexOf("`n    });",$open,[StringComparison]::Ordinal)
    if($end -le $open){throw "production IPC body end missing: $signature"}
    return $native.Substring($open+1,$end-$open-1)
}
$gates=(Get-ProductionFunction 'static bool fanHostPhysicalWakeCurrent() {')+"`n"+(Get-ProductionFunction 'static bool fanHostPowerMayWrite() {')+"`n"+(Get-ProductionFunction 'static bool fanHostPowerMayResume() {')
$lifecycle=Get-ProductionIpcBody '    ipc_on("power.fanLifecycle", [](const json&) -> json {'
$manual=Get-ProductionIpcBody '    ipc_on("power.fanManualWake", [](const json& a) -> json {'
$sharedSnapshot=(Get-ProductionFunction 'static json sharedPowerLifecycleSnapshot() {')+"`n"+
    (Get-ProductionFunction 'static void publishSharedFanWake(unsigned long long generation) noexcept {')
$fanDeliveryAnchor='static constexpr UINT_PTR kSharedFanWakeDeliveryTimer='
if($native.Contains($fanDeliveryAnchor)){
    $deliveryStart=$native.IndexOf($fanDeliveryAnchor,[StringComparison]::Ordinal)
    $deliveryPublish='static void publishSharedFanWake(unsigned long long generation) noexcept {'
    $publishStart=$native.IndexOf($deliveryPublish,[StringComparison]::Ordinal)
    if($publishStart -le $deliveryStart){throw 'production Fan delivery block is out of order'}
    $sharedSnapshot=(Get-ProductionFunction 'static json sharedPowerLifecycleSnapshot() {')+"`n"+
        $native.Substring($deliveryStart,$publishStart-$deliveryStart)+
        (Get-ProductionFunction $deliveryPublish)
}
foreach($symbol in @('g_gamepadUiWakeGeneration','g_gameWakeRecoveryRevision')){
    if(!$sharedSnapshot.Contains($symbol)){continue}
    $wakeState=[regex]::Matches($native,('(?m)^static std::atomic<uint64_t> '+[regex]::Escape($symbol)+'\{[^\r\n]+;'))
    if($wakeState.Count -ne 1){throw "production wake projection storage missing/ambiguous: $symbol"}
    $sharedSnapshot=$wakeState[0].Value+"`n"+$sharedSnapshot
}
$queueWithContinuation='static void fanHostQueueSuspend(const char* reason,unsigned long long generation,bool resumeUnstarted) {'
if($native.Contains($queueWithContinuation)){
    $queueDeclaration='static void fanHostQueueSuspend(const char* reason, unsigned long long generation, bool resumeUnstarted = false);'
    if(-not $native.Contains($queueDeclaration)){throw 'production Fan Close declaration missing'}
    $queue=$queueDeclaration+"`n"+(Get-ProductionFunction $queueWithContinuation)
}else{
    $queue=Get-ProductionFunction 'static void fanHostQueueSuspend(const char* reason,unsigned long long generation) {'
}
$guard='static bool fanLeaseKeepaliveReceiptCurrentLocked('
if($native.Contains($guard)){
    $disarm=Get-ProductionFunction 'static void fanLeaseKeepaliveDisarm(const char* reason, const std::string* expectedLease,'
    $clear='static bool fanLeaseKeepaliveClearLocked() {'
    if($native.Contains($clear)){
        $disarm=(Get-ProductionFunction $clear)+"`n"+(Get-ProductionFunction 'static void fanLeaseKeepaliveLogDisarm(')+"`n"+$disarm
    }
    $queue='static void fanLeaseKeepaliveDisarm(const char*, const std::string* = nullptr, unsigned long long = 0, const FanHttpPowerAdmission* = nullptr);'+"`n"+(Get-ProductionFunction $guard)+"`n"+$disarm+"`n"+$queue
}else{
    $disarm=Get-ProductionFunction 'static void fanLeaseKeepaliveDisarm(const char* reason, const std::string* expectedLease) {'
    $queue='static void fanLeaseKeepaliveDisarm(const char*, const std::string* = nullptr);'+"`n"+$disarm+"`n"+$queue
}
$delivery=([regex]::Match($native,'(?m)^enum class FanReadyDelivery \{[^\r\n]+\};')).Value
if(-not $delivery){throw 'production Fan delivery receipt missing'}
$publish=Get-ProductionFunction 'static FanReadyDelivery fanHostPublishPhysicalWake(unsigned long long generation, bool refreshAdmission) noexcept {'
$publishDeclaration='static FanReadyDelivery fanHostPublishPhysicalWake(unsigned long long generation, bool refreshAdmission = false) noexcept;'
if(-not $native.Contains($publishDeclaration)){throw 'production Fan publisher declaration missing'}
$publish=$delivery+"`n"+$publishDeclaration+"`n"+$publish
$closeContinuation='static void fanHostContinueUnstartedClose(unsigned long long generation) {'
if($native.Contains($closeContinuation)){
    $publish=(Get-ProductionFunction $closeContinuation)+"`n"+$publish
}
$publish+="`n"+(Get-ProductionFunction 'static FanReadyDelivery fanHostScheduleEmergencyResume(unsigned long long sharedGeneration) {')
$s4Admit=Get-ProductionFunction 'static bool sgAdmitS4Wake(unsigned long long callbackGeneration,ULONGLONG eventFileTime'
$s4Handler=[regex]::Match($native,'(?ms)^    case WM_SG_S4_WAKE:(?<body>.*?)^    case WM_SG_RESUME_GAME_FOCUS:').Groups['body'].Value
if(!$s4Handler){throw 'production S4 handler missing'}
if($s4Handler.Contains('sgArmS4WakeRedelivery(g_hwnd)')){
    $deliveryStart=$native.IndexOf('static std::mutex g_sgFailedS4Mutex;',[StringComparison]::Ordinal)
    $deliveryAnchor=if($native.Contains('static void sgPostKernelWake(')){'static void sgPostKernelWake('}else{'static void sgPostS4Wake('}
    $deliveryEnd=$native.IndexOf($deliveryAnchor,[StringComparison]::Ordinal)
    if($deliveryStart -lt 0 -or $deliveryEnd -le $deliveryStart){throw 'production S4 delivery state missing'}
    $publish+="`nusing HWND=int;`nstatic void sleepPowerArmRedelivery(HWND){throw ::std::logic_error(`"unexpected window timer in Fan physical fixture`");}`n"
    $publish+=$native.Substring($deliveryStart,$deliveryEnd-$deliveryStart)
    $publish+=(Get-ProductionFunction 'static void sgArmS4WakeRedelivery(HWND window) {')+"`n"
    $publish+=(Get-ProductionFunction 'static void sgPostKernelWake(HWND window,')+"`n"
    $publish+=(Get-ProductionFunction 'static void sgRedeliverS4Wake(HWND window) {')+"`n"
}
$entryRecorder='static void fanHostObserveKernelEntry(unsigned long long generation, ULONGLONG eventFileTime) {'
$wakeDelivery='static void fanHostRequestPhysicalWakeDelivery(unsigned long long generation) {'
if($native.Contains($wakeDelivery)){$publish+="`n"+(Get-ProductionFunction $wakeDelivery)}
if($native.Contains($entryRecorder)){$publish+="`n"+(Get-ProductionFunction $entryRecorder)}
$publish+="`n"+(Get-ProductionFunction 'static void fanHostObserveKernelExit(')
if($native.Contains('static ymcc::fan_wake::Observation::OffRequest fanStartupOffRequest(')){
    $offRequest=Get-ProductionFunction 'static ymcc::fan_wake::Observation::OffRequest fanStartupOffRequest('
    $documentInput=if($offRequest.Contains('g_gyroTelemetryDocument')){'g_gyroTelemetryDocument'}else{'g_webviewNavigationId'}
    $publish+="`nstatic ::std::atomic<unsigned long long> g_webviewGeneration{1};`nstatic ULONGLONG $documentInput=1;`n"+$offRequest
}
$publish+="`n"+(Get-ProductionFunction 'static bool fanStartupWakeCurrent(')+"`n"+(Get-ProductionFunction 'static json fanClaimStartupWake(')
$sharedSnapshot='static bool fanStartupWakeCurrent(const ymcc::sleeppower::SharedPowerV1&,unsigned long long,ULONGLONG);'+"`n"+
    (Get-ProductionFunction 'static unsigned long long fanPublishedWakeGeneration(')+"`n"+$sharedSnapshot
$s0Case=[regex]::Match($native,'(?ms)^    case WM_SG_S0_WAKE: \{(?<before>.*?)        const auto applySharedWakePolicy=\[&\]\(\) -> LRESULT \{.*?^        \};\r?\n(?<after>.*?)^    \}\r?$')
if(!$s0Case.Success){throw 'production coordinator S0 dispatch wrapper missing'}
$publish+="`nstatic LRESULT fanSharedS0(WPARAM w, LPARAM l) {"+$s0Case.Groups['before'].Value+"const auto applySharedWakePolicy=[&]() -> LRESULT {fanStartupEligible=fixtureAcceptUserS0&&w==1;return fixtureSharedS0Policy(w,l);};`n"+$s0Case.Groups['after'].Value+"}`n"
$readyDispatch=[regex]::Match($native,'(?ms)^    case WM_POWER_RESUME_READY: \{(?<body>.*?)^    \}\r?$')
$publicFan=[regex]::Match($native,'(?ms)^    constexpr unsigned fanBit=512;(?<body>.*?)^    // A cancelled suspend can publish admitted Ready')
$cancelTail=[regex]::Match($native,'(?ms)^    deliverReady\("input-host-rearm", \[&\]\{inputHostRequestPowerRearm\(nativeGeneration, "sleep-intent-canceled"\);\}\);\r?\n(?<body>.*?)^\}\r?\n\r?\n// Idle owns no periodic wakeup')
if(!$readyDispatch.Success -or !$publicFan.Success -or !$cancelTail.Success){throw 'production public Ready dispatch, Fan projection or cancellation tail missing'}
$publicReadyCode="`nstatic LRESULT fanSharedReadyDispatch(WPARAM w, LPARAM l) {"+$readyDispatch.Groups['body'].Value+"}`n"
$publicReadyCode+="`nstatic void fanCancelReadyTail(unsigned long long nativeGeneration) {`n"+@'
    const auto abortCurrent=[&]{return nativeGeneration==currentPowerGeneration() && !sharedPowerPending && !g_exitRequested.load();};
    const auto deliverReady=[](const char*,auto&& consume){consume();};
'@+"`n"+$cancelTail.Groups['body'].Value+"}`n"
$publicReadyCode+="`nstatic void fanSharedReadyProjection() {`n"+@'
    using Power = ymcc::sleeppower::SharedPowerV1;
    Power power;
    const bool powerKnown=captureSharedPowerLifecycle(power);
    const bool admitted=powerKnown&&ymcc::sleeppower::powerAdmitted(power);
    const auto readyCurrent=[&]{return admitted&&ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower,power);};
    bool readyDeferred=false;
    auto& delivery=fixtureFanProjection;
    const auto dispatch=[&](unsigned bit,const char*,bool,auto&& consume){
        if(!(delivery.attempted&bit)){delivery.attempted|=bit;consume();}
        return true;
    };
    constexpr unsigned fanBit=512;
'@+"`n"+$publicFan.Groups['body'].Value+"}`n"
$s4Recorder='static void fanHostRecordSharedHibernateWake(unsigned long long generation, ULONGLONG eventFileTime) {'
if($native.Contains($s4Recorder)){$publish+="`n"+(Get-ProductionFunction $s4Recorder)}
$criticalRecorder='static void fanHostRecordSharedCriticalWake(uint64_t receiptSequence, ULONGLONG receiptTime) {'
$criticalHandler=[regex]::Match($native,'(?ms)^        else if \(w == PBT_APMRESUMECRITICAL\) \{\r?\n(?<body>.*?)^        \}\r?$').Groups['body'].Value
if(!$criticalHandler){throw 'production critical resume branch missing'}
if($native.Contains($criticalRecorder)){$publish+="`n"+(Get-ProductionFunction $criticalRecorder)}
$publish+="`nstatic void fanCriticalResume(FanCriticalReceipt powerReceipt) {`n"+$criticalHandler+"}`n"
$workItem=[regex]::Match($native,'(?ms)^struct SgWorkItem \{.*?^\};').Value
if(!$workItem){throw 'production wake item missing'}
$publish+="`n"+$workItem+"`n"+$s4Admit+"`nstatic long fanSharedS4(WPARAM w, LPARAM l) {`n"+$s4Handler+"}`n"
# Extract the original switch case body, removing only its case label/braces so
# the same statements compile inside the fixture's function with LPARAM l.
$factSignature='    case WM_FAN_KERNEL_POWER_FACT: {'
$factStart=$native.IndexOf($factSignature,[StringComparison]::Ordinal)
if($factStart -lt 0 -or $native.IndexOf($factSignature,$factStart+1,[StringComparison]::Ordinal) -ge 0){throw 'production Fan kernel fact case missing/ambiguous'}
$factEnd=$native.IndexOf('    case WM_SG_S0_WAKE:',$factStart,[StringComparison]::Ordinal)
if($factEnd -le $factStart){throw 'production Fan kernel fact case end missing'}
$factCase=$native.Substring($factStart,$factEnd-$factStart)
$factOpen=$factCase.IndexOf('{')
$factClose=$factCase.LastIndexOf('}')
if($factClose -le $factOpen){throw 'production Fan kernel fact braces missing'}
$fact=$factCase.Substring($factOpen+1,$factClose-$factOpen-1)
if($publish -match 'commitPowerResume|g_powerLifecycle.store|g_powerGeneration.store|gamepad.restart|scheduleResumeTransaction'){throw 'private Fan wake changed shared recovery'}
$publish+=$publicReadyCode
$entryNeedles=@('fanHostObserveKernelEntry(currentPowerGeneration(),eventFileTime)','g_fanPhysicalWake.recordSleep(generation,eventFileTime)')
$wakeStamp='g_fanPhysicalWake\.recordWake\(generation,eventFileTime,sgSleepRetryActive\(\),startupUntil,\s+suspendSerial,boundary\.sequence\)'
if($native -notmatch $wakeStamp){throw 'production startup wake missing the original public boundary stamp'}
foreach($needle in ($entryNeedles + @('fanHostQueueSuspend("kernel-power-506-physical",fanGeneration)','next=g_fanPhysicalWake.completeClose(current)','ipc_on("power.fanLifecycle"','generation != currentFanPowerGeneration()'))){if(-not $native.Contains($needle)){throw "production caller missing: $needle"}}
$utf8=New-Object Text.UTF8Encoding($false)
$queuePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_queue.inc'
$publishPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_publish.inc'
$factPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_fact.inc'
$gatesPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_gates.inc'
$lifecyclePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_lifecycle.inc'
$manualPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_manual.inc'
$readyPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_ready.inc'
$readyStart=$native.IndexOf('static bool powerLifecycleMatches(', [StringComparison]::Ordinal)
$readyEnd=$native.IndexOf('static bool commitPowerResumeGate(', [StringComparison]::Ordinal)
if($readyStart -lt 0 -or $readyEnd -le $readyStart){throw 'production Ready receipt helpers missing'}
$ready=$native.Substring($readyStart,$readyEnd-$readyStart)
[IO.File]::WriteAllText($readyPath,$ready,$utf8)
$snapshotPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_shared_snapshot.inc'
$jointDefines=@()
$jointSources=@()
$cppStandard='/std:c++17'
if($SharedInputAdmission){
    $jointFrame=Get-ProductionFunction 'static bool inputHostFrameSharedPowerCurrent(InputFrameSubmission& frame) noexcept {'
    $jointFramePath=Join-Path $EvidenceRoot 'fan_shared_input_native_frame.inc'
    [IO.File]::WriteAllText($jointFramePath,$jointFrame,$utf8)
    $jointSources=@('tools/fan_shared_input_admission_cases.h','native/input_frame_submission.h','native/virtual_output_status.h','native/input_host_shared_power.h','native/sleep_power_revocation.h')|ForEach-Object{
        $p=Join-Path $ProjectRoot $_; @{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
    }
    $jointDefines=@('/DYMCC_FAN_SHARED_INPUT_ADMISSION')
    $cppStandard='/std:c++20'
}
[IO.File]::WriteAllText($snapshotPath,$sharedSnapshot,$utf8)
[IO.File]::WriteAllText($gatesPath,$gates,$utf8)
[IO.File]::WriteAllText($lifecyclePath,$lifecycle,$utf8)
[IO.File]::WriteAllText($manualPath,$manual,$utf8)
[IO.File]::WriteAllText($queuePath,$queue,$utf8)
[IO.File]::WriteAllText($publishPath,$publish,$utf8)
[IO.File]::WriteAllText($factPath,$fact,$utf8)
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){$eq=$line.IndexOf('=');if($eq -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$eq),$line.Substring($eq+1),'Process')}}
# Match the existing Fan transport runner when vcvars omits the installed SDK.
if(-not $env:WindowsSdkDir){$env:WindowsSdkDir='C:\Program Files (x86)\Windows Kits\10\'}
if(-not $env:WindowsSDKVersion -or $env:WindowsSDKVersion -eq '\'){$env:WindowsSDKVersion='10.0.26100.0\'}
$fanSdkInclude=Join-Path $env:WindowsSdkDir ('Include\'+$env:WindowsSDKVersion)
$fanSdkLibrary=Join-Path $env:WindowsSdkDir ('Lib\'+$env:WindowsSDKVersion)
if(-not (Test-Path -LiteralPath (Join-Path $fanSdkInclude 'ucrt\crtdbg.h'))){throw 'installed Windows SDK UCRT headers unavailable'}
$env:INCLUDE=(Join-Path $fanSdkInclude 'um')+';'+(Join-Path $fanSdkInclude 'shared')+';'+(Join-Path $fanSdkInclude 'ucrt')+';'+$env:INCLUDE
$env:LIB=(Join-Path $fanSdkLibrary 'um\x64')+';'+(Join-Path $fanSdkLibrary 'ucrt\x64')+';'+$env:LIB
$exe=Join-Path $EvidenceRoot 'physical-wake.exe'
& cl.exe /nologo $cppStandard /EHsc /utf-8 /DYMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS @jointDefines "/I$EvidenceRoot" ('/I'+(Split-Path -Parent $jsonPath)) $fixturePath "/Fo$EvidenceRoot\physical-wake.obj" "/Fe$exe" *> (Join-Path $EvidenceRoot 'build.out')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $EvidenceRoot 'build.out');throw 'physical gate compile failed'}
# Bind extraction and compilation to one source identity; source drift is not a PASS.
foreach($binding in @(@($nativePath,$nativeSha),@($headerPath,$headerSha),@($fixturePath,$fixtureSha),@($runnerPath,$runnerSha),@($jsonPath,$jsonSha))){if((Get-FileHash -LiteralPath $binding[0] -Algorithm SHA256).Hash -ne $binding[1]){throw "source changed during native fixture preparation: $($binding[0])"}}
if([IO.File]::ReadAllText($queuePath) -cne $queue -or [IO.File]::ReadAllText($publishPath) -cne $publish -or [IO.File]::ReadAllText($factPath) -cne $fact -or [IO.File]::ReadAllText($gatesPath) -cne $gates -or [IO.File]::ReadAllText($lifecyclePath) -cne $lifecycle -or [IO.File]::ReadAllText($manualPath) -cne $manual){throw 'extracted native caller bytes changed'}
foreach($binding in $sharedHeaders){if((Get-FileHash -LiteralPath $binding.path -Algorithm SHA256).Hash -ne $binding.sha256){throw 'Shared power header drift'}}
if([IO.File]::ReadAllText($snapshotPath) -cne $sharedSnapshot){throw 'extracted shared Fan projection bytes changed'}
if([IO.File]::ReadAllText($readyPath) -cne $ready){throw 'extracted Ready receipt helpers changed'}
$selection=if($CallersOnly){'--callers-only'}elseif($SharedPowerOnly){'--shared-power-only'}elseif($WakeDiagnosticsOnly){'--wake-diagnostics-only'}elseif($SharedInputAdmission){'--shared-input-admission'}elseif($S4WireOnly){'--s4-wire-only'}
& $exe @($selection | Where-Object {$_}) *> (Join-Path $EvidenceRoot 'gate.out');$code=$LASTEXITCODE
Get-Content (Join-Path $EvidenceRoot 'gate.out') -Tail 2
if($code -ne 0){throw 'physical gate failed'}
$joint=$null
if($SharedInputAdmission){
    $controlPath=Join-Path $EvidenceRoot 'shared-input-control.out'
    & $exe --shared-input-admission-control *> $controlPath
    $controlExit=$LASTEXITCODE
    $positiveText=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'gate.out') -Raw
    $controlText=Get-Content -LiteralPath $controlPath -Raw
    $failure='late frame cannot reuse an old scope after both consumers see Ready'
    $positive=@([regex]::Matches($positiveText,'(?m)^PASS ([^\r\n]+)') | ForEach-Object {$_.Groups[1].Value})
    $negativeFailures=@([regex]::Matches($controlText,'(?m)^FAIL ([^\r\n]+)') | ForEach-Object {$_.Groups[1].Value})
    $expectedControl=$positiveText.Replace("PASS $failure","FAIL $failure").Replace('failures=0 selected_suite=shared-input-admission','failures=1 selected_suite=shared-input-admission-control')
    if($controlExit -ne 1 -or $negativeFailures.Count -ne 1 -or $negativeFailures[0] -cne $failure -or $controlText -cne $expectedControl){throw 'unexpected shared input admission control result'}
    if([IO.File]::ReadAllText($jointFramePath) -cne $jointFrame){throw 'extracted input frame gate changed'}
    foreach($binding in $jointSources){if((Get-FileHash -LiteralPath $binding.path -Algorithm SHA256).Hash -cne $binding.sha256){throw 'shared input source drift'}}
    $joint=@{assertions=@($positive);controlExit=$controlExit;originalControlStatus='FAIL';failure=$failure;
        frameGate=@{path=$jointFramePath;sha256=(Get-FileHash -LiteralPath $jointFramePath -Algorithm SHA256).Hash};sources=$jointSources}
}
$receipt=@{
    scope='production-Fan-physical-cycle-and-serialized-Close-owner; extracted-production-queue-publisher-kernel-fact-Fan-gates-and-manual-lifecycle-IPCs; no hardware'
    exit=$code;headerSha256=$headerSha;nativeSourcePath=$nativePath;nativeSha256=$nativeSha;fixtureSha256=$fixtureSha;runnerSha256=$runnerSha;jsonSha256=$jsonSha
    extractedSources=@(@{path=$queuePath;sha256=(Get-FileHash -LiteralPath $queuePath -Algorithm SHA256).Hash},@{path=$publishPath;sha256=(Get-FileHash -LiteralPath $publishPath -Algorithm SHA256).Hash},@{path=$factPath;sha256=(Get-FileHash -LiteralPath $factPath -Algorithm SHA256).Hash},@{path=$gatesPath;sha256=(Get-FileHash -LiteralPath $gatesPath -Algorithm SHA256).Hash},@{path=$lifecyclePath;sha256=(Get-FileHash -LiteralPath $lifecyclePath -Algorithm SHA256).Hash},@{path=$manualPath;sha256=(Get-FileHash -LiteralPath $manualPath -Algorithm SHA256).Hash})
    sharedPowerHeaders=$sharedHeaders
    sharedFanProjection=@{path=$snapshotPath;sha256=(Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash}
    sharedReadyReceipt=@{path=$readyPath;sha256=(Get-FileHash -LiteralPath $readyPath -Algorithm SHA256).Hash}
    scheduler='deterministic-deferred-production-worker; original concurrent header assertion uses real threads'
    limits='external Windows/HTTP/Host/shared-commit dependencies are no-I/O stubs; real local JSON and virtual clock; no device or shared-resource acceptance claim'
}
if($SharedInputAdmission){$receipt.sharedInputAdmission=$joint}
$receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
