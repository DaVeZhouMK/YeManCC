[CmdletBinding()]
param([string]$OutputRoot,[switch]$CompileNative)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $workspace=Split-Path (Split-Path (Split-Path $root -Parent) -Parent) -Parent
    $OutputRoot=Join-Path $workspace 'Build\Validation\GpuDedicatedMemory-20261013'
}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot|Out-Null
$paths=@('native\gpu_dedicated_memory_contract.h','native\gpu_dedicated_memory_runtime.h','native\main.cpp',
    'src\bridge\gpuDedicatedMemory.ts','src\components\GpuDedicatedMemorySettings.vue','src\views\SettingsView.vue',
    'tools\gpu_dedicated_memory_selftest.cpp','tools\gpu_dedicated_memory_channel_selftest.cpp','tools\gpu_dedicated_memory_binding_selftest.cpp',
    'tools\gpu_dedicated_memory_probe.cpp','tools\gpu_dedicated_memory_browser_selftest.mjs',
    'tools\gpu_dedicated_memory_windows_selftest.cpp','tools\fixtures\gpu-dedicated-memory-g3e-20261014.json','tools\gpu-dedicated-memory-export-guard.ps1','tools\verify-gpu-dedicated-memory.ps1')
$hashes=[ordered]@{}
foreach($p in $paths){$hashes[$p]=(Get-FileHash -LiteralPath (Join-Path $root $p) -Algorithm SHA256).Hash}
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envLines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE){throw 'vcvars64 failed'}
foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
function Compile([string[]]$Arguments,[string]$Log) {
    $output=@(& cl.exe @Arguments 2>&1);$code=$LASTEXITCODE
    $output|Set-Content -LiteralPath (Join-Path $OutputRoot $Log) -Encoding utf8
    if($code){$output|Write-Output;throw "compile failed: $Log / $code"}
}
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/DNOMINMAX','/DUNICODE','/D_UNICODE')
Compile ($common+@('/W4','/WX','/O2','/DNDEBUG',(Join-Path $root 'tools\gpu_dedicated_memory_selftest.cpp'),"/Fo$OutputRoot\contract.obj","/Fe$OutputRoot\contract.exe")) 'contract-compile.txt'
$receipt=@(& (Join-Path $OutputRoot 'contract.exe'))
if($LASTEXITCODE -or $receipt.Count -ne 1 -or $receipt[0] -notmatch '^GPU_DEDICATED_MEMORY_SELFTEST_OK cases=(\d+) hardware=none registry=fake$'){throw 'contract receipt failed'}
$cases=[int]$matches[1];$receipt|Tee-Object -FilePath (Join-Path $OutputRoot 'contract-result.txt')
Compile ($common+@('/W4','/WX','/O2','/DNDEBUG',"/I$(Join-Path $root 'deps\json')",
    (Join-Path $root 'tools\gpu_dedicated_memory_channel_selftest.cpp'),"/Fo$OutputRoot\channel.obj","/Fe$OutputRoot\channel.exe",
    '/link','gdi32.lib','ole32.lib','advapi32.lib')) 'channel-compile.txt'
$channelReceipt=@(& (Join-Path $OutputRoot 'channel.exe'))
if($LASTEXITCODE -or $channelReceipt.Count -ne 1 -or $channelReceipt[0] -notmatch '^GPU_DEDICATED_MEMORY_CHANNEL_SELFTEST_OK cases=(\d+) hardware=fake registry=fake$'){throw 'channel receipt failed'}
$channelCases=[int]$matches[1];$channelReceipt|Tee-Object -FilePath (Join-Path $OutputRoot 'channel-result.txt')
Compile ($common+@('/W4','/WX','/O2','/DNDEBUG',"/I$(Join-Path $root 'deps\json')",
    (Join-Path $root 'tools\gpu_dedicated_memory_binding_selftest.cpp'),"/Fo$OutputRoot\binding.obj","/Fe$OutputRoot\binding.exe",
    '/link','gdi32.lib','ole32.lib','advapi32.lib')) 'binding-compile.txt'
$bindingReceipt=@(& (Join-Path $OutputRoot 'binding.exe'))
if($LASTEXITCODE -or $bindingReceipt.Count -ne 1 -or $bindingReceipt[0] -notmatch '^GPU_DEDICATED_MEMORY_BINDING_SELFTEST_OK cases=(\d+) hardware=fake registry=fake$'){throw 'binding receipt failed'}
$bindingCases=[int]$matches[1];$bindingReceipt|Tee-Object -FilePath (Join-Path $OutputRoot 'binding-result.txt')
Compile ($common+@('/W4','/WX','/O2','/DNDEBUG',"/I$(Join-Path $root 'deps\json')",
    (Join-Path $root 'tools\gpu_dedicated_memory_windows_selftest.cpp'),"/Fo$OutputRoot\windows.obj","/Fe$OutputRoot\windows.exe",
    '/link','gdi32.lib','ole32.lib','advapi32.lib')) 'windows-compile.txt'
$windowsReceipt=@(& (Join-Path $OutputRoot 'windows.exe') $OutputRoot (Join-Path $root 'tools\fixtures\gpu-dedicated-memory-g3e-20261014.json'))
if($LASTEXITCODE -or $windowsReceipt.Count -ne 1 -or $windowsReceipt[0] -notmatch '^GPU_DEDICATED_MEMORY_WINDOWS_SELFTEST_OK cases=(\d+) hardware=fake registry=fake$'){throw 'Windows transport receipt failed'}
$windowsCases=[int]$matches[1];$windowsReceipt|Tee-Object -FilePath (Join-Path $OutputRoot 'windows-receipt.txt')
Compile ($common+@('/W4','/WX','/O2','/DNDEBUG',"/I$(Join-Path $root 'deps\json')",
    (Join-Path $root 'tools\gpu_dedicated_memory_probe.cpp'),"/Fo$OutputRoot\probe.obj","/Fe$OutputRoot\YMCC-G3E-Memory-ReadOnly.exe",
    '/link','gdi32.lib','ole32.lib','advapi32.lib')) 'probe-compile.txt'
$smoke=Join-Path $OutputRoot 'runtime-smoke.cpp'
$header=(Join-Path $root 'native\gpu_dedicated_memory_runtime.h').Replace('\','/')
('#include "'+$header+'"')|Set-Content -LiteralPath $smoke -Encoding utf8
Compile ($common+@('/W4','/WX','/c',"/I$(Join-Path $root 'deps\json')",$smoke,"/Fo$OutputRoot\runtime-smoke.obj")) 'runtime-compile.txt'
Push-Location -LiteralPath $root
try {
    $typeOutput=@(& node 'node_modules/vue-tsc/bin/vue-tsc.js' --noEmit 2>&1);$typeExit=$LASTEXITCODE
    $typeOutput|Set-Content -LiteralPath (Join-Path $OutputRoot 'type-check.txt') -Encoding utf8
    if($typeExit){$typeOutput|Write-Output;throw 'type-check failed'}
    $env:YMCC_GPU_MEMORY_TEST_OUT=Join-Path $OutputRoot 'browser'
    $env:YMCC_GPU_MEMORY_WINDOWS_RESULT=Join-Path $OutputRoot 'windows-result.json'
    & node 'tools/gpu_dedicated_memory_browser_selftest.mjs'
    if($LASTEXITCODE){throw 'browser regression failed'}
    $auditRaw=@(& node 'tools/steam-integration-audit.mjs' --json);$auditExit=$LASTEXITCODE
    $auditRaw|Set-Content -LiteralPath (Join-Path $OutputRoot 'steam-inventory.json') -Encoding utf8
    $audit=($auditRaw -join "`n")|ConvertFrom-Json
    if($auditExit -or $audit.missingAnchors.Count -or $audit.unmappedCandidates.Count){throw 'Steam inventory changed unexpectedly'}
} finally {Pop-Location}
$nativeSnapshotHashes=[ordered]@{}
$nativeGeneratedSourceHashes=[ordered]@{}
$nativeSnapshot=$null
if($CompileNative) {
    # Compile a recorded source snapshot so parallel main.cpp edits cannot change the compiler input.
    # This is a private compile-only artifact; the working source and product are never replaced.
    $nativeSnapshot=Join-Path $OutputRoot 'native-source'
    $nativeInputs=@(& rg --files native -g '*.h' -g '*.cpp' -g '*.rc' -g '*.ico' | Where-Object { $_ -notmatch '(^|[\\/])(?:build|out|tmp|temp)([\\/]|$)' })
    if($LASTEXITCODE){throw 'native source inventory failed'}
    $nativeInputs+= 'deps\json\json.hpp'
    # The battery owner generates a C++ contract from C# source. Capture those
    # inputs and run its existing generator only inside this private snapshot.
    if(Test-Path -LiteralPath (Join-Path $root 'tools/battery-charge-contract.mjs')) {
        $nativeInputs+= @(& rg --files native/BatteryHost -g '*.cs' -g '*.csproj' -g '*.json' | Where-Object { $_ -notmatch '(^|[\/])(?:bin|obj)([\/]|$)' })
        $nativeInputs+= 'tools\battery-charge-contract.mjs'
    }
    $nativeInputs=@($nativeInputs | Select-Object -Unique)
    foreach($relative in $nativeInputs) {
        $inputPath=Join-Path $root $relative
        $snapshotPath=Join-Path $nativeSnapshot $relative
        $inputHash=(Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $snapshotPath)|Out-Null
        Copy-Item -LiteralPath $inputPath -Destination $snapshotPath -Force
        if($inputHash -ne (Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash -or
           $inputHash -ne (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash){throw "Native source changed during snapshot: $relative"}
        $nativeSnapshotHashes[$relative]=$inputHash
    }
    foreach($relative in $nativeInputs) {
        if($nativeSnapshotHashes[$relative] -ne (Get-FileHash -LiteralPath (Join-Path $root $relative) -Algorithm SHA256).Hash){throw "Native source changed while capturing snapshot: $relative"}
    }
    foreach($relative in @('native\gpu_dedicated_memory_contract.h','native\gpu_dedicated_memory_runtime.h')) {
        if($hashes[$relative] -ne $nativeSnapshotHashes[$relative]){throw "Bound memory source changed: $relative"}
    }
    if(Test-Path -LiteralPath (Join-Path $nativeSnapshot 'tools/battery-charge-contract.mjs')) {
        & node (Join-Path $nativeSnapshot 'tools/battery-charge-contract.mjs')
        if($LASTEXITCODE){throw 'Snapshot battery contract generation failed'}
        foreach($generated in @('native\battery_host_contract.h','native\BatteryHost\runtime-contract.json')) {
            $nativeGeneratedSourceHashes[$generated]=(Get-FileHash -LiteralPath (Join-Path $nativeSnapshot $generated) -Algorithm SHA256).Hash
            if($nativeSnapshotHashes.Contains($generated) -and $nativeSnapshotHashes[$generated] -ne $nativeGeneratedSourceHashes[$generated]){throw "Existing battery contract stale in snapshot: $generated"}
        }
    }
    $hashes['native\main.cpp']=$nativeSnapshotHashes['native\main.cpp']
    $native=Join-Path $nativeSnapshot 'native'
    $includes=@("/I$(Join-Path $root 'deps\webview2\build\native\include')","/I$(Join-Path $nativeSnapshot 'deps\json')",
        '/IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt')
    Push-Location -LiteralPath $native
    try {
        Compile ($common+@('/O2','/bigobj','/DNDEBUG','/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1','/DYMCC_MSI_VISIBILITY_DIAGNOSTIC',
            '/DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC','/c','main.cpp',"/Fo$OutputRoot\main.obj")+$includes) 'main-compile.txt'
        Compile ($common+@('/O2','/DNDEBUG','/c','dsu_server.cpp',"/Fo$OutputRoot\dsu_server.obj")) 'dsu-compile.txt'
        & rc.exe /nologo /fo (Join-Path $OutputRoot 'app.res') 'app.rc'
        if($LASTEXITCODE){throw 'resource compile failed'}
        Compile @('/nologo','/MT',"$OutputRoot\main.obj","$OutputRoot\dsu_server.obj","$OutputRoot\app.res","/Fe$OutputRoot\YeManCC-compile-only.exe",
            '/link','/SUBSYSTEM:WINDOWS','/MACHINE:x64',(Join-Path $root 'deps\webview2\build\native\x64\WebView2LoaderStatic.lib'),
            'C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64\WindowsApp.lib',
            'user32.lib','gdi32.lib','shell32.lib','shlwapi.lib','ole32.lib','oleaut32.lib','dwmapi.lib','winhttp.lib','advapi32.lib',
            'shcore.lib','version.lib','psapi.lib','PowrProf.lib','ws2_32.lib') 'native-link.txt'
    } finally {Pop-Location}
    . (Join-Path $root 'tools/gpu-dedicated-memory-export-guard.ps1')
    $gpuExportReceipt=Assert-GpuDedicatedMemoryNativeExport $nativeSnapshot (Join-Path $OutputRoot 'YeManCC-compile-only.exe')
    $gpuExportReceipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputRoot 'gpu-export-guard.json') -Encoding utf8
}
$sourceDrift=@()
foreach($p in $paths){
    if($hashes[$p] -ne (Get-FileHash -LiteralPath (Join-Path $root $p) -Algorithm SHA256).Hash){
        if($CompileNative -and $p -eq 'native\main.cpp'){$sourceDrift+=$p}
        else{throw "Source changed during validation: $p"}
    }
}
foreach($relative in $nativeSnapshotHashes.Keys){
    if($nativeSnapshotHashes[$relative] -ne (Get-FileHash -LiteralPath (Join-Path $nativeSnapshot $relative) -Algorithm SHA256).Hash){throw "Compiled snapshot changed: $relative"}
}
foreach($relative in $nativeGeneratedSourceHashes.Keys){
    if($nativeGeneratedSourceHashes[$relative] -ne (Get-FileHash -LiteralPath (Join-Path $nativeSnapshot $relative) -Algorithm SHA256).Hash){throw "Generated snapshot input changed: $relative"}
}
$browserResult=Get-Content -LiteralPath (Join-Path $OutputRoot 'browser\result.json') -Raw|ConvertFrom-Json
[pscustomobject]@{schema=1;completedAt=(Get-Date -Format o);contractCases=$cases;channelCases=$channelCases;bindingCases=$bindingCases;windowsCases=$windowsCases;browserCases=$browserResult.cases.Count;
    typeCheck=$true;runtimeHeaderCompiled=$true;nativeCompiledAndLinked=[bool]$CompileNative;executableRun=$false;
    hardwareAccess=$false;registryAccess=$false;sharedMemoryControl=$false;sourceHashes=$hashes;sourceDriftAtCompletion=$sourceDrift;nativeSourceSnapshot=$nativeSnapshot;nativeSourceHashes=$nativeSnapshotHashes;nativeGeneratedSourceHashes=$nativeGeneratedSourceHashes}|
    ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputRoot 'verification.json') -Encoding utf8
Write-Output 'GPU_DEDICATED_MEMORY_VERIFICATION_OK hardware=none registry=none'
