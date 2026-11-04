param(
 [string]$OutputDirectory='G:\YeManCC-Work\_scratch\steamdeck-live-20261005\transport-test',
 [switch]$ListenerInventoryOnly,
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
if($ListenerInventoryOnly -and @($LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Listener inventory selection cannot run live operations'}
$root=Split-Path $PSScriptRoot
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM BuildTools may omit Windows SDK registry discovery; use the installed SDK explicitly.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" /I"$root\native" "$root\tools\steam_live_transport_fixture.cpp" /Fo"$OutputDirectory\fixture.obj" /Fe"$OutputDirectory\fixture.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw 'Transport fixture compile failed'}
$exe=Join-Path $OutputDirectory 'fixture.exe'
if($ListenerInventoryOnly){ & $exe --listener-inventory-only }
elseif($LiveReadOnly){ & $exe --live-readonly $SteamPath $Account $ControllerPath $ControllerType }
elseif($LiveRunningReadOnly){ if($AppId){ & $exe --live-running-read $SteamPath $Account $AppId } else { & $exe --live-running-read $SteamPath $Account } }
elseif($LiveAppLifetimeReadOnly){ if($AppId){ & $exe --live-app-lifetime-read $SteamPath $Account $AppId } else { & $exe --live-app-lifetime-read $SteamPath $Account } }
elseif($LiveApiSurfaceReadOnly){ & $exe --live-api-surface-read $SteamPath $Account }
elseif($LiveApiContractReadOnly){ & $exe --live-api-contract-read $SteamPath $Account }
elseif($LiveOverlayRoundTrip){ & $exe --live-overlay-roundtrip $SteamPath $Account }
else{ & $exe }
if($LASTEXITCODE -ne 0){throw "Transport test failed: $LASTEXITCODE"}
