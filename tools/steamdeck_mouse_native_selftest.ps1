param([string]$OutputDirectory='G:\YeManCC-Work\_scratch\steamdeck-mouse-20261005\native-selftest')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$main=[IO.File]::ReadAllText((Join-Path $root 'native\main.cpp'))
$start=$main.IndexOf('// SteamDeck desktop right-stick sensitivity.')
$end=$main.IndexOf('static bool sofApplyValueToConfigText(const std::string& in, int target, std::string* out) {',$start)
if($start -lt 0 -or $end -lt $start){throw 'Production SteamDeck mouse runtime block not found'}
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'steamdeck_mouse_native_fixture.cpp')).Replace('//__PRODUCTION_RUNTIME__',$main.Substring($start,$end-$start))
$overlayStart=$main.IndexOf('static std::atomic<bool> g_sofQueued{false};')
$overlayEnd=$main.IndexOf('// SteamDeck desktop right-stick sensitivity.',$overlayStart)
if($overlayStart -lt 0 -or $overlayEnd -lt $overlayStart){throw 'Production Steam overlay runtime block not found'}
$fixture=$fixture.Replace('//__PRODUCTION_OVERLAY_RUNTIME__',$main.Substring($overlayStart,$overlayEnd-$overlayStart))
$liveStart=$main.IndexOf('static json steamLiveRequest(DWORD account, json request) {')
$liveEnd=$main.IndexOf('static std::atomic<bool> g_sofQueued{false};',$liveStart)
if($liveStart -lt 0 -or $liveEnd -lt $liveStart){throw 'Production Steam live preflight block not found'}
$fixture=$fixture.Replace('//__PRODUCTION_LIVE_PREFLIGHT__',$main.Substring($liveStart,$liveEnd-$liveStart))
$transport=[IO.File]::ReadAllText((Join-Path $root 'native\steam_live_cdp.h'))
$policyStart=$transport.IndexOf('inline bool canDefer(')
$policyEnd=$transport.LastIndexOf('}')
if($policyStart -lt 0 -or $policyEnd -lt $policyStart){throw 'Production Steam live deferral policy not found'}
$fixture=$fixture.Replace('//__PRODUCTION_DEFER_POLICY__',$transport.Substring($policyStart,$policyEnd-$policyStart))
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source=Join-Path $OutputDirectory 'native-selftest.cpp'
[IO.File]::WriteAllText($source,$fixture,[Text.UTF8Encoding]::new($false))
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" /I"$root\native" "$source" /Fo"$OutputDirectory\native-selftest.obj" /Fe"$OutputDirectory\native-selftest.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\native-selftest.exe" "$root\tools\fixtures\steamdeck-mouse\configset_controller_neptune.vdf" "$root\tools\fixtures\steamdeck-mouse\controller_neptune-100.vdf" "$root\tools\fixtures\steamdeck-mouse\controller_neptune-137.vdf"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "SteamDeck native selftest failed: $LASTEXITCODE"}
