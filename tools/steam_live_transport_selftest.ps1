param(
 [string]$OutputDirectory='G:\YeManCC-Work\_scratch\steamdeck-live-20261005\transport-test',
 [switch]$LiveReadOnly,
 [switch]$LiveRunningReadOnly,
 [switch]$LiveAppLifetimeReadOnly,
 [switch]$LiveApiSurfaceReadOnly,
 [switch]$LiveApiContractReadOnly,
 [switch]$LiveOverlayRoundTrip,
 [string]$SteamPath='',
 [uint32]$Account=0,
 [uint32]$AppId=0,
 [string]$ControllerPath='',
 [string]$ControllerType='controller_generic'
)
$ErrorActionPreference='Stop'
if(@($LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count -gt 1){throw 'Choose one live operation'}
if(($LiveReadOnly -or $LiveRunningReadOnly -or $LiveApiSurfaceReadOnly -or $LiveApiContractReadOnly -or $LiveOverlayRoundTrip) -and (!$SteamPath -or !$Account)){throw 'Explicit SteamPath and Account are required for live access'}
if($LiveReadOnly -and !$ControllerPath){throw 'ControllerPath is required for live read'}
$root=Split-Path $PSScriptRoot
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" /I"$root\native" "$root\tools\steam_live_transport_fixture.cpp" /Fo"$OutputDirectory\fixture.obj" /Fe"$OutputDirectory\fixture.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw 'Transport fixture compile failed'}
$exe=Join-Path $OutputDirectory 'fixture.exe'
if($LiveReadOnly){ & $exe --live-readonly $SteamPath $Account $ControllerPath $ControllerType }
elseif($LiveRunningReadOnly){ if($AppId){ & $exe --live-running-read $SteamPath $Account $AppId } else { & $exe --live-running-read $SteamPath $Account } }
elseif($LiveAppLifetimeReadOnly){ if($AppId){ & $exe --live-app-lifetime-read $SteamPath $Account $AppId } else { & $exe --live-app-lifetime-read $SteamPath $Account } }
elseif($LiveApiSurfaceReadOnly){ & $exe --live-api-surface-read $SteamPath $Account }
elseif($LiveApiContractReadOnly){ & $exe --live-api-contract-read $SteamPath $Account }
elseif($LiveOverlayRoundTrip){ & $exe --live-overlay-roundtrip $SteamPath $Account }
else{ & $exe }
if($LASTEXITCODE -ne 0){throw "Transport test failed: $LASTEXITCODE"}
