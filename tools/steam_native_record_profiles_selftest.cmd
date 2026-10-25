@echo off
setlocal EnableExtensions DisableDelayedExpansion
if "%~3"=="" exit /b 2
set "OUT=%~f1"
set "LEGACY=%~f2"
set "PUBLIC=%~f3"
if not exist "%OUT%" mkdir "%OUT%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if defined UniversalCRTSdkDir goto compile
set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVERSION="
for /f "delims=" %%V in ('dir /b /ad /on "%SDKROOT%\Include\10.*" 2^>nul') do if exist "%SDKROOT%\Include\%%V\ucrt\crtdbg.h" if exist "%SDKROOT%\Lib\%%V\ucrt\x64\libucrt.lib" set "SDKVERSION=%%V"
if not defined SDKVERSION exit /b 1
set "INCLUDE=%INCLUDE%;%SDKROOT%\Include\%SDKVERSION%\ucrt;%SDKROOT%\Include\%SDKVERSION%\shared;%SDKROOT%\Include\%SDKVERSION%\um"
set "LIB=%LIB%;%SDKROOT%\Lib\%SDKVERSION%\ucrt\x64;%SDKROOT%\Lib\%SDKVERSION%\um\x64"
:compile
for %%V in (root vendor) do (
    call :one %%V
    if errorlevel 1 exit /b 1
)
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /I"%~dp0..\native\steam_original_recovery_engine\vendor" /Fo"%OUT%\legacy-regression.obj" /Fe"%OUT%\legacy-regression.exe" "%~dp0steam_native_record_diagnostic_selftest.cpp" > "%OUT%\legacy-regression-build.log" 2>&1
if errorlevel 1 exit /b 1
"%OUT%\legacy-regression.exe" > "%OUT%\legacy-regression.json"
exit /b %errorlevel%
:one
set "DEFINE="
if "%1"=="vendor" set "DEFINE=/DYMCC_VENDOR_PROFILE"
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /I"%~dp0..\native\steam_original_recovery_engine\vendor" %DEFINE% /Fo"%OUT%\%1.obj" /Fe"%OUT%\%1.exe" "%~dp0steam_native_record_profiles_selftest.cpp" user32.lib > "%OUT%\%1-build.log" 2>&1
if errorlevel 1 exit /b 1
"%OUT%\%1.exe" "%LEGACY%" "%PUBLIC%" "%OUT%\%1-cache-fixture.bin" > "%OUT%\%1-result.json" 2> "%OUT%\%1-stderr.log"
exit /b %errorlevel%

