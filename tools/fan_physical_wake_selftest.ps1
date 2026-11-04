param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot,[string]$NativeSourcePath,[switch]$CallersOnly)
$ErrorActionPreference='Stop'
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $env:TEMP ('fan-physical-wake-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
# NativeSourcePath is an explicit private Evidence mutation input; default is production.
$nativePath=if($NativeSourcePath){[IO.Path]::GetFullPath($NativeSourcePath)}else{Join-Path $ProjectRoot 'native/main.cpp'}
$headerPath=Join-Path $ProjectRoot 'native/fan_physical_wake.h'
$fixturePath=Join-Path $ProjectRoot 'tools/fan_physical_wake_selftest.cpp'
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
$queue=Get-ProductionFunction 'static void fanHostQueueSuspend(const char* reason,unsigned long long generation) {'
$publish=Get-ProductionFunction 'static void fanHostPublishPhysicalWake(unsigned long long generation) noexcept {'
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
if($publish -match 'commitPowerResume|g_powerLifecycle.store|g_powerGeneration.store|gamepad.restart|scheduleResumeTransaction'){throw 'private Fan wake changed shared recovery'}
foreach($needle in @('g_fanPhysicalWake.recordSleep(generation,fact->fileTime)','g_fanPhysicalWake.recordWake(generation,fact->fileTime','sgSleepRetryActive()))','fanHostQueueSuspend("kernel-power-506-physical",fanGeneration)','next=g_fanPhysicalWake.completeClose(current)','ipc_on("power.fanLifecycle"','generation != currentFanPowerGeneration()')){if(-not $native.Contains($needle)){throw "production caller missing: $needle"}}
$utf8=New-Object Text.UTF8Encoding($false)
$queuePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_queue.inc'
$publishPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_publish.inc'
$factPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_fact.inc'
$gatesPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_gates.inc'
$lifecyclePath=Join-Path $EvidenceRoot 'fan_physical_wake_native_lifecycle.inc'
$manualPath=Join-Path $EvidenceRoot 'fan_physical_wake_native_manual.inc'
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
& $exe @($(if($CallersOnly){'--callers-only'})) *> (Join-Path $EvidenceRoot 'gate.out');$code=$LASTEXITCODE
Get-Content (Join-Path $EvidenceRoot 'gate.out') -Tail 2
if($code -ne 0){throw 'physical gate failed'}
@{
    scope='production-Fan-physical-cycle-and-serialized-Close-owner; extracted-production-queue-publisher-kernel-fact-Fan-gates-and-manual-lifecycle-IPCs; no hardware'
    exit=$code;headerSha256=$headerSha;nativeSourcePath=$nativePath;nativeSha256=$nativeSha;fixtureSha256=$fixtureSha;runnerSha256=$runnerSha;jsonSha256=$jsonSha
    extractedSources=@(@{path=$queuePath;sha256=(Get-FileHash -LiteralPath $queuePath -Algorithm SHA256).Hash},@{path=$publishPath;sha256=(Get-FileHash -LiteralPath $publishPath -Algorithm SHA256).Hash},@{path=$factPath;sha256=(Get-FileHash -LiteralPath $factPath -Algorithm SHA256).Hash},@{path=$gatesPath;sha256=(Get-FileHash -LiteralPath $gatesPath -Algorithm SHA256).Hash},@{path=$lifecyclePath;sha256=(Get-FileHash -LiteralPath $lifecyclePath -Algorithm SHA256).Hash},@{path=$manualPath;sha256=(Get-FileHash -LiteralPath $manualPath -Algorithm SHA256).Hash})
    scheduler='deterministic-deferred-production-worker; original concurrent header assertion uses real threads'
    limits='external Windows/HTTP/Host/shared-commit dependencies are no-I/O stubs; real local JSON and virtual clock; no device or shared-resource acceptance claim'
}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
