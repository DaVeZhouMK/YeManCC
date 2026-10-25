param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $env:TEMP ('fan-physical-wake-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$native=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/main.cpp'))
$start=$native.IndexOf('static void fanHostPublishPhysicalWake(unsigned long long generation) noexcept {')
$end=$native.IndexOf('static void fanHostScheduleEmergencyResume(unsigned long long sharedGeneration)', $start)
if($start -lt 0 -or $end -le $start){throw 'Fan-only publish entry missing'}
$publish=$native.Substring($start,$end-$start)
if($publish -match 'commitPowerResume|g_powerLifecycle.store|g_powerGeneration.store|gamepad.restart|scheduleResumeTransaction'){throw 'private Fan wake changed shared recovery'}
foreach($needle in @('g_fanPhysicalWake.recordSleep(generation,fact->fileTime)','g_fanPhysicalWake.recordWake(generation,fact->fileTime','sgSleepRetryActive(),fanActivityIntentPresent()','fanHostQueueSuspend("kernel-power-506-physical",fanGeneration)','g_fanPhysicalWake.beginClose(generation)','next=g_fanPhysicalWake.completeClose(current)','ipc_on("power.fanLifecycle"','generation != currentFanPowerGeneration()')){if(-not $native.Contains($needle)){throw "production caller missing: $needle"}}
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){$eq=$line.IndexOf('=');if($eq -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$eq),$line.Substring($eq+1),'Process')}}
$exe=Join-Path $EvidenceRoot 'physical-wake.exe'
& cl.exe /nologo /std:c++17 /EHsc /utf-8 (Join-Path $ProjectRoot 'tools/fan_physical_wake_selftest.cpp') "/Fo$EvidenceRoot\physical-wake.obj" "/Fe$exe" *> (Join-Path $EvidenceRoot 'build.out')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $EvidenceRoot 'build.out');throw 'physical gate compile failed'}
& $exe *> (Join-Path $EvidenceRoot 'gate.out');$code=$LASTEXITCODE
Get-Content (Join-Path $EvidenceRoot 'gate.out') -Tail 2
if($code -ne 0){throw 'physical gate failed'}
@{scope='production-Fan-physical-cycle-and-serialized-Close-owner; native-caller-shape; no hardware';exit=$code;headerSha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/fan_physical_wake.h')).Hash;nativeSha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/main.cpp')).Hash}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
