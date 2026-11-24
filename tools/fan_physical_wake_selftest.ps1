param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot,[string]$NativeSourcePath,[switch]$CallersOnly,[switch]$SharedPowerOnly,[switch]$WakeDiagnosticsOnly)
$ErrorActionPreference='Stop'
if(@($CallersOnly,$SharedPowerOnly,$WakeDiagnosticsOnly).Where({$_}).Count -gt 1){throw 'Explicit selections are mutually exclusive'}
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
if($sharedSnapshot.Contains('g_gamepadUiWakeGeneration')){
    $wakeState=[regex]::Matches($native,'(?m)^static std::atomic<uint64_t> g_gamepadUiWakeGeneration[^\r\n]+;')
    if($wakeState.Count -ne 1){throw 'production gamepad wake projection storage missing/ambiguous'}
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
$disarm=Get-ProductionFunction 'static void fanLeaseKeepaliveDisarm(const char* reason, const std::string* expectedLease) {'
$queue='static void fanLeaseKeepaliveDisarm(const char*, const std::string* = nullptr);'+"`n"+$disarm+"`n"+$queue
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
    $deliveryEnd=$native.IndexOf('static void sgPostS4Wake(',[StringComparison]::Ordinal)
    if($deliveryStart -lt 0 -or $deliveryEnd -le $deliveryStart){throw 'production S4 delivery state missing'}
    $publish+="`nusing HWND=int;`nstatic void sleepPowerArmRedelivery(HWND){throw ::std::logic_error(`"unexpected window timer in Fan physical fixture`");}`n"
    $publish+=$native.Substring($deliveryStart,$deliveryEnd-$deliveryStart)
    $publish+=(Get-ProductionFunction 'static void sgArmS4WakeRedelivery(HWND window) {')+"`n"
}
$s4Recorder='static void fanHostRecordSharedHibernateWake(unsigned long long generation, ULONGLONG eventFileTime) {'
if($native.Contains($s4Recorder)){$publish+="`n"+(Get-ProductionFunction $s4Recorder)}
$publish+="`n"+$s4Admit+"`nstatic long fanSharedS4(WPARAM w, LPARAM l) {`n"+$s4Handler+"}`n"
# Extract the original switch case body, removing only its case label/braces so
# the same statements compile inside the fixture's function with LPARAM l.
$factSignature='    case WM_FAN_KERNEL_POWER_FACT: {'
$factStart=$native.IndexOf($factSignature,[StringComparison]::Ordinal)
if($factStart -lt 0 -or $native.IndexOf($factSignature,$factStart+1,[StringComparison]::Ordinal) -ge 0){throw 'production Fan kernel fact case missing/ambiguous'}
$factEnd=$native.IndexOf('    case WM_FAN_PHYSICAL_WAKE_READY:',$factStart,[StringComparison]::Ordinal)
if($factEnd -le $factStart){throw 'production Fan kernel fact case end missing'}
$factCase=$native.Substring($factStart,$factEnd-$factStart)
$factOpen=$factCase.IndexOf('{')
$factClose=$factCase.LastIndexOf('}')
if($factClose -le $factOpen){throw 'production Fan kernel fact braces missing'}
$fact=$factCase.Substring($factOpen+1,$factClose-$factOpen-1)
$closeWakeStart=$native.IndexOf('    case WM_FAN_PHYSICAL_WAKE_READY:',$factEnd,[StringComparison]::Ordinal)
$closeWakeEnd=$native.IndexOf('    case WM_SG_S0_WAKE:', $closeWakeStart,[StringComparison]::Ordinal)
if($closeWakeStart -ne $factEnd -or $closeWakeEnd -le $closeWakeStart){throw 'production Fan Close completion caller missing'}
$closeWakeBody=$native.Substring($closeWakeStart,$closeWakeEnd-$closeWakeStart)
$fact+=' ' + "`n}`nstatic int fanCloseWake(WPARAM w) {" + $closeWakeBody.Substring($closeWakeBody.IndexOf(':')+1)
if($publish -match 'commitPowerResume|g_powerLifecycle.store|g_powerGeneration.store|gamepad.restart|scheduleResumeTransaction'){throw 'private Fan wake changed shared recovery'}
foreach($needle in @('g_fanPhysicalWake.recordSleep(generation,fact->fileTime)','g_fanPhysicalWake.recordWake(generation,fact->fileTime','sgSleepRetryActive()))','fanHostQueueSuspend("kernel-power-506-physical",fanGeneration)','next=g_fanPhysicalWake.completeClose(current)','ipc_on("power.fanLifecycle"','generation != currentFanPowerGeneration()')){if(-not $native.Contains($needle)){throw "production caller missing: $needle"}}
$utf8=New-Object Text.UTF8Encoding($false)
$queuePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_queue.inc'
$publishPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_publish.inc'
$factPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_fact.inc'
$gatesPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_gates.inc'
$lifecyclePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_lifecycle.inc'
$manualPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_manual.inc'
$snapshotPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_shared_snapshot.inc'
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
& cl.exe /nologo /std:c++17 /EHsc /utf-8 /DYMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS "/I$EvidenceRoot" ('/I'+(Split-Path -Parent $jsonPath)) $fixturePath "/Fo$EvidenceRoot\physical-wake.obj" "/Fe$exe" *> (Join-Path $EvidenceRoot 'build.out')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $EvidenceRoot 'build.out');throw 'physical gate compile failed'}
# Bind extraction and compilation to one source identity; source drift is not a PASS.
foreach($binding in @(@($nativePath,$nativeSha),@($headerPath,$headerSha),@($fixturePath,$fixtureSha),@($runnerPath,$runnerSha),@($jsonPath,$jsonSha))){if((Get-FileHash -LiteralPath $binding[0] -Algorithm SHA256).Hash -ne $binding[1]){throw "source changed during native fixture preparation: $($binding[0])"}}
if([IO.File]::ReadAllText($queuePath) -cne $queue -or [IO.File]::ReadAllText($publishPath) -cne $publish -or [IO.File]::ReadAllText($factPath) -cne $fact -or [IO.File]::ReadAllText($gatesPath) -cne $gates -or [IO.File]::ReadAllText($lifecyclePath) -cne $lifecycle -or [IO.File]::ReadAllText($manualPath) -cne $manual){throw 'extracted native caller bytes changed'}
foreach($binding in $sharedHeaders){if((Get-FileHash -LiteralPath $binding.path -Algorithm SHA256).Hash -ne $binding.sha256){throw 'Shared power header drift'}}
if([IO.File]::ReadAllText($snapshotPath) -cne $sharedSnapshot){throw 'extracted shared Fan projection bytes changed'}
& $exe @($(if($CallersOnly){'--callers-only'}elseif($SharedPowerOnly){'--shared-power-only'}elseif($WakeDiagnosticsOnly){'--wake-diagnostics-only'})) *> (Join-Path $EvidenceRoot 'gate.out');$code=$LASTEXITCODE
Get-Content (Join-Path $EvidenceRoot 'gate.out') -Tail 2
if($code -ne 0){throw 'physical gate failed'}
@{
    scope='production-Fan-physical-cycle-and-serialized-Close-owner; extracted-production-queue-publisher-kernel-fact-Fan-gates-and-manual-lifecycle-IPCs; no hardware'
    exit=$code;headerSha256=$headerSha;nativeSourcePath=$nativePath;nativeSha256=$nativeSha;fixtureSha256=$fixtureSha;runnerSha256=$runnerSha;jsonSha256=$jsonSha
    extractedSources=@(@{path=$queuePath;sha256=(Get-FileHash -LiteralPath $queuePath -Algorithm SHA256).Hash},@{path=$publishPath;sha256=(Get-FileHash -LiteralPath $publishPath -Algorithm SHA256).Hash},@{path=$factPath;sha256=(Get-FileHash -LiteralPath $factPath -Algorithm SHA256).Hash},@{path=$gatesPath;sha256=(Get-FileHash -LiteralPath $gatesPath -Algorithm SHA256).Hash},@{path=$lifecyclePath;sha256=(Get-FileHash -LiteralPath $lifecyclePath -Algorithm SHA256).Hash},@{path=$manualPath;sha256=(Get-FileHash -LiteralPath $manualPath -Algorithm SHA256).Hash})
    sharedPowerHeaders=$sharedHeaders
    sharedFanProjection=@{path=$snapshotPath;sha256=(Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash}
    scheduler='deterministic-deferred-production-worker; original concurrent header assertion uses real threads'
    limits='external Windows/HTTP/Host/shared-commit dependencies are no-I/O stubs; real local JSON and virtual clock; no device or shared-resource acceptance claim'
}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
