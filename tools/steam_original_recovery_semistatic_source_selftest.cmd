@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Optional first argument selects the output/evidence directory.
rem Build and run only a sparse-fixture selftest; never attaches to Steam.
set "OUT=%~1"
if not defined OUT set "OUT=%TEMP%\ymcc-semistatic-source-selftest"
for %%I in ("%OUT%") do set "OUT=%%~fI"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%" exit /b 1
where cl.exe >nul 2>nul
if not errorlevel 1 goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: cl.exe and vswhere.exe not found.
    exit /b 1
)
set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
    echo ERROR: Visual C++ build tools not found.
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
:build
rem Some standalone Build Tools installs lack Windows SDK registry discovery.
rem Reuse installed headers/libraries; do not install or alter machine settings.
if defined UniversalCRTSdkDir goto compile
set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVERSION="
for /f "delims=" %%V in ('dir /b /ad /on "%SDKROOT%\Include\10.*" 2^>nul') do if exist "%SDKROOT%\Include\%%V\ucrt\crtdbg.h" if exist "%SDKROOT%\Lib\%%V\ucrt\x64\libucrt.lib" set "SDKVERSION=%%V"
if not defined SDKVERSION (
    echo ERROR: Installed Windows SDK UCRT headers/libraries not found.
    exit /b 1
)
set "INCLUDE=%INCLUDE%;%SDKROOT%\Include\%SDKVERSION%\ucrt;%SDKROOT%\Include\%SDKVERSION%\shared;%SDKROOT%\Include\%SDKVERSION%\um"
set "LIB=%LIB%;%SDKROOT%\Lib\%SDKVERSION%\ucrt\x64;%SDKROOT%\Lib\%SDKVERSION%\um\x64"
:compile
cl.exe /nologo /std:c++17 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /Fo"%OUT%\steam_original_recovery_semistatic_source_selftest.obj" /Fe"%OUT%\steam_original_recovery_semistatic_source_selftest.exe" "%~dp0steam_original_recovery_semistatic_source_selftest.cpp"
if errorlevel 1 exit /b 1
"%OUT%\steam_original_recovery_semistatic_source_selftest.exe"
exit /b %errorlevel%
